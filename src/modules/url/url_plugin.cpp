#include <mobius/mobius_plugin.h>

#include <cctype>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

static bool is_unreserved(unsigned char c) {
    return std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~';
}

static std::string percent_encode(const std::string& input) {
    std::string out;
    char buf[4];
    for (unsigned char c : input) {
        if (is_unreserved(c)) {
            out.push_back((char)c);
        } else {
            snprintf(buf, sizeof(buf), "%%%02X", c);
            out.append(buf);
        }
    }
    return out;
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool percent_decode(const std::string& input, std::string& out, bool plus_to_space = false) {
    out.clear();
    for (size_t i = 0; i < input.size(); i++) {
        char c = input[i];
        if (plus_to_space && c == '+') {
            out.push_back(' ');
        } else if (c == '%') {
            if (i + 2 >= input.size()) return false;
            int hi = hex_value(input[i + 1]);
            int lo = hex_value(input[i + 2]);
            if (hi < 0 || lo < 0) return false;
            out.push_back((char)((hi << 4) | lo));
            i += 2;
        } else {
            out.push_back(c);
        }
    }
    return true;
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

static int url_encode(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "url.encode() expects 1 argument");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "url.encode() expects a string argument");
    std::string input = mobius_stack_asString(state, -1);
    mobius_stack_pop(state, 1);
    std::string out = percent_encode(input);
    mobius_stack_pushString(state, out.c_str());
    return 1;
}

static int url_decode(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "url.decode() expects 1 argument");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "url.decode() expects a string argument");
    std::string input = mobius_stack_asString(state, -1);
    mobius_stack_pop(state, 1);
    std::string out;
    if (!percent_decode(input, out, false))
        return mobius_error(state, "url.decode() invalid percent-escape");
    mobius_stack_pushString(state, out.c_str());
    return 1;
}

static int url_parse_query(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "url.parse_query() expects 1 argument");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "url.parse_query() expects a string argument");
    std::string query = mobius_stack_asString(state, -1);
    mobius_stack_pop(state, 1);

    mobius_stack_pushNewTable(state, 8);
    int tbl = mobius_stack_size(state) - 1;

    size_t start = 0;
    while (start <= query.size()) {
        size_t end = query.find('&', start);
        std::string pair = query.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!pair.empty()) {
            size_t eq = pair.find('=');
            std::string raw_key = pair.substr(0, eq);
            std::string raw_value = eq == std::string::npos ? "" : pair.substr(eq + 1);
            std::string key, value;
            if (!percent_decode(raw_key, key, true) || !percent_decode(raw_value, value, true)) {
                return mobius_error(state, "url.parse_query() invalid percent-escape");
            }
            mobius_stack_pushString(state, value.c_str());
            mobius_stack_setTableField(state, tbl, key.c_str());
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }

    return 1;
}

static int url_build_query(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "url.build_query() expects 1 argument");
    if (!mobius_stack_isTable(state, -1))
        return mobius_error(state, "url.build_query() expects a table argument");

    int tbl = mobius_stack_size(state) - 1;
    std::vector<std::string> keys;
    collect_sorted_string_keys(state, tbl, keys);

    std::string out;
    bool first = true;
    for (const std::string& key : keys) {
        mobius_stack_getTableField(state, tbl, key.c_str());
        if (mobius_stack_isNil(state, -1)) {
            mobius_stack_pop(state, 1);
            continue;
        }
        const char* value = mobius_stack_asString(state, -1);
        if (!first) out.push_back('&');
        out.append(percent_encode(key));
        out.push_back('=');
        out.append(percent_encode(value ? value : ""));
        mobius_stack_pop(state, 1);
        first = false;
    }

    mobius_stack_pop(state, 1);
    mobius_stack_pushString(state, out.c_str());
    return 1;
}

static int url_parse(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "url.parse() expects 1 argument");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "url.parse() expects a string argument");
    std::string input = mobius_stack_asString(state, -1);
    mobius_stack_pop(state, 1);

    std::string scheme, username, password, host, path, query, fragment;
    bool has_authority = false;
    int64_t port = -1;

    size_t scheme_pos = input.find(':');
    size_t first_delim = input.find_first_of("/?#");
    std::string rest = input;
    if (scheme_pos != std::string::npos && (first_delim == std::string::npos || scheme_pos < first_delim)) {
        scheme = input.substr(0, scheme_pos);
        rest = input.substr(scheme_pos + 1);
    }

    if (rest.rfind("//", 0) == 0) {
        has_authority = true;
        rest = rest.substr(2);
        size_t authority_end = rest.find_first_of("/?#");
        std::string authority = rest.substr(0, authority_end);
        rest = authority_end == std::string::npos ? "" : rest.substr(authority_end);

        size_t at = authority.rfind('@');
        std::string hostport = authority;
        if (at != std::string::npos) {
            std::string userinfo = authority.substr(0, at);
            hostport = authority.substr(at + 1);
            size_t colon = userinfo.find(':');
            if (colon == std::string::npos) {
                username = userinfo;
            } else {
                username = userinfo.substr(0, colon);
                password = userinfo.substr(colon + 1);
            }
        }

        if (!hostport.empty() && hostport.front() == '[') {
            size_t close = hostport.find(']');
            if (close == std::string::npos) {
                return mobius_error(state, "url.parse() invalid IPv6 host");
            }
            host = hostport.substr(0, close + 1);
            if (close + 1 < hostport.size() && hostport[close + 1] == ':') {
                port = strtoll(hostport.c_str() + close + 2, nullptr, 10);
            }
        } else {
            size_t colon = hostport.rfind(':');
            if (colon != std::string::npos && hostport.find(':') == colon) {
                host = hostport.substr(0, colon);
                if (colon + 1 < hostport.size()) {
                    port = strtoll(hostport.c_str() + colon + 1, nullptr, 10);
                }
            } else {
                host = hostport;
            }
        }
    }

    size_t query_pos = rest.find('?');
    size_t frag_pos = rest.find('#');
    if (query_pos == std::string::npos && frag_pos == std::string::npos) {
        path = rest;
    } else if (query_pos != std::string::npos && (frag_pos == std::string::npos || query_pos < frag_pos)) {
        path = rest.substr(0, query_pos);
        query = frag_pos == std::string::npos ? rest.substr(query_pos + 1)
                                              : rest.substr(query_pos + 1, frag_pos - query_pos - 1);
        if (frag_pos != std::string::npos) fragment = rest.substr(frag_pos + 1);
    } else {
        path = rest.substr(0, frag_pos);
        fragment = rest.substr(frag_pos + 1);
    }

    mobius_stack_pushNewTable(state, 10);
    int tbl = mobius_stack_size(state) - 1;
    mobius_stack_pushString(state, input.c_str());
    mobius_stack_setTableField(state, tbl, "href");
    mobius_stack_pushString(state, scheme.c_str());
    mobius_stack_setTableField(state, tbl, "scheme");
    mobius_stack_pushString(state, username.c_str());
    mobius_stack_setTableField(state, tbl, "username");
    mobius_stack_pushString(state, password.c_str());
    mobius_stack_setTableField(state, tbl, "password");
    mobius_stack_pushString(state, host.c_str());
    mobius_stack_setTableField(state, tbl, "host");
    if (port >= 0) {
        mobius_stack_pushInt64(state, port);
        mobius_stack_setTableField(state, tbl, "port");
    }
    mobius_stack_pushString(state, path.c_str());
    mobius_stack_setTableField(state, tbl, "path");
    mobius_stack_pushString(state, query.c_str());
    mobius_stack_setTableField(state, tbl, "query");
    mobius_stack_pushString(state, fragment.c_str());
    mobius_stack_setTableField(state, tbl, "fragment");
    mobius_stack_pushBool(state, has_authority);
    mobius_stack_setTableField(state, tbl, "has_authority");
    return 1;
}

static int url_build(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "url.build() expects 1 argument");
    if (!mobius_stack_isTable(state, -1))
        return mobius_error(state, "url.build() expects a table argument");

    int tbl = mobius_stack_size(state) - 1;
    auto get_string_field = [&](const char* key) -> std::string {
        mobius_stack_getTableField(state, tbl, key);
        std::string value;
        if (!mobius_stack_isNil(state, -1)) {
            const char* s = mobius_stack_asString(state, -1);
            if (s) value = s;
        }
        mobius_stack_pop(state, 1);
        return value;
    };

    std::string scheme = get_string_field("scheme");
    std::string username = get_string_field("username");
    std::string password = get_string_field("password");
    std::string host = get_string_field("host");
    std::string path = get_string_field("path");
    std::string query = get_string_field("query");
    std::string fragment = get_string_field("fragment");
    int64_t port = -1;
    mobius_stack_getTableField(state, tbl, "port");
    if (!mobius_stack_isNil(state, -1)) {
        if (!mobius_stack_isInteger(state, -1)) {
            return mobius_error(state, "url.build() field 'port' must be an integer");
        }
        port = mobius_stack_asInt64(state, -1);
    }
    mobius_stack_pop(state, 1);

    std::string out;
    if (!scheme.empty()) {
        out.append(scheme);
        out.push_back(':');
    }
    if (!host.empty()) {
        out.append("//");
        if (!username.empty()) {
            out.append(username);
            if (!password.empty()) {
                out.push_back(':');
                out.append(password);
            }
            out.push_back('@');
        }
        out.append(host);
        if (port >= 0) {
            char buf[32];
            snprintf(buf, sizeof(buf), ":%" PRId64, port);
            out.append(buf);
        }
    }
    if (!path.empty()) {
        if (!host.empty() && path.front() != '/') out.push_back('/');
        out.append(path);
    } else if (!host.empty() && (!query.empty() || !fragment.empty())) {
        out.push_back('/');
    }
    if (!query.empty()) {
        out.push_back('?');
        out.append(query);
    }
    if (!fragment.empty()) {
        out.push_back('#');
        out.append(fragment);
    }

    mobius_stack_pop(state, 1);
    mobius_stack_pushString(state, out.c_str());
    return 1;
}

static int init_url_plugin(MobiusState* /*state*/) { return 0; }
static void cleanup_url_plugin(void) {}

static MobiusPluginFunction url_functions[] = {
    {"parse",       url_parse,       1, MOBIUS_VAL_TABLE,  "Parse a URL into components"},
    {"build",       url_build,       1, MOBIUS_VAL_STRING, "Build a URL string from components"},
    {"encode",      url_encode,      1, MOBIUS_VAL_STRING, "Percent-encode a URL component"},
    {"decode",      url_decode,      1, MOBIUS_VAL_STRING, "Percent-decode a URL component"},
    {"parse_query", url_parse_query, 1, MOBIUS_VAL_TABLE,  "Parse a query string into a table"},
    {"build_query", url_build_query, 1, MOBIUS_VAL_STRING, "Build a query string from a table"},
};

static MobiusPlugin url_plugin = {
    .metadata = {
        .name = "url",
        .version = "1.0.0",
        .description = "URL parsing and encoding helpers",
        .author = "Mobius Team",
        .api_version = MOBIUS_PLUGIN_API_VERSION,
        .license = "MIT"
    },
    .functions = url_functions,
    .function_count = sizeof(url_functions) / sizeof(url_functions[0]),
    .init_plugin = init_url_plugin,
    .cleanup_plugin = cleanup_url_plugin,
    .post_init = nullptr,
};

extern "C" MOBIUS_PLUGIN_EXPORT MobiusPlugin* mobius_plugin_info(void) {
    return &url_plugin;
}
