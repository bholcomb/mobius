#include "../../mobius/plugin.h"
#include "../../mobius/ast.h"
#include "../../mobius/evaluator.h"
#include "../../mobius/environment.h"
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

EvalResult math_sin(ExecutionContext* ctx, int arg_count) {
    // ctx is now passed as parameter
    if (arg_count != 1) {
        return make_error("sin() expects exactly 1 argument", 0, 0);
    }
    
    Value num_val = ctx_pop(ctx);
    if (num_val.type != VAL_FLOAT64 && num_val.type != VAL_INTEGER) {
        return make_error("sin() expects a numeric argument", 0, 0);
    }
    
    double val = extract_number(num_val);
    ctx_push(ctx, make_float_value(sin(val)));
    return make_success(1);
}

EvalResult math_cos(ExecutionContext* ctx, int arg_count) {
    if (arg_count != 1) {
        return make_error("cos() expects exactly 1 argument", 0, 0);
    }
    
    Value num_val = ctx_pop(ctx);
    if (num_val.type != VAL_FLOAT64 && num_val.type != VAL_INTEGER) {
        return make_error("cos() expects a numeric argument", 0, 0);
    }
    
    double val = extract_number(num_val);
    ctx_push(ctx, make_float_value(cos(val)));
    return make_success(1);
}

EvalResult math_tan(ExecutionContext* ctx, int arg_count) {
    if (arg_count != 1) {
        return make_error("tan() expects exactly 1 argument", 0, 0);
    }
    
    Value num_val = ctx_pop(ctx);
    if (num_val.type != VAL_FLOAT64 && num_val.type != VAL_INTEGER) {
        return make_error("tan() expects a numeric argument", 0, 0);
    }
    
    double val = extract_number(num_val);
    ctx_push(ctx, make_float_value(tan(val)));
    return make_success(1);
}

EvalResult math_asin(ExecutionContext* ctx, int arg_count) {
    if (arg_count != 1) {
        return make_error("asin() expects exactly 1 argument", 0, 0);
    }
    
    Value num_val = ctx_pop(ctx);
    if (num_val.type != VAL_FLOAT64 && num_val.type != VAL_INTEGER) {
        return make_error("asin() expects a numeric argument", 0, 0);
    }
    
    double val = extract_number(num_val);
    if (val < -1.0 || val > 1.0) {
        return make_error("asin() argument must be between -1 and 1", 0, 0);
    }
    ctx_push(ctx, make_float_value(asin(val)));
    return make_success(1);
}

EvalResult math_acos(ExecutionContext* ctx, int arg_count) {
    if (arg_count != 1) {
        return make_error("acos() expects exactly 1 argument", 0, 0);
    }

    Value num_val = ctx_pop(ctx);
    if (num_val.type != VAL_FLOAT64 && num_val.type != VAL_INTEGER) {
        return make_error("acos() expects a numeric argument", 0, 0);
    }
    
    double val = extract_number(num_val);
    if (val < -1.0 || val > 1.0) {
        return make_error("acos() argument must be between -1 and 1", 0, 0);
    }
    ctx_push(ctx, make_float_value(acos(val)));
    return make_success(1);
}

EvalResult math_atan(ExecutionContext* ctx, int arg_count) {
    if (arg_count != 1) {
        return make_error("atan() expects exactly 1 argument", 0, 0);
    }
    
    Value num_val = ctx_pop(ctx);
    if (num_val.type != VAL_FLOAT64 && num_val.type != VAL_INTEGER) {
        return make_error("atan() expects a numeric argument", 0, 0);
    }
    
    
    double val = extract_number(num_val);
    ctx_push(ctx, make_float_value(atan(val)));
    return make_success(1);
}

EvalResult math_atan2(ExecutionContext* ctx, int arg_count) {
    if (arg_count != 2) {
        return make_error("atan2() expects exactly 2 arguments", 0, 0);
    }

    Value y_val = ctx_pop(ctx);
    Value x_val = ctx_pop(ctx);
  
    
    if ((x_val.type != VAL_FLOAT64 && x_val.type != VAL_INTEGER) ||
        (y_val.type != VAL_FLOAT64 && y_val.type != VAL_INTEGER)) {
        return make_error("atan2() expects numeric arguments", 0, 0);
    }
    
    double y = extract_number(x_val);
    double x = extract_number(y_val);
    ctx_push(ctx, make_float_value(atan2(y, x)));
    return make_success(1);
}

// ============================================================================
// LOGARITHMIC AND EXPONENTIAL FUNCTIONS
// ============================================================================

EvalResult math_log(ExecutionContext* ctx, int arg_count) {
    if (arg_count != 1) {
        return make_error("log() expects exactly 1 argument", 0, 0);
    }
    
    Value num_val = ctx_pop(ctx);
    if (num_val.type != VAL_FLOAT64 && num_val.type != VAL_INTEGER) {
        return make_error("log() expects a numeric argument", 0, 0);
    }
    
    double val = extract_number(num_val);
    if (val <= 0.0) {
        return make_error("log() argument must be positive", 0, 0);
    }
    ctx_push(ctx, make_float_value(log(val)));
    return make_success(1);
}

EvalResult math_log10(ExecutionContext* ctx, int arg_count) {
    if (arg_count != 1) {
        return make_error("log10() expects exactly 1 argument", 0, 0);
    }
    
    Value num_val = ctx_pop(ctx);
    if (num_val.type != VAL_FLOAT64 && num_val.type != VAL_INTEGER) {
        return make_error("log10() expects a numeric argument", 0, 0);
    }
    
    double val = extract_number(num_val);
    if (val <= 0.0) {
        return make_error("log10() argument must be positive", 0, 0);
    }
    ctx_push(ctx, make_float_value(log10(val)));
    return make_success(1);
}

EvalResult math_exp(ExecutionContext* ctx, int arg_count) {
    if (arg_count != 1) {
        return make_error("exp() expects exactly 1 argument", 0, 0);
    }
    
    Value num_val = ctx_pop(ctx);
    if (num_val.type != VAL_FLOAT64 && num_val.type != VAL_INTEGER) {
        return make_error("exp() expects a numeric argument", 0, 0);
    }
    
    double val = extract_number(num_val);
    ctx_push(ctx, make_float_value(exp(val)));
    return make_success(1);
}

// ============================================================================
// HYPERBOLIC FUNCTIONS
// ============================================================================

EvalResult math_sinh(ExecutionContext* ctx, int arg_count) {
    if (arg_count != 1) {
        return make_error("sinh() expects exactly 1 argument", 0, 0);
    }
    
    Value num_val = ctx_pop(ctx);
    if (num_val.type != VAL_FLOAT64 && num_val.type != VAL_INTEGER) {
        return make_error("sinh() expects a numeric argument", 0, 0);
    }
    
    double val = extract_number(num_val);
    ctx_push(ctx, make_float_value(sinh(val)));
    return make_success(1);
}

EvalResult math_cosh(ExecutionContext* ctx, int arg_count) {
    if (arg_count != 1) {
        return make_error("cosh() expects exactly 1 argument", 0, 0);
    }
    
    Value num_val = ctx_pop(ctx);
    if (num_val.type != VAL_FLOAT64 && num_val.type != VAL_INTEGER) {
        return make_error("cosh() expects a numeric argument", 0, 0);
    }
    
    double val = extract_number(num_val);
    ctx_push(ctx, make_float_value(cosh(val)));
    return make_success(1);
}

EvalResult math_tanh(ExecutionContext* ctx, int arg_count) {
    if (arg_count != 1) {
        return make_error("tanh() expects exactly 1 argument", 0, 0);
    }
    
    Value num_val = ctx_pop(ctx);
    if (num_val.type != VAL_FLOAT64 && num_val.type != VAL_INTEGER) {
        return make_error("tanh() expects a numeric argument", 0, 0);
    }
    
    double val = extract_number(num_val);
    ctx_push(ctx, make_float_value(tanh(val)));
    return make_success(1);
}

// ============================================================================
// ADVANCED MATHEMATICAL FUNCTIONS
// ============================================================================

EvalResult math_factorial(ExecutionContext* ctx, int arg_count) {
    if (arg_count != 1) {
        return make_error("factorial() expects exactly 1 argument", 0, 0);
    }
    
    Value num_val = ctx_pop(ctx);
    if (num_val.type != VAL_INTEGER) {
        return make_error("factorial() expects an integer argument", 0, 0);
    }
    
    // Extract value properly based on the actual type, but result should be int64_t
    int64_t value = extract_int64(num_val);

    if (value < 0) {
        return make_error("factorial() argument must be non-negative", 0, 0);
    }
    if (value > 20) {
        return make_error("factorial() argument too large (max 20)", 0, 0);
    }
    
    double result = 1.0;
    for (int64_t i = 2; i <= value; i++) {
        result *= (double)i;
    }
    
    ctx_push(ctx, make_float_value(result));
    return make_success(1);
}

EvalResult math_gcd(ExecutionContext* ctx, int arg_count) {
    if (arg_count != 2) {
        return make_error("gcd() expects exactly 2 arguments", 0, 0);
    }
    
    Value b_val = ctx_pop(ctx);
    Value a_val = ctx_pop(ctx);

    if (a_val.type != VAL_INTEGER || b_val.type != VAL_INTEGER) {
        return make_error("gcd() expects integer arguments", 0, 0);
    }
    
    int64_t a = extract_int64(a_val);
    int64_t b = extract_int64(b_val);
    
    while (b != 0) {
        int64_t temp = b;
        b = a % b;
        a = temp;
    }
    
    ctx_push(ctx, make_integer_value(NUM_INT32, (int32_t)a));
    return make_success(1);
}

EvalResult math_lcm(ExecutionContext* ctx, int arg_count) {
    if (arg_count != 2) {
        return make_error("lcm() expects exactly 2 arguments", 0, 0);
    }

    Value b_val = ctx_pop(ctx);
    Value a_val = ctx_pop(ctx);
    
    if (a_val.type != VAL_INTEGER || b_val.type != VAL_INTEGER) {
        return make_error("lcm() expects integer arguments", 0, 0);
    }
    
    int64_t a = extract_int64(a_val);
    int64_t b = extract_int64(b_val);
    
    if (a == 0 || b == 0) {
        ctx_push(ctx, make_integer_value(NUM_INT32, 0));
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
    ctx_push(ctx, make_integer_value(NUM_INT32, (int32_t)lcm_val));
    return make_success(1);
}

// ============================================================================
// MATHEMATICAL CONSTANTS
// ============================================================================

EvalResult math_pi(ExecutionContext* ctx, int arg_count) {
    if (arg_count != 0) {
        return make_error("pi() expects no arguments", 0, 0);
    }
    ctx_push(ctx, make_float_value(M_PI));
    return make_success(1);
}

EvalResult math_e(ExecutionContext* ctx, int arg_count) {
    if (arg_count != 0) {
        return make_error("e() expects no arguments", 0, 0);
    }
    ctx_push(ctx, make_float_value(M_E));
    return make_success(1);
}

// ============================================================================
// TIER 1: ESSENTIAL FUNCTIONS
// ============================================================================

EvalResult math_sqrt(ExecutionContext* ctx, int arg_count) {
    if (arg_count != 1) {
        return make_error("sqrt() expects exactly 1 argument", 0, 0);
    }
    
    Value num_val = ctx_pop(ctx);
    if (num_val.type != VAL_FLOAT64 && num_val.type != VAL_INTEGER) {
        return make_error("sqrt() expects a numeric argument", 0, 0);
    }
    
    double val = extract_number(num_val);
    if (val < 0.0) {
        return make_error("sqrt() argument must be non-negative", 0, 0);
    }
    ctx_push(ctx, make_float_value(sqrt(val)));
    return make_success(1);
}

EvalResult math_pow(ExecutionContext* ctx, int arg_count) {
    if (arg_count != 2) {
        return make_error("pow() expects exactly 2 arguments", 0, 0);
    }
    
    Value exponent_val = ctx_pop(ctx);
    Value base_val = ctx_pop(ctx);
    
    if ((base_val.type != VAL_FLOAT64 && base_val.type != VAL_INTEGER) ||
        (exponent_val.type != VAL_FLOAT64 && exponent_val.type != VAL_INTEGER)) {
        return make_error("pow() expects numeric arguments", 0, 0);
    }
    
    double base = extract_number(base_val);
    double exponent = extract_number(exponent_val);
    ctx_push(ctx, make_float_value(pow(base, exponent)));
    return make_success(1);
}

EvalResult math_abs(ExecutionContext* ctx, int arg_count) {
    if (arg_count != 1) {
        return make_error("abs() expects exactly 1 argument", 0, 0);
    }
    
    Value num_val = ctx_pop(ctx);
    if (num_val.type != VAL_FLOAT64 && num_val.type != VAL_INTEGER) {
        return make_error("abs() expects a numeric argument", 0, 0);
    }
    
    double val = extract_number(num_val);
    ctx_push(ctx, make_float_value(fabs(val)));
    return make_success(1);
}

EvalResult math_floor(ExecutionContext* ctx, int arg_count) {
    if (arg_count != 1) {
        return make_error("floor() expects exactly 1 argument", 0, 0);
    }
    
    Value num_val = ctx_pop(ctx);
    if (num_val.type != VAL_FLOAT64 && num_val.type != VAL_INTEGER) {
        return make_error("floor() expects a numeric argument", 0, 0);
    }
    
    double val = extract_number(num_val);
    ctx_push(ctx, make_float_value(floor(val)));
    return make_success(1);
}

EvalResult math_ceil(ExecutionContext* ctx, int arg_count) {
    if (arg_count != 1) {
        return make_error("ceil() expects exactly 1 argument", 0, 0);
    }
    
    Value num_val = ctx_pop(ctx);
    if (num_val.type != VAL_FLOAT64 && num_val.type != VAL_INTEGER) {
        return make_error("ceil() expects a numeric argument", 0, 0);
    }
    
    double val = extract_number(num_val);
    ctx_push(ctx, make_float_value(ceil(val)));
    return make_success(1);
}

EvalResult math_round(ExecutionContext* ctx, int arg_count) {
    if (arg_count != 1) {
        return make_error("round() expects exactly 1 argument", 0, 0);
    }
    
    Value num_val = ctx_pop(ctx);
    if (num_val.type != VAL_FLOAT64 && num_val.type != VAL_INTEGER) {
        return make_error("round() expects a numeric argument", 0, 0);
    }
    
    double val = extract_number(num_val);
    ctx_push(ctx, make_float_value(round(val)));
    return make_success(1);
}

EvalResult math_min(ExecutionContext* ctx, int arg_count) {
    if (arg_count < 2) {
        return make_error("min() expects at least 2 arguments", 0, 0);
    }
    
    // Pop all arguments and find minimum
    double min_val = INFINITY;
    for (int i = 0; i < arg_count; i++) {
        Value val = ctx_pop(ctx);
        if (val.type != VAL_FLOAT64 && val.type != VAL_INTEGER) {
            return make_error("min() expects numeric arguments", 0, 0);
        }
        double num = extract_number(val);
        if (num < min_val) {
            min_val = num;
        }
    }
    
    ctx_push(ctx, make_float_value(min_val));
    return make_success(1);
}

EvalResult math_max(ExecutionContext* ctx, int arg_count) {
    if (arg_count < 2) {
        return make_error("max() expects at least 2 arguments", 0, 0);
    }
    
    // Pop all arguments and find maximum
    double max_val = -INFINITY;
    for (int i = 0; i < arg_count; i++) {
        Value val = ctx_pop(ctx);
        if (val.type != VAL_FLOAT64 && val.type != VAL_INTEGER) {
            return make_error("max() expects numeric arguments", 0, 0);
        }
        double num = extract_number(val);
        if (num > max_val) {
            max_val = num;
        }
    }
    
    ctx_push(ctx, make_float_value(max_val));
    return make_success(1);
}

// ============================================================================
// TIER 2: VERY USEFUL FUNCTIONS
// ============================================================================

EvalResult math_random(ExecutionContext* ctx, int arg_count) {
    if (arg_count != 0) {
        return make_error("random() expects no arguments", 0, 0);
    }
    
    // Generate random number between 0.0 and 1.0
    double r = (double)rand() / (double)RAND_MAX;
    ctx_push(ctx, make_float_value(r));
    return make_success(1);
}

EvalResult math_deg2rad(ExecutionContext* ctx, int arg_count) {
    if (arg_count != 1) {
        return make_error("deg2rad() expects exactly 1 argument", 0, 0);
    }
    
    Value num_val = ctx_pop(ctx);
    if (num_val.type != VAL_FLOAT64 && num_val.type != VAL_INTEGER) {
        return make_error("deg2rad() expects a numeric argument", 0, 0);
    }
    
    double degrees = extract_number(num_val);
    double radians = degrees * M_PI / 180.0;
    ctx_push(ctx, make_float_value(radians));
    return make_success(1);
}

EvalResult math_rad2deg(ExecutionContext* ctx, int arg_count) {
    if (arg_count != 1) {
        return make_error("rad2deg() expects exactly 1 argument", 0, 0);
    }
    
    Value num_val = ctx_pop(ctx);
    if (num_val.type != VAL_FLOAT64 && num_val.type != VAL_INTEGER) {
        return make_error("rad2deg() expects a numeric argument", 0, 0);
    }
    
    double radians = extract_number(num_val);
    double degrees = radians * 180.0 / M_PI;
    ctx_push(ctx, make_float_value(degrees));
    return make_success(1);
}

EvalResult math_sign(ExecutionContext* ctx, int arg_count) {
    if (arg_count != 1) {
        return make_error("sign() expects exactly 1 argument", 0, 0);
    }
    
    Value num_val = ctx_pop(ctx);
    if (num_val.type != VAL_FLOAT64 && num_val.type != VAL_INTEGER) {
        return make_error("sign() expects a numeric argument", 0, 0);
    }
    
    double val = extract_number(num_val);
    double sign = (val > 0.0) ? 1.0 : (val < 0.0) ? -1.0 : 0.0;
    ctx_push(ctx, make_float_value(sign));
    return make_success(1);
}

EvalResult math_clamp(ExecutionContext* ctx, int arg_count) {
    if (arg_count != 3) {
        return make_error("clamp() expects exactly 3 arguments", 0, 0);
    }
    
    Value max_val = ctx_pop(ctx);
    Value min_val = ctx_pop(ctx);
    Value value_val = ctx_pop(ctx);
    
    if ((value_val.type != VAL_FLOAT64 && value_val.type != VAL_INTEGER) ||
        (min_val.type != VAL_FLOAT64 && min_val.type != VAL_INTEGER) ||
        (max_val.type != VAL_FLOAT64 && max_val.type != VAL_INTEGER)) {
        return make_error("clamp() expects numeric arguments", 0, 0);
    }
    
    double value = extract_number(value_val);
    double min = extract_number(min_val);
    double max = extract_number(max_val);
    
    if (min > max) {
        return make_error("clamp() min must be less than or equal to max", 0, 0);
    }
    
    double result = (value < min) ? min : (value > max) ? max : value;
    ctx_push(ctx, make_float_value(result));
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
    {"sqrt", math_sqrt, 1, "Square root", "basic", "sqrt(value)"},
    {"pow", math_pow, 2, "Power function", "basic", "pow(base, exponent)"},
    {"abs", math_abs, 1, "Absolute value", "basic", "abs(value)"},
    {"floor", math_floor, 1, "Round down to integer", "rounding", "floor(value)"},
    {"ceil", math_ceil, 1, "Round up to integer", "rounding", "ceil(value)"},
    {"round", math_round, 1, "Round to nearest integer", "rounding", "round(value)"},
    {"min", math_min, -1, "Minimum of values", "comparison", "min(a, b, ...)"},
    {"max", math_max, -1, "Maximum of values", "comparison", "max(a, b, ...)"},
    
    // Useful functions
    {"random", math_random, 0, "Random number [0,1)", "random", "random()"},
    {"deg2rad", math_deg2rad, 1, "Degrees to radians", "conversion", "deg2rad(degrees)"},
    {"rad2deg", math_rad2deg, 1, "Radians to degrees", "conversion", "rad2deg(radians)"},
    {"sign", math_sign, 1, "Sign of number (-1, 0, 1)", "basic", "sign(value)"},
    {"clamp", math_clamp, 3, "Constrain value to range", "utility", "clamp(value, min, max)"},

    // Trigonometric functions
    {"sin", math_sin, 1, "Sine function", "trigonometry", "sin(radians)"},
    {"cos", math_cos, 1, "Cosine function", "trigonometry", "cos(radians)"},
    {"tan", math_tan, 1, "Tangent function", "trigonometry", "tan(radians)"},
    {"asin", math_asin, 1, "Arc sine function", "trigonometry", "asin(value)"},
    {"acos", math_acos, 1, "Arc cosine function", "trigonometry", "acos(value)"},
    {"atan", math_atan, 1, "Arc tangent function", "trigonometry", "atan(value)"},
    {"atan2", math_atan2, 2, "Two-argument arc tangent", "trigonometry", "atan2(y, x)"},
    
    // Logarithmic and exponential functions
    {"log", math_log, 1, "Natural logarithm", "logarithmic", "log(value)"},
    {"log10", math_log10, 1, "Base-10 logarithm", "logarithmic", "log10(value)"},
    {"exp", math_exp, 1, "Exponential function", "exponential", "exp(value)"},
    
    // Hyperbolic functions
    {"sinh", math_sinh, 1, "Hyperbolic sine", "hyperbolic", "sinh(value)"},
    {"cosh", math_cosh, 1, "Hyperbolic cosine", "hyperbolic", "cosh(value)"},
    {"tanh", math_tanh, 1, "Hyperbolic tangent", "hyperbolic", "tanh(value)"},

    // Factorial and GCD/LCM functions
    {"factorial", math_factorial, 1, "Factorial function", "basic", "factorial(n)"},
    {"gcd", math_gcd, 2, "Greatest common divisor", "basic", "gcd(a, b)"},
    {"lcm", math_lcm, 2, "Least common multiple", "basic", "lcm(a, b)"},
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
