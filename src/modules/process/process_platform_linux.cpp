// The Linux-only parts of the process module (see process_platform_posix.h).

#include "process_platform_posix.h"

#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>

extern char** environ;

char** posix_environ() { return environ; }

int posix_cloexec_pipe(int fds[2]) { return pipe2(fds, O_CLOEXEC); }

int posix_exit_fd(pid_t pid) {
#ifdef SYS_pidfd_open
    int fd = (int)syscall(SYS_pidfd_open, pid, 0);   // -1 on kernels before 5.3
    if (fd >= 0) fcntl(fd, F_SETFD, FD_CLOEXEC);
    return fd;
#else
    (void)pid;
    return -1;
#endif
}
