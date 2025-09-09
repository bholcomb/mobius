#include "library.h"
#include "core.h"
#include "math.h"
#include "string.h"
#include "table_lib.h"
#include "array.h"
#include "types.h"
#include "util.h"
#include <stdio.h>
#include <string.h>

// =============================================================================
// UNIFIED LIBRARY REGISTRY
// =============================================================================

static const LibraryEntry library_registry[] = {
    // Core functions
    {"print", lib_print, 0, -1},        // Variadic: 0 or more args
    {"typeof", lib_typeof, 1, 1},       // Exactly 1 arg
    {"str", lib_str, 1, 1},             // Exactly 1 arg
    {"int", lib_int, 1, 1},             // Exactly 1 arg
    {"float", lib_float, 1, 1},         // Exactly 1 arg
    
    // Math functions
    {"abs", lib_abs, 1, 1},             // Exactly 1 arg
    {"min", lib_min, 2, -1},            // At least 2 args, variadic
    {"max", lib_max, 2, -1},            // At least 2 args, variadic
    {"pow", lib_pow, 2, 2},             // Exactly 2 args
    {"sqrt", lib_sqrt, 1, 1},           // Exactly 1 arg
    {"floor", lib_floor, 1, 1},         // Exactly 1 arg
    {"ceil", lib_ceil, 1, 1},           // Exactly 1 arg
    {"round", lib_round, 1, 1},         // Exactly 1 arg
    
    // String functions
    {"len", lib_len, 1, 1},             // Exactly 1 arg (works for strings and arrays)
    {"upper", lib_upper, 1, 1},         // Exactly 1 arg
    {"lower", lib_lower, 1, 1},         // Exactly 1 arg
    {"substr", lib_substr, 3, 3},       // Exactly 3 args
    {"concat", lib_concat, 2, -1},      // At least 2 args, variadic
    {"contains", lib_contains, 2, 2},   // Exactly 2 args
    
    // Table functions
    {"table_insert", lib_table_insert, 3, 3},     // Exactly 3 args
    {"table_remove", lib_table_remove, 2, 2},     // Exactly 2 args
    {"table_has_key", lib_table_has_key, 2, 2},   // Exactly 2 args
    {"table_size", lib_table_size, 1, 1},         // Exactly 1 arg
    {"setmetatable", lib_setmetatable, 2, 2},     // Exactly 2 args
    {"getmetatable", lib_getmetatable, 1, 1},     // Exactly 1 arg
    {"pairs", lib_pairs, 1, 1},                   // Exactly 1 arg
    
    // Array functions
    {"array_create", lib_array_create, 0, 1},     // 0 or 1 args
    {"array_push", lib_array_push, 2, 2},         // Exactly 2 args
    {"array_pop", lib_array_pop, 1, 1},           // Exactly 1 arg
    {"array_get", lib_array_get, 2, 2},           // Exactly 2 args
    {"array_set", lib_array_set, 3, 3},           // Exactly 3 args
    {"array_length", lib_array_length, 1, 1},     // Exactly 1 arg
    {"array_slice", lib_array_slice, 3, 3},       // Exactly 3 args
    {"array_concat", lib_array_concat, 2, -1},    // At least 2 args, variadic
    {"array_reverse", lib_array_reverse, 1, 1},   // Exactly 1 arg
    {"array_find", lib_array_find, 2, 2},         // Exactly 2 args
    
    // Type system functions
    {"set_strict_types", lib_set_strict_types, 0, 1},     // 0 or 1 args
    {"set_type_warnings", lib_set_type_warnings, 1, 1},   // Exactly 1 arg
    {"get_type_config", lib_get_type_config, 0, 0},       // No args
    
    // Utility functions
    {"random", lib_random, 0, 2},                     // 0, 1, or 2 args
    {"time", lib_time, 0, 0},                         // No args
    {"clock", lib_clock, 0, 0},                       // No args
    {"load", lib_load, 1, 1},                         // Exactly 1 arg
    
    // Sentinel
    {NULL, NULL, 0, 0}
};

// Get the complete library function registry
const LibraryEntry* get_library_registry(void) {
    return library_registry;
}

// Get the size of the library registry
size_t get_library_registry_size(void) {
    static size_t size = 0;
    if (size == 0) {
        const LibraryEntry* entry = library_registry;
        while (entry->name != NULL) {
            size++;
            entry++;
        }
    }
    return size;
}

// Lookup a library function by name
LibraryFunction lookup_library_function(const char* name) {
    if (!name) return NULL;
    
    const LibraryEntry* entry = library_registry;
    while (entry->name != NULL) {
        if (strcmp(entry->name, name) == 0) {
            return entry->func;
        }
        entry++;
    }
    
    return NULL;
}

// =============================================================================
// UNIFIED FUNCTION CALLING INTERFACE
// =============================================================================

// Call a library function with argument validation
EvalResult call_library_function(const char* name, Environment* env, int arg_count) {
    if (!name || !env) {
        return make_error("Invalid function call parameters", 0, 0);
    }
    
    // Find the function in the registry
    const LibraryEntry* entry = library_registry;
    while (entry->name != NULL) {
        if (strcmp(entry->name, name) == 0) {
            // Validate argument count
            if (entry->min_args >= 0 && arg_count < entry->min_args) {
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg), 
                        "%s expects at least %d arguments but got %d", 
                        name, entry->min_args, arg_count);
                return make_error(error_msg, 0, 0);
            }
            
            if (entry->max_args >= 0 && arg_count > entry->max_args) {
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg), 
                        "%s expects at most %d arguments but got %d", 
                        name, entry->max_args, arg_count);
                return make_error(error_msg, 0, 0);
            }
            
            // Call the function
            return entry->func(env, arg_count);
        }
        entry++;
    }
    
    // Function not found
    char error_msg[256];
    snprintf(error_msg, sizeof(error_msg), "Unknown function '%s'", name);
    return make_error(error_msg, 0, 0);
}
