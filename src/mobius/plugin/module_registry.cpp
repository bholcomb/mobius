
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
#include <thread>
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

void mobius_add_plugin_directory(MobiusState* state, const char* path) {
    if (!state || !path) return;
    state->addPluginDirectory(path);
}

void mobius_clear_plugin_directories(MobiusState* state) {
    if (!state) return;
    state->clearPluginDirectories();
}

} // extern "C"

namespace {

thread_local std::vector<std::string> g_module_load_stack;

struct ModulePaths {
    bool found = false;
    bool has_mob = false;
    bool has_so = false;
    std::string mob_path;
    std::string so_path;
};

static ModulePaths resolve_module_paths(const char* name, const char* caller_source, MobiusState* state) {
    ModulePaths out;
    const char* default_dirs[] = {
        "./modules",
        "./bin/modules",
        "/usr/local/lib/mobius/modules",
        nullptr
    };

    std::string mob_filename = std::string(name) + ".mob";
    std::string so_filename  = std::string(name) + ".so";
    std::vector<std::string> search_dirs;

    if (caller_source && caller_source[0] != '\0' && strcmp(caller_source, "<string>") != 0) {
        char* buf = strdup(caller_source);
        if (buf) {
            char* dir = dirname(buf);
            if (dir && dir[0] != '\0') search_dirs.emplace_back(dir);
            free(buf);
        }
    }

    for (const auto& d : state->pluginDirectories()) search_dirs.push_back(d);
    for (int i = 0; default_dirs[i]; i++) search_dirs.emplace_back(default_dirs[i]);

    for (const auto& dir : search_dirs) {
        std::string mob_path = dir + "/" + mob_filename;
        std::string so_path = dir + "/" + so_filename;

        struct stat st_mob, st_so;
        bool has_mob = (stat(mob_path.c_str(), &st_mob) == 0 && S_ISREG(st_mob.st_mode));
        bool has_so  = (stat(so_path.c_str(),  &st_so)  == 0 && S_ISREG(st_so.st_mode));
        if (!has_mob && !has_so) continue;

        out.found = true;
        out.has_mob = has_mob;
        out.has_so = has_so;
        if (has_mob) out.mob_path = std::move(mob_path);
        if (has_so) out.so_path = std::move(so_path);
        break;
    }

    return out;
}

static std::string format_import_cycle_error(const std::string& module_name) {
    std::string message = "Import cycle detected: ";
    bool found_start = false;
    for (const std::string& name : g_module_load_stack) {
        if (!found_start && name == module_name) found_start = true;
        if (!found_start) continue;
        if (message.back() != ' ') message += " -> ";
        message += name;
    }
    if (message.back() != ' ') message += " -> ";
    message += module_name;
    return message;
}

static void run_plugin_post_init(Plugin* plugin, Table* mod_table, MobiusState* state) {
    if (!plugin || !plugin->post_init || !mod_table) return;

    Value scratch[32];
    mod_table->retain();
    scratch[0] = make_table_value(mod_table);
    MobiusVM* vm = MobiusVM::t_current_vm;
    if (vm) {
        int saved_base = vm->native_ctx_.base;
        int saved_top  = vm->native_ctx_.top;
        Value* saved_regs = vm->native_ctx_.registers;
        int saved_cap  = vm->native_ctx_.capacity;

        vm->native_ctx_.registers = scratch;
        vm->native_ctx_.base      = 0;
        vm->native_ctx_.top       = 1;
        vm->native_ctx_.capacity  = 32;

        plugin->post_init(state);

        vm->native_ctx_.registers = saved_regs;
        vm->native_ctx_.base      = saved_base;
        vm->native_ctx_.top       = saved_top;
        vm->native_ctx_.capacity  = saved_cap;
    } else {
        plugin->post_init(state);
    }
}

} // namespace

// ============================================================================
// ModuleRegistry implementation
// ============================================================================

ModuleRegistry::~ModuleRegistry() {
    for (auto& mod : modules_) {
        if (mod && mod->plugin && mod->plugin->cleanup_plugin) {
            mod->plugin->cleanup_plugin();
        }
        if (mod && mod->handle) {
            dlclose(mod->handle);
        }
    }
}

LoadedModule* ModuleRegistry::findModule(const char* name) {
    if (!name) return nullptr;
    std::shared_lock<std::shared_mutex> lock(registry_mutex_);
    for (auto& mod : modules_) {
        if (mod && mod->name == name) {
            return mod.get();
        }
    }
    return nullptr;
}

void ModuleRegistry::registerBuiltinModule(const char* name, Table* module_table) {
    if (!name || !module_table) return;
    std::unique_lock<std::shared_mutex> lock(registry_mutex_);
    ModuleRecord record;
    record.table = module_table;
    record.state = ModuleLoadState::loaded;
    module_records_[name] = std::move(record);
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

    {
        std::shared_lock<std::shared_mutex> lock(registry_mutex_);
        for (const auto& mod : modules_) {
            if (mod && mod->name == plugin->metadata.name) {
                dlclose(handle);
                last_error_ = std::string("Module '") + plugin->metadata.name + "' is already loaded";
                result.error_message = last_error_.c_str();
                return result;
            }
        }
    }

    for (size_t i = 0; i < plugin->metadata.depends_on_count; i++) {
        const char* dep_name = plugin->metadata.depends_on ? plugin->metadata.depends_on[i] : nullptr;
        if (!dep_name || dep_name[0] == '\0') continue;
        if (strcmp(dep_name, plugin->metadata.name) == 0) {
            dlclose(handle);
            last_error_ = std::string("Module '") + plugin->metadata.name + "' cannot depend on itself";
            result.error_message = last_error_.c_str();
            return result;
        }
        Table* dep_table = resolveModule(dep_name, path, state);
        if (!dep_table) {
            std::string dep_error = last_error_;
            dlclose(handle);
            last_error_ = std::string("Failed to load dependency '") + dep_name +
                          "' for module '" + plugin->metadata.name + "'" +
                          (dep_error.empty() ? "" : std::string(": ") + dep_error);
            result.error_message = last_error_.c_str();
            return result;
        }
    }

    if (plugin->init_plugin && plugin->init_plugin(state) != 0) {
        dlclose(handle);
        last_error_ = "Plugin initialization failed";
        result.error_message = last_error_.c_str();
        return result;
    }

    auto mod = std::make_unique<LoadedModule>();
    mod->name = plugin->metadata.name;
    mod->path = path;
    mod->handle = handle;
    mod->plugin = plugin;
    mod->status = PLUGIN_STATUS_LOADED;
    {
        std::unique_lock<std::shared_mutex> lock(registry_mutex_);
        modules_.push_back(std::move(mod));
    }

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
    const std::string module_name(name);
    const std::thread::id current_thread = std::this_thread::get_id();

    {
        std::unique_lock<std::shared_mutex> lock(registry_mutex_);
        while (true) {
            auto it = module_records_.find(module_name);
            if (it == module_records_.end()) break;

            ModuleRecord& record = it->second;
            if (record.state == ModuleLoadState::loaded) {
                last_error_.clear();
                return record.table;
            }
            if (record.state == ModuleLoadState::failed) {
                last_error_ = record.error_message;
                return nullptr;
            }
            if (record.owner_thread == current_thread) {
                last_error_ = format_import_cycle_error(module_name);
                return nullptr;
            }
            module_cv_.wait(lock);
        }

        ModulePaths paths = resolve_module_paths(name, caller_source, state);
        if (!paths.found) {
            last_error_ = std::string("Module '") + name + "' not found";
            return nullptr;
        }

        Table* mod_table = new (std::nothrow) Table(state, 16);
        if (!mod_table) {
            last_error_ = "Failed to allocate module table";
            return nullptr;
        }

        ModuleRecord record;
        record.table = mod_table;
        record.state = ModuleLoadState::loading;
        record.path = paths.has_mob ? paths.mob_path : paths.so_path;
        record.owner_thread = current_thread;
        record.error_message.clear();
        record.globals = std::make_unique<GlobalEnvironment>();
        const GlobalEnvironment* root_globals = state->rootGlobalEnvironment();
        {
            std::lock_guard<std::recursive_mutex> globals_lock(root_globals->mutex);
            record.globals->slots = root_globals->slots;
            record.globals->count.store(root_globals->count.load(std::memory_order_relaxed),
                                        std::memory_order_relaxed);
            record.globals->slot_map = root_globals->slot_map;
        }
        record.globals->backing_table = mod_table;
        module_records_[module_name] = std::move(record);
        GlobalEnvironment* module_globals = module_records_[module_name].globals.get();

        lock.unlock();

        g_module_load_stack.push_back(module_name);

        bool ok = true;
        std::string error_message;

        if (paths.has_so) {
            LoadedModule* lm = findModule(name);
            if (!lm) {
                PluginLoadResult result = loadPlugin(paths.so_path.c_str(), state);
                if (result.status == PLUGIN_STATUS_LOADED && result.plugin) {
                    lm = findModule(result.plugin->metadata.name);
                } else if (result.error_message) {
                    ok = false;
                    error_message = result.error_message;
                } else {
                    ok = false;
                    error_message = "Plugin load failed";
                }
            }

            if (ok && lm && lm->plugin) {
                for (size_t i = 0; i < lm->plugin->function_count; i++) {
                    PluginFunction* func = &lm->plugin->functions[i];
                    if (!func || !func->name || !func->function) continue;
                    Value func_key = make_string_value_from_cstr(state, func->name);
                    Value func_val = make_native_function_value(func->function);
                    mod_table->set(func_key, func_val);
                }
                state->seedGlobalEnvironmentFromTable(module_globals, mod_table);
                run_plugin_post_init(lm->plugin, mod_table, state);
                state->seedGlobalEnvironmentFromTable(module_globals, mod_table);
            }
        }

        if (ok && paths.has_mob) {
            int rc = state->execFileInEnvironment(paths.mob_path.c_str(), module_globals);
            if (rc != 0) {
                ok = false;
                error_message = last_error_.empty() ? std::string("Failed to execute module '") + name + "'" : last_error_;
            }
        }

        g_module_load_stack.pop_back();

        lock.lock();
        auto final_it = module_records_.find(module_name);
        if (final_it == module_records_.end()) {
            last_error_ = std::string("Module '") + name + "' vanished during load";
            module_cv_.notify_all();
            return nullptr;
        }

        if (ok) {
            final_it->second.state = ModuleLoadState::loaded;
            final_it->second.error_message.clear();
            final_it->second.owner_thread = std::thread::id();
            last_error_.clear();
            Table* table = final_it->second.table;
            module_cv_.notify_all();
            return table;
        }

        final_it->second.state = ModuleLoadState::failed;
        final_it->second.error_message = error_message;
        final_it->second.owner_thread = std::thread::id();
        last_error_ = error_message;
        module_cv_.notify_all();
        return nullptr;
    }
}
