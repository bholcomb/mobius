#ifndef MOBIUS_ARRAY_H
#define MOBIUS_ARRAY_H

#include <cstddef>
#include <new>
#include <vector>
#include <algorithm>
#include <atomic>

#include "data/value.h"
#include "internal/ref_counted.h"
#include "internal/gc.h"
#include "internal/small_vec.h"

class ArrayValue : public RefCounted {
public:
    ArrayValue(size_t initial_capacity = 8);
    ~ArrayValue() override;

    ArrayValue* retain();

    void push(const Value& value);
    Value pop();
    const Value& get(size_t index) const;
    void set(size_t index, const Value& value);
    void insert(size_t index, Value value);
    Value remove(size_t index);

    inline size_t length() const { return elements.size(); }

    inline const Value& unsafeGet(size_t index) const { return elements[index]; }

    void reserve(size_t new_capacity);
    void reverse();

    const Value& operator[](size_t index) const;
    Value& operator[](size_t index);

    Value* data() { return elements.data(); }
    const Value* data() const { return elements.data(); }

    GcHeader* gcHeader() { return &gc_; }

    // Pool-backed allocation; see Table's operators (definitions in array.cpp).
    static void* operator new(size_t sz);
    static void* operator new(size_t sz, const std::nothrow_t&) noexcept;
    static void  operator delete(void* p, size_t sz) noexcept;
    static void  operator delete(void* p) noexcept;
    static void  operator delete(void* p, const std::nothrow_t&) noexcept;

    void acquireSlice() { active_slice_count_.fetch_add(1, std::memory_order_relaxed); }
    void releaseSlice() { active_slice_count_.fetch_sub(1, std::memory_order_relaxed); }
    bool hasActiveSlices() const { return active_slice_count_.load(std::memory_order_acquire) > 0; }

private:
    SmallVec<Value, 8> elements;   // literals up to 8 need no heap
    std::atomic<size_t> active_slice_count_{0};
    GcHeader gc_;   // tracing-GC registry link (see internal/gc.h)
};

#endif // MOBIUS_ARRAY_H
