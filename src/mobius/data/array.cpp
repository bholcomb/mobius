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

Value ArrayValue::pop() {
    if (elements.empty()) {
        return make_nil_value();
    }

    Value result = std::move(elements.back());
    elements.pop_back();
    return result;
}

void ArrayValue::insert(size_t index, Value value) {
    if (index > elements.size()) index = elements.size();
    elements.insert(elements.begin() + (ptrdiff_t)index, value);
}

Value ArrayValue::remove(size_t index) {
    if (index >= elements.size()) return make_nil_value();
    Value result = std::move(elements[index]);
    elements.erase(elements.begin() + (ptrdiff_t)index);
    return result;
}

void ArrayValue::reserve(size_t new_capacity) {
    elements.reserve(new_capacity);
}

void ArrayValue::reverse() {
    std::reverse(elements.begin(), elements.end());
}
