// The Linux-only parts of the os module (see os_platform_posix.h).

#include "os_platform.h"
#include "os_platform_posix.h"

#include <climits>
#include <unistd.h>

extern char** environ;

char** posix_environ() { return environ; }

bool os_platform_executable(std::string* path) {
    char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len < 0) return false;
    path->assign(buf, (size_t)len);
    return true;
}

const char* os_platform_name() {
#if defined(__aarch64__)
    return "linux-aarch64";
#else
    return "linux-x86_64";
#endif
}
