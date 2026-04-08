#include "data/shared_cell.h"
#include "data/value.h"

SharedCell::SharedCell(const Value& initial) {
    value_ = new Value(initial);
}

SharedCell::~SharedCell() {
    delete value_;
}

Value SharedCell::load() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return *value_;
}

void SharedCell::store(const Value& val) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    *value_ = val;
}
