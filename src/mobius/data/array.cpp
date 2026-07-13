#include "data/array.h"
#include "data/table.h"
#include "data/value.h"


// ----------------------------------------------------------------------------
// Pool-backed operator new/delete. Exact-size allocations use the per-thread
// GC pools; any other size (a subclass) uses the global heap. The unsized /
// nothrow deletes assume the class size — a larger (glibc-origin) chunk that
// reaches the pool is absorbed safely: chunks never return to the global
// heap, so the size routing can never mismatch an actual glibc free.
// ----------------------------------------------------------------------------
void* ArrayValue::operator new(size_t sz) {
    if (sz == sizeof(ArrayValue))
        if (void* p = gc_object_alloc(GC_ARRAY, sz)) return p;
    return ::operator new(sz);
}
void* ArrayValue::operator new(size_t sz, const std::nothrow_t&) noexcept {
    if (sz == sizeof(ArrayValue))
        if (void* p = gc_object_alloc(GC_ARRAY, sz)) return p;
    return ::operator new(sz, std::nothrow);
}
void ArrayValue::operator delete(void* p, size_t sz) noexcept {
    (void)sz;
    if (p) gc_object_free(GC_ARRAY, p);
}
void ArrayValue::operator delete(void* p) noexcept {
    if (p) gc_object_free(GC_ARRAY, p);
}
void ArrayValue::operator delete(void* p, const std::nothrow_t&) noexcept {
    if (p) gc_object_free(GC_ARRAY, p);
}
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
    setGcManaged();
    gc_track(&gc_, GC_ARRAY, this);
}

ArrayValue::~ArrayValue()
{
    gc_untrack(&gc_);
}

ArrayValue* ArrayValue::retain() {
    RefCounted::retain();
    return this;
}

void ArrayValue::push(const Value& value) {
    elements.push_back(value);
}

Value ArrayValue::pop() {
    if (elements.empty()) return make_nil_value();
    Value result = std::move(elements.back());
    elements.pop_back();
    return result;
}

const Value& ArrayValue::get(size_t index) const {
    if (index >= elements.size()) return invalid_array_value();
    return elements[index];
}

void ArrayValue::set(size_t index, const Value& value) {
    if (index >= elements.size()) return;
    elements[index] = value;
}

void ArrayValue::insert(size_t index, Value value) {
    if (index > elements.size()) index = elements.size();
    elements.insert(elements.begin() + (ptrdiff_t)index, std::move(value));
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

const Value& ArrayValue::operator[](size_t index) const {
    if (index >= elements.size()) return invalid_array_value();
    return elements[index];
}

Value& ArrayValue::operator[](size_t index) {
    if (index >= elements.size()) return invalid_array_value();
    return elements[index];
}

