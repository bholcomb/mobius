#include "library/string.h"
#include "data/value.h"
#include "state/environment.h"
#include "eval/evaluator.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// =============================================================================
// UNIFIED STRING FUNCTION IMPLEMENTATIONS
// =============================================================================

EvalResult lib_len(ExecutionContext* ctx, int arg_count) {
    if (arg_count != 1) {
        return make_error("len expects exactly 1 argument", 0, 0);
    }
    
    Value arg = ctx_peek(ctx, 0);
    ctx_pop(ctx); // Remove argument
    
    if (arg.type == VAL_STRING && arg.as.string) {
        ctx_push(ctx, make_integer_value(NUM_INT64, (int64_t)string_length(arg.as.string)));
    } else if (arg.type == VAL_ARRAY && arg.as.array) {
        ctx_push(ctx, make_integer_value(NUM_INT64, (int64_t)arg.as.array->length));
    } else {
        return make_error("len expects a string or array argument", 0, 0);
    }
    
    return make_success(1);
}

EvalResult lib_upper(ExecutionContext* ctx, int arg_count) {
    if (arg_count != 1) {
        return make_error("upper expects exactly 1 argument", 0, 0);
    }
    
    Value arg = ctx_peek(ctx, 0);
    ctx_pop(ctx); // Remove argument
    
    if (arg.type != VAL_STRING || !arg.as.string) {
        return make_error("upper expects a string argument", 0, 0);
    }
    
    const char* input = string_data(arg.as.string);
    size_t len = string_length(arg.as.string);
    char* upper_str = malloc(len + 1);
    if (!upper_str) {
        return make_error("Memory allocation failed", 0, 0);
    }
    
    for (size_t i = 0; i < len; i++) {
        upper_str[i] = toupper(input[i]);
    }
    upper_str[len] = '\0';
    
    RefCountedString* result_string = string_create(upper_str);
    free(upper_str);
    
    if (!result_string) {
        return make_error("String creation failed", 0, 0);
    }
    
    ctx_push(ctx, make_string_value(result_string));
    return make_success(1);
}

EvalResult lib_lower(ExecutionContext* ctx, int arg_count) {
    if (arg_count != 1) {
        return make_error("lower expects exactly 1 argument", 0, 0);
    }
    
    Value arg = ctx_peek(ctx, 0);
    ctx_pop(ctx); // Remove argument
    
    if (arg.type != VAL_STRING || !arg.as.string) {
        return make_error("lower expects a string argument", 0, 0);
    }
    
    const char* input = string_data(arg.as.string);
    size_t len = string_length(arg.as.string);
    char* lower_str = malloc(len + 1);
    if (!lower_str) {
        return make_error("Memory allocation failed", 0, 0);
    }
    
    for (size_t i = 0; i < len; i++) {
        lower_str[i] = tolower(input[i]);
    }
    lower_str[len] = '\0';
    
    RefCountedString* result_string = string_create(lower_str);
    free(lower_str);
    
    if (!result_string) {
        return make_error("String creation failed", 0, 0);
    }
    
    ctx_push(ctx, make_string_value(result_string));
    return make_success(1);
}

EvalResult lib_substr(ExecutionContext* ctx, int arg_count) {
    if (arg_count != 3) {
        return make_error("substr expects exactly 3 arguments (string, start, length)", 0, 0);
    }
    
    Value length_val = ctx_peek(ctx, 0);
    Value start_val = ctx_peek(ctx, 1);
    Value string_val = ctx_peek(ctx, 2);
    
    // Remove arguments
    for (int i = 0; i < 3; i++) {
        ctx_pop(ctx);
    }
    
    if (string_val.type != VAL_STRING || !string_val.as.string) {
        return make_error("substr expects first argument to be a string", 0, 0);
    }
    
    if (start_val.type != VAL_INTEGER || length_val.type != VAL_INTEGER) {
        return make_error("substr expects start and length to be integers", 0, 0);
    }
    
    const char* input = string_data(string_val.as.string);
    size_t input_len = string_length(string_val.as.string);
    int64_t start = start_val.as.integer.value.i64;
    int64_t length = length_val.as.integer.value.i64;
    
    // Handle negative start (from end)
    if (start < 0) {
        start = (int64_t)input_len + start;
    }
    
    if (start < 0 || start >= (int64_t)input_len || length < 0) {
        ctx_push(ctx, make_string_value_from_cstr(""));
        return make_success(1);
    }
    
    size_t actual_length = (size_t)length;
    if (start + length > (int64_t)input_len) {
        actual_length = input_len - (size_t)start;
    }
    
    char* substr_data = malloc(actual_length + 1);
    if (!substr_data) {
        return make_error("Memory allocation failed", 0, 0);
    }
    
    strncpy(substr_data, input + start, actual_length);
    substr_data[actual_length] = '\0';
    
    RefCountedString* result_string = string_create(substr_data);
    free(substr_data);
    
    if (!result_string) {
        return make_error("String creation failed", 0, 0);
    }
    
    ctx_push(ctx, make_string_value(result_string));
    return make_success(1);
}

EvalResult lib_concat(ExecutionContext* ctx, int arg_count) {
    if (arg_count < 2) {
        return make_error("concat expects at least 2 arguments", 0, 0);
    }
    
    // Calculate total length
    size_t total_length = 0;
    for (int i = 0; i < arg_count; i++) {
        Value arg = ctx_peek(ctx, i);
        if (arg.type == VAL_STRING && arg.as.string) {
            total_length += string_length(arg.as.string);
        } else {
            return make_error("concat expects all arguments to be strings", 0, 0);
        }
    }
    
    char* result_data = malloc(total_length + 1);
    if (!result_data) {
        return make_error("Memory allocation failed", 0, 0);
    }
    
    size_t offset = 0;
    for (int i = arg_count - 1; i >= 0; i--) {
        Value arg = ctx_peek(ctx, i);
        const char* str_data = string_data(arg.as.string);
        size_t str_len = string_length(arg.as.string);
        memcpy(result_data + offset, str_data, str_len);
        offset += str_len;
    }
    result_data[total_length] = '\0';
    
    // Remove arguments
    for (int i = 0; i < arg_count; i++) {
        ctx_pop(ctx);
    }
    
    RefCountedString* result_string = string_create(result_data);
    free(result_data);
    
    if (!result_string) {
        return make_error("String creation failed", 0, 0);
    }
    
    ctx_push(ctx, make_string_value(result_string));
    return make_success(1);
}

EvalResult lib_contains(ExecutionContext* ctx, int arg_count) {
    if (arg_count != 2) {
        return make_error("contains expects exactly 2 arguments (haystack, needle)", 0, 0);
    }
    
    Value needle_val = ctx_peek(ctx, 0);
    Value haystack_val = ctx_peek(ctx, 1);
    
    // Remove arguments
    ctx_pop(ctx);
    ctx_pop(ctx);
    
    if (haystack_val.type != VAL_STRING || !haystack_val.as.string ||
        needle_val.type != VAL_STRING || !needle_val.as.string) {
        return make_error("contains expects both arguments to be strings", 0, 0);
    }
    
    const char* haystack = string_data(haystack_val.as.string);
    const char* needle = string_data(needle_val.as.string);
    
    bool found = strstr(haystack, needle) != NULL;
    ctx_push(ctx, make_bool_value(found));
    
    return make_success(1);
}