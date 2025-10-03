
#include "plugin/module_registry.h"
#include "util/utility.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <dirent.h>
#include <sys/stat.h>

// Global error message for the module registry
static char last_error[512] = {0};


// Global module registry (will be initialized by main.c)
static ModuleRegistry* global_registry = NULL;

void set_global_module_registry(ModuleRegistry* registry) {
    global_registry = registry;
}

ModuleRegistry* get_global_module_registry(void) {
    return global_registry;
}


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
    registry->plugin_directories = NULL;
    registry->plugin_dir_count = 0;
    registry->plugin_dir_capacity = 0;
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

// Add a plugin directory to the registry
bool add_plugin_directory(ModuleRegistry* registry, const char* directory) {
    if (!registry || !directory) {
        set_error("Invalid arguments to add_plugin_directory");
        return false;
    }
    
    // Check if we need to expand the array
    if (registry->plugin_dir_count >= registry->plugin_dir_capacity) {
        size_t new_capacity = registry->plugin_dir_capacity == 0 ? 4 : registry->plugin_dir_capacity * 2;
        char** new_dirs = realloc(registry->plugin_directories, new_capacity * sizeof(char*));
        if (!new_dirs) {
            set_error("Failed to allocate memory for plugin directories");
            return false;
        }
        registry->plugin_directories = new_dirs;
        registry->plugin_dir_capacity = new_capacity;
    }
    
    // Duplicate the directory string
    char* dir_copy = mobius_strdup(directory);
    if (!dir_copy) {
        set_error("Failed to allocate memory for directory string");
        return false;
    }
    
    registry->plugin_directories[registry->plugin_dir_count++] = dir_copy;
    return true;
}

// Clear all plugin directories
void clear_plugin_directories(ModuleRegistry* registry) {
    if (!registry) return;
    
    for (size_t i = 0; i < registry->plugin_dir_count; i++) {
        free(registry->plugin_directories[i]);
    }
    free(registry->plugin_directories);
    
    registry->plugin_directories = NULL;
    registry->plugin_dir_count = 0;
    registry->plugin_dir_capacity = 0;
}

// Free module registry and all loaded modules
void free_module_registry(ModuleRegistry* registry) {
    if (!registry) return;
    
    // Free plugin directories
    clear_plugin_directories(registry);
    
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
        // name is NULL for namespaced functions, so only free if not NULL
        if (registry->function_table[i].name) {
            free(registry->function_table[i].name);
        }
        free(registry->function_table[i].qualified_name);
    }
    free(registry->function_table);
    
    free(registry);
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
    union { void* obj; PluginInfoFunc func; } plugin_info_ptr;
    plugin_info_ptr.obj = dlsym(handle, "mobius_plugin_info");
    PluginInfoFunc get_plugin_info = plugin_info_ptr.func;
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
// Plugin functions are NAMESPACED - only accessible via qualified name (module.function)
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
    
    // NAMESPACE ENFORCEMENT: Don't store unqualified name
    // Plugin functions can ONLY be accessed via qualified name
    entry->name = NULL;
    
    // Create qualified name (module.function) - REQUIRED for access
    size_t qualified_len = strlen(module_name) + strlen(func->name) + 2;
    entry->qualified_name = malloc(qualified_len);
    if (!entry->qualified_name) {
        return false;
    }
    snprintf(entry->qualified_name, qualified_len, "%s.%s", module_name, func->name);
    
    entry->function = func;
    entry->module = find_module(registry, module_name);
    entry->is_namespaced = true;  // All plugin functions are namespaced
    
    registry->function_count++;
    return true;
}

// Lookup function by name (searches all modules)
// NAMESPACE ENFORCEMENT: This function returns NULL for all plugin functions
// Plugin functions MUST be accessed via qualified name (module.function)
// Only kept for backward compatibility - should not be used for plugins
PluginFunction* lookup_function(ModuleRegistry* registry, const char* name) {
    if (!registry || !name) return NULL;
    
    // Plugin functions are namespaced - they cannot be looked up by unqualified name
    // This function will always return NULL for plugin functions
    // Use lookup_qualified_function() instead
    
    (void)registry;  // Unused - plugin functions require namespace
    (void)name;      // Unused - plugin functions require namespace
    
    return NULL;  // All plugin functions require namespace
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
    printf("Note: All plugin functions require namespace (module.function)\n\n");
    
    for (size_t i = 0; i < registry->function_count; i++) {
        FunctionEntry* entry = &registry->function_table[i];
        // Show only qualified name since plugins are namespaced
        printf("🔧 %s\n", entry->qualified_name);
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

// ============================================================================
// PLUGIN DISCOVERY AND AUTO-LOADING
// ============================================================================

/**
 * Check if a filename has a .so extension
 */
static bool is_plugin_file(const char* filename) {
    if (!filename) return false;
    
    size_t len = strlen(filename);
    if (len < 4) return false;
    
    return strcmp(filename + len - 3, ".so") == 0;
}

/**
 * Scan a directory for plugin files and load them
 */
int scan_plugin_directory(ModuleRegistry* registry, const char* directory) {
    if (!registry || !directory) {
        set_error("Invalid arguments to scan_plugin_directory");
        return -1;
    }
    
    DIR* dir = opendir(directory);
    if (!dir) {
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "Failed to open directory: %s", directory);
        set_error(error_msg);
        return -1;
    }
    
    int loaded_count = 0;
    struct dirent* entry;
    
    if (registry->debug_mode) {
        printf("🔍 Scanning plugin directory: %s\n", directory);
    }
    
    while ((entry = readdir(dir)) != NULL) {
        // Skip . and .. entries
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        // Check if it's a plugin file
        if (!is_plugin_file(entry->d_name)) {
            continue;
        }
        
        // Build full path
        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", directory, entry->d_name);
        
        // Check if it's a regular file
        struct stat file_stat;
        if (stat(full_path, &file_stat) != 0 || !S_ISREG(file_stat.st_mode)) {
            continue;
        }
        
        if (registry->debug_mode) {
            printf("📦 Found plugin file: %s\n", entry->d_name);
        }
        
        // Try to load the plugin
        PluginLoadResult result = load_module(registry, full_path);
        if (result.status == PLUGIN_STATUS_LOADED) {
            loaded_count++;
            if (registry->debug_mode) {
                printf("✅ Loaded plugin: %s v%s\n", 
                       result.plugin->metadata.name, 
                       result.plugin->metadata.version);
            }
        } else {
            if (registry->debug_mode) {
                printf("❌ Failed to load %s: %s\n", 
                       entry->d_name, 
                       result.error_message ? result.error_message : "unknown error");
            }
        }
    }
    
    closedir(dir);
    
    if (registry->debug_mode) {
        printf("📊 Loaded %d plugins from %s\n", loaded_count, directory);
    }
    
    return loaded_count;
}


/**
 * Auto-load core modules from the default directory
 */
int auto_load_core_modules(ModuleRegistry* registry) {
    if (!registry) {
        set_error("Invalid registry for auto_load_core_modules");
        return -1;
    }
    
    const char* default_dirs[] = {
        "./bin/modules",
        "./modules", 
        "/usr/local/lib/mobius/modules",
        NULL
    };
    
    // Try user-specified directories first
    for (size_t i = 0; i < registry->plugin_dir_count; i++) {
        int count = scan_plugin_directory(registry, registry->plugin_directories[i]);
        if (count >= 0) {
            return count;
        }
    }
    
    // Try default directories
    for (int i = 0; default_dirs[i]; i++) {
        DIR* dir = opendir(default_dirs[i]);
        if (dir) {
            closedir(dir);
            int count = scan_plugin_directory(registry, default_dirs[i]);
            if (count >= 0) {
                return count;
            }
        }
    }
    
    set_error("No plugin directories found");
    return 0; // No plugins loaded, but not an error
}

/**
 * List available plugins in a directory
 */
char** list_available_plugins(const char* directory, size_t* count) {
    if (!directory || !count) {
        return NULL;
    }
    
    *count = 0;
    
    DIR* dir = opendir(directory);
    if (!dir) {
        return NULL;
    }
    
    // First pass: count plugin files
    struct dirent* entry;
    size_t plugin_count = 0;
    
    while ((entry = readdir(dir)) != NULL) {
        if (is_plugin_file(entry->d_name)) {
            plugin_count++;
        }
    }
    
    if (plugin_count == 0) {
        closedir(dir);
        return NULL;
    }
    
    // Allocate array for plugin names
    char** plugins = malloc(plugin_count * sizeof(char*));
    if (!plugins) {
        closedir(dir);
        return NULL;
    }
    
    // Second pass: collect plugin names
    rewinddir(dir);
    size_t index = 0;
    
    while ((entry = readdir(dir)) != NULL && index < plugin_count) {
        if (is_plugin_file(entry->d_name)) {
            plugins[index] = mobius_strdup(entry->d_name);
            if (plugins[index]) {
                index++;
            }
        }
    }
    
    closedir(dir);
    *count = index;
    return plugins;
}

/**
 * Load a module by name from the default plugin directory
 */
PluginLoadResult load_module_by_name(ModuleRegistry* registry, const char* name) {
    PluginLoadResult result;
    result.status = PLUGIN_STATUS_ERROR;
    result.plugin = NULL;
    result.error_message = "Invalid arguments";
    
    if (!registry || !name) {
        return result;
    }
    
    // Prepare directories to search (custom + defaults)
    const char* default_dirs[] = {
        "./bin/modules",
        "./modules", 
        "/usr/local/lib/mobius/modules",
        NULL
    };
    
    char full_path[1024];
    const char* module_filename;
    
    // Determine filename with .so extension
    if (strstr(name, ".so")) {
        module_filename = name;
    } else {
        static char temp_name[256];
        snprintf(temp_name, sizeof(temp_name), "%s.so", name);
        module_filename = temp_name;
    }
    
    // First try user-specified directories
    for (size_t i = 0; i < registry->plugin_dir_count; i++) {
        snprintf(full_path, sizeof(full_path), "%s/%s", registry->plugin_directories[i], module_filename);
        
        // Check if file exists
        struct stat file_stat;
        if (stat(full_path, &file_stat) == 0 && S_ISREG(file_stat.st_mode)) {
            result = load_module(registry, full_path);
            if (result.status == PLUGIN_STATUS_LOADED) {
                return result;
            }
        }
    }
    
    // Then try default directories
    for (int i = 0; default_dirs[i]; i++) {
        snprintf(full_path, sizeof(full_path), "%s/%s", default_dirs[i], module_filename);
        
        // Check if file exists
        struct stat file_stat;
        if (stat(full_path, &file_stat) == 0 && S_ISREG(file_stat.st_mode)) {
            result = load_module(registry, full_path);
            if (result.status == PLUGIN_STATUS_LOADED) {
                return result;
            }
        }
    }
    
    // Module not found in any directory
    result.status = PLUGIN_STATUS_ERROR;
    result.error_message = "Module not found in any configured directory";
    return result;
}

// is_module_loaded already implemented above

/**
 * Get error message for a specific module
 */
const char* get_module_error(ModuleRegistry* registry, const char* name) {
    LoadedModule* module = find_module(registry, name);
    return module ? module->error_message : "Module not found";
}

/**
 * Check if a function is available in the registry
 */
bool is_function_available(ModuleRegistry* registry, const char* name) {
    return lookup_function(registry, name) != NULL;
}
