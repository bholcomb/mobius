#ifndef MOBIUS_MODULE_REGISTRY_H
#define MOBIUS_MODULE_REGISTRY_H

#include "plugin.h"
#include <stdbool.h>

// Forward declarations
typedef struct LoadedModule LoadedModule;

// Loaded module representation
struct LoadedModule {
    char* name;                 // Module name
    char* path;                 // File path to the module
    void* handle;               // dlopen handle (or HMODULE on Windows)
    Plugin* plugin;             // Plugin interface
    PluginStatus status;        // Current status
    const char* error_message;  // Error message if status is error
    int ref_count;              // Number of states using this module
    LoadedModule* next;         // Linked list pointer
};

// Module registry - central plugin management
typedef struct ModuleRegistry {
    LoadedModule* modules;      // Linked list of loaded modules
    size_t module_count;        // Number of loaded modules
    
    // Configuration
    char** plugin_directories;  // List of plugin directories
    size_t plugin_dir_count;    // Number of plugin directories
    size_t plugin_dir_capacity; // Capacity of plugin directories array
    bool debug_mode;            // Debug output
} ModuleRegistry;

// ============================================================================
// PUBLIC API - Used by evaluator and external code
// ============================================================================

// Module information
LoadedModule* find_module(ModuleRegistry* registry, const char* name);
bool is_module_loaded(ModuleRegistry* registry, const char* name);
PluginLoadResult load_module_by_name(ModuleRegistry* registry, const char* name);

// Registry introspection
void print_loaded_modules(ModuleRegistry* registry);

// ============================================================================
// GLOBAL PLUGIN SYSTEM API
// ============================================================================

/**
 * Add a directory to scan for plugins
 * Can be called before creating any states
 * @param path Directory path to scan for .so files
 */
void mobius_add_plugin_directory(const char* path);

/**
 * Remove all plugin directories
 */
void mobius_clear_plugin_directories(void);

/**
 * Manually trigger a plugin scan (optional - called automatically)
 * @param force_rescan If true, rescan even if already scanned
 * @return Number of modules discovered, -1 on error
 */
int mobius_scan_plugins(bool force_rescan);

/**
 * Get global module registry (for debugging/introspection)
 * Note: Registry is automatically created on first access and
 * cleaned up at process exit via atexit()
 * @return Global module registry
 */
struct ModuleRegistry* mobius_get_global_registry(void);

/**
 * Increment reference count for a module (called during import)
 * @param module_name Name of the module
 */
void mobius_plugin_increment_refcount(const char* module_name);

/**
 * Decrement reference count for a module (called when state is freed)
 * @param module_name Name of the module
 */
void mobius_plugin_decrement_refcount(const char* module_name);

#endif // MOBIUS_MODULE_REGISTRY_H
