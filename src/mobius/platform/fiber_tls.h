#ifndef MOBIUS_FIBER_TLS_H
#define MOBIUS_FIBER_TLS_H

// Thread-locals on fibers.
//
// A fiber can stop on one thread and resume on another. Compilers assume
// a function runs on one thread from start to end, so they may find a
// thread_local's address once and use it again after a call, and that
// call may have switched fibers: the address is then the old thread's
// variable. (MSVC has a switch to stop this, /GT; GCC and Clang don't.)
// On Apple Silicon finding the address is a function call, and Clang
// reuses its result; glibc declares __errno_location, errno's address,
// "const", so GCC may too.
//
// So code that can run on a fiber reaches a thread_local only through a
// small function marked MOBIUS_TLS_ACCESSOR that starts with
// MOBIUS_TLS_FENCE(): the compiler can't inline it, nor merge or move its
// calls, so each call finds the variable of the thread it runs on. The
// caller must not keep what it returns across anything that can switch
// fibers (a yield, an await, a channel operation, a sleep, an I/O wait).
#if defined(_MSC_VER) && !defined(__clang__)
#  define MOBIUS_TLS_ACCESSOR __declspec(noinline)
#  define MOBIUS_TLS_FENCE() ((void)0)   // /GT already makes MSVC's thread-locals fiber-safe
#else
#  define MOBIUS_TLS_ACCESSOR __attribute__((noinline))
// An empty asm that may read and write any memory: the function has a
// side effect, so its calls can't be merged or moved (without it the
// compiler could find that it only reads, or computes, an address).
#  define MOBIUS_TLS_FENCE() __asm__ __volatile__("" ::: "memory")
#endif

#endif // MOBIUS_FIBER_TLS_H
