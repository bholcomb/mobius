#ifndef MOBIUS_PLUGIN_H
#define MOBIUS_PLUGIN_H

#include "evaluator.h"
#include <stddef.h>

// Plugin API version for compatibility checking
#define MOBIUS_PLUGIN_API_VERSION 1

// Plugin metadata structure
typedef struct {
    const char* name;           // Plugin name (e.g., "mathlib")
    const char* version;        // Plugin version (e.g., "1.0.0")
    const char* description;    // Brief description
    const char* author;         // Plugin author
    size_t api_version;         // Plugin API version
    const char* license;        // Plugin license (optional)
} PluginMetadata;

// Plugin function definition
typedef struct {
    const char* name;           // Function name
    BuiltinFunction function;   // Function pointer
    size_t arity;              // Expected arguments (SIZE_MAX for variadic)
    const char* description;    // Function description
    const char* category;       // Function category (e.g., "math", "string")
    const char* usage;          // Usage example (optional)
} PluginFunction;

// Plugin structure - main interface
typedef struct {
    PluginMetadata metadata;    // Plugin information
    PluginFunction* functions;  // Array of functions provided
    size_t function_count;      // Number of functions
    
    // Plugin lifecycle hooks
    int (*init_plugin)(void);                    // Initialize plugin (return 0 on success)
    void (*cleanup_plugin)(void);               // Cleanup plugin resources
    const char* (*get_help)(const char* name);  // Get help for specific function
    int (*validate_env)(void);                  // Validate runtime environment
} Plugin;

// Plugin entry point - every plugin must export this symbol
// Plugins should implement: Plugin* mobius_plugin_info(void);
typedef Plugin* (*PluginInfoFunc)(void);

// Plugin loading status
typedef enum {
    PLUGIN_STATUS_UNLOADED,     // Plugin not loaded
    PLUGIN_STATUS_LOADED,       // Plugin loaded successfully
    PLUGIN_STATUS_ERROR,        // Plugin failed to load
    PLUGIN_STATUS_INCOMPATIBLE  // Plugin API version mismatch
} PluginStatus;

// Plugin load result
typedef struct {
    PluginStatus status;
    const char* error_message;
    Plugin* plugin;
} PluginLoadResult;

// Convenience macros for plugin authors
#define MOBIUS_PLUGIN_EXPORT __attribute__((visibility("default")))

#define MOBIUS_PLUGIN_FUNCTION(func_name, func_ptr, arity, desc, category) \
    {func_name, func_ptr, arity, desc, category, NULL}

#define MOBIUS_PLUGIN_FUNCTION_WITH_USAGE(func_name, func_ptr, arity, desc, category, usage) \
    {func_name, func_ptr, arity, desc, category, usage}

// Function signature validation
bool validate_plugin_function_signature(PluginFunction* func);

// Plugin helper functions
const char* plugin_status_string(PluginStatus status);
void print_plugin_info(Plugin* plugin);
void print_plugin_functions(Plugin* plugin);

#endif // MOBIUS_PLUGIN_H
