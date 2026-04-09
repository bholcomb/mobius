#include <mobius/mobius_plugin.h>

#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <cinttypes>
#include <string>
#include <vector>
#include <algorithm>
#include <climits>
#include <cerrno>
#include <fstream>
#include <sstream>

static constexpr int MAX_DEPTH = 128;

// ============================================================================
// TOML PARSER
// ============================================================================

struct TomlParser {
    MobiusState* state;
    const char*  src;
    size_t       pos;
    size_t       len;
    int          line;
    char         error[512];

    // The root table is always at stack index root_idx
    int root_idx;

    char peek() { return pos < len ? src[pos] : '\0'; }
    char advance() {
        if (pos < len) {
            char c = src[pos++];
            if (c == '\n') line++;
            return c;
        }
        return '\0';
    }

    void skip_whitespace_inline() {
        while (pos < len && (src[pos] == ' ' || src[pos] == '\t')) pos++;
    }

    void skip_whitespace_and_newlines() {
        while (pos < len) {
            char c = src[pos];
            if (c == ' ' || c == '\t' || c == '\r') { pos++; continue; }
            if (c == '\n') { pos++; line++; continue; }
            if (c == '#') { skip_comment(); continue; }
            break;
        }
    }

    void skip_comment() {
        while (pos < len && src[pos] != '\n') pos++;
    }

    void skip_to_newline() {
        skip_whitespace_inline();
        if (pos < len && src[pos] == '#') skip_comment();
        if (pos < len && src[pos] == '\r') pos++;
        if (pos < len && src[pos] == '\n') { pos++; line++; }
    }

    bool is_bare_key_char(char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '-' || c == '_';
    }

    // -----------------------------------------------------------------------
    // Key parsing
    // -----------------------------------------------------------------------

    bool parse_bare_key(std::string& out) {
        size_t start = pos;
        while (pos < len && is_bare_key_char(src[pos])) pos++;
        if (pos == start) {
            snprintf(error, sizeof(error), "line %d: expected key", line);
            return false;
        }
        out.assign(src + start, pos - start);
        return true;
    }

    bool parse_basic_string(std::string& out) {
        pos++; // skip opening "
        out.clear();
        while (pos < len) {
            char c = src[pos++];
            if (c == '"') return true;
            if (c == '\n') { line++; }
            if (c == '\\') {
                if (pos >= len) break;
                char esc = src[pos++];
                switch (esc) {
                    case '"':  out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case 'b':  out.push_back('\b'); break;
                    case 'f':  out.push_back('\f'); break;
                    case 'n':  out.push_back('\n'); break;
                    case 'r':  out.push_back('\r'); break;
                    case 't':  out.push_back('\t'); break;
                    case 'u': case 'U': {
                        int digits = (esc == 'u') ? 4 : 8;
                        int cp = 0;
                        for (int i = 0; i < digits; i++) {
                            if (pos >= len) {
                                snprintf(error, sizeof(error), "line %d: incomplete unicode escape", line);
                                return false;
                            }
                            char h = src[pos++];
                            cp <<= 4;
                            if (h >= '0' && h <= '9') cp |= (h - '0');
                            else if (h >= 'a' && h <= 'f') cp |= (h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') cp |= (h - 'A' + 10);
                            else {
                                snprintf(error, sizeof(error), "line %d: invalid hex in unicode escape", line);
                                return false;
                            }
                        }
                        encode_utf8(out, cp);
                        break;
                    }
                    default:
                        snprintf(error, sizeof(error), "line %d: invalid escape '\\%c'", line, esc);
                        return false;
                }
            } else {
                out.push_back(c);
            }
        }
        snprintf(error, sizeof(error), "line %d: unterminated string", line);
        return false;
    }

    bool parse_literal_string(std::string& out) {
        pos++; // skip opening '
        out.clear();
        while (pos < len) {
            char c = src[pos++];
            if (c == '\'') return true;
            if (c == '\n') line++;
            out.push_back(c);
        }
        snprintf(error, sizeof(error), "line %d: unterminated literal string", line);
        return false;
    }

    bool parse_multiline_basic_string(std::string& out) {
        pos += 3; // skip """
        out.clear();
        // Skip first newline immediately after """
        if (pos < len && src[pos] == '\n') { pos++; line++; }
        else if (pos + 1 < len && src[pos] == '\r' && src[pos + 1] == '\n') { pos += 2; line++; }

        while (pos < len) {
            if (pos + 2 < len && src[pos] == '"' && src[pos + 1] == '"' && src[pos + 2] == '"') {
                pos += 3;
                return true;
            }
            char c = src[pos++];
            if (c == '\n') line++;
            if (c == '\\') {
                if (pos >= len) break;
                char esc = src[pos];
                if (esc == '\n' || esc == '\r' || esc == ' ' || esc == '\t') {
                    // line-ending backslash: trim whitespace and newlines
                    while (pos < len && (src[pos] == ' ' || src[pos] == '\t' ||
                           src[pos] == '\n' || src[pos] == '\r')) {
                        if (src[pos] == '\n') line++;
                        pos++;
                    }
                    continue;
                }
                pos++;
                switch (esc) {
                    case '"':  out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case 'b':  out.push_back('\b'); break;
                    case 'f':  out.push_back('\f'); break;
                    case 'n':  out.push_back('\n'); break;
                    case 'r':  out.push_back('\r'); break;
                    case 't':  out.push_back('\t'); break;
                    case 'u': case 'U': {
                        int digits = (esc == 'u') ? 4 : 8;
                        int cp = 0;
                        for (int i = 0; i < digits; i++) {
                            if (pos >= len) {
                                snprintf(error, sizeof(error), "line %d: incomplete unicode escape", line);
                                return false;
                            }
                            char h = src[pos++];
                            cp <<= 4;
                            if (h >= '0' && h <= '9') cp |= (h - '0');
                            else if (h >= 'a' && h <= 'f') cp |= (h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') cp |= (h - 'A' + 10);
                            else {
                                snprintf(error, sizeof(error), "line %d: invalid hex in unicode escape", line);
                                return false;
                            }
                        }
                        encode_utf8(out, cp);
                        break;
                    }
                    default:
                        snprintf(error, sizeof(error), "line %d: invalid escape '\\%c'", line, esc);
                        return false;
                }
            } else {
                out.push_back(c);
            }
        }
        snprintf(error, sizeof(error), "line %d: unterminated multiline string", line);
        return false;
    }

    bool parse_multiline_literal_string(std::string& out) {
        pos += 3; // skip '''
        out.clear();
        if (pos < len && src[pos] == '\n') { pos++; line++; }
        else if (pos + 1 < len && src[pos] == '\r' && src[pos + 1] == '\n') { pos += 2; line++; }

        while (pos < len) {
            if (pos + 2 < len && src[pos] == '\'' && src[pos + 1] == '\'' && src[pos + 2] == '\'') {
                pos += 3;
                return true;
            }
            char c = src[pos++];
            if (c == '\n') line++;
            out.push_back(c);
        }
        snprintf(error, sizeof(error), "line %d: unterminated multiline literal string", line);
        return false;
    }

    bool parse_key_part(std::string& out) {
        skip_whitespace_inline();
        if (peek() == '"') return parse_basic_string(out);
        if (peek() == '\'') return parse_literal_string(out);
        return parse_bare_key(out);
    }

    // Parse a dotted key like a.b."c d" into parts
    bool parse_key(std::vector<std::string>& parts) {
        parts.clear();
        std::string part;
        if (!parse_key_part(part)) return false;
        parts.push_back(std::move(part));

        while (pos < len) {
            skip_whitespace_inline();
            if (peek() != '.') break;
            pos++; // skip '.'
            if (!parse_key_part(part)) return false;
            parts.push_back(std::move(part));
        }
        return true;
    }

    static void encode_utf8(std::string& out, int cp) {
        if (cp < 0x80) {
            out.push_back((char)cp);
        } else if (cp < 0x800) {
            out.push_back((char)(0xC0 | (cp >> 6)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back((char)(0xE0 | (cp >> 12)));
            out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        } else {
            out.push_back((char)(0xF0 | (cp >> 18)));
            out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        }
    }

    // -----------------------------------------------------------------------
    // Value parsing
    // -----------------------------------------------------------------------

    bool parse_string_val() {
        if (pos + 2 < len && src[pos] == '"' && src[pos + 1] == '"' && src[pos + 2] == '"') {
            std::string s;
            if (!parse_multiline_basic_string(s)) return false;
            mobius_stack_pushString(state, s.c_str());
            return true;
        }
        if (pos + 2 < len && src[pos] == '\'' && src[pos + 1] == '\'' && src[pos + 2] == '\'') {
            std::string s;
            if (!parse_multiline_literal_string(s)) return false;
            mobius_stack_pushString(state, s.c_str());
            return true;
        }
        if (peek() == '"') {
            std::string s;
            if (!parse_basic_string(s)) return false;
            mobius_stack_pushString(state, s.c_str());
            return true;
        }
        if (peek() == '\'') {
            std::string s;
            if (!parse_literal_string(s)) return false;
            mobius_stack_pushString(state, s.c_str());
            return true;
        }
        snprintf(error, sizeof(error), "line %d: expected string", line);
        return false;
    }

    // Try to parse a datetime/date/time string starting at `start` position.
    // Returns true if it looks like a datetime and pushes a table onto the stack.
    bool try_parse_datetime(size_t start) {
        // Scan forward to find the extent of the datetime token
        size_t end = start;
        while (end < len && src[end] != ',' && src[end] != ']' && src[end] != '}'
               && src[end] != '#' && src[end] != '\n' && src[end] != '\r') {
            end++;
        }
        // Trim trailing whitespace
        while (end > start && (src[end - 1] == ' ' || src[end - 1] == '\t')) end--;

        std::string tok(src + start, end - start);

        int year = 0, month = 0, day = 0, hour = -1, minute = -1, sec = -1;
        int frac_digits = 0;
        double frac = 0.0;
        std::string tz;
        bool has_date = false, has_time = false;

        const char* p = tok.c_str();

        // Try date: YYYY-MM-DD
        if (tok.size() >= 10 && p[4] == '-' && p[7] == '-') {
            if (sscanf(p, "%4d-%2d-%2d", &year, &month, &day) == 3) {
                has_date = true;
                p += 10;
                if (*p == 'T' || *p == 't' || *p == ' ') p++;
            }
        }

        // Try time: HH:MM:SS[.frac][Z|+HH:MM|-HH:MM]
        if (*p && p[2] == ':') {
            if (sscanf(p, "%2d:%2d:%2d", &hour, &minute, &sec) == 3) {
                has_time = true;
                p += 8;
                if (*p == '.') {
                    p++;
                    const char* frac_start = p;
                    while (*p >= '0' && *p <= '9') p++;
                    frac_digits = (int)(p - frac_start);
                    if (frac_digits > 0) {
                        std::string frac_str(frac_start, (size_t)frac_digits);
                        frac = strtod(("0." + frac_str).c_str(), nullptr);
                    }
                }
                // Timezone
                if (*p == 'Z' || *p == 'z') {
                    tz = "Z";
                } else if (*p == '+' || *p == '-') {
                    tz = std::string(p);
                }
            }
        }

        if (!has_date && !has_time) return false;

        pos = end;

        mobius_stack_pushNewTable(state, 8);
        int tbl = mobius_stack_size(state) - 1;

        if (has_date) {
            mobius_stack_pushInt64(state, year);
            mobius_stack_setTableField(state, tbl, "year");
            mobius_stack_pushInt64(state, month);
            mobius_stack_setTableField(state, tbl, "month");
            mobius_stack_pushInt64(state, day);
            mobius_stack_setTableField(state, tbl, "day");
        }
        if (has_time) {
            mobius_stack_pushInt64(state, hour);
            mobius_stack_setTableField(state, tbl, "hour");
            mobius_stack_pushInt64(state, minute);
            mobius_stack_setTableField(state, tbl, "min");
            mobius_stack_pushInt64(state, sec);
            mobius_stack_setTableField(state, tbl, "sec");
            if (frac_digits > 0) {
                mobius_stack_pushFloat64(state, frac);
                mobius_stack_setTableField(state, tbl, "frac");
            }
        }
        if (!tz.empty()) {
            mobius_stack_pushString(state, tz.c_str());
            mobius_stack_setTableField(state, tbl, "tz");
        }

        // Type tag so stringify can distinguish
        if (has_date && has_time) {
            mobius_stack_pushString(state, "datetime");
        } else if (has_date) {
            mobius_stack_pushString(state, "date");
        } else {
            mobius_stack_pushString(state, "time");
        }
        mobius_stack_setTableField(state, tbl, "__toml_type");

        return true;
    }

    bool parse_number_or_date() {
        size_t start = pos;

        // Check for special float values
        if (pos + 3 <= len) {
            if (memcmp(src + pos, "inf", 3) == 0 || memcmp(src + pos, "nan", 3) == 0) {
                bool is_inf = (src[pos] == 'i');
                pos += 3;
                if (is_inf) mobius_stack_pushFloat64(state, HUGE_VAL);
                else mobius_stack_pushFloat64(state, NAN);
                return true;
            }
        }
        if (pos + 4 <= len) {
            if (memcmp(src + pos, "+inf", 4) == 0) {
                pos += 4;
                mobius_stack_pushFloat64(state, HUGE_VAL);
                return true;
            }
            if (memcmp(src + pos, "-inf", 4) == 0) {
                pos += 4;
                mobius_stack_pushFloat64(state, -HUGE_VAL);
                return true;
            }
            if (memcmp(src + pos, "+nan", 4) == 0 || memcmp(src + pos, "-nan", 4) == 0) {
                pos += 4;
                mobius_stack_pushFloat64(state, NAN);
                return true;
            }
        }

        // Check for hex, octal, binary integer prefixes
        if (peek() == '0' && pos + 1 < len) {
            char next = src[pos + 1];
            if (next == 'x' || next == 'X') {
                pos += 2;
                size_t s = pos;
                while (pos < len && (is_hex_digit(src[pos]) || src[pos] == '_')) pos++;
                std::string num;
                for (size_t i = s; i < pos; i++) if (src[i] != '_') num.push_back(src[i]);
                mobius_stack_pushInt64(state, (int64_t)strtoull(num.c_str(), nullptr, 16));
                return true;
            }
            if (next == 'o' || next == 'O') {
                pos += 2;
                size_t s = pos;
                while (pos < len && ((src[pos] >= '0' && src[pos] <= '7') || src[pos] == '_')) pos++;
                std::string num;
                for (size_t i = s; i < pos; i++) if (src[i] != '_') num.push_back(src[i]);
                mobius_stack_pushInt64(state, (int64_t)strtoull(num.c_str(), nullptr, 8));
                return true;
            }
            if (next == 'b' || next == 'B') {
                pos += 2;
                size_t s = pos;
                while (pos < len && (src[pos] == '0' || src[pos] == '1' || src[pos] == '_')) pos++;
                std::string num;
                for (size_t i = s; i < pos; i++) if (src[i] != '_') num.push_back(src[i]);
                mobius_stack_pushInt64(state, (int64_t)strtoull(num.c_str(), nullptr, 2));
                return true;
            }
        }

        // Might be a datetime — check for pattern like YYYY-MM-DD or HH:MM
        // Datetime detection: look ahead for date-like pattern
        if (pos + 4 < len && src[pos] >= '0' && src[pos] <= '9') {
            // Could be YYYY-MM-DD datetime or just a plain number
            // If we see digits followed by '-' at position 4, try datetime
            bool could_be_date = (pos + 10 <= len && src[pos + 4] == '-' && src[pos + 7] == '-');
            // Time: HH:MM
            bool could_be_time = false;
            {
                size_t t = pos;
                while (t < len && src[t] >= '0' && src[t] <= '9') t++;
                if (t == pos + 2 && t < len && src[t] == ':') could_be_time = true;
            }
            if (could_be_date || could_be_time) {
                if (try_parse_datetime(start)) return true;
                pos = start; // reset and fall through to number
            }
        }

        // Regular number (integer or float)
        bool negative = false;
        if (peek() == '+' || peek() == '-') {
            negative = (peek() == '-');
            pos++;
        }

        bool is_float = false;
        size_t num_start = pos;

        while (pos < len && ((src[pos] >= '0' && src[pos] <= '9') || src[pos] == '_')) pos++;

        if (peek() == '.' && pos + 1 < len && src[pos + 1] >= '0' && src[pos + 1] <= '9') {
            is_float = true;
            pos++;
            while (pos < len && ((src[pos] >= '0' && src[pos] <= '9') || src[pos] == '_')) pos++;
        }

        if (peek() == 'e' || peek() == 'E') {
            is_float = true;
            pos++;
            if (peek() == '+' || peek() == '-') pos++;
            while (pos < len && ((src[pos] >= '0' && src[pos] <= '9') || src[pos] == '_')) pos++;
        }

        // Build the number string (stripping underscores)
        std::string num;
        if (negative) num.push_back('-');
        for (size_t i = num_start; i < pos; i++)
            if (src[i] != '_') num.push_back(src[i]);

        if (num.empty() || num == "-") {
            snprintf(error, sizeof(error), "line %d: invalid number", line);
            return false;
        }

        if (is_float) {
            mobius_stack_pushFloat64(state, strtod(num.c_str(), nullptr));
        } else {
            errno = 0;
            long long val = strtoll(num.c_str(), nullptr, 10);
            if (errno == ERANGE) {
                mobius_stack_pushFloat64(state, strtod(num.c_str(), nullptr));
            } else {
                mobius_stack_pushInt64(state, (int64_t)val);
            }
        }
        return true;
    }

    static bool is_hex_digit(char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    }

    bool parse_inline_table(int depth) {
        pos++; // skip '{'
        mobius_stack_pushNewTable(state, 4);
        int tbl = mobius_stack_size(state) - 1;

        skip_whitespace_inline();
        if (peek() == '}') { pos++; return true; }

        for (;;) {
            skip_whitespace_inline();
            std::vector<std::string> key_parts;
            if (!parse_key(key_parts)) return false;

            skip_whitespace_inline();
            if (peek() != '=') {
                snprintf(error, sizeof(error), "line %d: expected '=' after key", line);
                return false;
            }
            pos++;
            skip_whitespace_inline();

            // Navigate/create sub-tables for dotted keys
            int target_tbl = tbl;
            for (size_t i = 0; i + 1 < key_parts.size(); i++) {
                mobius_stack_getTableField(state, target_tbl, key_parts[i].c_str());
                int sub = mobius_stack_size(state) - 1;
                if (mobius_stack_isNil(state, sub)) {
                    mobius_stack_pop(state, 1);
                    mobius_stack_pushNewTable(state, 4);
                    sub = mobius_stack_size(state) - 1;
                    mobius_stack_copy(state, sub);
                    mobius_stack_setTableField(state, target_tbl, key_parts[i].c_str());
                }
                target_tbl = sub;
            }

            if (!parse_value(depth + 1)) return false;
            mobius_stack_setTableField(state, target_tbl, key_parts.back().c_str());

            // Pop any intermediate sub-table references we pushed
            while (mobius_stack_size(state) - 1 > tbl) {
                // Check if TOS is above our table + the value we just set
                // Actually, setTableField already popped the value. So anything
                // above tbl is intermediate tables we navigated into.
                if (mobius_stack_size(state) - 1 > tbl)
                    mobius_stack_pop(state, mobius_stack_size(state) - 1 - tbl);
                break;
            }

            skip_whitespace_inline();
            if (peek() == ',') { pos++; continue; }
            if (peek() == '}') { pos++; return true; }

            snprintf(error, sizeof(error), "line %d: expected ',' or '}' in inline table", line);
            return false;
        }
    }

    bool parse_inline_array(int depth) {
        pos++; // skip '['
        mobius_stack_pushNewArray(state, 4);
        int arr = mobius_stack_size(state) - 1;

        skip_whitespace_and_newlines();
        if (peek() == ']') { pos++; return true; }

        for (;;) {
            skip_whitespace_and_newlines();
            if (peek() == ']') { pos++; return true; }
            if (!parse_value(depth + 1)) return false;
            mobius_stack_arrayPush(state, arr);

            skip_whitespace_and_newlines();
            if (peek() == ',') { pos++; continue; }
            if (peek() == ']') { pos++; return true; }

            snprintf(error, sizeof(error), "line %d: expected ',' or ']' in array", line);
            return false;
        }
    }

    bool parse_value(int depth) {
        if (depth > MAX_DEPTH) {
            snprintf(error, sizeof(error), "line %d: exceeded maximum nesting depth", line);
            return false;
        }

        skip_whitespace_inline();
        char c = peek();

        if (c == '"' || c == '\'') return parse_string_val();
        if (c == '{') return parse_inline_table(depth);
        if (c == '[') return parse_inline_array(depth);

        if (c == 't' && pos + 4 <= len && memcmp(src + pos, "true", 4) == 0) {
            pos += 4;
            mobius_stack_pushBool(state, true);
            return true;
        }
        if (c == 'f' && pos + 5 <= len && memcmp(src + pos, "false", 5) == 0) {
            pos += 5;
            mobius_stack_pushBool(state, false);
            return true;
        }

        if (c == '-' || c == '+' || (c >= '0' && c <= '9') ||
            c == 'i' || c == 'n') {
            return parse_number_or_date();
        }

        snprintf(error, sizeof(error), "line %d: unexpected character '%c'", line, c);
        return false;
    }

    // -----------------------------------------------------------------------
    // Navigate to or create a sub-table path from a given parent table
    // on the stack. Returns the stack index of the deepest table, or -1.
    // -----------------------------------------------------------------------

    int navigate_to_table(int parent_idx, const std::vector<std::string>& parts, bool create_if_missing) {
        int current = parent_idx;

        for (size_t i = 0; i < parts.size(); i++) {
            mobius_stack_getTableField(state, current, parts[i].c_str());
            int sub = mobius_stack_size(state) - 1;

            if (mobius_stack_isNil(state, sub)) {
                mobius_stack_pop(state, 1);
                if (!create_if_missing) {
                    snprintf(error, sizeof(error), "line %d: table '%s' not found", line, parts[i].c_str());
                    return -1;
                }
                mobius_stack_pushNewTable(state, 4);
                sub = mobius_stack_size(state) - 1;
                mobius_stack_copy(state, sub);
                mobius_stack_setTableField(state, current, parts[i].c_str());
            } else if (mobius_stack_isArray(state, sub)) {
                // [[array_of_tables]] — get the last element
                size_t arr_len = mobius_stack_getArrayLength(state, sub);
                if (arr_len == 0) {
                    snprintf(error, sizeof(error), "line %d: empty array-of-tables", line);
                    return -1;
                }
                int arr_idx = sub;
                mobius_stack_getArrayElement(state, arr_idx, arr_len - 1);
                int last = mobius_stack_size(state) - 1;
                // Remove the array from stack, keep the element
                // We'll leave both on stack; the element is what we use
                sub = last;
            }
            current = sub;
        }
        return current;
    }

    // -----------------------------------------------------------------------
    // Top-level document parse
    // -----------------------------------------------------------------------

    bool parse_document() {
        mobius_stack_pushNewTable(state, 8);
        root_idx = mobius_stack_size(state) - 1;

        int current_table = root_idx;

        while (pos < len) {
            skip_whitespace_and_newlines();
            if (pos >= len) break;

            char c = peek();

            if (c == '[') {
                // Table header or array-of-tables header
                pos++;
                bool is_array_of_tables = false;
                if (peek() == '[') {
                    is_array_of_tables = true;
                    pos++;
                }

                std::vector<std::string> key_parts;
                skip_whitespace_inline();
                if (!parse_key(key_parts)) return false;
                skip_whitespace_inline();

                if (is_array_of_tables) {
                    if (pos + 1 >= len || src[pos] != ']' || src[pos + 1] != ']') {
                        snprintf(error, sizeof(error), "line %d: expected ']]'", line);
                        return false;
                    }
                    pos += 2;
                } else {
                    if (peek() != ']') {
                        snprintf(error, sizeof(error), "line %d: expected ']'", line);
                        return false;
                    }
                    pos++;
                }

                skip_to_newline();

                // Reset stack to just root table
                while (mobius_stack_size(state) - 1 > root_idx)
                    mobius_stack_pop(state, 1);

                if (is_array_of_tables) {
                    // Navigate to parent, then append new table to array
                    int parent = root_idx;
                    if (key_parts.size() > 1) {
                        std::vector<std::string> parent_parts(key_parts.begin(), key_parts.end() - 1);
                        parent = navigate_to_table(root_idx, parent_parts, true);
                        if (parent < 0) return false;
                    }

                    const std::string& last_key = key_parts.back();
                    mobius_stack_getTableField(state, parent, last_key.c_str());
                    int arr_slot = mobius_stack_size(state) - 1;

                    if (mobius_stack_isNil(state, arr_slot)) {
                        mobius_stack_pop(state, 1);
                        mobius_stack_pushNewArray(state, 4);
                        arr_slot = mobius_stack_size(state) - 1;
                        mobius_stack_copy(state, arr_slot);
                        mobius_stack_setTableField(state, parent, last_key.c_str());
                    }

                    // Push a new table and add to array
                    mobius_stack_pushNewTable(state, 4);
                    int new_tbl = mobius_stack_size(state) - 1;
                    mobius_stack_copy(state, new_tbl);
                    mobius_stack_arrayPush(state, arr_slot);

                    current_table = new_tbl;
                } else {
                    // Standard table header
                    int tbl = navigate_to_table(root_idx, key_parts, true);
                    if (tbl < 0) return false;
                    current_table = tbl;
                }
                continue;
            }

            // Key-value pair
            if (is_bare_key_char(c) || c == '"' || c == '\'') {
                std::vector<std::string> key_parts;
                if (!parse_key(key_parts)) return false;

                skip_whitespace_inline();
                if (peek() != '=') {
                    snprintf(error, sizeof(error), "line %d: expected '=' after key", line);
                    return false;
                }
                pos++;
                skip_whitespace_inline();

                // Navigate dotted key to find target table
                int target = current_table;
                for (size_t i = 0; i + 1 < key_parts.size(); i++) {
                    mobius_stack_getTableField(state, target, key_parts[i].c_str());
                    int sub = mobius_stack_size(state) - 1;
                    if (mobius_stack_isNil(state, sub)) {
                        mobius_stack_pop(state, 1);
                        mobius_stack_pushNewTable(state, 4);
                        sub = mobius_stack_size(state) - 1;
                        mobius_stack_copy(state, sub);
                        mobius_stack_setTableField(state, target, key_parts[i].c_str());
                    }
                    target = sub;
                }

                if (!parse_value(0)) return false;
                mobius_stack_setTableField(state, target, key_parts.back().c_str());

                skip_to_newline();
                continue;
            }

            snprintf(error, sizeof(error), "line %d: unexpected character '%c'", line, c);
            return false;
        }

        // Clean up stack: only leave root table
        while (mobius_stack_size(state) - 1 > root_idx)
            mobius_stack_pop(state, 1);

        return true;
    }
};

// ============================================================================
// TOML SERIALIZER
// ============================================================================

struct TomlSerializer {
    MobiusState* state;
    std::string  out;
    bool         sort_keys;
    char         error[512];

    void escape_string(const char* s) {
        out.push_back('"');
        for (; *s; s++) {
            unsigned char c = (unsigned char)*s;
            switch (c) {
                case '"':  out.append("\\\""); break;
                case '\\': out.append("\\\\"); break;
                case '\b': out.append("\\b"); break;
                case '\f': out.append("\\f"); break;
                case '\n': out.append("\\n"); break;
                case '\r': out.append("\\r"); break;
                case '\t': out.append("\\t"); break;
                default:
                    if (c < 0x20) {
                        char buf[8];
                        snprintf(buf, sizeof(buf), "\\u%04x", c);
                        out.append(buf);
                    } else {
                        out.push_back((char)c);
                    }
                    break;
            }
        }
        out.push_back('"');
    }

    bool needs_quoting(const char* s) {
        if (!*s) return true;
        for (const char* p = s; *p; p++) {
            char c = *p;
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '-' || c == '_'))
                return true;
        }
        return false;
    }

    void write_key(const char* key) {
        if (needs_quoting(key))
            escape_string(key);
        else
            out.append(key);
    }

    bool is_datetime_table(int tbl_idx) {
        mobius_stack_getTableField(state, tbl_idx, "__toml_type");
        int type_idx = mobius_stack_size(state) - 1;
        bool result = mobius_stack_isString(state, type_idx);
        if (result) {
            const char* t = mobius_stack_asString(state, type_idx);
            result = (strcmp(t, "datetime") == 0 || strcmp(t, "date") == 0 || strcmp(t, "time") == 0);
        }
        mobius_stack_pop(state, 1);
        return result;
    }

    std::vector<std::string> collect_table_keys(int tbl_idx) {
        mobius_stack_getTableKeys(state, tbl_idx);
        int keys_arr = mobius_stack_size(state) - 1;
        size_t count = mobius_stack_getArrayLength(state, keys_arr);
        std::vector<std::string> keys;
        keys.reserve(count);

        for (size_t i = 0; i < count; i++) {
            mobius_stack_getArrayElement(state, keys_arr, i);
            if (mobius_stack_isString(state, -1)) {
                keys.emplace_back(mobius_stack_asString(state, -1));
            }
            mobius_stack_pop(state, 1);
        }

        mobius_stack_pop(state, 1); // keys array

        if (sort_keys) {
            std::sort(keys.begin(), keys.end());
        }
        return keys;
    }

    bool write_datetime(int tbl_idx) {
        mobius_stack_getTableField(state, tbl_idx, "__toml_type");
        const char* type = mobius_stack_asString(state, -1);
        bool is_date = (strcmp(type, "date") == 0 || strcmp(type, "datetime") == 0);
        bool is_time = (strcmp(type, "time") == 0 || strcmp(type, "datetime") == 0);
        mobius_stack_pop(state, 1);

        if (is_date) {
            mobius_stack_getTableField(state, tbl_idx, "year");
            mobius_stack_getTableField(state, tbl_idx, "month");
            mobius_stack_getTableField(state, tbl_idx, "day");
            int64_t y = mobius_stack_asInt64(state, -3);
            int64_t m = mobius_stack_asInt64(state, -2);
            int64_t d = mobius_stack_asInt64(state, -1);
            mobius_stack_pop(state, 3);
            char buf[32];
            snprintf(buf, sizeof(buf), "%04" PRId64 "-%02" PRId64 "-%02" PRId64, y, m, d);
            out.append(buf);
        }

        if (is_date && is_time) out.push_back('T');

        if (is_time) {
            mobius_stack_getTableField(state, tbl_idx, "hour");
            mobius_stack_getTableField(state, tbl_idx, "min");
            mobius_stack_getTableField(state, tbl_idx, "sec");
            int64_t h = mobius_stack_asInt64(state, -3);
            int64_t mi = mobius_stack_asInt64(state, -2);
            int64_t s = mobius_stack_asInt64(state, -1);
            mobius_stack_pop(state, 3);
            char buf[32];
            snprintf(buf, sizeof(buf), "%02" PRId64 ":%02" PRId64 ":%02" PRId64, h, mi, s);
            out.append(buf);

            mobius_stack_getTableField(state, tbl_idx, "tz");
            if (mobius_stack_isString(state, -1)) {
                out.append(mobius_stack_asString(state, -1));
            }
            mobius_stack_pop(state, 1);
        }
        return true;
    }

    // Check if a table contains only simple values (no sub-tables or arrays of tables)
    bool is_simple_table(int tbl_idx) {
        mobius_stack_getTableKeys(state, tbl_idx);
        int keys_arr = mobius_stack_size(state) - 1;
        size_t count = mobius_stack_getArrayLength(state, keys_arr);

        bool simple = true;
        for (size_t i = 0; i < count && simple; i++) {
            mobius_stack_getArrayElement(state, keys_arr, i);
            if (mobius_stack_isString(state, -1)) {
                const char* k = mobius_stack_asString(state, -1);
                mobius_stack_getTableField(state, tbl_idx, k);
                MobiusValueType vt = mobius_stack_type(state, -1);
                if (vt == MOBIUS_VAL_TABLE) {
                    int sub = mobius_stack_size(state) - 1;
                    if (!is_datetime_table(sub)) simple = false;
                } else if (vt == MOBIUS_VAL_ARRAY) {
                    int arr = mobius_stack_size(state) - 1;
                    size_t arr_len = mobius_stack_getArrayLength(state, arr);
                    if (arr_len > 0) {
                        mobius_stack_getArrayElement(state, arr, 0);
                        if (mobius_stack_type(state, -1) == MOBIUS_VAL_TABLE) simple = false;
                        mobius_stack_pop(state, 1);
                    }
                }
                mobius_stack_pop(state, 1); // value
            }
            mobius_stack_pop(state, 1); // key
        }
        mobius_stack_pop(state, 1); // keys array
        return simple;
    }

    bool serialize_inline_value(int idx, int depth) {
        if (depth > MAX_DEPTH) {
            snprintf(error, sizeof(error), "toml.stringify() exceeded maximum nesting depth");
            return false;
        }

        MobiusValueType vt = mobius_stack_type(state, idx);

        switch (vt) {
            case MOBIUS_VAL_NIL:
                snprintf(error, sizeof(error), "toml.stringify() nil values not supported in TOML");
                return false;

            case MOBIUS_VAL_BOOL:
                out.append(mobius_stack_asBool(state, idx) ? "true" : "false");
                return true;

            case MOBIUS_VAL_INT64: {
                char buf[32];
                snprintf(buf, sizeof(buf), "%" PRId64, mobius_stack_asInt64(state, idx));
                out.append(buf);
                return true;
            }

            case MOBIUS_VAL_UINT64: {
                char buf[32];
                snprintf(buf, sizeof(buf), "%" PRIu64, mobius_stack_asUInt64(state, idx));
                out.append(buf);
                return true;
            }

            case MOBIUS_VAL_FLOAT64: {
                double d = mobius_stack_asFloat64(state, idx);
                if (std::isinf(d)) {
                    out.append(d > 0 ? "inf" : "-inf");
                } else if (std::isnan(d)) {
                    out.append("nan");
                } else {
                    char buf[32];
                    snprintf(buf, sizeof(buf), "%.17g", d);
                    out.append(buf);
                    // Ensure it looks like a float
                    if (strchr(buf, '.') == nullptr && strchr(buf, 'e') == nullptr &&
                        strchr(buf, 'E') == nullptr) {
                        out.append(".0");
                    }
                }
                return true;
            }

            case MOBIUS_VAL_STRING: {
                const char* s = mobius_stack_asString(state, idx);
                escape_string(s);
                return true;
            }

            case MOBIUS_VAL_ARRAY: {
                size_t arr_len = mobius_stack_getArrayLength(state, idx);
                out.push_back('[');
                for (size_t i = 0; i < arr_len; i++) {
                    if (i > 0) out.append(", ");
                    mobius_stack_getArrayElement(state, idx, i);
                    int elem = mobius_stack_size(state) - 1;
                    if (!serialize_inline_value(elem, depth + 1)) {
                        mobius_stack_pop(state, 1);
                        return false;
                    }
                    mobius_stack_pop(state, 1);
                }
                out.push_back(']');
                return true;
            }

            case MOBIUS_VAL_TABLE: {
                if (is_datetime_table(idx)) return write_datetime(idx);
                // Inline table
                out.push_back('{');
                std::vector<std::string> keys = collect_table_keys(idx);
                bool first = true;
                for (const std::string& key : keys) {
                    const char* k = key.c_str();
                    if (strcmp(k, "__toml_type") == 0) continue;
                    if (!first) out.append(", ");
                    write_key(k);
                    out.append(" = ");
                    mobius_stack_getTableField(state, idx, k);
                    int val = mobius_stack_size(state) - 1;
                    if (!serialize_inline_value(val, depth + 1)) {
                        mobius_stack_pop(state, 1); // val
                        return false;
                    }
                    mobius_stack_pop(state, 1); // val
                    first = false;
                }
                out.push_back('}');
                return true;
            }

            default:
                snprintf(error, sizeof(error), "toml.stringify() unsupported value type");
                return false;
        }
    }

    bool serialize_table_body(int tbl_idx, const std::string& prefix, int depth) {
        if (depth > MAX_DEPTH) {
            snprintf(error, sizeof(error), "toml.stringify() exceeded maximum nesting depth");
            return false;
        }

        std::vector<std::string> keys = collect_table_keys(tbl_idx);

        // First pass: simple key-value pairs
        for (const std::string& key : keys) {
            const char* k = key.c_str();
            if (strcmp(k, "__toml_type") == 0) continue;

            mobius_stack_getTableField(state, tbl_idx, k);
            int val = mobius_stack_size(state) - 1;
            MobiusValueType vt = mobius_stack_type(state, val);

            if (vt == MOBIUS_VAL_TABLE && !is_datetime_table(val)) {
                mobius_stack_pop(state, 1); // val
                continue;
            }
            if (vt == MOBIUS_VAL_ARRAY) {
                size_t arr_len = mobius_stack_getArrayLength(state, val);
                if (arr_len > 0) {
                    mobius_stack_getArrayElement(state, val, 0);
                    bool is_aot = (mobius_stack_type(state, -1) == MOBIUS_VAL_TABLE);
                    mobius_stack_pop(state, 1);
                    if (is_aot) {
                        mobius_stack_pop(state, 1); // val
                        continue;
                    }
                }
            }

            write_key(k);
            out.append(" = ");
            if (!serialize_inline_value(val, depth + 1)) {
                mobius_stack_pop(state, 1); // val
                return false;
            }
            out.push_back('\n');
            mobius_stack_pop(state, 1); // val
        }

        // Second pass: sub-tables
        for (const std::string& key : keys) {
            const char* k = key.c_str();
            if (strcmp(k, "__toml_type") == 0) continue;
            std::string key_str(k);
            mobius_stack_getTableField(state, tbl_idx, k);
            int val = mobius_stack_size(state) - 1;
            MobiusValueType vt = mobius_stack_type(state, val);

            if (vt == MOBIUS_VAL_TABLE && !is_datetime_table(val)) {
                std::string full_key = prefix.empty() ? key_str : (prefix + "." + key_str);
                out.push_back('\n');
                out.push_back('[');
                out.append(full_key);
                out.append("]\n");
                if (!serialize_table_body(val, full_key, depth + 1)) {
                    mobius_stack_pop(state, 1); // val
                    return false;
                }
            }
            mobius_stack_pop(state, 1); // val
        }

        // Third pass: arrays of tables
        for (const std::string& key : keys) {
            const char* k = key.c_str();
            if (strcmp(k, "__toml_type") == 0) continue;
            std::string key_str(k);
            mobius_stack_getTableField(state, tbl_idx, k);
            int val = mobius_stack_size(state) - 1;

            if (mobius_stack_type(state, val) == MOBIUS_VAL_ARRAY) {
                size_t arr_len = mobius_stack_getArrayLength(state, val);
                if (arr_len > 0) {
                    mobius_stack_getArrayElement(state, val, 0);
                    bool is_aot = (mobius_stack_type(state, -1) == MOBIUS_VAL_TABLE);
                    mobius_stack_pop(state, 1);

                    if (is_aot) {
                        std::string full_key = prefix.empty() ? key_str : (prefix + "." + key_str);
                        for (size_t j = 0; j < arr_len; j++) {
                            out.push_back('\n');
                            out.append("[[");
                            out.append(full_key);
                            out.append("]]\n");
                            mobius_stack_getArrayElement(state, val, j);
                            int elem = mobius_stack_size(state) - 1;
                            if (!serialize_table_body(elem, full_key, depth + 1)) {
                                mobius_stack_pop(state, 2); // elem, val
                                return false;
                            }
                            mobius_stack_pop(state, 1); // elem
                        }
                    }
                }
            }
            mobius_stack_pop(state, 1); // val
        }
        return true;
    }
};

struct TomlStringifyOptions {
    bool sort_keys = false;
};

static bool toml_read_stringify_options(MobiusState* state, int options_idx,
                                        TomlStringifyOptions& options,
                                        char* error, size_t error_size) {
    mobius_stack_getTableField(state, options_idx, "sort_keys");
    if (!mobius_stack_isNil(state, -1)) {
        if (!mobius_stack_isBool(state, -1)) {
            snprintf(error, error_size, "toml.stringify() options.sort_keys must be a bool");
            mobius_stack_pop(state, 1);
            return false;
        }
        options.sort_keys = mobius_stack_asBool(state, -1);
    }
    mobius_stack_pop(state, 1);
    return true;
}

// ============================================================================
// toml.parse(string) -> table
// ============================================================================

static int toml_parse(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "toml.parse() expects 1 argument (string)");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "toml.parse() argument must be a string");

    const char* input = mobius_stack_asString(state, -1);

    TomlParser parser;
    parser.state = state;
    parser.src = input;
    parser.pos = 0;
    parser.len = strlen(input);
    parser.line = 1;
    parser.error[0] = '\0';
    parser.root_idx = -1;

    mobius_stack_pop(state, arg_count);

    if (!parser.parse_document()) {
        return mobius_error(state, parser.error);
    }
    return 1;
}

// ============================================================================
// toml.parsefile(path) -> table
// ============================================================================

static int toml_parsefile(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "toml.parsefile() expects 1 argument (path)");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "toml.parsefile() argument must be a string");

    const char* path = mobius_stack_asString(state, -1);

    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        char err[512];
        snprintf(err, sizeof(err), "toml.parsefile() cannot open '%s'", path);
        return mobius_error(state, err);
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    std::string content = ss.str();

    TomlParser parser;
    parser.state = state;
    parser.src = content.c_str();
    parser.pos = 0;
    parser.len = content.size();
    parser.line = 1;
    parser.error[0] = '\0';
    parser.root_idx = -1;

    mobius_stack_pop(state, arg_count);

    if (!parser.parse_document()) {
        return mobius_error(state, parser.error);
    }
    return 1;
}

// ============================================================================
// toml.stringify(table) -> string
// ============================================================================

static int toml_stringify(MobiusState* state, int arg_count) {
    if (arg_count < 1 || arg_count > 2)
        return mobius_error(state, "toml.stringify() expects 1 or 2 arguments (table [, options])");
    if (!mobius_stack_isTable(state, -arg_count))
        return mobius_error(state, "toml.stringify() first argument must be a table");

    TomlStringifyOptions options;
    if (arg_count == 2) {
        if (!mobius_stack_isTable(state, -1)) {
            return mobius_error(state, "toml.stringify() second argument must be an options table");
        }
        char error[256];
        if (!toml_read_stringify_options(state, mobius_stack_size(state) - 1, options,
                                         error, sizeof(error))) {
            return mobius_error(state, error);
        }
    }

    int tbl_idx = mobius_stack_size(state) - arg_count;

    TomlSerializer ser;
    ser.state = state;
    ser.sort_keys = options.sort_keys;
    ser.error[0] = '\0';

    if (!ser.serialize_table_body(tbl_idx, "", 0)) {
        return mobius_error(state, ser.error);
    }

    mobius_stack_pop(state, arg_count);
    mobius_stack_pushString(state, ser.out.c_str());
    return 1;
}

// ============================================================================
// Plugin boilerplate
// ============================================================================

static int init_toml_plugin(MobiusState* /*state*/) { return 0; }
static void cleanup_toml_plugin(void) {}

static MobiusPluginFunction toml_functions[] = {
    {"parse",     toml_parse,     1, MOBIUS_VAL_TABLE,  "Parse a TOML string into Mobius tables"},
    {"parsefile", toml_parsefile, 1, MOBIUS_VAL_TABLE,  "Parse a TOML file into Mobius tables"},
    {"stringify", toml_stringify, SIZE_MAX, MOBIUS_VAL_STRING, "Serialize a Mobius table to TOML string"},
};

static MobiusPlugin toml_plugin = {
    .metadata = {
        .name = "toml",
        .version = "1.0.0",
        .description = "TOML Parser and Serializer",
        .author = "Mobius Team",
        .api_version = MOBIUS_PLUGIN_API_VERSION,
        .license = "MIT"
    },
    .functions = toml_functions,
    .function_count = sizeof(toml_functions) / sizeof(toml_functions[0]),
    .init_plugin = init_toml_plugin,
    .cleanup_plugin = cleanup_toml_plugin,
    .post_init = nullptr,
};

extern "C" MOBIUS_PLUGIN_EXPORT MobiusPlugin* mobius_plugin_info(void) {
    return &toml_plugin;
}
