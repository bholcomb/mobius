#ifndef MOBIUS_MODULE_REGISTRY_H
#define MOBIUS_MODULE_REGISTRY_H

#include "plugin.h"
#include <stdbool.h>

// Forward declarations
typedef struct LoadedModule LoadedModule;
typedef struct ModuleRegistry ModuleRegistry;

// Loaded module representation
struct LoadedModule {
    char* name;                 // Module name
    char* path;                 // File path to the module
    void* handle;               // dlopen handle (or HMODULE on Windows)
    Plugin* plugin;             // Plugin interface
    PluginStatus status;        // Current status
    const char* error_message;  // Error message if status is error
    LoadedModule* next;         // Linked list pointer
};

// Function lookup entry for fast resolution
typedef struct {
    char* name;                 // Function name
    char* qualified_name;       // Full name (module.function)
    PluginFunction* function;   // Function pointer
    LoadedModule* module;       // Source module
} FunctionEntry;

// Module registry - central plugin management
struct ModuleRegistry {
    LoadedModule* modules;      // Linked list of loaded modules
    size_t module_count;        // Number of loaded modules
    
    // Function lookup table for O(1) function resolution
    FunctionEntry* function_table;
    size_t function_count;
    size_t function_capacity;
    
    // Configuration
    char* plugin_directory;     // Default plugin directory
    bool auto_load_core;        // Auto-load core modules
    bool allow_unload;          // Allow module unloading
    bool debug_mode;            // Debug output
};

// Registry management
ModuleRegistry* create_module_registry(void);
void free_module_registry(ModuleRegistry* registry);
void set_plugin_directory(ModuleRegistry* registry, const char* directory);

// Module loading/unloading
PluginLoadResult load_module(ModuleRegistry* registry, const char* path);
PluginLoadResult load_module_by_name(ModuleRegistry* registry, const char* name);
bool unload_module(ModuleRegistry* registry, const char* name);
bool reload_module(ModuleRegistry* registry, const char* name);

// Plugin discovery
int scan_plugin_directory(ModuleRegistry* registry, const char* directory);
int auto_load_core_modules(ModuleRegistry* registry);
char** list_available_plugins(const char* directory, size_t* count);

// Function resolution
PluginFunction* lookup_function(ModuleRegistry* registry, const char* name);
PluginFunction* lookup_qualified_function(ModuleRegistry* registry, 
                                         const char* module_name, 
                                         const char* function_name);
bool is_function_available(ModuleRegistry* registry, const char* name);

// Module information
LoadedModule* find_module(ModuleRegistry* registry, const char* name);
bool is_module_loaded(ModuleRegistry* registry, const char* name);
const char* get_module_error(ModuleRegistry* registry, const char* name);

// Registry introspection
void print_loaded_modules(ModuleRegistry* registry);
void print_available_functions(ModuleRegistry* registry);
void print_module_functions(ModuleRegistry* registry, const char* module_name);

// Function table management (internal)
bool add_function_to_table(ModuleRegistry* registry, const char* module_name, PluginFunction* func);
void remove_functions_from_table(ModuleRegistry* registry, const char* module_name);
void rebuild_function_table(ModuleRegistry* registry);

// Namespace parsing utilities
bool parse_qualified_name(const char* full_name, char* module_name, char* function_name);
char* build_qualified_name(const char* module_name, const char* function_name);

// Error handling
const char* module_registry_get_last_error(void);
void module_registry_clear_error(void);

#endif // MOBIUS_MODULE_REGISTRY_H
