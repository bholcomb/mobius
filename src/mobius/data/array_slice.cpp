#include "data/array_slice.h"
#include "data/shared_cell.h"

ArraySlice::ArraySlice(ArrayValue* parent, size_t offset, size_t length, SharedCell* owner_cell)
    : parent_(parent), offset_(offset), length_(length), owner_cell_(owner_cell)
{
    if (parent_) {
        parent_->retain();
        parent_->acquireSlice();
    }
    if (owner_cell_) owner_cell_->retain();
}

ArraySlice::~ArraySlice() {
    if (parent_) {
        parent_->releaseSlice();
        parent_->RefCounted::release();
    }
    if (owner_cell_) owner_cell_->release();
}

Value ArraySlice::get(size_t index) const {
    if (!parent_ || index >= length_) {
        return make_nil_value();
    }
    size_t parent_index = offset_ + index;
    if (parent_index < offset_ || parent_index >= parent_->length()) {
        return make_nil_value();
    }
    if (owner_cell_) {
        std::lock_guard<std::recursive_mutex> lock(owner_cell_->mutex());
        return parent_->get(parent_index);
    }
    return parent_->get(parent_index);
}

void ArraySlice::set(size_t index, const Value& value) {
    if (!parent_ || index >= length_) {
        return;
    }
    size_t parent_index = offset_ + index;
    if (parent_index < offset_ || parent_index >= parent_->length()) {
        return;
    }
    if (owner_cell_) {
        std::lock_guard<std::recursive_mutex> lock(owner_cell_->mutex());
        parent_->set(parent_index, value);
        return;
    }
    parent_->set(parent_index, value);
}
