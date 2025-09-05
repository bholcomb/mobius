#define _GNU_SOURCE  // For strdup
#include "embedding.h"
#include "scanner.h"
#include "parser.h"
#include "file_io.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// INTERNAL STATE STRUCTURE
// ============================================================================

struct MobiusState {
    Environment* global_env;        // Global environment
    ModuleRegistry* registry;       // Plugin registry
    MobiusError* last_error;        // Last error information
    bool initialized;               // Whether core has been initialized
    
    // Custom function storage
    struct {
        char* name;
        MobiusCFunction func;
        size_t arg_count;
        char* description;
    }* custom_functions;
    size_t custom_function_count;
    size_t custom_function_capacity;
};

// ============================================================================
// INTERNAL HELPER FUNCTIONS
// ============================================================================

static void clear_error(MobiusState* state) {
    if (state->last_error) {
        mobius_free_error(state->last_error);
        state->last_error = NULL;
    }
}

static void set_error_internal(MobiusState* state, int code, const char* message, 
                              const char* suggestion, int line, int column, 
                              const char* function_name) {
    clear_error(state);
    
    state->last_error = malloc(sizeof(MobiusError));
    if (!state->last_error) return;
    
    state->last_error->code = code;
    state->last_error->message = message ? strdup(message) : NULL;
    state->last_error->suggestion = suggestion ? strdup(suggestion) : NULL;
    state->last_error->line = line;
    state->last_error->column = column;
    state->last_error->function_name = function_name ? strdup(function_name) : NULL;
}

// Note: custom_function_wrapper removed as it was unused and incomplete

// ============================================================================
// STATE MANAGEMENT
// ============================================================================

MobiusState* mobius_new_state(void) {
    MobiusState* state = malloc(sizeof(MobiusState));
    if (!state) return NULL;
    
    // Initialize all fields
    state->global_env = NULL;
    state->registry = NULL;
    state->last_error = NULL;
    state->initialized = false;
    state->custom_functions = NULL;
    state->custom_function_count = 0;
    state->custom_function_capacity = 0;
    
    // Create global environment
    state->global_env = create_environment(NULL);
    if (!state->global_env) {
        free(state);
        return NULL;
    }
    
    // Create module registry
    state->registry = create_module_registry();
    if (!state->registry) {
        free_environment(state->global_env);
        free(state);
        return NULL;
    }
    
    return state;
}

void mobius_free_state(MobiusState* state) {
    if (!state) return;
    
    // Free global environment
    if (state->global_env) {
        free_environment(state->global_env);
    }
    
    // Free module registry
    if (state->registry) {
        free_module_registry(state->registry);
    }
    
    // Free error information
    clear_error(state);
    
    // Free custom functions
    for (size_t i = 0; i < state->custom_function_count; i++) {
        free(state->custom_functions[i].name);
        free(state->custom_functions[i].description);
    }
    free(state->custom_functions);
    
    free(state);
}

int mobius_init_core(MobiusState* state) {
    if (!state) return MOBIUS_ERROR_ARGUMENT;
    
    // Set the global registry for the evaluator
    set_global_module_registry(state->registry);
    
    state->initialized = true;
    return MOBIUS_OK;
}

int mobius_load_plugin(MobiusState* state, const char* plugin_path) {
    if (!state || !plugin_path) return MOBIUS_ERROR_ARGUMENT;
    
    PluginLoadResult result = load_module(state->registry, plugin_path);
    if (result.status != PLUGIN_STATUS_LOADED) {
        set_error_internal(state, MOBIUS_ERROR_RUNTIME, 
                          result.error_message ? result.error_message : "Failed to load plugin",
                          "Check that the plugin file exists and is valid", 0, 0, NULL);
        return MOBIUS_ERROR_RUNTIME;
    }
    
    return MOBIUS_OK;
}

// ============================================================================
// SCRIPT EXECUTION
// ============================================================================

int mobius_exec_string(MobiusState* state, const char* code) {
    if (!state || !code) return MOBIUS_ERROR_ARGUMENT;
    
    clear_error(state);
    
    // Scan tokens
    TokenArray tokens = scan_source(code);
    if (tokens.count == 0) {
        free_token_array(&tokens);
        set_error_internal(state, MOBIUS_ERROR_SYNTAX, "No tokens found", NULL, 0, 0, NULL);
        return MOBIUS_ERROR_SYNTAX;
    }
    
    // Parse AST
    ParseResult parse_result = parse(tokens);
    free_token_array(&tokens);
    
    if (parse_result.had_error) {
        set_error_internal(state, MOBIUS_ERROR_SYNTAX, "Parse error", 
                          "Check syntax and structure", 0, 0, NULL);
        free_parse_result(&parse_result);
        return MOBIUS_ERROR_SYNTAX;
    }
    
    // Execute
    EvalResult eval_result = evaluate_program(parse_result.statements, 
                                             parse_result.count, 
                                             state->global_env);
    free_parse_result(&parse_result);
    
    if (is_error(eval_result)) {
        set_error_internal(state, MOBIUS_ERROR_RUNTIME, 
                          eval_result.error.message,
                          eval_result.error.suggestion,
                          eval_result.error.line,
                          eval_result.error.column,
                          eval_result.error.function_name);
        return MOBIUS_ERROR_RUNTIME;
    }
    
    return MOBIUS_OK;
}

int mobius_exec_file(MobiusState* state, const char* filename) {
    if (!state || !filename) return MOBIUS_ERROR_ARGUMENT;
    
    // Read file content
    FileResult file_result = read_file(filename);
    if (!file_result.success) {
        set_error_internal(state, MOBIUS_ERROR_RUNTIME, 
                          file_result.error ? file_result.error : "Failed to read file", 
                          "Check that file exists and is readable", 
                          0, 0, NULL);
        return MOBIUS_ERROR_RUNTIME;
    }
    
    char* content = file_result.content;
    
    int result = mobius_exec_string(state, content);
    free_file_result(&file_result);
    return result;
}

int mobius_eval_string(MobiusState* state, const char* code, MobiusValue** result) {
    if (!state || !code || !result) return MOBIUS_ERROR_ARGUMENT;
    
    // For now, execute and set result to nil
    // A more sophisticated implementation would capture the last expression value
    int exec_result = mobius_exec_string(state, code);
    if (exec_result != MOBIUS_OK) {
        *result = NULL;
        return exec_result;
    }
    
    *result = mobius_create_nil(state);
    return MOBIUS_OK;
}

// ============================================================================
// VALUE MANAGEMENT
// ============================================================================

MobiusValue* mobius_create_nil(MobiusState* state) {
    if (!state) return NULL;
    
    MobiusValue* value = malloc(sizeof(MobiusValue));
    if (!value) return NULL;
    
    value->internal_value = make_nil_value();
    value->state = state;
    value->is_owned = true;
    
    return value;
}

MobiusValue* mobius_create_bool(MobiusState* state, bool val) {
    if (!state) return NULL;
    
    MobiusValue* value = malloc(sizeof(MobiusValue));
    if (!value) return NULL;
    
    value->internal_value = make_bool_value(val);
    value->state = state;
    value->is_owned = true;
    
    return value;
}

MobiusValue* mobius_create_integer(MobiusState* state, int64_t val) {
    if (!state) return NULL;
    
    MobiusValue* value = malloc(sizeof(MobiusValue));
    if (!value) return NULL;
    
    value->internal_value = make_integer_value(NUM_INT64, val);
    value->state = state;
    value->is_owned = true;
    
    return value;
}

MobiusValue* mobius_create_float(MobiusState* state, double val) {
    if (!state) return NULL;
    
    MobiusValue* value = malloc(sizeof(MobiusValue));
    if (!value) return NULL;
    
    value->internal_value = make_float_value(val);
    value->state = state;
    value->is_owned = true;
    
    return value;
}

MobiusValue* mobius_create_string(MobiusState* state, const char* val) {
    if (!state || !val) return NULL;
    
    MobiusValue* value = malloc(sizeof(MobiusValue));
    if (!value) return NULL;
    
    char* str_copy = malloc(strlen(val) + 1);
    if (!str_copy) {
        free(value);
        return NULL;
    }
    strcpy(str_copy, val);
    
    value->internal_value = make_string_value(str_copy);
    value->state = state;
    value->is_owned = true;
    
    return value;
}

MobiusValue* mobius_create_userdata(MobiusState* state, void* ptr, 
                                   UserdataDestructor destructor, 
                                   const char* type_name, size_t size) {
    if (!state || !ptr) return NULL;
    
    MobiusValue* value = malloc(sizeof(MobiusValue));
    if (!value) return NULL;
    
    value->internal_value = make_userdata_value(ptr, destructor, type_name, size);
    value->state = state;
    value->is_owned = true;
    
    return value;
}

// Type checking functions
bool mobius_is_nil(const MobiusValue* value) {
    return value && value->internal_value.type == VAL_NIL;
}

bool mobius_is_bool(const MobiusValue* value) {
    return value && value->internal_value.type == VAL_BOOL;
}

bool mobius_is_integer(const MobiusValue* value) {
    return value && value->internal_value.type == VAL_INTEGER;
}

bool mobius_is_float(const MobiusValue* value) {
    return value && value->internal_value.type == VAL_FLOAT;
}

bool mobius_is_string(const MobiusValue* value) {
    return value && value->internal_value.type == VAL_STRING;
}

bool mobius_is_function(const MobiusValue* value) {
    return value && value->internal_value.type == VAL_FUNCTION;
}

bool mobius_is_userdata(const MobiusValue* value) {
    return value && value->internal_value.type == VAL_USERDATA;
}

// Value extraction functions
bool mobius_to_bool(const MobiusValue* value) {
    if (!value || value->internal_value.type != VAL_BOOL) return false;
    return value->internal_value.as.boolean;
}

int64_t mobius_to_integer(const MobiusValue* value) {
    if (!value || value->internal_value.type != VAL_INTEGER) return 0;
    return value->internal_value.as.integer.value.i64;
}

double mobius_to_float(const MobiusValue* value) {
    if (!value || value->internal_value.type != VAL_FLOAT) return 0.0;
    return value->internal_value.as.float_val;
}

const char* mobius_to_string(const MobiusValue* value) {
    if (!value || value->internal_value.type != VAL_STRING) return NULL;
    return value->internal_value.as.string;
}

// Userdata extraction functions
void* mobius_to_userdata(const MobiusValue* value) {
    if (!value || value->internal_value.type != VAL_USERDATA) return NULL;
    return value->internal_value.as.userdata.ptr;
}

const char* mobius_userdata_type(const MobiusValue* value) {
    if (!value || value->internal_value.type != VAL_USERDATA) return NULL;
    return value->internal_value.as.userdata.type_name;
}

size_t mobius_userdata_size(const MobiusValue* value) {
    if (!value || value->internal_value.type != VAL_USERDATA) return 0;
    return value->internal_value.as.userdata.size;
}

bool mobius_is_userdata_type(const MobiusValue* value, const char* type_name) {
    if (!value || value->internal_value.type != VAL_USERDATA) return false;
    if (!type_name || !value->internal_value.as.userdata.type_name) return false;
    return strcmp(value->internal_value.as.userdata.type_name, type_name) == 0;
}

// Value conversion functions
bool mobius_convert_to_bool(const MobiusValue* value) {
    if (!value) return false;
    return is_truthy(value->internal_value);
}

int64_t mobius_convert_to_integer(const MobiusValue* value) {
    if (!value) return 0;
    
    switch (value->internal_value.type) {
        case VAL_INTEGER:
            return value->internal_value.as.integer.value.i64;
        case VAL_FLOAT:
            return (int64_t)value->internal_value.as.float_val;
        case VAL_BOOL:
            return value->internal_value.as.boolean ? 1 : 0;
        default:
            return 0;
    }
}

double mobius_convert_to_float(const MobiusValue* value) {
    if (!value) return 0.0;
    
    switch (value->internal_value.type) {
        case VAL_FLOAT:
            return value->internal_value.as.float_val;
        case VAL_INTEGER:
            return (double)value->internal_value.as.integer.value.i64;
        case VAL_BOOL:
            return value->internal_value.as.boolean ? 1.0 : 0.0;
        default:
            return 0.0;
    }
}

char* mobius_convert_to_string(const MobiusValue* value) {
    if (!value) return NULL;
    return value_to_string(value->internal_value);
}

void mobius_free_value(MobiusValue* value) {
    if (!value) return;
    
    if (value->is_owned) {
        // For userdata, handle destructor at this level to avoid double-free
        if (value->internal_value.type == VAL_USERDATA && 
            value->internal_value.as.userdata.ptr &&
            value->internal_value.as.userdata.destructor) {
            value->internal_value.as.userdata.destructor(value->internal_value.as.userdata.ptr);
        }
        free_value(value->internal_value);
    }
    
    free(value);
}

MobiusValue* mobius_copy_value(const MobiusValue* value) {
    if (!value) return NULL;
    
    MobiusValue* copy = malloc(sizeof(MobiusValue));
    if (!copy) return NULL;
    
    // Deep copy the internal value
    copy->internal_value = value->internal_value;
    if (value->internal_value.type == VAL_STRING && value->internal_value.as.string) {
        char* str_copy = malloc(strlen(value->internal_value.as.string) + 1);
        if (str_copy) {
            strcpy(str_copy, value->internal_value.as.string);
            copy->internal_value.as.string = str_copy;
        }
    }
    
    copy->state = value->state;
    
    // For userdata, copies should not own the userdata to prevent double-free
    // Only the original MobiusValue should own and manage the userdata lifetime
    if (value->internal_value.type == VAL_USERDATA) {
        copy->is_owned = false;  // Don't call destructor for copies
    } else {
        copy->is_owned = true;
    }
    
    return copy;
}

// ============================================================================
// VARIABLE MANAGEMENT
// ============================================================================

int mobius_set_global(MobiusState* state, const char* name, const MobiusValue* value) {
    if (!state || !name || !value) return MOBIUS_ERROR_ARGUMENT;
    
    define_variable(state->global_env, name, value->internal_value);
    return MOBIUS_OK;
}

MobiusValue* mobius_get_global(MobiusState* state, const char* name) {
    if (!state || !name) return NULL;
    
    bool found = false;
    Value val = get_variable(state->global_env, name, &found);
    if (!found) return NULL;
    
    MobiusValue* result = malloc(sizeof(MobiusValue));
    if (!result) return NULL;
    
    result->internal_value = val;
    result->state = state;
    result->is_owned = false;  // Don't own the value from environment
    
    return result;
}

bool mobius_has_global(MobiusState* state, const char* name) {
    if (!state || !name) return false;
    
    bool found = false;
    get_variable(state->global_env, name, &found);
    return found;
}

// ============================================================================
// CUSTOM FUNCTION REGISTRATION
// ============================================================================

int mobius_register_function(MobiusState* state,
                              const char* name,
                              MobiusCFunction func,
                              size_t arg_count,
                              const char* description) {
    if (!state || !name || !func) return MOBIUS_ERROR_ARGUMENT;
    
    // For now, store the function info but don't actually register it
    // Full implementation would integrate with the evaluator's function lookup
    
    // Expand custom function array if needed
    if (state->custom_function_count >= state->custom_function_capacity) {
        size_t new_capacity = state->custom_function_capacity == 0 ? 8 : state->custom_function_capacity * 2;
        void* new_array = realloc(state->custom_functions, 
                                 new_capacity * sizeof(*state->custom_functions));
        if (!new_array) return MOBIUS_ERROR_MEMORY;
        
        state->custom_functions = new_array;
        state->custom_function_capacity = new_capacity;
    }
    
    // Store function info
    state->custom_functions[state->custom_function_count].name = strdup(name);
    state->custom_functions[state->custom_function_count].func = func;
    state->custom_functions[state->custom_function_count].arg_count = arg_count;
    state->custom_functions[state->custom_function_count].description = description ? strdup(description) : NULL;
    
    state->custom_function_count++;
    
    return MOBIUS_OK;
}

int mobius_register_module_function(MobiusState* state,
                                     const char* module_name,
                                     const char* name,
                                     MobiusCFunction func,
                                     size_t arg_count,
                                     const char* description) {
    // For now, just register as a global function with module prefix
    if (!state || !module_name || !name || !func) return MOBIUS_ERROR_ARGUMENT;
    
    char full_name[256];
    snprintf(full_name, sizeof(full_name), "%s_%s", module_name, name);
    
    return mobius_register_function(state, full_name, func, arg_count, description);
}

// ============================================================================
// ERROR HANDLING
// ============================================================================

MobiusError* mobius_get_last_error(MobiusState* state) {
    if (!state || !state->last_error) return NULL;
    
    // Return a copy of the error
    MobiusError* copy = malloc(sizeof(MobiusError));
    if (!copy) return NULL;
    
    copy->code = state->last_error->code;
    copy->message = state->last_error->message ? strdup(state->last_error->message) : NULL;
    copy->suggestion = state->last_error->suggestion ? strdup(state->last_error->suggestion) : NULL;
    copy->line = state->last_error->line;
    copy->column = state->last_error->column;
    copy->function_name = state->last_error->function_name ? strdup(state->last_error->function_name) : NULL;
    
    return copy;
}

void mobius_clear_error(MobiusState* state) {
    if (!state) return;
    clear_error(state);
}

void mobius_free_error(MobiusError* error) {
    if (!error) return;
    
    free(error->message);
    free(error->suggestion);
    free(error->function_name);
    free(error);
}

int mobius_set_error(MobiusState* state, int code, const char* message) {
    if (!state) return code;
    
    set_error_internal(state, code, message, NULL, 0, 0, NULL);
    return code;
}

// ============================================================================
// UTILITY AND INTROSPECTION
// ============================================================================

void mobius_version(int* major, int* minor, int* patch) {
    if (major) *major = 0;   // MOBIUS_VERSION_MAJOR;
    if (minor) *minor = 1;   // MOBIUS_VERSION_MINOR;
    if (patch) *patch = 0;   // MOBIUS_VERSION_PATCH;
}

const char* mobius_version_string(void) {
    return "0.1.0";  // MOBIUS_VERSION_STRING;
}

size_t mobius_plugin_count(MobiusState* state) {
    if (!state || !state->registry) return 0;
    return state->registry->module_count;
}

const char* mobius_plugin_name(MobiusState* state, size_t index) {
    if (!state || !state->registry) return NULL;
    
    LoadedModule* module = state->registry->modules;
    for (size_t i = 0; i < index && module; i++) {
        module = module->next;
    }
    
    return module ? module->name : NULL;
}

size_t mobius_function_count(MobiusState* state) {
    if (!state) return 0;
    
    size_t count = 0;
    
    // Built-in stdlib functions (hardcoded for now)
    count += 22;  // Known stdlib function count
    
    // Plugin functions
    if (state->registry) {
        count += state->registry->function_count;
    }
    
    // Custom functions
    count += state->custom_function_count;
    
    return count;
}

// ============================================================================
// MEMORY AND RESOURCE MANAGEMENT
// ============================================================================

void mobius_gc_collect(MobiusState* state) {
    // Not implemented yet - would trigger garbage collection
    (void)state;
}

size_t mobius_memory_usage(MobiusState* state) {
    // Not implemented yet - would return memory usage statistics
    (void)state;
    return 0;
}
