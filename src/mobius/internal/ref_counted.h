#ifndef MOBIUS_REF_COUNTED_H
#define MOBIUS_REF_COUNTED_H

#include <atomic>

class RefCounted {
public:
    RefCounted() : ref_count_(1) {}
    virtual ~RefCounted() = default;

    void retain() { ref_count_.fetch_add(1, std::memory_order_relaxed); }

    void release() {
        if (ref_count_.fetch_sub(1, std::memory_order_acq_rel) <= 1) {
            delete this;
        }
    }

    int refCount() const { return ref_count_.load(std::memory_order_relaxed); }

private:
    std::atomic<int> ref_count_;
};

#endif // MOBIUS_REF_COUNTED_H
