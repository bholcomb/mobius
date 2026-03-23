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
    {"print", lib_print, SIZE_MAX},  // Variadic: 0 or more args
    {"typeof", lib_typeof, 1},       // Exactly 1 arg
    {"str", lib_str, 1},             // Exactly 1 arg
    {"int", lib_int, 1},             // Exactly 1 arg
    {"float", lib_float, 1},         // Exactly 1 arg
    
    // Math functions
    {"abs", lib_abs, 1},             // Exactly 1 arg
    {"min", lib_min, 2},            // At least 2 args, variadic
    {"max", lib_max, 2},            // At least 2 args, variadic
    {"pow", lib_pow, 2},             // Exactly 2 args
    {"sqrt", lib_sqrt, 1},           // Exactly 1 arg
    {"floor", lib_floor, 1},         // Exactly 1 arg
    {"ceil", lib_ceil, 1},           // Exactly 1 arg
    {"round", lib_round, 1},         // Exactly 1 arg
    
    // String functions
    {"len", lib_len, 1},             // Exactly 1 arg (works for strings and arrays)
    {"upper", lib_upper, 1},         // Exactly 1 arg
    {"lower", lib_lower, 1},         // Exactly 1 arg
    {"substr", lib_substr, 3},       // Exactly 3 args
    {"concat", lib_concat, 2},      // At least 2 args, variadic
    {"contains", lib_contains, 2},   // Exactly 2 args
    
    // Table functions
    {"table_insert", lib_table_insert, 3},     // Exactly 3 args
    {"table_remove", lib_table_remove, 2},     // Exactly 2 args
    {"table_has_key", lib_table_has_key, 2},   // Exactly 2 args
    {"table_size", lib_table_size, 1},         // Exactly 1 arg
    {"setmetatable", lib_setmetatable, 2},     // Exactly 2 args
    {"getmetatable", lib_getmetatable, 1},     // Exactly 1 arg
    {"pairs", lib_pairs, 1},                   // Exactly 1 arg
    
    // Array functions
    {"array_create", lib_array_create, 0},     // 0 or 1 args
    {"array_push", lib_array_push, 2},         // Exactly 2 args
    {"array_pop", lib_array_pop, 1},           // Exactly 1 arg
    {"array_get", lib_array_get, 2},           // Exactly 2 args
    {"array_set", lib_array_set, 3},           // Exactly 3 args
    {"array_length", lib_array_length, 1},     // Exactly 1 arg
    {"array_slice", lib_array_slice, 3},       // Exactly 3 args
    {"array_concat", lib_array_concat, 2},    // At least 2 args, variadic
    {"array_reverse", lib_array_reverse, 1},   // Exactly 1 arg
    {"array_find", lib_array_find, 2},         // Exactly 2 args
    
    // Type system functions
    // Note: set_strict_types() and set_type_warnings() removed - use #pragma instead
    {"get_type_config", lib_get_type_config, 0},       // No args
    
    // Utility functions
    {"random", lib_random, 0},                     // 0, 1, or 2 args
    {"time", lib_time, 0},                         // No args
    {"clock", lib_clock, 0},                       // No args
    {"load", lib_load, 1},                         // Exactly 1 arg
    {"id", lib_id, 1},                             // Exactly 1 arg - get identity/address
    
    // Sentinel
    {NULL, NULL, 0}
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
        
        const char* interned_name = pool->intern(func->name)->data;
        state->globalEnv()->define(interned_name, func_value);
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
