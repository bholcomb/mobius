// The macOS-only parts of the os module (see os_platform_posix.h).

#include "os_platform.h"
#include "os_platform_posix.h"

#include <crt_externs.h>
#include <mach-o/dyld.h>
#include <vector>

char** posix_environ() { return *_NSGetEnviron(); }

bool os_platform_executable(std::string* path) {
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buf(size + 1, '\0');
    if (_NSGetExecutablePath(buf.data(), &size) != 0) return false;
    *path = buf.data();
    return true;
}

const char* os_platform_name() {
#if defined(__aarch64__) || defined(__arm64__)
    return "macos-aarch64";
#else
    return "macos-x86_64";
#endif
}
