#include "data/array.h"
#include "data/value.h"

#include <stdlib.h>

// ============================================================================
// ARRAY REFERENCE COUNTING IMPLEMENTATION
// ============================================================================

ArrayValue* array_create(size_t initial_capacity) {
    ArrayValue* array = calloc(1, sizeof(ArrayValue));
    if (!array) return NULL;
    
    array->capacity = initial_capacity > 0 ? initial_capacity : 8;  // Default capacity
    array->elements = calloc(array->capacity, sizeof(Value));
    if (!array->elements) {
        free(array);
        return NULL;
    }
    
    array->length = 0;
    array->ref_count = 1;
    
    return array;
}

ArrayValue* array_retain(ArrayValue* array) {
    if (array) {
        array->ref_count++;
    }
    return array;
}

void array_release(ArrayValue* array) {
    if (!array) return;
    
    array->ref_count--;
    if (array->ref_count <= 0) {
        // Free all elements
        for (size_t i = 0; i < array->length; i++) {
            free_value(array->elements[i]);
        }
        
        // Free the elements array
        free(array->elements);
        
        // Free the array structure
        free(array);
    }
}

void array_push(ArrayValue* array, Value value) {
    if (!array) return;
    
    // Resize if necessary
    if (array->length >= array->capacity) {
        array_resize(array, array->capacity * 2);
    }
    
    // Add the element (copy to handle reference counting)
    array->elements[array->length] = copy_value(value);
    array->length++;
}

Value array_pop(ArrayValue* array) {
    if (!array || array->length == 0) {
        return make_nil_value();
    }
    
    array->length--;
    Value result = array->elements[array->length];
    
    // Clear the slot (set to nil without freeing, since we're returning it)
    array->elements[array->length] = make_nil_value();
    
    return result;
}

Value array_get(ArrayValue* array, size_t index) {
    if (!array || index >= array->length) {
        return make_nil_value();
    }
    
    return copy_value(array->elements[index]);
}

void array_set(ArrayValue* array, size_t index, Value value) {
    if (!array || index >= array->length) return;
    
    // Free the old value
    free_value(array->elements[index]);
    
    // Set the new value (copy to handle reference counting)
    array->elements[index] = copy_value(value);
}

size_t array_length(ArrayValue* array) {
    return array ? array->length : 0;
}

void array_resize(ArrayValue* array, size_t new_capacity) {
    if (!array || new_capacity <= array->capacity) return;
    
    Value* new_elements = realloc(array->elements, new_capacity * sizeof(Value));
    if (!new_elements) return;  // Failed to resize
    
    // Initialize new slots to nil
    for (size_t i = array->capacity; i < new_capacity; i++) {
        new_elements[i] = make_nil_value();
    }
    
    array->elements = new_elements;
    array->capacity = new_capacity;
}
