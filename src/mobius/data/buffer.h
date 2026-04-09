#ifndef MOBIUS_BUFFER_H
#define MOBIUS_BUFFER_H

#include "internal/ref_counted.h"

#include <cstddef>
#include <cstdint>

class BufferValue : public RefCounted {
public:
    typedef void (*ReleaseFn)(void* ptr, size_t size, void* userdata);

    BufferValue(size_t size = 0, uint8_t fill = 0, bool fixed = false, bool readonly = false);
    BufferValue(void* external_data, size_t size, ReleaseFn release_fn,
                void* release_userdata, bool readonly);
    ~BufferValue() override;

    BufferValue* retain();

    size_t size() const { return size_; }
    size_t capacity() const { return capacity_; }
    bool isFixed() const { return fixed_; }
    bool isReadonly() const { return readonly_; }
    bool isExternal() const { return external_; }

    uint8_t* data() { return data_; }
    const uint8_t* data() const { return data_; }
    uintptr_t address() const { return (uintptr_t)data_; }

    bool reserve(size_t new_capacity);
    bool resize(size_t new_size, uint8_t fill = 0);
    bool set(size_t index, uint8_t value);
    uint8_t get(size_t index) const;

    bool appendByte(uint8_t value);
    bool appendBytes(const uint8_t* bytes, size_t len);

    BufferValue* clone() const;

private:
    bool allocate(size_t size, uint8_t fill, bool fixed, bool readonly);

    uint8_t* data_ = nullptr;
    size_t size_ = 0;
    size_t capacity_ = 0;
    bool fixed_ = false;
    bool readonly_ = false;
    bool external_ = false;
    ReleaseFn release_fn_ = nullptr;
    void* release_userdata_ = nullptr;
};

#endif // MOBIUS_BUFFER_H
