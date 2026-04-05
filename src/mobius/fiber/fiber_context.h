#ifndef MOBIUS_FIBER_CONTEXT_H
#define MOBIUS_FIBER_CONTEXT_H

#include <cstddef>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <ucontext.h>
#endif

struct FiberContext {
#ifdef _WIN32
    void* fiber_handle;
#else
    ucontext_t uctx;
#endif
};

// Initialize a new fiber context with its own stack.
// entry(arg) will be called when the fiber is first switched to.
void fiber_context_init(FiberContext* ctx, void* stack, size_t stack_size,
                        void (*entry)(void*), void* arg);

// Switch execution from 'from' to 'to'. Saves state in 'from',
// resumes execution at wherever 'to' was last suspended.
void fiber_context_swap(FiberContext* from, FiberContext* to);

// Convert the calling thread into a fiber context so it can
// participate in fiber switching. No stack allocation needed --
// the thread's existing stack is used.
void fiber_context_convert_thread(FiberContext* ctx);

#endif // MOBIUS_FIBER_CONTEXT_H
