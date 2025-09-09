#include "bytecode_core.h"
#include "bytecode.h"
#include "value.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// =============================================================================
// CORE BUILTIN FUNCTIONS FOR BYTECODE VM  
// =============================================================================

void builtin_print_vm(MobiusVM* vm, int arg_count, void* result_ptr) {
    Value* result = (Value*)result_ptr;
    for (int i = 0; i < arg_count; i++) {
        Value arg = vm_peek(vm, arg_count - 1 - i);  // Get args in correct order
        char* str = value_to_string(arg);
        if (str) {
            printf("%s", str);
            if (i < arg_count - 1) printf(" ");  // Space between arguments
            free(str);
        }
    }
    printf("\n");
    *result = make_nil_value();
}

void builtin_typeof_vm(MobiusVM* vm, int arg_count, void* result_ptr) {
    Value* result = (Value*)result_ptr;
    if (arg_count != 1) {
        vm_runtime_error(vm, "typeof expects 1 argument");
        *result = make_nil_value();
        return;
    }

    Value arg = vm_peek(vm, 0);
    const char* type_name = value_type_name(arg.type);
    *result = make_string_value_from_cstr(type_name);
}

void builtin_int_vm(MobiusVM* vm, int arg_count, void* result_ptr) {
    Value* result = (Value*)result_ptr;
    if (arg_count != 1) {
        vm_runtime_error(vm, "int expects 1 argument");
        *result = make_nil_value();
        return;
    }

    Value arg = vm_peek(vm, 0);
    switch (arg.type) {
        case VAL_INTEGER:
            *result = arg;  // Already an integer
            break;
        case VAL_FLOAT32:
            *result = make_integer_value(NUM_INT32, (int32_t)arg.as.float32_val);
            break;
        case VAL_FLOAT:
            *result = make_integer_value(NUM_INT32, (int32_t)arg.as.float_val);
            break;
        case VAL_STRING: {
            if (arg.as.string) {
                const char* str = string_data(arg.as.string);
                char* endptr;
                long val = strtol(str, &endptr, 10);
                if (*endptr == '\0') {
                    *result = make_integer_value(NUM_INT32, (int32_t)val);
                } else {
                    vm_runtime_error(vm, "Cannot convert string to integer");
                    *result = make_nil_value();
                }
            } else {
                vm_runtime_error(vm, "Cannot convert null string to integer");
                *result = make_nil_value();
            }
            break;
        }
        case VAL_BOOL:
            *result = make_integer_value(NUM_INT32, arg.as.boolean ? 1 : 0);
            break;
        default:
            vm_runtime_error(vm, "Cannot convert value to integer");
            *result = make_nil_value();
            break;
    }
}

void builtin_float_vm(MobiusVM* vm, int arg_count, void* result_ptr) {
    Value* result = (Value*)result_ptr;
    if (arg_count != 1) {
        vm_runtime_error(vm, "float expects 1 argument");
        *result = make_nil_value();
        return;
    }

    Value arg = vm_peek(vm, 0);
    switch (arg.type) {
        case VAL_FLOAT:
            *result = arg;  // Already a float
            break;
        case VAL_FLOAT32:
            *result = make_float_value((double)arg.as.float32_val);
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
            *result = make_float_value((double)val);
            break;
        }
        case VAL_STRING: {
            if (arg.as.string) {
                const char* str = string_data(arg.as.string);
                char* endptr;
                double val = strtod(str, &endptr);
                if (*endptr == '\0') {
                    *result = make_float_value(val);
                } else {
                    vm_runtime_error(vm, "Cannot convert string to float");
                    *result = make_nil_value();
                }
            } else {
                vm_runtime_error(vm, "Cannot convert null string to float");
                *result = make_nil_value();
            }
            break;
        }
        default:
            vm_runtime_error(vm, "Cannot convert value to float");
            *result = make_nil_value();
            break;
    }
}

void builtin_str_vm(MobiusVM* vm, int arg_count, void* result_ptr) {
    Value* result = (Value*)result_ptr;
    if (arg_count != 1) {
        vm_runtime_error(vm, "str expects 1 argument");
        *result = make_nil_value();
        return;
    }

    Value arg = vm_peek(vm, 0);
    char* temp_str = value_to_string(arg);
    if (temp_str) {
        *result = make_string_value_from_cstr(temp_str);
        free(temp_str);
    } else {
        *result = make_nil_value();
    }
}
