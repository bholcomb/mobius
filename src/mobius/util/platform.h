#ifndef MOBIUS_UTIL_PLATFORM_H
#define MOBIUS_UTIL_PLATFORM_H

// The operating-system services the core needs, for Linux, macOS and
// Windows, so the rest of the core uses no OS headers directly.

#include <cstddef>
#include <cstdint>
#include <string>

// Monotonic clock in nanoseconds.
uint64_t platform_monotonic_ns();

size_t platform_page_size();

// The running executable's full path ("" if unknown).
std::string platform_executable_path();

// Shared libraries. `global`: make the library's symbols available to
// libraries loaded later (RTLD_GLOBAL; Windows has no equivalent and
// resolves each DLL's imports itself). On failure returns null and sets
// *error.
void* platform_library_open(const std::string& path, bool global, std::string* error);
void* platform_library_symbol(void* library, const char* name);
void  platform_library_close(void* library);

bool platform_is_regular_file(const std::string& path);
bool platform_is_directory(const std::string& path);

// The current thread's (or fiber's) stack: its lowest usable address and
// the current position, for overflow checks. False if unknown.
bool platform_stack_low(char** low);

// The address of the calling function's frame (the stack position).
#if defined(_MSC_VER) && !defined(__clang__)
#  include <intrin.h>
#  define MOBIUS_FRAME_ADDRESS() ((char*)_AddressOfReturnAddress())
#else
#  define MOBIUS_FRAME_ADDRESS() ((char*)__builtin_frame_address(0))
#endif

#endif // MOBIUS_UTIL_PLATFORM_H
