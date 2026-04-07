#ifndef MOBIUS_REF_COUNTED_H
#define MOBIUS_REF_COUNTED_H

#include <atomic>
#include <cassert>

class RefCounted {
public:
    RefCounted() : ref_count_(1) {}
    virtual ~RefCounted() = default;

    void retain() { ref_count_.fetch_add(1, std::memory_order_relaxed); }

    void release() {
        int old = ref_count_.fetch_sub(1, std::memory_order_acq_rel);
        assert(old > 0 && "RefCounted::release() called on object with refcount <= 0 (double-release)");
        if (old <= 1) {
            delete this;
        }
    }

    int refCount() const { return ref_count_.load(std::memory_order_relaxed); }

private:
    std::atomic<int> ref_count_;
};

#endif // MOBIUS_REF_COUNTED_H
