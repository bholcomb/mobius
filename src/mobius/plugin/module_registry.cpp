
#include <mobius/mobius.h>
#include "plugin/module_registry.h"
#include "state/mobius_state.h"
#include "vm/vm.h"
#include "data/table.h"
#include "data/value.h"
#include "internal/string_intern.h"
#include "util/utility.h"
#include "util/file_io.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <mutex>
#include <libgen.h>
#include <dlfcn.h>
#include <sys/stat.h>

// ============================================================================
// Global singleton
// ============================================================================

static ModuleRegistry* global_registry = nullptr;
static std::once_flag registry_init_flag;

static void cleanup_global_registry() {
    delete global_registry;
    global_registry = nullptr;
}

ModuleRegistry* getGlobalRegistry() {
    std::call_once(registry_init_flag, []() {
        global_registry = new ModuleRegistry();
        atexit(cleanup_global_registry);
    });
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

void ModuleRegistry::registerBuiltinModule(const char* name, Table* module_table) {
    if (!name || !module_table) return;
    std::unique_lock<std::shared_mutex> lock(registry_mutex_);
    module_tables_[name] = module_table;
}

void ModuleRegistry::addPluginDirectory(const char* directory) {
    if (!directory) return;
    std::unique_lock<std::shared_mutex> lock(registry_mutex_);
    plugin_directories_.emplace_back(directory);
}

void ModuleRegistry::clearPluginDirectories() {
    std::unique_lock<std::shared_mutex> lock(registry_mutex_);
    plugin_directories_.clear();
}

// ============================================================================
// Plugin loading (dlopen + init_plugin)
// ============================================================================

PluginLoadResult ModuleRegistry::loadPlugin(const char* path, MobiusState* state) {
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

    if (plugin->init_plugin && plugin->init_plugin(state) != 0) {
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

// ============================================================================
// Module resolution — lazy load, cache by name
// ============================================================================

Table* ModuleRegistry::resolveModule(const char* name, const char* caller_source, MobiusState* state) {
    if (!name || !state) return nullptr;

    {
        std::shared_lock<std::shared_mutex> rlock(registry_mutex_);
        auto it = module_tables_.find(name);
        if (it != module_tables_.end()) {
            return it->second;
        }
    }

    std::unique_lock<std::shared_mutex> wlock(registry_mutex_);

    auto it = module_tables_.find(name);
    if (it != module_tables_.end()) {
        return it->second;
    }

    const char* default_dirs[] = {
        "./modules",
        "./bin/modules",
        "/usr/local/lib/mobius/modules",
        nullptr
    };

    std::string mob_filename = std::string(name) + ".mob";
    std::string so_filename  = std::string(name) + ".so";

    std::vector<std::string> search_dirs;

    if (caller_source && caller_source[0] != '\0'
        && strcmp(caller_source, "<string>") != 0) {
        char* buf = strdup(caller_source);
        if (buf) {
            char* dir = dirname(buf);
            if (dir && dir[0] != '\0') {
                search_dirs.emplace_back(dir);
            }
            free(buf);
        }
    }

    for (const auto& d : plugin_directories_) {
        search_dirs.push_back(d);
    }

    for (int i = 0; default_dirs[i]; i++) {
        search_dirs.emplace_back(default_dirs[i]);
    }

    for (const auto& dir : search_dirs) {
        char mob_path[1024], so_path[1024];
        snprintf(mob_path, sizeof(mob_path), "%s/%s", dir.c_str(), mob_filename.c_str());
        snprintf(so_path,  sizeof(so_path),  "%s/%s", dir.c_str(), so_filename.c_str());

        struct stat st_mob, st_so;
        bool has_mob = (stat(mob_path, &st_mob) == 0 && S_ISREG(st_mob.st_mode));
        bool has_so  = (stat(so_path,  &st_so)  == 0 && S_ISREG(st_so.st_mode));

        if (!has_mob && !has_so) continue;

        Table* mod_table = new (std::nothrow) Table(state, 16);
        if (!mod_table) return nullptr;

        if (has_so) {
            LoadedModule* lm = findModule(name);
            if (!lm) {
                PluginLoadResult result = loadPlugin(so_path, state);
                if (result.status == PLUGIN_STATUS_LOADED && result.plugin) {
                    lm = findModule(result.plugin->metadata.name);
                }
            }
            if (lm && lm->plugin) {
                for (size_t i = 0; i < lm->plugin->function_count; i++) {
                    PluginFunction* func = &lm->plugin->functions[i];
                    if (!func || !func->name || !func->function) continue;
                    Value func_key = make_string_value_from_cstr(state, func->name);
                    Value func_val = make_native_function_value(func->function);
                    mod_table->set(func_key, func_val);
                }

                if (lm->plugin->post_init) {
                    // Prevent the scratch Value from dropping the last
                    // reference when it goes out of scope.
                    mod_table->retain();

                    Value scratch[32];
                    scratch[0] = make_table_value(mod_table);
                    NativeCallContext nctx;
                    nctx.registers = scratch;
                    nctx.base      = 0;
                    nctx.top       = 1;
                    nctx.capacity  = 32;

                    MobiusVM* vm = MobiusVM::t_current_vm;
                    NativeCallContext* prev_nctx = vm ? vm->native_ctx_ : nullptr;
                    if (vm) vm->native_ctx_ = &nctx;

                    lm->plugin->post_init(state);

                    if (vm) vm->native_ctx_ = prev_nctx;
                }
            }
        }

        if (has_mob) {
            int snapshot = state->globalSlotCount();
            int rc = state->execFile(mob_path);
            if (rc != 0) {
                state->removeGlobalSlots(snapshot);
                if (!has_so) {
                    delete mod_table;
                    return nullptr;
                }
            } else {
                int new_count = state->globalSlotCount();
                for (int i = snapshot; i < new_count; i++) {
                    const Value& val = state->globalSlot(i);
                    if (!(val.flags & VAL_FLAG_DEFINED)) continue;
                    const char* slot_name = state->globalSlotName(i);
                    if (!slot_name || strcmp(slot_name, "<unknown>") == 0) continue;
                    Value key = make_string_value_from_cstr(state, slot_name);
                    mod_table->set(key, val);
                }
                state->removeGlobalSlots(snapshot);
            }
        }

        module_tables_[name] = mod_table;
        return mod_table;
    }

    return nullptr;
}
