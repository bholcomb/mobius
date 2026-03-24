#include "library/string.h"
#include "data/value.h"
#include "data/array.h"
#include "state/environment.h"
#include "state/mobius_state.h"
#include "eval/evaluator.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// =============================================================================
// UNIFIED STRING FUNCTION IMPLEMENTATIONS
// =============================================================================

int lib_len(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("len expects exactly 1 argument");
    }
    
    Value arg = state->mainContext()->peek( 0);
    state->mainContext()->pop(); // Remove argument
    
    if (arg.type == VAL_STRING && arg.as.string) {
        state->mainContext()->push( make_integer_value(NUM_INT64, (int64_t)arg.as.string->length));
    } else if (arg.type == VAL_ARRAY && arg.as.array) {
        state->mainContext()->push( make_integer_value(NUM_INT64, (int64_t)arg.as.array->length()));
    } else {
        return state->error("len expects a string or array argument");
    }
    
    return 1;
}
    
int lib_upper(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("upper expects exactly 1 argument");
    }
    
    Value arg = state->mainContext()->peek( 0);
    state->mainContext()->pop(); // Remove argument
    
    if (arg.type != VAL_STRING || !arg.as.string) {
        return state->error("upper expects a string argument");
    }
    
    const char* input = arg.as.string->data;
    size_t len = arg.as.string->length;
    char* upper_str = malloc(len + 1);
    if (!upper_str) {
        return state->error("Memory allocation failed");
    }
    
    for (size_t i = 0; i < len; i++) {
        upper_str[i] = toupper(input[i]);
    }
    upper_str[len] = '\0';
    
    MobiusString* result_string = state->stringPool()->intern(upper_str);
    free(upper_str);
    
    if (!result_string) {
        return state->error("String creation failed");
    }
    
    state->mainContext()->push( make_string_value(result_string));
    return 1;
}

int lib_lower(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("lower expects exactly 1 argument");
    }
    
    Value arg = state->mainContext()->peek( 0);
    state->mainContext()->pop(); // Remove argument
    
    if (arg.type != VAL_STRING || !arg.as.string) {
        return state->error("lower expects a string argument");
    }
    
    const char* input = arg.as.string->data;
    size_t len = arg.as.string->length;
    char* lower_str = malloc(len + 1);
    if (!lower_str) {
        return state->error("Memory allocation failed");
    }
    
    for (size_t i = 0; i < len; i++) {
        lower_str[i] = tolower(input[i]);
    }
    lower_str[len] = '\0';
    
    MobiusString* result_string = state->stringPool()->intern(lower_str);
    free(lower_str);
    
    if (!result_string) {
        return state->error("String creation failed");
    }
    
    state->mainContext()->push( make_string_value(result_string));
    return 1;
}

int lib_substr(MobiusState* state, int arg_count) {
    if (arg_count != 3) {
        return state->error("substr expects exactly 3 arguments (string, start, length)");
    }
    
    Value length_val = state->mainContext()->peek( 0);
    Value start_val = state->mainContext()->peek( 1);
    Value string_val = state->mainContext()->peek( 2);
    
    // Remove arguments
    for (int i = 0; i < 3; i++) {
        state->mainContext()->pop();
    }
    
    if (string_val.type != VAL_STRING || !string_val.as.string) {
        return state->error("substr expects first argument to be a string");
    }
    
    if (start_val.type != VAL_INTEGER || length_val.type != VAL_INTEGER) {
        return state->error("substr expects start and length to be integers");
    }
    
    const char* input = string_val.as.string->data;
    size_t input_len = string_val.as.string->length;
    int64_t start = start_val.as.integer.value;
    int64_t length = length_val.as.integer.value;
    
    // Handle negative start (from end)
    if (start < 0) {
        start = (int64_t)input_len + start;
    }
    
    if (start < 0 || start >= (int64_t)input_len || length < 0) {
        state->mainContext()->push( make_string_value_from_cstr(state, ""));
        return 1;
    }
    
    size_t actual_length = (size_t)length;
    if (start + length > (int64_t)input_len) {
        actual_length = input_len - (size_t)start;
    }
    
    char* substr_data = malloc(actual_length + 1);
    if (!substr_data) {
        return state->error("Memory allocation failed");
    }
    
    strncpy(substr_data, input + start, actual_length);
    substr_data[actual_length] = '\0';
    
    MobiusString* result_string = state->stringPool()->intern(substr_data);
    free(substr_data);
    
    if (!result_string) {
        return state->error("String creation failed");
    }
    
    state->mainContext()->push( make_string_value(result_string));
    return 1;
}

int lib_concat(MobiusState* state, int arg_count) {
    if (arg_count < 2) {
        return state->error("concat expects at least 2 arguments");
    }
    
    // Calculate total length
    size_t total_length = 0;
    for (int i = 0; i < arg_count; i++) {
        Value arg = state->mainContext()->peek( i);
        if (arg.type == VAL_STRING && arg.as.string) {
            total_length += arg.as.string->length;
        } else {
            return state->error("concat expects all arguments to be strings");
        }
    }
    
    char* result_data = malloc(total_length + 1);
    if (!result_data) {
        return state->error("Memory allocation failed");
    }
    
    size_t offset = 0;
    for (int i = arg_count - 1; i >= 0; i--) {
        Value arg = state->mainContext()->peek( i);
        const char* str_data = arg.as.string->data;
        size_t str_len = arg.as.string->length;
        memcpy(result_data + offset, str_data, str_len);
        offset += str_len;
    }
    result_data[total_length] = '\0';
    
    // Remove arguments
    for (int i = 0; i < arg_count; i++) {
        state->mainContext()->pop();
    }
    
    MobiusString* result_string = state->stringPool()->intern(result_data);
    free(result_data);
    
    if (!result_string) {
        return state->error("String creation failed");
    }
    
    state->mainContext()->push( make_string_value(result_string));
    return 1;
}

int lib_contains(MobiusState* state, int arg_count) {
    if (arg_count != 2) {
        return state->error("contains expects exactly 2 arguments (haystack, needle)");
    }
    
    Value needle_val = state->mainContext()->peek( 0);
    Value haystack_val = state->mainContext()->peek( 1);
    
    // Remove arguments
    state->mainContext()->pop();
    state->mainContext()->pop();
    
    if (haystack_val.type != VAL_STRING || !haystack_val.as.string ||
        needle_val.type != VAL_STRING || !needle_val.as.string) {
        return state->error("contains expects both arguments to be strings");
    }
    
    const char* haystack = haystack_val.as.string->data;
    const char* needle = needle_val.as.string->data;
    
    bool found = strstr(haystack, needle) != NULL;
    state->mainContext()->push(make_bool_value(found));
    
    return 1;
}