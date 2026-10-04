#ifndef MOBIUS_MODULES_INTERNAL_MODULE_PLATFORM_H
#define MOBIUS_MODULES_INTERNAL_MODULE_PLATFORM_H

// Small operating-system services several modules share (os, datetime,
// crypto, websocket, compression), compiled into each module that uses them:
//
//   Linux, macOS  module_platform_posix.cpp
//   Windows       module_platform_win32.cpp

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>

// Thread-safe time conversions. False if the time can't be represented.
bool module_platform_localtime(int64_t timestamp, struct tm* out);
bool module_platform_gmtime(int64_t timestamp, struct tm* out);

// The inverse of gmtime: a UTC broken-down time as a timestamp.
int64_t module_platform_timegm(struct tm* value);

// fopen with a UTF-8 path (Windows' fopen reads paths in the ANSI code
// page).
FILE* module_platform_fopen(const std::string& path, const char* mode);

// Cryptographically secure random bytes.
bool module_platform_random(uint8_t* data, size_t len);

#endif // MOBIUS_MODULES_INTERNAL_MODULE_PLATFORM_H
