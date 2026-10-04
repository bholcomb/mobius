// The parts of mobius_platform.h shared by Linux and macOS. The rest (the
// executable's path, names, the cache directory and the poller) is in
// mobius_platform_linux.cpp and mobius_platform_macos.cpp.
//
// Fibers are ucontexts on mmap'd stacks with a guard page.

// Before any system header: macOS keeps the (deprecated) ucontext routines
// behind _XOPEN_SOURCE, and _DARWIN_C_SOURCE keeps the rest of its system
// headers complete. Linux (glibc, with _GNU_SOURCE) is unaffected.
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
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
#include <poll.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <ucontext.h>
#include <unistd.h>

#pragma GCC diagnostic ignored "-Wdeprecated-declarations"   // ucontext on macOS

// ---------------------------------------------------------------------------
// System
// ---------------------------------------------------------------------------

uint64_t platform_monotonic_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

char platform_path_list_separator() { return ':'; }

uint64_t platform_process_id() { return (uint64_t)getpid(); }

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
    // its run path ($ORIGIN on Linux, @loader_path on macOS), set when it
    // is linked.
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
// I/O readiness outside the reactor
// ---------------------------------------------------------------------------

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
