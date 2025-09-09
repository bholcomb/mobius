# VM-Agnostic Bytecode Design
## Supporting Both libriscv and RVVM

## 🎯 **Design Goal: VM Flexibility**

Create a bytecode system that can execute on multiple RISC-V VMs through a common abstraction layer:

```
Mobius Bytecode → RISC-V Assembly → [libriscv | RVVM | Future VMs]
```

## 🏗️ **Architecture Overview**

### **Abstraction Layer Design**
```c
// VM-agnostic interface
typedef struct RISCVBackend {
    const char* name;
    
    // VM lifecycle
    void* (*create_machine)(size_t memory_size);
    void (*destroy_machine)(void* machine);
    
    // Code execution
    bool (*load_program)(void* machine, void* code, size_t size);
    int (*execute)(void* machine);
    
    // Memory and register access
    uint64_t (*read_register)(void* machine, int reg);
    void (*write_register)(void* machine, int reg, uint64_t value);
    void* (*get_memory_ptr)(void* machine, uint64_t address);
    
    // Debugging support
    void (*set_breakpoint)(void* machine, uint64_t address);
    void (*single_step)(void* machine);
    
} RISCVBackend;
```

### **Backend Implementations**

#### **libriscv Backend**
```c
// libriscv implementation
static void* libriscv_create_machine(size_t memory_size) {
    riscv::Machine<riscv::RISCV64>* machine = 
        new riscv::Machine<riscv::RISCV64>(memory_size);
    return machine;
}

static bool libriscv_load_program(void* machine_ptr, void* code, size_t size) {
    auto* machine = (riscv::Machine<riscv::RISCV64>*)machine_ptr;
    machine->memory.memcpy(0x1000, code, size);  // Load at standard address
    machine->cpu.pc(0x1000);  // Set program counter
    return true;
}

static int libriscv_execute(void* machine_ptr) {
    auto* machine = (riscv::Machine<riscv::RISCV64>*)machine_ptr;
    try {
        machine->simulate();
        return machine->return_value();
    } catch (const std::exception& e) {
        return -1;  // Execution error
    }
}

// Complete libriscv backend
static RISCVBackend libriscv_backend = {
    .name = "libriscv",
    .create_machine = libriscv_create_machine,
    .destroy_machine = libriscv_destroy_machine,
    .load_program = libriscv_load_program,
    .execute = libriscv_execute,
    .read_register = libriscv_read_register,
    .write_register = libriscv_write_register,
    .get_memory_ptr = libriscv_get_memory_ptr,
    .set_breakpoint = libriscv_set_breakpoint,
    .single_step = libriscv_single_step,
};
```

#### **RVVM Backend**
```c
// RVVM implementation
static void* rvvm_create_machine(size_t memory_size) {
    rvvm_machine_t* machine = rvvm_create_machine(memory_size, 1, false);
    return machine;
}

static bool rvvm_load_program(void* machine_ptr, void* code, size_t size) {
    rvvm_machine_t* machine = (rvvm_machine_t*)machine_ptr;
    return rvvm_write_ram(machine, 0x1000, code, size);
}

static int rvvm_execute(void* machine_ptr) {
    rvvm_machine_t* machine = (rvvm_machine_t*)machine_ptr;
    rvvm_set_pc(machine, 0, 0x1000);  // Set program counter
    rvvm_run(machine);
    return rvvm_read_cpu_reg(machine, 0, REGISTER_X10);  // Return value in a0
}

// Complete RVVM backend
static RISCVBackend rvvm_backend = {
    .name = "RVVM",
    .create_machine = rvvm_create_machine,
    .destroy_machine = rvvm_destroy_machine,
    .load_program = rvvm_load_program,
    .execute = rvvm_execute,
    .read_register = rvvm_read_register,
    .write_register = rvvm_write_register,
    .get_memory_ptr = rvvm_get_memory_ptr,
    .set_breakpoint = rvvm_set_breakpoint,
    .single_step = rvvm_single_step,
};
```

## 🔧 **Unified Execution Interface**

### **VM Manager**
```c
typedef struct {
    RISCVBackend* backend;
    void* machine;
    bool is_initialized;
} RISCVExecutor;

// Create executor with chosen backend
RISCVExecutor* riscv_executor_create(RISCVBackend* backend, size_t memory_size) {
    RISCVExecutor* executor = malloc(sizeof(RISCVExecutor));
    executor->backend = backend;
    executor->machine = backend->create_machine(memory_size);
    executor->is_initialized = (executor->machine != NULL);
    return executor;
}

// Execute bytecode on any backend
int execute_bytecode_on_riscv(RISCVExecutor* executor, BytecodeChunk* chunk) {
    // Compile bytecode to RISC-V
    RISCVProgram* program = compile_bytecode_to_riscv(chunk);
    
    // Load and execute on chosen VM
    if (!executor->backend->load_program(executor->machine, 
                                        program->code, 
                                        program->size)) {
        return -1;
    }
    
    return executor->backend->execute(executor->machine);
}
```

### **Runtime Backend Selection**
```c
// Choose backend at runtime
typedef enum {
    RISCV_BACKEND_AUTO,      // Auto-detect best available
    RISCV_BACKEND_LIBRISCV,  // Force libriscv
    RISCV_BACKEND_RVVM,      // Force RVVM
} RISCVBackendType;

RISCVBackend* select_riscv_backend(RISCVBackendType type) {
    switch (type) {
        case RISCV_BACKEND_LIBRISCV:
            return &libriscv_backend;
            
        case RISCV_BACKEND_RVVM:
            return &rvvm_backend;
            
        case RISCV_BACKEND_AUTO:
        default:
            // Auto-detect: prefer libriscv for simplicity, fallback to RVVM
            if (is_libriscv_available()) {
                return &libriscv_backend;
            } else if (is_rvvm_available()) {
                return &rvvm_backend;
            }
            return NULL;  // No backend available
    }
}
```

## 📋 **Bytecode Compilation Strategy**

### **Standard RISC-V Output**
Our bytecode compiler generates **standard RISC-V assembly** that works on both VMs:

```c
// Bytecode instruction compilation
void compile_bytecode_instruction(RISCVCodeGen* gen, Instruction inst) {
    switch (inst.opcode) {
        case OP_ADD:
            // Standard RISC-V: works on both libriscv and RVVM
            riscv_emit_ld(gen, REG_T0, REG_SP, 0);     // Load first operand
            riscv_emit_ld(gen, REG_T1, REG_SP, 8);     // Load second operand
            riscv_emit_add(gen, REG_T0, REG_T0, REG_T1); // Add
            riscv_emit_sd(gen, REG_T0, REG_SP, 8);     // Store result
            riscv_emit_addi(gen, REG_SP, REG_SP, 8);   // Adjust stack
            break;
            
        case OP_LOAD_LOCAL:
            // Standard RISC-V: compatible with both VMs
            int offset = inst.operand * 8;  // 8 bytes per local
            riscv_emit_ld(gen, REG_T0, REG_FP, -offset);
            riscv_emit_addi(gen, REG_SP, REG_SP, -8);
            riscv_emit_sd(gen, REG_T0, REG_SP, 0);
            break;
    }
}
```

### **VM-Specific Optimizations**
```c
// Optional: VM-specific optimizations
void apply_vm_optimizations(RISCVProgram* program, RISCVBackend* backend) {
    if (strcmp(backend->name, "libriscv") == 0) {
        // libriscv-specific optimizations
        optimize_for_libriscv(program);
    } else if (strcmp(backend->name, "RVVM") == 0) {
        // RVVM-specific optimizations
        optimize_for_rvvm(program);
    }
}
```

## 🎛️ **Configuration and Selection**

### **Build-Time Configuration**
```c
// CMake configuration options
#ifdef MOBIUS_ENABLE_LIBRISCV
    #define HAS_LIBRISCV 1
#else
    #define HAS_LIBRISCV 0
#endif

#ifdef MOBIUS_ENABLE_RVVM
    #define HAS_RVVM 1
#else
    #define HAS_RVVM 0
#endif
```

### **Runtime Configuration**
```c
// Configuration file or command line
typedef struct {
    RISCVBackendType preferred_backend;
    size_t vm_memory_size;
    bool enable_jit;
    int optimization_level;
} MobiusConfig;

// Example usage
MobiusConfig config = {
    .preferred_backend = RISCV_BACKEND_LIBRISCV,  // Prefer libriscv
    .vm_memory_size = 64 * 1024 * 1024,          // 64MB
    .enable_jit = true,
    .optimization_level = 2
};
```

## 📊 **Comparison: libriscv vs RVVM**

| Feature | libriscv | RVVM |
|---------|----------|------|
| **Embedding** | ✅ Simple C++ API | ⚠️ More complex |
| **Memory Usage** | ✅ Lightweight | ⚠️ Heavier |
| **JIT Performance** | ⚠️ Basic | ✅ Advanced JIT |
| **Cross-platform** | ✅ Good | ✅ Excellent |
| **Debugging** | ✅ GDB support | ✅ Full debugging |
| **Complexity** | ✅ Simple | ⚠️ More complex |

## 🚀 **Implementation Benefits**

### **Flexibility**
- Choose VM based on use case (embedding vs performance)
- Easy to add new RISC-V VMs in the future
- Runtime switching between backends

### **Development**
- Start with libriscv (simpler integration)
- Add RVVM later for advanced JIT
- Test compatibility across VMs

### **Deployment**
- Embedded systems: Use libriscv (lighter)
- Server applications: Use RVVM (faster JIT)
- Development: Switch based on debugging needs

This approach gives us the best of both worlds - the simplicity of libriscv for embedding and the advanced JIT capabilities of RVVM when needed, all through a unified interface!

