#include "../../../src/mobius/plugin/plugin.h"
#include "../../../src/mobius/frontend/ast.h"
#include "../../../src/mobius/eval/evaluator.h"
#include "../../../src/mobius/state/environment.h"
#include "../../../src/mobius/state/mobius_state.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

// Define mathematical constants if not available
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef M_E
#define M_E 2.7182818284590452354
#endif


static int64_t extract_int64(Value val)
{
    int64_t value = 0;
    switch (val.type)
    {
        case VAL_FLOAT32:
            value = (int64_t)val.as.float32_val; break;
        case VAL_FLOAT64:
            value = (int64_t)val.as.float64_val; break;
        case VAL_INTEGER:
            switch (val.as.integer.num_type) {
                case NUM_INT8:   value = (int64_t)val.as.integer.value.i8; break;
                case NUM_UINT8:  value = (int64_t)val.as.integer.value.u8; break;
                case NUM_INT16:  value = (int64_t)val.as.integer.value.i16; break;
                case NUM_UINT16: value = (int64_t)val.as.integer.value.u16; break;
                case NUM_INT32:  value = (int64_t)val.as.integer.value.i32; break;
                case NUM_UINT32: value = (int64_t)val.as.integer.value.u32; break;
                case NUM_INT64:  value = (int64_t)val.as.integer.value.i64; break;
                case NUM_UINT64: value = (int64_t)val.as.integer.value.u64; break;
                default: value = 0; break;
            }
            break;
        default:
            value = 0; break;
    }
    
    return value;
}

// Helper function to extract numeric value from a Value
static double extract_number(Value val) {
    double value = 0.0;
    switch (val.type) {
        case VAL_FLOAT32:
            value = (double)val.as.float32_val; break;
        case VAL_FLOAT64:
            value = val.as.float64_val; break;
        case VAL_INTEGER:
            // Handle different integer types
            switch (val.as.integer.num_type) {
                case NUM_INT8:   value = (double)val.as.integer.value.i8; break;
                case NUM_UINT8:  value = (double)val.as.integer.value.u8; break;
                case NUM_INT16:  value = (double)val.as.integer.value.i16; break;
                case NUM_UINT16: value = (double)val.as.integer.value.u16; break;
                case NUM_INT32:  value = (double)val.as.integer.value.i32; break;
                case NUM_UINT32: value = (double)val.as.integer.value.u32; break;
                case NUM_INT64:  value = (double)val.as.integer.value.i64; break;
                case NUM_UINT64: value = (double)val.as.integer.value.u64; break;
                default: value = 0.0; break;
            }
            break;
        default:
            value = 0.0; break;
    }

    return value;
}

// ============================================================================
// TRIGONOMETRIC FUNCTIONS
// ============================================================================

EvalResult math_sin(MobiusState* state, int arg_count) {
    // ctx is now passed as parameter
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "sin() expects exactly 1 argument", 0, 0);
    }
    
    Value num_val = ctx_pop(state->main_context);
    if (num_val.type != VAL_FLOAT64 && num_val.type != VAL_INTEGER) {
        return make_error(state->main_context->current_env, "sin() expects a numeric argument", 0, 0);
    }
    
    double val = extract_number(num_val);
    ctx_push(state->main_context, make_float_value(sin(val)));
    return make_success(1);
}

EvalResult math_cos(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "cos() expects exactly 1 argument", 0, 0);
    }
    
    Value num_val = ctx_pop(state->main_context);
    if (num_val.type != VAL_FLOAT64 && num_val.type != VAL_INTEGER) {
        return make_error(state->main_context->current_env, "cos() expects a numeric argument", 0, 0);
    }
    
    double val = extract_number(num_val);
    ctx_push(state->main_context, make_float_value(cos(val)));
    return make_success(1);
}

EvalResult math_tan(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "tan() expects exactly 1 argument", 0, 0);
    }
    
    Value num_val = ctx_pop(state->main_context);
    if (num_val.type != VAL_FLOAT64 && num_val.type != VAL_INTEGER) {
        return make_error(state->main_context->current_env, "tan() expects a numeric argument", 0, 0);
    }
    
    double val = extract_number(num_val);
    ctx_push(state->main_context    , make_float_value(tan(val)));
    return make_success(1);
}

EvalResult math_asin(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "asin() expects exactly 1 argument", 0, 0);
    }
    
    Value num_val = ctx_pop(state->main_context);
    if (num_val.type != VAL_FLOAT64 && num_val.type != VAL_INTEGER) {
        return make_error(state->main_context->current_env, "asin() expects a numeric argument", 0, 0);
    }
    
    double val = extract_number(num_val);
    if (val < -1.0 || val > 1.0) {
        return make_error(state->main_context->current_env, "asin() argument must be between -1 and 1", 0, 0);
    }
    ctx_push(state->main_context, make_float_value(asin(val)));
    return make_success(1);
}

EvalResult math_acos(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "acos() expects exactly 1 argument", 0, 0);
    }

    Value num_val = ctx_pop(state->main_context);
    if (num_val.type != VAL_FLOAT64 && num_val.type != VAL_INTEGER) {
        return make_error(state->main_context->current_env, "acos() expects a numeric argument", 0, 0);
    }
    
    double val = extract_number(num_val);
    if (val < -1.0 || val > 1.0) {
        return make_error(state->main_context->current_env, "acos() argument must be between -1 and 1", 0, 0);
    }
    ctx_push(state->main_context, make_float_value(acos(val)));
    return make_success(1);
}

EvalResult math_atan(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "atan() expects exactly 1 argument", 0, 0);
    }
    
    Value num_val = ctx_pop(state->main_context);
    if (num_val.type != VAL_FLOAT64 && num_val.type != VAL_INTEGER) {
        return make_error(state->main_context->current_env, "atan() expects a numeric argument", 0, 0);
    }
    
    
    double val = extract_number(num_val);
    ctx_push(state->main_context, make_float_value(atan(val)));
    return make_success(1);
}

EvalResult math_atan2(MobiusState* state, int arg_count) {
    if (arg_count != 2) {
        return make_error(state->main_context->current_env, "atan2() expects exactly 2 arguments", 0, 0);
    }

    Value y_val = ctx_pop(state->main_context);
    Value x_val = ctx_pop(state->main_context);
  
    
    if ((x_val.type != VAL_FLOAT64 && x_val.type != VAL_INTEGER) ||
        (y_val.type != VAL_FLOAT64 && y_val.type != VAL_INTEGER)) {
        return make_error(state->main_context->current_env, "atan2() expects numeric arguments", 0, 0);
    }
    
    double y = extract_number(x_val);
    double x = extract_number(y_val);
    ctx_push(state->main_context, make_float_value(atan2(y, x)));
    return make_success(1);
}

// ============================================================================
// LOGARITHMIC AND EXPONENTIAL FUNCTIONS
// ============================================================================

EvalResult math_log(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "log() expects exactly 1 argument", 0, 0);
    }
    
    Value num_val = ctx_pop(state->main_context);
    if (num_val.type != VAL_FLOAT64 && num_val.type != VAL_INTEGER) {
        return make_error(state->main_context->current_env, "log() expects a numeric argument", 0, 0);
    }
    
    double val = extract_number(num_val);
    if (val <= 0.0) {
        return make_error(state->main_context->current_env, "log() argument must be positive", 0, 0);
    }
    ctx_push(state->main_context, make_float_value(log(val)));
    return make_success(1);
}

EvalResult math_log10(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "log10() expects exactly 1 argument", 0, 0);
    }
    
    Value num_val = ctx_pop(state->main_context);
    if (num_val.type != VAL_FLOAT64 && num_val.type != VAL_INTEGER) {
        return make_error(state->main_context->current_env, "log10() expects a numeric argument", 0, 0);
    }
    
    double val = extract_number(num_val);
    if (val <= 0.0) {
        return make_error(state->main_context->current_env, "log10() argument must be positive", 0, 0);
    }
    ctx_push(state->main_context, make_float_value(log10(val)));
    return make_success(1);
}

EvalResult math_exp(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "exp() expects exactly 1 argument", 0, 0);
    }
    
    Value num_val = ctx_pop(state->main_context);
    if (num_val.type != VAL_FLOAT64 && num_val.type != VAL_INTEGER) {
        return make_error(state->main_context->current_env, "exp() expects a numeric argument", 0, 0);
    }
    
    double val = extract_number(num_val);
    ctx_push(state->main_context, make_float_value(exp(val)));
    return make_success(1);
}

// ============================================================================
// HYPERBOLIC FUNCTIONS
// ============================================================================

EvalResult math_sinh(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "sinh() expects exactly 1 argument", 0, 0);
    }
    
    Value num_val = ctx_pop(state->main_context);
    if (num_val.type != VAL_FLOAT64 && num_val.type != VAL_INTEGER) {
        return make_error(state->main_context->current_env, "sinh() expects a numeric argument", 0, 0);
    }
    
    double val = extract_number(num_val);
    ctx_push(state->main_context, make_float_value(sinh(val)));
    return make_success(1);
}

EvalResult math_cosh(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "cosh() expects exactly 1 argument", 0, 0);
    }
    
    Value num_val = ctx_pop(state->main_context);
    if (num_val.type != VAL_FLOAT64 && num_val.type != VAL_INTEGER) {
        return make_error(state->main_context->current_env, "cosh() expects a numeric argument", 0, 0);
    }
    
    double val = extract_number(num_val);
    ctx_push(state->main_context, make_float_value(cosh(val)));
    return make_success(1);
}

EvalResult math_tanh(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "tanh() expects exactly 1 argument", 0, 0);
    }
    
    Value num_val = ctx_pop(state->main_context);
    if (num_val.type != VAL_FLOAT64 && num_val.type != VAL_INTEGER) {
        return make_error(state->main_context->current_env, "tanh() expects a numeric argument", 0, 0);
    }
    
    double val = extract_number(num_val);
    ctx_push(state->main_context, make_float_value(tanh(val)));
    return make_success(1);
}

// ============================================================================
// ADVANCED MATHEMATICAL FUNCTIONS
// ============================================================================

EvalResult math_factorial(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "factorial() expects exactly 1 argument", 0, 0);
    }
    
    Value num_val = ctx_pop(state->main_context);
    if (num_val.type != VAL_INTEGER) {
        return make_error(state->main_context->current_env, "factorial() expects an integer argument", 0, 0);
    }
    
    // Extract value properly based on the actual type, but result should be int64_t
    int64_t value = extract_int64(num_val);

    if (value < 0) {
        return make_error(state->main_context->current_env, "factorial() argument must be non-negative", 0, 0);
    }
    if (value > 20) {
        return make_error(state->main_context->current_env, "factorial() argument too large (max 20)", 0, 0);
    }
    
    double result = 1.0;
    for (int64_t i = 2; i <= value; i++) {
        result *= (double)i;
    }
    
    ctx_push(state->main_context, make_float_value(result));
    return make_success(1);
}

EvalResult math_gcd(MobiusState* state, int arg_count) {
    if (arg_count != 2) {
        return make_error(state->main_context->current_env, "gcd() expects exactly 2 arguments", 0, 0);
    }
    
    Value b_val = ctx_pop(state->main_context);
    Value a_val = ctx_pop(state->main_context);

    if (a_val.type != VAL_INTEGER || b_val.type != VAL_INTEGER) {
        return make_error(state->main_context->current_env, "gcd() expects integer arguments", 0, 0);
    }
    
    int64_t a = extract_int64(a_val);
    int64_t b = extract_int64(b_val);
    
    while (b != 0) {
        int64_t temp = b;
        b = a % b;
        a = temp;
    }
    
    ctx_push(state->main_context, make_integer_value(NUM_INT32, (int32_t)a));
    return make_success(1);
}

EvalResult math_lcm(MobiusState* state, int arg_count) {
    if (arg_count != 2) {
        return make_error(state->main_context->current_env, "lcm() expects exactly 2 arguments", 0, 0);
    }

    Value b_val = ctx_pop(state->main_context);
    Value a_val = ctx_pop(state->main_context);
    
    if (a_val.type != VAL_INTEGER || b_val.type != VAL_INTEGER) {
        return make_error(state->main_context->current_env, "lcm() expects integer arguments", 0, 0);
    }
    
    int64_t a = extract_int64(a_val);
    int64_t b = extract_int64(b_val);
    
    if (a == 0 || b == 0) {
        ctx_push(state->main_context, make_integer_value(NUM_INT32, 0));
        return make_success(1);
    }
    
    // Calculate GCD first
    int64_t gcd_val = a;
    int64_t temp_b = b;
    while (temp_b != 0) {
        int64_t temp = temp_b;
        temp_b = gcd_val % temp_b;
        gcd_val = temp;
    }
    
    int64_t lcm_val = (a / gcd_val) * b;
    ctx_push(state->main_context, make_integer_value(NUM_INT32, (int32_t)lcm_val));
    return make_success(1);
}

// ============================================================================
// MATHEMATICAL CONSTANTS
// ============================================================================

EvalResult math_pi(MobiusState* state, int arg_count) {
    if (arg_count != 0) {
        return make_error(state->main_context->current_env, "pi() expects no arguments", 0, 0);
    }
    ctx_push(state->main_context, make_float_value(M_PI));
    return make_success(1);
}

EvalResult math_e(MobiusState* state, int arg_count) {
    if (arg_count != 0) {
        return make_error(state->main_context->current_env, "e() expects no arguments", 0, 0);
    }
    ctx_push(state->main_context, make_float_value(M_E));
    return make_success(1);
}

// ============================================================================
// TIER 1: ESSENTIAL FUNCTIONS
// ============================================================================

EvalResult math_sqrt(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "sqrt() expects exactly 1 argument", 0, 0);
    }
    
    Value num_val = ctx_pop(state->main_context);
    if (num_val.type != VAL_FLOAT64 && num_val.type != VAL_INTEGER) {
        return make_error(state->main_context->current_env, "sqrt() expects a numeric argument", 0, 0);
    }
    
    double val = extract_number(num_val);
    if (val < 0.0) {
        return make_error(state->main_context->current_env, "sqrt() argument must be non-negative", 0, 0);
    }
    ctx_push(state->main_context, make_float_value(sqrt(val)));
    return make_success(1);
}

EvalResult math_pow(MobiusState* state, int arg_count) {
    if (arg_count != 2) {
        return make_error(state->main_context->current_env, "pow() expects exactly 2 arguments", 0, 0);
    }
    
    Value exponent_val = ctx_pop(state->main_context);
    Value base_val = ctx_pop(state->main_context);
    
    if ((base_val.type != VAL_FLOAT64 && base_val.type != VAL_INTEGER) ||
        (exponent_val.type != VAL_FLOAT64 && exponent_val.type != VAL_INTEGER)) {
        return make_error(state->main_context->current_env, "pow() expects numeric arguments", 0, 0);
    }
    
    double base = extract_number(base_val);
    double exponent = extract_number(exponent_val);
    ctx_push(state->main_context, make_float_value(pow(base, exponent)));
    return make_success(1);
}

EvalResult math_abs(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "abs() expects exactly 1 argument", 0, 0);
    }
    
    Value num_val = ctx_pop(state->main_context);
    if (num_val.type != VAL_FLOAT64 && num_val.type != VAL_INTEGER) {
        return make_error(state->main_context->current_env, "abs() expects a numeric argument", 0, 0);
    }
    
    double val = extract_number(num_val);
    ctx_push(state->main_context, make_float_value(fabs(val)));
    return make_success(1);
}

EvalResult math_floor(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "floor() expects exactly 1 argument", 0, 0);
    }
    
    Value num_val = ctx_pop(state->main_context);
    if (num_val.type != VAL_FLOAT64 && num_val.type != VAL_INTEGER) {
        return make_error(state->main_context->current_env, "floor() expects a numeric argument", 0, 0);
    }
    
    double val = extract_number(num_val);
    ctx_push(state->main_context, make_float_value(floor(val)));
    return make_success(1);
}

EvalResult math_ceil(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "ceil() expects exactly 1 argument", 0, 0);
    }
    
    Value num_val = ctx_pop(state->main_context);
    if (num_val.type != VAL_FLOAT64 && num_val.type != VAL_INTEGER) {
        return make_error(state->main_context->current_env, "ceil() expects a numeric argument", 0, 0);
    }
    
    double val = extract_number(num_val);
    ctx_push(state->main_context, make_float_value(ceil(val)));
    return make_success(1);
}

EvalResult math_round(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "round() expects exactly 1 argument", 0, 0);
    }
    
    Value num_val = ctx_pop(state->main_context);
    if (num_val.type != VAL_FLOAT64 && num_val.type != VAL_INTEGER) {
        return make_error(state->main_context->current_env, "round() expects a numeric argument", 0, 0);
    }
    
    double val = extract_number(num_val);
    ctx_push(state->main_context, make_float_value(round(val)));
    return make_success(1);
}

EvalResult math_min(MobiusState* state, int arg_count) {
    if (arg_count < 2) {
        return make_error(state->main_context->current_env, "min() expects at least 2 arguments", 0, 0);
    }
    
    // Pop all arguments and find minimum
    double min_val = INFINITY;
    for (int i = 0; i < arg_count; i++) {
        Value val = ctx_pop(state->main_context);
        if (val.type != VAL_FLOAT64 && val.type != VAL_INTEGER) {
            return make_error(state->main_context->current_env, "min() expects numeric arguments", 0, 0);
        }
        double num = extract_number(val);
        if (num < min_val) {
            min_val = num;
        }
    }
    
    ctx_push(state->main_context, make_float_value(min_val));
    return make_success(1);
}

EvalResult math_max(MobiusState* state, int arg_count) {
    if (arg_count < 2) {
        return make_error(state->main_context->current_env, "max() expects at least 2 arguments", 0, 0);
    }
    
    // Pop all arguments and find maximum
    double max_val = -INFINITY;
    for (int i = 0; i < arg_count; i++) {
        Value val = ctx_pop(state->main_context);
        if (val.type != VAL_FLOAT64 && val.type != VAL_INTEGER) {
            return make_error(state->main_context->current_env, "max() expects numeric arguments", 0, 0);
        }
        double num = extract_number(val);
        if (num > max_val) {
            max_val = num;
        }
    }
    
    ctx_push(state->main_context, make_float_value(max_val));
    return make_success(1);
}

// ============================================================================
// TIER 2: VERY USEFUL FUNCTIONS
// ============================================================================

EvalResult math_random(MobiusState* state, int arg_count) {
    if (arg_count != 0) {
        return make_error(state->main_context->current_env, "random() expects no arguments", 0, 0);
    }
    
    // Generate random number between 0.0 and 1.0
    double r = (double)rand() / (double)RAND_MAX;
    ctx_push(state->main_context, make_float_value(r));
    return make_success(1);
}

EvalResult math_deg2rad(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "deg2rad() expects exactly 1 argument", 0, 0);
    }
    
    Value num_val = ctx_pop(state->main_context);
    if (num_val.type != VAL_FLOAT64 && num_val.type != VAL_INTEGER) {
        return make_error(state->main_context->current_env, "deg2rad() expects a numeric argument", 0, 0);
    }
    
    double degrees = extract_number(num_val);
    double radians = degrees * M_PI / 180.0;
    ctx_push(state->main_context, make_float_value(radians));
    return make_success(1);
}

EvalResult math_rad2deg(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "rad2deg() expects exactly 1 argument", 0, 0);
    }
    
    Value num_val = ctx_pop(state->main_context);
    if (num_val.type != VAL_FLOAT64 && num_val.type != VAL_INTEGER) {
        return make_error(state->main_context->current_env, "rad2deg() expects a numeric argument", 0, 0);
    }
    
    double radians = extract_number(num_val);
    double degrees = radians * 180.0 / M_PI;
    ctx_push(state->main_context, make_float_value(degrees));
    return make_success(1);
}

EvalResult math_sign(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "sign() expects exactly 1 argument", 0, 0);
    }
    
    Value num_val = ctx_pop(state->main_context);
    if (num_val.type != VAL_FLOAT64 && num_val.type != VAL_INTEGER) {
        return make_error(state->main_context->current_env, "sign() expects a numeric argument", 0, 0);
    }
    
    double val = extract_number(num_val);
    double sign = (val > 0.0) ? 1.0 : (val < 0.0) ? -1.0 : 0.0;
    ctx_push(state->main_context, make_float_value(sign));
    return make_success(1);
}

EvalResult math_clamp(MobiusState* state, int arg_count) {
    if (arg_count != 3) {
        return make_error(state->main_context->current_env, "clamp() expects exactly 3 arguments", 0, 0);
    }
    
    Value max_val = ctx_pop(state->main_context);
    Value min_val = ctx_pop(state->main_context);
    Value value_val = ctx_pop(state->main_context);
    
    if ((value_val.type != VAL_FLOAT64 && value_val.type != VAL_INTEGER) ||
        (min_val.type != VAL_FLOAT64 && min_val.type != VAL_INTEGER) ||
        (max_val.type != VAL_FLOAT64 && max_val.type != VAL_INTEGER)) {
        return make_error(state->main_context->current_env, "clamp() expects numeric arguments", 0, 0);
    }
    
    double value = extract_number(value_val);
    double min = extract_number(min_val);
    double max = extract_number(max_val);
    
    if (min > max) {
        return make_error(state->main_context->current_env, "clamp() min must be less than or equal to max", 0, 0);
    }
    
    double result = (value < min) ? min : (value > max) ? max : value;
    ctx_push(state->main_context, make_float_value(result));
    return make_success(1);
}

// ============================================================================
// PLUGIN DEFINITION
// ============================================================================

// Plugin initialization
int init_math_plugin(void) {
    return 0;  // Success
}

// Plugin cleanup
void cleanup_math_plugin(void) {
    // Nothing to clean up
}

// Plugin function definitions
static PluginFunction math_functions[] = {
    // Math functions
    {"sqrt", math_sqrt, 1},
    {"pow", math_pow, 2},
    {"abs", math_abs, 1},
    {"floor", math_floor, 1},
    {"ceil", math_ceil, 1},
    {"round", math_round, 1},
    {"min", math_min, -1},
    {"max", math_max, -1},
    
    // Useful functions
    {"random", math_random, 0},
    {"deg2rad", math_deg2rad, 1},
    {"rad2deg", math_rad2deg, 1},
    {"sign", math_sign, 1},
    {"clamp", math_clamp, 3},

    // Trigonometric functions
    {"sin", math_sin, 1},
    {"cos", math_cos, 1},
    {"tan", math_tan, 1},
    {"asin", math_asin, 1},
    {"acos", math_acos, 1},
    {"atan", math_atan, 1},
    {"atan2", math_atan2, 2},
    
    // Logarithmic and exponential functions
    {"log", math_log, 1},
    {"log10", math_log10, 1},
    {"exp", math_exp, 1},
    
    // Hyperbolic functions
    {"sinh", math_sinh, 1},
    {"cosh", math_cosh, 1},
    {"tanh", math_tanh, 1},

    // Factorial and GCD/LCM functions
    {"factorial", math_factorial, 1},
    {"gcd", math_gcd, 2},
    {"lcm", math_lcm, 2},

    // Mathematical constants
    {"pi", math_pi, 0},
    {"e", math_e, 0}
};

// Plugin instance
static Plugin math_plugin = {
    .metadata = {
        .name = "math",
        .version = "1.0.0",
        .description = "Extended Mathematical Functions",
        .author = "Mobius Team",
        .api_version = MOBIUS_PLUGIN_API_VERSION,
        .license = "MIT"
    },
    .functions = math_functions,
    .function_count = sizeof(math_functions) / sizeof(math_functions[0]),
    .init_plugin = init_math_plugin,
    .cleanup_plugin = cleanup_math_plugin,
    .get_help = NULL,
    .validate_env = NULL
};

// Required plugin entry point
MOBIUS_PLUGIN_EXPORT Plugin* mobius_plugin_info(void) {
    return &math_plugin;
}
