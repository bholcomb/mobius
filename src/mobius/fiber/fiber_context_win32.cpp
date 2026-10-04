#ifdef _WIN32

// Windows fibers: each Mobius fiber is an OS fiber with its own
// OS-allocated stack, and each scheduling thread is converted to a fiber so
// it can switch to them. Fibers may be switched to from any converted
// thread (a fiber moves between worker threads), never from two at once.

#include "fiber/fiber_context.h"

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdio>
#include <cstdlib>

namespace {

struct FiberStartup {
    void (*entry)(void*);
    void* arg;
};

void WINAPI fiber_trampoline(LPVOID param) {
    FiberStartup* s = static_cast<FiberStartup*>(param);
    void (*fn)(void*) = s->entry;
    void* a = s->arg;
    delete s;
    fn(a);
    fprintf(stderr, "FATAL: fiber entry function returned\n");
    abort();
}

// Whether this thread was converted by Mobius (and so is turned back).
thread_local bool t_converted_here = false;

} // namespace

void fiber_context_init(FiberContext* ctx, void* stack, size_t stack_size,
                        void (*entry)(void*), void* arg) {
    (void)stack;
    fiber_context_destroy(ctx);
    FiberStartup* s = new FiberStartup{entry, arg};
    // Reserve stack_size; commit grows on demand (guard pages).
    ctx->fiber_handle = CreateFiberEx(0, stack_size, FIBER_FLAG_FLOAT_SWITCH, fiber_trampoline, s);
    if (!ctx->fiber_handle) {
        delete s;
        fprintf(stderr, "FATAL: CreateFiberEx failed (error %lu)\n", GetLastError());
        abort();
    }
}

void fiber_context_swap(FiberContext* from, FiberContext* to) {
    (void)from;   // the OS knows the running fiber
    SwitchToFiber(to->fiber_handle);
}

void fiber_context_convert_thread(FiberContext* ctx) {
    if (IsThreadAFiber()) {
        ctx->fiber_handle = GetCurrentFiber();
        return;
    }
    ctx->fiber_handle = ConvertThreadToFiberEx(nullptr, FIBER_FLAG_FLOAT_SWITCH);
    if (!ctx->fiber_handle) {
        fprintf(stderr, "FATAL: ConvertThreadToFiberEx failed (error %lu)\n", GetLastError());
        abort();
    }
    t_converted_here = true;
}

void fiber_context_release_thread() {
    if (t_converted_here && IsThreadAFiber()) ConvertFiberToThread();
    t_converted_here = false;
}

void fiber_context_destroy(FiberContext* ctx) {
    if (ctx->fiber_handle) {
        DeleteFiber(ctx->fiber_handle);
        ctx->fiber_handle = nullptr;
    }
}

#endif // _WIN32
