#include "library/array.h"
#include "data/array.h"
#include "data/value.h"
#include <mobius/mobius_plugin.h>

#include <stdio.h>
#include <stdlib.h>
#include <algorithm>

// =============================================================================
// UNIFIED ARRAY FUNCTION IMPLEMENTATIONS
// =============================================================================

int lib_array_create(MobiusState* state, int arg_count) {
    if (arg_count > 1) {
        return state->error("array_create expects 0 or 1 arguments");
    }

    size_t capacity = 8; // Default capacity
    if (arg_count == 1) {
        Value arg = state->npeek(0);
        state->npop(); // Remove argument
        
        if (arg.type != VAL_INT64) {
            return state->error("array_create capacity must be an integer");
        }
        capacity = (size_t)arg.as.i64;
        if (capacity == 0) capacity = 8; // Minimum capacity
    }

    ArrayValue* array = new ArrayValue(capacity);
    if (!array) {
        return state->error("Failed to create array");
    }

    state->npush(make_array_value(array));
    return 1;
}

int lib_array_push(MobiusState* state, int arg_count) {
    if (arg_count != 2) {
        return state->error("array_push expects exactly 2 arguments (array, value)");
    }

    Value value = state->npeek(0);
    Value array_val = state->npeek(1);
    
    // Remove arguments
    state->npop();
    state->npop();

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

    Value array_val = state->npeek(0);
    state->npop(); // Remove argument

    if (array_val.type != VAL_ARRAY) {
        return state->error("array_pop argument must be an array");
    }

    ArrayValue* array = array_val.as.array;
    if (array->length() == 0) {
        state->npush(make_nil_value());
        return 1;
    }

    Value popped_value = array->pop();

    state->npush(popped_value);
    return 1;
}

int lib_array_get(MobiusState* state, int arg_count) {
    if (arg_count != 2) {
        return state->error("array_get expects exactly 2 arguments (array, index)");
    }

    Value index_val = state->npeek(0);
    Value array_val = state->npeek(1);
    
    // Remove arguments
    state->npop();
    state->npop();

    if (array_val.type != VAL_ARRAY) {
        return state->error("array_get first argument must be an array");
    }

    if (index_val.type != VAL_INT64) {
        return state->error("array_get index must be an integer");
    }

    ArrayValue* array = array_val.as.array;
    int64_t index = index_val.as.i64;

    if (index < 0 || index >= (int64_t)array->length()) {
        state->npush(make_nil_value());
        return 1;
    }

    state->npush(array->get((size_t)index));
    return 1;
}

int lib_array_set(MobiusState* state, int arg_count) {
    if (arg_count != 3) {
        return state->error("array_set expects exactly 3 arguments (array, index, value)");
    }

    Value value = state->npeek(0);
    Value index_val = state->npeek(1);
    Value array_val = state->npeek(2);
    
    // Remove arguments
    for (int i = 0; i < 3; i++) {
        state->npop();
    }

    if (array_val.type != VAL_ARRAY) {
        return state->error("array_set first argument must be an array");
    }

    if (index_val.type != VAL_INT64) {
        return state->error("array_set index must be an integer");
    }

    ArrayValue* array = array_val.as.array;
    int64_t index = index_val.as.i64;

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

    Value array_val = state->npeek(0);
    state->npop(); // Remove argument

    if (array_val.type != VAL_ARRAY) {
        return state->error("array_length argument must be an array");
    }

    ArrayValue* array = array_val.as.array;
    state->npush(make_int64_value((int64_t)array->length()));
    return 1;
}

int lib_array_slice(MobiusState* state, int arg_count) {
    if (arg_count != 3) {
        return state->error("array_slice expects exactly 3 arguments (array, start, end)");
    }

    Value end_val = state->npeek(0);
    Value start_val = state->npeek(1);
    Value array_val = state->npeek(2);
    
    // Remove arguments
    for (int i = 0; i < 3; i++) {
        state->npop();
    }

    if (array_val.type != VAL_ARRAY) {
        return state->error("array_slice first argument must be an array");
    }

    if (start_val.type != VAL_INT64 || end_val.type != VAL_INT64) {
        return state->error("array_slice start and end must be integers");
    }

    ArrayValue* array = array_val.as.array;
    int64_t start = start_val.as.i64;
    int64_t end = end_val.as.i64;

    // Handle negative indices
    if (start < 0) start = 0;
    if (end > (int64_t)array->length()) end = (int64_t)array->length();
    if (start >= end) {
        // Return empty array
        ArrayValue* empty_array = new ArrayValue(8);
        state->npush(make_array_value(empty_array));
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

    state->npush(make_array_value(slice_array));
    return 1;
}

int lib_array_concat(MobiusState* state, int arg_count) {
    if (arg_count < 2) {
        return state->error("array_concat expects at least 2 arguments");
    }

    // Calculate total length
    size_t total_length = 0;
    for (int i = 0; i < arg_count; i++) {
        Value arg = state->npeek(i);
        if (arg.type != VAL_ARRAY) {
            // Clean up arguments before returning error
            for (int j = 0; j < arg_count; j++) {
                state->npop();
            }
            return state->error("array_concat expects all arguments to be arrays");
        }
        total_length += arg.as.array->length();
    }

    ArrayValue* result_array = new ArrayValue(total_length);
    if (!result_array) {
        // Clean up arguments before returning error
        for (int i = 0; i < arg_count; i++) {
            state->npop();
        }
        return state->error("Failed to create result array");
    }

    // Copy elements from all arrays (in reverse order due to stack)
    for (int i = arg_count - 1; i >= 0; i--) {
        Value arg = state->npeek(i);
        ArrayValue* array = arg.as.array;
        for (size_t j = 0; j < array->length(); j++) {
            result_array->push((*array)[j]);
        }
    }

    // Remove arguments
    for (int i = 0; i < arg_count; i++) {
        state->npop();
    }

    state->npush(make_array_value(result_array));
    return 1;
}

int lib_array_reverse(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("array_reverse expects exactly 1 argument");
    }

    Value array_val = state->npeek(0);
    state->npop(); // Remove argument

    if (array_val.type != VAL_ARRAY) {
        return state->error("array_reverse argument must be an array");
    }

    ArrayValue* array = array_val.as.array;
    
    array->reverse();

    state->npush(array_val); // Return the same array (don't free since we're returning it)
    return 1;
}

int lib_array_find(MobiusState* state, int arg_count) {
    if (arg_count != 2) {
        return state->error("array_find expects exactly 2 arguments (array, value)");
    }

    Value search_val = state->npeek(0);
    Value array_val = state->npeek(1);
    
    // Remove arguments
    state->npop();
    state->npop();

    if (array_val.type != VAL_ARRAY) {
        return state->error("array_find first argument must be an array");
    }

    ArrayValue* array = array_val.as.array;
    
    // Search for the value
    bool strict = state->config().strict_mode;
    for (size_t i = 0; i < array->length(); i++) {
        if (strict ? (*array)[i].exactlyEqual(search_val) : (*array)[i] == search_val) {
            state->npush(make_int64_value((int64_t)i));
            return 1;
        }
    }

    // Not found
    state->npush(make_int64_value(-1));
    return 1;
}

static bool value_less_than(const Value& a, const Value& b) {
    if (a.type == VAL_INT64 && b.type == VAL_INT64) return a.as.i64 < b.as.i64;
    if (a.type == VAL_FLOAT64 && b.type == VAL_FLOAT64) return a.as.double_val < b.as.double_val;
    if (a.type == VAL_INT64 && b.type == VAL_FLOAT64) return (double)a.as.i64 < b.as.double_val;
    if (a.type == VAL_FLOAT64 && b.type == VAL_INT64) return a.as.double_val < (double)b.as.i64;
    if (a.type == VAL_STRING && b.type == VAL_STRING && a.as.string && b.as.string)
        return strcmp(a.as.string->data, b.as.string->data) < 0;
    return false;
}

int lib_array_sort(MobiusState* state, int arg_count) {
    if (arg_count < 1 || arg_count > 2) {
        return state->error("array_sort expects 1 or 2 arguments (array [, comparator])");
    }

    Value comp_val;
    if (arg_count == 2) {
        comp_val = state->npeek(0);
        state->npop();
    }
    Value array_val = state->npeek(0);
    state->npop();

    if (array_val.type != VAL_ARRAY || !array_val.as.array) {
        return state->error("array_sort first argument must be an array");
    }

    ArrayValue* arr = array_val.as.array;
    size_t len = arr->length();

    if (arg_count == 1) {
        std::sort(arr->data(), arr->data() + len, value_less_than);
    } else {
        if (comp_val.type != VAL_FUNCTION && comp_val.type != VAL_NATIVE_FUNCTION) {
            return state->error("array_sort comparator must be a function");
        }
        bool sort_error = false;
        std::sort(arr->data(), arr->data() + len, [&](const Value& a, const Value& b) -> bool {
            if (sort_error) return false;
            mobius_stack_pushNil(state);
            state->npeek(0) = comp_val;
            state->npush(a);
            state->npush(b);
            int rc = mobius_pcall(state, 2, 1);
            if (rc < 0) { sort_error = true; return false; }
            Value result = state->npop();
            return is_truthy(result);
        });
        if (sort_error) return -1;
    }

    state->npush(array_val);
    return 1;
}

int lib_array_map(MobiusState* state, int arg_count) {
    if (arg_count != 2) {
        return state->error("array_map expects 2 arguments (array, function)");
    }

    Value func_val = state->npeek(0);
    Value array_val = state->npeek(1);
    state->npop();
    state->npop();

    if (array_val.type != VAL_ARRAY || !array_val.as.array) {
        return state->error("array_map first argument must be an array");
    }
    if (func_val.type != VAL_FUNCTION && func_val.type != VAL_NATIVE_FUNCTION) {
        return state->error("array_map second argument must be a function");
    }

    ArrayValue* src = array_val.as.array;
    size_t len = src->length();
    ArrayValue* result = new ArrayValue(len);

    for (size_t i = 0; i < len; i++) {
        mobius_stack_pushNil(state);
        state->npeek(0) = func_val;
        state->npush((*src)[i]);
        state->npush(make_int64_value((int64_t)i));
        int rc = mobius_pcall(state, 2, 1);
        if (rc < 0) { delete result; return -1; }
        result->push(state->npop());
    }

    state->npush(make_array_value(result));
    return 1;
}

int lib_array_filter(MobiusState* state, int arg_count) {
    if (arg_count != 2) {
        return state->error("array_filter expects 2 arguments (array, function)");
    }

    Value func_val = state->npeek(0);
    Value array_val = state->npeek(1);
    state->npop();
    state->npop();

    if (array_val.type != VAL_ARRAY || !array_val.as.array) {
        return state->error("array_filter first argument must be an array");
    }
    if (func_val.type != VAL_FUNCTION && func_val.type != VAL_NATIVE_FUNCTION) {
        return state->error("array_filter second argument must be a function");
    }

    ArrayValue* src = array_val.as.array;
    size_t len = src->length();
    ArrayValue* result = new ArrayValue(len);

    for (size_t i = 0; i < len; i++) {
        mobius_stack_pushNil(state);
        state->npeek(0) = func_val;
        state->npush((*src)[i]);
        state->npush(make_int64_value((int64_t)i));
        int rc = mobius_pcall(state, 2, 1);
        if (rc < 0) { delete result; return -1; }
        Value pred = state->npop();
        if (is_truthy(pred)) {
            result->push((*src)[i]);
        }
    }

    state->npush(make_array_value(result));
    return 1;
}

int lib_array_reduce(MobiusState* state, int arg_count) {
    if (arg_count != 3) {
        return state->error("array_reduce expects 3 arguments (array, function, initial)");
    }

    Value initial = state->npeek(0);
    Value func_val = state->npeek(1);
    Value array_val = state->npeek(2);
    state->npop();
    state->npop();
    state->npop();

    if (array_val.type != VAL_ARRAY || !array_val.as.array) {
        return state->error("array_reduce first argument must be an array");
    }
    if (func_val.type != VAL_FUNCTION && func_val.type != VAL_NATIVE_FUNCTION) {
        return state->error("array_reduce second argument must be a function");
    }

    ArrayValue* src = array_val.as.array;
    size_t len = src->length();
    Value accumulator = initial;

    for (size_t i = 0; i < len; i++) {
        mobius_stack_pushNil(state);
        state->npeek(0) = func_val;
        state->npush(accumulator);
        state->npush((*src)[i]);
        int rc = mobius_pcall(state, 2, 1);
        if (rc < 0) return -1;
        accumulator = state->npop();
    }

    state->npush(accumulator);
    return 1;
}

int lib_array_foreach(MobiusState* state, int arg_count) {
    if (arg_count != 2) {
        return state->error("array_foreach expects 2 arguments (array, function)");
    }

    Value func_val = state->npeek(0);
    Value array_val = state->npeek(1);
    state->npop();
    state->npop();

    if (array_val.type != VAL_ARRAY || !array_val.as.array) {
        return state->error("array_foreach first argument must be an array");
    }
    if (func_val.type != VAL_FUNCTION && func_val.type != VAL_NATIVE_FUNCTION) {
        return state->error("array_foreach second argument must be a function");
    }

    ArrayValue* src = array_val.as.array;
    size_t len = src->length();

    for (size_t i = 0; i < len; i++) {
        mobius_stack_pushNil(state);
        state->npeek(0) = func_val;
        state->npush((*src)[i]);
        state->npush(make_int64_value((int64_t)i));
        int rc = mobius_pcall(state, 2, 1);
        if (rc < 0) return -1;
        state->npop();
    }

    return 0;
}

int lib_array_any(MobiusState* state, int arg_count) {
    if (arg_count != 2) {
        return state->error("array_any expects 2 arguments (array, function)");
    }

    Value func_val = state->npeek(0);
    Value array_val = state->npeek(1);
    state->npop();
    state->npop();

    if (array_val.type != VAL_ARRAY || !array_val.as.array) {
        return state->error("array_any first argument must be an array");
    }
    if (func_val.type != VAL_FUNCTION && func_val.type != VAL_NATIVE_FUNCTION) {
        return state->error("array_any second argument must be a function");
    }

    ArrayValue* src = array_val.as.array;
    size_t len = src->length();

    for (size_t i = 0; i < len; i++) {
        mobius_stack_pushNil(state);
        state->npeek(0) = func_val;
        state->npush((*src)[i]);
        int rc = mobius_pcall(state, 1, 1);
        if (rc < 0) return -1;
        Value result = state->npop();
        if (is_truthy(result)) {
            state->npush(make_bool_value(true));
            return 1;
        }
    }

    state->npush(make_bool_value(false));
    return 1;
}

int lib_array_all(MobiusState* state, int arg_count) {
    if (arg_count != 2) {
        return state->error("array_all expects 2 arguments (array, function)");
    }

    Value func_val = state->npeek(0);
    Value array_val = state->npeek(1);
    state->npop();
    state->npop();

    if (array_val.type != VAL_ARRAY || !array_val.as.array) {
        return state->error("array_all first argument must be an array");
    }
    if (func_val.type != VAL_FUNCTION && func_val.type != VAL_NATIVE_FUNCTION) {
        return state->error("array_all second argument must be a function");
    }

    ArrayValue* src = array_val.as.array;
    size_t len = src->length();

    for (size_t i = 0; i < len; i++) {
        mobius_stack_pushNil(state);
        state->npeek(0) = func_val;
        state->npush((*src)[i]);
        int rc = mobius_pcall(state, 1, 1);
        if (rc < 0) return -1;
        Value result = state->npop();
        if (!is_truthy(result)) {
            state->npush(make_bool_value(false));
            return 1;
        }
    }

    state->npush(make_bool_value(true));
    return 1;
}