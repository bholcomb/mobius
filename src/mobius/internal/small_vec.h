#ifndef MOBIUS_SMALL_VEC_H
#define MOBIUS_SMALL_VEC_H

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <new>
#include <utility>

// ============================================================================
// SmallVec — a vector with inline capacity for trivially-relocatable types.
//
// Purpose-built for the hot container storage (Table entries/tags, ArrayValue
// elements): a container that fits its inline capacity performs ZERO heap
// allocations — the storage lives inside the owning object, which itself
// comes from the GC object pools. Only oversized containers spill to malloc.
//
// Element contract: T must be trivially RELOCATABLE — moving an element to a
// new address with memcpy/memmove and abandoning the old bytes (no destructor
// on the source) must be equivalent to a move. Value and TableEntry qualify:
// they hold no self-pointers, and their copy/destroy semantics are refcount
// ticks that relocation never triggers. Growth and insert/erase relocate raw
// bytes; destructors run exactly once per live element (on erase, shrink, or
// container destruction).
// ============================================================================

template <typename T, size_t InlineN>
class SmallVec {
public:
    SmallVec() : data_((T*)inline_), size_(0), cap_(InlineN) {}

    SmallVec(const SmallVec&) = delete;
    SmallVec& operator=(const SmallVec&) = delete;

    // Move: steal a spilled buffer outright; relocate inline contents by
    // memcpy (the element contract). The source is left empty either way.
    SmallVec(SmallVec&& other) noexcept : size_(other.size_) {
        if (other.data_ != (T*)other.inline_) {
            data_ = other.data_;
            cap_ = other.cap_;
        } else {
            data_ = (T*)inline_;
            cap_ = InlineN;
            memcpy((void*)data_, (const void*)other.data_, size_ * sizeof(T));
        }
        other.data_ = (T*)other.inline_;
        other.size_ = 0;
        other.cap_ = InlineN;
    }


    ~SmallVec() {
        destroy_range(0, size_);
        if (data_ != (T*)inline_) free(data_);
    }

    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    T* data() { return data_; }
    const T* data() const { return data_; }

    T& operator[](size_t i) { return data_[i]; }
    const T& operator[](size_t i) const { return data_[i]; }

    T* begin() { return data_; }
    T* end() { return data_ + size_; }
    const T* begin() const { return data_; }
    const T* end() const { return data_ + size_; }

    T& back() { return data_[size_ - 1]; }
    const T& back() const { return data_[size_ - 1]; }

    void reserve(size_t n) {
        if (n > cap_) grow(n);
    }

    void push_back(const T& v) {
        if (size_ == cap_) grow(cap_ * 2);
        new (data_ + size_) T(v);
        size_++;
    }

    void pop_back() {
        size_--;
        data_[size_].~T();
    }

    void resize(size_t n) {
        if (n > size_) {
            reserve(n);
            for (size_t i = size_; i < n; i++) new (data_ + i) T();
        } else {
            destroy_range(n, size_);
        }
        size_ = n;
    }

    void resize(size_t n, const T& fill) {
        if (n > size_) {
            reserve(n);
            for (size_t i = size_; i < n; i++) new (data_ + i) T(fill);
        } else {
            destroy_range(n, size_);
        }
        size_ = n;
    }

    // Set the size without constructing elements — the slots hold garbage
    // until the caller initializes them (placement-new). Only valid for
    // callers that track occupancy externally and never destroy or read an
    // uninitialized slot (Table's tag bytes are the canonical example).
    void resizeNoInit(size_t n) {
        if (n > cap_) grow(n);
        size_ = n;
    }

    void assign(size_t n, const T& fill) {
        destroy_range(0, size_);
        size_ = 0;
        resize(n, fill);
    }

    void clear() {
        destroy_range(0, size_);
        size_ = 0;
    }

    // Forget the contents without running destructors — for callers that
    // RELOCATED the elements elsewhere (memcpy) under the trivially-
    // relocatable contract. The buffer is kept for reuse.
    void clearNoDestroy() { size_ = 0; }

    // Move-assign: destroys current contents, then steals per the move
    // constructor's rules (spilled buffer taken outright, inline contents
    // relocated).
    SmallVec& operator=(SmallVec&& other) noexcept {
        if (this == &other) return *this;
        destroy_range(0, size_);
        if (data_ != (T*)inline_) free(data_);
        if (other.data_ != (T*)other.inline_) {
            data_ = other.data_;
            cap_ = other.cap_;
        } else {
            data_ = (T*)inline_;
            cap_ = InlineN;
            memcpy((void*)data_, (const void*)other.data_, other.size_ * sizeof(T));
        }
        size_ = other.size_;
        other.data_ = (T*)other.inline_;
        other.size_ = 0;
        other.cap_ = InlineN;
        return *this;
    }

    // Iterator-position insert/erase (relocation via memmove; see contract).
    void insert(T* pos, const T& v) {
        size_t idx = (size_t)(pos - data_);
        if (size_ == cap_) grow(cap_ * 2);
        memmove(data_ + idx + 1, data_ + idx, (size_ - idx) * sizeof(T));
        new (data_ + idx) T(v);
        size_++;
    }

    void erase(T* pos) {
        size_t idx = (size_t)(pos - data_);
        data_[idx].~T();
        memmove(data_ + idx, data_ + idx + 1, (size_ - idx - 1) * sizeof(T));
        size_--;
    }

private:
    void destroy_range(size_t from, size_t to) {
        for (size_t i = from; i < to; i++) data_[i].~T();
    }

    void grow(size_t n) {
        size_t new_cap = cap_ * 2 > n ? cap_ * 2 : n;
        T* nd = (T*)malloc(new_cap * sizeof(T));
        memcpy((void*)nd, (const void*)data_, size_ * sizeof(T));   // relocate
        if (data_ != (T*)inline_) free(data_);
        data_ = nd;
        cap_ = new_cap;
    }

    T* data_;
    size_t size_;
    size_t cap_;
    alignas(T) unsigned char inline_[InlineN * sizeof(T)];
};

#endif // MOBIUS_SMALL_VEC_H
