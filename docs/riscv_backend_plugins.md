# Execution Backend Plugin Architecture
## Extending Mobius Plugin System for Execution Backends

## 🎯 **Design Goal: Pluggable Execution Backends**

Extend Mobius's existing plugin system to support loadable execution backends:

```
Mobius Core → Plugin System → [interpreter.so | libriscv.so | rvvm.so | custom.so]
```

## 🏗️ **Backend Plugin API**

### **Backend Plugin Interface**
```c
// New plugin type for execution backends
#define MOBIUS_PLUGIN_TYPE_EXECUTION_BACKEND "execution_backend"

// Execution Backend Plugin API (extends existing Plugin structure)
typedef struct {
    const char* backend_name;       // "interpreter", "libriscv", "rvvm", etc.
    const char* backend_version;    // Backend version
    const char* backend_type;       // "interpreter", "vm", "jit", etc.
    
    // VM lifecycle
    void* (*create_machine)(size_t memory_size, const char* config);
    void (*destroy_machine)(void* machine);
    
    // Program execution
    bool (*load_program)(void* machine, const void* code, size_t size, const char* format);
    int (*execute)(void* machine);
    void (*reset)(void* machine);
    
    // State management
    uint64_t (*read_register)(void* machine, int reg);
    void (*write_register)(void* machine, int reg, uint64_t value);
    bool (*read_memory)(void* machine, uint64_t addr, void* buffer, size_t size);
    bool (*write_memory)(void* machine, uint64_t addr, const void* buffer, size_t size);
    
    // Debugging support
    void (*set_breakpoint)(void* machine, uint64_t address);
    void (*clear_breakpoint)(void* machine, uint64_t address);
    bool (*single_step)(void* machine);
    
    // Performance and capabilities
    bool has_jit;                   // JIT compilation support
    bool supports_debugging;        // Debugging capabilities
    const char** supported_formats; // "bytecode", "riscv", "ast", etc.
    
} ExecutionBackendAPI;

// Extended plugin structure for execution backends
typedef struct {
    Plugin base;                    // Standard plugin interface
    const char* plugin_type;        // Must be MOBIUS_PLUGIN_TYPE_EXECUTION_BACKEND
    ExecutionBackendAPI* backend_api; // Backend-specific API
} ExecutionBackendPlugin;
```

### **Plugin Registration**
```c
// Backend registry (extends existing module registry)
typedef struct {
    char* name;                     // Backend name
    ExecutionBackendPlugin* plugin; // Plugin interface
    void* handle;                   // dlopen handle
    bool is_active;                 // Currently selected backend
    int priority;                   // Selection priority (higher = preferred)
} RegisteredBackend;

typedef struct {
    RegisteredBackend* backends;    // Array of registered backends
    size_t backend_count;
    size_t backend_capacity;
    RegisteredBackend* active_backend;  // Currently active backend
} ExecutionBackendRegistry;

// Backend management functions
ExecutionBackendRegistry* execution_backend_registry_create(void);
void execution_backend_registry_free(ExecutionBackendRegistry* registry);
bool register_execution_backend(ExecutionBackendRegistry* registry, ExecutionBackendPlugin* plugin);
bool select_execution_backend(ExecutionBackendRegistry* registry, const char* name);
bool select_default_execution_backend(ExecutionBackendRegistry* registry);
ExecutionBackendPlugin* get_active_execution_backend(ExecutionBackendRegistry* registry);
const char** list_execution_backends(ExecutionBackendRegistry* registry, size_t* count);
```

## 🔌 **Example Backend Plugins**

### **Tree-Walking Interpreter Backend**
```c
// interpreter_backend.c - Built-in tree-walking interpreter as a backend
#include "execution_backend_plugin.h"

// Interpreter machine (wraps existing evaluator)
typedef struct {
    Environment* env;
    // ... existing interpreter state
} InterpreterMachine;

static void* interpreter_create_machine(size_t memory_size, const char* config) {
    InterpreterMachine* machine = malloc(sizeof(InterpreterMachine));
    if (!machine) return NULL;
    
    machine->env = create_environment(NULL);
    // Apply interpreter-specific configuration
    return machine;
}

static bool interpreter_load_program(void* machine_ptr, const void* code, 
                                    size_t size, const char* format) {
    InterpreterMachine* machine = (InterpreterMachine*)machine_ptr;
    
    if (strcmp(format, "ast") == 0) {
        // Load AST directly
        Stmt** statements = (Stmt**)code;
        // Store for execution
        return true;
    } else if (strcmp(format, "bytecode") == 0) {
        // Convert bytecode back to AST (or interpret bytecode directly)
        return convert_bytecode_to_ast(code, size);
    }
    
    return false;
}

static int interpreter_execute(void* machine_ptr) {
    InterpreterMachine* machine = (InterpreterMachine*)machine_ptr;
    // Use existing tree-walking evaluator
    EvalResult result = evaluate_statements(machine->statements, machine->env);
    return result.has_error ? -1 : 0;
}

// Interpreter backend API
static ExecutionBackendAPI interpreter_api = {
    .backend_name = "interpreter",
    .backend_version = "1.0.0",
    .backend_type = "interpreter",
    
    .create_machine = interpreter_create_machine,
    .destroy_machine = interpreter_destroy_machine,
    .load_program = interpreter_load_program,
    .execute = interpreter_execute,
    .reset = interpreter_reset,
    
    .read_register = NULL,          // Not applicable for interpreter
    .write_register = NULL,
    .read_memory = NULL,
    .write_memory = NULL,
    
    .set_breakpoint = interpreter_set_breakpoint,
    .clear_breakpoint = interpreter_clear_breakpoint,
    .single_step = interpreter_single_step,
    
    .has_jit = false,               // Pure interpreter
    .supports_debugging = true,     // Good debugging support
    .supported_formats = (const char*[]){"ast", "bytecode", NULL},
};
```

### **libriscv Plugin Implementation**
```c
// libriscv_backend.c - Plugin implementation
#include "riscv_backend_plugin.h"
#include <libriscv/machine.hpp>

// libriscv-specific machine wrapper
typedef struct {
    riscv::Machine<riscv::RISCV64>* machine;
    size_t memory_size;
} LibRISCVMachine;

static void* libriscv_create_machine(size_t memory_size, const char* config) {
    LibRISCVMachine* wrapper = malloc(sizeof(LibRISCVMachine));
    if (!wrapper) return NULL;
    
    try {
        wrapper->machine = new riscv::Machine<riscv::RISCV64>(memory_size);
        wrapper->memory_size = memory_size;
        
        // Apply configuration if provided
        if (config) {
            // Parse JSON config for libriscv-specific settings
            apply_libriscv_config(wrapper->machine, config);
        }
        
        return wrapper;
    } catch (const std::exception& e) {
        free(wrapper);
        return NULL;
    }
}

static bool libriscv_load_program(void* machine_ptr, const void* code, 
                                  size_t size, uint64_t entry_point) {
    LibRISCVMachine* wrapper = (LibRISCVMachine*)machine_ptr;
    try {
        wrapper->machine->memory.memcpy(entry_point, code, size);
        wrapper->machine->cpu.pc(entry_point);
        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

static int libriscv_execute(void* machine_ptr) {
    LibRISCVMachine* wrapper = (LibRISCVMachine*)machine_ptr;
    try {
        wrapper->machine->simulate();
        return wrapper->machine->return_value();
    } catch (const std::exception& e) {
        return -1;
    }
}

// Complete backend API implementation
static RISCVBackendAPI libriscv_api = {
    .backend_name = "libriscv",
    .backend_version = "1.0.0",
    .supported_extensions = RV64I | RV64M | RV64A | RV64F | RV64D,
    
    .create_machine = libriscv_create_machine,
    .destroy_machine = libriscv_destroy_machine,
    .load_program = libriscv_load_program,
    .execute = libriscv_execute,
    .reset = libriscv_reset,
    
    .read_register = libriscv_read_register,
    .write_register = libriscv_write_register,
    .read_memory = libriscv_read_memory,
    .write_memory = libriscv_write_memory,
    
    .set_breakpoint = libriscv_set_breakpoint,
    .clear_breakpoint = libriscv_clear_breakpoint,
    .single_step = libriscv_single_step,
    
    .has_jit = false,               // libriscv doesn't have JIT
    .supports_debugging = true,     // Good debugging support
    .max_memory_size = 1ULL << 32,  // 4GB max
};

// Standard plugin metadata
static PluginMetadata libriscv_metadata = {
    .name = "libriscv_backend",
    .version = "1.0.0",
    .description = "libriscv RISC-V backend for Mobius",
    .author = "Mobius Team",
    .api_version = MOBIUS_PLUGIN_API_VERSION,
    .license = "MIT"
};

// Plugin structure
static RISCVBackendPlugin libriscv_plugin = {
    .base = {
        .metadata = libriscv_metadata,
        .functions = NULL,          // No script functions
        .function_count = 0,
        .init_plugin = libriscv_init,
        .cleanup_plugin = libriscv_cleanup,
        .get_help = NULL,
        .validate_env = libriscv_validate_env,
    },
    .plugin_type = MOBIUS_PLUGIN_TYPE_RISCV_BACKEND,
    .backend_api = &libriscv_api
};

// Plugin entry point
Plugin* mobius_plugin_info(void) {
    return (Plugin*)&libriscv_plugin;
}
```

### **RVVM Plugin Implementation**
```c
// rvvm_backend.c - Plugin implementation
#include "riscv_backend_plugin.h"
#include <rvvm/rvvm.h>

static void* rvvm_create_machine(size_t memory_size, const char* config) {
    rvvm_machine_t* machine = rvvm_create_machine(memory_size, 1, false);
    if (!machine) return NULL;
    
    // Apply RVVM-specific configuration
    if (config) {
        apply_rvvm_config(machine, config);
    }
    
    return machine;
}

static bool rvvm_load_program(void* machine_ptr, const void* code, 
                              size_t size, uint64_t entry_point) {
    rvvm_machine_t* machine = (rvvm_machine_t*)machine_ptr;
    if (!rvvm_write_ram(machine, entry_point, code, size)) {
        return false;
    }
    rvvm_set_pc(machine, 0, entry_point);
    return true;
}

static int rvvm_execute(void* machine_ptr) {
    rvvm_machine_t* machine = (rvvm_machine_t*)machine_ptr;
    rvvm_run(machine);
    return rvvm_read_cpu_reg(machine, 0, REGISTER_X10);  // Return value in a0
}

// RVVM backend API
static RISCVBackendAPI rvvm_api = {
    .backend_name = "rvvm",
    .backend_version = "0.6.0",
    .supported_extensions = RV64I | RV64M | RV64A | RV64F | RV64D | RV64C,
    
    .create_machine = rvvm_create_machine,
    .destroy_machine = rvvm_destroy_machine,
    .load_program = rvvm_load_program,
    .execute = rvvm_execute,
    .reset = rvvm_reset,
    
    .read_register = rvvm_read_register,
    .write_register = rvvm_write_register,
    .read_memory = rvvm_read_memory,
    .write_memory = rvvm_write_memory,
    
    .set_breakpoint = rvvm_set_breakpoint,
    .clear_breakpoint = rvvm_clear_breakpoint,
    .single_step = rvvm_single_step,
    
    .has_jit = true,                // RVVM has advanced JIT
    .supports_debugging = true,     // Full debugging support
    .max_memory_size = 1ULL << 40,  // 1TB max
};

// Plugin entry point
Plugin* mobius_plugin_info(void) {
    static RISCVBackendPlugin rvvm_plugin = {
        .base = {
            .metadata = {
                .name = "rvvm_backend",
                .version = "1.0.0",
                .description = "RVVM RISC-V backend for Mobius",
                .author = "Mobius Team",
                .api_version = MOBIUS_PLUGIN_API_VERSION,
            },
            .functions = NULL,
            .function_count = 0,
            .init_plugin = rvvm_init,
            .cleanup_plugin = rvvm_cleanup,
        },
        .plugin_type = MOBIUS_PLUGIN_TYPE_RISCV_BACKEND,
        .backend_api = &rvvm_api
    };
    
    return (Plugin*)&rvvm_plugin;
}
```

## 🔧 **Integration with Mobius Core**

### **Backend Manager Integration**
```c
// Extend MobiusState to include backend registry
typedef struct MobiusState {
    Environment* global_env;
    ModuleRegistry* registry;           // Existing plugin registry
    ExecutionBackendRegistry* execution_backends;  // New: execution backend registry
    // ... other fields
} MobiusState;

// Enhanced state creation
MobiusState* mobius_new_state(void) {
    MobiusState* state = malloc(sizeof(MobiusState));
    if (!state) return NULL;
    
    state->global_env = create_environment(NULL);
    state->registry = create_module_registry();
    state->execution_backends = execution_backend_registry_create();  // New
    
    return state;
}

// Load execution backend plugin
int mobius_load_execution_backend(MobiusState* state, const char* plugin_path) {
    if (!state || !plugin_path) return MOBIUS_ERROR_ARGUMENT;
    
    // Load plugin using existing system
    PluginLoadResult result = load_module(state->registry, plugin_path);
    if (result.status != PLUGIN_STATUS_LOADED) {
        return MOBIUS_ERROR_RUNTIME;
    }
    
    // Check if it's an execution backend plugin
    Plugin* plugin = result.plugin;
    if (is_execution_backend_plugin(plugin)) {
        ExecutionBackendPlugin* backend_plugin = (ExecutionBackendPlugin*)plugin;
        register_execution_backend(state->execution_backends, backend_plugin);
    }
    
    return MOBIUS_OK;
}

// Select execution backend by name
int mobius_select_backend(MobiusState* state, const char* backend_name) {
    if (!state || !backend_name) return MOBIUS_ERROR_ARGUMENT;
    
    if (select_execution_backend(state->execution_backends, backend_name)) {
        return MOBIUS_OK;
    }
    
    return MOBIUS_ERROR_RUNTIME;  // Backend not found
}

// List available execution backends
const char** mobius_list_backends(MobiusState* state, size_t* count) {
    if (!state || !count) return NULL;
    
    return list_execution_backends(state->execution_backends, count);
}

// Get currently active backend name
const char* mobius_get_active_backend(MobiusState* state) {
    if (!state) return NULL;
    
    ExecutionBackendPlugin* backend = get_active_execution_backend(state->execution_backends);
    return backend ? backend->backend_api->backend_name : NULL;
}
```

### **Auto-Discovery and Loading**
```c
// Auto-discover RISC-V backend plugins
void mobius_discover_riscv_backends(MobiusState* state) {
    const char* backend_dirs[] = {
        "./backends/",
        "/usr/local/lib/mobius/backends/",
        "/opt/mobius/backends/",
        NULL
    };
    
    for (int i = 0; backend_dirs[i]; i++) {
        discover_backends_in_directory(state, backend_dirs[i]);
    }
    
    // Auto-select default backend if none selected
    if (!riscv_get_active_backend(state->riscv_backends)) {
        riscv_select_default_backend(state->riscv_backends);
    }
}

// Default backend selection logic
bool riscv_select_default_backend(RISCVBackendRegistry* registry) {
    if (!registry || registry->backend_count == 0) {
        return false;
    }
    
    // Priority order for default selection:
    // 1. interpreter (always available, reliable)
    // 2. libriscv (simple VM)
    // 3. rvvm (performance)
    // 4. First available backend
    
    const char* preferred_order[] = {"interpreter", "libriscv", "rvvm", NULL};
    
    for (int i = 0; preferred_order[i]; i++) {
        if (riscv_select_backend(registry, preferred_order[i])) {
            return true;
        }
    }
    
    // Fallback: select first available backend
    if (registry->backend_count > 0) {
        registry->active_backend = &registry->backends[0];
        registry->backends[0].is_active = true;
        return true;
    }
    
    return false;
}

// Configuration-based backend selection
void mobius_configure_riscv_backend(MobiusState* state, const char* config) {
    // Example config: {"backend": "libriscv", "memory": "64MB", "jit": false}
    cJSON* json = cJSON_Parse(config);
    if (!json) return;
    
    cJSON* backend_name = cJSON_GetObjectItem(json, "backend");
    if (backend_name && cJSON_IsString(backend_name)) {
        riscv_select_backend(state->riscv_backends, backend_name->valuestring);
    }
    
    cJSON_Delete(json);
}
```

## 📋 **Plugin Build System**

### **CMake Configuration**
```cmake
# CMakeLists.txt for backend plugins
option(BUILD_LIBRISCV_BACKEND "Build libriscv backend plugin" ON)
option(BUILD_RVVM_BACKEND "Build RVVM backend plugin" OFF)

if(BUILD_LIBRISCV_BACKEND)
    find_package(libriscv REQUIRED)
    add_library(libriscv_backend SHARED 
        backends/libriscv_backend.c
        backends/libriscv_wrapper.cpp
    )
    target_link_libraries(libriscv_backend libriscv::libriscv)
    target_compile_definitions(libriscv_backend PRIVATE MOBIUS_PLUGIN_EXPORT)
endif()

if(BUILD_RVVM_BACKEND)
    find_package(rvvm REQUIRED)
    add_library(rvvm_backend SHARED 
        backends/rvvm_backend.c
    )
    target_link_libraries(rvvm_backend rvvm::rvvm)
    target_compile_definitions(rvvm_backend PRIVATE MOBIUS_PLUGIN_EXPORT)
endif()
```

### **Plugin Installation**
```bash
# Install backend plugins
make install-backends

# Plugins installed to:
# /usr/local/lib/mobius/backends/libriscv_backend.so
# /usr/local/lib/mobius/backends/rvvm_backend.so
```

## 🚀 **Usage Examples**

### **Runtime Backend Selection**
```c
// C API usage
MobiusState* state = mobius_new_state();

// Auto-discover available backends (selects default if none chosen)
mobius_discover_backends(state);

// List available backends
size_t count;
const char** backends = mobius_list_backends(state, &count);
printf("Available backends: ");
for (size_t i = 0; i < count; i++) {
    printf("%s ", backends[i]);
}
printf("\n");
// Output: "interpreter libriscv rvvm"

// Select specific backend by name
if (mobius_select_backend(state, "interpreter") != MOBIUS_OK) {
    printf("Failed to select interpreter backend\n");
}

// Check active backend
printf("Active backend: %s\n", mobius_get_active_backend(state));

// Execute code (uses selected backend)
mobius_exec_string(state, "func test() { return 42; } test();");
```

### **Mobius Script Configuration**
```javascript
// List available backends
print("Available backends:", list_backends());
// Output: ["interpreter", "libriscv", "rvvm"]

// Select backend by name
select_backend("interpreter");     // Use tree-walking interpreter
select_backend("libriscv");        // Use libriscv VM
select_backend("rvvm");            // Use RVVM with JIT

// Configure backend with options
configure_backend({
    backend: "libriscv",     // Select libriscv backend
    memory: "32MB",          // Memory limit
    debug: true              // Enable debugging support
});

// Check current backend
print("Active backend:", get_active_backend());
```

## 🎯 **Benefits of Plugin Architecture**

1. **Modularity**: Backends are separate, loadable modules
2. **Flexibility**: Choose backend at runtime based on needs
3. **Extensibility**: Easy to add new RISC-V VMs as plugins
4. **Optional Dependencies**: Only link what you need
5. **Distribution**: Backends can be distributed separately
6. **Testing**: Easy to test different backends
7. **Fallback**: Graceful fallback if preferred backend unavailable

This plugin-based approach gives you maximum flexibility while leveraging Mobius's existing, well-designed plugin infrastructure!
