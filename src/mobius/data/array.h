#ifndef MOBIUS_ARRAY_H
#define MOBIUS_ARRAY_H

#include <cstddef>
#include <vector>
#include <algorithm>

#include "data/value.h"
#include "internal/ref_counted.h"

class ArrayValue : public RefCounted {
public:
    ArrayValue(size_t initial_capacity = 8);
    ~ArrayValue() override;

    ArrayValue* retain();

    void push(Value value);
    Value pop();
    Value get(size_t index) const;
    void set(size_t index, Value value);
    void insert(size_t index, Value value);
    Value remove(size_t index);
    size_t length() const;
    void reserve(size_t new_capacity);
    void reverse();

    const Value& operator[](size_t index) const;
    Value& operator[](size_t index);

    Value* data() { return elements.data(); }
    const Value* data() const { return elements.data(); }

private:
    std::vector<Value> elements;
};

#endif // MOBIUS_ARRAY_H
