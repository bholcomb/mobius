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

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #ifdef _MSC_VER
    #pragma comment(lib, "Ws2_32.lib")
  #endif
  typedef SOCKET http_socket_handle;
  static const http_socket_handle HTTP_INVALID_SOCKET_HANDLE = INVALID_SOCKET;
#else
  #include <arpa/inet.h>
  #include <cerrno>
  #include <netdb.h>
  #include <netinet/in.h>
  #include <netinet/tcp.h>
  #include <sys/socket.h>
  #include <sys/types.h>
  #include <unistd.h>
  typedef int http_socket_handle;
  static const http_socket_handle HTTP_INVALID_SOCKET_HANDLE = -1;
#endif

using namespace mobius_net;

namespace {

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

#ifdef _WIN32
static bool g_winsock_initialized = false;
#endif

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

static bool http_socket_platform_init() {
#ifdef _WIN32
    if (g_winsock_initialized) return true;
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) return false;
    g_winsock_initialized = true;
#endif
    return true;
}

static void http_socket_platform_cleanup() {
#ifdef _WIN32
    if (g_winsock_initialized) {
        WSACleanup();
        g_winsock_initialized = false;
    }
#endif
}

static void http_socket_close(http_socket_handle handle) {
    if (handle == HTTP_INVALID_SOCKET_HANDLE) return;
#ifdef _WIN32
    closesocket(handle);
#else
    close(handle);
#endif
}

static int http_socket_last_error() {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

static std::string http_socket_error_message(const char* prefix, int code) {
    char buffer[256];
#ifdef _WIN32
    snprintf(buffer, sizeof(buffer), "%s failed (code %d)", prefix, code);
#else
    snprintf(buffer, sizeof(buffer), "%s failed: %s", prefix, strerror(code));
#endif
    return std::string(buffer);
}

static bool http_socket_set_timeout(http_socket_handle handle, int64_t timeout_ms) {
#ifdef _WIN32
    DWORD timeout = (DWORD)timeout_ms;
    int rc1 = setsockopt(handle, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    int rc2 = setsockopt(handle, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
#else
    timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (int)((timeout_ms % 1000) * 1000);
    int rc1 = setsockopt(handle, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    int rc2 = setsockopt(handle, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
    return rc1 == 0 && rc2 == 0;
}

static bool http_socket_connect(const std::string& host, int64_t port,
                                int64_t timeout_ms, http_socket_handle& out_handle,
                                std::string& error) {
    out_handle = HTTP_INVALID_SOCKET_HANDLE;
    if (!http_socket_platform_init()) {
        error = "http.request() socket platform init failed";
        return false;
    }

    char port_buffer[32];
    snprintf(port_buffer, sizeof(port_buffer), "%" PRId64, port);

    addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* results = nullptr;
    int rc = getaddrinfo(host.c_str(), port_buffer, &hints, &results);
    if (rc != 0 || !results) {
        error = "http.request() address resolution failed";
        return false;
    }

    int last_error = 0;
    for (addrinfo* it = results; it; it = it->ai_next) {
        http_socket_handle handle = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (handle == HTTP_INVALID_SOCKET_HANDLE) {
            last_error = http_socket_last_error();
            continue;
        }
        if (::connect(handle, it->ai_addr, (socklen_t)it->ai_addrlen) == 0) {
            if (timeout_ms >= 0 && !http_socket_set_timeout(handle, timeout_ms)) {
                last_error = http_socket_last_error();
                http_socket_close(handle);
                freeaddrinfo(results);
                error = http_socket_error_message("http.request() set timeout", last_error);
                return false;
            }
            int yes = 1;
            setsockopt(handle, IPPROTO_TCP, TCP_NODELAY, (const char*)&yes, sizeof(yes));
            out_handle = handle;
            freeaddrinfo(results);
            return true;
        }
        last_error = http_socket_last_error();
        http_socket_close(handle);
    }
    freeaddrinfo(results);
    error = http_socket_error_message("http.request() connect", last_error);
    return false;
}

static bool http_socket_send_all(http_socket_handle handle, const char* data, size_t size, std::string& error) {
    size_t sent = 0;
    while (sent < size) {
#ifdef _WIN32
        int rc = send(handle, data + sent, (int)(size - sent), 0);
#else
        ssize_t rc = send(handle, data + sent, size - sent, 0);
#endif
        if (rc <= 0) {
            error = http_socket_error_message("http.request() send", http_socket_last_error());
            return false;
        }
        sent += (size_t)rc;
    }
    return true;
}

static bool http_socket_recv_some(http_socket_handle handle, std::string& buffer, std::string& error) {
    char chunk[4096];
#ifdef _WIN32
    int rc = recv(handle, chunk, (int)sizeof(chunk), 0);
#else
    ssize_t rc = recv(handle, chunk, sizeof(chunk), 0);
#endif
    if (rc < 0) {
        error = http_socket_error_message("http.request() recv", http_socket_last_error());
        return false;
    }
    if (rc == 0) return false;
    buffer.append(chunk, (size_t)rc);
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

static bool read_http_header_block(http_socket_handle handle, std::string& raw, std::string& head,
                                   std::string& remaining, std::string& error) {
    size_t header_end = std::string::npos;
    size_t separator_len = 0;
    while (true) {
        header_end = raw.find("\r\n\r\n");
        separator_len = 4;
        if (header_end == std::string::npos) {
            header_end = raw.find("\n\n");
            separator_len = 2;
        }
        if (header_end != std::string::npos) break;
        if (!http_socket_recv_some(handle, raw, error)) {
            if (error.empty()) error = "http.request() connection closed before response headers completed";
            return false;
        }
    }
    head = raw.substr(0, header_end);
    remaining = raw.substr(header_end + separator_len);
    return true;
}

static bool read_exact_bytes(http_socket_handle handle, std::string& buffer,
                             size_t required_size, std::string& error) {
    while (buffer.size() < required_size) {
        if (!http_socket_recv_some(handle, buffer, error)) {
            if (error.empty()) error = "http.request() connection closed before response body completed";
            return false;
        }
    }
    return true;
}

static bool read_until_close(http_socket_handle handle, std::string& buffer, std::string& error) {
    while (true) {
        std::string before = buffer;
        if (!http_socket_recv_some(handle, buffer, error)) {
            if (!error.empty()) return false;
            return true;
        }
        (void)before;
    }
}

static bool read_chunked_body(http_socket_handle handle, std::string& buffer,
                              std::string& out, std::string& error) {
    size_t cursor = 0;
    out.clear();
    while (true) {
        while (true) {
            size_t line_end = buffer.find('\n', cursor);
            if (line_end != std::string::npos) {
                std::string line = buffer.substr(cursor, line_end - cursor);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                size_t semicolon = line.find(';');
                std::string size_part = trim(line.substr(0, semicolon));
                char* end_ptr = nullptr;
                unsigned long long chunk_size = strtoull(size_part.c_str(), &end_ptr, 16);
                if (!end_ptr || *end_ptr != '\0') {
                    error = "http.request() invalid chunked response size";
                    return false;
                }
                cursor = line_end + 1;
                if (chunk_size == 0) {
                    while (true) {
                        size_t trailer_end = buffer.find('\n', cursor);
                        if (trailer_end == std::string::npos) {
                            if (!http_socket_recv_some(handle, buffer, error)) {
                                if (error.empty()) error = "http.request() incomplete chunked response trailer";
                                return false;
                            }
                            continue;
                        }
                        std::string trailer = buffer.substr(cursor, trailer_end - cursor);
                        if (!trailer.empty() && trailer.back() == '\r') trailer.pop_back();
                        cursor = trailer_end + 1;
                        if (trailer.empty()) return true;
                    }
                }
                size_t needed = cursor + (size_t)chunk_size + 2;
                if (!read_exact_bytes(handle, buffer, needed, error)) return false;
                out.append(buffer, cursor, (size_t)chunk_size);
                cursor += (size_t)chunk_size;
                if (buffer[cursor] == '\r') {
                    if (cursor + 1 >= buffer.size() || buffer[cursor + 1] != '\n') {
                        error = "http.request() invalid chunked response terminator";
                        return false;
                    }
                    cursor += 2;
                } else if (buffer[cursor] == '\n') {
                    cursor += 1;
                } else {
                    error = "http.request() invalid chunked response terminator";
                    return false;
                }
                break;
            }
            if (!http_socket_recv_some(handle, buffer, error)) {
                if (error.empty()) error = "http.request() incomplete chunked response";
                return false;
            }
        }
    }
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

static int http_request(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "http.request() expects 1 argument");
    if (!mobius_stack_isTable(state, -1)) return mobius_error(state, "http.request() expects a table argument");

    int tbl = mobius_stack_size(state) - 1;
    std::string method = get_optional_string_field(state, tbl, "method");
    std::string target = get_optional_string_field(state, tbl, "target");
    std::string version = get_optional_string_field(state, tbl, "version");
    std::string host = get_optional_string_field(state, tbl, "host");
    std::string path = get_optional_string_field(state, tbl, "path");
    std::string query = get_optional_string_field(state, tbl, "query");
    int64_t port = 80;
    int64_t timeout_ms = -1;
    get_optional_int_field(state, tbl, "port", port);
    get_optional_int_field(state, tbl, "timeout_ms", timeout_ms);

    std::string body;
    std::string error;
    if (!get_optional_body_bytes_field(state, tbl, "body", body, error, "http.request()")) {
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
            return mobius_error(state, "http.request() headers must be a table");
        }
        int headers_tbl = mobius_stack_size(state) - 1;
        append_sorted_header_table(state, headers_tbl, headers);
    }
    mobius_stack_pop(state, 1);
    mobius_stack_pop(state, 1);

    if (host.empty()) host = header_lookup(headers, "host");
    if (host.empty()) return mobius_error(state, "http.request() host is required");
    if (port < 0 || port > 65535) return mobius_error(state, "http.request() port must be in [0, 65535]");
    if (timeout_ms < -1) return mobius_error(state, "http.request() timeout_ms must be >= -1");

    append_or_replace_header(headers, "host", host);
    append_or_replace_header(headers, "connection", "close");
    if (!body.empty()) append_or_replace_header(headers, "content-length", std::to_string(body.size()));

    std::sort(headers.begin(), headers.end(), [](const HeaderEntry& a, const HeaderEntry& b) {
        return a.name < b.name;
    });

    std::string request_head = method + " " + target + " " + version + "\r\n";
    request_head += build_header_block(headers);
    request_head += "\r\n";

    http_socket_handle handle = HTTP_INVALID_SOCKET_HANDLE;
    if (!http_socket_connect(host, port, timeout_ms, handle, error)) {
        return mobius_error(state, error.c_str());
    }

    bool ok = http_socket_send_all(handle, request_head.data(), request_head.size(), error);
    if (ok && !body.empty()) ok = http_socket_send_all(handle, body.data(), body.size(), error);
    if (!ok) {
        http_socket_close(handle);
        return mobius_error(state, error.c_str());
    }

    std::string raw;
    std::string head;
    std::string remaining;
    if (!read_http_header_block(handle, raw, head, remaining, error)) {
        http_socket_close(handle);
        return mobius_error(state, error.c_str());
    }

    ParsedResponseHead parsed;
    if (!parse_response_head(head, parsed, error)) {
        http_socket_close(handle);
        return mobius_error(state, error.c_str());
    }

    std::string response_body;
    if (response_has_no_body(method, parsed.status)) {
        response_body.clear();
    } else if (header_value_has_token(header_lookup(parsed.headers, "transfer-encoding"), "chunked")) {
        if (!read_chunked_body(handle, remaining, response_body, error)) {
            http_socket_close(handle);
            return mobius_error(state, error.c_str());
        }
    } else if (!header_lookup(parsed.headers, "transfer-encoding").empty()) {
        http_socket_close(handle);
        return mobius_error(state, "http.request() unsupported transfer-encoding");
    } else if (parsed.has_content_length) {
        if (!read_exact_bytes(handle, remaining, (size_t)parsed.content_length, error)) {
            http_socket_close(handle);
            return mobius_error(state, error.c_str());
        }
        response_body = remaining.substr(0, (size_t)parsed.content_length);
    } else {
        response_body = remaining;
        if (!read_until_close(handle, response_body, error)) {
            http_socket_close(handle);
            return mobius_error(state, error.c_str());
        }
    }

    http_socket_close(handle);
    return push_parsed_response_table(state, parsed, response_body, true);
}

} // namespace

static int init_http_plugin(MobiusState* /*state*/) {
    return http_socket_platform_init() ? 0 : -1;
}

static void cleanup_http_plugin(void) {
    http_socket_platform_cleanup();
}

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
    {"request",        http_request,        1, MOBIUS_VAL_TABLE,   "Perform a plain HTTP request over TCP"},
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
