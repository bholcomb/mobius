#ifndef MOBIUS_BYTECODE_H
#define MOBIUS_BYTECODE_H

#include "value.h"
#include "ast.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// =============================================================================
// RISC-V TARGETED BYTECODE SYSTEM
// =============================================================================

// Forward declarations
typedef struct MobiusVM MobiusVM;
typedef struct BytecodeChunk BytecodeChunk;
typedef struct CompilerContext CompilerContext;

// =============================================================================
// INSTRUCTION FORMAT (RISC-V Compatible)
// =============================================================================

// 32-bit instruction format designed for RISC-V efficiency
typedef struct {
    uint8_t opcode;     // 8-bit opcode (256 possible instructions)
    uint8_t reg_hint;   // Register allocation hint for native compilation
    uint16_t operand;   // 16-bit operand (immediate, offset, or register)
} Instruction;

// Extended instruction for complex operations requiring more data
typedef struct {
    uint8_t opcode;     // OP_EXTENDED marker
    uint8_t sub_opcode; // Actual extended operation
    uint16_t operand1;  // First operand
    uint32_t operand2;  // Second operand (next 32 bits)
} ExtendedInstruction;

// =============================================================================
// OPCODE DEFINITIONS (RISC-V Optimized)
// =============================================================================

typedef enum {
    // Stack Operations (0x00-0x1F) - RISC-V Memory Mapping
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
    OP_PUSH_CONSTANT    = 0x0B,  // Push constant (operand = constant index)
    
    OP_POP              = 0x10,  // Pop top value
    OP_DUP              = 0x11,  // Duplicate top value
    OP_SWAP             = 0x12,  // Swap top two values
    OP_ROT3             = 0x13,  // Rotate top 3 values
    OP_PEEK             = 0x14,  // Peek at stack value (operand = depth)
    
    // Arithmetic Operations (0x20-0x2F) - RISC-V ALU Mapping
    OP_ADD              = 0x20,  // Add top two values
    OP_SUB              = 0x21,  // Subtract (second - top)
    OP_MUL              = 0x22,  // Multiply
    OP_DIV              = 0x23,  // Divide (second / top)
    OP_MOD              = 0x24,  // Modulo
    OP_NEG              = 0x25,  // Negate top value
    OP_ABS              = 0x26,  // Absolute value
    
    // Bitwise Operations (0x28-0x2F) - Direct RISC-V Mapping
    OP_AND              = 0x28,  // Bitwise AND
    OP_OR               = 0x29,  // Bitwise OR
    OP_XOR              = 0x2A,  // Bitwise XOR
    OP_NOT              = 0x2B,  // Bitwise NOT
    OP_SHL              = 0x2C,  // Shift left
    OP_SHR              = 0x2D,  // Shift right (logical)
    OP_SAR              = 0x2E,  // Shift right (arithmetic)
    
    // Comparison Operations (0x30-0x3F) - RISC-V Branch Mapping
    OP_EQ               = 0x30,  // Equal (==)
    OP_NE               = 0x31,  // Not equal (!=)
    OP_LT               = 0x32,  // Less than (<)
    OP_LE               = 0x33,  // Less or equal (<=)
    OP_GT               = 0x34,  // Greater than (>)
    OP_GE               = 0x35,  // Greater or equal (>=)
    
    // Logical Operations
    OP_LOGICAL_AND      = 0x38,  // Logical AND (&&)
    OP_LOGICAL_OR       = 0x39,  // Logical OR (||)
    OP_LOGICAL_NOT      = 0x3A,  // Logical NOT (!)
    
    // Memory Operations (0x40-0x5F) - RISC-V Load/Store Architecture
    OP_LOAD_LOCAL       = 0x40,  // Load local variable (operand = slot index)
    OP_STORE_LOCAL      = 0x41,  // Store to local variable
    OP_LOAD_GLOBAL      = 0x42,  // Load global variable (operand = name index)
    OP_STORE_GLOBAL     = 0x43,  // Store to global variable
    OP_LOAD_UPVALUE     = 0x44,  // Load upvalue (closure variable)
    OP_STORE_UPVALUE    = 0x45,  // Store to upvalue
    
    // Array Operations (0x48-0x4F) - Structured Memory Access
    OP_ARRAY_NEW        = 0x48,  // Create new array (operand = initial capacity)
    OP_ARRAY_GET        = 0x49,  // Get array element (array, index on stack)
    OP_ARRAY_SET        = 0x4A,  // Set array element (array, index, value on stack)
    OP_ARRAY_PUSH       = 0x4B,  // Push to array (array, value on stack)
    OP_ARRAY_POP        = 0x4C,  // Pop from array
    OP_ARRAY_LEN        = 0x4D,  // Get array length
    
    // Table Operations (0x50-0x5F)
    OP_TABLE_NEW        = 0x50,  // Create new table
    OP_TABLE_GET        = 0x51,  // Get table value (table, key on stack)
    OP_TABLE_SET        = 0x52,  // Set table value (table, key, value on stack)
    OP_TABLE_HAS        = 0x53,  // Check if table has key
    OP_TABLE_DELETE     = 0x54,  // Delete table key
    OP_TABLE_KEYS       = 0x55,  // Get table keys as array
    OP_TABLE_VALUES     = 0x56,  // Get table values as array
    
    // Control Flow (0x60-0x6F) - RISC-V Branch/Jump Mapping
    OP_JUMP             = 0x60,  // Unconditional jump (operand = offset)
    OP_JUMP_IF_FALSE    = 0x61,  // Jump if top of stack is false
    OP_JUMP_IF_TRUE     = 0x62,  // Jump if top of stack is true
    OP_JUMP_IF_NIL      = 0x63,  // Jump if top of stack is nil
    OP_JUMP_IF_NOT_NIL  = 0x64,  // Jump if top of stack is not nil
    
    // Loop Operations - Optimized for RISC-V
    OP_LOOP             = 0x65,  // Loop back (operand = negative offset)
    OP_FOR_PREP         = 0x66,  // Prepare for loop (numeric for)
    OP_FOR_LOOP         = 0x67,  // For loop iteration
    
    // Function Operations (0x68-0x6F) - RISC-V Calling Convention
    OP_CALL             = 0x68,  // Call function (operand = arg count)
    OP_CALL_BUILTIN     = 0x69,  // Call builtin function (operand = builtin index)
    OP_RETURN           = 0x6A,  // Return from function
    OP_RETURN_NIL       = 0x6B,  // Return nil (optimized)
    OP_CLOSURE          = 0x6C,  // Create closure (operand = function index)
    OP_CLOSE_UPVALUE    = 0x6D,  // Close upvalue
    
    // Type Operations (0x70-0x7F)
    OP_TYPE_CHECK       = 0x70,  // Check value type (operand = expected type)
    OP_TYPE_CAST        = 0x71,  // Cast value type
    OP_IS_NIL           = 0x72,  // Check if value is nil
    OP_IS_TRUTHY        = 0x73,  // Check if value is truthy
    OP_GET_TYPE         = 0x74,  // Get value type
    
    // String Operations (0x78-0x7F)
    OP_STRING_CONCAT    = 0x78,  // Concatenate strings
    OP_STRING_LEN       = 0x79,  // Get string length
    OP_STRING_SLICE     = 0x7A,  // String slicing
    OP_STRING_CHAR_AT   = 0x7B,  // Get character at index
    
    // Advanced Operations (0x80-0xEF)
    OP_PRINT            = 0x80,  // Print value (debug/builtin)
    OP_ASSERT           = 0x81,  // Runtime assertion
    OP_THROW            = 0x82,  // Throw exception
    OP_TRY_BEGIN        = 0x83,  // Begin try block
    OP_TRY_END          = 0x84,  // End try block
    OP_CATCH            = 0x85,  // Catch exception
    
    // Optimization Hints (0xF0-0xFE)
    OP_HOT_PATH         = 0xF0,  // Mark hot path for JIT
    OP_COLD_PATH        = 0xF1,  // Mark cold path
    OP_INLINE_HINT      = 0xF2,  // Inline function hint
    OP_NO_INLINE        = 0xF3,  // Prevent inlining
    
    // Debug/Profiling (0xF4-0xFE)
    OP_DEBUG_BREAK      = 0xF4,  // Debug breakpoint
    OP_PROFILE_ENTER    = 0xF5,  // Profiling function entry
    OP_PROFILE_EXIT     = 0xF6,  // Profiling function exit
    OP_LINE_INFO        = 0xF7,  // Line number information
    
    // Extended Operations
    OP_EXTENDED         = 0xFF,  // Indicates extended instruction follows
} Opcode;

// Extended opcodes for complex operations
typedef enum {
    EXT_OP_REGEX_MATCH      = 0x00,  // Regular expression matching
    EXT_OP_JSON_PARSE       = 0x01,  // JSON parsing
    EXT_OP_JSON_STRINGIFY   = 0x02,  // JSON serialization
    EXT_OP_HTTP_GET         = 0x03,  // HTTP GET request
    EXT_OP_FILE_READ        = 0x04,  // File reading
    EXT_OP_FILE_WRITE       = 0x05,  // File writing
    EXT_OP_MATH_SIN         = 0x06,  // Math functions
    EXT_OP_MATH_COS         = 0x07,
    EXT_OP_MATH_SQRT        = 0x08,
    // ... more extended operations
} ExtendedOpcode;

// =============================================================================
// RISC-V REGISTER HINTS
// =============================================================================

typedef enum {
    REG_HINT_NONE       = 0x00,  // No preference
    REG_HINT_TEMP       = 0x01,  // Temporary register (t0-t6)
    REG_HINT_SAVED      = 0x02,  // Saved register (s0-s11)
    REG_HINT_ARG        = 0x03,  // Argument register (a0-a7)
    REG_HINT_RETURN     = 0x04,  // Return value register
    REG_HINT_FLOAT      = 0x05,  // Floating-point register
    REG_HINT_HOT        = 0x06,  // Hot variable (prefer saved register)
    REG_HINT_SPILL      = 0x07,  // Likely to spill (use memory)
} RegisterHint;

// =============================================================================
// BYTECODE CHUNK STRUCTURE
// =============================================================================

typedef struct {
    Instruction* instructions;    // Bytecode instructions
    size_t count;                // Number of instructions
    size_t capacity;             // Allocated capacity
    
    // RISC-V optimization metadata
    uint32_t* register_hints;    // Register allocation hints per instruction
    uint32_t* branch_targets;    // Branch target addresses
    bool* hot_paths;             // Hot path identification
    
    // Constant pools
    Value* constants;            // Constant values
    char** string_pool;          // String literals
    size_t constant_count;
    size_t string_count;
    
    // Debug information
    int* line_numbers;           // Source line numbers
    char** source_files;         // Source file names
    size_t debug_info_count;
    
    // Function metadata
    struct {
        char* name;              // Function name
        int start_pc;            // Starting program counter
        int end_pc;              // Ending program counter
        int local_count;         // Number of local variables
        int upvalue_count;       // Number of upvalues
        bool is_hot;             // Frequently called function
    } functions[256];
    size_t function_count;
    
} BytecodeChunk;

// =============================================================================
// COMPILATION CONTEXT
// =============================================================================

typedef struct {
    BytecodeChunk* chunk;
    
    // Local variable tracking (for register allocation)
    struct {
        char* name;
        int slot;
        bool is_hot;             // Frequently accessed
        int risc_v_reg_hint;     // Preferred RISC-V register
        int scope_depth;         // Lexical scope depth
        bool is_captured;        // Captured by closure
    } locals[256];
    int local_count;
    
    // Upvalue tracking
    struct {
        int local_index;         // Index in enclosing function's locals
        bool is_local;           // True if in immediately enclosing function
    } upvalues[256];
    int upvalue_count;
    
    // Control flow for branch optimization
    struct {
        int start;
        int end;
        bool is_loop;
        int break_jumps[32];     // Jump addresses for break statements
        int continue_jumps[32];  // Jump addresses for continue statements
        int break_count;
        int continue_count;
    } scopes[64];
    int scope_depth;
    
    // Enclosing compiler (for nested functions)
    CompilerContext* enclosing;
    
    // Current function being compiled
    MobiusFunction* function;
    
    // Error handling
    bool had_error;
    bool panic_mode;
    
} CompilerContext;

// =============================================================================
// VIRTUAL MACHINE STATE
// =============================================================================

typedef struct CallFrame {
    Instruction* return_ip;      // Return instruction pointer
    Value* slots;                // Local variable slots
    MobiusFunction* function;    // Current function
} CallFrame;

struct MobiusVM {
    // Execution state
    Instruction* ip;             // Instruction pointer
    Value* stack;                // Value stack
    Value* stack_top;            // Stack top pointer
    size_t stack_capacity;
    
    // RISC-V virtual registers (for JIT compilation)
    uint64_t registers[32];      // General-purpose registers
    double fp_registers[32];     // Floating-point registers
    
    // Function call state
    CallFrame frames[256];       // Call stack
    int frame_count;
    
    // Global state
    Table* globals;              // Global variables
    Value* constants;            // Constant pool
    char** strings;              // String pool
    
    // Current execution context
    BytecodeChunk* current_chunk;
    
    // RISC-V JIT support
    void* jit_cache;             // Compiled native code cache
    bool jit_enabled;            // JIT compilation enabled
    int jit_threshold;           // Hotness threshold for JIT
    
    // Garbage collection
    size_t bytes_allocated;
    size_t next_gc;
    bool gc_enabled;
    
    // Error handling
    bool has_error;
    char* error_message;
    
};

// =============================================================================
// FUNCTION DECLARATIONS
// =============================================================================

// Bytecode chunk management
BytecodeChunk* bytecode_chunk_create(void);
void bytecode_chunk_free(BytecodeChunk* chunk);
void bytecode_chunk_write(BytecodeChunk* chunk, Instruction instruction);
void bytecode_chunk_write_constant(BytecodeChunk* chunk, Value value);
int bytecode_chunk_add_string(BytecodeChunk* chunk, const char* string);

// Compilation
CompilerContext* compiler_create(BytecodeChunk* chunk, MobiusFunction* function);
void compiler_free(CompilerContext* compiler);
bool compile_ast_to_bytecode(Stmt** statements, size_t count, BytecodeChunk* chunk);
bool compile_expression(CompilerContext* compiler, Expr* expr);
bool compile_statement(CompilerContext* compiler, Stmt* stmt);

// Virtual machine
MobiusVM* vm_create(void);
void vm_free(MobiusVM* vm);
bool vm_execute(MobiusVM* vm, BytecodeChunk* chunk);
void vm_push(MobiusVM* vm, Value value);
Value vm_pop(MobiusVM* vm);
Value vm_peek(MobiusVM* vm, int distance);

// RISC-V specific functions
void optimize_for_riscv(BytecodeChunk* chunk);
void* compile_to_riscv_native(BytecodeChunk* chunk, int start_pc, int end_pc);
bool execute_riscv_native(MobiusVM* vm, void* native_code);

// Debug and profiling
void bytecode_disassemble(BytecodeChunk* chunk, const char* name);
void bytecode_disassemble_instruction(BytecodeChunk* chunk, int offset);
void vm_print_stack(MobiusVM* vm);

#endif // MOBIUS_BYTECODE_H

