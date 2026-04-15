
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
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_set>
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
    bool is_packaged = false;
    std::string package_root;
    std::string manifest_path;
    std::string mob_path;
    std::string so_path;
    std::vector<std::string> runtime_library_paths;
    std::string error_message;
};

struct PackageManifest {
    std::string name;
    std::string version;
    std::string entry_script;
    std::string module_library;
    std::vector<std::string> runtime_libraries;
};

static bool is_regular_file_path(const std::string& path);

static std::string parent_path(std::string path) {
    while (path.size() > 1 && path.back() == '/') path.pop_back();
    size_t slash = path.find_last_of('/');
    if (slash == std::string::npos) return "";
    if (slash == 0) return "/";
    return path.substr(0, slash);
}

static std::string dirname_path(const char* path) {
    if (!path || path[0] == '\0') return "";
    return parent_path(path);
}

static std::string find_enclosing_package_root(const char* source_path) {
    std::string dir = dirname_path(source_path);
    while (!dir.empty()) {
        if (is_regular_file_path(dir + "/module.yaml")) return dir;
        std::string parent = parent_path(dir);
        if (parent.empty() || parent == dir) break;
        dir = parent;
    }
    return "";
}

static void append_search_dir(std::vector<std::string>& search_dirs,
                              std::unordered_set<std::string>& seen_dirs,
                              const std::string& dir) {
    if (dir.empty()) return;
    if (!seen_dirs.insert(dir).second) return;
    search_dirs.push_back(dir);
}

static std::string trim_copy(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) start++;
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) end--;
    return s.substr(start, end - start);
}

static std::string strip_quotes(std::string value) {
    value = trim_copy(value);
    if (value.size() >= 2) {
        char first = value.front();
        char last = value.back();
        if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
            return value.substr(1, value.size() - 2);
        }
    }
    return value;
}

static bool is_regular_file_path(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

static std::string current_platform_key() {
#if defined(_WIN32)
  #if defined(_M_ARM64) || defined(__aarch64__)
    return "windows-aarch64";
  #else
    return "windows-x86_64";
  #endif
#elif defined(__APPLE__)
  #if defined(__aarch64__) || defined(__arm64__)
    return "macos-aarch64";
  #else
    return "macos-x86_64";
  #endif
#else
  #if defined(__aarch64__)
    return "linux-aarch64";
  #else
    return "linux-x86_64";
  #endif
#endif
}

static bool parse_package_manifest(const std::string& manifest_path, PackageManifest& out, std::string& error) {
    out = PackageManifest{};
    std::ifstream in(manifest_path);
    if (!in) {
        error = "could not open module.yaml";
        return false;
    }

    enum class Section {
        root,
        entry,
        platforms,
        current_platform,
        runtime_libraries,
        other
    };

    Section section = Section::root;
    const std::string wanted_platform = current_platform_key();
    bool in_wanted_platform = false;
    bool saw_platforms = false;
    bool saw_any_platform_module_library = false;
    std::string line;

    while (std::getline(in, line)) {
        size_t comment_pos = line.find('#');
        if (comment_pos != std::string::npos) line.erase(comment_pos);
        std::string trimmed = trim_copy(line);
        if (trimmed.empty()) continue;

        size_t indent = 0;
        while (indent < line.size() && line[indent] == ' ') indent++;

        if (indent == 0) {
            section = Section::root;
            in_wanted_platform = false;
            if (trimmed.rfind("name:", 0) == 0) {
                out.name = strip_quotes(trimmed.substr(5));
            } else if (trimmed.rfind("version:", 0) == 0) {
                out.version = strip_quotes(trimmed.substr(8));
            } else if (trimmed == "entry:") {
                section = Section::entry;
            } else if (trimmed == "platforms:") {
                section = Section::platforms;
                saw_platforms = true;
            } else {
                section = Section::other;
            }
            continue;
        }

        if (indent == 2 && section == Section::entry) {
            if (trimmed.rfind("script:", 0) == 0) {
                out.entry_script = strip_quotes(trimmed.substr(7));
            }
            continue;
        }

        if (indent == 2 && saw_platforms) {
            if (!trimmed.empty() && trimmed.back() == ':') {
                std::string platform_key = trim_copy(trimmed.substr(0, trimmed.size() - 1));
                in_wanted_platform = (platform_key == wanted_platform);
                section = in_wanted_platform ? Section::current_platform : Section::platforms;
            }
            continue;
        }

        if (indent == 4 && in_wanted_platform) {
            if (trimmed.rfind("module_library:", 0) == 0) {
                out.module_library = strip_quotes(trimmed.substr(15));
                saw_any_platform_module_library = true;
                section = Section::current_platform;
            } else if (trimmed == "runtime_libraries:") {
                section = Section::runtime_libraries;
            } else {
                section = Section::current_platform;
            }
            continue;
        }

        if (indent == 4 && saw_platforms) {
            if (trimmed.rfind("module_library:", 0) == 0) {
                saw_any_platform_module_library = true;
            }
            continue;
        }

        if (indent >= 6 && in_wanted_platform && section == Section::runtime_libraries) {
            if (trimmed.rfind("- ", 0) == 0) {
                out.runtime_libraries.push_back(strip_quotes(trimmed.substr(2)));
            }
            continue;
        }
    }

    if (out.name.empty()) {
        error = "module.yaml is missing name";
        return false;
    }
    if (out.version.empty()) {
        error = "module.yaml is missing version";
        return false;
    }
    if (out.entry_script.empty() && !saw_any_platform_module_library) {
        error = "module.yaml must declare entry.script and/or a platform module_library";
        return false;
    }
    return true;
}

static ModulePaths resolve_packaged_module_paths(const std::string& dir, const char* name) {
    ModulePaths out;
    const std::string package_root = dir + "/" + name;
    const std::string manifest_path = package_root + "/module.yaml";
    if (!is_regular_file_path(manifest_path)) return out;

    PackageManifest manifest;
    std::string error;
    if (!parse_package_manifest(manifest_path, manifest, error)) {
        out.error_message = "Invalid package manifest for module '" + std::string(name) + "': " + error;
        return out;
    }
    if (manifest.name != name) {
        out.error_message = "Package manifest name mismatch for module '" + std::string(name) + "'";
        return out;
    }

    out.is_packaged = true;
    out.package_root = package_root;
    out.manifest_path = manifest_path;

    if (!manifest.entry_script.empty()) {
        std::string mob_path = package_root + "/" + manifest.entry_script;
        if (!is_regular_file_path(mob_path)) {
            out.error_message = "Package entry script not found: " + mob_path;
            return out;
        }
        out.has_mob = true;
        out.mob_path = std::move(mob_path);
    }

    if (!manifest.module_library.empty()) {
        std::string so_path = package_root + "/" + manifest.module_library;
        if (!is_regular_file_path(so_path)) {
            out.error_message = "Package module library not found: " + so_path;
            return out;
        }
        out.has_so = true;
        out.so_path = std::move(so_path);
        for (const std::string& runtime_lib : manifest.runtime_libraries) {
            std::string full_runtime_path = package_root + "/" + runtime_lib;
            if (!is_regular_file_path(full_runtime_path)) {
                out.error_message = "Package runtime library not found: " + full_runtime_path;
                return out;
            }
            out.runtime_library_paths.push_back(std::move(full_runtime_path));
        }
    }

    out.found = out.has_mob || out.has_so;
    if (!out.found && out.error_message.empty()) {
        out.error_message = "Package does not contain a loadable entry for the current platform";
    }
    return out;
}

static ModulePaths resolve_module_paths(const char* name, const char* caller_source, MobiusState* state) {
    ModulePaths out;
    const char* default_dirs[] = {
        "./modules",
        "./bin/modules",
        "/usr/local/lib/mobius/modules",
        nullptr
    };
    std::vector<std::string> search_dirs;

    std::unordered_set<std::string> seen_dirs;
    if (caller_source && caller_source[0] != '\0' && strcmp(caller_source, "<string>") != 0) {
        append_search_dir(search_dirs, seen_dirs, dirname_path(caller_source));

        std::string caller_package_root = find_enclosing_package_root(caller_source);
        if (!caller_package_root.empty()) {
            append_search_dir(search_dirs, seen_dirs, parent_path(caller_package_root));
        }
    }

    for (const auto& d : state->pluginDirectories()) append_search_dir(search_dirs, seen_dirs, d);
    for (int i = 0; default_dirs[i]; i++) append_search_dir(search_dirs, seen_dirs, default_dirs[i]);

    for (const auto& dir : search_dirs) {
        ModulePaths packaged = resolve_packaged_module_paths(dir, name);
        if (!packaged.error_message.empty()) return packaged;
        if (packaged.found) return packaged;
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
        if (mod) {
            for (void* extra : mod->extra_handles) {
                if (extra) dlclose(extra);
            }
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

PluginLoadResult ModuleRegistry::loadPlugin(const char* path, MobiusState* state,
                                            const std::vector<std::string>* preload_paths) {
    PluginLoadResult result = {PLUGIN_STATUS_ERROR, nullptr, nullptr};
    std::vector<void*> extra_handles;

    if (!path) {
        result.error_message = "Invalid path";
        return result;
    }

    if (debug_mode_) {
        printf("Loading plugin: %s\n", path);
    }

    if (preload_paths) {
        for (const std::string& preload_path : *preload_paths) {
            void* extra = dlopen(preload_path.c_str(), RTLD_LAZY | RTLD_GLOBAL);
            if (!extra) {
                for (void* handle : extra_handles) {
                    if (handle) dlclose(handle);
                }
                last_error_ = std::string("Failed to preload runtime library: ") + dlerror();
                result.error_message = last_error_.c_str();
                return result;
            }
            extra_handles.push_back(extra);
        }
    }

    void* handle = dlopen(path, RTLD_LAZY);
    if (!handle) {
        for (void* extra : extra_handles) {
            if (extra) dlclose(extra);
        }
        last_error_ = std::string("Failed to load library: ") + dlerror();
        result.error_message = last_error_.c_str();
        return result;
    }

    union { void* obj; PluginInfoFunc func; } plugin_info_ptr;
    plugin_info_ptr.obj = dlsym(handle, "mobius_plugin_info");
    PluginInfoFunc get_plugin_info = plugin_info_ptr.func;
    if (!get_plugin_info) {
        for (void* extra : extra_handles) {
            if (extra) dlclose(extra);
        }
        dlclose(handle);
        last_error_ = "Plugin does not export mobius_plugin_info function";
        result.error_message = last_error_.c_str();
        return result;
    }

    Plugin* plugin = get_plugin_info();
    if (!plugin) {
        for (void* extra : extra_handles) {
            if (extra) dlclose(extra);
        }
        dlclose(handle);
        last_error_ = "Plugin returned NULL from mobius_plugin_info";
        result.error_message = last_error_.c_str();
        return result;
    }

    if (plugin->metadata.api_version != MOBIUS_PLUGIN_API_VERSION) {
        for (void* extra : extra_handles) {
            if (extra) dlclose(extra);
        }
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
                for (void* extra : extra_handles) {
                    if (extra) dlclose(extra);
                }
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
            for (void* extra : extra_handles) {
                if (extra) dlclose(extra);
            }
            dlclose(handle);
            last_error_ = std::string("Module '") + plugin->metadata.name + "' cannot depend on itself";
            result.error_message = last_error_.c_str();
            return result;
        }
        Table* dep_table = resolveModule(dep_name, path, state);
        if (!dep_table) {
            std::string dep_error = last_error_;
            for (void* extra : extra_handles) {
                if (extra) dlclose(extra);
            }
            dlclose(handle);
            last_error_ = std::string("Failed to load dependency '") + dep_name +
                          "' for module '" + plugin->metadata.name + "'" +
                          (dep_error.empty() ? "" : std::string(": ") + dep_error);
            result.error_message = last_error_.c_str();
            return result;
        }
    }

    if (plugin->init_plugin && plugin->init_plugin(state) != 0) {
        for (void* extra : extra_handles) {
            if (extra) dlclose(extra);
        }
        dlclose(handle);
        last_error_ = "Plugin initialization failed";
        result.error_message = last_error_.c_str();
        return result;
    }

    auto mod = std::make_unique<LoadedModule>();
    mod->name = plugin->metadata.name;
    mod->path = path;
    mod->handle = handle;
    mod->extra_handles = std::move(extra_handles);
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
            last_error_ = paths.error_message.empty()
                ? std::string("Module '") + name + "' not found"
                : paths.error_message;
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
        record.path = paths.is_packaged
            ? paths.package_root
            : (paths.has_mob ? paths.mob_path : paths.so_path);
        record.owner_thread = current_thread;
        record.error_message.clear();
        record.globals = std::make_unique<GlobalEnvironment>();
        const GlobalEnvironment* root_globals = state->rootGlobalEnvironment();
        {
            std::lock_guard<std::mutex> globals_lock(root_globals->mutex);
            record.globals->slots = root_globals->slots;
            record.globals->slot_names = root_globals->slot_names;
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
                PluginLoadResult result = loadPlugin(paths.so_path.c_str(), state,
                                                     paths.runtime_library_paths.empty() ? nullptr : &paths.runtime_library_paths);
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
