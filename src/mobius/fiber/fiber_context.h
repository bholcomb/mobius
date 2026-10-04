#ifndef MOBIUS_FIBER_CONTEXT_H
#define MOBIUS_FIBER_CONTEXT_H

#include <cstddef>

#ifndef _WIN32
#  if defined(__APPLE__)
     // macOS keeps the (deprecated) ucontext routines behind _XOPEN_SOURCE;
     // _DARWIN_C_SOURCE keeps the rest of the system headers complete.
#    ifndef _XOPEN_SOURCE
#      define _XOPEN_SOURCE 600
#    endif
#    ifndef _DARWIN_C_SOURCE
#      define _DARWIN_C_SOURCE
#    endif
#  endif
#  include <ucontext.h>
#endif

// A fiber's saved execution context.
//
// POSIX: a ucontext on a stack the fiber pool allocated.
// Windows: an OS fiber (CreateFiberEx), which allocates its own stack; the
// thread information block then tracks the running fiber's stack, as SEH,
// stack probes and C++ exceptions require.
struct FiberContext {
#ifdef _WIN32
    void* fiber_handle = nullptr;
#else
    ucontext_t uctx;
#endif
};

// Whether fibers run on stacks the fiber pool allocates (POSIX) rather
// than ones the OS allocates (Windows).
#ifdef _WIN32
constexpr bool kFiberStacksFromPool = false;
#else
constexpr bool kFiberStacksFromPool = true;
#endif

// Initialize a new fiber context with its own stack. entry(arg) will be
// called when the fiber is first switched to. `stack` is the pool's stack
// (ignored on Windows, where stack_size sizes the OS-allocated stack).
// Re-initializing a context that already had a fiber replaces it.
void fiber_context_init(FiberContext* ctx, void* stack, size_t stack_size,
                        void (*entry)(void*), void* arg);

// Switch execution from 'from' to 'to'. Saves state in 'from',
// resumes execution at wherever 'to' was last suspended.
void fiber_context_swap(FiberContext* from, FiberContext* to);

// Convert the calling thread into a fiber context so it can
// participate in fiber switching. No stack allocation needed --
// the thread's existing stack is used.
void fiber_context_convert_thread(FiberContext* ctx);

// Undo fiber_context_convert_thread when the thread stops scheduling
// (Windows: the thread stops being a fiber, unless it was one already).
void fiber_context_release_thread();

// Free a context's resources (Windows: its OS fiber). Not the running one.
void fiber_context_destroy(FiberContext* ctx);

#endif // MOBIUS_FIBER_CONTEXT_H
