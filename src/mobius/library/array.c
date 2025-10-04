#include "library/array.h"
#include "data/array.h"
#include "data/value.h"
#include "state/environment.h"
#include "eval/evaluator.h"

#include <stdio.h>
#include <stdlib.h>

// =============================================================================
// UNIFIED ARRAY FUNCTION IMPLEMENTATIONS
// =============================================================================

EvalResult lib_array_create(MobiusState* state, int arg_count) {
    if (arg_count > 1) {
        return make_error(state->main_context->current_env, "array_create expects 0 or 1 arguments", 0, 0);
    }

    size_t capacity = 8; // Default capacity
    if (arg_count == 1) {
        Value arg = ctx_peek(state->main_context, 0);
        ctx_pop(state->main_context); // Remove argument
        
        if (arg.type != VAL_INTEGER) {
            return make_error(state->main_context->current_env, "array_create capacity must be an integer", 0, 0);
        }
        capacity = (size_t)arg.as.integer.value.i64;
        if (capacity == 0) capacity = 8; // Minimum capacity
    }

    ArrayValue* array = array_create(capacity);
    if (!array) {
        return make_error(state->main_context->current_env, "Failed to create array", 0, 0);
    }

    ctx_push(state->main_context, make_array_value(array));
    return make_success(1);
}

EvalResult lib_array_push(MobiusState* state, int arg_count) {
    if (arg_count != 2) {
        return make_error(state->main_context->current_env, "array_push expects exactly 2 arguments (array, value)", 0, 0);
    }

    Value value = ctx_peek(state->main_context, 0);
    Value array_val = ctx_peek(state->main_context, 1);
    
    // Remove arguments
    ctx_pop(state->main_context);
    ctx_pop(state->main_context);

    if (array_val.type != VAL_ARRAY) {
        free_value(array_val);
        free_value(value);
        return make_error(state->main_context->current_env, "array_push first argument must be an array", 0, 0);
    }

    ArrayValue* array = array_val.as.array;
    array_push(array, value);

    free_value(array_val);
    free_value(value);
    return make_success(0);
}

EvalResult lib_array_pop(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "array_pop expects exactly 1 argument", 0, 0);
    }

    Value array_val = ctx_peek(state->main_context, 0);
    ctx_pop(state->main_context); // Remove argument

    if (array_val.type != VAL_ARRAY) {
        free_value(array_val);
        return make_error(state->main_context->current_env, "array_pop argument must be an array", 0, 0);
    }

    ArrayValue* array = array_val.as.array;
    if (array->length == 0) {
        free_value(array_val);
        ctx_push(state->main_context, make_nil_value());
        return make_success(1);
    }

    Value popped_value = array->elements[array->length - 1];
    array->length--;

    free_value(array_val);
    ctx_push(state->main_context, popped_value);
    return make_success(1);
}

EvalResult lib_array_get(MobiusState* state, int arg_count) {
    if (arg_count != 2) {
        return make_error(state->main_context->current_env, "array_get expects exactly 2 arguments (array, index)", 0, 0);
    }

    Value index_val = ctx_peek(state->main_context, 0);
    Value array_val = ctx_peek(state->main_context, 1);
    
    // Remove arguments
    ctx_pop(state->main_context);
    ctx_pop(state->main_context);

    if (array_val.type != VAL_ARRAY) {
        free_value(array_val);
        free_value(index_val);
        return make_error(state->main_context->current_env, "array_get first argument must be an array", 0, 0);
    }

    if (index_val.type != VAL_INTEGER) {
        free_value(array_val);
        free_value(index_val);
        return make_error(state->main_context->current_env, "array_get index must be an integer", 0, 0);
    }

    ArrayValue* array = array_val.as.array;
    int64_t index = index_val.as.integer.value.i64;

    if (index < 0 || index >= (int64_t)array->length) {
        free_value(array_val);
        free_value(index_val);
        ctx_push(state->main_context, make_nil_value());
        return make_success(1);
    }

    free_value(array_val);
    free_value(index_val);
    ctx_push(state->main_context, copy_value(array->elements[index]));
    return make_success(1);
}

EvalResult lib_array_set(MobiusState* state, int arg_count) {
    if (arg_count != 3) {
        return make_error(state->main_context->current_env, "array_set expects exactly 3 arguments (array, index, value)", 0, 0);
    }

    Value value = ctx_peek(state->main_context, 0);
    Value index_val = ctx_peek(state->main_context, 1);
    Value array_val = ctx_peek(state->main_context, 2);
    
    // Remove arguments
    for (int i = 0; i < 3; i++) {
        ctx_pop(state->main_context);
    }

    if (array_val.type != VAL_ARRAY) {
        free_value(array_val);
        free_value(index_val);
        free_value(value);
        return make_error(state->main_context->current_env, "array_set first argument must be an array", 0, 0);
    }

    if (index_val.type != VAL_INTEGER) {
        free_value(array_val);
        free_value(index_val);
        free_value(value);
        return make_error(state->main_context->current_env, "array_set index must be an integer", 0, 0);
    }

    ArrayValue* array = array_val.as.array;
    int64_t index = index_val.as.integer.value.i64;

    if (index < 0 || index >= (int64_t)array->length) {
        free_value(array_val);
        free_value(index_val);
        free_value(value);
        return make_error(state->main_context->current_env, "array_set index out of bounds", 0, 0);
    }

    // Free the old value and set the new one
    free_value(array->elements[index]);
    array->elements[index] = copy_value(value);

    free_value(array_val);
    free_value(index_val);
    free_value(value);
    return make_success(0);
}

EvalResult lib_array_length(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "array_length expects exactly 1 argument", 0, 0);
    }

    Value array_val = ctx_peek(state->main_context, 0);
    ctx_pop(state->main_context); // Remove argument

    if (array_val.type != VAL_ARRAY) {
        free_value(array_val);
        return make_error(state->main_context->current_env, "array_length argument must be an array", 0, 0);
    }

    ArrayValue* array = array_val.as.array;
    free_value(array_val);
    ctx_push(state->main_context, make_integer_value(NUM_INT64, (int64_t)array->length));
    return make_success(1);
}

EvalResult lib_array_slice(MobiusState* state, int arg_count) {
    if (arg_count != 3) {
        return make_error(state->main_context->current_env, "array_slice expects exactly 3 arguments (array, start, end)", 0, 0);
    }

    Value end_val = ctx_peek(state->main_context, 0);
    Value start_val = ctx_peek(state->main_context, 1);
    Value array_val = ctx_peek(state->main_context, 2);
    
    // Remove arguments
    for (int i = 0; i < 3; i++) {
        ctx_pop(state->main_context);
    }

    if (array_val.type != VAL_ARRAY) {
        free_value(array_val);
        free_value(start_val);
        free_value(end_val);
        return make_error(state->main_context->current_env, "array_slice first argument must be an array", 0, 0);
    }

    if (start_val.type != VAL_INTEGER || end_val.type != VAL_INTEGER) {
        free_value(array_val);
        free_value(start_val);
        free_value(end_val);
        return make_error(state->main_context->current_env, "array_slice start and end must be integers", 0, 0);
    }

    ArrayValue* array = array_val.as.array;
    int64_t start = start_val.as.integer.value.i64;
    int64_t end = end_val.as.integer.value.i64;

    // Handle negative indices
    if (start < 0) start = 0;
    if (end > (int64_t)array->length) end = (int64_t)array->length;
    if (start >= end) {
        // Return empty array
        ArrayValue* empty_array = array_create(8);
        free_value(array_val);
        free_value(start_val);
        free_value(end_val);
        ctx_push(state->main_context, make_array_value(empty_array));
        return make_success(1);
    }

    size_t slice_length = (size_t)(end - start);
    ArrayValue* slice_array = array_create(slice_length);
    if (!slice_array) {
        free_value(array_val);
        free_value(start_val);
        free_value(end_val);
        return make_error(state->main_context->current_env, "Failed to create slice array", 0, 0);
    }

    for (size_t i = 0; i < slice_length; i++) {
        slice_array->elements[i] = copy_value(array->elements[start + i]);
        slice_array->length++;
    }

    free_value(array_val);
    free_value(start_val);
    free_value(end_val);
    ctx_push(state->main_context, make_array_value(slice_array));
    return make_success(1);
}

EvalResult lib_array_concat(MobiusState* state, int arg_count) {
    if (arg_count < 2) {
        return make_error(state->main_context->current_env, "array_concat expects at least 2 arguments", 0, 0);
    }

    // Calculate total length
    size_t total_length = 0;
    for (int i = 0; i < arg_count; i++) {
        Value arg = ctx_peek(state->main_context, i);
        if (arg.type != VAL_ARRAY) {
            // Clean up arguments before returning error
            for (int j = 0; j < arg_count; j++) {
                free_value(ctx_pop(state->main_context));
            }
            return make_error(state->main_context->current_env, "array_concat expects all arguments to be arrays", 0, 0);
        }
        total_length += arg.as.array->length;
    }

    ArrayValue* result_array = array_create(total_length);
    if (!result_array) {
        // Clean up arguments before returning error
        for (int i = 0; i < arg_count; i++) {
            free_value(ctx_pop(state->main_context));
        }
        return make_error(state->main_context->current_env, "Failed to create result array", 0, 0);
    }

    // Copy elements from all arrays (in reverse order due to stack)
    for (int i = arg_count - 1; i >= 0; i--) {
        Value arg = ctx_peek(state->main_context, i);
        ArrayValue* array = arg.as.array;
        for (size_t j = 0; j < array->length; j++) {
            result_array->elements[result_array->length] = copy_value(array->elements[j]);
            result_array->length++;
        }
    }

    // Remove and free arguments
    for (int i = 0; i < arg_count; i++) {
        free_value(ctx_pop(state->main_context));
    }

    ctx_push(state->main_context, make_array_value(result_array));
    return make_success(1);
}

EvalResult lib_array_reverse(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "array_reverse expects exactly 1 argument", 0, 0);
    }

    Value array_val = ctx_peek(state->main_context, 0);
    ctx_pop(state->main_context); // Remove argument

    if (array_val.type != VAL_ARRAY) {
        free_value(array_val);
        return make_error(state->main_context->current_env, "array_reverse argument must be an array", 0, 0);
    }

    ArrayValue* array = array_val.as.array;
    
    // Reverse in place
    for (size_t i = 0; i < array->length / 2; i++) {
        size_t j = array->length - 1 - i;
        Value temp = array->elements[i];
        array->elements[i] = array->elements[j];
        array->elements[j] = temp;
    }

    ctx_push(state->main_context, array_val); // Return the same array (don't free since we're returning it)
    return make_success(1);
}

EvalResult lib_array_find(MobiusState* state, int arg_count) {
    if (arg_count != 2) {
        return make_error(state->main_context->current_env, "array_find expects exactly 2 arguments (array, value)", 0, 0);
    }

    Value search_val = ctx_peek(state->main_context, 0);
    Value array_val = ctx_peek(state->main_context, 1);
    
    // Remove arguments
    ctx_pop(state->main_context);
    ctx_pop(state->main_context);

    if (array_val.type != VAL_ARRAY) {
        free_value(array_val);
        free_value(search_val);
        return make_error(state->main_context->current_env, "array_find first argument must be an array", 0, 0);
    }

    ArrayValue* array = array_val.as.array;
    
    // Search for the value
    for (size_t i = 0; i < array->length; i++) {
        if (values_equal(array->elements[i], search_val)) {
            free_value(array_val);
            free_value(search_val);
            ctx_push(state->main_context, make_integer_value(NUM_INT64, (int64_t)i));
            return make_success(1);
        }
    }

    // Not found
    free_value(array_val);
    free_value(search_val);
    ctx_push(state->main_context, make_integer_value(NUM_INT64, -1));
    return make_success(1);
}