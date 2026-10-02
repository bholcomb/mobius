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
    Parked,     // waiting for an event (I/O, timer); off the ready queue
                // until JobSystem::wakeFiber requeues it
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

    // Parking handshake (see JobSystem::beginPark). A wake can arrive while
    // the fiber is still switching out; PARKING -> WOKEN records it so the
    // worker requeues the fiber instead of leaving it parked.
    enum : int { PARK_IDLE = 0, PARK_PARKING = 1, PARK_PARKED = 2, PARK_WOKEN = 3 };
    std::atomic<int> park_state{PARK_IDLE};

    size_t       peak_stack_bytes; // high-water mark for metrics

    // > 0 while a host function runs on this fiber's stack. The fiber must
    // then not switch out (it could resume on another thread with the
    // host's frames on its stack): waiting for another fiber is an error,
    // and sleeps and I/O block the thread.
    int          host_call_depth = 0;

    MobiusFiber()
        : id(0), state(FiberState::Idle),
          stack_memory(nullptr), stack_size(0),
          vm(nullptr), cancel_requested(false),
          peak_stack_bytes(0) {}
};

#endif // MOBIUS_FIBER_H
