# Mobius Bytecode System Design
## RISC-V Targeted Virtual Machine Architecture

### 1. Overview

The Mobius bytecode system is designed as a stack-based virtual machine with RISC-V architecture compatibility in mind. This design enables:

- **Bytecode interpretation** via stack-based VM
- **Native compilation** to RISC-V assembly
- **JIT compilation** for frequently executed code
- **Cross-platform compatibility** with RISC-V optimization

### 2. Design Principles

#### 2.1 RISC-V Alignment
- **Register-friendly operations**: Bytecode maps cleanly to RISC-V's 32 general-purpose registers
- **Load/Store architecture**: Memory operations are explicit and RISC-V compatible
- **Simple instruction format**: Fixed-width instructions that can map to RISC-V efficiently
- **Branch prediction friendly**: Conditional operations designed for RISC-V branch prediction

#### 2.2 Dual Execution Model
- **Bytecode Interpreter**: Pure stack-based VM for portability and simplicity
- **JIT Compiler**: Converts bytecode to register-based RISC-V native code
- **Register hints**: Metadata in bytecode to guide JIT register allocation (not used by interpreter)

### 3. Instruction Set Architecture

#### 3.1 Instruction Format

```c
// 32-bit instruction format
typedef struct {
    uint8_t opcode;     // 8-bit opcode (256 possible instructions)
    uint8_t reg_hint;   // Register allocation hint for JIT compiler (ignored by interpreter)
    uint16_t operand;   // 16-bit operand (immediate, offset, or constant index)
} Instruction;

// Extended instruction for complex operations
typedef struct {
    uint8_t opcode;     // Extended opcode marker
    uint8_t sub_opcode; // Actual operation
    uint16_t operand1;  // First operand
    uint32_t operand2;  // Second operand (next 32 bits)
} ExtendedInstruction;
```

#### 3.2 Core Instruction Categories

##### 3.2.1 Stack Operations (RISC-V Memory Mapping)
```c
// Stack manipulation - maps to RISC-V load/store
OP_PUSH_NIL         = 0x00,  // Push nil value
OP_PUSH_TRUE        = 0x01,  // Push boolean true
OP_PUSH_FALSE       = 0x02,  // Push boolean false
OP_PUSH_INT8        = 0x03,  // Push 8-bit integer (operand)
OP_PUSH_INT16       = 0x04,  // Push 16-bit integer (operand)
OP_PUSH_INT32       = 0x05,  // Push 32-bit integer (next 4 bytes)
OP_PUSH_INT64       = 0x06,  // Push 64-bit integer (next 8 bytes)
OP_PUSH_FLOAT32     = 0x07,  // Push 32-bit float
OP_PUSH_FLOAT64     = 0x08,  // Push 64-bit float
OP_PUSH_STRING      = 0x09,  // Push string (operand = string table index)
OP_PUSH_CHAR        = 0x0A,  // Push character (operand)

OP_POP              = 0x10,  // Pop top value
OP_DUP              = 0x11,  // Duplicate top value
OP_SWAP             = 0x12,  // Swap top two values
OP_ROT3             = 0x13,  // Rotate top 3 values
```

##### 3.2.2 Arithmetic Operations (RISC-V ALU Mapping)
```c
// Arithmetic - maps directly to RISC-V arithmetic instructions
OP_ADD              = 0x20,  // Add top two values
OP_SUB              = 0x21,  // Subtract (second - top)
OP_MUL              = 0x22,  // Multiply
OP_DIV              = 0x23,  // Divide (second / top)
OP_MOD              = 0x24,  // Modulo
OP_NEG              = 0x25,  // Negate top value
OP_ABS              = 0x26,  // Absolute value

// Bitwise operations - direct RISC-V mapping
OP_AND              = 0x28,  // Bitwise AND
OP_OR               = 0x29,  // Bitwise OR
OP_XOR              = 0x2A,  // Bitwise XOR
OP_NOT              = 0x2B,  // Bitwise NOT
OP_SHL              = 0x2C,  // Shift left
OP_SHR              = 0x2D,  // Shift right (logical)
OP_SAR              = 0x2E,  // Shift right (arithmetic)
```

##### 3.2.3 Comparison Operations (RISC-V Branch Mapping)
```c
// Comparisons - optimized for RISC-V branch instructions
OP_EQ               = 0x30,  // Equal (==)
OP_NE               = 0x31,  // Not equal (!=)
OP_LT               = 0x32,  // Less than (<)
OP_LE               = 0x33,  // Less or equal (<=)
OP_GT               = 0x34,  // Greater than (>)
OP_GE               = 0x35,  // Greater or equal (>=)

// Logical operations
OP_LOGICAL_AND      = 0x38,  // Logical AND (&&)
OP_LOGICAL_OR       = 0x39,  // Logical OR (||)
OP_LOGICAL_NOT      = 0x3A,  // Logical NOT (!)
```

##### 3.2.4 Memory Operations (RISC-V Load/Store Architecture)
```c
// Variable operations - RISC-V memory model
OP_LOAD_LOCAL       = 0x40,  // Load local variable (operand = slot index)
OP_STORE_LOCAL      = 0x41,  // Store to local variable
OP_LOAD_GLOBAL      = 0x42,  // Load global variable (operand = name index)
OP_STORE_GLOBAL     = 0x43,  // Store to global variable
OP_LOAD_UPVALUE     = 0x44,  // Load upvalue (closure variable)
OP_STORE_UPVALUE    = 0x45,  // Store to upvalue

// Array/Table operations - structured memory access
OP_ARRAY_NEW        = 0x48,  // Create new array (operand = initial capacity)
OP_ARRAY_GET        = 0x49,  // Get array element (array, index on stack)
OP_ARRAY_SET        = 0x4A,  // Set array element (array, index, value on stack)
OP_ARRAY_PUSH       = 0x4B,  // Push to array (array, value on stack)
OP_ARRAY_POP        = 0x4C,  // Pop from array
OP_ARRAY_LEN        = 0x4D,  // Get array length

OP_TABLE_NEW        = 0x50,  // Create new table
OP_TABLE_GET        = 0x51,  // Get table value (table, key on stack)
OP_TABLE_SET        = 0x52,  // Set table value (table, key, value on stack)
OP_TABLE_HAS        = 0x53,  // Check if table has key
```

##### 3.2.5 Control Flow (RISC-V Branch/Jump Mapping)
```c
// Control flow - designed for RISC-V branch prediction
OP_JUMP             = 0x60,  // Unconditional jump (operand = offset)
OP_JUMP_IF_FALSE    = 0x61,  // Jump if top of stack is false
OP_JUMP_IF_TRUE     = 0x62,  // Jump if top of stack is true
OP_JUMP_IF_NIL      = 0x63,  // Jump if top of stack is nil

// Loop operations - optimized for RISC-V
OP_LOOP             = 0x64,  // Loop back (operand = negative offset)
OP_FOR_PREP         = 0x65,  // Prepare for loop (numeric for)
OP_FOR_LOOP         = 0x66,  // For loop iteration

// Function calls - RISC-V calling convention friendly
OP_CALL             = 0x68,  // Call function (operand = arg count)
OP_CALL_BUILTIN     = 0x69,  // Call builtin function (operand = builtin index)
OP_RETURN           = 0x6A,  // Return from function
OP_RETURN_NIL       = 0x6B,  // Return nil (optimized)
```

##### 3.2.6 Advanced Operations
```c
// Type operations
OP_TYPE_CHECK       = 0x70,  // Check value type (operand = expected type)
OP_TYPE_CAST        = 0x71,  // Cast value type
OP_IS_NIL           = 0x72,  // Check if value is nil
OP_IS_TRUTHY        = 0x73,  // Check if value is truthy

// String operations
OP_STRING_CONCAT    = 0x78,  // Concatenate strings
OP_STRING_LEN       = 0x79,  // Get string length
OP_STRING_SLICE     = 0x7A,  // String slicing

// Debug/Profiling
OP_DEBUG_BREAK      = 0xF0,  // Debug breakpoint
OP_PROFILE_ENTER    = 0xF1,  // Profiling function entry
OP_PROFILE_EXIT     = 0xF2,  // Profiling function exit

// Extended operations marker
OP_EXTENDED         = 0xFF,  // Indicates extended instruction follows
```

### 4. Execution Models

#### 4.1 Stack-Based Interpreter
The bytecode interpreter operates as a traditional stack machine:
- All operations work on a value stack
- `OP_ADD` pops two values, adds them, pushes result
- `OP_LOAD_LOCAL 5` pushes local variable 5 onto stack
- Register hints are completely ignored

#### 4.2 JIT Compilation to RISC-V
The JIT compiler converts bytecode to register-based native code:
- Analyzes bytecode sequences for optimization opportunities
- Uses register hints to guide allocation decisions
- Converts stack operations to register operations
- Generates native RISC-V machine code

### 5. RISC-V Native Compilation Mapping

#### 4.1 Register Allocation Strategy

```c
// RISC-V Register Usage Convention for Mobius VM
// Based on RISC-V ABI with Mobius-specific optimizations

// x0: zero (hardwired to 0)
// x1: ra (return address)
// x2: sp (stack pointer) - VM stack pointer
// x3: gp (global pointer) - VM global data
// x4: tp (thread pointer) - VM thread context

// Temporary registers (caller-saved)
// x5-x7, x28-x31: t0-t6 - Expression evaluation
// x10-x17: a0-a7 - Function arguments/return values

// Saved registers (callee-saved)  
// x8-x9: s0-s1 - VM base pointer, current function
// x18-x27: s2-s11 - Local variables (hot variables)

// Floating-point registers
// f0-f7: ft0-ft7 - Temporary floating-point
// f8-f9: fs0-fs1 - Saved floating-point
// f10-f17: fa0-fa7 - Floating-point arguments/return
// f18-f27: fs2-fs11 - Saved floating-point registers
```

#### 4.2 Instruction Mapping Examples

```c
// Bytecode to RISC-V mapping examples

// OP_ADD (stack-based) -> RISC-V
// Bytecode: OP_ADD
// RISC-V:   ld   t0, 0(sp)      # Load second operand
//           ld   t1, 8(sp)      # Load first operand  
//           add  t0, t1, t0     # Add
//           sd   t0, 8(sp)      # Store result
//           addi sp, sp, 8      # Adjust stack

// OP_LOAD_LOCAL -> RISC-V
// Bytecode: OP_LOAD_LOCAL 5
// RISC-V:   ld   t0, 40(s0)     # Load from local slot 5 (5*8 bytes)
//           addi sp, sp, -8     # Grow stack
//           sd   t0, 0(sp)      # Push to stack

// OP_JUMP_IF_FALSE -> RISC-V  
// Bytecode: OP_JUMP_IF_FALSE offset
// RISC-V:   ld   t0, 0(sp)      # Load condition
//           addi sp, sp, 8      # Pop stack
//           beq  t0, zero, target # Branch if false (0)
```

### 5. Bytecode Generation Strategy

#### 5.1 AST to Bytecode Compilation

```c
// Compilation phases for RISC-V optimization

typedef struct {
    Instruction* instructions;
    size_t count;
    size_t capacity;
    
    // RISC-V optimization metadata
    uint32_t* register_hints;     // Register allocation hints
    uint32_t* branch_targets;     // Branch target addresses
    bool* hot_paths;              // Hot path identification
    
    // Constant pools
    Value* constants;             // Constant values
    char** string_pool;           // String literals
    size_t constant_count;
    size_t string_count;
} BytecodeChunk;

// Compilation context for RISC-V optimization
typedef struct {
    BytecodeChunk* chunk;
    
    // Local variable tracking (for register allocation)
    struct {
        char* name;
        int slot;
        bool is_hot;              // Frequently accessed
        int risc_v_reg_hint;      // Preferred RISC-V register
    } locals[256];
    int local_count;
    
    // Control flow for branch optimization
    struct {
        int start;
        int end;
        bool is_loop;
    } scopes[64];
    int scope_depth;
    
} CompilerContext;
```

#### 5.2 Optimization Passes

```c
// RISC-V-specific optimization passes

// Pass 1: Register allocation hints
void analyze_register_usage(BytecodeChunk* chunk) {
    // Analyze variable lifetime and usage frequency
    // Assign RISC-V register hints for hot variables
    // Mark candidates for register promotion
}

// Pass 2: Branch optimization
void optimize_branches(BytecodeChunk* chunk) {
    // Identify branch targets and patterns
    // Optimize for RISC-V branch prediction
    // Convert to branch-likely where beneficial
}

// Pass 3: Instruction selection
void select_risc_v_instructions(BytecodeChunk* chunk) {
    // Choose optimal RISC-V instruction sequences
    // Combine operations where possible
    // Use RISC-V-specific optimizations (compressed instructions)
}
```

### 6. Virtual Machine Architecture

#### 6.1 VM State Structure

```c
typedef struct {
    // Execution state
    Instruction* ip;              // Instruction pointer
    Value* stack;                 // Value stack
    Value* stack_top;             // Stack top pointer
    size_t stack_capacity;
    
    // RISC-V compatibility
    uint64_t registers[32];       // Virtual RISC-V registers
    double fp_registers[32];      // Floating-point registers
    
    // Memory management
    Value* globals;               // Global variables
    size_t global_count;
    
    // Function call state
    struct CallFrame {
        Instruction* return_ip;   // Return instruction pointer
        Value* base_pointer;      // Frame base pointer
        MobiusFunction* function; // Current function
        int local_count;          // Number of locals
    } call_stack[256];
    int call_depth;
    
    // Runtime support
    BytecodeChunk* current_chunk;
    Value* constants;             // Constant pool
    char** strings;               // String pool
    
    // RISC-V JIT support
    void* jit_cache;              // Compiled native code cache
    bool jit_enabled;             // JIT compilation enabled
    
} MobiusVM;
```

#### 6.2 Execution Engine

```c
// Main execution loop optimized for RISC-V
typedef enum {
    VM_OK,
    VM_COMPILE_ERROR,
    VM_RUNTIME_ERROR,
    VM_STACK_OVERFLOW,
    VM_STACK_UNDERFLOW
} VMResult;

VMResult vm_execute(MobiusVM* vm) {
    // Fast dispatch table for RISC-V branch prediction
    static void* dispatch_table[] = {
        &&op_push_nil,
        &&op_push_true,
        &&op_push_false,
        // ... all opcodes
    };
    
    // Main execution loop
    for (;;) {
        uint8_t opcode = vm->ip->opcode;
        
        // Direct dispatch (RISC-V branch prediction friendly)
        goto *dispatch_table[opcode];
        
        op_push_nil:
            vm_push(vm, make_nil_value());
            vm->ip++;
            continue;
            
        op_add:
            {
                Value b = vm_pop(vm);
                Value a = vm_pop(vm);
                Value result = add_values(a, b);
                vm_push(vm, result);
                vm->ip++;
            }
            continue;
            
        // ... other operations
    }
}
```

### 7. RISC-V Native Code Generation

#### 7.1 JIT Compilation Interface

```c
// JIT compiler for hot bytecode sequences
typedef struct {
    uint32_t* native_code;        // Generated RISC-V instructions
    size_t code_size;
    void* entry_point;
    
    // Optimization metadata
    int execution_count;          // Hotness counter
    bool is_compiled;             // Native code available
    
} JITCompiledFunction;

// JIT compilation trigger
void jit_compile_if_hot(MobiusVM* vm, BytecodeChunk* chunk, int pc) {
    static int execution_counts[1024] = {0};
    
    execution_counts[pc]++;
    
    if (execution_counts[pc] > JIT_THRESHOLD) {
        // Compile to native RISC-V code
        JITCompiledFunction* compiled = compile_to_riscv(chunk, pc);
        
        if (compiled) {
            // Execute native code directly
            execute_native_riscv(vm, compiled);
        }
    }
}
```

#### 7.2 Native Code Generation

```c
// RISC-V code generator
typedef struct {
    uint32_t* code_buffer;
    size_t code_pos;
    size_t code_capacity;
    
    // Register allocation state
    bool register_used[32];
    int register_map[256];        // Bytecode slot -> RISC-V register
    
} RISCVCodeGen;

// Generate RISC-V instruction
void emit_riscv_instruction(RISCVCodeGen* gen, uint32_t instruction) {
    if (gen->code_pos >= gen->code_capacity) {
        // Resize buffer
        gen->code_capacity *= 2;
        gen->code_buffer = realloc(gen->code_buffer, 
                                  gen->code_capacity * sizeof(uint32_t));
    }
    gen->code_buffer[gen->code_pos++] = instruction;
}

// Example: Compile OP_ADD to RISC-V
void compile_add_to_riscv(RISCVCodeGen* gen) {
    // Assume operands in registers t0, t1
    // add t0, t0, t1
    uint32_t add_inst = 0x00628533;  // RISC-V ADD instruction encoding
    emit_riscv_instruction(gen, add_inst);
}
```

### 8. Memory Layout and ABI

#### 8.1 RISC-V Memory Model Compatibility

```c
// Memory layout designed for RISC-V efficiency
typedef struct {
    // Code segment (read-only, executable)
    struct {
        Instruction* bytecode;
        uint32_t* native_code;    // JIT compiled RISC-V code
        size_t code_size;
    } text_segment;
    
    // Data segment (read-write)
    struct {
        Value* constants;         // Constant pool
        char** string_literals;   // String pool
        Value* globals;           // Global variables
    } data_segment;
    
    // Stack segment (RISC-V stack model)
    struct {
        Value* stack_base;        // Stack bottom
        Value* stack_top;         // Current stack top
        Value* stack_limit;       // Stack overflow guard
        size_t stack_size;
    } stack_segment;
    
    // Heap segment (garbage collected)
    struct {
        void* heap_base;
        void* heap_top;
        size_t heap_size;
        // GC metadata
    } heap_segment;
    
} MobiusMemoryLayout;
```

#### 8.2 Calling Convention

```c
// RISC-V calling convention for Mobius functions
// Follows RISC-V ABI with Mobius-specific extensions

// Function prologue (RISC-V)
void generate_function_prologue(RISCVCodeGen* gen, int local_count) {
    // addi sp, sp, -frame_size
    // sd   ra, frame_size-8(sp)    # Save return address
    // sd   s0, frame_size-16(sp)   # Save frame pointer
    // addi s0, sp, frame_size      # Set new frame pointer
    
    int frame_size = (local_count + 2) * 8;  // locals + ra + s0
    
    emit_addi(gen, REG_SP, REG_SP, -frame_size);
    emit_sd(gen, REG_RA, REG_SP, frame_size - 8);
    emit_sd(gen, REG_S0, REG_SP, frame_size - 16);
    emit_addi(gen, REG_S0, REG_SP, frame_size);
}

// Function epilogue (RISC-V)
void generate_function_epilogue(RISCVCodeGen* gen, int frame_size) {
    // ld   ra, frame_size-8(sp)    # Restore return address
    // ld   s0, frame_size-16(sp)   # Restore frame pointer
    // addi sp, sp, frame_size      # Restore stack pointer
    // ret                          # Return
    
    emit_ld(gen, REG_RA, REG_SP, frame_size - 8);
    emit_ld(gen, REG_S0, REG_SP, frame_size - 16);
    emit_addi(gen, REG_SP, REG_SP, frame_size);
    emit_ret(gen);
}
```

### 9. Performance Considerations

#### 9.1 RISC-V Optimization Strategies

1. **Instruction Cache Efficiency**
   - Compact bytecode representation
   - Hot code clustering
   - Branch target alignment

2. **Data Cache Optimization**
   - Value stack locality
   - Constant pool organization
   - Structure padding for cache lines

3. **Branch Prediction**
   - Consistent branch patterns
   - Loop optimization
   - Indirect branch minimization

4. **Register Allocation**
   - Hot variable identification
   - Register pressure management
   - Spill code minimization

#### 9.2 Benchmarking Framework

```c
// Performance measurement for RISC-V optimization
typedef struct {
    uint64_t instruction_count;
    uint64_t cycle_count;
    uint64_t cache_misses;
    uint64_t branch_mispredictions;
    
    // RISC-V specific counters
    uint64_t load_stalls;
    uint64_t store_stalls;
    uint64_t multiply_stalls;
    
} RISCVPerformanceCounters;

// Benchmark bytecode vs native performance
void benchmark_execution_modes(BytecodeChunk* chunk) {
    RISCVPerformanceCounters bytecode_perf = {0};
    RISCVPerformanceCounters native_perf = {0};
    
    // Measure bytecode execution
    measure_performance(&bytecode_perf, execute_bytecode, chunk);
    
    // Measure native RISC-V execution
    JITCompiledFunction* native = compile_to_riscv(chunk, 0);
    measure_performance(&native_perf, execute_native, native);
    
    // Compare and optimize
    analyze_performance_difference(&bytecode_perf, &native_perf);
}
```

### 10. Implementation Roadmap

#### Phase 1: Core Bytecode VM
- [ ] Basic instruction set implementation
- [ ] Stack-based execution engine
- [ ] AST to bytecode compiler
- [ ] Constant pool management

#### Phase 2: RISC-V Integration
- [ ] Register allocation hints
- [ ] RISC-V instruction mapping
- [ ] Native code generation framework
- [ ] Calling convention implementation

#### Phase 3: JIT Compilation
- [ ] Hotspot detection
- [ ] Dynamic compilation
- [ ] Code cache management
- [ ] Performance profiling

#### Phase 4: Advanced Optimization
- [ ] Interprocedural optimization
- [ ] Loop optimization
- [ ] Vectorization (RISC-V Vector Extension)
- [ ] Memory layout optimization

### 11. Testing Strategy

#### 11.1 Correctness Testing
- Bytecode generation correctness
- VM execution semantics
- RISC-V code generation validation
- Cross-platform compatibility

#### 11.2 Performance Testing
- Bytecode vs tree-walking interpreter
- Native RISC-V vs bytecode execution
- JIT compilation overhead analysis
- Memory usage profiling

#### 11.3 RISC-V Specific Testing
- Instruction encoding validation
- ABI compliance testing
- Performance counter analysis
- Cache behavior optimization

This design provides a solid foundation for a RISC-V-targeted bytecode system that can efficiently bridge high-level Mobius language constructs to native RISC-V execution while maintaining portability and performance.
