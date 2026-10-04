// Linux and macOS implementation of module_platform.h.

#include "module_platform.h"

#include <cstdio>

bool module_platform_localtime(int64_t timestamp, struct tm* out) {
    time_t t = (time_t)timestamp;
    return localtime_r(&t, out) != nullptr;
}

bool module_platform_gmtime(int64_t timestamp, struct tm* out) {
    time_t t = (time_t)timestamp;
    return gmtime_r(&t, out) != nullptr;
}

int64_t module_platform_timegm(struct tm* value) {
    return (int64_t)timegm(value);
}

FILE* module_platform_fopen(const std::string& path, const char* mode) {
    return fopen(path.c_str(), mode);
}

bool module_platform_random(uint8_t* data, size_t len) {
    if (len == 0) return true;
    FILE* fp = fopen("/dev/urandom", "rb");
    if (!fp) return false;
    size_t got = fread(data, 1, len, fp);
    fclose(fp);
    return got == len;
}
