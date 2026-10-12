#ifndef MOBIUS_FIBER_CONTEXT_H
#define MOBIUS_FIBER_CONTEXT_H

// The context switch under the POSIX fibers (mobius_platform_posix.cpp),
// for x86-64 and AArch64. Windows uses the OS's fibers instead.
//
// A suspended context is just a stack pointer: the switch pushes the
// registers a function call must preserve onto the running stack, saves
// the stack pointer, loads the other one and pops that context's
// registers. Everything else (the other registers, the signal mask) is
// either the caller's to save or left alone.

// Prepare a stack whose highest address is `stack_top` so that the first
// switch to the returned stack pointer calls entry(arg) on it. entry must
// not return.
void* fiber_context_make(char* stack_top, void (*entry)(void*), void* arg);

// Suspend the running context, storing its stack pointer in *from_sp, and
// resume the one whose stack pointer is `to_sp`. Returns when something
// switches back to *from_sp.
extern "C" void mobius_fiber_switch(void** from_sp, void* to_sp);

#endif // MOBIUS_FIBER_CONTEXT_H
