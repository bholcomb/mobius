#include "data/array.h"
#include "data/value.h"

ArrayValue::ArrayValue(size_t initial_capacity)
{
    elements.reserve(initial_capacity > 0 ? initial_capacity : 8);
}

ArrayValue::~ArrayValue()
{
}

ArrayValue* ArrayValue::retain() {
    RefCounted::retain();
    return this;
}

void ArrayValue::push(Value value) {
    elements.push_back(value);
}

Value ArrayValue::pop() {
    if (elements.empty()) {
        return make_nil_value();
    }

    Value result = std::move(elements.back());
    elements.pop_back();
    return result;
}

Value ArrayValue::get(size_t index) const {
    if (index >= elements.size()) {
        return make_nil_value();
    }
    return elements[index];
}

void ArrayValue::set(size_t index, Value value) {
    if (index >= elements.size()) return;

    elements[index] = value;
}

size_t ArrayValue::length() const {
    return elements.size();
}

void ArrayValue::reserve(size_t new_capacity) {
    elements.reserve(new_capacity);
}

void ArrayValue::reverse() {
    std::reverse(elements.begin(), elements.end());
}

const Value& ArrayValue::operator[](size_t index) const {
    return elements[index];
}

Value& ArrayValue::operator[](size_t index) {
    return elements[index];
}