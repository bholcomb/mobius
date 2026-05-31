#include "library/library.h"
#include "library/array.h"
#include "library/buffer_lib.h"
#include "library/core.h"
#include "library/fiber_lib.h"
#include "library/file_lib.h"
#include "library/math.h"
#include "library/struct_view_lib.h"
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
    //                                                          return type
    {"print",      lib_print,      SIZE_MAX, MOBIUS_VAL_NIL,     "Print values to stdout"},
    {"typeof",     lib_typeof,     1,        MOBIUS_VAL_STRING,  "Return the type name of a value as a string"},
    {"str",        lib_str,        1,        MOBIUS_VAL_STRING,  "Convert a value to its string representation"},
    {"int",        lib_int,        1,        MOBIUS_VAL_INT64,   "Convert a value to an integer"},
    {"float",      lib_float,      1,        MOBIUS_VAL_FLOAT64, "Convert a value to a float"},
    {"exit",       lib_exit,       SIZE_MAX, MOBIUS_VAL_NIL,     "Exit the script with optional exit code"},

    // Math functions — abs/min/max depend on input types
    {"abs",        lib_abs,        1,        MOBIUS_VAL_UNKNOWN, "Absolute value"},
    {"min",        lib_min,        2,        MOBIUS_VAL_UNKNOWN, "Minimum of two or more values"},
    {"max",        lib_max,        2,        MOBIUS_VAL_UNKNOWN, "Maximum of two or more values"},
    {"pow",        lib_pow,        2,        MOBIUS_VAL_FLOAT64, "Raise base to an exponent"},
    {"sqrt",       lib_sqrt,       1,        MOBIUS_VAL_FLOAT64, "Square root"},
    {"floor",      lib_floor,      1,        MOBIUS_VAL_FLOAT64, "Round down to nearest integer, returned as a float"},
    {"ceil",       lib_ceil,       1,        MOBIUS_VAL_FLOAT64, "Round up to nearest integer, returned as a float"},
    {"round",      lib_round,      1,        MOBIUS_VAL_FLOAT64, "Round to nearest integer, returned as a float"},

    // String functions
    {"size",       lib_len,        1,        MOBIUS_VAL_INT64,   "Number of elements in an array/table, or bytes in a string/buffer"},
    {"upper",      lib_upper,      1,        MOBIUS_VAL_STRING,  "Convert string to uppercase"},
    {"lower",      lib_lower,      1,        MOBIUS_VAL_STRING,  "Convert string to lowercase"},
    {"substr",     lib_substr,     3,        MOBIUS_VAL_STRING,  "Extract a substring (string, start, length)"},
    {"concat",     lib_concat,     2,        MOBIUS_VAL_STRING,  "Concatenate two or more strings"},
    {"contains",   lib_contains,   2,        MOBIUS_VAL_BOOL,    "Return true if string contains a substring"},
    {"split",      lib_split,      2,        MOBIUS_VAL_ARRAY,   "Split string by delimiter into array"},
    {"join",       lib_join,       2,        MOBIUS_VAL_STRING,  "Join array elements with separator"},
    {"trim",       lib_trim,       1,        MOBIUS_VAL_STRING,  "Trim whitespace from both ends of string"},
    {"startswith", lib_startswith, 2,        MOBIUS_VAL_BOOL,    "Return true if string starts with prefix"},
    {"endswith",   lib_endswith,   2,        MOBIUS_VAL_BOOL,    "Return true if string ends with suffix"},
    {"replace",    lib_replace,    3,        MOBIUS_VAL_STRING,  "Replace all occurrences of old with new"},
    {"find",       lib_find,       2,        MOBIUS_VAL_INT64,   "Find index of substring (-1 if not found)"},
    {"repeat",     lib_repeat,     2,        MOBIUS_VAL_STRING,  "Repeat string N times"},

    // File I/O functions
    {"readfile",   lib_readfile,   1,        MOBIUS_VAL_STRING,  "Read entire file contents as a string"},
    {"writefile",  lib_writefile,  2,        MOBIUS_VAL_BOOL,    "Write string content to a file (overwrites)"},
    {"appendfile", lib_appendfile, 2,        MOBIUS_VAL_BOOL,    "Append string content to a file"},
    {"file_exists",lib_file_exists,1,        MOBIUS_VAL_BOOL,    "Return true if a file exists at the given path"},
    {"readlines",  lib_readlines,  1,        MOBIUS_VAL_ARRAY,   "Read file into array of lines"},

    // Table globals
    {"setmetatable", lib_setmetatable, 2,    MOBIUS_VAL_TABLE,   "Set the metatable for a table"},
    {"getmetatable", lib_getmetatable, 1,    MOBIUS_VAL_TABLE,   "Get the metatable of a table"},

    // Array globals
    {"array_create", lib_array_create, SIZE_MAX, MOBIUS_VAL_ARRAY, "Create a new array with required capacity and optional fill value"},
    {"buffer_create", lib_buffer_create, SIZE_MAX, MOBIUS_VAL_BUFFER, "Create a new byte buffer with optional fill byte"},
    {"buffer_from_string", lib_buffer_from_string, 1, MOBIUS_VAL_BUFFER, "Create a new byte buffer from a string's raw bytes"},
    {"__define_struct", lib_define_struct, 2, MOBIUS_VAL_USERDATA, "Define a buffer-backed struct layout"},

    // Type system functions
    {"get_type_config", lib_get_type_config, 0, MOBIUS_VAL_TABLE, "Return the current type checking configuration"},

    // Utility functions
    {"random",     lib_random,     0,        MOBIUS_VAL_UNKNOWN, "Random float in [0,1), or integer in [min,max] with 1 or 2 args"},
    {"randomseed", lib_randomseed, 1,        MOBIUS_VAL_NIL,     "Seed the random number generator"},
    {"clock",      lib_clock,      0,        MOBIUS_VAL_INT64,   "Return monotonic wall-clock time in nanoseconds"},
    {"time",       lib_time,       0,        MOBIUS_VAL_INT64,   "Return current Unix timestamp in seconds"},
    {"load",       lib_load,       1,        MOBIUS_VAL_UNKNOWN, "Execute a Mobius script file by path"},
    {"id",         lib_id,         1,        MOBIUS_VAL_INT64,   "Return the memory address of a heap-allocated value"},

    // Float type inspection
    {"isnan",      lib_isnan,      1,        MOBIUS_VAL_BOOL,    "Return true if value is NaN"},
    {"isinf",      lib_isinf,      1,        MOBIUS_VAL_BOOL,    "Return true if value is infinity"},
    {"isfinite",   lib_isfinite,   1,        MOBIUS_VAL_BOOL,    "Return true if value is finite (not NaN or infinity)"},

    // Sentinel
    {NULL, NULL, 0, MOBIUS_VAL_UNKNOWN, NULL}
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
        if (slot < 0) return;
        state->setGlobalValue(slot, func_value);
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
