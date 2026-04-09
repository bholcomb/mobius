#include <mobius/mobius_plugin.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <limits>

#ifdef _WIN32
  #include <stdlib.h>
#else
  #include <unistd.h>
#endif

static uint32_t rotl32(uint32_t value, uint32_t shift) {
    return (value << shift) | (value >> (32 - shift));
}

static uint32_t rotr32(uint32_t value, uint32_t shift) {
    return (value >> shift) | (value << (32 - shift));
}

static std::string hex_encode(const uint8_t* data, size_t len) {
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

static std::string hex_encode(const std::vector<uint8_t>& data) {
    return hex_encode(data.data(), data.size());
}

static std::vector<uint8_t> sha256_bytes(const uint8_t* data, size_t len) {
    static const uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
        0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
        0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
        0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
        0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
        0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
        0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
        0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
        0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
    };

    uint32_t h[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };

    std::vector<uint8_t> msg(data, data + len);
    msg.push_back(0x80);
    while ((msg.size() % 64) != 56) msg.push_back(0);

    uint64_t bit_len = (uint64_t)len * 8;
    for (int i = 7; i >= 0; i--) msg.push_back((uint8_t)(bit_len >> (i * 8)));

    for (size_t offset = 0; offset < msg.size(); offset += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; i++) {
            size_t idx = offset + (size_t)i * 4;
            w[i] = ((uint32_t)msg[idx] << 24) | ((uint32_t)msg[idx + 1] << 16) |
                   ((uint32_t)msg[idx + 2] << 8) | (uint32_t)msg[idx + 3];
        }
        for (int i = 16; i < 64; i++) {
            uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];

        for (int i = 0; i < 64; i++) {
            uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
            uint32_t ch = (e & f) ^ ((~e) & g);
            uint32_t temp1 = hh + S1 + ch + K[i] + w[i];
            uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t temp2 = S0 + maj;

            hh = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    std::vector<uint8_t> out(32);
    for (int i = 0; i < 8; i++) {
        out[(size_t)i * 4] = (uint8_t)(h[i] >> 24);
        out[(size_t)i * 4 + 1] = (uint8_t)(h[i] >> 16);
        out[(size_t)i * 4 + 2] = (uint8_t)(h[i] >> 8);
        out[(size_t)i * 4 + 3] = (uint8_t)h[i];
    }
    return out;
}

static std::vector<uint8_t> sha1_bytes(const uint8_t* data, size_t len) {
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

static std::vector<uint8_t> md5_bytes(const uint8_t* data, size_t len) {
    static const uint32_t S[64] = {
        7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
        5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
        4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
        6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21
    };
    static const uint32_t K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
        0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
        0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
        0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
        0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
        0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
        0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
        0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
        0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
    };

    uint32_t a0 = 0x67452301;
    uint32_t b0 = 0xefcdab89;
    uint32_t c0 = 0x98badcfe;
    uint32_t d0 = 0x10325476;

    std::vector<uint8_t> msg(data, data + len);
    msg.push_back(0x80);
    while ((msg.size() % 64) != 56) msg.push_back(0);

    uint64_t bit_len = (uint64_t)len * 8;
    for (int i = 0; i < 8; i++) msg.push_back((uint8_t)(bit_len >> (i * 8)));

    for (size_t offset = 0; offset < msg.size(); offset += 64) {
        uint32_t M[16];
        for (int i = 0; i < 16; i++) {
            size_t idx = offset + (size_t)i * 4;
            M[i] = (uint32_t)msg[idx] | ((uint32_t)msg[idx + 1] << 8) |
                   ((uint32_t)msg[idx + 2] << 16) | ((uint32_t)msg[idx + 3] << 24);
        }

        uint32_t A = a0, B = b0, C = c0, D = d0;
        for (int i = 0; i < 64; i++) {
            uint32_t F = 0;
            uint32_t g = 0;
            if (i < 16) {
                F = (B & C) | ((~B) & D);
                g = (uint32_t)i;
            } else if (i < 32) {
                F = (D & B) | ((~D) & C);
                g = (uint32_t)((5 * i + 1) % 16);
            } else if (i < 48) {
                F = B ^ C ^ D;
                g = (uint32_t)((3 * i + 5) % 16);
            } else {
                F = C ^ (B | (~D));
                g = (uint32_t)((7 * i) % 16);
            }

            uint32_t temp = D;
            D = C;
            C = B;
            B = B + rotl32(A + F + K[i] + M[g], S[i]);
            A = temp;
        }

        a0 += A;
        b0 += B;
        c0 += C;
        d0 += D;
    }

    std::vector<uint8_t> out(16);
    uint32_t hs[4] = {a0, b0, c0, d0};
    for (int i = 0; i < 4; i++) {
        out[(size_t)i * 4] = (uint8_t)hs[i];
        out[(size_t)i * 4 + 1] = (uint8_t)(hs[i] >> 8);
        out[(size_t)i * 4 + 2] = (uint8_t)(hs[i] >> 16);
        out[(size_t)i * 4 + 3] = (uint8_t)(hs[i] >> 24);
    }
    return out;
}

static uint32_t crc32_value(const uint8_t* data, size_t len) {
    static bool initialized = false;
    static uint32_t table[256];
    if (!initialized) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int j = 0; j < 8; j++) {
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            table[i] = c;
        }
        initialized = true;
    }

    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

static bool secure_random_fill(uint8_t* data, size_t len) {
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

static bool secure_random_u64(uint64_t* out) {
    return secure_random_fill(reinterpret_cast<uint8_t*>(out), sizeof(*out));
}

static std::string base64_encode_bytes(const uint8_t* data, size_t len) {
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

static bool base64_decode_string(const std::string& input, std::string& out) {
    int table[256];
    for (int i = 0; i < 256; i++) table[i] = -1;
    for (int i = 0; i < 26; i++) { table['A' + i] = i; table['a' + i] = 26 + i; }
    for (int i = 0; i < 10; i++) table['0' + i] = 52 + i;
    table[(unsigned char)'+'] = 62;
    table[(unsigned char)'/'] = 63;

    std::vector<int> vals;
    vals.reserve(input.size());
    for (unsigned char c : input) {
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
        if (c == '=') vals.push_back(-2);
        else if (table[c] >= 0) vals.push_back(table[c]);
        else return false;
    }

    if (vals.size() % 4 != 0) return false;
    out.clear();
    for (size_t i = 0; i < vals.size(); i += 4) {
        int a = vals[i], b = vals[i + 1], c = vals[i + 2], d = vals[i + 3];
        if (a < 0 || b < 0) return false;
        uint32_t n = ((uint32_t)a << 18) | ((uint32_t)b << 12);
        out.push_back((char)((n >> 16) & 0xFF));
        if (c == -2) {
            if (d != -2) return false;
            continue;
        }
        if (c < 0) return false;
        n |= (uint32_t)c << 6;
        out.push_back((char)((n >> 8) & 0xFF));
        if (d == -2) continue;
        if (d < 0) return false;
        n |= (uint32_t)d;
        out.push_back((char)(n & 0xFF));
    }
    return true;
}

static std::string uuid4_string() {
    uint8_t bytes[16];
    if (!secure_random_fill(bytes, sizeof(bytes))) return "";
    bytes[6] = (uint8_t)((bytes[6] & 0x0F) | 0x40);
    bytes[8] = (uint8_t)((bytes[8] & 0x3F) | 0x80);
    std::string hex = hex_encode(bytes, sizeof(bytes));
    std::string out;
    out.reserve(36);
    out.append(hex, 0, 8);
    out.push_back('-');
    out.append(hex, 8, 4);
    out.push_back('-');
    out.append(hex, 12, 4);
    out.push_back('-');
    out.append(hex, 16, 4);
    out.push_back('-');
    out.append(hex, 20, 12);
    return out;
}

static std::string hmac_sha256_hex(const std::string& key, const std::string& message) {
    const size_t BLOCK_SIZE = 64;
    std::vector<uint8_t> key_block(BLOCK_SIZE, 0);
    if (key.size() > BLOCK_SIZE) {
        std::vector<uint8_t> hashed = sha256_bytes(reinterpret_cast<const uint8_t*>(key.data()), key.size());
        memcpy(key_block.data(), hashed.data(), hashed.size());
    } else if (!key.empty()) {
        memcpy(key_block.data(), key.data(), key.size());
    }

    std::vector<uint8_t> o_key_pad(BLOCK_SIZE), i_key_pad(BLOCK_SIZE);
    for (size_t i = 0; i < BLOCK_SIZE; i++) {
        o_key_pad[i] = (uint8_t)(key_block[i] ^ 0x5c);
        i_key_pad[i] = (uint8_t)(key_block[i] ^ 0x36);
    }

    std::vector<uint8_t> inner(i_key_pad.begin(), i_key_pad.end());
    inner.insert(inner.end(), message.begin(), message.end());
    std::vector<uint8_t> inner_hash = sha256_bytes(inner.data(), inner.size());

    std::vector<uint8_t> outer(o_key_pad.begin(), o_key_pad.end());
    outer.insert(outer.end(), inner_hash.begin(), inner_hash.end());
    std::vector<uint8_t> final_hash = sha256_bytes(outer.data(), outer.size());
    return hex_encode(final_hash);
}

static int require_string_arg(MobiusState* state, int arg_count, const char* name, const char** out) {
    char error[128];
    if (arg_count != 1) {
        snprintf(error, sizeof(error), "%s expects exactly 1 argument", name);
        return mobius_error(state, error);
    }
    if (!mobius_stack_isString(state, -1)) {
        snprintf(error, sizeof(error), "%s expects a string argument", name);
        return mobius_error(state, error);
    }
    *out = mobius_stack_asString(state, -1);
    return 0;
}

static int crypto_sha256(MobiusState* state, int arg_count) {
    const char* text = nullptr;
    int rc = require_string_arg(state, arg_count, "sha256()", &text);
    if (rc != 0) return rc;
    std::vector<uint8_t> hash = sha256_bytes(reinterpret_cast<const uint8_t*>(text), strlen(text));
    mobius_stack_pop(state, 1);
    std::string hex = hex_encode(hash);
    mobius_stack_pushString(state, hex.c_str());
    return 1;
}

static int crypto_sha1(MobiusState* state, int arg_count) {
    const char* text = nullptr;
    int rc = require_string_arg(state, arg_count, "sha1()", &text);
    if (rc != 0) return rc;
    std::vector<uint8_t> hash = sha1_bytes(reinterpret_cast<const uint8_t*>(text), strlen(text));
    mobius_stack_pop(state, 1);
    std::string hex = hex_encode(hash);
    mobius_stack_pushString(state, hex.c_str());
    return 1;
}

static int crypto_md5(MobiusState* state, int arg_count) {
    const char* text = nullptr;
    int rc = require_string_arg(state, arg_count, "md5()", &text);
    if (rc != 0) return rc;
    std::vector<uint8_t> hash = md5_bytes(reinterpret_cast<const uint8_t*>(text), strlen(text));
    mobius_stack_pop(state, 1);
    std::string hex = hex_encode(hash);
    mobius_stack_pushString(state, hex.c_str());
    return 1;
}

static int crypto_hmac_sha256(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "hmac_sha256() expects exactly 2 arguments");
    if (!mobius_stack_isString(state, -2) || !mobius_stack_isString(state, -1)) {
        return mobius_error(state, "hmac_sha256() expects string arguments");
    }
    std::string key = mobius_stack_asString(state, -2);
    std::string msg = mobius_stack_asString(state, -1);
    mobius_stack_pop(state, 2);
    std::string out = hmac_sha256_hex(key, msg);
    mobius_stack_pushString(state, out.c_str());
    return 1;
}

static int crypto_crc32(MobiusState* state, int arg_count) {
    const char* text = nullptr;
    int rc = require_string_arg(state, arg_count, "crc32()", &text);
    if (rc != 0) return rc;
    uint32_t crc = crc32_value(reinterpret_cast<const uint8_t*>(text), strlen(text));
    mobius_stack_pop(state, 1);
    mobius_stack_pushInt64(state, (int64_t)crc);
    return 1;
}

static int crypto_base64_encode(MobiusState* state, int arg_count) {
    const char* text = nullptr;
    int rc = require_string_arg(state, arg_count, "base64_encode()", &text);
    if (rc != 0) return rc;
    std::string out = base64_encode_bytes(reinterpret_cast<const uint8_t*>(text), strlen(text));
    mobius_stack_pop(state, 1);
    mobius_stack_pushString(state, out.c_str());
    return 1;
}

static int crypto_base64_decode(MobiusState* state, int arg_count) {
    const char* text = nullptr;
    int rc = require_string_arg(state, arg_count, "base64_decode()", &text);
    if (rc != 0) return rc;
    std::string decoded;
    if (!base64_decode_string(text, decoded)) {
        return mobius_error(state, "base64_decode() invalid base64 input");
    }
    if (decoded.find('\0') != std::string::npos) {
        return mobius_error(state, "base64_decode() produced binary data with NUL bytes, unsupported by Mobius strings");
    }
    mobius_stack_pop(state, 1);
    mobius_stack_pushString(state, decoded.c_str());
    return 1;
}

static int crypto_uuid4(MobiusState* state, int arg_count) {
    (void)arg_count;
    std::string uuid = uuid4_string();
    if (uuid.empty()) return mobius_error(state, "uuid4() secure random generation failed");
    mobius_stack_pushString(state, uuid.c_str());
    return 1;
}

static int crypto_random_hex(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "random_hex() expects exactly 1 argument");
    if (!mobius_stack_isInteger(state, -1)) return mobius_error(state, "random_hex() expects an integer argument");
    int64_t bytes = mobius_stack_asInt64(state, -1);
    if (bytes < 0 || bytes > 4096) return mobius_error(state, "random_hex() byte count must be between 0 and 4096");
    std::vector<uint8_t> buf((size_t)bytes);
    if (!secure_random_fill(buf.data(), buf.size())) return mobius_error(state, "random_hex() secure random generation failed");
    mobius_stack_pop(state, 1);
    std::string out = hex_encode(buf);
    mobius_stack_pushString(state, out.c_str());
    return 1;
}

static int crypto_random_int(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "random_int() expects exactly 2 arguments");
    if (!mobius_stack_isInteger(state, -2) || !mobius_stack_isInteger(state, -1)) {
        return mobius_error(state, "random_int() expects integer arguments");
    }
    int64_t min = mobius_stack_asInt64(state, -2);
    int64_t max = mobius_stack_asInt64(state, -1);
    if (min > max) return mobius_error(state, "random_int() min must be <= max");

    uint64_t span = (uint64_t)(max - min) + 1u;
    uint64_t limit = std::numeric_limits<uint64_t>::max() - (std::numeric_limits<uint64_t>::max() % span);
    uint64_t value = 0;
    do {
        if (!secure_random_u64(&value)) return mobius_error(state, "random_int() secure random generation failed");
    } while (value >= limit);

    mobius_stack_pop(state, 2);
    mobius_stack_pushInt64(state, min + (int64_t)(value % span));
    return 1;
}

static int init_crypto_plugin(MobiusState* /*state*/) { return 0; }
static void cleanup_crypto_plugin(void) {}

static MobiusPluginFunction crypto_functions[] = {
    {"sha256",        crypto_sha256,        1, MOBIUS_VAL_STRING, "SHA-256 digest as lowercase hex"},
    {"sha1",          crypto_sha1,          1, MOBIUS_VAL_STRING, "SHA-1 digest as lowercase hex"},
    {"md5",           crypto_md5,           1, MOBIUS_VAL_STRING, "MD5 digest as lowercase hex (weak)"},
    {"hmac_sha256",   crypto_hmac_sha256,   2, MOBIUS_VAL_STRING, "HMAC-SHA256 as lowercase hex"},
    {"crc32",         crypto_crc32,         1, MOBIUS_VAL_INT64,  "CRC32 checksum as integer"},
    {"base64_encode", crypto_base64_encode, 1, MOBIUS_VAL_STRING, "Base64-encode a string"},
    {"base64_decode", crypto_base64_decode, 1, MOBIUS_VAL_STRING, "Base64-decode a string"},
    {"uuid4",         crypto_uuid4,         0, MOBIUS_VAL_STRING, "Generate a random UUIDv4"},
    {"random_hex",    crypto_random_hex,    1, MOBIUS_VAL_STRING, "Generate secure random bytes encoded as hex"},
    {"random_int",    crypto_random_int,    2, MOBIUS_VAL_INT64,  "Generate a secure random integer in [min, max]"},
};

static MobiusPlugin crypto_plugin = {
    .metadata = {
        .name = "crypto",
        .version = "1.0.0",
        .description = "Dependency-free hashing and encoding helpers",
        .author = "Mobius Team",
        .api_version = MOBIUS_PLUGIN_API_VERSION,
        .license = "MIT"
    },
    .functions = crypto_functions,
    .function_count = sizeof(crypto_functions) / sizeof(crypto_functions[0]),
    .init_plugin = init_crypto_plugin,
    .cleanup_plugin = cleanup_crypto_plugin,
    .post_init = nullptr,
};

extern "C" MOBIUS_PLUGIN_EXPORT MobiusPlugin* mobius_plugin_info(void) {
    return &crypto_plugin;
}
