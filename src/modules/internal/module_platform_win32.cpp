// Windows implementation of module_platform.h.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>

#include "module_platform.h"

#pragma comment(lib, "bcrypt.lib")

bool module_platform_localtime(int64_t timestamp, struct tm* out) {
    __time64_t t = (__time64_t)timestamp;
    return _localtime64_s(out, &t) == 0;
}

bool module_platform_gmtime(int64_t timestamp, struct tm* out) {
    __time64_t t = (__time64_t)timestamp;
    return _gmtime64_s(out, &t) == 0;
}

int64_t module_platform_timegm(struct tm* value) {
    return (int64_t)_mkgmtime64(value);
}

static std::wstring widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

FILE* module_platform_fopen(const std::string& path, const char* mode) {
    return _wfopen(widen(path).c_str(), widen(mode).c_str());
}

bool module_platform_random(uint8_t* data, size_t len) {
    while (len > 0) {
        ULONG chunk = len > 0x7FFFFFFF ? 0x7FFFFFFF : (ULONG)len;
        if (BCryptGenRandom(nullptr, data, chunk, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) return false;
        data += chunk;
        len -= chunk;
    }
    return true;
}
