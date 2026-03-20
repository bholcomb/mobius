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

int lib_print(MobiusState* state, int arg_count) {
    for (int i = 0; i < arg_count; i++) {
        Value arg = state->mainContext()->peek( arg_count - 1 - i);  // Get args in correct order
        
        // Handle strings specially to process escape sequences
        if (arg.type == VAL_STRING && arg.as.string) {
            const char* str_data = arg.as.string->data;
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
        state->mainContext()->pop();
    }
    
    return 0;
}

int lib_typeof(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("typeof expects 1 argument");
    }

    Value arg = state->mainContext()->peek( 0);
    const char* type_name = value_type_name(arg.type);
    
    // Pop argument from stack
    state->mainContext()->pop();
    
    // Push result onto stack
    Value result = make_string_value_from_cstr(state, type_name);
    state->mainContext()->push( result);
    
    return 1;
}

int lib_int(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("int expects 1 argument");
    }

    Value arg = state->mainContext()->pop();
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
                const char* str = arg.as.string->data;
                char* endptr;
                long val = strtol(str, &endptr, 10);
                if (*endptr == '\0') {
                    result = make_integer_value(NUM_INT32, (int32_t)val);
                } else {
                    return state->error("Cannot convert string to integer");
                }
            } else {
                return state->error("Cannot convert null string to integer");
            }
            break;
        }
        case VAL_BOOL:
            result = make_integer_value(NUM_INT32, arg.as.boolean ? 1 : 0);
            break;
        default:
            return state->error("Cannot convert value to integer");
    }
       
    // Push result onto stack
    state->mainContext()->push( result);
    
    return 1;
}

int lib_float(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("float expects 1 argument");
    }

    Value arg = state->mainContext()->peek( 0);
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
                const char* str = arg.as.string->data;
                char* endptr;
                double val = strtod(str, &endptr);
                if (*endptr == '\0') {
                    result = make_float_value(val);
                } else {
                    return state->error("Cannot convert string to float");
                }
            } else {
                return state->error("Cannot convert null string to float");
            }
            break;
        }
        default:
            return state->error("Cannot convert value to float");
    }
    
    // Pop argument from stack
    state->mainContext()->pop();
    
    // Push result onto stack
    state->mainContext()->push( result);
    
    return 1;
}

int lib_str(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("str expects 1 argument");
    }

    Value arg = state->mainContext()->peek( 0);
    char* temp_str = value_to_string(arg);
    Value result;
    
    if (temp_str) {
        result = make_string_value_from_cstr(state, temp_str);
        free(temp_str);
    } else {
        result = make_nil_value();
    }
    
    // Pop argument from stack
    state->mainContext()->pop();
    
    // Push result onto stack
    state->mainContext()->push( result);
    
    return 1;
}
