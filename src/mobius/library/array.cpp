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

int lib_array_create(MobiusState* state, int arg_count) {
    if (arg_count > 1) {
        return state->error("array_create expects 0 or 1 arguments");
    }

    size_t capacity = 8; // Default capacity
    if (arg_count == 1) {
        Value arg = state->mainContext()->peek( 0);
        state->mainContext()->pop(); // Remove argument
        
        if (arg.type != VAL_INTEGER) {
            return state->error("array_create capacity must be an integer");
        }
        capacity = (size_t)arg.as.integer.value;
        if (capacity == 0) capacity = 8; // Minimum capacity
    }

    ArrayValue* array = new ArrayValue(capacity);
    if (!array) {
        return state->error("Failed to create array");
    }

    state->mainContext()->push( make_array_value(array));
    return 1;
}

int lib_array_push(MobiusState* state, int arg_count) {
    if (arg_count != 2) {
        return state->error("array_push expects exactly 2 arguments (array, value)");
    }

    Value value = state->mainContext()->peek( 0);
    Value array_val = state->mainContext()->peek( 1);
    
    // Remove arguments
    state->mainContext()->pop();
    state->mainContext()->pop();

    if (array_val.type != VAL_ARRAY) {
        return state->error("array_push first argument must be an array");
    }

    ArrayValue* array = array_val.as.array;
    array->push(value);

    return 0;
}

int lib_array_pop(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("array_pop expects exactly 1 argument");
    }

    Value array_val = state->mainContext()->peek( 0);
    state->mainContext()->pop(); // Remove argument

    if (array_val.type != VAL_ARRAY) {
        return state->error("array_pop argument must be an array");
    }

    ArrayValue* array = array_val.as.array;
    if (array->length() == 0) {
        state->mainContext()->push( make_nil_value());
        return 1;
    }

    Value popped_value = array->pop();

    state->mainContext()->push( popped_value);
    return 1;
}

int lib_array_get(MobiusState* state, int arg_count) {
    if (arg_count != 2) {
        return state->error("array_get expects exactly 2 arguments (array, index)");
    }

    Value index_val = state->mainContext()->peek( 0);
    Value array_val = state->mainContext()->peek( 1);
    
    // Remove arguments
    state->mainContext()->pop();
    state->mainContext()->pop();

    if (array_val.type != VAL_ARRAY) {
        return state->error("array_get first argument must be an array");
    }

    if (index_val.type != VAL_INTEGER) {
        return state->error("array_get index must be an integer");
    }

    ArrayValue* array = array_val.as.array;
    int64_t index = index_val.as.integer.value;

    if (index < 0 || index >= (int64_t)array->length()) {
        state->mainContext()->push( make_nil_value());
        return 1;
    }

    state->mainContext()->push( array->get((size_t)index));
    return 1;
}

int lib_array_set(MobiusState* state, int arg_count) {
    if (arg_count != 3) {
        return state->error("array_set expects exactly 3 arguments (array, index, value)");
    }

    Value value = state->mainContext()->peek( 0);
    Value index_val = state->mainContext()->peek( 1);
    Value array_val = state->mainContext()->peek( 2);
    
    // Remove arguments
    for (int i = 0; i < 3; i++) {
        state->mainContext()->pop();
    }

    if (array_val.type != VAL_ARRAY) {
        return state->error("array_set first argument must be an array");
    }

    if (index_val.type != VAL_INTEGER) {
        return state->error("array_set index must be an integer");
    }

    ArrayValue* array = array_val.as.array;
    int64_t index = index_val.as.integer.value;

    if (index < 0 || index >= (int64_t)array->length()) {
        return state->error("array_set index out of bounds");
    }

    array->set((size_t)index, value);

    return 0;
}

int lib_array_length(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("array_length expects exactly 1 argument");
    }

    Value array_val = state->mainContext()->peek( 0);
    state->mainContext()->pop(); // Remove argument

    if (array_val.type != VAL_ARRAY) {
        return state->error("array_length argument must be an array");
    }

    ArrayValue* array = array_val.as.array;
    state->mainContext()->push( make_integer_value(NUM_INT64, (int64_t)array->length()));
    return 1;
}

int lib_array_slice(MobiusState* state, int arg_count) {
    if (arg_count != 3) {
        return state->error("array_slice expects exactly 3 arguments (array, start, end)");
    }

    Value end_val = state->mainContext()->peek( 0);
    Value start_val = state->mainContext()->peek( 1);
    Value array_val = state->mainContext()->peek( 2);
    
    // Remove arguments
    for (int i = 0; i < 3; i++) {
        state->mainContext()->pop();
    }

    if (array_val.type != VAL_ARRAY) {
        return state->error("array_slice first argument must be an array");
    }

    if (start_val.type != VAL_INTEGER || end_val.type != VAL_INTEGER) {
        return state->error("array_slice start and end must be integers");
    }

    ArrayValue* array = array_val.as.array;
    int64_t start = start_val.as.integer.value;
    int64_t end = end_val.as.integer.value;

    // Handle negative indices
    if (start < 0) start = 0;
    if (end > (int64_t)array->length()) end = (int64_t)array->length();
    if (start >= end) {
        // Return empty array
        ArrayValue* empty_array = new ArrayValue(8);
        state->mainContext()->push( make_array_value(empty_array));
        return 1;
    }

    size_t slice_length = (size_t)(end - start);
    ArrayValue* slice_array = new ArrayValue(slice_length);
    if (!slice_array) {
        return state->error("Failed to create slice array");
    }

    for (size_t i = 0; i < slice_length; i++) {
        slice_array->push((*array)[start + i]);
    }

    state->mainContext()->push( make_array_value(slice_array));
    return 1;
}

int lib_array_concat(MobiusState* state, int arg_count) {
    if (arg_count < 2) {
        return state->error("array_concat expects at least 2 arguments");
    }

    // Calculate total length
    size_t total_length = 0;
    for (int i = 0; i < arg_count; i++) {
        Value arg = state->mainContext()->peek( i);
        if (arg.type != VAL_ARRAY) {
            // Clean up arguments before returning error
            for (int j = 0; j < arg_count; j++) {
                state->mainContext()->pop();
            }
            return state->error("array_concat expects all arguments to be arrays");
        }
        total_length += arg.as.array->length();
    }

    ArrayValue* result_array = new ArrayValue(total_length);
    if (!result_array) {
        // Clean up arguments before returning error
        for (int i = 0; i < arg_count; i++) {
            state->mainContext()->pop();
        }
        return state->error("Failed to create result array");
    }

    // Copy elements from all arrays (in reverse order due to stack)
    for (int i = arg_count - 1; i >= 0; i--) {
        Value arg = state->mainContext()->peek( i);
        ArrayValue* array = arg.as.array;
        for (size_t j = 0; j < array->length(); j++) {
            result_array->push((*array)[j]);
        }
    }

    // Remove arguments
    for (int i = 0; i < arg_count; i++) {
        state->mainContext()->pop();
    }

    state->mainContext()->push( make_array_value(result_array));
    return 1;
}

int lib_array_reverse(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("array_reverse expects exactly 1 argument");
    }

    Value array_val = state->mainContext()->peek( 0);
    state->mainContext()->pop(); // Remove argument

    if (array_val.type != VAL_ARRAY) {
        return state->error("array_reverse argument must be an array");
    }

    ArrayValue* array = array_val.as.array;
    
    array->reverse();

    state->mainContext()->push( array_val); // Return the same array (don't free since we're returning it)
    return 1;
}

int lib_array_find(MobiusState* state, int arg_count) {
    if (arg_count != 2) {
        return state->error("array_find expects exactly 2 arguments (array, value)");
    }

    Value search_val = state->mainContext()->peek( 0);
    Value array_val = state->mainContext()->peek( 1);
    
    // Remove arguments
    state->mainContext()->pop();
    state->mainContext()->pop();

    if (array_val.type != VAL_ARRAY) {
        return state->error("array_find first argument must be an array");
    }

    ArrayValue* array = array_val.as.array;
    
    // Search for the value
    bool strict = state->config().strict_mode;
    for (size_t i = 0; i < array->length(); i++) {
        if (strict ? (*array)[i].exactlyEqual(search_val) : (*array)[i] == search_val) {
            state->mainContext()->push( make_integer_value(NUM_INT64, (int64_t)i));
            return 1;
        }
    }

    // Not found
    state->mainContext()->push( make_integer_value(NUM_INT64, -1));
    return 1;
}