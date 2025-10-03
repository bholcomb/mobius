#ifndef MOBIUS_ARRAY_H
#define MOBIUS_ARRAY_H

#include <stddef.h>

#include "data/value.h"

// Dynamic array structure with reference counting
typedef struct ArrayValue {
    Value* elements;      // Array of values
    size_t length;        // Current number of elements
    size_t capacity;      // Allocated capacity
    int ref_count;        // Reference count for memory management
} ArrayValue;

// Array management functions
ArrayValue* array_create(size_t initial_capacity);
ArrayValue* array_retain(ArrayValue* array);
void array_release(ArrayValue* array);
void array_push(ArrayValue* array, Value value);
Value array_pop(ArrayValue* array);
Value array_get(ArrayValue* array, size_t index);
void array_set(ArrayValue* array, size_t index, Value value);
size_t array_length(ArrayValue* array);
void array_resize(ArrayValue* array, size_t new_capacity);

#endif // MOBIUS_ARRAY_H    