#ifndef MOBIUS_INTERNAL_PLUGIN_H
#define MOBIUS_INTERNAL_PLUGIN_H

#include "data/value.h"
#include <mobius/mobius_plugin.h>
#include <stddef.h>

typedef MobiusPluginMetadata PluginMetadata;
typedef MobiusPluginFunction PluginFunction;
typedef MobiusPlugin         Plugin;
typedef MobiusPlugin*      (*PluginInfoFunc)(void);

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

#define MOBIUS_PLUGIN_FUNCTION(func_name, func_ptr, arg_count) \
    {func_name, func_ptr, arg_count}

// Function signature validation
bool validate_plugin_function_signature(PluginFunction* func);

// Plugin helper functions
const char* plugin_status_string(PluginStatus status);
void print_plugin_info(Plugin* plugin);
void print_plugin_functions(Plugin* plugin);

#endif // MOBIUS_INTERNAL_PLUGIN_H
