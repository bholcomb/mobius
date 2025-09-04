#include "../src/plugin.h"
#include <math.h>

// Extended math functions for demonstration

EvalResult builtin_sin(Value* args, size_t arg_count) {
    if (arg_count != 1) {
        return make_error("sin() expects exactly 1 argument", 0, 0);
    }
    
    Value arg = args[0];
    double val = 0.0;
    
    if (arg.type == VAL_FLOAT) {
        val = arg.as.float_val;
    } else if (arg.type == VAL_INTEGER) {
        val = (double)arg.as.integer.value.i32;  // Simplified for demo
    } else {
        return make_error("sin() expects a numeric argument", 0, 0);
    }
    
    return make_success(make_float_value(sin(val)));
}

EvalResult builtin_cos(Value* args, size_t arg_count) {
    if (arg_count != 1) {
        return make_error("cos() expects exactly 1 argument", 0, 0);
    }
    
    Value arg = args[0];
    double val = 0.0;
    
    if (arg.type == VAL_FLOAT) {
        val = arg.as.float_val;
    } else if (arg.type == VAL_INTEGER) {
        val = (double)arg.as.integer.value.i32;  // Simplified for demo
    } else {
        return make_error("cos() expects a numeric argument", 0, 0);
    }
    
    return make_success(make_float_value(cos(val)));
}

EvalResult builtin_tan(Value* args, size_t arg_count) {
    if (arg_count != 1) {
        return make_error("tan() expects exactly 1 argument", 0, 0);
    }
    
    Value arg = args[0];
    double val = 0.0;
    
    if (arg.type == VAL_FLOAT) {
        val = arg.as.float_val;
    } else if (arg.type == VAL_INTEGER) {
        val = (double)arg.as.integer.value.i32;  // Simplified for demo
    } else {
        return make_error("tan() expects a numeric argument", 0, 0);
    }
    
    return make_success(make_float_value(tan(val)));
}

// Plugin initialization
int init_mathlib_plugin(void) {
    return 0;  // Success
}

// Plugin cleanup
void cleanup_mathlib_plugin(void) {
    // Nothing to clean up
}

// Plugin function definitions
static PluginFunction mathlib_functions[] = {
    {"sin", builtin_sin, 1, "Sine function", "trigonometry", "sin(radians)"},
    {"cos", builtin_cos, 1, "Cosine function", "trigonometry", "cos(radians)"},
    {"tan", builtin_tan, 1, "Tangent function", "trigonometry", "tan(radians)"}
};

// Plugin instance
static Plugin mathlib_plugin = {
    .metadata = {
        .name = "mathlib",
        .version = "1.0.0",
        .description = "Extended Mathematical Functions",
        .author = "Mobius Team",
        .api_version = MOBIUS_PLUGIN_API_VERSION,
        .license = "MIT"
    },
    .functions = mathlib_functions,
    .function_count = sizeof(mathlib_functions) / sizeof(mathlib_functions[0]),
    .init_plugin = init_mathlib_plugin,
    .cleanup_plugin = cleanup_mathlib_plugin,
    .get_help = NULL,
    .validate_env = NULL
};

// Required plugin entry point
MOBIUS_PLUGIN_EXPORT Plugin* mobius_plugin_info(void) {
    return &mathlib_plugin;
}
