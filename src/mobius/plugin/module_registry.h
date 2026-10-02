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
    void registerBuiltinModule(const char* name, Table* module_table);

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
    bool debug_mode_ = false;
    std::string last_error_;
};


#endif // MOBIUS_MODULE_REGISTRY_H
