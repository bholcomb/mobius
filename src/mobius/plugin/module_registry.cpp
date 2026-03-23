
#include <mobius/mobius.h>
#include "plugin/module_registry.h"
#include "util/utility.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <dlfcn.h>
#include <dirent.h>
#include <sys/stat.h>

// ============================================================================
// Global singleton
// ============================================================================

static ModuleRegistry* global_registry = nullptr;
static bool cleanup_registered = false;

static void cleanup_global_registry() {
    delete global_registry;
    global_registry = nullptr;
}

ModuleRegistry* getGlobalRegistry() {
    if (!global_registry) {
        global_registry = new ModuleRegistry();
        if (!cleanup_registered) {
            atexit(cleanup_global_registry);
            cleanup_registered = true;
        }
    }
    return global_registry;
}

// ============================================================================
// Public C API wrappers
// ============================================================================

extern "C" {

void mobius_add_plugin_directory(const char* path) {
    if (!path) return;
    getGlobalRegistry()->addPluginDirectory(path);
}

void mobius_clear_plugin_directories() {
    getGlobalRegistry()->clearPluginDirectories();
}

int mobius_scan_plugins(bool force_rescan) {
    return getGlobalRegistry()->scanPlugins(force_rescan);
}

} // extern "C"

// ============================================================================
// ModuleRegistry implementation
// ============================================================================

ModuleRegistry::~ModuleRegistry() {
    for (auto& mod : modules_) {
        if (mod.plugin && mod.plugin->cleanup_plugin) {
            mod.plugin->cleanup_plugin();
        }
        if (mod.handle) {
            dlclose(mod.handle);
        }
    }
}

LoadedModule* ModuleRegistry::findModule(const char* name) {
    if (!name) return nullptr;
    for (auto& mod : modules_) {
        if (mod.name == name) {
            return &mod;
        }
    }
    return nullptr;
}

bool ModuleRegistry::isModuleLoaded(const char* name) {
    return findModule(name) != nullptr;
}

void ModuleRegistry::printLoadedModules() const {
    printf("Loaded Modules (%zu):\n", modules_.size());
    printf("====================================\n");
    for (const auto& mod : modules_) {
        printf("  %s v%s\n", mod.name.c_str(), mod.plugin->metadata.version);
        printf("   Description: %s\n", mod.plugin->metadata.description);
        printf("   Functions: %zu\n", mod.plugin->function_count);
        printf("   Path: %s\n", mod.path.c_str());
        printf("\n");
    }
}

void ModuleRegistry::addPluginDirectory(const char* directory) {
    if (!directory) return;
    plugin_directories_.emplace_back(directory);
}

void ModuleRegistry::clearPluginDirectories() {
    plugin_directories_.clear();
}

int ModuleRegistry::scanPlugins(bool force_rescan) {
    if (!force_rescan && already_scanned_) {
        return 0;
    }

    int total_discovered = 0;
    for (const auto& dir : plugin_directories_) {
        int count = scanPluginDirectory(dir.c_str());
        if (count > 0) total_discovered += count;
    }

    already_scanned_ = true;
    return total_discovered;
}

void ModuleRegistry::incrementRefCount(const char* module_name) {
    LoadedModule* mod = findModule(module_name);
    if (mod) {
        mod->ref_count++;
    }
}

void ModuleRegistry::decrementRefCount(const char* module_name) {
    LoadedModule* mod = findModule(module_name);
    if (mod && mod->ref_count > 0) {
        mod->ref_count--;
    }
}

// ============================================================================
// Private helpers
// ============================================================================

bool ModuleRegistry::isPluginFile(const char* filename) {
    if (!filename) return false;
    size_t len = strlen(filename);
    if (len < 4) return false;
    return strcmp(filename + len - 3, ".so") == 0;
}

PluginLoadResult ModuleRegistry::loadModule(const char* path) {
    PluginLoadResult result = {PLUGIN_STATUS_ERROR, nullptr, nullptr};

    if (!path) {
        result.error_message = "Invalid path";
        return result;
    }

    if (debug_mode_) {
        printf("Loading plugin: %s\n", path);
    }

    void* handle = dlopen(path, RTLD_LAZY);
    if (!handle) {
        last_error_ = std::string("Failed to load library: ") + dlerror();
        result.error_message = last_error_.c_str();
        return result;
    }

    union { void* obj; PluginInfoFunc func; } plugin_info_ptr;
    plugin_info_ptr.obj = dlsym(handle, "mobius_plugin_info");
    PluginInfoFunc get_plugin_info = plugin_info_ptr.func;
    if (!get_plugin_info) {
        dlclose(handle);
        last_error_ = "Plugin does not export mobius_plugin_info function";
        result.error_message = last_error_.c_str();
        return result;
    }

    Plugin* plugin = get_plugin_info();
    if (!plugin) {
        dlclose(handle);
        last_error_ = "Plugin returned NULL from mobius_plugin_info";
        result.error_message = last_error_.c_str();
        return result;
    }

    if (plugin->metadata.api_version != MOBIUS_PLUGIN_API_VERSION) {
        dlclose(handle);
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "Plugin API version mismatch: expected %d, got %zu",
                 MOBIUS_PLUGIN_API_VERSION, plugin->metadata.api_version);
        last_error_ = buf;
        result.status = PLUGIN_STATUS_INCOMPATIBLE;
        result.error_message = last_error_.c_str();
        return result;
    }

    if (findModule(plugin->metadata.name)) {
        dlclose(handle);
        last_error_ = std::string("Module '") + plugin->metadata.name + "' is already loaded";
        result.error_message = last_error_.c_str();
        return result;
    }

    if (plugin->init_plugin && plugin->init_plugin() != 0) {
        dlclose(handle);
        last_error_ = "Plugin initialization failed";
        result.error_message = last_error_.c_str();
        return result;
    }

    LoadedModule mod;
    mod.name = plugin->metadata.name;
    mod.path = path;
    mod.handle = handle;
    mod.plugin = plugin;
    mod.status = PLUGIN_STATUS_LOADED;
    mod.ref_count = 0;
    modules_.push_back(std::move(mod));

    if (debug_mode_) {
        printf("Successfully loaded plugin '%s' v%s (%zu functions)\n",
               plugin->metadata.name, plugin->metadata.version, plugin->function_count);
    }

    result.status = PLUGIN_STATUS_LOADED;
    result.error_message = nullptr;
    result.plugin = plugin;
    last_error_.clear();
    return result;
}

int ModuleRegistry::scanPluginDirectory(const char* directory) {
    if (!directory) return -1;

    DIR* dir = opendir(directory);
    if (!dir) {
        last_error_ = std::string("Failed to open directory: ") + directory;
        return -1;
    }

    int loaded_count = 0;
    struct dirent* entry;

    if (debug_mode_) {
        printf("Scanning plugin directory: %s\n", directory);
    }

    while ((entry = readdir(dir)) != nullptr) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        if (!isPluginFile(entry->d_name)) {
            continue;
        }

        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", directory, entry->d_name);

        struct stat file_stat;
        if (stat(full_path, &file_stat) != 0 || !S_ISREG(file_stat.st_mode)) {
            continue;
        }

        if (debug_mode_) {
            printf("Found plugin file: %s\n", entry->d_name);
        }

        PluginLoadResult result = loadModule(full_path);
        if (result.status == PLUGIN_STATUS_LOADED) {
            loaded_count++;
            if (debug_mode_) {
                printf("Loaded plugin: %s v%s\n",
                       result.plugin->metadata.name,
                       result.plugin->metadata.version);
            }
        } else if (debug_mode_) {
            printf("Failed to load %s: %s\n",
                   entry->d_name,
                   result.error_message ? result.error_message : "unknown error");
        }
    }

    closedir(dir);

    if (debug_mode_) {
        printf("Loaded %d plugins from %s\n", loaded_count, directory);
    }

    return loaded_count;
}

PluginLoadResult ModuleRegistry::loadModuleByName(const char* name) {
    PluginLoadResult result;
    result.status = PLUGIN_STATUS_ERROR;
    result.plugin = nullptr;
    result.error_message = "Invalid arguments";

    if (!name) return result;

    const char* default_dirs[] = {
        "./bin/modules",
        "./modules",
        "/usr/local/lib/mobius/modules",
        nullptr
    };

    char full_path[1024];

    std::string module_filename;
    if (strstr(name, ".so")) {
        module_filename = name;
    } else {
        module_filename = std::string(name) + ".so";
    }

    for (const auto& dir : plugin_directories_) {
        snprintf(full_path, sizeof(full_path), "%s/%s", dir.c_str(), module_filename.c_str());
        struct stat file_stat;
        if (stat(full_path, &file_stat) == 0 && S_ISREG(file_stat.st_mode)) {
            result = loadModule(full_path);
            if (result.status == PLUGIN_STATUS_LOADED) {
                return result;
            }
        }
    }

    for (int i = 0; default_dirs[i]; i++) {
        snprintf(full_path, sizeof(full_path), "%s/%s", default_dirs[i], module_filename.c_str());
        struct stat file_stat;
        if (stat(full_path, &file_stat) == 0 && S_ISREG(file_stat.st_mode)) {
            result = loadModule(full_path);
            if (result.status == PLUGIN_STATUS_LOADED) {
                return result;
            }
        }
    }

    result.status = PLUGIN_STATUS_ERROR;
    result.error_message = "Module not found in any configured directory";
    return result;
}
