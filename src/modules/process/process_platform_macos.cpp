// The macOS-only parts of the process module (see process_platform_posix.h).

#include "process_platform_posix.h"

#include <crt_externs.h>
#include <fcntl.h>
#include <unistd.h>

char** posix_environ() { return *_NSGetEnviron(); }

// No pipe2: set close-on-exec after creating the pipe.
int posix_cloexec_pipe(int fds[2]) {
    if (pipe(fds) != 0) return -1;
    fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    return 0;
}

// No pidfd: exits are found by polling.
int posix_exit_fd(pid_t pid) {
    (void)pid;
    return -1;
}
