#ifndef MOBIUS_MODULES_PROCESS_PLATFORM_POSIX_H
#define MOBIUS_MODULES_PROCESS_PLATFORM_POSIX_H

// What differs between Linux and macOS under process_platform_posix.cpp,
// implemented by process_platform_linux.cpp and process_platform_macos.cpp.

#include <sys/types.h>

// The process environment (a shared library can't link `environ` on macOS).
char** posix_environ();

// A pipe whose ends are close-on-exec. 0, or -1 with errno set.
int posix_cloexec_pipe(int fds[2]);

// A descriptor that becomes readable when child `pid` exits (Linux: a
// pidfd), or -1 if there is none: then exits are found by polling.
int posix_exit_fd(pid_t pid);

#endif // MOBIUS_MODULES_PROCESS_PLATFORM_POSIX_H
