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

    inline void push(const Value& value) { elements.push_back(value); }
    Value pop();
    inline const Value& get(size_t index) const { return elements[index]; }
    inline void set(size_t index, const Value& value) { elements[index] = value; }
    void insert(size_t index, Value value);
    Value remove(size_t index);
    inline size_t length() const { return elements.size(); }
    void reserve(size_t new_capacity);
    void reverse();

    inline const Value& operator[](size_t index) const { return elements[index]; }
    inline Value& operator[](size_t index) { return elements[index]; }

    Value* data() { return elements.data(); }
    const Value* data() const { return elements.data(); }

private:
    std::vector<Value> elements;
};

#endif // MOBIUS_ARRAY_H
