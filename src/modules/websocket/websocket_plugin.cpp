#include <mobius/mobius_plugin.h>

#include "modules/net/protocol_common.h"

#include <algorithm>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace mobius_net;

namespace {

struct ParsedFrame {
    bool fin = true;
    bool rsv1 = false;
    bool rsv2 = false;
    bool rsv3 = false;
    bool masked = false;
    int opcode = 0;
    size_t frame_length = 0;
    std::vector<uint8_t> payload;
    std::vector<uint8_t> masking_key;
};

struct HeaderEntry {
    std::string name;
    std::string value;
};

enum class FrameParseStatus {
    ok,
    need_more,
    error,
};

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
            snprintf(error, error_size, "websocket invalid header line");
            return false;
        }
        std::string name = to_lower_copy(trim(line.substr(0, colon)));
        std::string value = trim(line.substr(colon + 1));
        if (name.empty()) {
            snprintf(error, error_size, "websocket invalid empty header name");
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

static int get_optional_string_bytes_field(MobiusState* state, int tbl, const char* key,
                                           std::string& out, std::string& error,
                                           const char* context) {
    mobius_stack_getTableField(state, tbl, key);
    if (mobius_stack_isNil(state, -1)) {
        mobius_stack_pop(state, 1);
        return 0;
    }
    if (!mobius_stack_isString(state, -1)) {
        char buffer[192];
        snprintf(buffer, sizeof(buffer), "%s %s must be a string", context, key);
        error = buffer;
        mobius_stack_pop(state, 1);
        return -1;
    }
    size_t len = 0;
    const char* s = mobius_stack_getStringData(state, -1, &len);
    out.assign(s ? s : "", len);
    mobius_stack_pop(state, 1);
    return 1;
}

static int get_optional_buffer_field(MobiusState* state, int tbl, const char* key,
                                     std::vector<uint8_t>& out, std::string& error,
                                     const char* context) {
    mobius_stack_getTableField(state, tbl, key);
    if (mobius_stack_isNil(state, -1)) {
        mobius_stack_pop(state, 1);
        return 0;
    }
    if (!mobius_stack_isBuffer(state, -1)) {
        char buffer[192];
        snprintf(buffer, sizeof(buffer), "%s %s must be a buffer", context, key);
        error = buffer;
        mobius_stack_pop(state, 1);
        return -1;
    }
    size_t len = 0;
    void* data = mobius_stack_getBufferData(state, -1, &len);
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    out.clear();
    if (len > 0) out.assign(bytes, bytes + len);
    mobius_stack_pop(state, 1);
    return 1;
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

static void set_string_field(MobiusState* state, int tbl, const char* key, const std::string& value) {
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

static void set_buffer_field(MobiusState* state, int tbl, const char* key,
                             const uint8_t* data, size_t size) {
    mobius_stack_pushBufferCopy(state, data, size);
    mobius_stack_setTableField(state, tbl, key);
}

static void set_buffer_field(MobiusState* state, int tbl, const char* key,
                             const std::vector<uint8_t>& data) {
    const uint8_t* bytes = data.empty() ? nullptr : data.data();
    set_buffer_field(state, tbl, key, bytes, data.size());
}

static std::string header_lookup_from_request(MobiusState* state, int request_tbl, const char* name) {
    std::string out;
    mobius_stack_getTableField(state, request_tbl, "headers");
    if (!mobius_stack_isNil(state, -1) && mobius_stack_isTable(state, -1)) {
        int headers_tbl = mobius_stack_size(state) - 1;
        std::string key = to_lower_copy(name ? name : "");
        mobius_stack_getTableField(state, headers_tbl, key.c_str());
        if (!mobius_stack_isNil(state, -1)) {
            const char* s = mobius_stack_asString(state, -1);
            out = s ? s : "";
        }
        mobius_stack_pop(state, 1);
    }
    mobius_stack_pop(state, 1);
    return out;
}

static bool request_is_upgrade(MobiusState* state, int request_tbl) {
    std::string method = get_optional_string_field(state, request_tbl, "method");
    std::string version = get_optional_string_field(state, request_tbl, "version");
    std::string upgrade = header_lookup_from_request(state, request_tbl, "upgrade");
    std::string connection = header_lookup_from_request(state, request_tbl, "connection");
    std::string key = header_lookup_from_request(state, request_tbl, "sec-websocket-key");
    std::string ws_version = header_lookup_from_request(state, request_tbl, "sec-websocket-version");

    if (!iequals(method, "GET")) return false;
    if (!version.empty() && !iequals(version, "HTTP/1.1")) return false;
    if (!iequals(trim(upgrade), "websocket")) return false;
    if (!header_value_has_token(connection, "upgrade")) return false;
    if (key.empty()) return false;
    if (!ws_version.empty() && trim(ws_version) != "13") return false;
    return true;
}

static std::string websocket_accept_key(const std::string& client_key) {
    static const char* GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string input = client_key + GUID;
    std::vector<uint8_t> sha = sha1_bytes(reinterpret_cast<const uint8_t*>(input.data()), input.size());
    return base64_encode_bytes(sha.data(), sha.size());
}

static int opcode_from_type(const std::string& type) {
    std::string lower = to_lower_copy(type);
    if (lower == "continuation") return 0x0;
    if (lower == "text") return 0x1;
    if (lower == "binary") return 0x2;
    if (lower == "close") return 0x8;
    if (lower == "ping") return 0x9;
    if (lower == "pong") return 0xA;
    return -1;
}

static std::string type_from_opcode(int opcode) {
    switch (opcode) {
        case 0x0: return "continuation";
        case 0x1: return "text";
        case 0x2: return "binary";
        case 0x8: return "close";
        case 0x9: return "ping";
        case 0xA: return "pong";
        default: return "unknown";
    }
}

static bool is_valid_utf8(const uint8_t* data, size_t len) {
    size_t i = 0;
    while (i < len) {
        uint8_t c = data[i];
        if (c <= 0x7F) {
            i++;
            continue;
        }

        int continuation_count = 0;
        uint32_t codepoint = 0;
        if ((c & 0xE0) == 0xC0) {
            continuation_count = 1;
            codepoint = c & 0x1F;
            if (codepoint == 0) return false;
        } else if ((c & 0xF0) == 0xE0) {
            continuation_count = 2;
            codepoint = c & 0x0F;
        } else if ((c & 0xF8) == 0xF0) {
            continuation_count = 3;
            codepoint = c & 0x07;
        } else {
            return false;
        }

        if (i + (size_t)continuation_count >= len) return false;
        for (int j = 0; j < continuation_count; j++) {
            uint8_t next = data[i + 1 + (size_t)j];
            if ((next & 0xC0) != 0x80) return false;
            codepoint = (codepoint << 6) | (uint32_t)(next & 0x3F);
        }

        if ((continuation_count == 1 && codepoint < 0x80) ||
            (continuation_count == 2 && codepoint < 0x800) ||
            (continuation_count == 3 && codepoint < 0x10000) ||
            (codepoint >= 0xD800 && codepoint <= 0xDFFF) ||
            codepoint > 0x10FFFF) {
            return false;
        }

        i += (size_t)continuation_count + 1;
    }
    return true;
}

static bool is_control_opcode(int opcode) {
    return (opcode & 0x8) != 0;
}

static bool is_text_opcode(int opcode) {
    return opcode == 0x1;
}

static bool is_close_opcode(int opcode) {
    return opcode == 0x8;
}

static bool encode_frame_bytes(int opcode, const std::vector<uint8_t>& payload, bool fin, bool mask,
                               bool rsv1, bool rsv2, bool rsv3,
                               std::vector<uint8_t>& out, std::string& error) {
    if (opcode < 0 || opcode > 0xF) {
        error = "invalid opcode";
        return false;
    }
    if (is_control_opcode(opcode)) {
        if (!fin) {
            error = "control frames must set fin=true";
            return false;
        }
        if (payload.size() > 125) {
            error = "control frame payloads must be <= 125 bytes";
            return false;
        }
    }

    uint8_t masking_key[4] = {0, 0, 0, 0};
    if (mask && !secure_random_fill(masking_key, sizeof(masking_key))) {
        error = "secure random generation failed";
        return false;
    }

    out.clear();
    out.reserve(payload.size() + 16);
    uint8_t b0 = (uint8_t)((fin ? 0x80 : 0x00) |
                           (rsv1 ? 0x40 : 0x00) |
                           (rsv2 ? 0x20 : 0x00) |
                           (rsv3 ? 0x10 : 0x00) |
                           (opcode & 0x0F));
    out.push_back(b0);
    uint64_t len = payload.size();
    if (len < 126) {
        out.push_back((uint8_t)((mask ? 0x80 : 0x00) | (uint8_t)len));
    } else if (len <= 0xFFFF) {
        out.push_back((uint8_t)((mask ? 0x80 : 0x00) | 126));
        out.push_back((uint8_t)((len >> 8) & 0xFF));
        out.push_back((uint8_t)(len & 0xFF));
    } else {
        out.push_back((uint8_t)((mask ? 0x80 : 0x00) | 127));
        for (int i = 7; i >= 0; i--) out.push_back((uint8_t)(len >> (i * 8)));
    }
    if (mask) out.insert(out.end(), masking_key, masking_key + 4);
    if (mask) {
        for (size_t i = 0; i < payload.size(); i++) out.push_back((uint8_t)(payload[i] ^ masking_key[i % 4]));
    } else {
        out.insert(out.end(), payload.begin(), payload.end());
    }
    return true;
}

static FrameParseStatus parse_frame_bytes(const uint8_t* bytes, size_t size, ParsedFrame& out, std::string& error) {
    if (size < 2) return FrameParseStatus::need_more;
    size_t offset = 0;
    uint8_t b0 = bytes[offset++];
    uint8_t b1 = bytes[offset++];
    out.fin = (b0 & 0x80) != 0;
    out.rsv1 = (b0 & 0x40) != 0;
    out.rsv2 = (b0 & 0x20) != 0;
    out.rsv3 = (b0 & 0x10) != 0;
    out.opcode = b0 & 0x0F;
    out.masked = (b1 & 0x80) != 0;
    uint64_t payload_len = b1 & 0x7F;
    if (payload_len == 126) {
        if (offset + 2 > size) return FrameParseStatus::need_more;
        payload_len = ((uint64_t)bytes[offset] << 8) | (uint64_t)bytes[offset + 1];
        offset += 2;
    } else if (payload_len == 127) {
        if (offset + 8 > size) return FrameParseStatus::need_more;
        payload_len = 0;
        for (int i = 0; i < 8; i++) payload_len = (payload_len << 8) | (uint64_t)bytes[offset + i];
        offset += 8;
    }
    if (is_control_opcode(out.opcode) && (!out.fin || payload_len > 125)) {
        error = "invalid control frame";
        return FrameParseStatus::error;
    }
    out.masking_key.clear();
    if (out.masked) {
        if (offset + 4 > size) return FrameParseStatus::need_more;
        out.masking_key.assign(bytes + offset, bytes + offset + 4);
        offset += 4;
    }
    if (offset + payload_len > size) return FrameParseStatus::need_more;
    out.payload.assign(bytes + offset, bytes + offset + (size_t)payload_len);
    if (out.masked) {
        for (size_t i = 0; i < out.payload.size(); i++) out.payload[i] = (uint8_t)(out.payload[i] ^ out.masking_key[i % 4]);
    }
    if (is_close_opcode(out.opcode) && out.payload.size() == 1) {
        error = "invalid close payload";
        return FrameParseStatus::error;
    }
    out.frame_length = offset + (size_t)payload_len;
    return FrameParseStatus::ok;
}

static int push_parsed_frame(MobiusState* state, const ParsedFrame& frame) {
    mobius_stack_pushNewTable(state, 14);
    int tbl = mobius_stack_size(state) - 1;
    set_bool_field(state, tbl, "fin", frame.fin);
    set_bool_field(state, tbl, "rsv1", frame.rsv1);
    set_bool_field(state, tbl, "rsv2", frame.rsv2);
    set_bool_field(state, tbl, "rsv3", frame.rsv3);
    set_bool_field(state, tbl, "masked", frame.masked);
    set_int_field(state, tbl, "opcode", frame.opcode);
    set_string_field(state, tbl, "type", type_from_opcode(frame.opcode));
    set_int_field(state, tbl, "payload_length", (int64_t)frame.payload.size());
    set_int_field(state, tbl, "frame_length", (int64_t)frame.frame_length);
    set_buffer_field(state, tbl, "payload", frame.payload);
    if (frame.masked) set_buffer_field(state, tbl, "masking_key", frame.masking_key);
    if (is_text_opcode(frame.opcode) && is_valid_utf8(frame.payload.data(), frame.payload.size())) {
        set_string_field(state, tbl, "text", std::string(frame.payload.begin(), frame.payload.end()));
    }
    if (is_close_opcode(frame.opcode) && frame.payload.size() >= 2) {
        int64_t close_code = ((int64_t)frame.payload[0] << 8) | (int64_t)frame.payload[1];
        set_int_field(state, tbl, "close_code", close_code);
        if (frame.payload.size() > 2 && is_valid_utf8(frame.payload.data() + 2, frame.payload.size() - 2)) {
            set_string_field(state, tbl, "close_reason", std::string(frame.payload.begin() + 2, frame.payload.end()));
        }
    }
    return 1;
}

static int websocket_accept_key_fn(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "websocket.accept_key() expects 1 argument");
    if (!mobius_stack_isString(state, -1)) return mobius_error(state, "websocket.accept_key() expects a string argument");
    std::string key = mobius_stack_asString(state, -1);
    mobius_stack_pop(state, 1);
    std::string accept = websocket_accept_key(key);
    mobius_stack_pushString(state, accept.c_str());
    return 1;
}

static int websocket_is_upgrade_request(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "websocket.is_upgrade_request() expects 1 argument");
    if (!mobius_stack_isTable(state, -1)) return mobius_error(state, "websocket.is_upgrade_request() expects a table argument");
    int request_tbl = mobius_stack_size(state) - 1;
    bool ok = request_is_upgrade(state, request_tbl);
    mobius_stack_pop(state, 1);
    mobius_stack_pushBool(state, ok);
    return 1;
}

static int websocket_handshake_response(MobiusState* state, int arg_count) {
    if (arg_count < 1 || arg_count > 2) return mobius_error(state, "websocket.handshake_response() expects 1 or 2 arguments");
    if (!mobius_stack_isTable(state, -arg_count)) return mobius_error(state, "websocket.handshake_response() expects a request table");
    if (arg_count == 2 && !mobius_stack_isString(state, -1)) return mobius_error(state, "websocket.handshake_response() protocol must be a string");

    std::string protocol = arg_count == 2 ? mobius_stack_asString(state, -1) : "";
    int request_tbl = mobius_stack_size(state) - arg_count;
    if (!request_is_upgrade(state, request_tbl)) return mobius_error(state, "websocket.handshake_response() invalid websocket upgrade request");

    std::string client_key = header_lookup_from_request(state, request_tbl, "sec-websocket-key");

    mobius_stack_pop(state, arg_count);
    mobius_stack_pushNewTable(state, 4);
    int tbl = mobius_stack_size(state) - 1;
    set_string_field(state, tbl, "version", "HTTP/1.1");
    set_int_field(state, tbl, "status", 101);
    set_string_field(state, tbl, "reason", "Switching Protocols");

    mobius_stack_pushNewTable(state, 4);
    int headers_tbl = mobius_stack_size(state) - 1;
    set_string_field(state, headers_tbl, "upgrade", "websocket");
    set_string_field(state, headers_tbl, "connection", "Upgrade");
    set_string_field(state, headers_tbl, "sec-websocket-accept", websocket_accept_key(client_key));
    if (!protocol.empty()) {
        set_string_field(state, headers_tbl, "sec-websocket-protocol", protocol);
    }
    mobius_stack_setTableField(state, tbl, "headers");
    return 1;
}

static int websocket_handshake_request(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "websocket.handshake_request() expects 1 argument");
    if (!mobius_stack_isTable(state, -1)) return mobius_error(state, "websocket.handshake_request() expects a table argument");

    int tbl = mobius_stack_size(state) - 1;
    std::string host = get_optional_string_field(state, tbl, "host");
    std::string path = get_optional_string_field(state, tbl, "path");
    std::string query = get_optional_string_field(state, tbl, "query");
    std::string origin = get_optional_string_field(state, tbl, "origin");
    std::string protocol = get_optional_string_field(state, tbl, "protocol");
    std::string version = get_optional_string_field(state, tbl, "version");
    std::string key = get_optional_string_field(state, tbl, "key");

    if (host.empty()) {
        mobius_stack_pop(state, 1);
        return mobius_error(state, "websocket.handshake_request() host is required");
    }
    if (path.empty()) path = "/";
    if (version.empty()) version = "HTTP/1.1";
    if (!query.empty()) path += "?" + query;

    if (key.empty()) {
        uint8_t bytes[16];
        if (!secure_random_fill(bytes, sizeof(bytes))) {
            mobius_stack_pop(state, 1);
            return mobius_error(state, "websocket.handshake_request() secure random generation failed");
        }
        key = base64_encode_bytes(bytes, sizeof(bytes));
    }

    std::string request = "GET " + path + " " + version + "\r\n";
    request += "Host: " + host + "\r\n";
    request += "Upgrade: websocket\r\n";
    request += "Connection: Upgrade\r\n";
    request += "Sec-WebSocket-Version: 13\r\n";
    request += "Sec-WebSocket-Key: " + key + "\r\n";
    if (!origin.empty()) request += "Origin: " + origin + "\r\n";
    if (!protocol.empty()) request += "Sec-WebSocket-Protocol: " + protocol + "\r\n";
    request += "\r\n";

    mobius_stack_pop(state, 1);
    mobius_stack_pushNewTable(state, 3);
    int out_tbl = mobius_stack_size(state) - 1;
    set_string_field(state, out_tbl, "request", request);
    set_string_field(state, out_tbl, "key", key);
    set_string_field(state, out_tbl, "path", path);
    return 1;
}

static int websocket_build_frame(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "websocket.build_frame() expects 1 argument");
    if (!mobius_stack_isTable(state, -1)) return mobius_error(state, "websocket.build_frame() expects a table argument");

    int tbl = mobius_stack_size(state) - 1;
    std::string error;
    std::string type;
    std::string text;
    std::string close_reason;
    std::vector<uint8_t> payload_bytes;
    std::vector<uint8_t> masking_key_bytes;
    int type_state = get_optional_string_bytes_field(state, tbl, "type", type, error, "websocket.build_frame()");
    if (type_state < 0) {
        mobius_stack_pop(state, 1);
        return mobius_error(state, error.c_str());
    }
    int text_state = get_optional_string_bytes_field(state, tbl, "text", text, error, "websocket.build_frame()");
    if (text_state < 0) {
        mobius_stack_pop(state, 1);
        return mobius_error(state, error.c_str());
    }
    int payload_state = get_optional_buffer_field(state, tbl, "payload", payload_bytes, error, "websocket.build_frame()");
    if (payload_state < 0) {
        mobius_stack_pop(state, 1);
        return mobius_error(state, error.c_str());
    }
    int masking_key_state = get_optional_buffer_field(state, tbl, "masking_key", masking_key_bytes, error, "websocket.build_frame()");
    if (masking_key_state < 0) {
        mobius_stack_pop(state, 1);
        return mobius_error(state, error.c_str());
    }
    int close_reason_state = get_optional_string_bytes_field(state, tbl, "close_reason", close_reason, error, "websocket.build_frame()");
    if (close_reason_state < 0) {
        mobius_stack_pop(state, 1);
        return mobius_error(state, error.c_str());
    }
    bool fin = get_optional_bool_field(state, tbl, "fin", true);
    bool mask = get_optional_bool_field(state, tbl, "mask", false);
    bool rsv1 = get_optional_bool_field(state, tbl, "rsv1", false);
    bool rsv2 = get_optional_bool_field(state, tbl, "rsv2", false);
    bool rsv3 = get_optional_bool_field(state, tbl, "rsv3", false);

    int64_t opcode_value = 0;
    int64_t close_code_value = 0;
    bool has_close_code = get_optional_int_field(state, tbl, "close_code", close_code_value);
    int opcode = -1;
    if (get_optional_int_field(state, tbl, "opcode", opcode_value)) {
        opcode = (int)opcode_value;
    } else if (!type.empty()) {
        opcode = opcode_from_type(type);
    } else if (text_state > 0) {
        opcode = 0x1;
    } else if (has_close_code || close_reason_state > 0) {
        opcode = 0x8;
    } else {
        opcode = 0x2;
    }
    if (opcode < 0 || opcode > 0xF) {
        mobius_stack_pop(state, 1);
        return mobius_error(state, "websocket.build_frame() invalid opcode");
    }

    if (masking_key_state > 0) {
        mask = true;
    }

    if (text_state > 0 && payload_state > 0) {
        mobius_stack_pop(state, 1);
        return mobius_error(state, "websocket.build_frame() accepts either text or payload, not both");
    }

    if (is_close_opcode(opcode)) {
        if (text_state > 0) {
            mobius_stack_pop(state, 1);
            return mobius_error(state, "websocket.build_frame() close frames use close_reason, not text");
        }
        if (payload_state > 0 && (has_close_code || close_reason_state > 0)) {
            mobius_stack_pop(state, 1);
            return mobius_error(state, "websocket.build_frame() close frames cannot combine payload with close_code/close_reason");
        }
        if (close_reason_state > 0 && !has_close_code) {
            mobius_stack_pop(state, 1);
            return mobius_error(state, "websocket.build_frame() close_reason requires close_code");
        }
        if (close_reason_state > 0 && !is_valid_utf8(reinterpret_cast<const uint8_t*>(close_reason.data()), close_reason.size())) {
            mobius_stack_pop(state, 1);
            return mobius_error(state, "websocket.build_frame() close_reason must be valid UTF-8");
        }
        if (has_close_code) {
            if (close_code_value < 0 || close_code_value > 0xFFFF) {
                mobius_stack_pop(state, 1);
                return mobius_error(state, "websocket.build_frame() close_code must fit in 16 bits");
            }
            payload_bytes.push_back((uint8_t)((close_code_value >> 8) & 0xFF));
            payload_bytes.push_back((uint8_t)(close_code_value & 0xFF));
            payload_bytes.insert(payload_bytes.end(), close_reason.begin(), close_reason.end());
        } else if (payload_state > 0 && payload_bytes.size() == 1) {
            mobius_stack_pop(state, 1);
            return mobius_error(state, "websocket.build_frame() close payload cannot be 1 byte");
        }
    } else {
        if (has_close_code || close_reason_state > 0) {
            mobius_stack_pop(state, 1);
            return mobius_error(state, "websocket.build_frame() close_code/close_reason are only valid for close frames");
        }
        if (text_state > 0) {
            if (!is_valid_utf8(reinterpret_cast<const uint8_t*>(text.data()), text.size())) {
                mobius_stack_pop(state, 1);
                return mobius_error(state, "websocket.build_frame() text must be valid UTF-8");
            }
            payload_bytes.assign(text.begin(), text.end());
        }
    }

    if (is_control_opcode(opcode)) {
        if (!fin) {
            mobius_stack_pop(state, 1);
            return mobius_error(state, "websocket.build_frame() control frames must set fin=true");
        }
        if (payload_bytes.size() > 125) {
            mobius_stack_pop(state, 1);
            return mobius_error(state, "websocket.build_frame() control frame payloads must be <= 125 bytes");
        }
    }

    uint8_t masking_key[4] = {0, 0, 0, 0};
    if (mask) {
        if (masking_key_state > 0) {
            if (masking_key_bytes.size() != 4) {
                mobius_stack_pop(state, 1);
                return mobius_error(state, "websocket.build_frame() masking_key must be a 4-byte buffer");
            }
            memcpy(masking_key, masking_key_bytes.data(), 4);
        } else if (!secure_random_fill(masking_key, sizeof(masking_key))) {
            mobius_stack_pop(state, 1);
            return mobius_error(state, "websocket.build_frame() secure random generation failed");
        }
    }

    std::vector<uint8_t> frame;
    frame.reserve(payload_bytes.size() + 16);
    uint8_t b0 = (uint8_t)((fin ? 0x80 : 0x00) |
                           (rsv1 ? 0x40 : 0x00) |
                           (rsv2 ? 0x20 : 0x00) |
                           (rsv3 ? 0x10 : 0x00) |
                           (opcode & 0x0F));
    frame.push_back(b0);

    uint64_t len = payload_bytes.size();
    if (len < 126) {
        frame.push_back((uint8_t)((mask ? 0x80 : 0x00) | (uint8_t)len));
    } else if (len <= 0xFFFF) {
        frame.push_back((uint8_t)((mask ? 0x80 : 0x00) | 126));
        frame.push_back((uint8_t)((len >> 8) & 0xFF));
        frame.push_back((uint8_t)(len & 0xFF));
    } else {
        frame.push_back((uint8_t)((mask ? 0x80 : 0x00) | 127));
        for (int i = 7; i >= 0; i--) frame.push_back((uint8_t)(len >> (i * 8)));
    }

    if (mask) {
        frame.insert(frame.end(), masking_key, masking_key + 4);
    }

    if (mask) {
        for (size_t i = 0; i < payload_bytes.size(); i++) {
            frame.push_back((uint8_t)(payload_bytes[i] ^ masking_key[i % 4]));
        }
    } else {
        frame.insert(frame.end(), payload_bytes.begin(), payload_bytes.end());
    }

    mobius_stack_pop(state, 1);
    mobius_stack_pushBufferCopy(state, frame.data(), frame.size());
    return 1;
}

static int websocket_parse_frame(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "websocket.parse_frame() expects 1 argument");
    if (!mobius_stack_isBuffer(state, -1)) return mobius_error(state, "websocket.parse_frame() expects a buffer argument");

    size_t input_size = 0;
    void* input_data = mobius_stack_getBufferData(state, -1, &input_size);
    mobius_stack_pop(state, 1);
    const uint8_t* input_bytes = static_cast<const uint8_t*>(input_data);
    std::vector<uint8_t> bytes;
    if (input_size > 0) bytes.assign(input_bytes, input_bytes + input_size);
    ParsedFrame frame;
    std::string error;
    FrameParseStatus status = parse_frame_bytes(bytes.data(), bytes.size(), frame, error);
    if (status == FrameParseStatus::need_more) return mobius_error(state, "websocket.parse_frame() truncated frame");
    if (status == FrameParseStatus::error) return mobius_error(state, ("websocket.parse_frame() " + error).c_str());
    return push_parsed_frame(state, frame);
}

static int websocket_try_parse_frame(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "websocket.__try_parse_frame() expects 1 argument");
    if (!mobius_stack_isBuffer(state, -1)) return mobius_error(state, "websocket.__try_parse_frame() expects a buffer argument");

    size_t input_size = 0;
    void* input_data = mobius_stack_getBufferData(state, -1, &input_size);
    mobius_stack_pop(state, 1);
    const uint8_t* input_bytes = static_cast<const uint8_t*>(input_data);
    std::vector<uint8_t> bytes;
    if (input_size > 0) bytes.assign(input_bytes, input_bytes + input_size);
    ParsedFrame frame;
    std::string error;
    FrameParseStatus status = parse_frame_bytes(bytes.data(), bytes.size(), frame, error);
    if (status == FrameParseStatus::need_more) {
        mobius_stack_pushNil(state);
        return 1;
    }
    if (status == FrameParseStatus::error) return mobius_error(state, ("websocket.__try_parse_frame() " + error).c_str());
    return push_parsed_frame(state, frame);
}

} // namespace

static int init_websocket_plugin(MobiusState* /*state*/) { return 0; }
static void cleanup_websocket_plugin(void) {}

static MobiusPluginFunction websocket_functions[] = {
    {"accept_key",         websocket_accept_key_fn,        1,          MOBIUS_VAL_STRING,  "Compute a Sec-WebSocket-Accept value from a client key"},
    {"is_upgrade_request", websocket_is_upgrade_request,   1,          MOBIUS_VAL_BOOL,    "Check whether an HTTP request table is a websocket upgrade request"},
    {"handshake_request",  websocket_handshake_request,    1,          MOBIUS_VAL_TABLE,   "Build a websocket client handshake request"},
    {"handshake_response", websocket_handshake_response,   SIZE_MAX,   MOBIUS_VAL_TABLE,   "Build a websocket server handshake response table"},
    {"build_frame",        websocket_build_frame,          1,          MOBIUS_VAL_BUFFER,  "Build a websocket frame and return encoded bytes as a buffer"},
    {"parse_frame",        websocket_parse_frame,          1,          MOBIUS_VAL_TABLE,   "Parse a websocket frame buffer into structured metadata"},
    {"__try_parse_frame",  websocket_try_parse_frame,      1,          MOBIUS_VAL_UNKNOWN, "Internal websocket frame parser returning nil when incomplete"},
};

static const char* websocket_depends_on[] = {
    "socket",
    "http",
};

static MobiusPlugin websocket_plugin = {
    .metadata = {
        .name = "websocket",
        .version = "1.0.0",
        .description = "Dependency-free websocket handshake, frame, and client transport helpers",
        .author = "Mobius Team",
        .api_version = MOBIUS_PLUGIN_API_VERSION,
        .license = "MIT",
        .depends_on = websocket_depends_on,
        .depends_on_count = sizeof(websocket_depends_on) / sizeof(websocket_depends_on[0]),
    },
    .functions = websocket_functions,
    .function_count = sizeof(websocket_functions) / sizeof(websocket_functions[0]),
    .init_plugin = init_websocket_plugin,
    .cleanup_plugin = cleanup_websocket_plugin,
    .post_init = nullptr,
};

extern "C" MOBIUS_PLUGIN_EXPORT MobiusPlugin* mobius_plugin_info(void) {
    return &websocket_plugin;
}
