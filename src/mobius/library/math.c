#include "library/math.h"
#include "data/value.h"
#include "state/environment.h"
#include "eval/evaluator.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>


// =============================================================================
// UNIFIED MATH FUNCTION IMPLEMENTATIONS
// =============================================================================

EvalResult lib_abs(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "abs expects 1 argument", 0, 0);
    }

    Value arg = ctx_peek(state->main_context, 0);
    Value result;
    
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
        result = make_integer_value(NUM_INT64, val < 0 ? -val : val);
    } else if (arg.type == VAL_FLOAT64) {
        result = make_float_value(fabs(arg.as.float64_val));
    } else if (arg.type == VAL_FLOAT32) {
        result = make_float32_value(fabsf(arg.as.float32_val));
    } else {
        return make_error(state->main_context->current_env, "abs expects a numeric argument", 0, 0);
    }
    
    // Pop argument from stack
    ctx_pop(state->main_context);
    
    // Push result onto stack
    ctx_push(state->main_context, result);
    
    return make_success(1);
}

EvalResult lib_min(MobiusState* state, int arg_count) {
    if (arg_count < 2) {
        return make_error(state->main_context->current_env, "min expects at least 2 arguments", 0, 0);
    }

    Value min_val = ctx_peek(state->main_context, arg_count - 1);  // First argument
    
    for (int i = arg_count - 2; i >= 0; i--) {
        Value current = ctx_peek(state->main_context, i);
        
        // Compare numeric values
        bool is_less = false;
        if (min_val.type == VAL_INTEGER && current.type == VAL_INTEGER) {
            int64_t min_int = 0, current_int = 0;
            // Extract integer values
            switch (min_val.as.integer.num_type) {
                case NUM_INT8:   min_int = min_val.as.integer.value.i8; break;
                case NUM_UINT8:  min_int = min_val.as.integer.value.u8; break;
                case NUM_INT16:  min_int = min_val.as.integer.value.i16; break;
                case NUM_UINT16: min_int = min_val.as.integer.value.u16; break;
                case NUM_INT32:  min_int = min_val.as.integer.value.i32; break;
                case NUM_UINT32: min_int = min_val.as.integer.value.u32; break;
                case NUM_INT64:  min_int = min_val.as.integer.value.i64; break;
                case NUM_UINT64: min_int = min_val.as.integer.value.u64; break;
                default: min_int = min_val.as.integer.value.i32; break;
            }
            switch (current.as.integer.num_type) {
                case NUM_INT8:   current_int = current.as.integer.value.i8; break;
                case NUM_UINT8:  current_int = current.as.integer.value.u8; break;
                case NUM_INT16:  current_int = current.as.integer.value.i16; break;
                case NUM_UINT16: current_int = current.as.integer.value.u16; break;
                case NUM_INT32:  current_int = current.as.integer.value.i32; break;
                case NUM_UINT32: current_int = current.as.integer.value.u32; break;
                case NUM_INT64:  current_int = current.as.integer.value.i64; break;
                case NUM_UINT64: current_int = current.as.integer.value.u64; break;
                default: current_int = current.as.integer.value.i32; break;
            }
            is_less = current_int < min_int;
        } else if (min_val.type == VAL_FLOAT64 && current.type == VAL_FLOAT64) {
            is_less = current.as.float64_val < min_val.as.float64_val;
        } else if (min_val.type == VAL_FLOAT32 && current.type == VAL_FLOAT32) {
            is_less = current.as.float32_val < min_val.as.float32_val;
        } else {
            return make_error(state->main_context->current_env, "min expects numeric arguments of compatible types", 0, 0);
        }
        
        if (is_less) {
            min_val = current;
        }
    }
    
    // Pop all arguments from stack
    for (int i = 0; i < arg_count; i++) {
        ctx_pop(state->main_context);
    }
    
    // Push result onto stack
    ctx_push(state->main_context, min_val);
    
    return make_success(1);
}

EvalResult lib_max(MobiusState* state, int arg_count) {
    if (arg_count < 2) {
        return make_error(state->main_context->current_env, "max expects at least 2 arguments", 0, 0);
    }

    Value max_val = ctx_peek(state->main_context, arg_count - 1);  // First argument
    
    for (int i = arg_count - 2; i >= 0; i--) {
        Value current = ctx_peek(state->main_context, i);
        
        // Compare numeric values (similar logic to min but reversed)
        bool is_greater = false;
        if (max_val.type == VAL_INTEGER && current.type == VAL_INTEGER) {
            int64_t max_int = 0, current_int = 0;
            // Extract integer values (same as min)
            switch (max_val.as.integer.num_type) {
                case NUM_INT8:   max_int = max_val.as.integer.value.i8; break;
                case NUM_UINT8:  max_int = max_val.as.integer.value.u8; break;
                case NUM_INT16:  max_int = max_val.as.integer.value.i16; break;
                case NUM_UINT16: max_int = max_val.as.integer.value.u16; break;
                case NUM_INT32:  max_int = max_val.as.integer.value.i32; break;
                case NUM_UINT32: max_int = max_val.as.integer.value.u32; break;
                case NUM_INT64:  max_int = max_val.as.integer.value.i64; break;
                case NUM_UINT64: max_int = max_val.as.integer.value.u64; break;
                default: max_int = max_val.as.integer.value.i32; break;
            }
            switch (current.as.integer.num_type) {
                case NUM_INT8:   current_int = current.as.integer.value.i8; break;
                case NUM_UINT8:  current_int = current.as.integer.value.u8; break;
                case NUM_INT16:  current_int = current.as.integer.value.i16; break;
                case NUM_UINT16: current_int = current.as.integer.value.u16; break;
                case NUM_INT32:  current_int = current.as.integer.value.i32; break;
                case NUM_UINT32: current_int = current.as.integer.value.u32; break;
                case NUM_INT64:  current_int = current.as.integer.value.i64; break;
                case NUM_UINT64: current_int = current.as.integer.value.u64; break;
                default: current_int = current.as.integer.value.i32; break;
            }
            is_greater = current_int > max_int;
        } else if (max_val.type == VAL_FLOAT64 && current.type == VAL_FLOAT64) {
            is_greater = current.as.float64_val > max_val.as.float64_val;
        } else if (max_val.type == VAL_FLOAT32 && current.type == VAL_FLOAT32) {
            is_greater = current.as.float32_val > max_val.as.float32_val;
        } else {
            return make_error(state->main_context->current_env, "max expects numeric arguments of compatible types", 0, 0);
        }
        
        if (is_greater) {
            max_val = current;
        }
    }
    
    // Pop all arguments from stack
    for (int i = 0; i < arg_count; i++) {
        ctx_pop(state->main_context);
    }
    
    // Push result onto stack
    ctx_push(state->main_context, max_val);
    
    return make_success(1);
}

EvalResult lib_pow(MobiusState* state, int arg_count) {
    if (arg_count != 2) {
        return make_error(state->main_context->current_env, "pow expects 2 arguments", 0, 0);
    }

    Value base = ctx_peek(state->main_context, 1);
    Value exponent = ctx_peek(state->main_context, 0);
    
    double base_val, exp_val;
    
    // Convert base to double
    if (base.type == VAL_FLOAT64) {
        base_val = base.as.float64_val;
    } else if (base.type == VAL_FLOAT32) {
        base_val = (double)base.as.float32_val;
    } else if (base.type == VAL_INTEGER) {
        int64_t val;
        switch (base.as.integer.num_type) {
            case NUM_INT8:   val = base.as.integer.value.i8; break;
            case NUM_UINT8:  val = base.as.integer.value.u8; break;
            case NUM_INT16:  val = base.as.integer.value.i16; break;
            case NUM_UINT16: val = base.as.integer.value.u16; break;
            case NUM_INT32:  val = base.as.integer.value.i32; break;
            case NUM_UINT32: val = base.as.integer.value.u32; break;
            case NUM_INT64:  val = base.as.integer.value.i64; break;
            case NUM_UINT64: val = base.as.integer.value.u64; break;
            default: val = base.as.integer.value.i32; break;
        }
        base_val = (double)val;
    } else {
        return make_error(state->main_context->current_env, "pow expects numeric arguments", 0, 0);
    }
    
    // Convert exponent to double (similar logic)
    if (exponent.type == VAL_FLOAT64) {
        exp_val = exponent.as.float64_val;
    } else if (exponent.type == VAL_FLOAT32) {
        exp_val = (double)exponent.as.float32_val;
    } else if (exponent.type == VAL_INTEGER) {
        int64_t val;
        switch (exponent.as.integer.num_type) {
            case NUM_INT8:   val = exponent.as.integer.value.i8; break;
            case NUM_UINT8:  val = exponent.as.integer.value.u8; break;
            case NUM_INT16:  val = exponent.as.integer.value.i16; break;
            case NUM_UINT16: val = exponent.as.integer.value.u16; break;
            case NUM_INT32:  val = exponent.as.integer.value.i32; break;
            case NUM_UINT32: val = exponent.as.integer.value.u32; break;
            case NUM_INT64:  val = exponent.as.integer.value.i64; break;
            case NUM_UINT64: val = exponent.as.integer.value.u64; break;
            default: val = exponent.as.integer.value.i32; break;
        }
        exp_val = (double)val;
    } else {
        return make_error(state->main_context->current_env, "pow expects numeric arguments", 0, 0);
    }
    
    double result_val = pow(base_val, exp_val);
    
    // Pop arguments from stack
    ctx_pop(state->main_context);
    ctx_pop(state->main_context);
    
    // Push result onto stack
    ctx_push(state->main_context, make_float_value(result_val));
    
    return make_success(1);
}

EvalResult lib_sqrt(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "sqrt expects 1 argument", 0, 0);
    }

    Value arg = ctx_peek(state->main_context, 0);
    double val;
    
    // Convert to double
    if (arg.type == VAL_FLOAT64) {
        val = arg.as.float64_val;
    } else if (arg.type == VAL_FLOAT32) {
        val = (double)arg.as.float32_val;
    } else if (arg.type == VAL_INTEGER) {
        int64_t int_val;
        switch (arg.as.integer.num_type) {
            case NUM_INT8:   int_val = arg.as.integer.value.i8; break;
            case NUM_UINT8:  int_val = arg.as.integer.value.u8; break;
            case NUM_INT16:  int_val = arg.as.integer.value.i16; break;
            case NUM_UINT16: int_val = arg.as.integer.value.u16; break;
            case NUM_INT32:  int_val = arg.as.integer.value.i32; break;
            case NUM_UINT32: int_val = arg.as.integer.value.u32; break;
            case NUM_INT64:  int_val = arg.as.integer.value.i64; break;
            case NUM_UINT64: int_val = arg.as.integer.value.u64; break;
            default: int_val = arg.as.integer.value.i32; break;
        }
        val = (double)int_val;
    } else {
        return make_error(state->main_context->current_env, "sqrt expects a numeric argument", 0, 0);
    }
    
    if (val < 0) {
        return make_error(state->main_context->current_env, "sqrt of negative number", 0, 0);
    }
    
    // Pop argument from stack
    ctx_pop(state->main_context);
    
    // Push result onto stack
    ctx_push(state->main_context, make_float_value(sqrt(val)));
    
    return make_success(1);
}

EvalResult lib_floor(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "floor expects 1 argument", 0, 0);
    }

    Value arg = ctx_peek(state->main_context, 0);
    double val;
    
    // Convert to double
    if (arg.type == VAL_FLOAT64) {
        val = arg.as.float64_val;
    } else if (arg.type == VAL_FLOAT32) {
        val = (double)arg.as.float32_val;
    } else if (arg.type == VAL_INTEGER) {
        // Integer floor is just the integer itself
        ctx_pop(state->main_context);
        ctx_push(state->main_context, arg);
        return make_success(1);
    } else {
        return make_error(state->main_context->current_env, "floor expects a numeric argument", 0, 0);
    }
    
    // Pop argument from stack
    ctx_pop(state->main_context);
    
    // Push result onto stack
    ctx_push(state->main_context, make_float_value(floor(val)));
    
    return make_success(1);
}

EvalResult lib_ceil(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "ceil expects 1 argument", 0, 0);
    }

    Value arg = ctx_peek(state->main_context, 0);
    double val;
    
    // Convert to double
    if (arg.type == VAL_FLOAT64) {
        val = arg.as.float64_val;
    } else if (arg.type == VAL_FLOAT32) {
        val = (double)arg.as.float32_val;
    } else if (arg.type == VAL_INTEGER) {
        // Integer ceil is just the integer itself
        ctx_pop(state->main_context);
        ctx_push(state->main_context, arg);
        return make_success(1);
    } else {
        return make_error(state->main_context->current_env, "ceil expects a numeric argument", 0, 0);
    }
    
    // Pop argument from stack
    ctx_pop(state->main_context);
    
    // Push result onto stack
    ctx_push(state->main_context, make_float_value(ceil(val)));
    
    return make_success(1);
}

EvalResult lib_round(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "round expects 1 argument", 0, 0);
    }

    Value arg = ctx_peek(state->main_context, 0);
    double val;
    
    // Convert to double
    if (arg.type == VAL_FLOAT64) {
        val = arg.as.float64_val;
    } else if (arg.type == VAL_FLOAT32) {
        val = (double)arg.as.float32_val;
    } else if (arg.type == VAL_INTEGER) {
        // Integer round is just the integer itself
        ctx_pop(state->main_context);
        ctx_push(state->main_context, arg);
        return make_success(1);
    } else {
        return make_error(state->main_context->current_env, "round expects a numeric argument", 0, 0);
    }
    
    // Pop argument from stack
    ctx_pop(state->main_context);
    
    // Push result onto stack
    ctx_push(state->main_context, make_float_value(round(val)));
    
    return make_success(1);
}
