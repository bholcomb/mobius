#include "../../../src/mobius/plugin/plugin.h"
#include "../../../src/mobius/frontend/ast.h"
#include "../../../src/mobius/eval/evaluator.h"
#include "../../../src/mobius/state/environment.h"
#include "../../../src/mobius/state/mobius_state.h"
#include "../../../src/mobius/state/stack.h"
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

// ============================================================================
// TRIGONOMETRIC FUNCTIONS
// ============================================================================

EvalResult math_sin(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "sin() expects exactly 1 argument", 0, 0);
    }
    
    if (!mobius_stack_isNumber(state, -1)) {
        return make_error(state->main_context->current_env, "sin() expects a numeric argument", 0, 0);
    }
    
    double val = mobius_stack_asFloat64(state, -1);
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, sin(val));
    return make_success(1);
}

EvalResult math_cos(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "cos() expects exactly 1 argument", 0, 0);
    }
    
    if (!mobius_stack_isNumber(state, -1)) {
        return make_error(state->main_context->current_env, "cos() expects a numeric argument", 0, 0);
    }
    
    double val = mobius_stack_asFloat64(state, -1);
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, cos(val));
    return make_success(1);
}

EvalResult math_tan(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "tan() expects exactly 1 argument", 0, 0);
    }
    
    if (!mobius_stack_isNumber(state, -1)) {
        return make_error(state->main_context->current_env, "tan() expects a numeric argument", 0, 0);
    }
    
    double val = mobius_stack_asFloat64(state, -1);
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, tan(val));
    return make_success(1);
}

EvalResult math_asin(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "asin() expects exactly 1 argument", 0, 0);
    }
    
    if (!mobius_stack_isNumber(state, -1)) {
        return make_error(state->main_context->current_env, "asin() expects a numeric argument", 0, 0);
    }
    
    double val = mobius_stack_asFloat64(state, -1);
    if (val < -1.0 || val > 1.0) {
        return make_error(state->main_context->current_env, "asin() argument must be between -1 and 1", 0, 0);
    }
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, asin(val));
    return make_success(1);
}

EvalResult math_acos(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "acos() expects exactly 1 argument", 0, 0);
    }

    if (!mobius_stack_isNumber(state, -1)) {
        return make_error(state->main_context->current_env, "acos() expects a numeric argument", 0, 0);
    }
    
    double val = mobius_stack_asFloat64(state, -1);
    if (val < -1.0 || val > 1.0) {
        return make_error(state->main_context->current_env, "acos() argument must be between -1 and 1", 0, 0);
    }
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, acos(val));
    return make_success(1);
}

EvalResult math_atan(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "atan() expects exactly 1 argument", 0, 0);
    }
    
    if (!mobius_stack_isNumber(state, -1)) {
        return make_error(state->main_context->current_env, "atan() expects a numeric argument", 0, 0);
    }
    
    double val = mobius_stack_asFloat64(state, -1);
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, atan(val));
    return make_success(1);
}

EvalResult math_atan2(MobiusState* state, int arg_count) {
    if (arg_count != 2) {
        return make_error(state->main_context->current_env, "atan2() expects exactly 2 arguments", 0, 0);
    }

    if (!mobius_stack_isNumber(state, -1) || !mobius_stack_isNumber(state, -2)) {
        return make_error(state->main_context->current_env, "atan2() expects numeric arguments", 0, 0);
    }
    
    double x = mobius_stack_asFloat64(state, -1);  // Second argument (x)
    double y = mobius_stack_asFloat64(state, -2);  // First argument (y)
    mobius_stack_pop(state, 2);
    mobius_stack_pushFloat64(state, atan2(y, x));
    return make_success(1);
}

// ============================================================================
// LOGARITHMIC AND EXPONENTIAL FUNCTIONS
// ============================================================================

EvalResult math_log(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "log() expects exactly 1 argument", 0, 0);
    }
    
    if (!mobius_stack_isNumber(state, -1)) {
        return make_error(state->main_context->current_env, "log() expects a numeric argument", 0, 0);
    }
    
    double val = mobius_stack_asFloat64(state, -1);
    if (val <= 0.0) {
        return make_error(state->main_context->current_env, "log() argument must be positive", 0, 0);
    }
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, log(val));
    return make_success(1);
}

EvalResult math_log10(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "log10() expects exactly 1 argument", 0, 0);
    }
    
    if (!mobius_stack_isNumber(state, -1)) {
        return make_error(state->main_context->current_env, "log10() expects a numeric argument", 0, 0);
    }
    
    double val = mobius_stack_asFloat64(state, -1);
    if (val <= 0.0) {
        return make_error(state->main_context->current_env, "log10() argument must be positive", 0, 0);
    }
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, log10(val));
    return make_success(1);
}

EvalResult math_exp(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "exp() expects exactly 1 argument", 0, 0);
    }
    
    if (!mobius_stack_isNumber(state, -1)) {
        return make_error(state->main_context->current_env, "exp() expects a numeric argument", 0, 0);
    }
    
    double val = mobius_stack_asFloat64(state, -1);
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, exp(val));
    return make_success(1);
}

// ============================================================================
// HYPERBOLIC FUNCTIONS
// ============================================================================

EvalResult math_sinh(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "sinh() expects exactly 1 argument", 0, 0);
    }
    
    if (!mobius_stack_isNumber(state, -1)) {
        return make_error(state->main_context->current_env, "sinh() expects a numeric argument", 0, 0);
    }
    
    double val = mobius_stack_asFloat64(state, -1);
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, sinh(val));
    return make_success(1);
}

EvalResult math_cosh(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "cosh() expects exactly 1 argument", 0, 0);
    }
    
    if (!mobius_stack_isNumber(state, -1)) {
        return make_error(state->main_context->current_env, "cosh() expects a numeric argument", 0, 0);
    }
    
    double val = mobius_stack_asFloat64(state, -1);
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, cosh(val));
    return make_success(1);
}

EvalResult math_tanh(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "tanh() expects exactly 1 argument", 0, 0);
    }
    
    if (!mobius_stack_isNumber(state, -1)) {
        return make_error(state->main_context->current_env, "tanh() expects a numeric argument", 0, 0);
    }
    
    double val = mobius_stack_asFloat64(state, -1);
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, tanh(val));
    return make_success(1);
}

// ============================================================================
// ADVANCED MATHEMATICAL FUNCTIONS
// ============================================================================

EvalResult math_factorial(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "factorial() expects exactly 1 argument", 0, 0);
    }
    
    if (!mobius_stack_isInteger(state, -1)) {
        return make_error(state->main_context->current_env, "factorial() expects an integer argument", 0, 0);
    }
    
    int64_t value = mobius_stack_asInt64(state, -1);

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
    
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, result);
    return make_success(1);
}

EvalResult math_gcd(MobiusState* state, int arg_count) {
    if (arg_count != 2) {
        return make_error(state->main_context->current_env, "gcd() expects exactly 2 arguments", 0, 0);
    }
    
    if (!mobius_stack_isInteger(state, -1) || !mobius_stack_isInteger(state, -2)) {
        return make_error(state->main_context->current_env, "gcd() expects integer arguments", 0, 0);
    }
    
    int64_t b = mobius_stack_asInt64(state, -1);
    int64_t a = mobius_stack_asInt64(state, -2);
    
    while (b != 0) {
        int64_t temp = b;
        b = a % b;
        a = temp;
    }
    
    mobius_stack_pop(state, 2);
    mobius_stack_pushInt32(state, (int32_t)a);
    return make_success(1);
}

EvalResult math_lcm(MobiusState* state, int arg_count) {
    if (arg_count != 2) {
        return make_error(state->main_context->current_env, "lcm() expects exactly 2 arguments", 0, 0);
    }

    if (!mobius_stack_isInteger(state, -1) || !mobius_stack_isInteger(state, -2)) {
        return make_error(state->main_context->current_env, "lcm() expects integer arguments", 0, 0);
    }
    
    int64_t b = mobius_stack_asInt64(state, -1);
    int64_t a = mobius_stack_asInt64(state, -2);
    
    if (a == 0 || b == 0) {
        mobius_stack_pop(state, 2);
        mobius_stack_pushInt32(state, 0);
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
    mobius_stack_pop(state, 2);
    mobius_stack_pushInt32(state, (int32_t)lcm_val);
    return make_success(1);
}

// ============================================================================
// MATHEMATICAL CONSTANTS
// ============================================================================

EvalResult math_pi(MobiusState* state, int arg_count) {
    if (arg_count != 0) {
        return make_error(state->main_context->current_env, "pi() expects no arguments", 0, 0);
    }
    mobius_stack_pushFloat64(state, M_PI);
    return make_success(1);
}

EvalResult math_e(MobiusState* state, int arg_count) {
    if (arg_count != 0) {
        return make_error(state->main_context->current_env, "e() expects no arguments", 0, 0);
    }
    mobius_stack_pushFloat64(state, M_E);
    return make_success(1);
}

// ============================================================================
// TIER 1: ESSENTIAL FUNCTIONS
// ============================================================================

EvalResult math_sqrt(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "sqrt() expects exactly 1 argument", 0, 0);
    }
    
    if (!mobius_stack_isNumber(state, -1)) {
        return make_error(state->main_context->current_env, "sqrt() expects a numeric argument", 0, 0);
    }
    
    double val = mobius_stack_asFloat64(state, -1);
    if (val < 0.0) {
        return make_error(state->main_context->current_env, "sqrt() argument must be non-negative", 0, 0);
    }
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, sqrt(val));
    return make_success(1);
}

EvalResult math_pow(MobiusState* state, int arg_count) {
    if (arg_count != 2) {
        return make_error(state->main_context->current_env, "pow() expects exactly 2 arguments", 0, 0);
    }
    
    if (!mobius_stack_isNumber(state, -1) || !mobius_stack_isNumber(state, -2)) {
        return make_error(state->main_context->current_env, "pow() expects numeric arguments", 0, 0);
    }
    
    double exponent = mobius_stack_asFloat64(state, -1);
    double base = mobius_stack_asFloat64(state, -2);
    mobius_stack_pop(state, 2);
    mobius_stack_pushFloat64(state, pow(base, exponent));
    return make_success(1);
}

EvalResult math_abs(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "abs() expects exactly 1 argument", 0, 0);
    }
    
    if (!mobius_stack_isNumber(state, -1)) {
        return make_error(state->main_context->current_env, "abs() expects a numeric argument", 0, 0);
    }
    
    double val = mobius_stack_asFloat64(state, -1);
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, fabs(val));
    return make_success(1);
}

EvalResult math_floor(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "floor() expects exactly 1 argument", 0, 0);
    }
    
    if (!mobius_stack_isNumber(state, -1)) {
        return make_error(state->main_context->current_env, "floor() expects a numeric argument", 0, 0);
    }
    
    double val = mobius_stack_asFloat64(state, -1);
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, floor(val));
    return make_success(1);
}

EvalResult math_ceil(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "ceil() expects exactly 1 argument", 0, 0);
    }
    
    if (!mobius_stack_isNumber(state, -1)) {
        return make_error(state->main_context->current_env, "ceil() expects a numeric argument", 0, 0);
    }
    
    double val = mobius_stack_asFloat64(state, -1);
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, ceil(val));
    return make_success(1);
}

EvalResult math_round(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "round() expects exactly 1 argument", 0, 0);
    }
    
    if (!mobius_stack_isNumber(state, -1)) {
        return make_error(state->main_context->current_env, "round() expects a numeric argument", 0, 0);
    }
    
    double val = mobius_stack_asFloat64(state, -1);
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, round(val));
    return make_success(1);
}

EvalResult math_min(MobiusState* state, int arg_count) {
    if (arg_count < 2) {
        return make_error(state->main_context->current_env, "min() expects at least 2 arguments", 0, 0);
    }
    
    // Check all arguments are numeric first
    for (int i = 1; i <= arg_count; i++) {
        if (!mobius_stack_isNumber(state, -i)) {
            return make_error(state->main_context->current_env, "min() expects numeric arguments", 0, 0);
        }
    }
    
    // Find minimum
    double min_val = INFINITY;
    for (int i = 1; i <= arg_count; i++) {
        double num = mobius_stack_asFloat64(state, -i);
        if (num < min_val) {
            min_val = num;
        }
    }
    
    mobius_stack_pop(state, arg_count);
    mobius_stack_pushFloat64(state, min_val);
    return make_success(1);
}

EvalResult math_max(MobiusState* state, int arg_count) {
    if (arg_count < 2) {
        return make_error(state->main_context->current_env, "max() expects at least 2 arguments", 0, 0);
    }
    
    // Check all arguments are numeric first
    for (int i = 1; i <= arg_count; i++) {
        if (!mobius_stack_isNumber(state, -i)) {
            return make_error(state->main_context->current_env, "max() expects numeric arguments", 0, 0);
        }
    }
    
    // Find maximum
    double max_val = -INFINITY;
    for (int i = 1; i <= arg_count; i++) {
        double num = mobius_stack_asFloat64(state, -i);
        if (num > max_val) {
            max_val = num;
        }
    }
    
    mobius_stack_pop(state, arg_count);
    mobius_stack_pushFloat64(state, max_val);
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
    mobius_stack_pushFloat64(state, r);
    return make_success(1);
}

EvalResult math_deg2rad(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "deg2rad() expects exactly 1 argument", 0, 0);
    }
    
    if (!mobius_stack_isNumber(state, -1)) {
        return make_error(state->main_context->current_env, "deg2rad() expects a numeric argument", 0, 0);
    }
    
    double degrees = mobius_stack_asFloat64(state, -1);
    double radians = degrees * M_PI / 180.0;
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, radians);
    return make_success(1);
}

EvalResult math_rad2deg(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "rad2deg() expects exactly 1 argument", 0, 0);
    }
    
    if (!mobius_stack_isNumber(state, -1)) {
        return make_error(state->main_context->current_env, "rad2deg() expects a numeric argument", 0, 0);
    }
    
    double radians = mobius_stack_asFloat64(state, -1);
    double degrees = radians * 180.0 / M_PI;
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, degrees);
    return make_success(1);
}

EvalResult math_sign(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "sign() expects exactly 1 argument", 0, 0);
    }
    
    if (!mobius_stack_isNumber(state, -1)) {
        return make_error(state->main_context->current_env, "sign() expects a numeric argument", 0, 0);
    }
    
    double val = mobius_stack_asFloat64(state, -1);
    double sign = (val > 0.0) ? 1.0 : (val < 0.0) ? -1.0 : 0.0;
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, sign);
    return make_success(1);
}

EvalResult math_clamp(MobiusState* state, int arg_count) {
    if (arg_count != 3) {
        return make_error(state->main_context->current_env, "clamp() expects exactly 3 arguments", 0, 0);
    }
    
    if (!mobius_stack_isNumber(state, -1) || !mobius_stack_isNumber(state, -2) || !mobius_stack_isNumber(state, -3)) {
        return make_error(state->main_context->current_env, "clamp() expects numeric arguments", 0, 0);
    }
    
    double max = mobius_stack_asFloat64(state, -1);
    double min = mobius_stack_asFloat64(state, -2);
    double value = mobius_stack_asFloat64(state, -3);
    
    if (min > max) {
        return make_error(state->main_context->current_env, "clamp() min must be less than or equal to max", 0, 0);
    }
    
    double result = (value < min) ? min : (value > max) ? max : value;
    mobius_stack_pop(state, 3);
    mobius_stack_pushFloat64(state, result);
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
