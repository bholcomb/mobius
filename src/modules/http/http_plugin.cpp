#include <mobius/mobius_plugin.h>

#include "modules/net/protocol_common.h"

#include <algorithm>
#include <cctype>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

using namespace mobius_net;

namespace {

struct HeaderEntry {
    std::string name;
    std::string value;
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

static bool parse_content_length(const std::vector<HeaderEntry>& headers, int64_t& out_length) {
    std::string value = header_lookup(headers, "content-length");
    if (value.empty()) return false;
    char* end_ptr = nullptr;
    long long parsed = strtoll(value.c_str(), &end_ptr, 10);
    if (!end_ptr || *end_ptr != '\0') return false;
    out_length = (int64_t)parsed;
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

static void set_bool_field(MobiusState* state, int tbl, const char* key, bool value) {
    mobius_stack_pushBool(state, value);
    mobius_stack_setTableField(state, tbl, key);
}

static void set_int_field(MobiusState* state, int tbl, const char* key, int64_t value) {
    mobius_stack_pushInt64(state, value);
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

static bool get_optional_bool_field(MobiusState* state, int tbl, const char* key, bool default_value) {
    mobius_stack_getTableField(state, tbl, key);
    bool out = default_value;
    if (!mobius_stack_isNil(state, -1)) out = mobius_stack_asBool(state, -1);
    mobius_stack_pop(state, 1);
    return out;
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

    std::string input = mobius_stack_asString(state, -1);
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
    set_string_field(state, tbl, "body", body);
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

    std::string input = mobius_stack_asString(state, -1);
    mobius_stack_pop(state, 1);

    std::string head, body;
    split_http_message(input, head, body);

    std::vector<std::string> lines;
    split_lines(head, lines);
    if (lines.empty() || trim(lines[0]).empty()) return mobius_error(state, "http.parse_response() missing status line");

    std::string status_line = lines[0];
    size_t sp1 = status_line.find(' ');
    size_t sp2 = sp1 == std::string::npos ? std::string::npos : status_line.find(' ', sp1 + 1);
    if (sp1 == std::string::npos) return mobius_error(state, "http.parse_response() invalid status line");

    std::string version = status_line.substr(0, sp1);
    std::string status_str = sp2 == std::string::npos ? trim(status_line.substr(sp1 + 1))
                                                      : trim(status_line.substr(sp1 + 1, sp2 - sp1 - 1));
    std::string reason = sp2 == std::string::npos ? "" : trim(status_line.substr(sp2 + 1));

    char* end_ptr = nullptr;
    long long status = strtoll(status_str.c_str(), &end_ptr, 10);
    if (!end_ptr || *end_ptr != '\0') return mobius_error(state, "http.parse_response() invalid status code");

    std::vector<HeaderEntry> headers;
    char error[128];
    if (!parse_headers_from_lines(lines, 1, headers, error, sizeof(error))) {
        return mobius_error(state, error);
    }

    bool keep_alive = true;
    std::string connection = header_lookup(headers, "connection");
    if (iequals(version, "HTTP/1.0")) keep_alive = header_value_has_token(connection, "keep-alive");
    else if (!connection.empty() && header_value_has_token(connection, "close")) keep_alive = false;

    mobius_stack_pushNewTable(state, 12);
    int tbl = mobius_stack_size(state) - 1;
    set_string_field(state, tbl, "version", version);
    set_int_field(state, tbl, "status", (int64_t)status);
    set_string_field(state, tbl, "reason", reason);
    set_string_field(state, tbl, "body", body);
    set_bool_field(state, tbl, "keep_alive", keep_alive);

    push_headers_table(state, headers);
    mobius_stack_setTableField(state, tbl, "headers");

    int64_t content_length = 0;
    if (parse_content_length(headers, content_length)) set_int_field(state, tbl, "content_length", content_length);
    std::string content_type = header_lookup(headers, "content-type");
    if (!content_type.empty()) set_string_field(state, tbl, "content_type", content_type);
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
    std::string body = get_optional_string_field(state, tbl, "body");
    std::string host = get_optional_string_field(state, tbl, "host");
    std::string path = get_optional_string_field(state, tbl, "path");
    std::string query = get_optional_string_field(state, tbl, "query");

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
    mobius_stack_pushString(state, out.c_str());
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
    mobius_stack_pushString(state, out.c_str());
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
    {"build_request",  http_build_request,  1, MOBIUS_VAL_STRING,  "Build a raw HTTP request string from a table"},
    {"build_response", http_build_response, 1, MOBIUS_VAL_STRING,  "Build a raw HTTP response string from a table"},
};

static MobiusPlugin http_plugin = {
    .metadata = {
        .name = "http",
        .version = "1.0.0",
        .description = "Dependency-free HTTP protocol parsing and message helpers",
        .author = "Mobius Team",
        .api_version = MOBIUS_PLUGIN_API_VERSION,
        .license = "MIT"
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
