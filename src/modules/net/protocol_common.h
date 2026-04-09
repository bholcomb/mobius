#ifndef MOBIUS_MODULES_NET_PROTOCOL_COMMON_H
#define MOBIUS_MODULES_NET_PROTOCOL_COMMON_H

#include <mobius/mobius_plugin.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
  #include <stdlib.h>
#else
  #include <unistd.h>
#endif

namespace mobius_net {

inline std::string trim_left(const std::string& s) {
    size_t i = 0;
    while (i < s.size() && std::isspace((unsigned char)s[i])) i++;
    return s.substr(i);
}

inline std::string trim_right(const std::string& s) {
    size_t end = s.size();
    while (end > 0 && std::isspace((unsigned char)s[end - 1])) end--;
    return s.substr(0, end);
}

inline std::string trim(const std::string& s) {
    return trim_right(trim_left(s));
}

inline std::string to_lower_copy(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = (char)std::tolower((unsigned char)c);
    return out;
}

inline bool iequals(const std::string& a, const std::string& b) {
    return to_lower_copy(a) == to_lower_copy(b);
}

inline bool header_value_has_token(const std::string& value, const char* token) {
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

inline void split_lines(const std::string& input, std::vector<std::string>& lines) {
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

inline std::string canonical_header_name(const std::string& name) {
    std::string out = to_lower_copy(name);
    bool upper = true;
    for (char& c : out) {
        if (upper && c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        upper = (c == '-');
    }
    return out;
}

inline void collect_sorted_string_keys(MobiusState* state, int tbl_idx, std::vector<std::string>& keys) {
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

inline uint32_t rotl32(uint32_t value, uint32_t shift) {
    return (value << shift) | (value >> (32 - shift));
}

inline std::vector<uint8_t> sha1_bytes(const uint8_t* data, size_t len) {
    uint32_t h0 = 0x67452301;
    uint32_t h1 = 0xEFCDAB89;
    uint32_t h2 = 0x98BADCFE;
    uint32_t h3 = 0x10325476;
    uint32_t h4 = 0xC3D2E1F0;

    std::vector<uint8_t> msg(data, data + len);
    msg.push_back(0x80);
    while ((msg.size() % 64) != 56) msg.push_back(0);

    uint64_t bit_len = (uint64_t)len * 8;
    for (int i = 7; i >= 0; i--) msg.push_back((uint8_t)(bit_len >> (i * 8)));

    for (size_t offset = 0; offset < msg.size(); offset += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++) {
            size_t idx = offset + (size_t)i * 4;
            w[i] = ((uint32_t)msg[idx] << 24) | ((uint32_t)msg[idx + 1] << 16) |
                   ((uint32_t)msg[idx + 2] << 8) | (uint32_t)msg[idx + 3];
        }
        for (int i = 16; i < 80; i++) {
            w[i] = rotl32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }

        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int i = 0; i < 80; i++) {
            uint32_t f = 0;
            uint32_t k = 0;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            uint32_t temp = rotl32(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rotl32(b, 30);
            b = a;
            a = temp;
        }

        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
    }

    std::vector<uint8_t> out(20);
    uint32_t hs[5] = {h0, h1, h2, h3, h4};
    for (int i = 0; i < 5; i++) {
        out[(size_t)i * 4] = (uint8_t)(hs[i] >> 24);
        out[(size_t)i * 4 + 1] = (uint8_t)(hs[i] >> 16);
        out[(size_t)i * 4 + 2] = (uint8_t)(hs[i] >> 8);
        out[(size_t)i * 4 + 3] = (uint8_t)hs[i];
    }
    return out;
}

inline std::string base64_encode_bytes(const uint8_t* data, size_t len) {
    static const char* TABLE = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = (uint32_t)data[i] << 16;
        if (i + 1 < len) n |= (uint32_t)data[i + 1] << 8;
        if (i + 2 < len) n |= data[i + 2];
        out.push_back(TABLE[(n >> 18) & 63]);
        out.push_back(TABLE[(n >> 12) & 63]);
        out.push_back(i + 1 < len ? TABLE[(n >> 6) & 63] : '=');
        out.push_back(i + 2 < len ? TABLE[n & 63] : '=');
    }
    return out;
}

inline std::string hex_encode(const uint8_t* data, size_t len) {
    static const char* HEX = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; i++) {
        unsigned char byte = data[i];
        out.push_back(HEX[(byte >> 4) & 0x0F]);
        out.push_back(HEX[byte & 0x0F]);
    }
    return out;
}

inline int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

inline bool hex_decode(const std::string& input, std::vector<uint8_t>& out) {
    if (input.size() % 2 != 0) return false;
    out.clear();
    out.reserve(input.size() / 2);
    for (size_t i = 0; i < input.size(); i += 2) {
        int hi = hex_value(input[i]);
        int lo = hex_value(input[i + 1]);
        if (hi < 0 || lo < 0) return false;
        out.push_back((uint8_t)((hi << 4) | lo));
    }
    return true;
}

inline bool secure_random_fill(uint8_t* data, size_t len) {
    if (len == 0) return true;
#ifdef _WIN32
    size_t offset = 0;
    while (offset < len) {
        unsigned int value = 0;
        if (rand_s(&value) != 0) return false;
        size_t remaining = len - offset;
        size_t chunk = remaining < sizeof(value) ? remaining : sizeof(value);
        memcpy(data + offset, &value, chunk);
        offset += chunk;
    }
    return true;
#else
    FILE* fp = fopen("/dev/urandom", "rb");
    if (!fp) return false;
    size_t read = fread(data, 1, len, fp);
    fclose(fp);
    return read == len;
#endif
}

} // namespace mobius_net

#endif
