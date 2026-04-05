#ifndef MOBIUS_ARRAY_SLICE_H
#define MOBIUS_ARRAY_SLICE_H

#include "data/array.h"
#include "internal/ref_counted.h"

class ArraySlice : public RefCounted {
public:
    ArraySlice(ArrayValue* parent, size_t offset, size_t length);
    ~ArraySlice() override;

    const Value& get(size_t index) const;
    void set(size_t index, const Value& value);
    size_t length() const { return length_; }

    ArrayValue* parent() const { return parent_; }
    size_t offset() const { return offset_; }

private:
    ArrayValue* parent_;
    size_t offset_;
    size_t length_;
};

#endif // MOBIUS_ARRAY_SLICE_H
