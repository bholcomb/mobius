#include "fiber/fiber_pool.h"

#include <cstdlib>
#include <cstdio>

FiberPool::FiberPool(size_t fiber_stack_size, size_t initial_count, size_t max_count)
    : fiber_stack_size_(fiber_stack_size),
      max_count_(max_count), next_id_(1) {
    all_fibers_.reserve(initial_count);
    free_list_.reserve(initial_count);
    for (size_t i = 0; i < initial_count; i++) {
        MobiusFiber* f = allocateFiber();
        if (f) free_list_.push_back(f);
    }
}

FiberPool::~FiberPool() {
    for (MobiusFiber* f : all_fibers_) {
        deallocateFiber(f);
    }
}

MobiusFiber* FiberPool::allocateFiber() {
    PlatformFiber* context = platform_fiber_create(fiber_stack_size_);
    if (!context) return nullptr;

    MobiusFiber* fiber = new MobiusFiber();
    fiber->id = next_id_++;
    fiber->context = context;
    fiber->stack_size = fiber_stack_size_;
    fiber->state = FiberState::Idle;
    fiber->vm = nullptr;
    fiber->cancel_requested.store(false, std::memory_order_relaxed);
    fiber->peak_stack_bytes = 0;

    all_fibers_.push_back(fiber);
    return fiber;
}

void FiberPool::deallocateFiber(MobiusFiber* fiber) {
    platform_fiber_destroy(fiber->context);
    delete fiber;
}

MobiusFiber* FiberPool::acquire() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!free_list_.empty()) {
        MobiusFiber* f = free_list_.back();
        free_list_.pop_back();
        f->state = FiberState::Idle;
        f->cancel_requested.store(false, std::memory_order_relaxed);
        f->peak_stack_bytes = 0;
        f->vm = nullptr;
        return f;
    }

    if (all_fibers_.size() >= max_count_) {
        return nullptr;
    }

    // Grow: allocate a new fiber on demand
    return allocateFiber();
}

void FiberPool::release(MobiusFiber* fiber) {
    std::lock_guard<std::mutex> lock(mutex_);
    fiber->state = FiberState::Idle;
    fiber->vm = nullptr;
    free_list_.push_back(fiber);
}

MobiusFiber* FiberPool::createDetachedFiber(size_t stack_size) {
    PlatformFiber* context = platform_fiber_create(stack_size);
    if (!context) return nullptr;

    MobiusFiber* fiber = new MobiusFiber();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        fiber->id = next_id_++;
    }
    fiber->context = context;
    fiber->stack_size = stack_size;
    fiber->state = FiberState::Idle;
    fiber->vm = nullptr;
    fiber->cancel_requested.store(false, std::memory_order_relaxed);
    fiber->peak_stack_bytes = 0;
    // Intentionally not tracked in all_fibers_/free_list_: detached fibers are
    // owned by the caller and never reused by the worker pool.
    return fiber;
}

void FiberPool::destroyDetachedFiber(MobiusFiber* fiber) {
    if (!fiber) return;
    deallocateFiber(fiber);
}

bool FiberPool::exhausted() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return free_list_.empty() && all_fibers_.size() >= max_count_;
}

size_t FiberPool::activeCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    // active = total - free
    return all_fibers_.size() - free_list_.size();
}

size_t FiberPool::totalCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return all_fibers_.size();
}
