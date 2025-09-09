#include "bytecode_string.h"
#include "bytecode.h"
#include "value.h"
#include <stdio.h>
#include <stdlib.h>

// =============================================================================
// STRING BUILTIN FUNCTIONS FOR BYTECODE VM
// =============================================================================

// String functions
void builtin_len_vm(MobiusVM* vm, int arg_count, void* result_ptr) {
    Value* result = (Value*)result_ptr;
    if (arg_count != 1) {
        vm_runtime_error(vm, "len expects 1 argument");
        *result = make_nil_value();
        return;
    }

    Value arg = vm_peek(vm, 0);
    if (arg.type == VAL_STRING && arg.as.string) {
        *result = make_integer_value(NUM_INT64, (int64_t)string_length(arg.as.string));
    } else if (arg.type == VAL_ARRAY && arg.as.array) {
        *result = make_integer_value(NUM_INT64, (int64_t)arg.as.array->length);
    } else {
        vm_runtime_error(vm, "len expects a string or array argument");
        *result = make_nil_value();
    }
}

void builtin_upper_vm(MobiusVM* vm, int arg_count, void* result_ptr) {
    Value* result = (Value*)result_ptr;
    if (arg_count != 1) {
        vm_runtime_error(vm, "upper expects 1 argument");
        *result = make_nil_value();
        return;
    }

    Value arg = vm_peek(vm, 0);
    if (arg.type != VAL_STRING || !arg.as.string) {
        vm_runtime_error(vm, "upper expects a string argument");
        *result = make_nil_value();
        return;
    }

    const char* input = string_data(arg.as.string);
    size_t len = string_length(arg.as.string);
    char* upper_str = malloc(len + 1);
    if (!upper_str) {
        vm_runtime_error(vm, "Memory allocation failed");
        *result = make_nil_value();
        return;
    }

    for (size_t i = 0; i < len; i++) {
        upper_str[i] = toupper(input[i]);
    }
    upper_str[len] = '\0';

    *result = make_string_value_from_cstr(upper_str);
    free(upper_str);
}

void builtin_lower_vm(MobiusVM* vm, int arg_count, void* result_ptr) {
    Value* result = (Value*)result_ptr;
    if (arg_count != 1) {
        vm_runtime_error(vm, "lower expects 1 argument");
        *result = make_nil_value();
        return;
    }

    Value arg = vm_peek(vm, 0);
    if (arg.type != VAL_STRING || !arg.as.string) {
        vm_runtime_error(vm, "lower expects a string argument");
        *result = make_nil_value();
        return;
    }

    const char* input = string_data(arg.as.string);
    size_t len = string_length(arg.as.string);
    char* lower_str = malloc(len + 1);
    if (!lower_str) {
        vm_runtime_error(vm, "Memory allocation failed");
        *result = make_nil_value();
        return;
    }

    for (size_t i = 0; i < len; i++) {
        lower_str[i] = tolower(input[i]);
    }
    lower_str[len] = '\0';

    *result = make_string_value_from_cstr(lower_str);
    free(lower_str);
}

void builtin_substr_vm(MobiusVM* vm, int arg_count, void* result_ptr) {
    Value* result = (Value*)result_ptr;
    if (arg_count < 2 || arg_count > 3) {
        vm_runtime_error(vm, "substr expects 2 or 3 arguments: substr(string, start [, length])");
        *result = make_nil_value();
        return;
    }
    
    Value str_val = vm_peek(vm, arg_count - 1);  // First argument
    Value start_val = vm_peek(vm, arg_count - 2); // Second argument
    
    if (str_val.type != VAL_STRING) {
        vm_runtime_error(vm, "substr() first argument must be a string");
        *result = make_nil_value();
        return;
    }
    if (start_val.type != VAL_INTEGER) {
        vm_runtime_error(vm, "substr() start index must be an integer");
        *result = make_nil_value();
        return;
    }
    
    const char* str = str_val.as.string ? string_data(str_val.as.string) : "";
    size_t str_len = str_val.as.string ? string_length(str_val.as.string) : 0;
    int32_t start = start_val.as.integer.value.i32;
    size_t length = str_len;
    
    // Handle negative start index
    if (start < 0) start = 0;
    if ((size_t)start >= str_len) {
        *result = make_string_value_from_cstr("");
        return;
    }
    
    // Handle length parameter
    if (arg_count == 3) {
        Value len_val = vm_peek(vm, 0); // Third argument
        if (len_val.type != VAL_INTEGER) {
            vm_runtime_error(vm, "substr() length must be an integer");
            *result = make_nil_value();
            return;
        }
        int32_t len = len_val.as.integer.value.i32;
        if (len < 0) len = 0;
        length = (size_t)len;
    }
    
    // Calculate actual substring length
    if ((size_t)start + length > str_len) {
        length = str_len - start;
    }
    
    char* temp_result = malloc(length + 1);
    if (!temp_result) {
        vm_runtime_error(vm, "Memory allocation failed");
        *result = make_nil_value();
        return;
    }
    
    strncpy(temp_result, str + start, length);
    temp_result[length] = '\0';
    
    *result = make_string_value_from_cstr(temp_result);
    free(temp_result);
}

void builtin_concat_vm(MobiusVM* vm, int arg_count, void* result_ptr) {
    Value* result = (Value*)result_ptr;
    if (arg_count < 2) {
        vm_runtime_error(vm, "concat expects at least 2 arguments");
        *result = make_nil_value();
        return;
    }
    
    // Calculate total length needed and validate all arguments are strings
    size_t total_len = 0;
    for (int i = 0; i < arg_count; i++) {
        Value arg = vm_peek(vm, arg_count - 1 - i);
        if (arg.type != VAL_STRING) {
            vm_runtime_error(vm, "concat() all arguments must be strings");
            *result = make_nil_value();
            return;
        }
        if (arg.as.string) {
            total_len += string_length(arg.as.string);
        }
    }
    
    char* temp_result = malloc(total_len + 1);
    if (!temp_result) {
        vm_runtime_error(vm, "Memory allocation failed");
        *result = make_nil_value();
        return;
    }
    
    temp_result[0] = '\0';
    for (int i = 0; i < arg_count; i++) {
        Value arg = vm_peek(vm, arg_count - 1 - i);
        if (arg.as.string) {
            const char* str_data = string_data(arg.as.string);
            if (str_data) {
                strcat(temp_result, str_data);
            }
        }
    }
    
    *result = make_string_value_from_cstr(temp_result);
    free(temp_result);
}

void builtin_contains_vm(MobiusVM* vm, int arg_count, void* result_ptr) {
    Value* result = (Value*)result_ptr;
    if (arg_count != 2) {
        vm_runtime_error(vm, "contains expects exactly 2 arguments: contains(string, substring)");
        *result = make_nil_value();
        return;
    }
    
    Value str_val = vm_peek(vm, 1);    // First argument
    Value substr_val = vm_peek(vm, 0); // Second argument
    
    if (str_val.type != VAL_STRING || substr_val.type != VAL_STRING) {
        vm_runtime_error(vm, "contains() requires string arguments");
        *result = make_nil_value();
        return;
    }
    
    const char* str = str_val.as.string ? string_data(str_val.as.string) : "";
    const char* substr = substr_val.as.string ? string_data(substr_val.as.string) : "";
    
    bool found = strstr(str, substr) != NULL;
    *result = make_bool_value(found);
}
