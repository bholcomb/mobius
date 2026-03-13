#include "eval/evaluator.h"
#include "util/utility.h"
#include "state/mobius_state.h"
#include "plugin/module_registry.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>


// Helper: Parse namespace path into components
// Returns allocated array of strings (caller must free), NULL on error
// Sets *count to number of components
static char** parse_namespace_path(const char* path, size_t* count, bool* is_global) {
    *count = 0;
    *is_global = false;
    
    if (!path || strlen(path) == 0) {
        return NULL;
    }
    
    // Check for special _GLOBAL identifier
    if (strcmp(path, "_GLOBAL") == 0) {
        *is_global = true;
        return NULL;  // No path components for global import
    }
    
    // Count components (separated by dots)
    size_t component_count = 1;
    for (const char* p = path; *p; p++) {
        if (*p == '.') component_count++;
    }
    
    // Allocate array for components
    char** components = calloc(component_count, sizeof(char*));
    if (!components) return NULL;
    
    // Split path by dots
    char* path_copy = mobius_strdup(path);
    if (!path_copy) {
        free(components);
        return NULL;
    }
    
    char* token = strtok(path_copy, ".");
    size_t index = 0;
    while (token && index < component_count) {
        // Validate component is valid identifier
        if (strlen(token) == 0) {
            // Empty component (e.g., "math..complex")
            for (size_t i = 0; i < index; i++) free(components[i]);
            free(components);
            free(path_copy);
            return NULL;
        }
        components[index++] = mobius_strdup(token);
        token = strtok(NULL, ".");
    }
    
    free(path_copy);
    *count = index;
    return components;
}

// Helper: Get or create nested table
// Walks the namespace path, creating tables as needed
// Returns the final target table, or NULL on error
static Table* get_or_create_nested_table(Environment* env, char** path, size_t path_len, 
                                         int line, int column, EvalResult* error_result) {
    if (path_len == 0) return NULL;
    
    // Start with the first component
    bool found = false;
    Value current_value = get_variable(env, path[0], &found);
    
    Table* current_table = NULL;
    if (found && current_value.type == VAL_TABLE) {
        // Table already exists
        current_table = current_value.as.table;
    } else if (found) {
        // Variable exists but is not a table - error
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), 
            "Cannot create nested namespace '%s': '%s' is not a table", 
            path[0], path[0]);
        *error_result = make_error(env, error_msg, line, column);
        return NULL;
    } else {
        // Create new table for first component
        MobiusState* state = env->current_context->state;
        current_table = create_table(state, 16);
        if (!current_table) {
            *error_result = make_error(env, "Failed to create namespace table", line, column);
            return NULL;
        }
        Value table_value = make_table_value(current_table);
        define_variable(env, path[0], table_value);
    }
    
    // Walk through remaining path components
    for (size_t i = 1; i < path_len; i++) {
        // Try to get the next component from current table
        Value key = make_string_value_from_cstr(env->current_context->state, path[i]);
        Value next_value = table_get(current_table, key);
        free_value(key);
        
        if (next_value.type == VAL_TABLE) {
            // Table exists, continue
            current_table = next_value.as.table;
        } else if (next_value.type == VAL_NIL) {
            // Doesn't exist, create new table
            MobiusState* state = env->current_context->state;
            Table* new_table = create_table(state, 16);
            if (!new_table) {
                *error_result = make_error(env, "Failed to create nested namespace table", line, column);
                return NULL;
            }
            Value new_table_value = make_table_value(new_table);
            Value key2 = make_string_value_from_cstr(env->current_context->state, path[i]);
            table_set(current_table, key2, new_table_value);
            free_value(key2);
            current_table = new_table;
        } else {
            // Exists but is not a table - error
            char error_msg[512];
            snprintf(error_msg, sizeof(error_msg), 
                "Cannot create nested namespace: intermediate path component '%s' is not a table", 
                path[i]);
            *error_result = make_error(env, error_msg, line, column);
            return NULL;
        }
    }
    
    return current_table;
}

// Helper: Check if function override is allowed
// Returns true if should continue, false if should abort
// Handles error/warn/quiet modes
static bool check_function_override(const char* func_name, Table* target_table, 
                                    int line, int column, EvalResult* error_result, Environment* env) {
    // For global imports, target_table is NULL and caller has already checked existence
    // For table imports, we check if function exists in the target table
    bool exists = false;
    
    if (target_table) {
        Value key = make_string_value_from_cstr(env->current_context->state, func_name);
        Value existing = table_get(target_table, key);
        free_value(key);
        exists = (existing.type != VAL_NIL);
    } else {
        // Global import - caller has already determined function exists
        exists = true;
    }
    
    if (!exists) {
        // No override, all good
        return true;
    }
    
    // Function exists - check override behavior from state config
    MobiusState* state = env->current_context->state;
    switch (state->config.override_behavior) {
        case MOBIUS_OVERRIDE_ERROR: {
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg), 
                "Function '%s' already exists in target namespace (use #pragma override_behavior to change)",
                func_name);
            *error_result = make_error(env, error_msg, line, column);
            return false;
        }
        case MOBIUS_OVERRIDE_WARN:
            fprintf(stderr, "Warning: Overriding existing function '%s' in namespace\n", func_name);
            return true;
        case MOBIUS_OVERRIDE_QUIET:
            return true;
    }
    
    return true;
}

// Import statement evaluation
EvalResult eval_import_stmt(ImportStmt* stmt, Environment* env) {
    if (!env || !env->current_context) {
        return make_error(env, "Invalid environment context", 0, 0);
    }
    
    ModuleRegistry* registry = mobius_get_global_registry();
    if (!registry) {
        return make_error(env, "Module registry not initialized", 0, 0);
    }
    
    // Extract module name from string literal token
    const char* module_name = stmt->module_name.literal.string;
    if (!module_name) {
        return make_error(env, "Invalid module name - null string", stmt->keyword.line, stmt->keyword.column);
    }
    
    // Validate module name
    size_t name_len = strlen(module_name);
    if (name_len == 0) {
        return make_error(env, "Invalid module name - empty string", stmt->keyword.line, stmt->keyword.column);
    }
    if (name_len > 100) {
        return make_error(env, "Invalid module name - too long", stmt->keyword.line, stmt->keyword.column);
    }
    
    // Determine the target name/path (alias or original module name)
    const char* target_name = module_name;  // Default to module name
    if (stmt->has_alias) {
        // Use alias instead
        if (stmt->alias.type == TOKEN_STRING && stmt->alias.literal.string) {
            target_name = stmt->alias.literal.string;
        } else if (stmt->alias.identifier) {
            target_name = stmt->alias.identifier;
        } else {
            return make_error(env, "Invalid alias", stmt->keyword.line, stmt->keyword.column);
        }
    }
    
    // Parse the target namespace path
    size_t path_len = 0;
    bool is_global = false;
    char** path_components = parse_namespace_path(target_name, &path_len, &is_global);
    
    // Note: We allow multiple modules to share the same namespace.
    // Override checking happens at the function level, not namespace level.
    // The check_function_override() helper will validate each individual function.
    
    // Find or load the module
    LoadedModule* module = find_module(registry, module_name);
    if (!module) {
        // Module not loaded yet, try to load it
        PluginLoadResult result = load_module_by_name(registry, module_name);
        if (result.status != PLUGIN_STATUS_LOADED) {
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg), "Import failed - module '%s' not found", module_name);
            if (path_components) {
                for (size_t i = 0; i < path_len; i++) free(path_components[i]);
                free(path_components);
            }
            return make_error(env, error_msg, stmt->keyword.line, stmt->keyword.column);
        }
        module = find_module(registry, module_name);
        if (!module) {
            if (path_components) {
                for (size_t i = 0; i < path_len; i++) free(path_components[i]);
                free(path_components);
            }
            return make_error(env, "Module loaded but not found in registry", stmt->keyword.line, stmt->keyword.column);
        }
    }
    
    // Increment reference count
    mobius_plugin_increment_refcount(module_name);
    
    // Get the plugin interface
    Plugin* plugin = module->plugin;
    if (!plugin) {
        if (path_components) {
            for (size_t i = 0; i < path_len; i++) free(path_components[i]);
            free(path_components);
        }
        return make_error(env, "Module has no plugin interface", stmt->keyword.line, stmt->keyword.column);
    }
    
    // Determine target table (global env, nested table, or new table)
    Table* target_table = NULL;
    bool table_is_new = false;  // Track if we created a new table
    EvalResult nested_error = {0};
    
    if (is_global) {
        // _GLOBAL import: functions go directly into global environment
        target_table = NULL;
    } else if (path_components && path_len > 0) {
        // Get or create namespace table (reuses existing tables)
        target_table = get_or_create_nested_table(env, path_components, path_len, 
                                                   stmt->keyword.line, stmt->keyword.column, &nested_error);
        if (!target_table) {
            for (size_t i = 0; i < path_len; i++) free(path_components[i]);
            free(path_components);
            return nested_error;
        }
        // Note: get_or_create_nested_table handles defining the variable if needed
    } else {
        // Simple single-name import: check if table already exists, or create new one
        bool found = false;
        Value existing = get_variable(env, target_name, &found);
        
        if (found && existing.type == VAL_TABLE) {
            // Reuse existing table
            target_table = existing.as.table;
            table_is_new = false;
        } else if (found) {
            // Variable exists but is not a table
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg), 
                "Cannot import to '%s': variable already exists and is not a table", target_name);
            if (path_components) {
                for (size_t i = 0; i < path_len; i++) free(path_components[i]);
                free(path_components);
            }
            return make_error(env, error_msg, stmt->keyword.line, stmt->keyword.column);
        } else {
            // Create new table
            MobiusState* state = env->current_context->state;
            target_table = create_table(state, 16);
            if (!target_table) {
                if (path_components) {
                    for (size_t i = 0; i < path_len; i++) free(path_components[i]);
                    free(path_components);
                }
                return make_error(env, "Failed to create module table", stmt->keyword.line, stmt->keyword.column);
            }
            table_is_new = true;
        }
    }
    
    // Populate functions from the plugin directly
    int functions_added = 0;
    for (size_t i = 0; i < plugin->function_count; i++) {
        PluginFunction* func = &plugin->functions[i];
        if (!func || !func->name || !func->function) continue;
        
        const char* func_name = func->name;
        MobiusCFunction func_ptr = func->function;
        
        if (is_global) {
            // Define function directly in global environment
            bool found = false;
            get_variable(env, func_name, &found);
            if (found) {
                EvalResult override_error = {0};
                if (!check_function_override(func_name, NULL, 
                                            stmt->keyword.line, stmt->keyword.column, &override_error, env)) {
                    if (path_components) {
                        for (size_t j = 0; j < path_len; j++) free(path_components[j]);
                        free(path_components);
                    }
                    return override_error;
                }
            }
            Value func_value = make_native_function_value(func_ptr);
            define_variable(env, func_name, func_value);
            free_value(func_value);  // Free after define_variable copies it
            functions_added++;
        } else {
            // Add to target table
            EvalResult override_error = {0};
            if (!check_function_override(func_name, target_table, 
                                        stmt->keyword.line, stmt->keyword.column, &override_error, env)) {
                if (path_components) {
                    for (size_t j = 0; j < path_len; j++) free(path_components[j]);
                    free(path_components);
                }
                return override_error;
            }
            
            Value func_key = make_string_value_from_cstr(env->current_context->state, func_name);
            Value func_value = make_native_function_value(func_ptr);
            table_set(target_table, func_key, func_value);
            free_value(func_key);
            free_value(func_value);  // Free after table_set copies it
            functions_added++;
        }
    }
    
    // If not global import and we created a new table, define it as a variable
    if (!is_global && target_table && table_is_new) {
        Value table_value = make_table_value(target_table);
        const char* var_name = target_name;
        define_variable(env, var_name, table_value);
    }
    
    // Cleanup
    if (path_components) {
        for (size_t i = 0; i < path_len; i++) free(path_components[i]);
        free(path_components);
    }
    
    ctx_push(env->current_context, make_nil_value());
    return make_success(1);
}

// Pragma statement evaluation
EvalResult eval_pragma_stmt(PragmaStmt* stmt, Environment* env) {
    if (!stmt->name.identifier) {
        return make_error(env, "Invalid pragma - missing name", stmt->keyword.line, stmt->keyword.column);
    }
    
    const char* pragma_name = stmt->name.identifier;
    MobiusState* state = env->current_context->state;
    
    // Handle strict_types pragma
    if (strcmp(pragma_name, "strict_types") == 0) {
        // Value should be true or false
        if (stmt->value.type == TOKEN_TRUE) {
            state->config.strict_mode = true;
        } else if (stmt->value.type == TOKEN_FALSE) {
            state->config.strict_mode = false;
        } else if (stmt->value.identifier) {
            if (strcmp(stmt->value.identifier, "true") == 0) {
                state->config.strict_mode = true;
            } else if (strcmp(stmt->value.identifier, "false") == 0) {
                state->config.strict_mode = false;
            } else {
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg), 
                    "Invalid value for pragma strict_types: '%s' (expected true or false)", 
                    stmt->value.identifier);
                return make_error(env, error_msg, stmt->name.line, stmt->name.column);
            }
        } else {
            return make_error(env, "Invalid value for pragma strict_types (expected true or false)", 
                stmt->name.line, stmt->name.column);
        }
        ctx_push(env->current_context, make_nil_value());
        return make_success(1);
    }
    
    // Handle type_warnings pragma
    if (strcmp(pragma_name, "type_warnings") == 0) {
        // Value should be true or false
        if (stmt->value.type == TOKEN_TRUE) {
            state->config.warn_on_conversion = true;
        } else if (stmt->value.type == TOKEN_FALSE) {
            state->config.warn_on_conversion = false;
        } else if (stmt->value.identifier) {
            if (strcmp(stmt->value.identifier, "true") == 0) {
                state->config.warn_on_conversion = true;
            } else if (strcmp(stmt->value.identifier, "false") == 0) {
                state->config.warn_on_conversion = false;
            } else {
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg), 
                    "Invalid value for pragma type_warnings: '%s' (expected true or false)", 
                    stmt->value.identifier);
                return make_error(env, error_msg, stmt->name.line, stmt->name.column);
            }
        } else {
            return make_error(env, "Invalid value for pragma type_warnings (expected true or false)", 
                stmt->name.line, stmt->name.column);
        }
        ctx_push(env->current_context, make_nil_value());
        return make_success(1);
    }
    
    // Handle override_behavior pragma
    if (strcmp(pragma_name, "override_behavior") == 0) {
        // Value should be "error", "warn", or "quiet"
        const char* value = NULL;
        if (stmt->value.identifier) {
            value = stmt->value.identifier;
        } else if (stmt->value.literal.string) {
            value = stmt->value.literal.string;
        }
        
        if (!value) {
            return make_error(env,  "Invalid value for pragma override_behavior", 
                stmt->name.line, stmt->name.column);
        }
        
        if (strcmp(value, "error") == 0) {
            state->config.override_behavior = MOBIUS_OVERRIDE_ERROR;
        } else if (strcmp(value, "warn") == 0) {
            state->config.override_behavior = MOBIUS_OVERRIDE_WARN;
        } else if (strcmp(value, "quiet") == 0) {
            state->config.override_behavior = MOBIUS_OVERRIDE_QUIET;
        } else {
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg), 
                "Invalid value for pragma override_behavior: '%s' (expected 'error', 'warn', or 'quiet')", 
                value);
            return make_error(env, error_msg, stmt->name.line, stmt->name.column);
        }
        ctx_push(env->current_context, make_nil_value());
        return make_success(1);
    }
    
    // Unknown pragma
    char error_msg[256];
    snprintf(error_msg, sizeof(error_msg), 
        "Unknown pragma: '%s' (supported pragmas: strict_types, type_warnings, override_behavior)", 
        pragma_name);
    return make_error(env, error_msg, stmt->name.line, stmt->name.column);
}



