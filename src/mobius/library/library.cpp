#include "library/library.h"
#include "library/array.h"
#include "library/core.h"
#include "library/fiber_lib.h"
#include "library/file_lib.h"
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
    {"exit",    lib_exit,   SIZE_MAX, "Exit the script with optional exit code"},

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

    // File I/O functions
    {"readfile",    lib_readfile,    1, "Read entire file contents as a string"},
    {"writefile",   lib_writefile,   2, "Write string content to a file (overwrites)"},
    {"appendfile",  lib_appendfile,  2, "Append string content to a file"},
    {"file_exists", lib_file_exists, 1, "Return true if a file exists at the given path"},
    {"readlines",   lib_readlines,   1, "Read file into array of lines"},

    // Table globals (setmetatable/getmetatable stay global; remove/has_key/size/pairs are now methods)
    {"setmetatable",  lib_setmetatable,  2, "Set the metatable for a table"},
    {"getmetatable",  lib_getmetatable,  1, "Get the metatable of a table"},

    // Array globals (array_create stays; all instance methods moved to type metatable)
    {"array_create",  lib_array_create,  SIZE_MAX, "Create a new array with required capacity and optional fill value"},

    // Type system functions
    {"get_type_config", lib_get_type_config, 0, "Return the current type checking configuration"},

    // Utility functions
    {"random",     lib_random,     0,        "Random float in [0,1), or integer in [min,max] with 1 or 2 args"},
    {"randomseed", lib_randomseed, 1,        "Seed the random number generator"},
    {"clock",      lib_clock,      0,        "Return monotonic wall-clock time in nanoseconds"},
    {"load",       lib_load,       1,        "Execute a Mobius script file by path"},
    {"id",         lib_id,         1,        "Return the memory address of a heap-allocated value"},

    // Float type inspection
    {"isnan",      lib_isnan,      1,        "Return true if value is NaN"},
    {"isinf",      lib_isinf,      1,        "Return true if value is infinity"},
    {"isfinite",   lib_isfinite,   1,        "Return true if value is finite (not NaN or infinity)"},

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
    if (!state) return;
    
    for (size_t i = 0; library_registry[i].name != NULL; i++) {
        const PluginFunction* func = &library_registry[i];
        
        Value func_value = make_native_function_value(func->function);
        
        int slot = state->assignGlobalSlot(func->name);
        func_value.flags |= VAL_FLAG_DEFINED;
        state->globalSlot(slot) = func_value;
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
