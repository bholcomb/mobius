#ifndef MOBIUS_ARRAY_H
#define MOBIUS_ARRAY_H

#include <cstddef>
#include <vector>
#include <algorithm>
#include <shared_mutex>

#include "data/value.h"
#include "internal/ref_counted.h"

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

    inline size_t length() const {
        if (MOBIUS_UNLIKELY(shared_)) {
            std::shared_lock lock(mutex_);
            return elements.size();
        }
        return elements.size();
    }

    inline const Value& unsafeGet(size_t index) const { return elements[index]; }

    void reserve(size_t new_capacity);
    void reverse();

    const Value& operator[](size_t index) const;
    Value& operator[](size_t index);

    Value* data() { return elements.data(); }
    const Value* data() const { return elements.data(); }

    void markShared();
    bool isShared() const { return shared_; }
    std::shared_mutex& mutex() { return mutex_; }

private:
    std::vector<Value> elements;
    bool shared_ = false;
    mutable std::shared_mutex mutex_;
};

#endif // MOBIUS_ARRAY_H
