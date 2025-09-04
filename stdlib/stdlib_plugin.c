#include "../src/plugin.h"
#include "../src/stdlib.h"
#include <stdio.h>

// Plugin initialization function
int init_stdlib_plugin(void) {
    // Any initialization needed for stdlib
    return 0;
}

// Plugin cleanup function
void cleanup_stdlib_plugin(void) {
    // Any cleanup needed
}

// Plugin function definitions
static PluginFunction stdlib_functions[] = {
    // Core functions
    {"print", builtin_print, SIZE_MAX, "Print values to stdout", "core", "print(value1, value2, ...)"},
    {"typeof", builtin_typeof, 1, "Get the type of a value", "core", "typeof(value)"},
    {"str", builtin_str, 1, "Convert value to string", "core", "str(value)"},
    {"int", builtin_int, 1, "Convert value to integer", "core", "int(value)"},
    {"float", builtin_float, 1, "Convert value to float", "core", "float(value)"},
    
    // Math functions
    {"abs", builtin_abs, 1, "Absolute value", "math", "abs(number)"},
    {"min", builtin_min, SIZE_MAX, "Minimum of values", "math", "min(value1, value2, ...)"},
    {"max", builtin_max, SIZE_MAX, "Maximum of values", "math", "max(value1, value2, ...)"},
    {"pow", builtin_pow, 2, "Power function", "math", "pow(base, exponent)"},
    {"sqrt", builtin_sqrt, 1, "Square root", "math", "sqrt(number)"},
    {"floor", builtin_floor, 1, "Floor function", "math", "floor(number)"},
    {"ceil", builtin_ceil, 1, "Ceiling function", "math", "ceil(number)"},
    {"round", builtin_round, 1, "Round to nearest integer", "math", "round(number)"},
    
    // String functions
    {"len", builtin_len, 1, "String length", "string", "len(string)"},
    {"substr", builtin_substr, 3, "Extract substring", "string", "substr(string, start, length)"},
    {"concat", builtin_concat, SIZE_MAX, "Concatenate strings", "string", "concat(string1, string2, ...)"},
    {"upper", builtin_upper, 1, "Convert to uppercase", "string", "upper(string)"},
    {"lower", builtin_lower, 1, "Convert to lowercase", "string", "lower(string)"},
    {"contains", builtin_contains, 2, "Check if string contains substring", "string", "contains(haystack, needle)"},
    
    // Utility functions
    {"random", builtin_random, SIZE_MAX, "Generate random number", "utility", "random() or random(max) or random(min, max)"},
    {"time", builtin_time, 0, "Get current timestamp", "utility", "time()"},
    {"clock", builtin_clock, 0, "Get program execution time", "utility", "clock()"}
};

// Plugin instance
static Plugin stdlib_plugin = {
    .metadata = {
        .name = "stdlib",
        .version = "1.0.0",
        .description = "Mobius Standard Library",
        .author = "Mobius Team",
        .api_version = MOBIUS_PLUGIN_API_VERSION,
        .license = "MIT"
    },
    .functions = stdlib_functions,
    .function_count = sizeof(stdlib_functions) / sizeof(stdlib_functions[0]),
    .init_plugin = init_stdlib_plugin,
    .cleanup_plugin = cleanup_stdlib_plugin,
    .get_help = NULL,
    .validate_env = NULL
};

// Required plugin entry point
MOBIUS_PLUGIN_EXPORT Plugin* mobius_plugin_info(void) {
    return &stdlib_plugin;
}
