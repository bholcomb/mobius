
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

// Global module registry singleton
static ModuleRegistry* global_registry = NULL;
static bool cleanup_registered = false;
static bool already_scanned = false;

// Forward declarations for internal functions
static ModuleRegistry* create_module_registry(void);
static void free_module_registry(ModuleRegistry* registry);

// ============================================================================
// GLOBAL REGISTRY MANAGEMENT
// ============================================================================

// Automatic cleanup (registered with atexit)
static void cleanup_global_registry(void) {
    if (global_registry) {
        free_module_registry(global_registry);
        global_registry = NULL;
        already_scanned = false;
    }
}

// Get or create global registry (lazy initialization)
static ModuleRegistry* get_or_create_global_registry(void) {
    if (!global_registry) {
        global_registry = create_module_registry();
        
        // Register cleanup automatically on first use
        if (!cleanup_registered && global_registry) {
            atexit(cleanup_global_registry);
            cleanup_registered = true;
        }
    }
    
    return global_registry;
}

// Public accessor - gets or creates global registry
ModuleRegistry* mobius_get_global_registry(void) {
    return get_or_create_global_registry();
}

// Deprecated compatibility functions
void set_global_module_registry(ModuleRegistry* registry) {
    global_registry = registry;
}

ModuleRegistry* get_global_module_registry(void) {
    return get_or_create_global_registry();
}


// ============================================================================
// INTERNAL HELPER FUNCTIONS
// ============================================================================

// Set error message
static void set_error(const char* message) {
    snprintf(last_error, sizeof(last_error), "%s", message);
}

// Clear error message
static void module_registry_clear_error(void) {
    last_error[0] = '\0';
}

// Get last error message (unused, but kept for potential debugging)
static const char* module_registry_get_last_error(void) {
    return last_error[0] ? last_error : NULL;
}

// Create a new module registry (internal - called by get_or_create_global_registry)
static ModuleRegistry* create_module_registry(void) {
    ModuleRegistry* registry = malloc(sizeof(ModuleRegistry));
    if (!registry) {
        set_error("Failed to allocate memory for module registry");
        return NULL;
    }
    
    registry->modules = NULL;
    registry->module_count = 0;
    registry->plugin_directories = NULL;
    registry->plugin_dir_count = 0;
    registry->plugin_dir_capacity = 0;
    registry->debug_mode = false;
    
    module_registry_clear_error();
    return registry;
}

// Add a plugin directory to the registry (internal)
static bool add_plugin_directory(ModuleRegistry* registry, const char* directory) {
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

// Clear all plugin directories (internal)
static void clear_plugin_directories(ModuleRegistry* registry) {
    if (!registry) return;
    
    for (size_t i = 0; i < registry->plugin_dir_count; i++) {
        free(registry->plugin_directories[i]);
    }
    free(registry->plugin_directories);
    
    registry->plugin_directories = NULL;
    registry->plugin_dir_count = 0;
    registry->plugin_dir_capacity = 0;
}

// Free module registry and all loaded modules (internal - called by atexit)
static void free_module_registry(ModuleRegistry* registry) {
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
    
    free(registry);
}


// Load a module from a file path (internal - used by load_module_by_name)
static PluginLoadResult load_module(ModuleRegistry* registry, const char* path) {
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
    module->ref_count = 0;  // Initialize reference count
    module->next = registry->modules;
    
    // Add to registry
    registry->modules = module;
    registry->module_count++;
    
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

// ============================================================================
// PUBLIC API IMPLEMENTATION
// ============================================================================

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
 * Scan a directory for plugin files and load them (internal)
 */
static int scan_plugin_directory(ModuleRegistry* registry, const char* directory) {
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
 * Load a module by name from configured and default plugin directories
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

// ============================================================================
// GLOBAL PLUGIN SYSTEM API IMPLEMENTATION
// ============================================================================

/**
 * Add a directory to scan for plugins
 */
void mobius_add_plugin_directory(const char* path) {
    if (!path) return;
    
    ModuleRegistry* registry = get_or_create_global_registry();
    if (!registry) return;
    
    add_plugin_directory(registry, path);
}

/**
 * Remove all plugin directories
 */
void mobius_clear_plugin_directories(void) {
    ModuleRegistry* registry = get_or_create_global_registry();
    if (!registry) return;
    
    clear_plugin_directories(registry);
}

/**
 * Scan for available plugins
 */
int mobius_scan_plugins(bool force_rescan) {
    if (!force_rescan && already_scanned) {
        return 0;  // Already scanned, no need to rescan
    }
    
    ModuleRegistry* registry = get_or_create_global_registry();
    if (!registry) return -1;
    
    // If rescanning, we don't unload modules, just rescan directories
    // This allows discovering new plugins without affecting loaded ones
    
    int total_discovered = 0;
    
    // Scan all configured directories
    for (size_t i = 0; i < registry->plugin_dir_count; i++) {
        int count = scan_plugin_directory(registry, registry->plugin_directories[i]);
        if (count > 0) total_discovered += count;
    }
    
    already_scanned = true;
    return total_discovered;
}

/**
 * Increment reference count for a module
 */
void mobius_plugin_increment_refcount(const char* module_name) {
    if (!module_name) return;
    
    ModuleRegistry* registry = get_or_create_global_registry();
    if (!registry) return;
    
    LoadedModule* module = find_module(registry, module_name);
    if (module) {
        module->ref_count++;
    }
}

/**
 * Decrement reference count for a module
 */
void mobius_plugin_decrement_refcount(const char* module_name) {
    if (!module_name) return;
    
    ModuleRegistry* registry = get_or_create_global_registry();
    if (!registry) return;
    
    LoadedModule* module = find_module(registry, module_name);
    if (module && module->ref_count > 0) {
        module->ref_count--;
        
        // For now: keep loaded until process exit
        // Future: If ref_count == 0 and hot reload active, could unload
    }
}
