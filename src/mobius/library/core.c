#include "library/core.h"
#include "data/value.h"
#include "state/environment.h"
#include "eval/evaluator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Helper function to process escape sequences in strings
static void print_with_escapes(const char* str) {
    for (const char* p = str; *p; p++) {
        if (*p == '\\' && *(p + 1)) {
            p++; // Skip the backslash
            switch (*p) {
                case 'n': putchar('\n'); break;
                case 't': putchar('\t'); break;
                case 'r': putchar('\r'); break;
                case '\\': putchar('\\'); break;
                case '"': putchar('"'); break;
                case '\'': putchar('\''); break;
                default:
                    // Unknown escape, print both characters
                    putchar('\\');
                    putchar(*p);
                    break;
            }
        } else {
            putchar(*p);
        }
    }
}

// =============================================================================
// UNIFIED CORE FUNCTION IMPLEMENTATIONS
// =============================================================================

EvalResult lib_print(MobiusState* state, int arg_count) {
    for (int i = 0; i < arg_count; i++) {
        Value arg = ctx_peek(state->main_context, arg_count - 1 - i);  // Get args in correct order
        
        // Handle strings specially to process escape sequences
        if (arg.type == VAL_STRING && arg.as.string) {
            const char* str_data = string_data(arg.as.string);
            if (str_data) {
                print_with_escapes(str_data);
            }
        } else {
            // For non-strings, use print_value to get proper formatting (especially for tables)
            print_value(arg);
        }
        
        if (i < arg_count - 1) printf(" ");  // Space between arguments
    }
    printf("\n");
    
    // Pop arguments from stack
    for (int i = 0; i < arg_count; i++) {
        free_value(ctx_pop(state->main_context));
    }
    
    return make_success(0);
}

EvalResult lib_typeof(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "typeof expects 1 argument", 0, 0);
    }

    Value arg = ctx_peek(state->main_context, 0);
    const char* type_name = value_type_name(arg.type);
    
    // Pop argument from stack
    ctx_pop(state->main_context);
    
    // Push result onto stack
    Value result = make_string_value_from_cstr(state, type_name);
    ctx_push(state->main_context, result);
    
    return make_success(1);
}

EvalResult lib_int(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "int expects 1 argument", 0, 0);
    }

    Value arg = ctx_pop(state->main_context);
    Value result;
    
    switch (arg.type) {
        case VAL_INTEGER:
            result = arg;  // Already an integer
            break;
        case VAL_FLOAT32:
            result = make_integer_value(NUM_INT32, (int32_t)arg.as.float32_val);
            break;
        case VAL_FLOAT64:
            result = make_integer_value(NUM_INT32, (int32_t)arg.as.float64_val);
            break;
        case VAL_STRING: {
            if (arg.as.string) {
                const char* str = string_data(arg.as.string);
                char* endptr;
                long val = strtol(str, &endptr, 10);
                if (*endptr == '\0') {
                    result = make_integer_value(NUM_INT32, (int32_t)val);
                } else {
                    return make_error(state->main_context->current_env, "Cannot convert string to integer", 0, 0);
                }
            } else {
                return make_error(state->main_context->current_env, "Cannot convert null string to integer", 0, 0);
            }
            break;
        }
        case VAL_BOOL:
            result = make_integer_value(NUM_INT32, arg.as.boolean ? 1 : 0);
            break;
        default:
            return make_error(state->main_context->current_env, "Cannot convert value to integer", 0, 0);
    }
       
    // Push result onto stack
    ctx_push(state->main_context, result);
    
    return make_success(1);
}

EvalResult lib_float(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "float expects 1 argument", 0, 0);
    }

    Value arg = ctx_peek(state->main_context, 0);
    Value result;
    
    switch (arg.type) {
        case VAL_FLOAT64:
            result = arg;  // Already a float
            break;
        case VAL_FLOAT32:
            result = make_float_value((double)arg.as.float32_val);
            break;
        case VAL_INTEGER: {
            int64_t val;
            switch (arg.as.integer.num_type) {
                case NUM_INT8:   val = arg.as.integer.value.i8; break;
                case NUM_UINT8:  val = arg.as.integer.value.u8; break;
                case NUM_INT16:  val = arg.as.integer.value.i16; break;
                case NUM_UINT16: val = arg.as.integer.value.u16; break;
                case NUM_INT32:  val = arg.as.integer.value.i32; break;
                case NUM_UINT32: val = arg.as.integer.value.u32; break;
                case NUM_INT64:  val = arg.as.integer.value.i64; break;
                case NUM_UINT64: val = arg.as.integer.value.u64; break;
                default: val = arg.as.integer.value.i32; break;
            }
            result = make_float_value((double)val);
            break;
        }
        case VAL_STRING: {
            if (arg.as.string) {
                const char* str = string_data(arg.as.string);
                char* endptr;
                double val = strtod(str, &endptr);
                if (*endptr == '\0') {
                    result = make_float_value(val);
                } else {
                    return make_error(state->main_context->current_env, "Cannot convert string to float", 0, 0);
                }
            } else {
                return make_error(state->main_context->current_env, "Cannot convert null string to float", 0, 0);
            }
            break;
        }
        default:
            return make_error(state->main_context->current_env, "Cannot convert value to float", 0, 0);
    }
    
    // Pop argument from stack
    ctx_pop(state->main_context);
    
    // Push result onto stack
    ctx_push(state->main_context, result);
    
    return make_success(1);
}

EvalResult lib_str(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "str expects 1 argument", 0, 0);
    }

    Value arg = ctx_peek(state->main_context, 0);
    char* temp_str = value_to_string(arg);
    Value result;
    
    if (temp_str) {
        result = make_string_value_from_cstr(state, temp_str);
        free(temp_str);
    } else {
        result = make_nil_value();
    }
    
    // Pop argument from stack
    ctx_pop(state->main_context);
    
    // Push result onto stack
    ctx_push(state->main_context, result);
    
    return make_success(1);
}
