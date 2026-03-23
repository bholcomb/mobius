#ifndef MOBIUS_MODULE_REGISTRY_H
#define MOBIUS_MODULE_REGISTRY_H

#include "plugin.h"

#include <mobius/mobius.h>
#include <string>
#include <vector>

struct LoadedModule {
    std::string name;
    std::string path;
    void* handle = nullptr;
    Plugin* plugin = nullptr;
    PluginStatus status = PLUGIN_STATUS_UNLOADED;
    std::string error_message;
    int ref_count = 0;
};

class MOBIUS_API ModuleRegistry {
public:
    ModuleRegistry() = default;
    ~ModuleRegistry();

    ModuleRegistry(const ModuleRegistry&) = delete;
    ModuleRegistry& operator=(const ModuleRegistry&) = delete;

    LoadedModule* findModule(const char* name);
    bool isModuleLoaded(const char* name);
    PluginLoadResult loadModuleByName(const char* name);
    void printLoadedModules() const;

    void addPluginDirectory(const char* directory);
    void clearPluginDirectories();
    int scanPlugins(bool force_rescan);

    void incrementRefCount(const char* module_name);
    void decrementRefCount(const char* module_name);

    size_t moduleCount() const { return modules_.size(); }
    bool debugMode() const { return debug_mode_; }
    void setDebugMode(bool mode) { debug_mode_ = mode; }

    const std::vector<LoadedModule>& modules() const { return modules_; }

private:
    PluginLoadResult loadModule(const char* path);
    int scanPluginDirectory(const char* directory);
    static bool isPluginFile(const char* filename);

    std::vector<LoadedModule> modules_;
    std::vector<std::string> plugin_directories_;
    bool debug_mode_ = false;
    bool already_scanned_ = false;
    std::string last_error_;
};

MOBIUS_API ModuleRegistry* getGlobalRegistry();

#endif // MOBIUS_MODULE_REGISTRY_H
