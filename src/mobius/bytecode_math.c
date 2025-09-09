#include "bytecode_math.h"
#include "bytecode.h"
#include "value.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

// =============================================================================
// MATH BUILTIN FUNCTIONS FOR BYTECODE VM
// =============================================================================

void builtin_abs_vm(MobiusVM* vm, int arg_count, void* result_ptr) {
    Value* result = (Value*)result_ptr;
    if (arg_count != 1) {
        vm_runtime_error(vm, "abs expects 1 argument");
        *result = make_nil_value();
        return;
    }

    Value arg = vm_peek(vm, 0);
    if (arg.type == VAL_INTEGER) {
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
        *result = make_integer_value(NUM_INT64, val < 0 ? -val : val);
    } else if (arg.type == VAL_FLOAT) {
        *result = make_float_value(fabs(arg.as.float_val));
    } else if (arg.type == VAL_FLOAT32) {
        *result = make_float32_value(fabsf(arg.as.float32_val));
    } else {
        vm_runtime_error(vm, "abs expects a numeric argument");
        *result = make_nil_value();
    }
}

void builtin_min_vm(MobiusVM* vm, int arg_count, void* result_ptr) {
    Value* result = (Value*)result_ptr;
    if (arg_count < 2) {
        vm_runtime_error(vm, "min expects at least 2 arguments");
        *result = make_nil_value();
        return;
    }

    Value min_val = vm_peek(vm, arg_count - 1);
    for (int i = arg_count - 2; i >= 0; i--) {
        Value current = vm_peek(vm, i);
        // Simple comparison for numeric types
        if (current.type == VAL_INTEGER && min_val.type == VAL_INTEGER) {
            if (current.as.integer.value.i64 < min_val.as.integer.value.i64) {
                min_val = current;
            }
        } else if (current.type == VAL_FLOAT && min_val.type == VAL_FLOAT) {
            if (current.as.float_val < min_val.as.float_val) {
                min_val = current;
            }
        }
    }
    *result = min_val;
}

void builtin_max_vm(MobiusVM* vm, int arg_count, void* result_ptr) {
    Value* result = (Value*)result_ptr;
    if (arg_count < 2) {
        vm_runtime_error(vm, "max expects at least 2 arguments");
        *result = make_nil_value();
        return;
    }

    Value max_val = vm_peek(vm, arg_count - 1);
    for (int i = arg_count - 2; i >= 0; i--) {
        Value current = vm_peek(vm, i);
        // Simple comparison for numeric types
        if (current.type == VAL_INTEGER && max_val.type == VAL_INTEGER) {
            if (current.as.integer.value.i64 > max_val.as.integer.value.i64) {
                max_val = current;
            }
        } else if (current.type == VAL_FLOAT && max_val.type == VAL_FLOAT) {
            if (current.as.float_val > max_val.as.float_val) {
                max_val = current;
            }
        }
    }
    *result = max_val;
}

void builtin_pow_vm(MobiusVM* vm, int arg_count, void* result_ptr) {
    Value* result = (Value*)result_ptr;
    if (arg_count != 2) {
        vm_runtime_error(vm, "pow expects 2 arguments");
        *result = make_nil_value();
        return;
    }

    Value base = vm_peek(vm, 1);
    Value exponent = vm_peek(vm, 0);
    
    if ((base.type != VAL_INTEGER && base.type != VAL_FLOAT) ||
        (exponent.type != VAL_INTEGER && exponent.type != VAL_FLOAT)) {
        vm_runtime_error(vm, "pow expects numeric arguments");
        *result = make_nil_value();
        return;
    }
    
    double base_val = (base.type == VAL_FLOAT) ? base.as.float_val : (double)base.as.integer.value.i64;
    double exp_val = (exponent.type == VAL_FLOAT) ? exponent.as.float_val : (double)exponent.as.integer.value.i64;
    
    *result = make_float_value(pow(base_val, exp_val));
}

void builtin_sqrt_vm(MobiusVM* vm, int arg_count, void* result_ptr) {
    Value* result = (Value*)result_ptr;
    if (arg_count != 1) {
        vm_runtime_error(vm, "sqrt expects 1 argument");
        *result = make_nil_value();
        return;
    }

    Value arg = vm_peek(vm, 0);
    if (arg.type == VAL_INTEGER) {
        double val = (double)arg.as.integer.value.i64;
        if (val < 0) {
            vm_runtime_error(vm, "sqrt of negative number");
            *result = make_nil_value();
            return;
        }
        *result = make_float_value(sqrt(val));
    } else if (arg.type == VAL_FLOAT) {
        if (arg.as.float_val < 0) {
            vm_runtime_error(vm, "sqrt of negative number");
            *result = make_nil_value();
            return;
        }
        *result = make_float_value(sqrt(arg.as.float_val));
    } else {
        vm_runtime_error(vm, "sqrt expects a numeric argument");
        *result = make_nil_value();
    }
}

void builtin_floor_vm(MobiusVM* vm, int arg_count, void* result_ptr) {
    Value* result = (Value*)result_ptr;
    if (arg_count != 1) {
        vm_runtime_error(vm, "floor expects 1 argument");
        *result = make_nil_value();
        return;
    }

    Value arg = vm_peek(vm, 0);
    if (arg.type == VAL_INTEGER) {
        *result = arg;  // Integers are already "floored"
    } else if (arg.type == VAL_FLOAT) {
        *result = make_float_value(floor(arg.as.float_val));
    } else if (arg.type == VAL_FLOAT32) {
        *result = make_float32_value(floorf(arg.as.float32_val));
    } else {
        vm_runtime_error(vm, "floor expects a numeric argument");
        *result = make_nil_value();
    }
}

void builtin_ceil_vm(MobiusVM* vm, int arg_count, void* result_ptr) {
    Value* result = (Value*)result_ptr;
    if (arg_count != 1) {
        vm_runtime_error(vm, "ceil expects 1 argument");
        *result = make_nil_value();
        return;
    }

    Value arg = vm_peek(vm, 0);
    if (arg.type == VAL_INTEGER) {
        *result = arg;  // Integers are already "ceiled"
    } else if (arg.type == VAL_FLOAT) {
        *result = make_float_value(ceil(arg.as.float_val));
    } else if (arg.type == VAL_FLOAT32) {
        *result = make_float32_value(ceilf(arg.as.float32_val));
    } else {
        vm_runtime_error(vm, "ceil expects a numeric argument");
        *result = make_nil_value();
    }
}

void builtin_round_vm(MobiusVM* vm, int arg_count, void* result_ptr) {
    Value* result = (Value*)result_ptr;
    if (arg_count != 1) {
        vm_runtime_error(vm, "round expects 1 argument");
        *result = make_nil_value();
        return;
    }

    Value arg = vm_peek(vm, 0);
    if (arg.type == VAL_INTEGER) {
        *result = arg;  // Integers are already "rounded"
    } else if (arg.type == VAL_FLOAT) {
        *result = make_float_value(round(arg.as.float_val));
    } else if (arg.type == VAL_FLOAT32) {
        *result = make_float32_value(roundf(arg.as.float32_val));
    } else {
        vm_runtime_error(vm, "round expects a numeric argument");
        *result = make_nil_value();
    }
}
