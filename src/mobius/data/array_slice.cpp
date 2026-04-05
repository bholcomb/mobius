#include "data/array_slice.h"

ArraySlice::ArraySlice(ArrayValue* parent, size_t offset, size_t length)
    : parent_(parent), offset_(offset), length_(length)
{
    if (parent_) parent_->retain();
}

ArraySlice::~ArraySlice() {
    if (parent_) parent_->RefCounted::release();
}

const Value& ArraySlice::get(size_t index) const {
    return parent_->get(offset_ + index);
}

void ArraySlice::set(size_t index, const Value& value) {
    parent_->set(offset_ + index, value);
}
