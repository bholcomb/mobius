#include "data/shared_cell.h"
#include "data/value.h"

SharedCell::SharedCell(const Value& initial) {
    value_ = new (std::nothrow) Value(initial);
}

SharedCell::~SharedCell() {
    delete value_;
}

Value SharedCell::load() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!value_) return Value();
    return *value_;
}

void SharedCell::store(const Value& val) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!value_) {
        value_ = new (std::nothrow) Value(val);
        return;
    }
    *value_ = val;
}
