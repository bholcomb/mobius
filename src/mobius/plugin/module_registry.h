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

    bool debugMode() const { return debug_mode_; }
    void setDebugMode(bool mode) { debug_mode_ = mode; }
    const std::string& lastError() const { return last_error_; }

private:
    LoadedModule* findModule(const char* name);
    PluginLoadResult loadPlugin(const char* path, MobiusState* state);

    mutable std::shared_mutex registry_mutex_;
    std::condition_variable_any module_cv_;
    std::vector<LoadedModule> modules_;
    std::unordered_map<std::string, ModuleRecord> module_records_;
    bool debug_mode_ = false;
    std::string last_error_;
};

MOBIUS_API ModuleRegistry* getGlobalRegistry();

#endif // MOBIUS_MODULE_REGISTRY_H
