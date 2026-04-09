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
        return mobius_error(state, "sin() expects exactly 1 argument");
    }
    
    if (!mobius_stack_isNumber(state, -1)) {
        return mobius_error(state, "sin() expects a numeric argument");
    }
    
    double val = mobius_stack_asFloat64(state, -1);
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, sin(val));
    return 1;
}

int math_cos(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return mobius_error(state, "cos() expects exactly 1 argument");
    }
    
    if (!mobius_stack_isNumber(state, -1)) {
        return mobius_error(state, "cos() expects a numeric argument");
    }
    
    double val = mobius_stack_asFloat64(state, -1);
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, cos(val));
    return 1;
}

int math_tan(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return mobius_error(state, "tan() expects exactly 1 argument");
    }
    
    if (!mobius_stack_isNumber(state, -1)) {
        return mobius_error(state, "tan() expects a numeric argument");
    }
    
    double val = mobius_stack_asFloat64(state, -1);
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, tan(val));
    return 1;
}

int math_asin(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return mobius_error(state, "asin() expects exactly 1 argument");
    }
    
    if (!mobius_stack_isNumber(state, -1)) {
        return mobius_error(state, "asin() expects a numeric argument");
    }
    
    double val = mobius_stack_asFloat64(state, -1);
    if (val < -1.0 || val > 1.0) {
        return mobius_error(state, "asin() argument must be between -1 and 1");
    }
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, asin(val));
    return 1;
}

int math_acos(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return mobius_error(state, "acos() expects exactly 1 argument");
    }

    if (!mobius_stack_isNumber(state, -1)) {
        return mobius_error(state, "acos() expects a numeric argument");
    }
    
    double val = mobius_stack_asFloat64(state, -1);
    if (val < -1.0 || val > 1.0) {
        return mobius_error(state, "acos() argument must be between -1 and 1");
    }
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, acos(val));
    return 1;
}

int math_atan(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return mobius_error(state, "atan() expects exactly 1 argument");
    }
    
    if (!mobius_stack_isNumber(state, -1)) {
        return mobius_error(state, "atan() expects a numeric argument");
    }
    
    double val = mobius_stack_asFloat64(state, -1);
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, atan(val));
    return 1;
}

int math_atan2(MobiusState* state, int arg_count) {
    if (arg_count != 2) {
        return mobius_error(state, "atan2() expects exactly 2 arguments");
    }

    if (!mobius_stack_isNumber(state, -1) || !mobius_stack_isNumber(state, -2)) {
        return mobius_error(state, "atan2() expects numeric arguments");
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
        return mobius_error(state, "log() expects exactly 1 argument");
    }
    
    if (!mobius_stack_isNumber(state, -1)) {
        return mobius_error(state, "log() expects a numeric argument");
    }
    
    double val = mobius_stack_asFloat64(state, -1);
    if (val <= 0.0) {
        return mobius_error(state, "log() argument must be positive");
    }
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, log(val));
    return 1;
}

int math_log10(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return mobius_error(state, "log10() expects exactly 1 argument");
    }
    
    if (!mobius_stack_isNumber(state, -1)) {
        return mobius_error(state, "log10() expects a numeric argument");
    }
    
    double val = mobius_stack_asFloat64(state, -1);
    if (val <= 0.0) {
        return mobius_error(state, "log10() argument must be positive");
    }
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, log10(val));
    return 1;
}

int math_exp(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return mobius_error(state, "exp() expects exactly 1 argument");
    }
    
    if (!mobius_stack_isNumber(state, -1)) {
        return mobius_error(state, "exp() expects a numeric argument");
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
        return mobius_error(state, "sinh() expects exactly 1 argument");
    }
    
    if (!mobius_stack_isNumber(state, -1)) {
        return mobius_error(state, "sinh() expects a numeric argument");
    }
    
    double val = mobius_stack_asFloat64(state, -1);
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, sinh(val));
    return 1;
}

int math_cosh(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return mobius_error(state, "cosh() expects exactly 1 argument");
    }
    
    if (!mobius_stack_isNumber(state, -1)) {
        return mobius_error(state, "cosh() expects a numeric argument");
    }
    
    double val = mobius_stack_asFloat64(state, -1);
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, cosh(val));
    return 1;
}

int math_tanh(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return mobius_error(state, "tanh() expects exactly 1 argument");
    }
    
    if (!mobius_stack_isNumber(state, -1)) {
        return mobius_error(state, "tanh() expects a numeric argument");
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
        return mobius_error(state, "factorial() expects exactly 1 argument");
    }
    
    if (!mobius_stack_isInteger(state, -1)) {
        return mobius_error(state, "factorial() expects an integer argument");
    }
    
    int64_t value = mobius_stack_asInt64(state, -1);

    if (value < 0) {
        return mobius_error(state, "factorial() argument must be non-negative");
    }
    if (value > 20) {
        return mobius_error(state, "factorial() argument too large (max 20)");
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
        return mobius_error(state, "gcd() expects exactly 2 arguments");
    }
    
    if (!mobius_stack_isInteger(state, -1) || !mobius_stack_isInteger(state, -2)) {
        return mobius_error(state, "gcd() expects integer arguments");
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
        return mobius_error(state, "lcm() expects exactly 2 arguments");
    }

    if (!mobius_stack_isInteger(state, -1) || !mobius_stack_isInteger(state, -2)) {
        return mobius_error(state, "lcm() expects integer arguments");
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
// MATHEMATICAL CONSTANTS (added to module table via post_init)
// ============================================================================

#ifndef M_TAU
#define M_TAU (2.0 * M_PI)
#endif

int math_post_init(MobiusState* state) {
    // Module table is at stack index 0
    mobius_stack_pushFloat64(state, M_PI);
    mobius_stack_setTableField(state, 0, "pi");

    mobius_stack_pushFloat64(state, M_E);
    mobius_stack_setTableField(state, 0, "e");

    mobius_stack_pushFloat64(state, M_TAU);
    mobius_stack_setTableField(state, 0, "tau");

    return 0;
}

// ============================================================================
// INVERSE HYPERBOLIC FUNCTIONS
// ============================================================================

int math_asinh(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return mobius_error(state, "asinh() expects exactly 1 argument");
    }
    if (!mobius_stack_isNumber(state, -1)) {
        return mobius_error(state, "asinh() expects a numeric argument");
    }
    double val = mobius_stack_asFloat64(state, -1);
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, asinh(val));
    return 1;
}

int math_acosh(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return mobius_error(state, "acosh() expects exactly 1 argument");
    }
    if (!mobius_stack_isNumber(state, -1)) {
        return mobius_error(state, "acosh() expects a numeric argument");
    }
    double val = mobius_stack_asFloat64(state, -1);
    if (val < 1.0) {
        return mobius_error(state, "acosh() argument must be >= 1");
    }
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, acosh(val));
    return 1;
}

int math_atanh(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return mobius_error(state, "atanh() expects exactly 1 argument");
    }
    if (!mobius_stack_isNumber(state, -1)) {
        return mobius_error(state, "atanh() expects a numeric argument");
    }
    double val = mobius_stack_asFloat64(state, -1);
    if (val <= -1.0 || val >= 1.0) {
        return mobius_error(state, "atanh() argument must be between -1 and 1 (exclusive)");
    }
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, atanh(val));
    return 1;
}

// ============================================================================
// ADDITIONAL LOGARITHMIC/EXPONENTIAL FUNCTIONS
// ============================================================================

int math_log2(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return mobius_error(state, "log2() expects exactly 1 argument");
    }
    if (!mobius_stack_isNumber(state, -1)) {
        return mobius_error(state, "log2() expects a numeric argument");
    }
    double val = mobius_stack_asFloat64(state, -1);
    if (val <= 0.0) {
        return mobius_error(state, "log2() argument must be positive");
    }
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, log2(val));
    return 1;
}

int math_log1p(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return mobius_error(state, "log1p() expects exactly 1 argument");
    }
    if (!mobius_stack_isNumber(state, -1)) {
        return mobius_error(state, "log1p() expects a numeric argument");
    }
    double val = mobius_stack_asFloat64(state, -1);
    if (val <= -1.0) {
        return mobius_error(state, "log1p() argument must be > -1");
    }
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, log1p(val));
    return 1;
}

int math_expm1(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return mobius_error(state, "expm1() expects exactly 1 argument");
    }
    if (!mobius_stack_isNumber(state, -1)) {
        return mobius_error(state, "expm1() expects a numeric argument");
    }
    double val = mobius_stack_asFloat64(state, -1);
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, expm1(val));
    return 1;
}

int math_exp2(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return mobius_error(state, "exp2() expects exactly 1 argument");
    }
    if (!mobius_stack_isNumber(state, -1)) {
        return mobius_error(state, "exp2() expects a numeric argument");
    }
    double val = mobius_stack_asFloat64(state, -1);
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, exp2(val));
    return 1;
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

int math_deg2rad(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return mobius_error(state, "deg2rad() expects exactly 1 argument");
    }
    
    if (!mobius_stack_isNumber(state, -1)) {
        return mobius_error(state, "deg2rad() expects a numeric argument");
    }
    
    double degrees = mobius_stack_asFloat64(state, -1);
    double radians = degrees * M_PI / 180.0;
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, radians);
    return 1;
}

int math_rad2deg(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return mobius_error(state, "rad2deg() expects exactly 1 argument");
    }
    
    if (!mobius_stack_isNumber(state, -1)) {
        return mobius_error(state, "rad2deg() expects a numeric argument");
    }
    
    double radians = mobius_stack_asFloat64(state, -1);
    double degrees = radians * 180.0 / M_PI;
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, degrees);
    return 1;
}

int math_sign(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return mobius_error(state, "sign() expects exactly 1 argument");
    }
    
    if (!mobius_stack_isNumber(state, -1)) {
        return mobius_error(state, "sign() expects a numeric argument");
    }
    
    double val = mobius_stack_asFloat64(state, -1);
    int64_t sign = (val > 0.0) ? 1 : (val < 0.0) ? -1 : 0;
    mobius_stack_pop(state, 1);
    mobius_stack_pushInt64(state, sign);
    return 1;
}

int math_clamp(MobiusState* state, int arg_count) {
    if (arg_count != 3) {
        return mobius_error(state, "clamp() expects exactly 3 arguments");
    }
    
    if (!mobius_stack_isNumber(state, -1) || !mobius_stack_isNumber(state, -2) || !mobius_stack_isNumber(state, -3)) {
        return mobius_error(state, "clamp() expects numeric arguments");
    }
    
    double max = mobius_stack_asFloat64(state, -1);
    double min = mobius_stack_asFloat64(state, -2);
    double value = mobius_stack_asFloat64(state, -3);
    
    if (min > max) {
        return mobius_error(state, "clamp() min must be less than or equal to max");
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
int init_math_plugin(MobiusState* state) {
    (void)state;
    return 0;
}

// Plugin cleanup
void cleanup_math_plugin(void) {
    // Nothing to clean up
}

// Plugin function definitions
static MobiusPluginFunction math_functions[] = {
    // Trigonometric — all return float64
    {"sin",       math_sin,       1,  MOBIUS_VAL_FLOAT64, "Sine"},
    {"cos",       math_cos,       1,  MOBIUS_VAL_FLOAT64, "Cosine"},
    {"tan",       math_tan,       1,  MOBIUS_VAL_FLOAT64, "Tangent"},
    {"asin",      math_asin,      1,  MOBIUS_VAL_FLOAT64, "Arc sine"},
    {"acos",      math_acos,      1,  MOBIUS_VAL_FLOAT64, "Arc cosine"},
    {"atan",      math_atan,      1,  MOBIUS_VAL_FLOAT64, "Arc tangent"},
    {"atan2",     math_atan2,     2,  MOBIUS_VAL_FLOAT64, "Arc tangent of y/x"},
    // Hyperbolic
    {"sinh",      math_sinh,      1,  MOBIUS_VAL_FLOAT64, "Hyperbolic sine"},
    {"cosh",      math_cosh,      1,  MOBIUS_VAL_FLOAT64, "Hyperbolic cosine"},
    {"tanh",      math_tanh,      1,  MOBIUS_VAL_FLOAT64, "Hyperbolic tangent"},
    {"asinh",     math_asinh,     1,  MOBIUS_VAL_FLOAT64, "Inverse hyperbolic sine"},
    {"acosh",     math_acosh,     1,  MOBIUS_VAL_FLOAT64, "Inverse hyperbolic cosine"},
    {"atanh",     math_atanh,     1,  MOBIUS_VAL_FLOAT64, "Inverse hyperbolic tangent"},
    // Logarithmic / Exponential
    {"log",       math_log,       1,  MOBIUS_VAL_FLOAT64, "Natural logarithm"},
    {"log10",     math_log10,     1,  MOBIUS_VAL_FLOAT64, "Base-10 logarithm"},
    {"log2",      math_log2,      1,  MOBIUS_VAL_FLOAT64, "Base-2 logarithm"},
    {"log1p",     math_log1p,     1,  MOBIUS_VAL_FLOAT64, "Natural log of (1+x), accurate for small x"},
    {"exp",       math_exp,       1,  MOBIUS_VAL_FLOAT64, "Exponential (e^x)"},
    {"exp2",      math_exp2,      1,  MOBIUS_VAL_FLOAT64, "Base-2 exponential (2^x)"},
    {"expm1",     math_expm1,     1,  MOBIUS_VAL_FLOAT64, "e^x - 1, accurate for small x"},
    // Utility
    {"deg2rad",   math_deg2rad,   1,  MOBIUS_VAL_FLOAT64, "Degrees to radians"},
    {"rad2deg",   math_rad2deg,   1,  MOBIUS_VAL_FLOAT64, "Radians to degrees"},
    {"sign",      math_sign,      1,  MOBIUS_VAL_INT64,   "Sign of number (-1, 0, or 1)"},
    {"clamp",     math_clamp,     3,  MOBIUS_VAL_UNKNOWN, "Clamp value to range"},
    // Advanced
    {"factorial", math_factorial, 1,  MOBIUS_VAL_INT64,   "Factorial (n!)"},
    {"gcd",       math_gcd,       2,  MOBIUS_VAL_INT64,   "Greatest common divisor"},
    {"lcm",       math_lcm,       2,  MOBIUS_VAL_INT64,   "Least common multiple"},
};

static MobiusPlugin math_plugin = {
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
    .post_init = math_post_init,
};

extern "C" MOBIUS_PLUGIN_EXPORT MobiusPlugin* mobius_plugin_info(void) {
    return &math_plugin;
}
