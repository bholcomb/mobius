#include <mobius/mobius_plugin.h>

#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <cinttypes>
#include <string>
#include <climits>
#include <cerrno>

static constexpr int MAX_DEPTH = 256;

// ============================================================================
// JSON PARSER — recursive descent
// ============================================================================

struct JsonParser {
    MobiusState* state;
    const char*  src;
    size_t       pos;
    size_t       len;
    char         error[256];

    void skip_whitespace() {
        while (pos < len) {
            char c = src[pos];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                pos++;
            else
                break;
        }
    }

    char peek() { return pos < len ? src[pos] : '\0'; }
    char advance() { return pos < len ? src[pos++] : '\0'; }

    bool expect(char c) {
        skip_whitespace();
        if (peek() == c) { pos++; return true; }
        snprintf(error, sizeof(error), "json.parse() expected '%c' at position %zu", c, pos);
        return false;
    }

    bool match_literal(const char* lit) {
        size_t l = strlen(lit);
        if (pos + l > len || memcmp(src + pos, lit, l) != 0) {
            snprintf(error, sizeof(error), "json.parse() unexpected token at position %zu", pos);
            return false;
        }
        pos += l;
        return true;
    }

    // Decode a \uXXXX hex escape, returns codepoint or -1 on error
    int parse_hex4() {
        if (pos + 4 > len) return -1;
        int cp = 0;
        for (int i = 0; i < 4; i++) {
            char c = src[pos++];
            cp <<= 4;
            if (c >= '0' && c <= '9') cp |= (c - '0');
            else if (c >= 'a' && c <= 'f') cp |= (c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') cp |= (c - 'A' + 10);
            else return -1;
        }
        return cp;
    }

    // Encode a Unicode codepoint to UTF-8, appended to out
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

    bool parse_string_value(std::string& out) {
        if (advance() != '"') {
            snprintf(error, sizeof(error), "json.parse() expected '\"' at position %zu", pos - 1);
            return false;
        }
        out.clear();
        while (pos < len) {
            char c = src[pos++];
            if (c == '"') return true;
            if (c == '\\') {
                if (pos >= len) break;
                char esc = src[pos++];
                switch (esc) {
                    case '"':  out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/':  out.push_back('/'); break;
                    case 'b':  out.push_back('\b'); break;
                    case 'f':  out.push_back('\f'); break;
                    case 'n':  out.push_back('\n'); break;
                    case 'r':  out.push_back('\r'); break;
                    case 't':  out.push_back('\t'); break;
                    case 'u': {
                        int cp = parse_hex4();
                        if (cp < 0) {
                            snprintf(error, sizeof(error),
                                     "json.parse() invalid \\uXXXX escape at position %zu", pos - 6);
                            return false;
                        }
                        // Handle UTF-16 surrogate pairs
                        if (cp >= 0xD800 && cp <= 0xDBFF) {
                            if (pos + 2 > len || src[pos] != '\\' || src[pos + 1] != 'u') {
                                snprintf(error, sizeof(error),
                                         "json.parse() missing low surrogate at position %zu", pos);
                                return false;
                            }
                            pos += 2;
                            int low = parse_hex4();
                            if (low < 0xDC00 || low > 0xDFFF) {
                                snprintf(error, sizeof(error),
                                         "json.parse() invalid low surrogate at position %zu", pos - 4);
                                return false;
                            }
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        }
                        encode_utf8(out, cp);
                        break;
                    }
                    default:
                        snprintf(error, sizeof(error),
                                 "json.parse() invalid escape '\\%c' at position %zu", esc, pos - 1);
                        return false;
                }
            } else {
                out.push_back(c);
            }
        }
        snprintf(error, sizeof(error), "json.parse() unterminated string");
        return false;
    }

    // Push a parsed JSON value onto the Mobius stack. Returns true on success.
    bool parse_value(int depth) {
        if (depth > MAX_DEPTH) {
            snprintf(error, sizeof(error), "json.parse() exceeded maximum nesting depth");
            return false;
        }

        skip_whitespace();
        if (pos >= len) {
            snprintf(error, sizeof(error), "json.parse() unexpected end of input");
            return false;
        }

        char c = peek();

        if (c == '{') return parse_object(depth);
        if (c == '[') return parse_array(depth);
        if (c == '"') return parse_string(depth);
        if (c == 't') return parse_true();
        if (c == 'f') return parse_false();
        if (c == 'n') return parse_null();
        if (c == '-' || (c >= '0' && c <= '9')) return parse_number();

        snprintf(error, sizeof(error), "json.parse() unexpected character '%c' at position %zu", c, pos);
        return false;
    }

    bool parse_object(int depth) {
        pos++; // skip '{'
        mobius_stack_pushNewTable(state, 8);
        int tbl_idx = mobius_stack_size(state) - 1;

        skip_whitespace();
        if (peek() == '}') { pos++; return true; }

        for (;;) {
            skip_whitespace();
            if (peek() != '"') {
                snprintf(error, sizeof(error),
                         "json.parse() expected string key at position %zu", pos);
                return false;
            }
            std::string key;
            if (!parse_string_value(key)) return false;

            if (!expect(':')) return false;

            if (!parse_value(depth + 1)) return false;

            // value is on top of stack; set it into the table
            mobius_stack_setTableField(state, tbl_idx, key.c_str());

            skip_whitespace();
            if (peek() == ',') { pos++; continue; }
            if (peek() == '}') { pos++; return true; }

            snprintf(error, sizeof(error),
                     "json.parse() expected ',' or '}' at position %zu", pos);
            return false;
        }
    }

    bool parse_array(int depth) {
        pos++; // skip '['
        mobius_stack_pushNewArray(state, 8);
        int arr_idx = mobius_stack_size(state) - 1;

        skip_whitespace();
        if (peek() == ']') { pos++; return true; }

        for (;;) {
            if (!parse_value(depth + 1)) return false;

            mobius_stack_arrayPush(state, arr_idx);

            skip_whitespace();
            if (peek() == ',') { pos++; continue; }
            if (peek() == ']') { pos++; return true; }

            snprintf(error, sizeof(error),
                     "json.parse() expected ',' or ']' at position %zu", pos);
            return false;
        }
    }

    bool parse_string(int /*depth*/) {
        std::string s;
        if (!parse_string_value(s)) return false;
        mobius_stack_pushString(state, s.c_str());
        return true;
    }

    bool parse_true() {
        if (!match_literal("true")) return false;
        mobius_stack_pushBool(state, true);
        return true;
    }

    bool parse_false() {
        if (!match_literal("false")) return false;
        mobius_stack_pushBool(state, false);
        return true;
    }

    bool parse_null() {
        if (!match_literal("null")) return false;
        mobius_stack_pushNil(state);
        return true;
    }

    bool parse_number() {
        const char* start = src + pos;
        bool is_float = false;

        if (peek() == '-') pos++;

        if (peek() == '0') {
            pos++;
        } else if (peek() >= '1' && peek() <= '9') {
            while (pos < len && peek() >= '0' && peek() <= '9') pos++;
        } else {
            snprintf(error, sizeof(error), "json.parse() invalid number at position %zu", pos);
            return false;
        }

        if (peek() == '.') {
            is_float = true;
            pos++;
            if (pos >= len || peek() < '0' || peek() > '9') {
                snprintf(error, sizeof(error),
                         "json.parse() expected digit after '.' at position %zu", pos);
                return false;
            }
            while (pos < len && peek() >= '0' && peek() <= '9') pos++;
        }

        if (peek() == 'e' || peek() == 'E') {
            is_float = true;
            pos++;
            if (peek() == '+' || peek() == '-') pos++;
            if (pos >= len || peek() < '0' || peek() > '9') {
                snprintf(error, sizeof(error),
                         "json.parse() expected digit in exponent at position %zu", pos);
                return false;
            }
            while (pos < len && peek() >= '0' && peek() <= '9') pos++;
        }

        size_t num_len = (size_t)((src + pos) - start);
        std::string num_str(start, num_len);

        if (!is_float) {
            // Try to parse as int64
            errno = 0;
            char* endp;
            long long val = strtoll(num_str.c_str(), &endp, 10);
            if (errno == 0 && *endp == '\0' &&
                val >= (long long)INT64_MIN && val <= (long long)INT64_MAX) {
                mobius_stack_pushInt64(state, (int64_t)val);
                return true;
            }
            // Falls through to float if it's too large
        }

        errno = 0;
        char* endp;
        double dval = strtod(num_str.c_str(), &endp);
        if (errno == ERANGE && (dval == HUGE_VAL || dval == -HUGE_VAL)) {
            snprintf(error, sizeof(error), "json.parse() number overflow at position %zu",
                     (size_t)(start - src));
            return false;
        }
        mobius_stack_pushFloat64(state, dval);
        return true;
    }
};

// ============================================================================
// JSON SERIALIZER
// ============================================================================

struct JsonSerializer {
    MobiusState* state;
    std::string  out;
    int          indent_size; // 0 = compact
    char         error[256];

    void write_indent(int depth) {
        if (indent_size <= 0) return;
        out.push_back('\n');
        for (int i = 0; i < depth * indent_size; i++)
            out.push_back(' ');
    }

    void write_newline_or_space() {
        if (indent_size > 0)
            out.push_back('\n');
    }

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

    bool serialize_value(int stack_idx, int depth) {
        if (depth > MAX_DEPTH) {
            snprintf(error, sizeof(error), "json.stringify() exceeded maximum nesting depth");
            return false;
        }

        MobiusValueType vtype = mobius_stack_type(state, stack_idx);

        switch (vtype) {
            case MOBIUS_VAL_NIL:
                out.append("null");
                return true;

            case MOBIUS_VAL_BOOL:
                out.append(mobius_stack_asBool(state, stack_idx) ? "true" : "false");
                return true;

            case MOBIUS_VAL_INT64: {
                char buf[32];
                snprintf(buf, sizeof(buf), "%" PRId64, mobius_stack_asInt64(state, stack_idx));
                out.append(buf);
                return true;
            }

            case MOBIUS_VAL_UINT64: {
                char buf[32];
                snprintf(buf, sizeof(buf), "%" PRIu64, mobius_stack_asUInt64(state, stack_idx));
                out.append(buf);
                return true;
            }

            case MOBIUS_VAL_FLOAT64: {
                double d = mobius_stack_asFloat64(state, stack_idx);
                if (std::isnan(d) || std::isinf(d)) {
                    out.append("null");
                } else {
                    char buf[32];
                    snprintf(buf, sizeof(buf), "%.17g", d);
                    out.append(buf);
                }
                return true;
            }

            case MOBIUS_VAL_STRING: {
                const char* s = mobius_stack_asString(state, stack_idx);
                escape_string(s);
                return true;
            }

            case MOBIUS_VAL_ARRAY:
                return serialize_array(stack_idx, depth);

            case MOBIUS_VAL_TABLE:
                return serialize_table(stack_idx, depth);

            default:
                snprintf(error, sizeof(error),
                         "json.stringify() unsupported type (functions, userdata, enums cannot be serialized)");
                return false;
        }
    }

    bool serialize_array(int arr_idx, int depth) {
        size_t len = mobius_stack_getArrayLength(state, arr_idx);
        if (len == 0) {
            out.append("[]");
            return true;
        }

        out.push_back('[');
        for (size_t i = 0; i < len; i++) {
            if (i > 0) out.push_back(',');
            write_indent(depth + 1);

            mobius_stack_getArrayElement(state, arr_idx, i);
            int elem_idx = mobius_stack_size(state) - 1;
            if (!serialize_value(elem_idx, depth + 1)) {
                mobius_stack_pop(state, 1);
                return false;
            }
            mobius_stack_pop(state, 1);
        }
        write_indent(depth);
        out.push_back(']');
        return true;
    }

    bool serialize_table(int tbl_idx, int depth) {
        size_t tbl_size = mobius_stack_getTableSize(state, tbl_idx);
        if (tbl_size == 0) {
            out.append("{}");
            return true;
        }

        // Get keys array
        mobius_stack_getTableKeys(state, tbl_idx);
        int keys_arr_idx = mobius_stack_size(state) - 1;
        size_t key_count = mobius_stack_getArrayLength(state, keys_arr_idx);

        out.push_back('{');
        bool first = true;

        for (size_t i = 0; i < key_count; i++) {
            // Get key
            mobius_stack_getArrayElement(state, keys_arr_idx, i);
            int key_idx = mobius_stack_size(state) - 1;

            // Keys must be strings for JSON
            if (mobius_stack_type(state, key_idx) != MOBIUS_VAL_STRING) {
                mobius_stack_pop(state, 1); // pop key
                continue; // skip non-string keys
            }

            const char* key_str = mobius_stack_asString(state, key_idx);

            // Get value
            mobius_stack_getTableField(state, tbl_idx, key_str);
            int val_idx = mobius_stack_size(state) - 1;

            if (!first) out.push_back(',');
            write_indent(depth + 1);

            escape_string(key_str);
            out.push_back(':');
            if (indent_size > 0) out.push_back(' ');

            if (!serialize_value(val_idx, depth + 1)) {
                mobius_stack_pop(state, 2); // pop value, key
                mobius_stack_pop(state, 1); // pop keys array
                return false;
            }

            mobius_stack_pop(state, 2); // pop value, key
            first = false;
        }

        mobius_stack_pop(state, 1); // pop keys array
        write_indent(depth);
        out.push_back('}');
        return true;
    }
};

static int json_parse_input(MobiusState* state, const char* input) {
    JsonParser parser;
    parser.state = state;
    parser.src = input;
    parser.pos = 0;
    parser.len = strlen(input);
    parser.error[0] = '\0';

    if (!parser.parse_value(0)) {
        return mobius_error(state, parser.error);
    }

    // Check for trailing content
    parser.skip_whitespace();
    if (parser.pos < parser.len) {
        mobius_stack_pop(state, 1);
        return mobius_error(state, "json.parse() unexpected trailing content after value");
    }

    return 1;
}

// ============================================================================
// json.parse(string) -> value
// ============================================================================

static int json_parse(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "json.parse() expects 1 argument (string)");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "json.parse() argument must be a string");

    const char* input = mobius_stack_asString(state, -1);
    mobius_stack_pop(state, arg_count);
    return json_parse_input(state, input);
}

// ============================================================================
// json.parsefile(path) -> value
// ============================================================================

static int json_parsefile(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "json.parsefile() expects 1 argument (path)");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "json.parsefile() argument must be a string");

    const char* path = mobius_stack_asString(state, -1);
    FILE* file = fopen(path, "rb");
    if (!file) {
        return mobius_error(state, "json.parsefile() could not open file");
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return mobius_error(state, "json.parsefile() could not seek file");
    }
    long size = ftell(file);
    if (size < 0) {
        fclose(file);
        return mobius_error(state, "json.parsefile() could not determine file size");
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return mobius_error(state, "json.parsefile() could not rewind file");
    }

    std::string input;
    input.resize((size_t)size);
    size_t nread = size > 0 ? fread(&input[0], 1, (size_t)size, file) : 0;
    fclose(file);
    if (nread != (size_t)size) {
        return mobius_error(state, "json.parsefile() could not read file");
    }

    mobius_stack_pop(state, arg_count);
    return json_parse_input(state, input.c_str());
}

// ============================================================================
// json.stringify(value [, indent]) -> string
// ============================================================================

static int json_stringify(MobiusState* state, int arg_count) {
    if (arg_count < 1 || arg_count > 2)
        return mobius_error(state, "json.stringify() expects 1 or 2 arguments (value [, indent])");

    int indent_size = 0;
    if (arg_count == 2) {
        if (!mobius_stack_isInteger(state, -1))
            return mobius_error(state, "json.stringify() indent must be an integer");
        indent_size = (int)mobius_stack_asInt64(state, -1);
        if (indent_size < 0) indent_size = 0;
        if (indent_size > 16) indent_size = 16;
    }

    // Convert to absolute stack index so it remains valid as we push/pop during serialization
    int value_idx = mobius_stack_size(state) - arg_count;

    JsonSerializer ser;
    ser.state = state;
    ser.indent_size = indent_size;
    ser.error[0] = '\0';

    if (!ser.serialize_value(value_idx, 0)) {
        return mobius_error(state, ser.error);
    }

    if (indent_size > 0) ser.out.push_back('\n');

    mobius_stack_pop(state, arg_count);
    mobius_stack_pushString(state, ser.out.c_str());
    return 1;
}

// ============================================================================
// Plugin boilerplate
// ============================================================================

static int init_json_plugin(MobiusState* /*state*/) { return 0; }
static void cleanup_json_plugin(void) {}

static MobiusPluginFunction json_functions[] = {
    {"parse",      json_parse,      1,        MOBIUS_VAL_UNKNOWN, "Parse a JSON string into Mobius values"},
    {"parsefile",  json_parsefile,  1,        MOBIUS_VAL_UNKNOWN, "Parse a JSON file into Mobius values"},
    {"stringify",  json_stringify,  SIZE_MAX, MOBIUS_VAL_STRING,  "Serialize a Mobius value to JSON string"},
};

static MobiusPlugin json_plugin = {
    .metadata = {
        .name = "json",
        .version = "1.0.0",
        .description = "JSON Parser and Serializer",
        .author = "Mobius Team",
        .api_version = MOBIUS_PLUGIN_API_VERSION,
        .license = "MIT"
    },
    .functions = json_functions,
    .function_count = sizeof(json_functions) / sizeof(json_functions[0]),
    .init_plugin = init_json_plugin,
    .cleanup_plugin = cleanup_json_plugin,
    .post_init = nullptr,
};

extern "C" MOBIUS_PLUGIN_EXPORT MobiusPlugin* mobius_plugin_info(void) {
    return &json_plugin;
}
