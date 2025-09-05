#ifndef MOBIUS_EMBEDDING_H
#define MOBIUS_EMBEDDING_H

/*
 * Mobius Scripting Language - Embedding API
 * 
 * This header provides a complete C API for embedding the Mobius interpreter
 * into applications, similar to Lua's lua_State or Python's C API.
 * 
 * Key Features:
 * - Isolated interpreter contexts
 * - Bidirectional value exchange (C ↔ Mobius)
 * - Custom function registration
 * - Error handling and propagation
 * - Memory management
 * - Plugin system integration
 */

#include "ast.h"
#include "evaluator.h"
#include "environment.h"
#include "module_registry.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

typedef struct MobiusState MobiusState;
typedef struct MobiusValue MobiusValue;

// ============================================================================
// EMBEDDING CONTEXT AND STATE MANAGEMENT
// ============================================================================

/**
 * Create a new Mobius interpreter state
 * Similar to lua_newstate() or Py_NewInterpreter()
 * 
 * @return New interpreter state, or NULL on failure
 */
MobiusState* mobius_new_state(void);

/**
 * Free a Mobius interpreter state
 * Cleans up all associated memory and resources
 * 
 * @param state The state to free
 */
void mobius_free_state(MobiusState* state);

/**
 * Load and initialize core modules in the state
 * Sets up the standard library and plugin system
 * 
 * @param state The interpreter state
 * @return 0 on success, non-zero on error
 */
int mobius_init_core(MobiusState* state);

/**
 * Load a plugin module into the state
 * 
 * @param state The interpreter state
 * @param plugin_path Path to the .so plugin file
 * @return 0 on success, non-zero on error
 */
int mobius_load_plugin(MobiusState* state, const char* plugin_path);

// ============================================================================
// SCRIPT EXECUTION
// ============================================================================

/**
 * Execute a string of Mobius code
 * 
 * @param state The interpreter state
 * @param code The Mobius code to execute
 * @return 0 on success, non-zero on error
 */
int mobius_exec_string(MobiusState* state, const char* code);

/**
 * Execute a Mobius script file
 * 
 * @param state The interpreter state
 * @param filename Path to the script file
 * @return 0 on success, non-zero on error
 */
int mobius_exec_file(MobiusState* state, const char* filename);

/**
 * Execute code and return the result value
 * 
 * @param state The interpreter state
 * @param code The Mobius code to execute
 * @param result Pointer to store the result (caller must free)
 * @return 0 on success, non-zero on error
 */
int mobius_eval_string(MobiusState* state, const char* code, MobiusValue** result);

// ============================================================================
// VALUE EXCHANGE SYSTEM
// ============================================================================

/**
 * Mobius value wrapper for C API
 * Provides safe access to Mobius values from C code
 */
struct MobiusValue {
    Value internal_value;
    MobiusState* state;  // Reference to owning state
    bool is_owned;       // Whether this wrapper owns the value
};

// Value creation functions
MobiusValue* mobius_create_nil(MobiusState* state);
MobiusValue* mobius_create_bool(MobiusState* state, bool value);
MobiusValue* mobius_create_integer(MobiusState* state, int64_t value);
MobiusValue* mobius_create_float(MobiusState* state, double value);
MobiusValue* mobius_create_string(MobiusState* state, const char* value);
MobiusValue* mobius_create_userdata(MobiusState* state, void* ptr, 
                                   UserdataDestructor destructor, 
                                   const char* type_name, size_t size);

// Value access functions
bool mobius_is_nil(const MobiusValue* value);
bool mobius_is_bool(const MobiusValue* value);
bool mobius_is_integer(const MobiusValue* value);
bool mobius_is_float(const MobiusValue* value);
bool mobius_is_string(const MobiusValue* value);
bool mobius_is_function(const MobiusValue* value);
bool mobius_is_userdata(const MobiusValue* value);

// Value extraction functions
bool mobius_to_bool(const MobiusValue* value);
int64_t mobius_to_integer(const MobiusValue* value);
double mobius_to_float(const MobiusValue* value);
const char* mobius_to_string(const MobiusValue* value);

// Userdata extraction functions
void* mobius_to_userdata(const MobiusValue* value);
const char* mobius_userdata_type(const MobiusValue* value);
size_t mobius_userdata_size(const MobiusValue* value);
bool mobius_is_userdata_type(const MobiusValue* value, const char* type_name);

// Value conversion functions
bool mobius_convert_to_bool(const MobiusValue* value);
int64_t mobius_convert_to_integer(const MobiusValue* value);
double mobius_convert_to_float(const MobiusValue* value);
char* mobius_convert_to_string(const MobiusValue* value); // Caller must free

// Value management
void mobius_free_value(MobiusValue* value);
MobiusValue* mobius_copy_value(const MobiusValue* value);

// ============================================================================
// VARIABLE MANAGEMENT
// ============================================================================

/**
 * Set a global variable in the interpreter
 * 
 * @param state The interpreter state
 * @param name Variable name
 * @param value Value to set
 * @return 0 on success, non-zero on error
 */
int mobius_set_global(MobiusState* state, const char* name, const MobiusValue* value);

/**
 * Get a global variable from the interpreter
 * 
 * @param state The interpreter state
 * @param name Variable name
 * @return Value, or NULL if not found (caller must free)
 */
MobiusValue* mobius_get_global(MobiusState* state, const char* name);

/**
 * Check if a global variable exists
 * 
 * @param state The interpreter state
 * @param name Variable name
 * @return true if exists, false otherwise
 */
bool mobius_has_global(MobiusState* state, const char* name);

// ============================================================================
// CUSTOM FUNCTION REGISTRATION
// ============================================================================

/**
 * C function signature for custom functions
 * Similar to lua_CFunction
 * 
 * @param state The interpreter state
 * @param args Array of arguments
 * @param arg_count Number of arguments
 * @param result Pointer to store result (allocated by function)
 * @return 0 on success, non-zero on error
 */
typedef int (*MobiusCFunction)(MobiusState* state, 
                               MobiusValue** args, 
                               size_t arg_count, 
                               MobiusValue** result);

/**
 * Register a C function as a Mobius global function
 * 
 * @param state The interpreter state
 * @param name Function name in Mobius
 * @param func C function implementation
 * @param arg_count Expected argument count (SIZE_MAX for variadic)
 * @param description Optional description
 * @return 0 on success, non-zero on error
 */
int mobius_register_function(MobiusState* state,
                              const char* name,
                              MobiusCFunction func,
                              size_t arg_count,
                              const char* description);

/**
 * Register a C function in a specific namespace/module
 * 
 * @param state The interpreter state
 * @param module_name Module/namespace name
 * @param name Function name
 * @param func C function implementation
 * @param arg_count Expected argument count
 * @param description Optional description
 * @return 0 on success, non-zero on error
 */
int mobius_register_module_function(MobiusState* state,
                                     const char* module_name,
                                     const char* name,
                                     MobiusCFunction func,
                                     size_t arg_count,
                                     const char* description);

// ============================================================================
// ERROR HANDLING
// ============================================================================

/**
 * Error information structure
 */
typedef struct {
    int code;               // Error code
    char* message;          // Error message (caller must free)
    char* suggestion;       // Optional suggestion (caller must free)
    int line;              // Line number (if applicable)
    int column;            // Column number (if applicable)
    char* function_name;   // Function where error occurred (caller must free)
} MobiusError;

/**
 * Get the last error from the interpreter state
 * 
 * @param state The interpreter state
 * @return Error information, or NULL if no error (caller must free)
 */
MobiusError* mobius_get_last_error(MobiusState* state);

/**
 * Clear the last error
 * 
 * @param state The interpreter state
 */
void mobius_clear_error(MobiusState* state);

/**
 * Free error information
 * 
 * @param error Error to free
 */
void mobius_free_error(MobiusError* error);

/**
 * Set an error in the interpreter state (for use in C functions)
 * 
 * @param state The interpreter state
 * @param code Error code
 * @param message Error message
 * @return Error code (for convenience in returning from functions)
 */
int mobius_set_error(MobiusState* state, int code, const char* message);

// ============================================================================
// UTILITY AND INTROSPECTION
// ============================================================================

/**
 * Get interpreter version information
 * 
 * @param major Pointer to store major version
 * @param minor Pointer to store minor version
 * @param patch Pointer to store patch version
 */
void mobius_version(int* major, int* minor, int* patch);

/**
 * Get version as string
 * 
 * @return Version string (static, do not free)
 */
const char* mobius_version_string(void);

/**
 * Get number of loaded plugins
 * 
 * @param state The interpreter state
 * @return Number of loaded plugins
 */
size_t mobius_plugin_count(MobiusState* state);

/**
 * Get information about loaded plugins
 * 
 * @param state The interpreter state
 * @param index Plugin index
 * @return Plugin name, or NULL if index invalid (do not free)
 */
const char* mobius_plugin_name(MobiusState* state, size_t index);

/**
 * Get total number of available functions
 * 
 * @param state The interpreter state
 * @return Total function count (built-in + plugins + custom)
 */
size_t mobius_function_count(MobiusState* state);

// ============================================================================
// MEMORY AND RESOURCE MANAGEMENT
// ============================================================================

/**
 * Force garbage collection (if implemented)
 * 
 * @param state The interpreter state
 */
void mobius_gc_collect(MobiusState* state);

/**
 * Get memory usage statistics
 * 
 * @param state The interpreter state
 * @return Memory usage in bytes, or 0 if not available
 */
size_t mobius_memory_usage(MobiusState* state);

// ============================================================================
// CONVENIENCE MACROS
// ============================================================================

// Error handling macros
#define MOBIUS_OK 0
#define MOBIUS_ERROR_SYNTAX 1
#define MOBIUS_ERROR_RUNTIME 2
#define MOBIUS_ERROR_MEMORY 3
#define MOBIUS_ERROR_TYPE 4
#define MOBIUS_ERROR_ARGUMENT 5

// Argument validation macros for custom functions
#define MOBIUS_CHECK_ARG_COUNT(expected) \
    if (arg_count != expected) { \
        return mobius_set_error(state, MOBIUS_ERROR_ARGUMENT, \
            "Function expects " #expected " arguments"); \
    }

#define MOBIUS_CHECK_ARG_TYPE(index, type_check, type_name) \
    if (!type_check(args[index])) { \
        return mobius_set_error(state, MOBIUS_ERROR_TYPE, \
            "Argument " #index " must be " type_name); \
    }

#ifdef __cplusplus
}
#endif

#endif // MOBIUS_EMBEDDING_H
