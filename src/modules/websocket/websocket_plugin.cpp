#include <mobius/mobius_plugin.h>

#include "modules/net/protocol_common.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace mobius_net;

namespace {

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

static bool string_has_nul(const std::vector<uint8_t>& bytes) {
    for (uint8_t byte : bytes) {
        if (byte == 0) return true;
    }
    return false;
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
    std::string type = get_optional_string_field(state, tbl, "type");
    std::string payload = get_optional_string_field(state, tbl, "payload");
    std::string payload_hex = get_optional_string_field(state, tbl, "payload_hex");
    std::string masking_key_hex = get_optional_string_field(state, tbl, "masking_key");
    bool fin = get_optional_bool_field(state, tbl, "fin", true);
    bool mask = get_optional_bool_field(state, tbl, "mask", false);

    int64_t opcode_value = 0;
    int opcode = -1;
    if (get_optional_int_field(state, tbl, "opcode", opcode_value)) {
        opcode = (int)opcode_value;
    } else if (!type.empty()) {
        opcode = opcode_from_type(type);
    } else {
        opcode = 0x1;
    }
    if (opcode < 0 || opcode > 0xF) {
        mobius_stack_pop(state, 1);
        return mobius_error(state, "websocket.build_frame() invalid opcode");
    }

    std::vector<uint8_t> payload_bytes;
    if (!payload_hex.empty()) {
        if (!hex_decode(payload_hex, payload_bytes)) {
            mobius_stack_pop(state, 1);
            return mobius_error(state, "websocket.build_frame() payload_hex must be valid hex");
        }
    } else {
        payload_bytes.assign(payload.begin(), payload.end());
    }

    uint8_t masking_key[4] = {0, 0, 0, 0};
    if (mask) {
        if (!masking_key_hex.empty()) {
            std::vector<uint8_t> decoded_key;
            if (!hex_decode(masking_key_hex, decoded_key) || decoded_key.size() != 4) {
                mobius_stack_pop(state, 1);
                return mobius_error(state, "websocket.build_frame() masking_key must be 8 hex chars");
            }
            memcpy(masking_key, decoded_key.data(), 4);
        } else if (!secure_random_fill(masking_key, sizeof(masking_key))) {
            mobius_stack_pop(state, 1);
            return mobius_error(state, "websocket.build_frame() secure random generation failed");
        }
    }

    std::vector<uint8_t> frame;
    frame.reserve(payload_bytes.size() + 16);
    uint8_t b0 = (uint8_t)((fin ? 0x80 : 0x00) | (opcode & 0x0F));
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
    std::string out_hex = hex_encode(frame.data(), frame.size());
    mobius_stack_pushString(state, out_hex.c_str());
    return 1;
}

static int websocket_parse_frame(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "websocket.parse_frame() expects 1 argument");
    if (!mobius_stack_isString(state, -1)) return mobius_error(state, "websocket.parse_frame() expects a string argument");

    std::string frame_hex = mobius_stack_asString(state, -1);
    mobius_stack_pop(state, 1);

    std::vector<uint8_t> bytes;
    if (!hex_decode(frame_hex, bytes)) return mobius_error(state, "websocket.parse_frame() argument must be valid hex");
    if (bytes.size() < 2) return mobius_error(state, "websocket.parse_frame() frame too short");

    size_t offset = 0;
    uint8_t b0 = bytes[offset++];
    uint8_t b1 = bytes[offset++];
    bool fin = (b0 & 0x80) != 0;
    int opcode = b0 & 0x0F;
    bool masked = (b1 & 0x80) != 0;
    uint64_t payload_len = b1 & 0x7F;
    if (payload_len == 126) {
        if (offset + 2 > bytes.size()) return mobius_error(state, "websocket.parse_frame() truncated extended payload length");
        payload_len = ((uint64_t)bytes[offset] << 8) | (uint64_t)bytes[offset + 1];
        offset += 2;
    } else if (payload_len == 127) {
        if (offset + 8 > bytes.size()) return mobius_error(state, "websocket.parse_frame() truncated extended payload length");
        payload_len = 0;
        for (int i = 0; i < 8; i++) payload_len = (payload_len << 8) | (uint64_t)bytes[offset + i];
        offset += 8;
    }

    uint8_t masking_key[4] = {0, 0, 0, 0};
    std::string masking_key_str;
    if (masked) {
        if (offset + 4 > bytes.size()) return mobius_error(state, "websocket.parse_frame() truncated masking key");
        memcpy(masking_key, bytes.data() + offset, 4);
        masking_key_str = hex_encode(masking_key, 4);
        offset += 4;
    }

    if (offset + payload_len > bytes.size()) return mobius_error(state, "websocket.parse_frame() truncated payload");
    std::vector<uint8_t> payload(bytes.begin() + (ptrdiff_t)offset, bytes.begin() + (ptrdiff_t)(offset + payload_len));
    if (masked) {
        for (size_t i = 0; i < payload.size(); i++) payload[i] = (uint8_t)(payload[i] ^ masking_key[i % 4]);
    }

    mobius_stack_pushNewTable(state, 10);
    int tbl = mobius_stack_size(state) - 1;
    set_bool_field(state, tbl, "fin", fin);
    set_bool_field(state, tbl, "masked", masked);
    set_int_field(state, tbl, "opcode", opcode);
    set_string_field(state, tbl, "type", type_from_opcode(opcode));
    set_int_field(state, tbl, "payload_length", (int64_t)payload.size());
    set_string_field(state, tbl, "payload_hex", hex_encode(payload.data(), payload.size()));
    if (masked) set_string_field(state, tbl, "masking_key", masking_key_str);
    if (!string_has_nul(payload)) {
        std::string text(payload.begin(), payload.end());
        set_string_field(state, tbl, "payload", text);
    }
    if (opcode == 0x8 && payload.size() >= 2) {
        int64_t close_code = ((int64_t)payload[0] << 8) | (int64_t)payload[1];
        set_int_field(state, tbl, "close_code", close_code);
    }
    return 1;
}

} // namespace

static int init_websocket_plugin(MobiusState* /*state*/) { return 0; }
static void cleanup_websocket_plugin(void) {}

static MobiusPluginFunction websocket_functions[] = {
    {"accept_key",         websocket_accept_key_fn,        1,          MOBIUS_VAL_STRING,  "Compute a Sec-WebSocket-Accept value from a client key"},
    {"is_upgrade_request", websocket_is_upgrade_request,   1,          MOBIUS_VAL_BOOL,    "Check whether an HTTP request table is a websocket upgrade request"},
    {"handshake_request",  websocket_handshake_request,    1,          MOBIUS_VAL_TABLE,   "Build a websocket client handshake request"},
    {"handshake_response", websocket_handshake_response,   SIZE_MAX,   MOBIUS_VAL_TABLE,   "Build a websocket server handshake response table"},
    {"build_frame",        websocket_build_frame,          1,          MOBIUS_VAL_STRING,  "Build a websocket frame and return hex-encoded bytes"},
    {"parse_frame",        websocket_parse_frame,          1,          MOBIUS_VAL_TABLE,   "Parse a hex-encoded websocket frame"},
};

static MobiusPlugin websocket_plugin = {
    .metadata = {
        .name = "websocket",
        .version = "1.0.0",
        .description = "Dependency-free websocket handshake and frame helpers",
        .author = "Mobius Team",
        .api_version = MOBIUS_PLUGIN_API_VERSION,
        .license = "MIT"
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
