#include "bytecode_array.h"
#include "bytecode.h"
#include "value.h"
#include <stdio.h>
#include <stdlib.h>

// Forward declaration for vm_runtime_error and vm_peek (they'll be in bytecode.h)
extern void vm_runtime_error(MobiusVM* vm, const char* format, ...);
extern Value vm_peek(MobiusVM* vm, int distance);

// =============================================================================
// ARRAY BUILTIN FUNCTIONS FOR BYTECODE VM
// =============================================================================

void builtin_array_create_vm(MobiusVM* vm, int arg_count, void* result_ptr) {
    Value* result = (Value*)result_ptr;
    if (arg_count > 1) {
        vm_runtime_error(vm, "array_create expects 0 or 1 arguments");
        *result = make_nil_value();
        return;
    }

    size_t capacity = 8; // Default capacity
    if (arg_count == 1) {
        Value arg = vm_peek(vm, 0);
        if (arg.type != VAL_INTEGER) {
            vm_runtime_error(vm, "array_create capacity must be an integer");
            *result = make_nil_value();
            return;
        }
        capacity = (size_t)arg.as.integer.value.i64;
        if (capacity == 0) capacity = 8; // Minimum capacity
    }

    ArrayValue* array = array_create(capacity);
    if (!array) {
        vm_runtime_error(vm, "Failed to create array");
        *result = make_nil_value();
        return;
    }

    *result = make_array_value(array);
}

void builtin_array_push_vm(MobiusVM* vm, int arg_count, void* result_ptr) {
    Value* result = (Value*)result_ptr;
    if (arg_count != 2) {
        vm_runtime_error(vm, "array_push expects 2 arguments");
        *result = make_nil_value();
        return;
    }

    Value array_val = vm_peek(vm, 1);
    Value value = vm_peek(vm, 0);
    
    if (array_val.type != VAL_ARRAY) {
        vm_runtime_error(vm, "array_push first argument must be an array");
        *result = make_nil_value();
        return;
    }

    array_push(array_val.as.array, value);
    *result = make_nil_value();
}

void builtin_array_pop_vm(MobiusVM* vm, int arg_count, void* result_ptr) {
    Value* result = (Value*)result_ptr;
    if (arg_count != 1) {
        vm_runtime_error(vm, "array_pop expects 1 argument");
        *result = make_nil_value();
        return;
    }

    Value array_val = vm_peek(vm, 0);
    
    if (array_val.type != VAL_ARRAY) {
        vm_runtime_error(vm, "array_pop argument must be an array");
        *result = make_nil_value();
        return;
    }

    ArrayValue* array = array_val.as.array;
    if (array->length == 0) {
        vm_runtime_error(vm, "Cannot pop from empty array");
        *result = make_nil_value();
        return;
    }

    *result = array_pop(array);
}

void builtin_array_get_vm(MobiusVM* vm, int arg_count, void* result_ptr) {
    Value* result = (Value*)result_ptr;
    if (arg_count != 2) {
        vm_runtime_error(vm, "array_get expects 2 arguments");
        *result = make_nil_value();
        return;
    }

    Value array_val = vm_peek(vm, 1);
    Value index_val = vm_peek(vm, 0);
    
    if (array_val.type != VAL_ARRAY) {
        vm_runtime_error(vm, "array_get first argument must be an array");
        *result = make_nil_value();
        return;
    }
    
    if (index_val.type != VAL_INTEGER) {
        vm_runtime_error(vm, "array_get second argument must be an integer");
        *result = make_nil_value();
        return;
    }

    size_t index = (size_t)index_val.as.integer.value.i64;
    *result = array_get(array_val.as.array, index);
}

void builtin_array_set_vm(MobiusVM* vm, int arg_count, void* result_ptr) {
    Value* result = (Value*)result_ptr;
    if (arg_count != 3) {
        vm_runtime_error(vm, "array_set expects 3 arguments");
        *result = make_nil_value();
        return;
    }

    Value array_val = vm_peek(vm, 2);
    Value index_val = vm_peek(vm, 1);
    Value value = vm_peek(vm, 0);
    
    if (array_val.type != VAL_ARRAY) {
        vm_runtime_error(vm, "array_set first argument must be an array");
        *result = make_nil_value();
        return;
    }
    
    if (index_val.type != VAL_INTEGER) {
        vm_runtime_error(vm, "array_set second argument must be an integer");
        *result = make_nil_value();
        return;
    }

    size_t index = (size_t)index_val.as.integer.value.i64;
    array_set(array_val.as.array, index, value);
    *result = make_nil_value();
}

void builtin_array_length_vm(MobiusVM* vm, int arg_count, void* result_ptr) {
    Value* result = (Value*)result_ptr;
    if (arg_count != 1) {
        vm_runtime_error(vm, "array_length expects 1 argument");
        *result = make_nil_value();
        return;
    }

    Value array_val = vm_peek(vm, 0);
    
    if (array_val.type != VAL_ARRAY) {
        vm_runtime_error(vm, "array_length argument must be an array");
        *result = make_nil_value();
        return;
    }

    ArrayValue* array = array_val.as.array;
    *result = make_integer_value(NUM_INT64, (int64_t)array->length);
}

void builtin_array_slice_vm(MobiusVM* vm, int arg_count, void* result_ptr) {
    Value* result = (Value*)result_ptr;
    if (arg_count < 2 || arg_count > 3) {
        vm_runtime_error(vm, "array_slice expects 2 or 3 arguments");
        *result = make_nil_value();
        return;
    }

    Value array_val = vm_peek(vm, arg_count - 1);
    Value start_val = vm_peek(vm, arg_count - 2);
    
    if (array_val.type != VAL_ARRAY) {
        vm_runtime_error(vm, "array_slice first argument must be an array");
        *result = make_nil_value();
        return;
    }
    
    if (start_val.type != VAL_INTEGER) {
        vm_runtime_error(vm, "array_slice start index must be an integer");
        *result = make_nil_value();
        return;
    }

    ArrayValue* source = array_val.as.array;
    size_t start = (size_t)start_val.as.integer.value.i64;
    size_t end = source->length;
    
    if (arg_count == 3) {
        Value end_val = vm_peek(vm, 0);
        if (end_val.type != VAL_INTEGER) {
            vm_runtime_error(vm, "array_slice end index must be an integer");
            *result = make_nil_value();
            return;
        }
        end = (size_t)end_val.as.integer.value.i64;
    }
    
    // Bounds checking
    if (start >= source->length || end > source->length || start >= end) {
        ArrayValue* empty_array = array_create(0);
        *result = make_array_value(empty_array);
        return;
    }
    
    // Create new array with slice
    size_t slice_length = end - start;
    ArrayValue* slice = array_create(slice_length);
    
    for (size_t i = 0; i < slice_length; i++) {
        Value element = array_get(source, start + i);
        array_push(slice, element);
        free_value(element);
    }
    
    *result = make_array_value(slice);
}

void builtin_array_concat_vm(MobiusVM* vm, int arg_count, void* result_ptr) {
    Value* result = (Value*)result_ptr;
    if (arg_count != 2) {
        vm_runtime_error(vm, "array_concat expects 2 arguments");
        *result = make_nil_value();
        return;
    }

    Value array1_val = vm_peek(vm, 1);
    Value array2_val = vm_peek(vm, 0);
    
    if (array1_val.type != VAL_ARRAY || array2_val.type != VAL_ARRAY) {
        vm_runtime_error(vm, "array_concat arguments must be arrays");
        *result = make_nil_value();
        return;
    }

    ArrayValue* array1 = array1_val.as.array;
    ArrayValue* array2 = array2_val.as.array;
    
    // Create new array with combined capacity
    ArrayValue* concat = array_create(array1->length + array2->length);
    
    // Copy first array
    for (size_t i = 0; i < array1->length; i++) {
        Value element = array_get(array1, i);
        array_push(concat, element);
        free_value(element);
    }
    
    // Copy second array
    for (size_t i = 0; i < array2->length; i++) {
        Value element = array_get(array2, i);
        array_push(concat, element);
        free_value(element);
    }
    
    *result = make_array_value(concat);
}

void builtin_array_reverse_vm(MobiusVM* vm, int arg_count, void* result_ptr) {
    Value* result = (Value*)result_ptr;
    if (arg_count != 1) {
        vm_runtime_error(vm, "array_reverse expects 1 argument");
        *result = make_nil_value();
        return;
    }

    Value array_val = vm_peek(vm, 0);
    
    if (array_val.type != VAL_ARRAY) {
        vm_runtime_error(vm, "array_reverse argument must be an array");
        *result = make_nil_value();
        return;
    }

    ArrayValue* array = array_val.as.array;
    
    // Reverse the array in place
    for (size_t i = 0; i < array->length / 2; i++) {
        size_t j = array->length - 1 - i;
        Value temp = array->elements[i];
        array->elements[i] = array->elements[j];
        array->elements[j] = temp;
    }
    
    *result = make_nil_value();
}

void builtin_array_find_vm(MobiusVM* vm, int arg_count, void* result_ptr) {
    Value* result = (Value*)result_ptr;
    if (arg_count != 2) {
        vm_runtime_error(vm, "array_find expects 2 arguments");
        *result = make_nil_value();
        return;
    }

    Value array_val = vm_peek(vm, 1);
    Value search_value = vm_peek(vm, 0);
    
    if (array_val.type != VAL_ARRAY) {
        vm_runtime_error(vm, "array_find first argument must be an array");
        *result = make_nil_value();
        return;
    }

    ArrayValue* array = array_val.as.array;
    
    // Search for the value
    for (size_t i = 0; i < array->length; i++) {
        Value element = array_get(array, i);
        if (values_equal(element, search_value)) {
            free_value(element);
            *result = make_integer_value(NUM_INT64, (int64_t)i);
            return;
        }
        free_value(element);
    }
    
    // Not found, return -1
    *result = make_integer_value(NUM_INT64, -1);
}
