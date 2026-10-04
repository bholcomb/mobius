// The macOS-only parts of mobius_platform.h (the rest is in
// mobius_platform_posix.cpp): the executable's path, names, the cache
// directory, and the reactor's poller (kqueue, woken through an
// EVFILT_USER event).

#include "platform/mobius_platform.h"

#include <climits>
#include <cstdlib>
#include <cstring>

#include <fcntl.h>
#include <mach-o/dyld.h>
#include <sys/event.h>
#include <unistd.h>

// ---------------------------------------------------------------------------
// System
// ---------------------------------------------------------------------------

std::string platform_executable_path() {
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buf(size, '\0');
    if (_NSGetExecutablePath(&buf[0], &size) != 0) return "";
    buf.resize(strlen(buf.c_str()));
    char resolved[PATH_MAX];
    return realpath(buf.c_str(), resolved) ? std::string(resolved) : buf;
}

const char* platform_name() {
#if defined(__aarch64__) || defined(__arm64__)
    return "macos-aarch64";
#else
    return "macos-x86_64";
#endif
}

const char* platform_library_extension() { return ".dylib"; }

const char* platform_core_library_name() { return "libmobius-core.dylib"; }

std::string platform_user_cache_dir() {
    const char* home = getenv("HOME");
    if (home && *home) return std::string(home) + "/Library/Caches";
    return "";
}

// ---------------------------------------------------------------------------
// I/O readiness: kqueue. Each registration watches a dup() of the
// descriptor, so any number can watch one descriptor; closing the dup
// removes its events. An EVFILT_USER event wakes a wait.
// ---------------------------------------------------------------------------

struct PlatformPoller {
    int kq = -1;
};

PlatformPoller* platform_poller_create() {
    int kq = kqueue();
    if (kq < 0) return nullptr;
    struct kevent ev;
    EV_SET(&ev, 0, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, nullptr);
    if (kevent(kq, &ev, 1, nullptr, 0, nullptr) != 0) {
        close(kq);
        return nullptr;
    }
    fcntl(kq, F_SETFD, FD_CLOEXEC);
    PlatformPoller* p = new PlatformPoller();
    p->kq = kq;
    return p;
}

intptr_t platform_poller_add(PlatformPoller* poller, intptr_t handle, int events, uint64_t id) {
    int dup_fd = fcntl((int)handle, F_DUPFD_CLOEXEC, 0);
    if (dup_fd < 0) return -1;
    struct kevent changes[2];
    int n = 0;
    if (events & MOBIUS_IO_READ)
        EV_SET(&changes[n++], dup_fd, EVFILT_READ, EV_ADD | EV_ONESHOT, 0, 0, (void*)(uintptr_t)id);
    if (events & MOBIUS_IO_WRITE)
        EV_SET(&changes[n++], dup_fd, EVFILT_WRITE, EV_ADD | EV_ONESHOT, 0, 0, (void*)(uintptr_t)id);
    if (kevent(poller->kq, changes, n, nullptr, 0, nullptr) != 0) {
        close(dup_fd);
        return -1;
    }
    return dup_fd;
}

void platform_poller_remove(PlatformPoller* poller, intptr_t token) {
    (void)poller;
    close((int)token);   // removes the dup's events
}

void platform_poller_wait(PlatformPoller* poller, int timeout_ms, std::vector<uint64_t>& ready) {
    struct kevent events[64];
    struct timespec ts;
    struct timespec* tsp = nullptr;
    if (timeout_ms >= 0) {
        ts.tv_sec = timeout_ms / 1000;
        ts.tv_nsec = (long)(timeout_ms % 1000) * 1000000L;
        tsp = &ts;
    }
    int n = kevent(poller->kq, nullptr, 0, events, 64, tsp);
    for (int i = 0; i < n; i++) {
        if (events[i].filter == EVFILT_USER) continue;
        ready.push_back((uint64_t)(uintptr_t)events[i].udata);
    }
}

void platform_poller_wake(PlatformPoller* poller) {
    struct kevent ev;
    EV_SET(&ev, 0, EVFILT_USER, 0, NOTE_TRIGGER, 0, nullptr);
    kevent(poller->kq, &ev, 1, nullptr, 0, nullptr);
}
