#include "data/shared_cell.h"
#include "data/value.h"
#include "data/array.h"
#include "data/table.h"
#include "data/buffer.h"

#include <utility>
#include <vector>

static SharedCell* owner_of(const Value& v) {
    switch (v.type) {
        case VAL_ARRAY:  return v.as.array ? v.as.array->ownerCell() : nullptr;
        case VAL_TABLE:  return v.as.table ? v.as.table->ownerCell() : nullptr;
        case VAL_BUFFER: return v.as.buffer ? v.as.buffer->ownerCell() : nullptr;
        default:         return nullptr;
    }
}

static bool is_shareable_container(const Value& v) {
    return (v.type == VAL_ARRAY && v.as.array) ||
           (v.type == VAL_TABLE && v.as.table) ||
           (v.type == VAL_BUFFER && v.as.buffer);
}

static void set_owner(const Value& v, SharedCell* cell) {
    switch (v.type) {
        case VAL_ARRAY:  v.as.array->setOwnerCell(cell); break;
        case VAL_TABLE:  v.as.table->setOwnerCell(cell); break;
        case VAL_BUFFER: v.as.buffer->setOwnerCell(cell); break;
        default: break;
    }
}

Value share_for_cell(const Value& v) {
    if (!is_shareable_container(v)) return v;
    SharedCell* cell = owner_of(v);
    if (cell) {
        ((RefCounted*)cell)->retain();
    } else {
        cell = new (std::nothrow) SharedCell(v);
        if (!cell) return v;
    }
    Value cv = make_shared_cell_value(cell);
    cv.flags |= VAL_FLAG_SHARED;
    return cv;
}

void SharedCell::adopt(const Value& v) {
    if (!is_shareable_container(v) || owner_of(v)) return;
    set_owner(v, this);
    // Re-store nested containers: the container now has an owner, so its
    // store functions wrap them (recursively, through share_for_cell).
    if (v.type == VAL_ARRAY) {
        ArrayValue* arr = v.as.array;
        for (size_t i = 0; i < arr->length(); i++) {
            Value elem = arr->get(i);
            if (is_shareable_container(elem)) arr->set(i, elem);
        }
    } else if (v.type == VAL_TABLE) {
        // Collect first: storing may not reorder entries mid-iteration.
        std::vector<std::pair<Value, Value>> nested;
        v.as.table->forEach([&](const Value& key, const Value& value) {
            if (is_shareable_container(value)) nested.emplace_back(key, value);
        });
        for (auto& kv : nested) v.as.table->set(kv.first, kv.second);
    }
}

void SharedCell::release_ownership() {
    if (value_ && owner_of(*value_) == this) set_owner(*value_, nullptr);
}

SharedCell::SharedCell(const Value& initial) {
    value_ = new (std::nothrow) Value(initial);
    if (value_) adopt(*value_);
}

SharedCell::~SharedCell() {
    release_ownership();
    delete value_;
}

Value SharedCell::load() {
    std::lock_guard<FiberMutex> lock(mutex_);
    if (!value_) return Value();
    return *value_;
}

void SharedCell::store(const Value& val) {
    std::lock_guard<FiberMutex> lock(mutex_);
    if (!value_) {
        value_ = new (std::nothrow) Value(val);
        if (value_) adopt(*value_);
        return;
    }
    release_ownership();
    *value_ = val;
    adopt(*value_);
}
