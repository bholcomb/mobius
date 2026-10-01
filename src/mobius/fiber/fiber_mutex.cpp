#include "fiber/fiber_mutex.h"
#include "fiber/job_system.h"
#include "state/mobius_state.h"
#include "vm/vm.h"

#include <thread>

// Identity of the current lock owner: the running fiber's VM, or this
// thread when no VM is active (e.g. an embedder's own thread).
static const void* current_owner() {
    if (MobiusVM::t_current_vm) return MobiusVM::t_current_vm;
    static thread_local char thread_identity;
    return &thread_identity;
}

// Let the lock holder run: yield this fiber if we are on one, otherwise
// yield the thread.
static void wait_for_holder() {
    MobiusVM* vm = MobiusVM::t_current_vm;
    JobSystem* js = vm ? vm->state_->jobSystem() : nullptr;
    if (js && js->currentFiber()) {
        js->yieldFiber();
    } else {
        std::this_thread::yield();
    }
}

void FiberMutex::lock() {
    const void* me = current_owner();
    if (owner_.load(std::memory_order_acquire) == me) {
        depth_++;
        return;
    }
    const void* expected = nullptr;
    while (!owner_.compare_exchange_weak(expected, me, std::memory_order_acquire,
                                         std::memory_order_relaxed)) {
        expected = nullptr;
        wait_for_holder();
    }
    depth_ = 1;
}

bool FiberMutex::try_lock() {
    const void* me = current_owner();
    if (owner_.load(std::memory_order_acquire) == me) {
        depth_++;
        return true;
    }
    const void* expected = nullptr;
    if (owner_.compare_exchange_strong(expected, me, std::memory_order_acquire,
                                       std::memory_order_relaxed)) {
        depth_ = 1;
        return true;
    }
    return false;
}

void FiberMutex::unlock() {
    if (--depth_ == 0) {
        owner_.store(nullptr, std::memory_order_release);
    }
}
