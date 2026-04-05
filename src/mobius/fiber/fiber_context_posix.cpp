#ifndef _WIN32

#include "fiber/fiber_context.h"
#include <cstdlib>
#include <cstdint>
#include <cstdio>

// ucontext's makecontext only accepts int arguments, so we pack the
// entry function pointer into two unsigned ints (high/low halves).
// The arg pointer is stored at a known offset in the stack region
// (first sizeof(void*) bytes past the guard page).
struct FiberStartup {
    void (*entry)(void*);
    void* arg;
};

static void fiber_trampoline(unsigned int hi, unsigned int lo) {
    uintptr_t addr = ((uintptr_t)hi << 32) | (uintptr_t)lo;
    FiberStartup* s = reinterpret_cast<FiberStartup*>(addr);
    void (*fn)(void*) = s->entry;
    void* a = s->arg;
    delete s;
    fn(a);

    fprintf(stderr, "FATAL: fiber entry function returned\n");
    abort();
}

void fiber_context_init(FiberContext* ctx, void* stack, size_t stack_size,
                        void (*entry)(void*), void* arg) {
    getcontext(&ctx->uctx);
    ctx->uctx.uc_stack.ss_sp = stack;
    ctx->uctx.uc_stack.ss_size = stack_size;
    ctx->uctx.uc_link = nullptr;

    FiberStartup* s = new FiberStartup{entry, arg};
    uintptr_t addr = reinterpret_cast<uintptr_t>(s);
    unsigned int hi = (unsigned int)(addr >> 32);
    unsigned int lo = (unsigned int)(addr & 0xFFFFFFFF);
    makecontext(&ctx->uctx, (void(*)())fiber_trampoline, 2, hi, lo);
}

void fiber_context_swap(FiberContext* from, FiberContext* to) {
    swapcontext(&from->uctx, &to->uctx);
}

void fiber_context_convert_thread(FiberContext* ctx) {
    getcontext(&ctx->uctx);
}

#endif // !_WIN32
