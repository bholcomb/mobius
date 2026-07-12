#ifndef MOBIUS_MODULE_REGISTRY_H
#define MOBIUS_MODULE_REGISTRY_H

#include "plugin.h"

#include <mobius/mobius.h>
#include <string>
#include <vector>
#include <unordered_map>
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

class MOBIUS_API ModuleRegistry {
public:
    ModuleRegistry() = default;
    ~ModuleRegistry();

    ModuleRegistry(const ModuleRegistry&) = delete;
    ModuleRegistry& operator=(const ModuleRegistry&) = delete;

    Table* resolveModule(const char* name, const char* caller_source, MobiusState* state);
    void registerBuiltinModule(const char* name, Table* module_table);

    // Release every Value held by cached module environments. Module records
    // live in this process-lifetime singleton (destroyed at atexit), but the
    // strings and tables in their global slots are owned by a MobiusState's
    // string pool. A MobiusState must call this before it frees that pool, or
    // the atexit teardown dereferences freed strings. Safe to call repeatedly.
    void releaseModuleValues();

    // Visit every Value held by cached module environments (global slots and
    // module tables) — GC roots that live outside any MobiusState.
    void forEachGlobalValue(void (*cb)(const Value&, void*), void* ud);

    bool debugMode() const { return debug_mode_; }
    void setDebugMode(bool mode) { debug_mode_ = mode; }
    const std::string& lastError() const { return last_error_; }

private:
    LoadedModule* findModule(const char* name);
    PluginLoadResult loadPlugin(const char* path, MobiusState* state,
                                const std::vector<std::string>* preload_paths = nullptr);

    mutable std::shared_mutex registry_mutex_;
    std::condition_variable_any module_cv_;
    std::vector<std::unique_ptr<LoadedModule>> modules_;
    std::unordered_map<std::string, ModuleRecord> module_records_;
    bool debug_mode_ = false;
    std::string last_error_;
};

MOBIUS_API ModuleRegistry* getGlobalRegistry();

#endif // MOBIUS_MODULE_REGISTRY_H
