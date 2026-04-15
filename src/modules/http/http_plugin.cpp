#include <mobius/mobius_plugin.h>

#include <algorithm>
#include <cctype>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace {

static std::string trim_left(const std::string& s) {
    size_t i = 0;
    while (i < s.size() && std::isspace((unsigned char)s[i])) i++;
    return s.substr(i);
}

static std::string trim_right(const std::string& s) {
    size_t end = s.size();
    while (end > 0 && std::isspace((unsigned char)s[end - 1])) end--;
    return s.substr(0, end);
}

static std::string trim(const std::string& s) {
    return trim_right(trim_left(s));
}

static std::string to_lower_copy(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = (char)std::tolower((unsigned char)c);
    return out;
}

static bool iequals(const std::string& a, const std::string& b) {
    return to_lower_copy(a) == to_lower_copy(b);
}

static bool header_value_has_token(const std::string& value, const char* token) {
    std::string wanted = to_lower_copy(token ? token : "");
    size_t start = 0;
    while (start <= value.size()) {
        size_t end = value.find(',', start);
        std::string part = to_lower_copy(trim(value.substr(start, end == std::string::npos ? std::string::npos : end - start)));
        if (part == wanted) return true;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return false;
}

static void split_lines(const std::string& input, std::vector<std::string>& lines) {
    lines.clear();
    size_t start = 0;
    while (start <= input.size()) {
        size_t end = input.find('\n', start);
        std::string line = input.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(std::move(line));
        if (end == std::string::npos) break;
        start = end + 1;
    }
}

static std::string canonical_header_name(const std::string& name) {
    std::string out = to_lower_copy(name);
    bool upper = true;
    for (char& c : out) {
        if (upper && c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        upper = (c == '-');
    }
    return out;
}

static void collect_sorted_string_keys(MobiusState* state, int tbl_idx, std::vector<std::string>& keys) {
    mobius_stack_getTableKeys(state, tbl_idx);
    int keys_arr = mobius_stack_size(state) - 1;
    size_t count = mobius_stack_getArrayLength(state, keys_arr);
    keys.clear();
    keys.reserve(count);
    for (size_t i = 0; i < count; i++) {
        mobius_stack_getArrayElement(state, keys_arr, i);
        if (mobius_stack_isString(state, -1)) {
            keys.emplace_back(mobius_stack_asString(state, -1));
        }
        mobius_stack_pop(state, 1);
    }
    mobius_stack_pop(state, 1);
    std::sort(keys.begin(), keys.end());
}

struct HeaderEntry {
    std::string name;
    std::string value;
};

struct ParsedResponseHead {
    std::string version;
    int64_t status = 0;
    std::string reason;
    std::vector<HeaderEntry> headers;
    bool keep_alive = true;
    bool has_content_length = false;
    int64_t content_length = 0;
    std::string content_type;
};

static const char* http_status_text_impl(int64_t status) {
    switch (status) {
        case 100: return "Continue";
        case 101: return "Switching Protocols";
        case 200: return "OK";
        case 201: return "Created";
        case 202: return "Accepted";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 304: return "Not Modified";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 409: return "Conflict";
        case 410: return "Gone";
        case 418: return "I'm a teapot";
        case 422: return "Unprocessable Content";
        case 426: return "Upgrade Required";
        case 429: return "Too Many Requests";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        case 504: return "Gateway Timeout";
        default: return "";
    }
}

static bool split_http_message(const std::string& input, std::string& head, std::string& body) {
    size_t pos = input.find("\r\n\r\n");
    size_t sep_len = 4;
    if (pos == std::string::npos) {
        pos = input.find("\n\n");
        sep_len = 2;
    }
    if (pos == std::string::npos) {
        head = input;
        body.clear();
        return true;
    }
    head = input.substr(0, pos);
    body = input.substr(pos + sep_len);
    return true;
}

static bool parse_headers_from_lines(const std::vector<std::string>& lines, size_t start,
                                     std::vector<HeaderEntry>& headers,
                                     char* error, size_t error_size) {
    headers.clear();
    for (size_t i = start; i < lines.size(); i++) {
        const std::string line = lines[i];
        if (line.empty()) continue;
        size_t colon = line.find(':');
        if (colon == std::string::npos) {
            snprintf(error, error_size, "http.parse_*() invalid header line");
            return false;
        }
        std::string name = to_lower_copy(trim(line.substr(0, colon)));
        std::string value = trim(line.substr(colon + 1));
        if (name.empty()) {
            snprintf(error, error_size, "http.parse_*() invalid empty header name");
            return false;
        }
        headers.push_back({name, value});
    }
    return true;
}

static std::string header_lookup(const std::vector<HeaderEntry>& headers, const char* name) {
    std::string key = to_lower_copy(name ? name : "");
    for (auto it = headers.rbegin(); it != headers.rend(); ++it) {
        if (it->name == key) return it->value;
    }
    return "";
}

static bool parse_content_length(const std::vector<HeaderEntry>& headers, int64_t& out_length) {
    std::string value = header_lookup(headers, "content-length");
    if (value.empty()) return false;
    char* end_ptr = nullptr;
    long long parsed = strtoll(value.c_str(), &end_ptr, 10);
    if (!end_ptr || *end_ptr != '\0' || parsed < 0) return false;
    out_length = (int64_t)parsed;
    return true;
}

static bool is_valid_utf8(const uint8_t* data, size_t len) {
    size_t i = 0;
    while (i < len) {
        uint8_t c = data[i];
        if ((c & 0x80) == 0) {
            i++;
            continue;
        }

        size_t needed = 0;
        uint32_t codepoint = 0;
        if ((c & 0xE0) == 0xC0) {
            needed = 1;
            codepoint = (uint32_t)(c & 0x1F);
            if (codepoint == 0) return false;
        } else if ((c & 0xF0) == 0xE0) {
            needed = 2;
            codepoint = (uint32_t)(c & 0x0F);
        } else if ((c & 0xF8) == 0xF0) {
            needed = 3;
            codepoint = (uint32_t)(c & 0x07);
        } else {
            return false;
        }

        if (i + needed >= len) return false;
        for (size_t j = 1; j <= needed; j++) {
            uint8_t cc = data[i + j];
            if ((cc & 0xC0) != 0x80) return false;
            codepoint = (codepoint << 6) | (uint32_t)(cc & 0x3F);
        }

        if ((needed == 1 && codepoint < 0x80) ||
            (needed == 2 && codepoint < 0x800) ||
            (needed == 3 && codepoint < 0x10000) ||
            codepoint > 0x10FFFF ||
            (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
            return false;
        }

        i += needed + 1;
    }
    return true;
}

static void push_headers_table(MobiusState* state, const std::vector<HeaderEntry>& headers) {
    mobius_stack_pushNewTable(state, headers.size());
    int tbl = mobius_stack_size(state) - 1;
    for (const HeaderEntry& header : headers) {
        mobius_stack_pushString(state, header.value.c_str());
        mobius_stack_setTableField(state, tbl, header.name.c_str());
    }
}

static void set_string_field(MobiusState* state, int tbl, const char* key, const std::string& value) {
    mobius_stack_pushString(state, value.c_str());
    mobius_stack_setTableField(state, tbl, key);
}

static void set_string_bytes_field(MobiusState* state, int tbl, const char* key, const std::string& value) {
    mobius_stack_pushStringLength(state, value.data(), value.size());
    mobius_stack_setTableField(state, tbl, key);
}

static void set_bool_field(MobiusState* state, int tbl, const char* key, bool value) {
    mobius_stack_pushBool(state, value);
    mobius_stack_setTableField(state, tbl, key);
}

static void set_int_field(MobiusState* state, int tbl, const char* key, int64_t value) {
    mobius_stack_pushInt64(state, value);
    mobius_stack_setTableField(state, tbl, key);
}

static void set_buffer_field(MobiusState* state, int tbl, const char* key, const std::string& value) {
    mobius_stack_pushBufferCopy(state, value.data(), value.size());
    mobius_stack_setTableField(state, tbl, key);
}

static std::string get_optional_string_field(MobiusState* state, int tbl, const char* key) {
    mobius_stack_getTableField(state, tbl, key);
    std::string out;
    if (!mobius_stack_isNil(state, -1)) {
        const char* s = mobius_stack_asString(state, -1);
        out = s ? s : "";
    }
    mobius_stack_pop(state, 1);
    return out;
}

static bool get_optional_body_bytes_field(MobiusState* state, int tbl, const char* key,
                                          std::string& out, std::string& error,
                                          const char* context) {
    mobius_stack_getTableField(state, tbl, key);
    if (mobius_stack_isNil(state, -1)) {
        mobius_stack_pop(state, 1);
        out.clear();
        return true;
    }
    if (mobius_stack_isBuffer(state, -1)) {
        size_t size = 0;
        void* data = mobius_stack_getBufferData(state, -1, &size);
        const char* bytes = static_cast<const char*>(data);
        out.assign(bytes ? bytes : "", size);
        mobius_stack_pop(state, 1);
        return true;
    }
    if (mobius_stack_isString(state, -1)) {
        size_t size = 0;
        const char* data = mobius_stack_getStringData(state, -1, &size);
        out.assign(data ? data : "", size);
        mobius_stack_pop(state, 1);
        return true;
    }
    char buffer[160];
    snprintf(buffer, sizeof(buffer), "%s %s must be a string or buffer", context, key);
    error = buffer;
    mobius_stack_pop(state, 1);
    return false;
}

static bool get_optional_int_field(MobiusState* state, int tbl, const char* key, int64_t& out) {
    mobius_stack_getTableField(state, tbl, key);
    bool ok = false;
    if (!mobius_stack_isNil(state, -1)) {
        out = mobius_stack_asInt64(state, -1);
        ok = true;
    }
    mobius_stack_pop(state, 1);
    return ok;
}

static void append_sorted_header_table(MobiusState* state, int tbl, std::vector<HeaderEntry>& headers) {
    std::vector<std::string> keys;
    collect_sorted_string_keys(state, tbl, keys);
    for (const std::string& key : keys) {
        mobius_stack_getTableField(state, tbl, key.c_str());
        if (mobius_stack_isNil(state, -1)) {
            mobius_stack_pop(state, 1);
            continue;
        }
        const char* value = mobius_stack_asString(state, -1);
        headers.push_back({to_lower_copy(key), value ? value : ""});
        mobius_stack_pop(state, 1);
    }
}

static void append_or_replace_header(std::vector<HeaderEntry>& headers, const std::string& name, const std::string& value) {
    std::string key = to_lower_copy(name);
    for (HeaderEntry& header : headers) {
        if (header.name == key) {
            header.value = value;
            return;
        }
    }
    headers.push_back({key, value});
}

static std::string build_header_block(const std::vector<HeaderEntry>& headers) {
    std::string out;
    for (const HeaderEntry& header : headers) {
        out.append(canonical_header_name(header.name));
        out.append(": ");
        out.append(header.value);
        out.append("\r\n");
    }
    return out;
}

static void parse_target_components(const std::string& target, std::string& path, std::string& query) {
    size_t q = target.find('?');
    if (q == std::string::npos) {
        path = target;
        query.clear();
    } else {
        path = target.substr(0, q);
        query = target.substr(q + 1);
    }
}

static bool parse_response_head(const std::string& head, ParsedResponseHead& out, std::string& error) {
    std::vector<std::string> lines;
    split_lines(head, lines);
    if (lines.empty() || trim(lines[0]).empty()) {
        error = "http.parse_response() missing status line";
        return false;
    }

    std::string status_line = lines[0];
    size_t sp1 = status_line.find(' ');
    size_t sp2 = sp1 == std::string::npos ? std::string::npos : status_line.find(' ', sp1 + 1);
    if (sp1 == std::string::npos) {
        error = "http.parse_response() invalid status line";
        return false;
    }

    out.version = status_line.substr(0, sp1);
    std::string status_str = sp2 == std::string::npos ? trim(status_line.substr(sp1 + 1))
                                                      : trim(status_line.substr(sp1 + 1, sp2 - sp1 - 1));
    out.reason = sp2 == std::string::npos ? "" : trim(status_line.substr(sp2 + 1));

    char* end_ptr = nullptr;
    long long status = strtoll(status_str.c_str(), &end_ptr, 10);
    if (!end_ptr || *end_ptr != '\0' || status < 0) {
        error = "http.parse_response() invalid status code";
        return false;
    }
    out.status = (int64_t)status;

    char parse_error[128];
    if (!parse_headers_from_lines(lines, 1, out.headers, parse_error, sizeof(parse_error))) {
        error = parse_error;
        return false;
    }

    std::string connection = header_lookup(out.headers, "connection");
    out.keep_alive = true;
    if (iequals(out.version, "HTTP/1.0")) out.keep_alive = header_value_has_token(connection, "keep-alive");
    else if (!connection.empty() && header_value_has_token(connection, "close")) out.keep_alive = false;

    out.has_content_length = parse_content_length(out.headers, out.content_length);
    out.content_type = header_lookup(out.headers, "content-type");
    return true;
}

static bool response_has_no_body(const std::string& method, int64_t status) {
    if (iequals(method, "HEAD")) return true;
    if (status >= 100 && status < 200) return true;
    return status == 204 || status == 304;
}

static int push_parsed_response_table(MobiusState* state, const ParsedResponseHead& parsed,
                                      const std::string& body, bool buffer_body) {
    mobius_stack_pushNewTable(state, 12);
    int tbl = mobius_stack_size(state) - 1;
    set_string_field(state, tbl, "version", parsed.version);
    set_int_field(state, tbl, "status", parsed.status);
    set_string_field(state, tbl, "reason", parsed.reason);
    set_bool_field(state, tbl, "keep_alive", parsed.keep_alive);

    if (buffer_body) {
        set_buffer_field(state, tbl, "body", body);
        if (!body.empty() && is_valid_utf8(reinterpret_cast<const uint8_t*>(body.data()), body.size())) {
            set_string_bytes_field(state, tbl, "text", body);
        }
    } else {
        set_string_bytes_field(state, tbl, "body", body);
    }

    push_headers_table(state, parsed.headers);
    mobius_stack_setTableField(state, tbl, "headers");

    if (parsed.has_content_length) set_int_field(state, tbl, "content_length", parsed.content_length);
    if (!parsed.content_type.empty()) set_string_field(state, tbl, "content_type", parsed.content_type);
    return 1;
}

static int push_response_head_table(MobiusState* state, const ParsedResponseHead& parsed) {
    mobius_stack_pushNewTable(state, 10);
    int tbl = mobius_stack_size(state) - 1;
    set_string_field(state, tbl, "version", parsed.version);
    set_int_field(state, tbl, "status", parsed.status);
    set_string_field(state, tbl, "reason", parsed.reason);
    set_bool_field(state, tbl, "keep_alive", parsed.keep_alive);
    push_headers_table(state, parsed.headers);
    mobius_stack_setTableField(state, tbl, "headers");
    if (parsed.has_content_length) set_int_field(state, tbl, "content_length", parsed.content_length);
    if (!parsed.content_type.empty()) set_string_field(state, tbl, "content_type", parsed.content_type);
    return 1;
}

static int http_status_text(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "http.status_text() expects 1 argument");
    if (!mobius_stack_isInteger(state, -1)) return mobius_error(state, "http.status_text() expects an integer argument");
    int64_t code = mobius_stack_asInt64(state, -1);
    mobius_stack_pop(state, 1);
    mobius_stack_pushString(state, http_status_text_impl(code));
    return 1;
}

static int http_parse_request(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "http.parse_request() expects 1 argument");
    if (!mobius_stack_isString(state, -1)) return mobius_error(state, "http.parse_request() expects a string argument");

    size_t input_len = 0;
    const char* input_ptr = mobius_stack_getStringData(state, -1, &input_len);
    std::string input(input_ptr ? input_ptr : "", input_len);
    mobius_stack_pop(state, 1);

    std::string head, body;
    split_http_message(input, head, body);

    std::vector<std::string> lines;
    split_lines(head, lines);
    if (lines.empty() || trim(lines[0]).empty()) return mobius_error(state, "http.parse_request() missing request line");

    std::istringstream start_line(lines[0]);
    std::string method, target, version;
    start_line >> method >> target >> version;
    if (method.empty() || target.empty() || version.empty()) {
        return mobius_error(state, "http.parse_request() invalid request line");
    }

    std::vector<HeaderEntry> headers;
    char error[128];
    if (!parse_headers_from_lines(lines, 1, headers, error, sizeof(error))) {
        return mobius_error(state, error);
    }

    std::string path, query;
    parse_target_components(target, path, query);

    bool keep_alive = true;
    std::string connection = header_lookup(headers, "connection");
    if (iequals(version, "HTTP/1.0")) keep_alive = header_value_has_token(connection, "keep-alive");
    else if (!connection.empty() && header_value_has_token(connection, "close")) keep_alive = false;

    mobius_stack_pushNewTable(state, 12);
    int tbl = mobius_stack_size(state) - 1;
    set_string_field(state, tbl, "method", method);
    set_string_field(state, tbl, "target", target);
    set_string_field(state, tbl, "version", version);
    set_string_field(state, tbl, "path", path.empty() ? target : path);
    set_string_field(state, tbl, "query", query);
    set_string_bytes_field(state, tbl, "body", body);
    set_bool_field(state, tbl, "keep_alive", keep_alive);

    push_headers_table(state, headers);
    mobius_stack_setTableField(state, tbl, "headers");

    int64_t content_length = 0;
    if (parse_content_length(headers, content_length)) set_int_field(state, tbl, "content_length", content_length);
    std::string host = header_lookup(headers, "host");
    if (!host.empty()) set_string_field(state, tbl, "host", host);
    return 1;
}

static int http_parse_response(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "http.parse_response() expects 1 argument");
    if (!mobius_stack_isString(state, -1)) return mobius_error(state, "http.parse_response() expects a string argument");

    size_t input_len = 0;
    const char* input_ptr = mobius_stack_getStringData(state, -1, &input_len);
    std::string input(input_ptr ? input_ptr : "", input_len);
    mobius_stack_pop(state, 1);

    std::string head, body;
    split_http_message(input, head, body);

    ParsedResponseHead parsed;
    std::string error;
    if (!parse_response_head(head, parsed, error)) return mobius_error(state, error.c_str());
    return push_parsed_response_table(state, parsed, body, false);
}

static int http_parse_response_head_only(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "http.__parse_response_head() expects 1 argument");
    if (!mobius_stack_isString(state, -1)) return mobius_error(state, "http.__parse_response_head() expects a string argument");

    size_t input_len = 0;
    const char* input_ptr = mobius_stack_getStringData(state, -1, &input_len);
    std::string input(input_ptr ? input_ptr : "", input_len);
    mobius_stack_pop(state, 1);

    ParsedResponseHead parsed;
    std::string error;
    if (!parse_response_head(input, parsed, error)) return mobius_error(state, error.c_str());
    return push_response_head_table(state, parsed);
}

static bool decode_chunked_message(const std::string& input, std::string& out, std::string& error) {
    size_t cursor = 0;
    out.clear();
    while (true) {
        size_t line_end = input.find('\n', cursor);
        if (line_end == std::string::npos) {
            error = "http.__decode_chunked_body() incomplete chunk size line";
            return false;
        }

        std::string line = input.substr(cursor, line_end - cursor);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t semicolon = line.find(';');
        std::string size_part = trim(line.substr(0, semicolon));
        char* end_ptr = nullptr;
        unsigned long long chunk_size = strtoull(size_part.c_str(), &end_ptr, 16);
        if (!end_ptr || *end_ptr != '\0') {
            error = "http.__decode_chunked_body() invalid chunk size";
            return false;
        }

        cursor = line_end + 1;
        if (chunk_size == 0) {
            while (true) {
                size_t trailer_end = input.find('\n', cursor);
                if (trailer_end == std::string::npos) {
                    error = "http.__decode_chunked_body() incomplete chunk trailer";
                    return false;
                }
                std::string trailer = input.substr(cursor, trailer_end - cursor);
                if (!trailer.empty() && trailer.back() == '\r') trailer.pop_back();
                cursor = trailer_end + 1;
                if (trailer.empty()) return true;
            }
        }

        size_t needed = cursor + (size_t)chunk_size + 1;
        if (needed > input.size()) {
            error = "http.__decode_chunked_body() incomplete chunk payload";
            return false;
        }

        out.append(input, cursor, (size_t)chunk_size);
        cursor += (size_t)chunk_size;
        if (cursor >= input.size()) {
            error = "http.__decode_chunked_body() incomplete chunk terminator";
            return false;
        }
        if (input[cursor] == '\r') {
            if (cursor + 1 >= input.size() || input[cursor + 1] != '\n') {
                error = "http.__decode_chunked_body() invalid chunk terminator";
                return false;
            }
            cursor += 2;
        } else if (input[cursor] == '\n') {
            cursor += 1;
        } else {
            error = "http.__decode_chunked_body() invalid chunk terminator";
            return false;
        }
    }
}

static int http_decode_chunked_body(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "http.__decode_chunked_body() expects 1 argument");

    std::string input;
    if (mobius_stack_isBuffer(state, -1)) {
        size_t size = 0;
        void* data = mobius_stack_getBufferData(state, -1, &size);
        const char* bytes = static_cast<const char*>(data);
        input.assign(bytes ? bytes : "", size);
    } else if (mobius_stack_isString(state, -1)) {
        size_t size = 0;
        const char* data = mobius_stack_getStringData(state, -1, &size);
        input.assign(data ? data : "", size);
    } else {
        return mobius_error(state, "http.__decode_chunked_body() expects a string or buffer argument");
    }
    mobius_stack_pop(state, 1);

    std::string out;
    std::string error;
    if (!decode_chunked_message(input, out, error)) return mobius_error(state, error.c_str());
    mobius_stack_pushBufferCopy(state, out.data(), out.size());
    return 1;
}

static int http_try_decode_chunked_body(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "http.__try_decode_chunked_body() expects 1 argument");

    std::string input;
    if (mobius_stack_isBuffer(state, -1)) {
        size_t size = 0;
        void* data = mobius_stack_getBufferData(state, -1, &size);
        const char* bytes = static_cast<const char*>(data);
        input.assign(bytes ? bytes : "", size);
    } else if (mobius_stack_isString(state, -1)) {
        size_t size = 0;
        const char* data = mobius_stack_getStringData(state, -1, &size);
        input.assign(data ? data : "", size);
    } else {
        return mobius_error(state, "http.__try_decode_chunked_body() expects a string or buffer argument");
    }
    mobius_stack_pop(state, 1);

    std::string out;
    std::string error;
    if (!decode_chunked_message(input, out, error)) {
        if (error.find("incomplete") != std::string::npos) {
            mobius_stack_pushNil(state);
            return 1;
        }
        return mobius_error(state, error.c_str());
    }

    mobius_stack_pushBufferCopy(state, out.data(), out.size());
    return 1;
}

static int http_text_if_utf8(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "http.__text_if_utf8() expects 1 argument");

    const uint8_t* bytes = nullptr;
    size_t size = 0;
    if (mobius_stack_isBuffer(state, -1)) {
        void* data = mobius_stack_getBufferData(state, -1, &size);
        bytes = static_cast<const uint8_t*>(data);
    } else if (mobius_stack_isString(state, -1)) {
        const char* data = mobius_stack_getStringData(state, -1, &size);
        bytes = reinterpret_cast<const uint8_t*>(data);
    } else {
        return mobius_error(state, "http.__text_if_utf8() expects a string or buffer argument");
    }

    if (size > 0 && is_valid_utf8(bytes, size)) {
        mobius_stack_pop(state, 1);
        mobius_stack_pushStringLength(state, reinterpret_cast<const char*>(bytes), size);
        return 1;
    }

    mobius_stack_pop(state, 1);
    mobius_stack_pushNil(state);
    return 1;
}

static int http_build_headers(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "http.build_headers() expects 1 argument");
    if (!mobius_stack_isTable(state, -1)) return mobius_error(state, "http.build_headers() expects a table argument");
    int tbl = mobius_stack_size(state) - 1;
    std::vector<HeaderEntry> headers;
    append_sorted_header_table(state, tbl, headers);
    mobius_stack_pop(state, 1);
    std::string out = build_header_block(headers);
    mobius_stack_pushString(state, out.c_str());
    return 1;
}

static int http_parse_headers(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "http.parse_headers() expects 1 argument");
    if (!mobius_stack_isString(state, -1)) return mobius_error(state, "http.parse_headers() expects a string argument");
    std::string input = mobius_stack_asString(state, -1);
    mobius_stack_pop(state, 1);
    std::vector<std::string> lines;
    split_lines(input, lines);
    std::vector<HeaderEntry> headers;
    char error[128];
    if (!parse_headers_from_lines(lines, 0, headers, error, sizeof(error))) {
        return mobius_error(state, error);
    }
    push_headers_table(state, headers);
    return 1;
}

static int http_build_request(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "http.build_request() expects 1 argument");
    if (!mobius_stack_isTable(state, -1)) return mobius_error(state, "http.build_request() expects a table argument");

    int tbl = mobius_stack_size(state) - 1;
    std::string method = get_optional_string_field(state, tbl, "method");
    std::string target = get_optional_string_field(state, tbl, "target");
    std::string version = get_optional_string_field(state, tbl, "version");
    std::string host = get_optional_string_field(state, tbl, "host");
    std::string path = get_optional_string_field(state, tbl, "path");
    std::string query = get_optional_string_field(state, tbl, "query");
    std::string body;
    std::string error;
    if (!get_optional_body_bytes_field(state, tbl, "body", body, error, "http.build_request()")) {
        mobius_stack_pop(state, 1);
        return mobius_error(state, error.c_str());
    }

    if (method.empty()) method = "GET";
    if (version.empty()) version = "HTTP/1.1";
    if (target.empty()) {
        target = path.empty() ? "/" : path;
        if (!query.empty()) target += "?" + query;
    }

    std::vector<HeaderEntry> headers;
    mobius_stack_getTableField(state, tbl, "headers");
    if (!mobius_stack_isNil(state, -1)) {
        if (!mobius_stack_isTable(state, -1)) {
            mobius_stack_pop(state, 2);
            return mobius_error(state, "http.build_request() headers must be a table");
        }
        int headers_tbl = mobius_stack_size(state) - 1;
        append_sorted_header_table(state, headers_tbl, headers);
    }
    mobius_stack_pop(state, 1);

    if (!host.empty()) append_or_replace_header(headers, "host", host);
    if (!body.empty()) append_or_replace_header(headers, "content-length", std::to_string(body.size()));

    std::sort(headers.begin(), headers.end(), [](const HeaderEntry& a, const HeaderEntry& b) {
        return a.name < b.name;
    });

    std::string out = method + " " + target + " " + version + "\r\n";
    out += build_header_block(headers);
    out += "\r\n";
    out += body;

    mobius_stack_pop(state, 1);
    mobius_stack_pushStringLength(state, out.data(), out.size());
    return 1;
}

static int http_build_response(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "http.build_response() expects 1 argument");
    if (!mobius_stack_isTable(state, -1)) return mobius_error(state, "http.build_response() expects a table argument");

    int tbl = mobius_stack_size(state) - 1;
    int64_t status = 0;
    if (!get_optional_int_field(state, tbl, "status", status)) {
        mobius_stack_pop(state, 1);
        return mobius_error(state, "http.build_response() status is required");
    }

    std::string version = get_optional_string_field(state, tbl, "version");
    std::string reason = get_optional_string_field(state, tbl, "reason");
    std::string body = get_optional_string_field(state, tbl, "body");
    if (version.empty()) version = "HTTP/1.1";
    if (reason.empty()) reason = http_status_text_impl(status);

    std::vector<HeaderEntry> headers;
    mobius_stack_getTableField(state, tbl, "headers");
    if (!mobius_stack_isNil(state, -1)) {
        if (!mobius_stack_isTable(state, -1)) {
            mobius_stack_pop(state, 2);
            return mobius_error(state, "http.build_response() headers must be a table");
        }
        int headers_tbl = mobius_stack_size(state) - 1;
        append_sorted_header_table(state, headers_tbl, headers);
    }
    mobius_stack_pop(state, 1);

    if (!body.empty()) append_or_replace_header(headers, "content-length", std::to_string(body.size()));

    std::sort(headers.begin(), headers.end(), [](const HeaderEntry& a, const HeaderEntry& b) {
        return a.name < b.name;
    });

    std::string out = version + " " + std::to_string(status) + " " + reason + "\r\n";
    out += build_header_block(headers);
    out += "\r\n";
    out += body;

    mobius_stack_pop(state, 1);
    mobius_stack_pushStringLength(state, out.data(), out.size());
    return 1;
}

} // namespace

static int init_http_plugin(MobiusState* /*state*/) { return 0; }
static void cleanup_http_plugin(void) {}

static MobiusPluginFunction http_functions[] = {
    {"status_text",    http_status_text,    1, MOBIUS_VAL_STRING,  "Return the standard reason phrase for an HTTP status code"},
    {"parse_headers",  http_parse_headers,  1, MOBIUS_VAL_TABLE,   "Parse raw HTTP headers into a table"},
    {"build_headers",  http_build_headers,  1, MOBIUS_VAL_STRING,  "Build CRLF-separated HTTP header lines from a table"},
    {"parse_request",  http_parse_request,  1, MOBIUS_VAL_TABLE,   "Parse a raw HTTP request string"},
    {"parse_response", http_parse_response, 1, MOBIUS_VAL_TABLE,   "Parse a raw HTTP response string"},
    {"__parse_response_head", http_parse_response_head_only, 1, MOBIUS_VAL_TABLE, "Internal HTTP response-head parser"},
    {"__decode_chunked_body", http_decode_chunked_body, 1, MOBIUS_VAL_BUFFER, "Internal HTTP chunked decoder"},
    {"__try_decode_chunked_body", http_try_decode_chunked_body, 1, MOBIUS_VAL_UNKNOWN, "Internal HTTP chunked decoder with incomplete=nil"},
    {"__text_if_utf8", http_text_if_utf8, 1, MOBIUS_VAL_UNKNOWN, "Internal UTF-8 response text helper"},
    {"build_request",  http_build_request,  1, MOBIUS_VAL_STRING,  "Build a raw HTTP request string from a table"},
    {"build_response", http_build_response, 1, MOBIUS_VAL_STRING,  "Build a raw HTTP response string from a table"},
};

static const char* http_depends_on[] = {
    "socket",
};

static MobiusPlugin http_plugin = {
    .metadata = {
        .name = "http",
        .version = "1.0.0",
        .description = "Dependency-free HTTP protocol parsing and transport helpers",
        .author = "Mobius Team",
        .api_version = MOBIUS_PLUGIN_API_VERSION,
        .license = "MIT",
        .depends_on = http_depends_on,
        .depends_on_count = sizeof(http_depends_on) / sizeof(http_depends_on[0]),
    },
    .functions = http_functions,
    .function_count = sizeof(http_functions) / sizeof(http_functions[0]),
    .init_plugin = init_http_plugin,
    .cleanup_plugin = cleanup_http_plugin,
    .post_init = nullptr,
};

extern "C" MOBIUS_PLUGIN_EXPORT MobiusPlugin* mobius_plugin_info(void) {
    return &http_plugin;
}
