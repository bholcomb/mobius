#ifndef MOBIUS_FIBER_POOL_H
#define MOBIUS_FIBER_POOL_H

#include "fiber/fiber.h"
#include <mobius/mobius.h>

#include <vector>
#include <mutex>
#include <cstddef>
#include <cstdint>

class FiberPool {
public:
    FiberPool(size_t fiber_stack_size, size_t initial_count, size_t max_count);
    ~FiberPool();

    FiberPool(const FiberPool&) = delete;
    FiberPool& operator=(const FiberPool&) = delete;

    MobiusFiber* acquire();
    void release(MobiusFiber* fiber);

    // Allocate a standalone fiber with a custom stack size that is NOT part of
    // the pool (never handed to workers via acquire). The caller owns it and
    // must free it with destroyDetachedFiber(). Used for the larger-stacked
    // top-level main fiber.
    MobiusFiber* createDetachedFiber(size_t stack_size);
    void destroyDetachedFiber(MobiusFiber* fiber);

    size_t activeCount() const;
    size_t totalCount() const;

private:
    MobiusFiber* allocateFiber();
    void deallocateFiber(MobiusFiber* fiber);

    size_t fiber_stack_size_;
    size_t max_count_;

    std::vector<MobiusFiber*> all_fibers_;
    std::vector<MobiusFiber*> free_list_;
    mutable std::mutex mutex_;

    uint32_t next_id_;
};

#endif // MOBIUS_FIBER_POOL_H
