#ifndef MOBIUS_RISCV_CODEGEN_H
#define MOBIUS_RISCV_CODEGEN_H

#include "bytecode.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// =============================================================================
// RISC-V NATIVE CODE GENERATION
// =============================================================================

// RISC-V Register Definitions (RV64I)
typedef enum {
    // Zero register
    REG_ZERO = 0,   // x0: hardwired zero
    
    // Return address
    REG_RA = 1,     // x1: return address
    
    // Stack and global pointers
    REG_SP = 2,     // x2: stack pointer
    REG_GP = 3,     // x3: global pointer
    REG_TP = 4,     // x4: thread pointer
    
    // Temporary registers (caller-saved)
    REG_T0 = 5,     // x5: temporary 0
    REG_T1 = 6,     // x6: temporary 1
    REG_T2 = 7,     // x7: temporary 2
    
    // Saved registers (callee-saved)
    REG_S0 = 8,     // x8: saved 0 / frame pointer
    REG_FP = 8,     // x8: frame pointer (alias)
    REG_S1 = 9,     // x9: saved 1
    
    // Function arguments/return values (caller-saved)
    REG_A0 = 10,    // x10: argument/return 0
    REG_A1 = 11,    // x11: argument/return 1
    REG_A2 = 12,    // x12: argument 2
    REG_A3 = 13,    // x13: argument 3
    REG_A4 = 14,    // x14: argument 4
    REG_A5 = 15,    // x15: argument 5
    REG_A6 = 16,    // x16: argument 6
    REG_A7 = 17,    // x17: argument 7
    
    // Saved registers (callee-saved)
    REG_S2 = 18,    // x18: saved 2
    REG_S3 = 19,    // x19: saved 3
    REG_S4 = 20,    // x20: saved 4
    REG_S5 = 21,    // x21: saved 5
    REG_S6 = 22,    // x22: saved 6
    REG_S7 = 23,    // x23: saved 7
    REG_S8 = 24,    // x24: saved 8
    REG_S9 = 25,    // x25: saved 9
    REG_S10 = 26,   // x26: saved 10
    REG_S11 = 27,   // x27: saved 11
    
    // Additional temporary registers (caller-saved)
    REG_T3 = 28,    // x28: temporary 3
    REG_T4 = 29,    // x29: temporary 4
    REG_T5 = 30,    // x30: temporary 5
    REG_T6 = 31,    // x31: temporary 6
} RISCVRegister;

// RISC-V Floating-Point Registers
typedef enum {
    // Temporary FP registers (caller-saved)
    FREG_FT0 = 0,   // f0: temporary 0
    FREG_FT1 = 1,   // f1: temporary 1
    FREG_FT2 = 2,   // f2: temporary 2
    FREG_FT3 = 3,   // f3: temporary 3
    FREG_FT4 = 4,   // f4: temporary 4
    FREG_FT5 = 5,   // f5: temporary 5
    FREG_FT6 = 6,   // f6: temporary 6
    FREG_FT7 = 7,   // f7: temporary 7
    
    // Saved FP registers (callee-saved)
    FREG_FS0 = 8,   // f8: saved 0
    FREG_FS1 = 9,   // f9: saved 1
    
    // FP arguments/return values (caller-saved)
    FREG_FA0 = 10,  // f10: argument/return 0
    FREG_FA1 = 11,  // f11: argument/return 1
    FREG_FA2 = 12,  // f12: argument 2
    FREG_FA3 = 13,  // f13: argument 3
    FREG_FA4 = 14,  // f14: argument 4
    FREG_FA5 = 15,  // f15: argument 5
    FREG_FA6 = 16,  // f16: argument 6
    FREG_FA7 = 17,  // f17: argument 7
    
    // Saved FP registers (callee-saved)
    FREG_FS2 = 18,  // f18: saved 2
    FREG_FS3 = 19,  // f19: saved 3
    FREG_FS4 = 20,  // f20: saved 4
    FREG_FS5 = 21,  // f21: saved 5
    FREG_FS6 = 22,  // f22: saved 6
    FREG_FS7 = 23,  // f23: saved 7
    FREG_FS8 = 24,  // f24: saved 8
    FREG_FS9 = 25,  // f25: saved 9
    FREG_FS10 = 26, // f26: saved 10
    FREG_FS11 = 27, // f27: saved 11
    
    // Additional temporary FP registers (caller-saved)
    FREG_FT8 = 28,  // f28: temporary 8
    FREG_FT9 = 29,  // f29: temporary 9
    FREG_FT10 = 30, // f30: temporary 10
    FREG_FT11 = 31, // f31: temporary 11
} RISCVFPRegister;

// RISC-V Instruction Types
typedef enum {
    RISCV_R_TYPE,   // Register-register operations
    RISCV_I_TYPE,   // Immediate operations
    RISCV_S_TYPE,   // Store operations
    RISCV_B_TYPE,   // Branch operations
    RISCV_U_TYPE,   // Upper immediate operations
    RISCV_J_TYPE,   // Jump operations
} RISCVInstructionType;

// RISC-V Code Generator State
typedef struct {
    uint32_t* code_buffer;       // Generated instruction buffer
    size_t code_pos;             // Current position in buffer
    size_t code_capacity;        // Buffer capacity
    
    // Register allocation state
    bool register_used[32];      // General-purpose register usage
    bool fp_register_used[32];   // Floating-point register usage
    int register_map[256];       // Bytecode slot -> RISC-V register mapping
    
    // Label and jump management
    struct {
        int address;             // Target address (-1 if unresolved)
        int* patch_locations;    // Locations that need patching
        size_t patch_count;
        size_t patch_capacity;
    } labels[256];
    size_t label_count;
    
    // Function call state
    int current_frame_size;      // Current function frame size
    int max_call_args;           // Maximum arguments in any call
    
    // Optimization state
    bool in_hot_path;            // Currently in hot path
    int loop_depth;              // Current loop nesting depth
    
} RISCVCodeGen;

// Compiled native function
typedef struct {
    uint32_t* code;              // Native RISC-V instructions
    size_t code_size;            // Size in bytes
    void* entry_point;           // Executable entry point
    
    // Metadata
    int local_count;             // Number of local variables
    int param_count;             // Number of parameters
    bool is_leaf;                // Leaf function (no calls)
    
    // Performance data
    int execution_count;         // Number of times executed
    uint64_t total_cycles;       // Total execution cycles
    
} RISCVCompiledFunction;

// JIT compilation context
typedef struct {
    RISCVCodeGen* codegen;       // Code generator
    BytecodeChunk* chunk;        // Source bytecode
    MobiusVM* vm;                // Target VM
    
    // Compilation options
    bool optimize_for_size;      // Optimize for code size vs speed
    bool enable_profiling;       // Include profiling instrumentation
    int optimization_level;      // 0=none, 1=basic, 2=aggressive
    
    // Runtime feedback
    int* execution_counts;       // Per-instruction execution counts
    bool* branch_taken;          // Branch taken statistics
    
} RISCVJITContext;

// =============================================================================
// RISC-V INSTRUCTION ENCODING FUNCTIONS
// =============================================================================

// R-Type instruction encoding
uint32_t encode_r_type(uint8_t opcode, uint8_t rd, uint8_t funct3, 
                       uint8_t rs1, uint8_t rs2, uint8_t funct7);

// I-Type instruction encoding
uint32_t encode_i_type(uint8_t opcode, uint8_t rd, uint8_t funct3, 
                       uint8_t rs1, int16_t imm);

// S-Type instruction encoding
uint32_t encode_s_type(uint8_t opcode, uint8_t funct3, uint8_t rs1, 
                       uint8_t rs2, int16_t imm);

// B-Type instruction encoding
uint32_t encode_b_type(uint8_t opcode, uint8_t funct3, uint8_t rs1, 
                       uint8_t rs2, int16_t imm);

// U-Type instruction encoding
uint32_t encode_u_type(uint8_t opcode, uint8_t rd, int32_t imm);

// J-Type instruction encoding
uint32_t encode_j_type(uint8_t opcode, uint8_t rd, int32_t imm);

// =============================================================================
// RISC-V INSTRUCTION GENERATION
// =============================================================================

// Code generator management
RISCVCodeGen* riscv_codegen_create(size_t initial_capacity);
void riscv_codegen_free(RISCVCodeGen* gen);
void riscv_emit_instruction(RISCVCodeGen* gen, uint32_t instruction);

// Register allocation
int riscv_allocate_register(RISCVCodeGen* gen, RegisterHint hint);
int riscv_allocate_fp_register(RISCVCodeGen* gen);
void riscv_free_register(RISCVCodeGen* gen, int reg);
void riscv_free_fp_register(RISCVCodeGen* gen, int reg);

// Label management
int riscv_create_label(RISCVCodeGen* gen);
void riscv_bind_label(RISCVCodeGen* gen, int label);
void riscv_patch_jumps(RISCVCodeGen* gen, int label);

// Basic arithmetic instructions
void riscv_emit_add(RISCVCodeGen* gen, int rd, int rs1, int rs2);
void riscv_emit_addi(RISCVCodeGen* gen, int rd, int rs1, int16_t imm);
void riscv_emit_sub(RISCVCodeGen* gen, int rd, int rs1, int rs2);
void riscv_emit_mul(RISCVCodeGen* gen, int rd, int rs1, int rs2);
void riscv_emit_div(RISCVCodeGen* gen, int rd, int rs1, int rs2);

// Logical instructions
void riscv_emit_and(RISCVCodeGen* gen, int rd, int rs1, int rs2);
void riscv_emit_andi(RISCVCodeGen* gen, int rd, int rs1, int16_t imm);
void riscv_emit_or(RISCVCodeGen* gen, int rd, int rs1, int rs2);
void riscv_emit_ori(RISCVCodeGen* gen, int rd, int rs1, int16_t imm);
void riscv_emit_xor(RISCVCodeGen* gen, int rd, int rs1, int rs2);
void riscv_emit_xori(RISCVCodeGen* gen, int rd, int rs1, int16_t imm);

// Shift instructions
void riscv_emit_sll(RISCVCodeGen* gen, int rd, int rs1, int rs2);
void riscv_emit_slli(RISCVCodeGen* gen, int rd, int rs1, int shamt);
void riscv_emit_srl(RISCVCodeGen* gen, int rd, int rs1, int rs2);
void riscv_emit_srli(RISCVCodeGen* gen, int rd, int rs1, int shamt);
void riscv_emit_sra(RISCVCodeGen* gen, int rd, int rs1, int rs2);
void riscv_emit_srai(RISCVCodeGen* gen, int rd, int rs1, int shamt);

// Comparison instructions
void riscv_emit_slt(RISCVCodeGen* gen, int rd, int rs1, int rs2);
void riscv_emit_slti(RISCVCodeGen* gen, int rd, int rs1, int16_t imm);
void riscv_emit_sltu(RISCVCodeGen* gen, int rd, int rs1, int rs2);
void riscv_emit_sltiu(RISCVCodeGen* gen, int rd, int rs1, int16_t imm);

// Load/Store instructions
void riscv_emit_ld(RISCVCodeGen* gen, int rd, int rs1, int16_t offset);
void riscv_emit_lw(RISCVCodeGen* gen, int rd, int rs1, int16_t offset);
void riscv_emit_lh(RISCVCodeGen* gen, int rd, int rs1, int16_t offset);
void riscv_emit_lb(RISCVCodeGen* gen, int rd, int rs1, int16_t offset);
void riscv_emit_sd(RISCVCodeGen* gen, int rs2, int rs1, int16_t offset);
void riscv_emit_sw(RISCVCodeGen* gen, int rs2, int rs1, int16_t offset);
void riscv_emit_sh(RISCVCodeGen* gen, int rs2, int rs1, int16_t offset);
void riscv_emit_sb(RISCVCodeGen* gen, int rs2, int rs1, int16_t offset);

// Branch instructions
void riscv_emit_beq(RISCVCodeGen* gen, int rs1, int rs2, int label);
void riscv_emit_bne(RISCVCodeGen* gen, int rs1, int rs2, int label);
void riscv_emit_blt(RISCVCodeGen* gen, int rs1, int rs2, int label);
void riscv_emit_bge(RISCVCodeGen* gen, int rs1, int rs2, int label);
void riscv_emit_bltu(RISCVCodeGen* gen, int rs1, int rs2, int label);
void riscv_emit_bgeu(RISCVCodeGen* gen, int rs1, int rs2, int label);

// Jump instructions
void riscv_emit_jal(RISCVCodeGen* gen, int rd, int label);
void riscv_emit_jalr(RISCVCodeGen* gen, int rd, int rs1, int16_t offset);
void riscv_emit_ret(RISCVCodeGen* gen);

// Upper immediate instructions
void riscv_emit_lui(RISCVCodeGen* gen, int rd, int32_t imm);
void riscv_emit_auipc(RISCVCodeGen* gen, int rd, int32_t imm);

// Floating-point instructions (RV64F/D)
void riscv_emit_fadd_s(RISCVCodeGen* gen, int rd, int rs1, int rs2);
void riscv_emit_fsub_s(RISCVCodeGen* gen, int rd, int rs1, int rs2);
void riscv_emit_fmul_s(RISCVCodeGen* gen, int rd, int rs1, int rs2);
void riscv_emit_fdiv_s(RISCVCodeGen* gen, int rd, int rs1, int rs2);
void riscv_emit_fadd_d(RISCVCodeGen* gen, int rd, int rs1, int rs2);
void riscv_emit_fsub_d(RISCVCodeGen* gen, int rd, int rs1, int rs2);
void riscv_emit_fmul_d(RISCVCodeGen* gen, int rd, int rs1, int rs2);
void riscv_emit_fdiv_d(RISCVCodeGen* gen, int rd, int rs1, int rs2);

// Floating-point load/store
void riscv_emit_flw(RISCVCodeGen* gen, int rd, int rs1, int16_t offset);
void riscv_emit_fld(RISCVCodeGen* gen, int rd, int rs1, int16_t offset);
void riscv_emit_fsw(RISCVCodeGen* gen, int rs2, int rs1, int16_t offset);
void riscv_emit_fsd(RISCVCodeGen* gen, int rs2, int rs1, int16_t offset);

// =============================================================================
// BYTECODE TO RISC-V COMPILATION
// =============================================================================

// High-level compilation functions
RISCVCompiledFunction* compile_bytecode_to_riscv(BytecodeChunk* chunk, 
                                                 int start_pc, int end_pc);
bool compile_instruction_to_riscv(RISCVCodeGen* gen, Instruction inst, 
                                  BytecodeChunk* chunk);

// Specific bytecode instruction compilation
void compile_push_constant_riscv(RISCVCodeGen* gen, Value constant);
void compile_arithmetic_riscv(RISCVCodeGen* gen, Opcode op);
void compile_comparison_riscv(RISCVCodeGen* gen, Opcode op);
void compile_branch_riscv(RISCVCodeGen* gen, Opcode op, int16_t offset);
void compile_call_riscv(RISCVCodeGen* gen, int arg_count);
void compile_return_riscv(RISCVCodeGen* gen);

// Function prologue/epilogue generation
void generate_function_prologue_riscv(RISCVCodeGen* gen, int local_count, 
                                      int param_count);
void generate_function_epilogue_riscv(RISCVCodeGen* gen, int frame_size);

// Stack management
void riscv_push_value(RISCVCodeGen* gen, int src_reg);
void riscv_pop_value(RISCVCodeGen* gen, int dst_reg);
void riscv_peek_value(RISCVCodeGen* gen, int dst_reg, int depth);

// =============================================================================
// JIT COMPILATION AND EXECUTION
// =============================================================================

// JIT context management
RISCVJITContext* riscv_jit_create(MobiusVM* vm);
void riscv_jit_free(RISCVJITContext* jit);

// Hot spot detection and compilation
bool riscv_jit_should_compile(RISCVJITContext* jit, int pc);
RISCVCompiledFunction* riscv_jit_compile_function(RISCVJITContext* jit, 
                                                  BytecodeChunk* chunk, 
                                                  int start_pc);

// Native execution
bool execute_riscv_function(MobiusVM* vm, RISCVCompiledFunction* func);
void riscv_call_native_function(MobiusVM* vm, void* entry_point, 
                                int arg_count);

// Code cache management
void* riscv_allocate_executable_memory(size_t size);
void riscv_free_executable_memory(void* ptr, size_t size);
void riscv_make_executable(void* ptr, size_t size);

// =============================================================================
// OPTIMIZATION PASSES
// =============================================================================

// Register allocation optimization
void riscv_optimize_register_allocation(RISCVCodeGen* gen, BytecodeChunk* chunk);
void riscv_analyze_variable_lifetime(BytecodeChunk* chunk, int* lifetimes);
void riscv_assign_registers(RISCVCodeGen* gen, int* lifetimes, int var_count);

// Branch optimization
void riscv_optimize_branches(RISCVCodeGen* gen);
void riscv_predict_branch_targets(BytecodeChunk* chunk, bool* predictions);
void riscv_reorder_basic_blocks(RISCVCodeGen* gen, bool* predictions);

// Instruction selection optimization
void riscv_optimize_instruction_selection(RISCVCodeGen* gen);
void riscv_combine_instructions(RISCVCodeGen* gen);
void riscv_use_compressed_instructions(RISCVCodeGen* gen);

// Loop optimization
void riscv_optimize_loops(RISCVCodeGen* gen, BytecodeChunk* chunk);
void riscv_unroll_small_loops(RISCVCodeGen* gen);
void riscv_optimize_loop_invariants(RISCVCodeGen* gen);

// =============================================================================
// DEBUGGING AND PROFILING
// =============================================================================

// Disassembly
void riscv_disassemble_function(RISCVCompiledFunction* func, const char* name);
void riscv_disassemble_instruction(uint32_t instruction, int pc);
const char* riscv_instruction_name(uint32_t instruction);

// Performance analysis
void riscv_profile_function(RISCVCompiledFunction* func);
void riscv_print_performance_stats(RISCVCompiledFunction* func);
void riscv_analyze_hotspots(RISCVJITContext* jit);

// Debug support
void riscv_insert_breakpoint(RISCVCodeGen* gen);
void riscv_insert_profiling_hooks(RISCVCodeGen* gen);
void riscv_generate_debug_info(RISCVCodeGen* gen, BytecodeChunk* chunk);

#endif // MOBIUS_RISCV_CODEGEN_H

