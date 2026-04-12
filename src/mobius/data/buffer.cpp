#include "data/buffer.h"

#include <cstring>
#include <limits>
#include <new>

BufferValue::BufferValue(size_t size, uint8_t fill, bool fixed, bool readonly) {
    allocate(size, fill, fixed, readonly);
}

BufferValue::BufferValue(void* external_data, size_t size, ReleaseFn release_fn,
                         void* release_userdata, bool readonly)
    : data_(static_cast<uint8_t*>(external_data)),
      size_(size),
      capacity_(size),
      fixed_(true),
      readonly_(readonly),
      external_(true),
      release_fn_(release_fn),
      release_userdata_(release_userdata) {
}

BufferValue::~BufferValue() {
    if (external_) {
        if (release_fn_ && data_) {
            release_fn_(data_, size_, release_userdata_);
        }
        data_ = nullptr;
        return;
    }
    delete[] data_;
    data_ = nullptr;
}

BufferValue* BufferValue::retain() {
    RefCounted::retain();
    return this;
}

bool BufferValue::allocate(size_t size, uint8_t fill, bool fixed, bool readonly) {
    ok_ = true;
    fixed_ = fixed;
    readonly_ = readonly;
    external_ = false;
    release_fn_ = nullptr;
    release_userdata_ = nullptr;
    size_ = size;
    capacity_ = size;
    if (size == 0) {
        data_ = nullptr;
        return true;
    }
    data_ = new (std::nothrow) uint8_t[size];
    if (!data_) {
        ok_ = false;
        size_ = 0;
        capacity_ = 0;
        return false;
    }
    memset(data_, fill, size);
    return true;
}

bool BufferValue::reserve(size_t new_capacity) {
    if (fixed_ || new_capacity <= capacity_) return !fixed_ || new_capacity <= capacity_;
    uint8_t* next = new (std::nothrow) uint8_t[new_capacity];
    if (!next) return false;
    if (data_ && size_ > 0) memcpy(next, data_, size_);
    delete[] data_;
    data_ = next;
    capacity_ = new_capacity;
    return true;
}

bool BufferValue::resize(size_t new_size, uint8_t fill) {
    if (fixed_ && new_size != size_) return false;
    if (readonly_ && new_size != size_) return false;
    if (new_size > capacity_) {
        size_t new_capacity = capacity_ > 0 ? capacity_ : 8;
        while (new_capacity < new_size) {
            if (new_capacity > std::numeric_limits<size_t>::max() / 2) {
                new_capacity = new_size;
                break;
            }
            new_capacity *= 2;
        }
        if (!reserve(new_capacity)) return false;
    }
    if (new_size > size_ && data_) {
        memset(data_ + size_, fill, new_size - size_);
    }
    size_ = new_size;
    return true;
}

bool BufferValue::set(size_t index, uint8_t value) {
    if (readonly_ || index >= size_ || !data_) return false;
    data_[index] = value;
    return true;
}

uint8_t BufferValue::get(size_t index) const {
    if (index >= size_ || !data_) return 0;
    return data_[index];
}

bool BufferValue::appendByte(uint8_t value) {
    if (size_ == std::numeric_limits<size_t>::max()) return false;
    if (!resize(size_ + 1, 0)) return false;
    data_[size_ - 1] = value;
    return true;
}

bool BufferValue::appendBytes(const uint8_t* bytes, size_t len) {
    if (!bytes && len > 0) return false;
    size_t old_size = size_;
    if (len > std::numeric_limits<size_t>::max() - size_) return false;
    if (!resize(size_ + len, 0)) return false;
    if (len > 0) memcpy(data_ + old_size, bytes, len);
    return true;
}

BufferValue* BufferValue::clone() const {
    BufferValue* copy = new (std::nothrow) BufferValue(size_, 0, fixed_, readonly_);
    if (!copy || !copy->ok()) {
        if (copy) copy->release();
        return nullptr;
    }
    if (size_ > 0 && data_) memcpy(copy->data_, data_, size_);
    return copy;
}
