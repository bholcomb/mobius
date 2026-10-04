// macOS implementation of mobius_platform.h.
//
// Fibers are ucontexts on mmap'd stacks with a guard page (macOS keeps the
// deprecated ucontext routines behind _XOPEN_SOURCE); the reactor's poller
// is kqueue, woken through an EVFILT_USER event.

// Before any system header: ucontext needs _XOPEN_SOURCE, and
// _DARWIN_C_SOURCE keeps the rest of the system headers complete.
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif

#include "platform/mobius_platform.h"

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <dlfcn.h>
#include <fcntl.h>
#include <mach-o/dyld.h>
#include <poll.h>
#include <sys/event.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <ucontext.h>
#include <unistd.h>

#pragma clang diagnostic ignored "-Wdeprecated-declarations"   // ucontext

// ---------------------------------------------------------------------------
// System
// ---------------------------------------------------------------------------

uint64_t platform_monotonic_ns() {
    return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
}

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

char platform_path_list_separator() { return ':'; }

uint64_t platform_process_id() { return (uint64_t)getpid(); }

std::string platform_user_cache_dir() {
    const char* home = getenv("HOME");
    if (home && *home) return std::string(home) + "/Library/Caches";
    return "";
}

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------

bool platform_is_regular_file(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool platform_is_directory(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// ---------------------------------------------------------------------------
// Shared libraries
// ---------------------------------------------------------------------------

void* platform_library_open(const std::string& path, bool global, std::string* error) {
    // A library's own directory is searched for its dependencies through
    // @loader_path / @rpath install names, set when it is linked.
    void* h = dlopen(path.c_str(), RTLD_LAZY | (global ? RTLD_GLOBAL : RTLD_LOCAL));
    if (!h && error) {
        const char* e = dlerror();
        *error = e ? e : "unknown error";
    }
    return h;
}

void* platform_library_symbol(void* library, const char* name) {
    return library ? dlsym(library, name) : nullptr;
}

void platform_library_close(void* library) {
    if (library) dlclose(library);
}

// ---------------------------------------------------------------------------
// Fibers
// ---------------------------------------------------------------------------

struct PlatformFiber {
    ucontext_t ctx;
    void* mem = nullptr;        // mapping: guard page, then the stack
    size_t mem_size = 0;
    void (*entry)(void*) = nullptr;
    void* arg = nullptr;
};

namespace {

thread_local PlatformFiber* t_thread_fiber = nullptr;
thread_local int t_thread_fiber_refs = 0;
// The context running on this thread, for platform_stack_bounds.
thread_local PlatformFiber* t_running = nullptr;

size_t page_size() {
    static const size_t size = (size_t)sysconf(_SC_PAGESIZE);
    return size;
}

// makecontext passes only int arguments: the fiber's address goes in two.
void fiber_trampoline(unsigned int hi, unsigned int lo) {
    PlatformFiber* f = reinterpret_cast<PlatformFiber*>(((uintptr_t)hi << 32) | (uintptr_t)lo);
    f->entry(f->arg);
    fprintf(stderr, "FATAL: fiber entry function returned\n");
    abort();
}

} // namespace

PlatformFiber* platform_fiber_create(size_t stack_size) {
    size_t page = page_size();
    size_t usable = (stack_size + page - 1) & ~(page - 1);
    size_t total = page + usable;
    void* mem = mmap(nullptr, total, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (mem == MAP_FAILED) return nullptr;
    mprotect(mem, page, PROT_NONE);   // stacks grow down into the guard page
    PlatformFiber* f = new PlatformFiber();
    f->mem = mem;
    f->mem_size = total;
    return f;
}

void platform_fiber_destroy(PlatformFiber* fiber) {
    if (!fiber) return;
    if (fiber->mem) munmap(fiber->mem, fiber->mem_size);
    delete fiber;
}

void platform_fiber_start(PlatformFiber* fiber, void (*entry)(void*), void* arg) {
    fiber->entry = entry;
    fiber->arg = arg;
    getcontext(&fiber->ctx);
    size_t page = page_size();
    fiber->ctx.uc_stack.ss_sp = (char*)fiber->mem + page;
    fiber->ctx.uc_stack.ss_size = fiber->mem_size - page;
    fiber->ctx.uc_link = nullptr;
    uintptr_t addr = reinterpret_cast<uintptr_t>(fiber);
    makecontext(&fiber->ctx, (void (*)())fiber_trampoline, 2,
                (unsigned int)(addr >> 32), (unsigned int)(addr & 0xFFFFFFFFu));
}

void platform_fiber_switch(PlatformFiber* from, PlatformFiber* to) {
    t_running = to;
    swapcontext(&from->ctx, &to->ctx);
}

PlatformFiber* platform_thread_fiber() {
    if (t_thread_fiber_refs++ == 0) {
        t_thread_fiber = new PlatformFiber();
        getcontext(&t_thread_fiber->ctx);
        t_running = t_thread_fiber;
    }
    return t_thread_fiber;
}

void platform_thread_fiber_release() {
    if (t_thread_fiber_refs > 0 && --t_thread_fiber_refs == 0) {
        delete t_thread_fiber;
        t_thread_fiber = nullptr;
        t_running = nullptr;
    }
}

bool platform_stack_bounds(char** low, char** high) {
    PlatformFiber* f = t_running;
    if (!f || !f->mem) return false;   // a thread's own stack: not tracked
    *low = (char*)f->mem + page_size();
    *high = (char*)f->mem + f->mem_size;
    return true;
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

int platform_poll(const MobiusIoWait* waits, int count, int64_t timeout_ms) {
    std::vector<struct pollfd> fds((size_t)count);
    for (int i = 0; i < count; i++) {
        fds[i].fd = (int)waits[i].fd;
        fds[i].events = 0;
        if (waits[i].events & MOBIUS_IO_READ) fds[i].events |= POLLIN;
        if (waits[i].events & MOBIUS_IO_WRITE) fds[i].events |= POLLOUT;
        fds[i].revents = 0;
    }
    int64_t deadline = timeout_ms >= 0 ? (int64_t)(platform_monotonic_ns() / 1000000) + timeout_ms : -1;
    while (true) {
        int t = -1;
        if (deadline >= 0) {
            int64_t left = deadline - (int64_t)(platform_monotonic_ns() / 1000000);
            t = left < 0 ? 0 : (left > INT_MAX ? INT_MAX : (int)left);
        }
        int r = poll(fds.data(), (nfds_t)count, t);
        if (r < 0 && errno == EINTR) continue;
        if (r < 0) return MOBIUS_IO_ERROR;
        if (r == 0) return MOBIUS_IO_TIMEOUT;
        for (int i = 0; i < count; i++) if (fds[i].revents) return i;
    }
}
