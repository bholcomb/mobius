// The fiber context switch (fiber_context.h), in assembly for each
// supported CPU, with the C++ that prepares a new fiber's stack.
//
// mobius_fiber_switch(from_sp, to_sp) is called like any function, so the
// compiler has already saved whatever the calling convention lets a call
// destroy. It saves the rest, the callee-saved registers, by pushing them
// on the running stack, and stores the stack pointer in *from_sp. Then it
// loads to_sp, pops the registers saved there and returns: into the code
// that once switched away from that stack, or, for a new fiber, into
// mobius_fiber_trampoline, which calls entry(arg).
//
// fiber_context_make writes a new fiber's first frame so that it looks
// exactly like one the switch saved. Each CPU's SwitchFrame below is that
// layout and must match the order of its pushes.
//
// x86 CET: the switch returns on another stack than it was called on,
// which a shadow stack (SHSTK) rejects, so the Linux x86-64 build claims
// IBT only (-fcf-protection=branch in the root buildy.yaml), and the
// loader, which turns shadow stacks on only when every object claims
// SHSTK, leaves them off. Indirect branch tracking holds: the switch
// starts with endbr64, and the trampoline is reached by a return, which
// IBT doesn't check.

#include "platform/fiber_context.h"

#include <cstdint>
#include <cstring>

#if !defined(__linux__) && !defined(__APPLE__)
#  error "fiber_context.cpp: the fiber context switch is for Linux and macOS only"
#endif
#if !defined(__x86_64__) && !defined(__aarch64__)
#  error "fiber_context.cpp: no fiber context switch for this CPU (only x86-64 and AArch64)"
#endif

// Defining a function in assembly: Mach-O (macOS) puts an underscore before
// C names and has no .type/.size; ELF (Linux) has them, and a section stack
// so the compiler's own current section is left as it was. Both keep the
// symbol out of the shared library's exports (.private_extern, .hidden).
#if defined(__APPLE__)
#  define ASM_FUNCTION(name) \
    ".text\n" ".globl _" #name "\n" ".private_extern _" #name "\n" ".p2align 4\n" "_" #name ":\n"
#  define ASM_END(name) ""
#else
#  define ASM_FUNCTION(name) \
    ".pushsection .text\n" ".globl " #name "\n" ".hidden " #name "\n" \
    ".type " #name ", %function\n" ".p2align 4\n" #name ":\n"
#  define ASM_END(name) ".size " #name ", .-" #name "\n" ".popsection\n"
#endif

// The first code a new fiber runs (never called: the switch "returns" to
// it). Its SwitchFrame put entry and arg in two callee-saved registers.
extern "C" void mobius_fiber_trampoline();

#if defined(__x86_64__)

// ---------------------------------------------------------------------------
// x86-64, System V ABI (Linux and macOS)
// ---------------------------------------------------------------------------
//
// Callee-saved (System V AMD64 ABI, section 3.2.1 and figure 3.4): rbx,
// rbp, r12-r15 and rsp, plus the control bits of MXCSR and the x87 control
// word (section 3.2.3). All the xmm registers are caller-saved.
//
// Stack alignment (section 3.2.2): rsp + 8 is a multiple of 16 when a
// function starts, i.e. rsp is 16-byte aligned at each call instruction.

__asm__(
    ASM_FUNCTION(mobius_fiber_switch)        // rdi = from_sp, rsi = to_sp
    "    endbr64\n"                          // a valid target of an indirect call
                                             // under CET IBT (else a no-op)
    "    pushq  %rbp\n"
    "    pushq  %rbx\n"
    "    pushq  %r12\n"
    "    pushq  %r13\n"
    "    pushq  %r14\n"
    "    pushq  %r15\n"
    "    subq   $8, %rsp\n"
    "    stmxcsr (%rsp)\n"                  // MXCSR in the low 4 bytes,
    "    fnstcw 4(%rsp)\n"                  // the x87 control word above it
    "    movq   %rsp, (%rdi)\n"              // *from_sp = rsp
    "    movq   %rsi, %rsp\n"                // switch stacks
    "    ldmxcsr (%rsp)\n"
    "    fldcw  4(%rsp)\n"
    "    addq   $8, %rsp\n"
    "    popq   %r15\n"
    "    popq   %r14\n"
    "    popq   %r13\n"
    "    popq   %r12\n"
    "    popq   %rbx\n"
    "    popq   %rbp\n"
    "    ret\n"                              // to where to_sp's context left off
    ASM_END(mobius_fiber_switch)

    ASM_FUNCTION(mobius_fiber_trampoline)    // rsp is 16-byte aligned here
    "    movq   %r13, %rdi\n"                // entry(arg)
    "    callq  *%r12\n"
    "    ud2\n"                              // entry returned: crash here
    ASM_END(mobius_fiber_trampoline)
);

// What mobius_fiber_switch leaves on a suspended stack, lowest address
// (the saved stack pointer) first.
struct SwitchFrame {
    uint32_t mxcsr;
    uint16_t x87_control;
    uint16_t unused;
    uint64_t r15, r14, r13, r12, rbx, rbp;
    uint64_t return_address;
};
static_assert(sizeof(SwitchFrame) == 64, "SwitchFrame must match mobius_fiber_switch");

void* fiber_context_make(char* stack_top, void (*entry)(void*), void* arg) {
    // Aligned so that rsp is 16-byte aligned when the switch's ret has
    // popped the frame, as the trampoline's call needs.
    uintptr_t top = (uintptr_t)stack_top & ~(uintptr_t)15;
    SwitchFrame* frame = (SwitchFrame*)(top - sizeof(SwitchFrame));
    memset(frame, 0, sizeof(SwitchFrame));   // rbp = 0 ends a backtrace here
    // The fiber starts with this thread's floating-point modes.
    __asm__ volatile("stmxcsr %0" : "=m"(frame->mxcsr));
    __asm__ volatile("fnstcw %0" : "=m"(frame->x87_control));
    frame->r12 = (uint64_t)(uintptr_t)entry;
    frame->r13 = (uint64_t)(uintptr_t)arg;
    frame->return_address = (uint64_t)(uintptr_t)&mobius_fiber_trampoline;
    return frame;
}

#elif defined(__aarch64__)

// ---------------------------------------------------------------------------
// AArch64, AAPCS64 (Linux, and macOS, which follows it here)
// ---------------------------------------------------------------------------
//
// Callee-saved (AAPCS64, sections 6.1.1 and 6.1.2): x19-x28, the frame
// pointer x29, the link register x30 (our return address) and sp, plus
// the low 64 bits of v8-v15, i.e. d8-d15. x18 is the platform register
// (reserved on macOS): not touched.
//
// Stack alignment (section 6.2.2): sp is always 16-byte aligned; the
// hardware faults on a misaligned sp when it is used.

__asm__(
    ASM_FUNCTION(mobius_fiber_switch)        // x0 = from_sp, x1 = to_sp
    "    hint   #34\n"                       // bti c: a valid target of an indirect call
                                             // where branch protection is on (else a no-op)
    "    sub    sp, sp, #160\n"
    "    stp    x19, x20, [sp, #0]\n"
    "    stp    x21, x22, [sp, #16]\n"
    "    stp    x23, x24, [sp, #32]\n"
    "    stp    x25, x26, [sp, #48]\n"
    "    stp    x27, x28, [sp, #64]\n"
    "    stp    x29, x30, [sp, #80]\n"
    "    stp    d8,  d9,  [sp, #96]\n"
    "    stp    d10, d11, [sp, #112]\n"
    "    stp    d12, d13, [sp, #128]\n"
    "    stp    d14, d15, [sp, #144]\n"
    "    mov    x9, sp\n"                    // *from_sp = sp
    "    str    x9, [x0]\n"
    "    mov    sp, x1\n"                    // switch stacks
    "    ldp    x19, x20, [sp, #0]\n"
    "    ldp    x21, x22, [sp, #16]\n"
    "    ldp    x23, x24, [sp, #32]\n"
    "    ldp    x25, x26, [sp, #48]\n"
    "    ldp    x27, x28, [sp, #64]\n"
    "    ldp    x29, x30, [sp, #80]\n"
    "    ldp    d8,  d9,  [sp, #96]\n"
    "    ldp    d10, d11, [sp, #112]\n"
    "    ldp    d12, d13, [sp, #128]\n"
    "    ldp    d14, d15, [sp, #144]\n"
    "    add    sp, sp, #160\n"
    "    ret\n"                              // to x30: where to_sp's context left off
    ASM_END(mobius_fiber_switch)

    ASM_FUNCTION(mobius_fiber_trampoline)
    "    mov    x0, x20\n"                   // entry(arg)
    "    blr    x19\n"
    "    brk    #0\n"                        // entry returned: crash here
    ASM_END(mobius_fiber_trampoline)
);

// What mobius_fiber_switch leaves on a suspended stack, lowest address
// (the saved stack pointer) first.
struct SwitchFrame {
    uint64_t x19, x20, x21, x22, x23, x24, x25, x26, x27, x28;
    uint64_t x29;      // frame pointer
    uint64_t x30;      // link register: where the switch returns to
    uint64_t d8_to_d15[8];
};
static_assert(sizeof(SwitchFrame) == 160, "SwitchFrame must match mobius_fiber_switch");

void* fiber_context_make(char* stack_top, void (*entry)(void*), void* arg) {
    uintptr_t top = (uintptr_t)stack_top & ~(uintptr_t)15;
    SwitchFrame* frame = (SwitchFrame*)(top - sizeof(SwitchFrame));
    memset(frame, 0, sizeof(SwitchFrame));   // x29 = 0 ends a backtrace here
    frame->x19 = (uint64_t)(uintptr_t)entry;
    frame->x20 = (uint64_t)(uintptr_t)arg;
    frame->x30 = (uint64_t)(uintptr_t)&mobius_fiber_trampoline;
    return frame;
}

#endif
