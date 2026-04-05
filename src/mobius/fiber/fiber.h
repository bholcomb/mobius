#ifndef MOBIUS_FIBER_H
#define MOBIUS_FIBER_H

#include "fiber/fiber_context.h"
#include <atomic>
#include <cstddef>
#include <cstdint>

class MobiusVM;

enum class FiberState : uint8_t {
    Idle,       // in pool, not running
    Running,    // actively executing on a worker
    Suspended,  // yielded or awaiting, can be resumed
    Dead        // finished, will be returned to pool
};

struct MobiusFiber {
    FiberContext context;
    uint32_t     id;
    FiberState   state;

    void*        stack_memory;     // mmap'd region (includes guard page)
    size_t       stack_size;       // usable stack bytes (excludes guard page)

    MobiusVM*    vm;               // back-pointer to the VM running on this fiber

    std::atomic<bool> cancel_requested;

    size_t       peak_stack_bytes; // high-water mark for metrics

    MobiusFiber()
        : id(0), state(FiberState::Idle),
          stack_memory(nullptr), stack_size(0),
          vm(nullptr), cancel_requested(false),
          peak_stack_bytes(0) {}
};

#endif // MOBIUS_FIBER_H
