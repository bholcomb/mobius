#ifndef MOBIUS_FIBER_MUTEX_H
#define MOBIUS_FIBER_MUTEX_H

#include <atomic>
#include <mobius/mobius.h>   // MOBIUS_API: plugins (io, process) lock streams with it

// Recursive mutex owned by a *fiber*, not an OS thread.
//
// Shared values were guarded by std::recursive_mutex, which belongs to a
// thread. Fibers yield and resume on any worker thread, so a fiber that
// yielded inside atomic(...) (or inside a callback run while an array
// method held the lock) unlocked from a thread that did not own the mutex:
// undefined behavior, which in practice never released the lock, so every
// later user deadlocked. And two fibers on the same thread could both
// "acquire" the recursive lock, so it did not exclude them from each other.
//
// The owner here is the running fiber's VM (each fiber has its own, and it
// stays the same when the fiber moves between threads), or the thread
// itself outside any VM. A contended lock yields the waiting fiber to the
// scheduler instead of blocking the thread, so the holder can run and
// release it even when both share a worker thread. Satisfies Lockable, so
// std::lock_guard / std::unique_lock work unchanged.
class FiberMutex {
public:
    FiberMutex() = default;
    FiberMutex(const FiberMutex&) = delete;
    FiberMutex& operator=(const FiberMutex&) = delete;

    MOBIUS_API void lock();
    MOBIUS_API bool try_lock();
    MOBIUS_API void unlock();

private:
    std::atomic<const void*> owner_{nullptr};
    int depth_ = 0;   // only read or written by the owner
};

#endif // MOBIUS_FIBER_MUTEX_H
