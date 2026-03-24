#include "library/library.h"
#include "library/array.h"
#include "library/core.h"
#include "library/math.h"
#include "library/string.h"
#include "library/table_lib.h"
#include "library/types.h"
#include "library/util.h"
#include "state/mobius_state.h"

#include <stdio.h>
#include <string.h>

// =============================================================================
// UNIFIED LIBRARY REGISTRY
// =============================================================================

static const PluginFunction library_registry[] = {
    // Core functions
    {"print",   lib_print,  SIZE_MAX, "Print values to stdout"},
    {"typeof",  lib_typeof, 1,        "Return the type name of a value as a string"},
    {"str",     lib_str,    1,        "Convert a value to its string representation"},
    {"int",     lib_int,    1,        "Convert a value to an integer"},
    {"float",   lib_float,  1,        "Convert a value to a float"},

    // Math functions
    {"abs",   lib_abs,   1,        "Absolute value"},
    {"min",   lib_min,   2,        "Minimum of two or more values"},
    {"max",   lib_max,   2,        "Maximum of two or more values"},
    {"pow",   lib_pow,   2,        "Raise base to an exponent"},
    {"sqrt",  lib_sqrt,  1,        "Square root"},
    {"floor", lib_floor, 1,        "Round down to nearest integer"},
    {"ceil",  lib_ceil,  1,        "Round up to nearest integer"},
    {"round", lib_round, 1,        "Round to nearest integer"},

    // String functions
    {"len",      lib_len,      1,        "Length of a string or array"},
    {"upper",    lib_upper,    1,        "Convert string to uppercase"},
    {"lower",    lib_lower,    1,        "Convert string to lowercase"},
    {"substr",   lib_substr,   3,        "Extract a substring (string, start, length)"},
    {"concat",   lib_concat,   2,        "Concatenate two or more strings"},
    {"contains", lib_contains, 2,        "Return true if string contains a substring"},
    {"split",    lib_split,    2,        "Split string by delimiter into array"},
    {"join",     lib_join,     2,        "Join array elements with separator"},
    {"trim",     lib_trim,     1,        "Trim whitespace from both ends of string"},
    {"startswith", lib_startswith, 2,    "Return true if string starts with prefix"},
    {"endswith", lib_endswith, 2,        "Return true if string ends with suffix"},
    {"replace",  lib_replace,  3,        "Replace all occurrences of old with new"},
    {"find",     lib_find,     2,        "Find index of substring (-1 if not found)"},
    {"repeat",   lib_repeat,   2,        "Repeat string N times"},

    // Table functions
    {"table_remove",  lib_table_remove,  2, "Remove a key from a table"},
    {"table_has_key", lib_table_has_key, 2, "Return true if a key exists in a table"},
    {"table_size",    lib_table_size,    1, "Return the number of entries in a table"},
    {"setmetatable",  lib_setmetatable,  2, "Set the metatable for a table"},
    {"getmetatable",  lib_getmetatable,  1, "Get the metatable of a table"},
    {"pairs",         lib_pairs,         1, "Return an array of [key, value] pairs for a table"},

    // Array functions
    {"array_create",  lib_array_create,  0,        "Create a new empty array with optional capacity hint"},
    {"array_push",    lib_array_push,    2,        "Append a value to the end of an array"},
    {"array_pop",     lib_array_pop,     1,        "Remove and return the last element of an array"},
    {"array_get",     lib_array_get,     2,        "Get element at a zero-based index"},
    {"array_set",     lib_array_set,     3,        "Set element at a zero-based index (strict bounds)"},
    {"array_length",  lib_array_length,  1,        "Return the number of elements in an array"},
    {"array_slice",   lib_array_slice,   3,        "Return a sub-array from start (inclusive) to end (exclusive)"},
    {"array_concat",  lib_array_concat,  2,        "Return a new array combining two or more arrays"},
    {"array_reverse", lib_array_reverse, 1,        "Return a reversed copy of an array"},
    {"array_find",    lib_array_find,    2,        "Return the index of a value, or -1 if not found"},

    // Type system functions
    {"get_type_config", lib_get_type_config, 0, "Return the current type checking configuration"},

    // Utility functions
    {"random", lib_random, 0, "Random float in [0,1), or integer in [min,max] with 1 or 2 args"},
    {"time",   lib_time,   0, "Return the current Unix timestamp as an integer"},
    {"clock",  lib_clock,  0, "Return elapsed CPU time in seconds as a float"},
    {"load",   lib_load,   1, "Execute a Mobius script file by path"},
    {"id",     lib_id,     1, "Return the memory address of a heap-allocated value"},

    // Sentinel
    {NULL, NULL, 0, NULL}
};

// =============================================================================
// LIBRARY REGISTRATION
// =============================================================================

/**
 * Register all standard library functions in the global environment
 * @param state The MobiusState containing the global environment
 */
void register_stdlib_functions(MobiusState* state) {
    if (!state || !state->globalEnv()) return;
    
    StringInternPool* pool = state->stringPool();
    for (size_t i = 0; library_registry[i].name != NULL; i++) {
        const PluginFunction* func = &library_registry[i];
        
        Value func_value = make_native_function_value(func->function);
        
        int slot = state->assignGlobalSlot(func->name);
        func_value.flags |= VAL_FLAG_DEFINED;
        state->globalSlot(slot) = func_value;
        state->globalEnv()->define(pool->intern(func->name), func_value);
    }
}

/**
 * Get the library function registry (for inspection/tooling)
 * @return Pointer to the library function registry array
 */
const PluginFunction* get_library_registry(void) {
    return library_registry;
}

/**
 * Get the number of functions in the library registry
 * @return Number of registered library functions
 */
size_t get_library_function_count(void) {
    size_t count = 0;
    while (library_registry[count].name != NULL) {
        count++;
    }
    return count;
}
