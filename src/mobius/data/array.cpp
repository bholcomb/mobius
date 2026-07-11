#include "data/array.h"
#include "data/table.h"
#include "data/value.h"

static Value& invalid_array_value() {
    // Handed out for out-of-bounds access through the non-const operator[],
    // so a caller CAN write through it. thread_local + reset-to-nil on every
    // call keeps a stray OOB write from (a) racing across fibers and (b)
    // becoming the permanent result of every later OOB read.
    static thread_local Value nil_value;
    nil_value = make_nil_value();
    return nil_value;
}

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

void ArrayValue::push(const Value& value) {
    if (MOBIUS_UNLIKELY(shared_)) {
        std::lock_guard lock(mutex_);
        elements.push_back(value);
    } else {
        elements.push_back(value);
    }
}

Value ArrayValue::pop() {
    if (MOBIUS_UNLIKELY(shared_)) {
        std::lock_guard lock(mutex_);
        if (elements.empty()) return make_nil_value();
        Value result = std::move(elements.back());
        elements.pop_back();
        return result;
    }

    if (elements.empty()) return make_nil_value();
    Value result = std::move(elements.back());
    elements.pop_back();
    return result;
}

const Value& ArrayValue::get(size_t index) const {
    if (MOBIUS_UNLIKELY(shared_)) {
        std::lock_guard lock(mutex_);
        if (index >= elements.size()) return invalid_array_value();
        return elements[index];
    }
    if (index >= elements.size()) return invalid_array_value();
    return elements[index];
}

void ArrayValue::set(size_t index, const Value& value) {
    if (MOBIUS_UNLIKELY(shared_)) {
        std::lock_guard lock(mutex_);
        if (index >= elements.size()) return;
        elements[index] = value;
    } else {
        if (index >= elements.size()) return;
        elements[index] = value;
    }
}

void ArrayValue::insert(size_t index, Value value) {
    if (MOBIUS_UNLIKELY(shared_)) {
        std::lock_guard lock(mutex_);
        if (index > elements.size()) index = elements.size();
        elements.insert(elements.begin() + (ptrdiff_t)index, value);
    } else {
        if (index > elements.size()) index = elements.size();
        elements.insert(elements.begin() + (ptrdiff_t)index, value);
    }
}

Value ArrayValue::remove(size_t index) {
    if (MOBIUS_UNLIKELY(shared_)) {
        std::lock_guard lock(mutex_);
        if (index >= elements.size()) return make_nil_value();
        Value result = std::move(elements[index]);
        elements.erase(elements.begin() + (ptrdiff_t)index);
        return result;
    }

    if (index >= elements.size()) return make_nil_value();
    Value result = std::move(elements[index]);
    elements.erase(elements.begin() + (ptrdiff_t)index);
    return result;
}

void ArrayValue::reserve(size_t new_capacity) {
    if (MOBIUS_UNLIKELY(shared_)) {
        std::lock_guard lock(mutex_);
        elements.reserve(new_capacity);
    } else {
        elements.reserve(new_capacity);
    }
}

void ArrayValue::reverse() {
    if (MOBIUS_UNLIKELY(shared_)) {
        std::lock_guard lock(mutex_);
        std::reverse(elements.begin(), elements.end());
    } else {
        std::reverse(elements.begin(), elements.end());
    }
}

const Value& ArrayValue::operator[](size_t index) const {
    if (MOBIUS_UNLIKELY(shared_)) {
        std::lock_guard lock(mutex_);
        if (index >= elements.size()) return invalid_array_value();
        return elements[index];
    }
    if (index >= elements.size()) return invalid_array_value();
    return elements[index];
}

Value& ArrayValue::operator[](size_t index) {
    if (MOBIUS_UNLIKELY(shared_)) {
        std::lock_guard lock(mutex_);
        if (index >= elements.size()) return invalid_array_value();
        return elements[index];
    }
    if (index >= elements.size()) return invalid_array_value();
    return elements[index];
}

void ArrayValue::markShared() {
    if (shared_) return;
    shared_ = true;
    for (auto& elem : elements) {
        if (elem.type == VAL_ARRAY && elem.as.array) {
            elem.as.array->markShared();
            elem.flags |= VAL_FLAG_SHARED;
        } else if (elem.type == VAL_TABLE && elem.as.table) {
            elem.as.table->markShared();
            elem.flags |= VAL_FLAG_SHARED;
        }
    }
}
