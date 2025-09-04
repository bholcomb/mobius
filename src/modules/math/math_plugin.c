#include "../../mobius/plugin.h"
#include "../../mobius/ast.h"
#include "../../mobius/evaluator.h"
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

// Helper function to extract numeric value from a Value
static double extract_number(Value val) {
    switch (val.type) {
        case VAL_FLOAT:
            return val.as.float_val;
        case VAL_INTEGER:
            // Handle different integer types
            switch (val.as.integer.num_type) {
                case NUM_INT8:   return (double)val.as.integer.value.i8;
                case NUM_UINT8:  return (double)val.as.integer.value.u8;
                case NUM_INT16:  return (double)val.as.integer.value.i16;
                case NUM_UINT16: return (double)val.as.integer.value.u16;
                case NUM_INT32:  return (double)val.as.integer.value.i32;
                case NUM_UINT32: return (double)val.as.integer.value.u32;
                case NUM_INT64:  return (double)val.as.integer.value.i64;
                case NUM_UINT64: return (double)val.as.integer.value.u64;
                default: return 0.0;
            }
        default:
            return 0.0;
    }
}

// ============================================================================
// TRIGONOMETRIC FUNCTIONS
// ============================================================================

EvalResult math_sin(Value* args, size_t arg_count) {
    if (arg_count != 1) {
        return make_error("sin() expects exactly 1 argument", 0, 0);
    }
    
    if (args[0].type != VAL_FLOAT && args[0].type != VAL_INTEGER) {
        return make_error("sin() expects a numeric argument", 0, 0);
    }
    
    double val = extract_number(args[0]);
    return make_success(make_float_value(sin(val)));
}

EvalResult math_cos(Value* args, size_t arg_count) {
    if (arg_count != 1) {
        return make_error("cos() expects exactly 1 argument", 0, 0);
    }
    
    if (args[0].type != VAL_FLOAT && args[0].type != VAL_INTEGER) {
        return make_error("cos() expects a numeric argument", 0, 0);
    }
    
    double val = extract_number(args[0]);
    return make_success(make_float_value(cos(val)));
}

EvalResult math_tan(Value* args, size_t arg_count) {
    if (arg_count != 1) {
        return make_error("tan() expects exactly 1 argument", 0, 0);
    }
    
    if (args[0].type != VAL_FLOAT && args[0].type != VAL_INTEGER) {
        return make_error("tan() expects a numeric argument", 0, 0);
    }
    
    double val = extract_number(args[0]);
    return make_success(make_float_value(tan(val)));
}

EvalResult math_asin(Value* args, size_t arg_count) {
    if (arg_count != 1) {
        return make_error("asin() expects exactly 1 argument", 0, 0);
    }
    
    if (args[0].type != VAL_FLOAT && args[0].type != VAL_INTEGER) {
        return make_error("asin() expects a numeric argument", 0, 0);
    }
    
    double val = extract_number(args[0]);
    if (val < -1.0 || val > 1.0) {
        return make_error("asin() argument must be between -1 and 1", 0, 0);
    }
    return make_success(make_float_value(asin(val)));
}

EvalResult math_acos(Value* args, size_t arg_count) {
    if (arg_count != 1) {
        return make_error("acos() expects exactly 1 argument", 0, 0);
    }
    
    if (args[0].type != VAL_FLOAT && args[0].type != VAL_INTEGER) {
        return make_error("acos() expects a numeric argument", 0, 0);
    }
    
    double val = extract_number(args[0]);
    if (val < -1.0 || val > 1.0) {
        return make_error("acos() argument must be between -1 and 1", 0, 0);
    }
    return make_success(make_float_value(acos(val)));
}

EvalResult math_atan(Value* args, size_t arg_count) {
    if (arg_count != 1) {
        return make_error("atan() expects exactly 1 argument", 0, 0);
    }
    
    if (args[0].type != VAL_FLOAT && args[0].type != VAL_INTEGER) {
        return make_error("atan() expects a numeric argument", 0, 0);
    }
    
    double val = extract_number(args[0]);
    return make_success(make_float_value(atan(val)));
}

EvalResult math_atan2(Value* args, size_t arg_count) {
    if (arg_count != 2) {
        return make_error("atan2() expects exactly 2 arguments", 0, 0);
    }
    
    if ((args[0].type != VAL_FLOAT && args[0].type != VAL_INTEGER) ||
        (args[1].type != VAL_FLOAT && args[1].type != VAL_INTEGER)) {
        return make_error("atan2() expects numeric arguments", 0, 0);
    }
    
    double y = extract_number(args[0]);
    double x = extract_number(args[1]);
    return make_success(make_float_value(atan2(y, x)));
}

// ============================================================================
// LOGARITHMIC AND EXPONENTIAL FUNCTIONS
// ============================================================================

EvalResult math_log(Value* args, size_t arg_count) {
    if (arg_count != 1) {
        return make_error("log() expects exactly 1 argument", 0, 0);
    }
    
    if (args[0].type != VAL_FLOAT && args[0].type != VAL_INTEGER) {
        return make_error("log() expects a numeric argument", 0, 0);
    }
    
    double val = extract_number(args[0]);
    if (val <= 0.0) {
        return make_error("log() argument must be positive", 0, 0);
    }
    return make_success(make_float_value(log(val)));
}

EvalResult math_log10(Value* args, size_t arg_count) {
    if (arg_count != 1) {
        return make_error("log10() expects exactly 1 argument", 0, 0);
    }
    
    if (args[0].type != VAL_FLOAT && args[0].type != VAL_INTEGER) {
        return make_error("log10() expects a numeric argument", 0, 0);
    }
    
    double val = extract_number(args[0]);
    if (val <= 0.0) {
        return make_error("log10() argument must be positive", 0, 0);
    }
    return make_success(make_float_value(log10(val)));
}

EvalResult math_exp(Value* args, size_t arg_count) {
    if (arg_count != 1) {
        return make_error("exp() expects exactly 1 argument", 0, 0);
    }
    
    if (args[0].type != VAL_FLOAT && args[0].type != VAL_INTEGER) {
        return make_error("exp() expects a numeric argument", 0, 0);
    }
    
    double val = extract_number(args[0]);
    return make_success(make_float_value(exp(val)));
}

// ============================================================================
// HYPERBOLIC FUNCTIONS
// ============================================================================

EvalResult math_sinh(Value* args, size_t arg_count) {
    if (arg_count != 1) {
        return make_error("sinh() expects exactly 1 argument", 0, 0);
    }
    
    if (args[0].type != VAL_FLOAT && args[0].type != VAL_INTEGER) {
        return make_error("sinh() expects a numeric argument", 0, 0);
    }
    
    double val = extract_number(args[0]);
    return make_success(make_float_value(sinh(val)));
}

EvalResult math_cosh(Value* args, size_t arg_count) {
    if (arg_count != 1) {
        return make_error("cosh() expects exactly 1 argument", 0, 0);
    }
    
    if (args[0].type != VAL_FLOAT && args[0].type != VAL_INTEGER) {
        return make_error("cosh() expects a numeric argument", 0, 0);
    }
    
    double val = extract_number(args[0]);
    return make_success(make_float_value(cosh(val)));
}

EvalResult math_tanh(Value* args, size_t arg_count) {
    if (arg_count != 1) {
        return make_error("tanh() expects exactly 1 argument", 0, 0);
    }
    
    if (args[0].type != VAL_FLOAT && args[0].type != VAL_INTEGER) {
        return make_error("tanh() expects a numeric argument", 0, 0);
    }
    
    double val = extract_number(args[0]);
    return make_success(make_float_value(tanh(val)));
}

// ============================================================================
// ADVANCED MATHEMATICAL FUNCTIONS
// ============================================================================

EvalResult math_factorial(Value* args, size_t arg_count) {
    if (arg_count != 1) {
        return make_error("factorial() expects exactly 1 argument", 0, 0);
    }
    
    if (args[0].type != VAL_INTEGER) {
        return make_error("factorial() expects an integer argument", 0, 0);
    }
    
    int64_t n = args[0].as.integer.value.i32; // Simplified for demo
    if (n < 0) {
        return make_error("factorial() argument must be non-negative", 0, 0);
    }
    if (n > 20) {
        return make_error("factorial() argument too large (max 20)", 0, 0);
    }
    
    double result = 1.0;
    for (int64_t i = 2; i <= n; i++) {
        result *= (double)i;
    }
    
    return make_success(make_float_value(result));
}

EvalResult math_gcd(Value* args, size_t arg_count) {
    if (arg_count != 2) {
        return make_error("gcd() expects exactly 2 arguments", 0, 0);
    }
    
    if (args[0].type != VAL_INTEGER || args[1].type != VAL_INTEGER) {
        return make_error("gcd() expects integer arguments", 0, 0);
    }
    
    int64_t a = llabs(args[0].as.integer.value.i32);
    int64_t b = llabs(args[1].as.integer.value.i32);
    
    while (b != 0) {
        int64_t temp = b;
        b = a % b;
        a = temp;
    }
    
    return make_success(make_integer_value(NUM_INT32, (int32_t)a));
}

EvalResult math_lcm(Value* args, size_t arg_count) {
    if (arg_count != 2) {
        return make_error("lcm() expects exactly 2 arguments", 0, 0);
    }
    
    if (args[0].type != VAL_INTEGER || args[1].type != VAL_INTEGER) {
        return make_error("lcm() expects integer arguments", 0, 0);
    }
    
    int64_t a = llabs(args[0].as.integer.value.i32);
    int64_t b = llabs(args[1].as.integer.value.i32);
    
    if (a == 0 || b == 0) {
        return make_success(make_integer_value(NUM_INT32, 0));
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
    return make_success(make_integer_value(NUM_INT32, (int32_t)lcm_val));
}

// ============================================================================
// MATHEMATICAL CONSTANTS
// ============================================================================

EvalResult math_pi(Value* args, size_t arg_count) {
    if (arg_count != 0) {
        return make_error("pi() expects no arguments", 0, 0);
    }
    return make_success(make_float_value(M_PI));
}

EvalResult math_e(Value* args, size_t arg_count) {
    if (arg_count != 0) {
        return make_error("e() expects no arguments", 0, 0);
    }
    return make_success(make_float_value(M_E));
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
    
    // Advanced mathematical functions
    {"factorial", math_factorial, 1, "Factorial function", "combinatorial", "factorial(n)"},
    {"gcd", math_gcd, 2, "Greatest common divisor", "number_theory", "gcd(a, b)"},
    {"lcm", math_lcm, 2, "Least common multiple", "number_theory", "lcm(a, b)"},
    
    // Mathematical constants
    {"pi", math_pi, 0, "Pi constant", "constants", "pi()"},
    {"e", math_e, 0, "Euler's number", "constants", "e()"}
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
