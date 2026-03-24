#include "../../../src/mobius/plugin/plugin.h"
#include "../../../src/mobius/frontend/ast.h"
#include "../../../src/mobius/eval/evaluator.h"
#include "../../../src/mobius/state/environment.h"
#include "../../../src/mobius/state/mobius_state.h"
#include <mobius/mobius_plugin.h>
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

int math_sin(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("sin() expects exactly 1 argument");
    }
    
    if (!mobius_stack_isNumber(state, -1)) {
        return state->error("sin() expects a numeric argument");
    }
    
    double val = mobius_stack_asFloat64(state, -1);
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, sin(val));
    return 1;
}

int math_cos(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("cos() expects exactly 1 argument");
    }
    
    if (!mobius_stack_isNumber(state, -1)) {
        return state->error("cos() expects a numeric argument");
    }
    
    double val = mobius_stack_asFloat64(state, -1);
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, cos(val));
    return 1;
}

int math_tan(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("tan() expects exactly 1 argument");
    }
    
    if (!mobius_stack_isNumber(state, -1)) {
        return state->error("tan() expects a numeric argument");
    }
    
    double val = mobius_stack_asFloat64(state, -1);
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, tan(val));
    return 1;
}

int math_asin(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("asin() expects exactly 1 argument");
    }
    
    if (!mobius_stack_isNumber(state, -1)) {
        return state->error("asin() expects a numeric argument");
    }
    
    double val = mobius_stack_asFloat64(state, -1);
    if (val < -1.0 || val > 1.0) {
        return state->error("asin() argument must be between -1 and 1");
    }
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, asin(val));
    return 1;
}

int math_acos(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("acos() expects exactly 1 argument");
    }

    if (!mobius_stack_isNumber(state, -1)) {
        return state->error("acos() expects a numeric argument");
    }
    
    double val = mobius_stack_asFloat64(state, -1);
    if (val < -1.0 || val > 1.0) {
        return state->error("acos() argument must be between -1 and 1");
    }
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, acos(val));
    return 1;
}

int math_atan(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("atan() expects exactly 1 argument");
    }
    
    if (!mobius_stack_isNumber(state, -1)) {
        return state->error("atan() expects a numeric argument");
    }
    
    double val = mobius_stack_asFloat64(state, -1);
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, atan(val));
    return 1;
}

int math_atan2(MobiusState* state, int arg_count) {
    if (arg_count != 2) {
        return state->error("atan2() expects exactly 2 arguments");
    }

    if (!mobius_stack_isNumber(state, -1) || !mobius_stack_isNumber(state, -2)) {
        return state->error("atan2() expects numeric arguments");
    }
    
    double x = mobius_stack_asFloat64(state, -1);  // Second argument (x)
    double y = mobius_stack_asFloat64(state, -2);  // First argument (y)
    mobius_stack_pop(state, 2);
    mobius_stack_pushFloat64(state, atan2(y, x));
    return 1;
}

// ============================================================================
// LOGARITHMIC AND EXPONENTIAL FUNCTIONS
// ============================================================================

int math_log(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("log() expects exactly 1 argument");
    }
    
    if (!mobius_stack_isNumber(state, -1)) {
        return state->error("log() expects a numeric argument");
    }
    
    double val = mobius_stack_asFloat64(state, -1);
    if (val <= 0.0) {
        return state->error("log() argument must be positive");
    }
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, log(val));
    return 1;
}

int math_log10(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("log10() expects exactly 1 argument");
    }
    
    if (!mobius_stack_isNumber(state, -1)) {
        return state->error("log10() expects a numeric argument");
    }
    
    double val = mobius_stack_asFloat64(state, -1);
    if (val <= 0.0) {
        return state->error("log10() argument must be positive");
    }
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, log10(val));
    return 1;
}

int math_exp(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("exp() expects exactly 1 argument");
    }
    
    if (!mobius_stack_isNumber(state, -1)) {
        return state->error("exp() expects a numeric argument");
    }
    
    double val = mobius_stack_asFloat64(state, -1);
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, exp(val));
    return 1;
}

// ============================================================================
// HYPERBOLIC FUNCTIONS
// ============================================================================

int math_sinh(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("sinh() expects exactly 1 argument");
    }
    
    if (!mobius_stack_isNumber(state, -1)) {
        return state->error("sinh() expects a numeric argument");
    }
    
    double val = mobius_stack_asFloat64(state, -1);
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, sinh(val));
    return 1;
}

int math_cosh(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("cosh() expects exactly 1 argument");
    }
    
    if (!mobius_stack_isNumber(state, -1)) {
        return state->error("cosh() expects a numeric argument");
    }
    
    double val = mobius_stack_asFloat64(state, -1);
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, cosh(val));
    return 1;
}

int math_tanh(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("tanh() expects exactly 1 argument");
    }
    
    if (!mobius_stack_isNumber(state, -1)) {
        return state->error("tanh() expects a numeric argument");
    }
    
    double val = mobius_stack_asFloat64(state, -1);
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, tanh(val));
    return 1;
}

// ============================================================================
// ADVANCED MATHEMATICAL FUNCTIONS
// ============================================================================

int math_factorial(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("factorial() expects exactly 1 argument");
    }
    
    if (!mobius_stack_isInteger(state, -1)) {
        return state->error("factorial() expects an integer argument");
    }
    
    int64_t value = mobius_stack_asInt64(state, -1);

    if (value < 0) {
        return state->error("factorial() argument must be non-negative");
    }
    if (value > 20) {
        return state->error("factorial() argument too large (max 20)");
    }
    
    double result = 1.0;
    for (int64_t i = 2; i <= value; i++) {
        result *= (double)i;
    }
    
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, result);
    return 1;
}

int math_gcd(MobiusState* state, int arg_count) {
    if (arg_count != 2) {
        return state->error("gcd() expects exactly 2 arguments");
    }
    
    if (!mobius_stack_isInteger(state, -1) || !mobius_stack_isInteger(state, -2)) {
        return state->error("gcd() expects integer arguments");
    }
    
    int64_t b = mobius_stack_asInt64(state, -1);
    int64_t a = mobius_stack_asInt64(state, -2);
    
    while (b != 0) {
        int64_t temp = b;
        b = a % b;
        a = temp;
    }
    
    mobius_stack_pop(state, 2);
    mobius_stack_pushInt64(state, a);
    return 1;
}

int math_lcm(MobiusState* state, int arg_count) {
    if (arg_count != 2) {
        return state->error("lcm() expects exactly 2 arguments");
    }

    if (!mobius_stack_isInteger(state, -1) || !mobius_stack_isInteger(state, -2)) {
        return state->error("lcm() expects integer arguments");
    }
    
    int64_t b = mobius_stack_asInt64(state, -1);
    int64_t a = mobius_stack_asInt64(state, -2);
    
    if (a == 0 || b == 0) {
        mobius_stack_pop(state, 2);
        mobius_stack_pushInt64(state, 0);
        return 1;
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
    mobius_stack_pushInt64(state, lcm_val);
    return 1;
}

// ============================================================================
// MATHEMATICAL CONSTANTS
// ============================================================================

int math_pi(MobiusState* state, int arg_count) {
    if (arg_count != 0) {
        return state->error("pi() expects no arguments");
    }
    mobius_stack_pushFloat64(state, M_PI);
    return 1;
}

int math_e(MobiusState* state, int arg_count) {
    if (arg_count != 0) {
        return state->error("e() expects no arguments");
    }
    mobius_stack_pushFloat64(state, M_E);
    return 1;
}

// ============================================================================
// TIER 1: ESSENTIAL FUNCTIONS
// ============================================================================

int math_sqrt(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("sqrt() expects exactly 1 argument");
    }
    
    if (!mobius_stack_isNumber(state, -1)) {
        return state->error("sqrt() expects a numeric argument");
    }
    
    double val = mobius_stack_asFloat64(state, -1);
    if (val < 0.0) {
        return state->error("sqrt() argument must be non-negative");
    }
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, sqrt(val));
    return 1;
}

int math_pow(MobiusState* state, int arg_count) {
    if (arg_count != 2) {
        return state->error("pow() expects exactly 2 arguments");
    }
    
    if (!mobius_stack_isNumber(state, -1) || !mobius_stack_isNumber(state, -2)) {
        return state->error("pow() expects numeric arguments");
    }
    
    double exponent = mobius_stack_asFloat64(state, -1);
    double base = mobius_stack_asFloat64(state, -2);
    mobius_stack_pop(state, 2);
    mobius_stack_pushFloat64(state, pow(base, exponent));
    return 1;
}

int math_abs(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("abs() expects exactly 1 argument");
    }
    
    if (!mobius_stack_isNumber(state, -1)) {
        return state->error("abs() expects a numeric argument");
    }
    
    double val = mobius_stack_asFloat64(state, -1);
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, fabs(val));
    return 1;
}

int math_floor(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("floor() expects exactly 1 argument");
    }
    
    if (!mobius_stack_isNumber(state, -1)) {
        return state->error("floor() expects a numeric argument");
    }
    
    double val = mobius_stack_asFloat64(state, -1);
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, floor(val));
    return 1;
}

int math_ceil(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("ceil() expects exactly 1 argument");
    }
    
    if (!mobius_stack_isNumber(state, -1)) {
        return state->error("ceil() expects a numeric argument");
    }
    
    double val = mobius_stack_asFloat64(state, -1);
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, ceil(val));
    return 1;
}

int math_round(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("round() expects exactly 1 argument");
    }
    
    if (!mobius_stack_isNumber(state, -1)) {
        return state->error("round() expects a numeric argument");
    }
    
    double val = mobius_stack_asFloat64(state, -1);
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, round(val));
    return 1;
}

int math_min(MobiusState* state, int arg_count) {
    if (arg_count < 2) {
        return state->error("min() expects at least 2 arguments");
    }
    
    // Check all arguments are numeric first
    for (int i = 1; i <= arg_count; i++) {
        if (!mobius_stack_isNumber(state, -i)) {
            return state->error("min() expects numeric arguments");
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
    return 1;
}

int math_max(MobiusState* state, int arg_count) {
    if (arg_count < 2) {
        return state->error("max() expects at least 2 arguments");
    }
    
    // Check all arguments are numeric first
    for (int i = 1; i <= arg_count; i++) {
        if (!mobius_stack_isNumber(state, -i)) {
            return state->error("max() expects numeric arguments");
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
    return 1;
}

// ============================================================================
// TIER 2: VERY USEFUL FUNCTIONS
// ============================================================================

int math_random(MobiusState* state, int arg_count) {
    if (arg_count != 0) {
        return state->error("random() expects no arguments");
    }
    
    // Generate random number between 0.0 and 1.0
    double r = (double)rand() / (double)RAND_MAX;
    mobius_stack_pushFloat64(state, r);
    return 1;
}

int math_deg2rad(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("deg2rad() expects exactly 1 argument");
    }
    
    if (!mobius_stack_isNumber(state, -1)) {
        return state->error("deg2rad() expects a numeric argument");
    }
    
    double degrees = mobius_stack_asFloat64(state, -1);
    double radians = degrees * M_PI / 180.0;
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, radians);
    return 1;
}

int math_rad2deg(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("rad2deg() expects exactly 1 argument");
    }
    
    if (!mobius_stack_isNumber(state, -1)) {
        return state->error("rad2deg() expects a numeric argument");
    }
    
    double radians = mobius_stack_asFloat64(state, -1);
    double degrees = radians * 180.0 / M_PI;
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, degrees);
    return 1;
}

int math_sign(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("sign() expects exactly 1 argument");
    }
    
    if (!mobius_stack_isNumber(state, -1)) {
        return state->error("sign() expects a numeric argument");
    }
    
    double val = mobius_stack_asFloat64(state, -1);
    double sign = (val > 0.0) ? 1.0 : (val < 0.0) ? -1.0 : 0.0;
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, sign);
    return 1;
}

int math_clamp(MobiusState* state, int arg_count) {
    if (arg_count != 3) {
        return state->error("clamp() expects exactly 3 arguments");
    }
    
    if (!mobius_stack_isNumber(state, -1) || !mobius_stack_isNumber(state, -2) || !mobius_stack_isNumber(state, -3)) {
        return state->error("clamp() expects numeric arguments");
    }
    
    double max = mobius_stack_asFloat64(state, -1);
    double min = mobius_stack_asFloat64(state, -2);
    double value = mobius_stack_asFloat64(state, -3);
    
    if (min > max) {
        return state->error("clamp() min must be less than or equal to max");
    }
    
    double result = (value < min) ? min : (value > max) ? max : value;
    mobius_stack_pop(state, 3);
    mobius_stack_pushFloat64(state, result);
    return 1;
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
    {"min", math_min, SIZE_MAX},
    {"max", math_max, SIZE_MAX},
    
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
extern "C" MOBIUS_PLUGIN_EXPORT Plugin* mobius_plugin_info(void) {
    return &math_plugin;
}
