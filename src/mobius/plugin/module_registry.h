#ifndef MOBIUS_MODULE_REGISTRY_H
#define MOBIUS_MODULE_REGISTRY_H

#include "plugin.h"

#include <mobius/mobius.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <shared_mutex>
#include <condition_variable>
#include <thread>
#include <memory>

class MobiusState;
class Table;
struct GlobalEnvironment;

struct LoadedModule {
    std::string name;
    std::string path;
    void* handle = nullptr;
    std::vector<void*> extra_handles;
    Plugin* plugin = nullptr;
    PluginStatus status = PLUGIN_STATUS_UNLOADED;
    std::string error_message;
    bool initialized = false;
};

enum class ModuleLoadState {
    loading,
    loaded,
    failed,
};

struct ModuleRecord {
    Table* table = nullptr;
    ModuleLoadState state = ModuleLoadState::loading;
    std::string path;
    std::string error_message;
    std::thread::id owner_thread;
    std::unique_ptr<GlobalEnvironment> globals;
    bool builtin = false;   // a built-in module: a sandbox imports it only once allowed
};

// One per MobiusState: the state's imported modules (tables and
// environments). Loaded native libraries are shared process-wide.
class MOBIUS_API ModuleRegistry {
public:
    ModuleRegistry() = default;
    ~ModuleRegistry();

    ModuleRegistry(const ModuleRegistry&) = delete;
    ModuleRegistry& operator=(const ModuleRegistry&) = delete;

    Table* resolveModule(const char* name, const char* caller_source, MobiusState* state);
    // A table importable by `name` in any state, sandboxed or not (fiber,
    // and modules the host registers). Takes over one reference to
    // module_table.
    void registerBuiltinModule(const char* name, Table* module_table);

    // Built-in modules: standard modules compiled into the core (math).
    // Every one can be imported without a sandbox; a sandboxed state
    // imports only those the host allowed. A built-in module wins over a
    // plugin or script of the same name on the search path.
    static bool isBuiltinModule(const char* name);
    // Let a sandboxed state import the built-in module `name` (which must
    // be one: see isBuiltinModule).
    void allowModule(const char* name);

    // Release every Value held by cached module environments. The strings
    // and tables in their global slots belong to the owning state, which
    // calls this before freeing its string pool. Safe to call repeatedly.
    void releaseModuleValues();

    // Visit every Value held by cached module environments (global slots and
    // module tables): GC roots of the owning state.
    void forEachGlobalValue(void (*cb)(const Value&, void*), void* ud);

    bool debugMode() const { return debug_mode_; }
    void setDebugMode(bool mode) { debug_mode_ = mode; }
    const std::string& lastError() const { return last_error_; }

private:
    PluginLoadResult loadPlugin(const char* path, MobiusState* state,
                                const std::vector<std::string>* preload_paths = nullptr);
    LoadedModule* loadLibrary(const char* path, const std::vector<std::string>* preload_paths,
                              PluginLoadResult* result);

    mutable std::shared_mutex registry_mutex_;
    std::condition_variable_any module_cv_;
    std::unordered_set<Plugin*> initialized_plugins_;   // init_plugin ran for this state
    std::unordered_map<std::string, ModuleRecord> module_records_;
    std::unordered_set<std::string> allowed_modules_;   // built-in modules a sandbox may import
    bool debug_mode_ = false;
    std::string last_error_;
};


// Runs every loaded plugin library's cleanup_plugin, then unloads them. Called
// once no state is alive (mobius_free_state of the last one): every state is
// then done with the libraries, and their own static objects still exist.
void unload_plugin_libraries();

#endif // MOBIUS_MODULE_REGISTRY_H
