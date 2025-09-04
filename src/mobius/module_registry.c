#include "module_registry.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <dirent.h>
#include <sys/stat.h>

// Global error message for the module registry
static char last_error[512] = {0};

// Set error message
static void set_error(const char* message) {
    snprintf(last_error, sizeof(last_error), "%s", message);
}

// Clear error message
void module_registry_clear_error(void) {
    last_error[0] = '\0';
}

// Get last error message
const char* module_registry_get_last_error(void) {
    return last_error[0] ? last_error : NULL;
}

// Create a new module registry
ModuleRegistry* create_module_registry(void) {
    ModuleRegistry* registry = malloc(sizeof(ModuleRegistry));
    if (!registry) {
        set_error("Failed to allocate memory for module registry");
        return NULL;
    }
    
    registry->modules = NULL;
    registry->module_count = 0;
    registry->function_table = malloc(64 * sizeof(FunctionEntry));
    registry->function_count = 0;
    registry->function_capacity = 64;
    registry->plugin_directory = NULL;
    registry->auto_load_core = true;
    registry->allow_unload = true;
    registry->debug_mode = false;
    
    if (!registry->function_table) {
        free(registry);
        set_error("Failed to allocate memory for function table");
        return NULL;
    }
    
    module_registry_clear_error();
    return registry;
}

// Free module registry and all loaded modules
void free_module_registry(ModuleRegistry* registry) {
    if (!registry) return;
    
    // Unload all modules
    LoadedModule* module = registry->modules;
    while (module) {
        LoadedModule* next = module->next;
        
        // Call cleanup function if available
        if (module->plugin && module->plugin->cleanup_plugin) {
            module->plugin->cleanup_plugin();
        }
        
        // Close dynamic library
        if (module->handle) {
            dlclose(module->handle);
        }
        
        free(module->name);
        free(module->path);
        free(module);
        module = next;
    }
    
    // Free function table
    for (size_t i = 0; i < registry->function_count; i++) {
        free(registry->function_table[i].name);
        free(registry->function_table[i].qualified_name);
    }
    free(registry->function_table);
    
    free(registry->plugin_directory);
    free(registry);
}

// Set plugin directory
void set_plugin_directory(ModuleRegistry* registry, const char* directory) {
    if (!registry) return;
    
    free(registry->plugin_directory);
    registry->plugin_directory = malloc(strlen(directory) + 1);
    if (registry->plugin_directory) {
        strcpy(registry->plugin_directory, directory);
    }
}

// Load a module from a file path
PluginLoadResult load_module(ModuleRegistry* registry, const char* path) {
    PluginLoadResult result = {PLUGIN_STATUS_ERROR, NULL, NULL};
    
    if (!registry || !path) {
        result.error_message = "Invalid registry or path";
        return result;
    }
    
    if (registry->debug_mode) {
        printf("Loading plugin: %s\n", path);
    }
    
    // Open the dynamic library
    void* handle = dlopen(path, RTLD_LAZY);
    if (!handle) {
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "Failed to load library: %s", dlerror());
        set_error(error_msg);
        result.error_message = module_registry_get_last_error();
        return result;
    }
    
    // Find the plugin info function
    PluginInfoFunc get_plugin_info = (PluginInfoFunc)dlsym(handle, "mobius_plugin_info");
    if (!get_plugin_info) {
        dlclose(handle);
        set_error("Plugin does not export mobius_plugin_info function");
        result.error_message = module_registry_get_last_error();
        return result;
    }
    
    // Get plugin information
    Plugin* plugin = get_plugin_info();
    if (!plugin) {
        dlclose(handle);
        set_error("Plugin returned NULL from mobius_plugin_info");
        result.error_message = module_registry_get_last_error();
        return result;
    }
    
    // Check API version compatibility
    if (plugin->metadata.api_version != MOBIUS_PLUGIN_API_VERSION) {
        dlclose(handle);
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), 
                "Plugin API version mismatch: expected %d, got %zu",
                MOBIUS_PLUGIN_API_VERSION, plugin->metadata.api_version);
        set_error(error_msg);
        result.status = PLUGIN_STATUS_INCOMPATIBLE;
        result.error_message = module_registry_get_last_error();
        return result;
    }
    
    // Check if module is already loaded
    if (find_module(registry, plugin->metadata.name)) {
        dlclose(handle);
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Module '%s' is already loaded", plugin->metadata.name);
        set_error(error_msg);
        result.error_message = module_registry_get_last_error();
        return result;
    }
    
    // Initialize plugin if it has an init function
    if (plugin->init_plugin && plugin->init_plugin() != 0) {
        dlclose(handle);
        set_error("Plugin initialization failed");
        result.error_message = module_registry_get_last_error();
        return result;
    }
    
    // Create loaded module entry
    LoadedModule* module = malloc(sizeof(LoadedModule));
    if (!module) {
        if (plugin->cleanup_plugin) plugin->cleanup_plugin();
        dlclose(handle);
        set_error("Failed to allocate memory for module");
        result.error_message = module_registry_get_last_error();
        return result;
    }
    
    module->name = malloc(strlen(plugin->metadata.name) + 1);
    module->path = malloc(strlen(path) + 1);
    if (!module->name || !module->path) {
        free(module->name);
        free(module->path);
        free(module);
        if (plugin->cleanup_plugin) plugin->cleanup_plugin();
        dlclose(handle);
        set_error("Failed to allocate memory for module strings");
        result.error_message = module_registry_get_last_error();
        return result;
    }
    
    strcpy(module->name, plugin->metadata.name);
    strcpy(module->path, path);
    module->handle = handle;
    module->plugin = plugin;
    module->status = PLUGIN_STATUS_LOADED;
    module->error_message = NULL;
    module->next = registry->modules;
    
    // Add to registry
    registry->modules = module;
    registry->module_count++;
    
    // Add functions to lookup table
    for (size_t i = 0; i < plugin->function_count; i++) {
        add_function_to_table(registry, plugin->metadata.name, &plugin->functions[i]);
    }
    
    if (registry->debug_mode) {
        printf("Successfully loaded plugin '%s' v%s (%zu functions)\n", 
               plugin->metadata.name, plugin->metadata.version, plugin->function_count);
    }
    
    result.status = PLUGIN_STATUS_LOADED;
    result.error_message = NULL;
    result.plugin = plugin;
    module_registry_clear_error();
    return result;
}

// Find a module by name
LoadedModule* find_module(ModuleRegistry* registry, const char* name) {
    if (!registry || !name) return NULL;
    
    LoadedModule* module = registry->modules;
    while (module) {
        if (strcmp(module->name, name) == 0) {
            return module;
        }
        module = module->next;
    }
    return NULL;
}

// Check if a module is loaded
bool is_module_loaded(ModuleRegistry* registry, const char* name) {
    return find_module(registry, name) != NULL;
}

// Add function to lookup table
bool add_function_to_table(ModuleRegistry* registry, const char* module_name, PluginFunction* func) {
    if (!registry || !module_name || !func) return false;
    
    // Resize table if needed
    if (registry->function_count >= registry->function_capacity) {
        size_t new_capacity = registry->function_capacity * 2;
        FunctionEntry* new_table = realloc(registry->function_table, 
                                          new_capacity * sizeof(FunctionEntry));
        if (!new_table) return false;
        
        registry->function_table = new_table;
        registry->function_capacity = new_capacity;
    }
    
    FunctionEntry* entry = &registry->function_table[registry->function_count];
    
    // Allocate and copy function name
    entry->name = malloc(strlen(func->name) + 1);
    if (!entry->name) return false;
    strcpy(entry->name, func->name);
    
    // Create qualified name (module.function)
    size_t qualified_len = strlen(module_name) + strlen(func->name) + 2;
    entry->qualified_name = malloc(qualified_len);
    if (!entry->qualified_name) {
        free(entry->name);
        return false;
    }
    snprintf(entry->qualified_name, qualified_len, "%s.%s", module_name, func->name);
    
    entry->function = func;
    entry->module = find_module(registry, module_name);
    
    registry->function_count++;
    return true;
}

// Lookup function by name (searches all modules)
PluginFunction* lookup_function(ModuleRegistry* registry, const char* name) {
    if (!registry || !name) return NULL;
    
    for (size_t i = 0; i < registry->function_count; i++) {
        if (strcmp(registry->function_table[i].name, name) == 0) {
            return registry->function_table[i].function;
        }
    }
    return NULL;
}

// Lookup qualified function (module.function)
PluginFunction* lookup_qualified_function(ModuleRegistry* registry, 
                                         const char* module_name, 
                                         const char* function_name) {
    if (!registry || !module_name || !function_name) return NULL;
    
    char qualified_name[256];
    snprintf(qualified_name, sizeof(qualified_name), "%s.%s", module_name, function_name);
    
    for (size_t i = 0; i < registry->function_count; i++) {
        if (strcmp(registry->function_table[i].qualified_name, qualified_name) == 0) {
            return registry->function_table[i].function;
        }
    }
    return NULL;
}

// Parse qualified name (module.function)
bool parse_qualified_name(const char* full_name, char* module_name, char* function_name) {
    if (!full_name || !module_name || !function_name) return false;
    
    const char* dot = strchr(full_name, '.');
    if (!dot) return false;
    
    size_t module_len = dot - full_name;
    strncpy(module_name, full_name, module_len);
    module_name[module_len] = '\0';
    
    strcpy(function_name, dot + 1);
    return true;
}

// Print loaded modules
void print_loaded_modules(ModuleRegistry* registry) {
    if (!registry) return;
    
    printf("Loaded Modules (%zu):\n", registry->module_count);
    printf("====================================\n");
    
    LoadedModule* module = registry->modules;
    while (module) {
        printf("📦 %s v%s\n", module->name, module->plugin->metadata.version);
        printf("   Description: %s\n", module->plugin->metadata.description);
        printf("   Functions: %zu\n", module->plugin->function_count);
        printf("   Path: %s\n", module->path);
        printf("\n");
        module = module->next;
    }
}

// Print available functions
void print_available_functions(ModuleRegistry* registry) {
    if (!registry) return;
    
    printf("Available Functions (%zu):\n", registry->function_count);
    printf("=====================================\n");
    
    for (size_t i = 0; i < registry->function_count; i++) {
        FunctionEntry* entry = &registry->function_table[i];
        printf("🔧 %s (%s)\n", entry->name, entry->qualified_name);
        if (entry->function->description) {
            printf("   %s\n", entry->function->description);
        }
        if (entry->function->category) {
            printf("   Category: %s\n", entry->function->category);
        }
        printf("\n");
    }
}

// Plugin status to string
const char* plugin_status_string(PluginStatus status) {
    switch (status) {
        case PLUGIN_STATUS_UNLOADED: return "unloaded";
        case PLUGIN_STATUS_LOADED: return "loaded";
        case PLUGIN_STATUS_ERROR: return "error";
        case PLUGIN_STATUS_INCOMPATIBLE: return "incompatible";
        default: return "unknown";
    }
}

// Print plugin information
void print_plugin_info(Plugin* plugin) {
    if (!plugin) return;
    
    printf("Plugin Information:\n");
    printf("===================\n");
    printf("Name: %s\n", plugin->metadata.name);
    printf("Version: %s\n", plugin->metadata.version);
    printf("Description: %s\n", plugin->metadata.description);
    printf("Author: %s\n", plugin->metadata.author);
    printf("API Version: %zu\n", plugin->metadata.api_version);
    printf("Functions: %zu\n", plugin->function_count);
    printf("\n");
}
