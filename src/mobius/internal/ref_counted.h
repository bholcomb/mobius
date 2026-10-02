#ifndef MOBIUS_REF_COUNTED_H
#define MOBIUS_REF_COUNTED_H

#include <atomic>
#include <cassert>
#include <mobius/mobius.h>   // MOBIUS_API

class RefCounted {
public:
    RefCounted() : ref_count_(1) {}
    virtual ~RefCounted() = default;

    void retain() {
        if (immortal_.load(std::memory_order_relaxed)) return;
        ref_count_.fetch_add(1, std::memory_order_relaxed);
    }

    void release() {
        if (immortal_.load(std::memory_order_relaxed)) return;
        int old = ref_count_.fetch_sub(1, std::memory_order_acq_rel);
        if (old <= 1) releaseAtZero();
    }

    void setGcManaged() { gc_managed_ = true; }

    // Stop counting references: retain/release become a read of this flag.
    // For objects shared by every fiber for the program's life (top-level
    // constants): fibers copying references to them all wrote the same
    // count, and its cache line bounced between cores. Only GC-tracked
    // objects are made immortal, so the collector (or state teardown)
    // still frees them.
    void setImmortal() { immortal_.store(true, std::memory_order_relaxed); }
    bool isImmortal() const { return immortal_.load(std::memory_order_relaxed); }

    int refCount() const { return ref_count_.load(std::memory_order_relaxed); }

private:
    // Cold path, defined in the collector (gc.cpp). For GC-managed
    // subclasses (Table, ArrayValue) refcounting is the immediate reclaimer
    // for acyclic garbage and the tracing collector reclaims cycles; during
    // the collector's sweep an object hitting zero is already in the sweep's
    // dead list, so deleting here would double-free it. Out of line and
    // MOBIUS_API so plugins compiling this header bind an exported function
    // rather than the collector's internal sweep flag.
    MOBIUS_API void releaseAtZero();

    std::atomic<int> ref_count_;
    bool gc_managed_ = false;
    std::atomic<bool> immortal_{false};
};

#endif // MOBIUS_REF_COUNTED_H
