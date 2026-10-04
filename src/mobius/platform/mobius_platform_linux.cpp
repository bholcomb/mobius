// The Linux-only parts of mobius_platform.h (the rest is in
// mobius_platform_posix.cpp): the executable's path, names, the cache
// directory, and the reactor's poller (epoll, woken through an eventfd).

#include "platform/mobius_platform.h"

#include <climits>
#include <cstdlib>

#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

// ---------------------------------------------------------------------------
// System
// ---------------------------------------------------------------------------

std::string platform_executable_path() {
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return "";
    return std::string(buf, (size_t)n);
}

const char* platform_name() {
#if defined(__aarch64__)
    return "linux-aarch64";
#else
    return "linux-x86_64";
#endif
}

const char* platform_library_extension() { return ".so"; }

const char* platform_core_library_name() { return "libmobius-core.so"; }

std::string platform_user_cache_dir() {
    const char* xdg = getenv("XDG_CACHE_HOME");
    if (xdg && *xdg) return xdg;
    const char* home = getenv("HOME");
    if (home && *home) return std::string(home) + "/.cache";
    return "";
}

// ---------------------------------------------------------------------------
// I/O readiness: epoll. Each registration watches a dup() of the
// descriptor, so any number can watch one descriptor (epoll accepts a
// descriptor once). An eventfd (id 0) wakes a wait.
// ---------------------------------------------------------------------------

struct PlatformPoller {
    int epfd = -1;
    int evfd = -1;
};

PlatformPoller* platform_poller_create() {
    PlatformPoller* p = new PlatformPoller();
    p->epfd = epoll_create1(EPOLL_CLOEXEC);
    p->evfd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    struct epoll_event ev = {};
    ev.events = EPOLLIN;
    ev.data.u64 = 0;
    if (p->epfd < 0 || p->evfd < 0 || epoll_ctl(p->epfd, EPOLL_CTL_ADD, p->evfd, &ev) != 0) {
        if (p->epfd >= 0) close(p->epfd);
        if (p->evfd >= 0) close(p->evfd);
        delete p;
        return nullptr;
    }
    return p;
}

intptr_t platform_poller_add(PlatformPoller* poller, intptr_t handle, int events, uint64_t id) {
    int dup_fd = fcntl((int)handle, F_DUPFD_CLOEXEC, 0);
    if (dup_fd < 0) return -1;
    struct epoll_event ev = {};
    ev.events = EPOLLONESHOT;
    if (events & MOBIUS_IO_READ) ev.events |= EPOLLIN | EPOLLRDHUP;
    if (events & MOBIUS_IO_WRITE) ev.events |= EPOLLOUT;
    ev.data.u64 = id;
    if (epoll_ctl(poller->epfd, EPOLL_CTL_ADD, dup_fd, &ev) != 0) {
        close(dup_fd);
        return -1;
    }
    return dup_fd;
}

void platform_poller_remove(PlatformPoller* poller, intptr_t token) {
    epoll_ctl(poller->epfd, EPOLL_CTL_DEL, (int)token, nullptr);
    close((int)token);
}

void platform_poller_wait(PlatformPoller* poller, int timeout_ms, std::vector<uint64_t>& ready) {
    struct epoll_event events[64];
    int n = epoll_wait(poller->epfd, events, 64, timeout_ms);
    for (int i = 0; i < n; i++) {
        uint64_t id = events[i].data.u64;
        if (id == 0) {
            uint64_t drain;
            ssize_t r = read(poller->evfd, &drain, sizeof(drain));
            (void)r;
            continue;
        }
        ready.push_back(id);
    }
}

void platform_poller_wake(PlatformPoller* poller) {
    uint64_t one = 1;
    ssize_t r = write(poller->evfd, &one, sizeof(one));
    (void)r;
}
