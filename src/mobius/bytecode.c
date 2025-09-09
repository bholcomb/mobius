#define _GNU_SOURCE  // For strdup
#include "bytecode.h"
#include "value.h"
#include "utility.h"
#include "table.h"
#include "token.h"
#include "evaluator.h"
#include "stdlib.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include <ctype.h>
#include <time.h>

// Include builtin function modules
#include "bytecode_core.h"
#include "bytecode_math.h" 
#include "bytecode_string.h"
#include "bytecode_table.h"
#include "bytecode_array.h"
#include "bytecode_util.h"
#include "bytecode_types.h"

// =============================================================================
// BYTECODE CHUNK MANAGEMENT
// =============================================================================

BytecodeChunk* bytecode_chunk_create(void) {
    BytecodeChunk* chunk = malloc(sizeof(BytecodeChunk));
    if (!chunk) return NULL;
    
    // Initialize instruction array
    chunk->instructions = NULL;
    chunk->count = 0;
    chunk->capacity = 0;
    
    // Initialize RISC-V optimization metadata
    chunk->register_hints = NULL;
    chunk->branch_targets = NULL;
    chunk->hot_paths = NULL;
    
    // Initialize constant pools
    chunk->constants = NULL;
    chunk->string_pool = NULL;
    chunk->constant_count = 0;
    chunk->string_count = 0;
    
    // Initialize enum information
    chunk->enum_definitions = NULL;
    chunk->enum_count = 0;
    chunk->enum_accesses = NULL;
    chunk->enum_access_count = 0;
    
    // Initialize debug information
    chunk->line_numbers = NULL;
    chunk->source_files = NULL;
    chunk->debug_info_count = 0;
    
    // Initialize function metadata
    chunk->function_count = 0;
    
    return chunk;
}

void bytecode_chunk_free(BytecodeChunk* chunk) {
    if (!chunk) return;
    
    // Free instruction array
    free(chunk->instructions);
    
    // Free RISC-V optimization metadata
    free(chunk->register_hints);
    free(chunk->branch_targets);
    free(chunk->hot_paths);
    
    // Free constant pool
    if (chunk->constants) {
        for (size_t i = 0; i < chunk->constant_count; i++) {
            free_value(chunk->constants[i]);
        }
        free(chunk->constants);
    }
    
    // Free string pool
    if (chunk->string_pool) {
        for (size_t i = 0; i < chunk->string_count; i++) {
            free(chunk->string_pool[i]);
        }
        free(chunk->string_pool);
    }
    
    // Free debug information
    free(chunk->line_numbers);
    if (chunk->source_files) {
        for (size_t i = 0; i < chunk->debug_info_count; i++) {
            free(chunk->source_files[i]);
        }
        free(chunk->source_files);
    }
    
    // Free function metadata
    for (size_t i = 0; i < chunk->function_count; i++) {
        free(chunk->functions[i].name);
    }
    
    // Free enum information
    if (chunk->enum_definitions) {
        for (size_t i = 0; i < chunk->enum_count; i++) {
            enum_definition_release(chunk->enum_definitions[i]);
        }
        free(chunk->enum_definitions);
    }
    free(chunk->enum_accesses);
    
    free(chunk);
}

static void bytecode_chunk_grow(BytecodeChunk* chunk) {
    size_t old_capacity = chunk->capacity;
    chunk->capacity = old_capacity < 8 ? 8 : old_capacity * 2;
    
    // Grow instruction array
    chunk->instructions = realloc(chunk->instructions, 
                                  chunk->capacity * sizeof(Instruction));
    
    // Grow metadata arrays
    chunk->register_hints = realloc(chunk->register_hints,
                                    chunk->capacity * sizeof(uint32_t));
    chunk->branch_targets = realloc(chunk->branch_targets,
                                    chunk->capacity * sizeof(uint32_t));
    chunk->hot_paths = realloc(chunk->hot_paths,
                               chunk->capacity * sizeof(bool));
    chunk->line_numbers = realloc(chunk->line_numbers,
                                  chunk->capacity * sizeof(int));
    
    // Initialize new metadata entries
    for (size_t i = old_capacity; i < chunk->capacity; i++) {
        chunk->register_hints[i] = REG_HINT_NONE;
        chunk->branch_targets[i] = 0;
        chunk->hot_paths[i] = false;
        chunk->line_numbers[i] = -1;
    }
}

void bytecode_chunk_write(BytecodeChunk* chunk, Instruction instruction) {
    if (chunk->count + 1 > chunk->capacity) {
        bytecode_chunk_grow(chunk);
    }
    
    chunk->instructions[chunk->count] = instruction;
    chunk->count++;
}

void bytecode_chunk_write_constant(BytecodeChunk* chunk, Value value) {
    // Grow constant pool if needed
    if (chunk->constant_count + 1 > 256) {  // Arbitrary limit for now
        return;  // TODO: Better error handling
    }
    
    if (!chunk->constants) {
        chunk->constants = malloc(256 * sizeof(Value));
    }
    
    // Make a copy of the value and add to constant pool
    chunk->constants[chunk->constant_count] = copy_value(value);
    chunk->constant_count++;
}

int bytecode_chunk_add_string(BytecodeChunk* chunk, const char* string) {
    if (!string) return -1;
    
    // Check if string already exists in pool
    for (size_t i = 0; i < chunk->string_count; i++) {
        if (strcmp(chunk->string_pool[i], string) == 0) {
            return (int)i;  // Return existing index
        }
    }
    
    // Grow string pool if needed
    if (chunk->string_count + 1 > 256) {  // Arbitrary limit
        return -1;  // TODO: Better error handling
    }
    
    if (!chunk->string_pool) {
        chunk->string_pool = malloc(256 * sizeof(char*));
    }
    
    // Add new string
    chunk->string_pool[chunk->string_count] = strdup(string);
    int index = (int)chunk->string_count;
    chunk->string_count++;
    
    return index;
}

// =============================================================================
// BYTECODE FUNCTION MANAGEMENT
// =============================================================================

BytecodeFunction* bytecode_function_create(const char* name, char** param_names, size_t param_count) {
    BytecodeFunction* func = malloc(sizeof(BytecodeFunction));
    if (!func) return NULL;
    
    func->name = strdup(name);
    if (!func->name) {
        free(func);
        return NULL;
    }
    
    func->param_count = param_count;
    if (param_count > 0) {
        func->param_names = malloc(param_count * sizeof(char*));
        if (!func->param_names) {
            free(func->name);
            free(func);
            return NULL;
        }
        
        for (size_t i = 0; i < param_count; i++) {
            func->param_names[i] = strdup(param_names[i]);
            if (!func->param_names[i]) {
                // Cleanup previously allocated names
                for (size_t j = 0; j < i; j++) {
                    free(func->param_names[j]);
                }
                free(func->param_names);
                free(func->name);
                free(func);
                return NULL;
            }
        }
    } else {
        func->param_names = NULL;
    }
    
    func->bytecode = NULL;  // Will be set later
    func->ref_count = 1;
    
    return func;
}

void bytecode_function_free(BytecodeFunction* func) {
    if (!func) return;
    
    func->ref_count--;
    if (func->ref_count > 0) return;
    
    free(func->name);
    
    if (func->param_names) {
        for (size_t i = 0; i < func->param_count; i++) {
            free(func->param_names[i]);
        }
        free(func->param_names);
    }
    
    if (func->bytecode) {
        bytecode_chunk_free(func->bytecode);
    }
    
    free(func);
}

// =============================================================================
// LUA-INSPIRED BUILTIN FUNCTION SYSTEM
// =============================================================================

// Use the VMBuiltinFunction typedef from value.h

// VM Builtin function registry entry
typedef struct {
    const char* name;
    VMBuiltinFunction func;
} VMBuiltinEntry;


 // VM Builtin function registry (Lua-inspired approach)
static VMBuiltinEntry builtin_registry[] = {
    // Core functions
    {"print", builtin_print_vm},
    {"typeof", builtin_typeof_vm},
    {"int", builtin_int_vm},
    {"float", builtin_float_vm},
    {"str", builtin_str_vm},
    
    // Math functions
    {"abs", builtin_abs_vm},
    {"min", builtin_min_vm},
    {"max", builtin_max_vm},
    {"pow", builtin_pow_vm},
    {"sqrt", builtin_sqrt_vm},
    {"floor", builtin_floor_vm},
    {"ceil", builtin_ceil_vm},
    {"round", builtin_round_vm},
    
    // String functions
    {"len", builtin_len_vm},
    {"upper", builtin_upper_vm},
    {"lower", builtin_lower_vm},
    {"substr", builtin_substr_vm},
    {"concat", builtin_concat_vm},
    {"contains", builtin_contains_vm},
    
    // Utility functions
    {"random", builtin_random_vm},
    {"time", builtin_time_vm},
    {"clock", builtin_clock_vm},
    
    // Table functions
    {"table_insert", builtin_table_insert_vm},
    {"table_remove", builtin_table_remove_vm},
    {"table_has_key", builtin_table_has_key_vm},
    {"table_size", builtin_table_size_vm},
    {"setmetatable", builtin_setmetatable_vm},
    {"getmetatable", builtin_getmetatable_vm},
    {"pairs", builtin_pairs_vm},

    // Array functions
    {"array_create", builtin_array_create_vm},
    {"array_push", builtin_array_push_vm},
    {"array_pop", builtin_array_pop_vm},
    {"array_get", builtin_array_get_vm},
    {"array_set", builtin_array_set_vm},
    {"array_length", builtin_array_length_vm},
    {"array_slice", builtin_array_slice_vm},
    {"array_concat", builtin_array_concat_vm},
    {"array_reverse", builtin_array_reverse_vm},
    {"array_find", builtin_array_find_vm},

    // Type system functions
    {"set_strict_types", builtin_set_strict_types_vm},
    {"set_type_warnings", builtin_set_type_warnings_vm},
    {"get_type_config", builtin_get_type_config_vm},
    
    {NULL, NULL}  // Sentinel
};

// Helper function to populate VM with builtin functions (Lua-inspired)
static void vm_populate_builtins(MobiusVM* vm) {
    if (!vm || !vm->globals) return;
    
    for (int i = 0; builtin_registry[i].name != NULL; i++) {
        // Create a special builtin function value
        Value builtin_value;
        builtin_value.type = VAL_BUILTIN_FUNCTION;
        builtin_value.as.builtin_func = builtin_registry[i].func;
        
        table_set(vm->globals, make_string_value_from_cstr(builtin_registry[i].name), builtin_value);
    }
}

// =============================================================================
// VIRTUAL MACHINE IMPLEMENTATION
// =============================================================================

MobiusVM* vm_create(void) {
    MobiusVM* vm = malloc(sizeof(MobiusVM));
    if (!vm) return NULL;
    
    // Initialize execution state
    vm->ip = NULL;
    vm->stack = malloc(256 * sizeof(Value));  // Initial stack size
    vm->stack_top = vm->stack;
    vm->stack_capacity = 256;
    
    if (!vm->stack) {
        free(vm);
        return NULL;
    }
    
    // Initialize RISC-V virtual registers
    memset(vm->registers, 0, sizeof(vm->registers));
    memset(vm->fp_registers, 0, sizeof(vm->fp_registers));
    
    // Initialize call stack
    vm->frame_count = 0;
    
    // Initialize global state
    vm->globals = create_table(16);  // Initial capacity
    vm->constants = NULL;
    vm->strings = NULL;
    
    // Initialize current chunk
    vm->current_chunk = NULL;
    
    // Populate with builtin functions (Lua-inspired approach)
    vm_populate_builtins(vm);
    
    // Initialize RISC-V JIT support
    vm->jit_cache = NULL;
    vm->jit_enabled = false;
    vm->jit_threshold = 100;  // Default hotness threshold
    
    // Initialize GC
    vm->bytes_allocated = 0;
    vm->next_gc = 1024;  // Initial GC threshold
    vm->gc_enabled = true;
    
    // Initialize error handling
    vm->has_error = false;
    vm->error_message = NULL;
    
    return vm;
}

void vm_free(MobiusVM* vm) {
    if (!vm) return;
    
    // Free stack
    if (vm->stack) {
        // Release all values on stack
        for (Value* slot = vm->stack; slot < vm->stack_top; slot++) {
            free_value(*slot);
        }
        free(vm->stack);
    }
    
    // Free global table
    if (vm->globals) {
        free_table(vm->globals);
    }
    
    // Free JIT cache
    if (vm->jit_cache) {
        // TODO: Free compiled native code
        free(vm->jit_cache);
    }
    
    // Free error message
    free(vm->error_message);
    
    free(vm);
}

void vm_push(MobiusVM* vm, Value value) {
    if (!vm || !vm->stack) return;
    
    // Check stack overflow
    if (vm->stack_top >= vm->stack + vm->stack_capacity) {
        // Grow stack
        // size_t old_size = vm->stack_capacity;  // Unused for now
        vm->stack_capacity *= 2;
        ptrdiff_t stack_offset = vm->stack_top - vm->stack;
        
        vm->stack = realloc(vm->stack, vm->stack_capacity * sizeof(Value));
        vm->stack_top = vm->stack + stack_offset;
    }
    
    *vm->stack_top = copy_value(value);  // Make copy for stack
    vm->stack_top++;
}

Value vm_pop(MobiusVM* vm) {
    if (!vm || !vm->stack || vm->stack_top <= vm->stack) {
        return make_nil_value();  // Stack underflow
    }
    
    vm->stack_top--;
    Value value = *vm->stack_top;
    return value;  // Don't release here, caller takes ownership
}

Value vm_peek(MobiusVM* vm, int distance) {
    if (!vm || !vm->stack || vm->stack_top - distance - 1 < vm->stack) {
        return make_nil_value();  // Out of bounds
    }
    
    return vm->stack_top[-1 - distance];
}

// =============================================================================
// BASIC EXECUTION ENGINE
// =============================================================================

typedef enum {
    VM_OK,
    VM_COMPILE_ERROR,
    VM_RUNTIME_ERROR,
    VM_STACK_OVERFLOW,
    VM_STACK_UNDERFLOW
} VMResult;

void vm_runtime_error(MobiusVM* vm, const char* format, ...) {
    va_list args;
    va_start(args, format);
    
    // Free previous error message
    free(vm->error_message);
    
    // Allocate new error message
    vm->error_message = malloc(256);
    if (vm->error_message) {
        vsnprintf(vm->error_message, 256, format, args);
    }
    
    vm->has_error = true;
    va_end(args);
}


static Value binary_arithmetic(MobiusVM* vm, Opcode op) {
    Value right = vm_pop(vm);
    Value left = vm_pop(vm);
    
    // Handle string concatenation for OP_ADD
    if (op == OP_ADD && (left.type == VAL_STRING || right.type == VAL_STRING)) {
        char* left_str = value_to_string(left);
        char* right_str = value_to_string(right);
        
        if (!left_str || !right_str) {
            free(left_str);
            free(right_str);
            vm_runtime_error(vm, "Memory allocation failed in string concatenation");
            return make_nil_value();
        }
        
        size_t len = strlen(left_str) + strlen(right_str) + 1;
        char* result = malloc(len);
        if (!result) {
            free(left_str);
            free(right_str);
            vm_runtime_error(vm, "Memory allocation failed");
            return make_nil_value();
        }
        
        strcpy(result, left_str);
        strcat(result, right_str);
        
        free(left_str);
        free(right_str);
        
        Value final_result = make_string_value_from_cstr(result);
        free(result);
        return final_result;
    }
    
    // Handle integer arithmetic
    if (left.type == VAL_INTEGER && right.type == VAL_INTEGER) {
        int32_t a = left.as.integer.value.i32;
        int32_t b = right.as.integer.value.i32;
        
        switch (op) {
            case OP_ADD: return make_integer_value(NUM_INT32, a + b);
            case OP_SUB: return make_integer_value(NUM_INT32, a - b);
            case OP_MUL: return make_integer_value(NUM_INT32, a * b);
            case OP_DIV: 
                if (b == 0) {
                    vm_runtime_error(vm, "Division by zero");
                    return make_nil_value();
                }
                // Division always returns float for consistency with AST interpreter
                return make_float_value((double)a / (double)b);
                break;
            case OP_MOD:
                if (b == 0) {
                    vm_runtime_error(vm, "Modulo by zero");
                    return make_nil_value();
                }
                return make_integer_value(NUM_INT32, a % b);
                break;
            default:
                vm_runtime_error(vm, "Unknown arithmetic operation");
                return make_nil_value();
        }
    }
    
    // Handle float arithmetic
    if ((left.type == VAL_FLOAT || left.type == VAL_INTEGER) &&
        (right.type == VAL_FLOAT || right.type == VAL_INTEGER)) {
        
        double a = (left.type == VAL_FLOAT) ? left.as.float_val : 
                   (double)left.as.integer.value.i32;
        double b = (right.type == VAL_FLOAT) ? right.as.float_val :
                   (double)right.as.integer.value.i32;
        
        switch (op) {
            case OP_ADD: return make_float_value(a + b);
            case OP_SUB: return make_float_value(a - b);
            case OP_MUL: return make_float_value(a * b);
            case OP_DIV:
                if (b == 0.0) {
                    vm_runtime_error(vm, "Division by zero");
                    return make_nil_value();
                }
                return make_float_value(a / b);
            case OP_MOD:
                if (b == 0.0) {
                    vm_runtime_error(vm, "Modulo by zero");
                    return make_nil_value();
                }
                return make_float_value(fmod(a, b));
            default:
                vm_runtime_error(vm, "Unknown arithmetic operation");
                return make_nil_value();
        }
    }
    
    vm_runtime_error(vm, "Invalid operands for arithmetic operation");
    return make_nil_value();
}

static bool call_value(MobiusVM* vm, Value callee, int arg_count) {
    if (callee.type == VAL_BUILTIN_FUNCTION) {
        // Lua-inspired: Builtin functions use unified calling convention
        VMBuiltinFunction builtin_func = callee.as.builtin_func;

        // Call the builtin function - it will read args from stack via vm_peek
        Value result;
        builtin_func(vm, arg_count, &result);

        // Pop arguments and function from stack
        for (int i = 0; i <= arg_count; i++) {
            vm_pop(vm);
        }

        // Push result onto stack
        vm_push(vm, result);

        return true;
    } else if (callee.type == VAL_FUNCTION) {
        // TODO: Implement AST function calls with call frames
        vm_runtime_error(vm, "AST function calls not yet implemented");
        return false;
    } else if (callee.type == VAL_BYTECODE_FUNCTION) {
        // TODO: Implement bytecode function calls with call frames
        vm_runtime_error(vm, "Bytecode function calls not yet implemented");
        return false;
    } else {
        vm_runtime_error(vm, "Can only call functions");
        return false;
    }
}

static Value binary_comparison(MobiusVM* vm, Opcode op) {
    Value right = vm_pop(vm);
    Value left = vm_pop(vm);
    
    bool result = false;
    
    // Handle numeric comparisons
    if ((left.type == VAL_INTEGER || left.type == VAL_FLOAT) &&
        (right.type == VAL_INTEGER || right.type == VAL_FLOAT)) {
        
        double a = (left.type == VAL_FLOAT) ? left.as.float_val :
                   (double)left.as.integer.value.i32;
        double b = (right.type == VAL_FLOAT) ? right.as.float_val :
                   (double)right.as.integer.value.i32;
        
        switch (op) {
            case OP_EQ: result = (a == b); break;
            case OP_NE: result = (a != b); break;
            case OP_LT: result = (a < b); break;
            case OP_LE: result = (a <= b); break;
            case OP_GT: result = (a > b); break;
            case OP_GE: result = (a >= b); break;
            default:
                vm_runtime_error(vm, "Unknown comparison operation");
                return make_nil_value();
        }
    }
    // Handle equality for other types
    else if (op == OP_EQ || op == OP_NE) {
        bool equal = values_equal(left, right);
        result = (op == OP_EQ) ? equal : !equal;
    }
    else {
        vm_runtime_error(vm, "Invalid operands for comparison");
        return make_nil_value();
    }
    
    return make_bool_value(result);
}

int vm_execute(MobiusVM* vm, BytecodeChunk* chunk) {
    if (!vm || !chunk) return VM_RUNTIME_ERROR;
    
    vm->current_chunk = chunk;
    vm->constants = chunk->constants;
    vm->strings = chunk->string_pool;
    vm->ip = chunk->instructions;
    
    #define READ_BYTE() (vm->ip++)->opcode
    #define READ_OPERAND() (vm->ip[-1].operand)
    #define READ_CONSTANT() (chunk->constants[READ_OPERAND()])
    
    for (;;) {
        if (vm->has_error) return VM_RUNTIME_ERROR;
        
        // Bounds check
        if (vm->ip >= chunk->instructions + chunk->count) {
            break;  // End of chunk
        }
        
        uint8_t instruction = READ_BYTE();
        
        switch (instruction) {
            case OP_PUSH_NIL:
                vm_push(vm, make_nil_value());
                break;
                
            case OP_PUSH_TRUE:
                vm_push(vm, make_bool_value(true));
                break;
                
            case OP_PUSH_FALSE:
                vm_push(vm, make_bool_value(false));
                break;
                
            case OP_PUSH_INT8: {
                uint8_t operand = READ_OPERAND();
                vm_push(vm, make_integer_value(NUM_INT8, (int8_t)operand));
                break;
            }
            
            case OP_PUSH_INT16: {
                uint16_t operand = READ_OPERAND();
                vm_push(vm, make_integer_value(NUM_INT16, (int16_t)operand));
                break;
            }
            
            case OP_PUSH_INT32: {
                // Read 32-bit integer from next 4 bytes
                if (vm->ip + 2 >= chunk->instructions + chunk->count) {
                    vm_runtime_error(vm, "Unexpected end of chunk reading int32");
                    return VM_RUNTIME_ERROR;
                }
                int32_t value = (int32_t)READ_OPERAND();  // This gets the full 16-bit operand
                // For true 32-bit values, we'd need to read from extended data
                // For now, treat as 16-bit sign-extended to 32-bit
                vm_push(vm, make_integer_value(NUM_INT32, value));
                break;
            }
            
            case OP_PUSH_INT64: {
                // Read 64-bit integer from next 8 bytes
                if (vm->ip + 2 >= chunk->instructions + chunk->count) {
                    vm_runtime_error(vm, "Unexpected end of chunk reading int64");
                    return VM_RUNTIME_ERROR;
                }
                int64_t value = (int64_t)READ_OPERAND();  // This gets the full 16-bit operand
                // For true 64-bit values, we'd need to read from extended data
                // For now, treat as 16-bit sign-extended to 64-bit
                vm_push(vm, make_integer_value(NUM_INT64, value));
                break;
            }
            
            case OP_PUSH_FLOAT32: {
                uint16_t operand = READ_OPERAND();
                // Convert 16-bit operand to float (limited precision)
                float value = (float)operand;
                vm_push(vm, make_float32_value(value));
                break;
            }
            
            case OP_PUSH_FLOAT64: {
                uint16_t operand = READ_OPERAND();
                // Convert 16-bit operand to double (limited precision)
                double value = (double)operand;
                vm_push(vm, make_float_value(value));
                break;
            }
            
            case OP_PUSH_STRING: {
                uint16_t string_index = READ_OPERAND();
                if (string_index >= chunk->string_count) {
                    vm_runtime_error(vm, "Invalid string index");
                    return VM_RUNTIME_ERROR;
                }
                const char* str = chunk->string_pool[string_index];
                vm_push(vm, make_string_value_from_cstr(str));
                break;
            }
            
            case OP_PUSH_CHAR: {
                uint16_t operand = READ_OPERAND();
                char character = (char)(operand & 0xFF);  // Take lower 8 bits
                vm_push(vm, make_char_value(character));
                break;
            }
            
            case OP_PUSH_UINT8: {
                uint16_t operand = READ_OPERAND();
                vm_push(vm, make_integer_value(NUM_UINT8, (uint8_t)operand));
                break;
            }
            
            case OP_PUSH_UINT16: {
                uint16_t operand = READ_OPERAND();
                vm_push(vm, make_integer_value(NUM_UINT16, operand));
                break;
            }
            
            case OP_PUSH_UINT32: {
                // Read 32-bit unsigned integer from next 4 bytes
                if (vm->ip + 2 >= chunk->instructions + chunk->count) {
                    vm_runtime_error(vm, "Unexpected end of chunk reading uint32");
                    return VM_RUNTIME_ERROR;
                }
                uint32_t value = (uint32_t)READ_OPERAND();  // This gets the full 16-bit operand
                // For true 32-bit values, we'd need to read from extended data
                // For now, treat as 16-bit extended to 32-bit
                vm_push(vm, make_integer_value(NUM_UINT32, value));
                break;
            }
            
            case OP_PUSH_UINT64: {
                // Read 64-bit unsigned integer from next 8 bytes
                if (vm->ip + 2 >= chunk->instructions + chunk->count) {
                    vm_runtime_error(vm, "Unexpected end of chunk reading uint64");
                    return VM_RUNTIME_ERROR;
                }
                uint64_t value = (uint64_t)READ_OPERAND();  // This gets the full 16-bit operand
                // For true 64-bit values, we'd need to read from extended data
                // For now, treat as 16-bit extended to 64-bit
                vm_push(vm, make_integer_value(NUM_UINT64, value));
                break;
            }
            
            case OP_PUSH_CONSTANT: {
                uint16_t index = READ_OPERAND();
                if (index < chunk->constant_count) {
                    Value constant = copy_value(chunk->constants[index]);
                    vm_push(vm, constant);
                } else {
                    vm_runtime_error(vm, "Invalid constant index");
                    return VM_RUNTIME_ERROR;
                }
                break;
            }
            
            case OP_POP:
                if (vm->stack_top > vm->stack) {
                    Value popped = vm_pop(vm);
                    free_value(popped);
                } else {
                    vm_runtime_error(vm, "Stack underflow");
                    return VM_STACK_UNDERFLOW;
                }
                break;
                
            case OP_DUP: {
                Value top = vm_peek(vm, 0);
                
                // For arrays, create deep copies to avoid shared mutation during construction
                if (top.type == VAL_ARRAY && top.as.array) {
                    ArrayValue* original = top.as.array;
                    ArrayValue* copy = array_create(original->capacity);
                    
                    // Copy all elements
                    for (size_t i = 0; i < original->length; i++) {
                        array_push(copy, original->elements[i]);
                    }
                    
                    vm_push(vm, make_array_value(copy));
                } else {
                    // For tables and other types, use normal copy (reference counting)
                    vm_push(vm, copy_value(top));
                }
                break;
            }
            
            case OP_ADD:
            case OP_SUB:
            case OP_MUL:
            case OP_DIV:
            case OP_MOD: {
                Value result = binary_arithmetic(vm, instruction);
                if (vm->has_error) return VM_RUNTIME_ERROR;
                vm_push(vm, result);
                break;
            }
            
            case OP_NEG: {
                Value operand = vm_pop(vm);
                if (operand.type == VAL_INTEGER) {
                    // Extract value properly based on the actual type, but result should be int64_t
                    int64_t value = 0;
                    switch (operand.as.integer.num_type) {
                        case NUM_INT8:   value = operand.as.integer.value.i8; break;
                        case NUM_UINT8:  value = operand.as.integer.value.u8; break;
                        case NUM_INT16:  value = operand.as.integer.value.i16; break;
                        case NUM_UINT16: value = operand.as.integer.value.u16; break;
                        case NUM_INT32:  value = operand.as.integer.value.i32; break;
                        case NUM_UINT32: value = operand.as.integer.value.u32; break;
                        case NUM_INT64:  value = operand.as.integer.value.i64; break;
                        case NUM_UINT64: value = operand.as.integer.value.u64; break;
                        default: value = operand.as.integer.value.i32; break;
                    }
                    vm_push(vm, make_integer_value(NUM_INT64, -value));
                } else if (operand.type == VAL_FLOAT) {
                    vm_push(vm, make_float_value(-operand.as.float_val));
                } else {
                    vm_runtime_error(vm, "Invalid operand for negation");
                    return VM_RUNTIME_ERROR;
                }
                break;
            }
            
            case OP_POS: {
                Value operand = vm_pop(vm);
                if (operand.type == VAL_INTEGER || operand.type == VAL_FLOAT) {
                    // Unary plus is identity for numbers
                    vm_push(vm, operand);
                } else {
                    vm_runtime_error(vm, "Invalid operand for unary plus");
                    return VM_RUNTIME_ERROR;
                }
                break;
            }
            
            case OP_EQ:
            case OP_NE:
            case OP_LT:
            case OP_LE:
            case OP_GT:
            case OP_GE: {
                Value result = binary_comparison(vm, instruction);
                if (vm->has_error) return VM_RUNTIME_ERROR;
                vm_push(vm, result);
                break;
            }
            
            case OP_LOGICAL_AND: {
                Value right = vm_pop(vm);
                Value left = vm_pop(vm);
                
                // In Mobius, logical AND returns the first falsy value, or the last value
                if (!is_truthy(left)) {
                    vm_push(vm, left);
                    free_value(right);
                } else {
                    vm_push(vm, right);
                    free_value(left);
                }
                break;
            }
            
            case OP_LOGICAL_OR: {
                Value right = vm_pop(vm);
                Value left = vm_pop(vm);
                
                // In Mobius, logical OR returns the first truthy value, or the last value
                if (is_truthy(left)) {
                    vm_push(vm, left);
                    free_value(right);
                } else {
                    vm_push(vm, right);
                    free_value(left);
                }
                break;
            }
            
            case OP_LOGICAL_NOT: {
                Value operand = vm_pop(vm);
                vm_push(vm, make_bool_value(!is_truthy(operand)));
                free_value(operand);
                break;
            }
            
            case OP_PRINT: {
                Value value = vm_pop(vm);
                char* str = value_to_string(value);
                printf("%s\n", str ? str : "nil");
                free(str);
                free_value(value);
                break;
            }
            
            case OP_LOAD_GLOBAL: {
                uint16_t name_index = READ_OPERAND();
                if (name_index < chunk->string_count) {
                    const char* name = chunk->string_pool[name_index];
                    Value key = make_string_value_from_cstr(name);
                    
                    // Check if the variable exists in the global table
                    if (!table_has_key(vm->globals, key)) {
                        vm_runtime_error(vm, "Undefined variable '%s'", name);
                        free_value(key);
                        return VM_RUNTIME_ERROR;
                    }
                    
                    Value value = table_get(vm->globals, key);
                    free_value(key);
                    vm_push(vm, value);
                } else {
                    vm_runtime_error(vm, "Invalid global variable name index");
                    return VM_RUNTIME_ERROR;
                }
                break;
            }
            
            case OP_STORE_GLOBAL: {
                uint16_t name_index = READ_OPERAND();
                if (name_index < chunk->string_count) {
                    const char* name = chunk->string_pool[name_index];
                    Value value = vm_peek(vm, 0);  // Don't pop yet
                    table_set(vm->globals, make_string_value_from_cstr(name), value);
                } else {
                    vm_runtime_error(vm, "Invalid global variable name index");
                    return VM_RUNTIME_ERROR;
                }
                break;
            }
            
            case OP_ARRAY_NEW: {
                uint16_t capacity = READ_OPERAND();
                ArrayValue* array = array_create(capacity > 0 ? capacity : 8);
                if (array) {
                    vm_push(vm, make_array_value(array));
                } else {
                    vm_runtime_error(vm, "Failed to create array");
                    return VM_RUNTIME_ERROR;
                }
                break;
            }
            
            case OP_ARRAY_GET: {
                Value index_val = vm_pop(vm);
                Value container_val = vm_pop(vm);
                
                // Handle both arrays and tables with the same instruction
                if (container_val.type == VAL_ARRAY) {
                    // Array indexing
                    if (index_val.type != VAL_INTEGER) {
                        vm_runtime_error(vm, "Array index must be integer");
                        return VM_RUNTIME_ERROR;
                    }
                    
                    int64_t index;
                    switch (index_val.as.integer.num_type) {
                        case NUM_INT8:  index = index_val.as.integer.value.i8; break;
                        case NUM_INT16: index = index_val.as.integer.value.i16; break;
                        case NUM_INT32: index = index_val.as.integer.value.i32; break;
                        case NUM_INT64: index = index_val.as.integer.value.i64; break;
                        default: index = index_val.as.integer.value.i32; break;
                    }
                    ArrayValue* array = container_val.as.array;
                    
                    if (index < 0 || (size_t)index >= array->length) {
                        vm_push(vm, make_nil_value());  // Out of bounds returns nil
                    } else {
                        vm_push(vm, array_get(array, (size_t)index));
                    }
                } else if (container_val.type == VAL_TABLE) {
                    // Table indexing
                    Table* table = container_val.as.table;
                    Value result = table_get(table, index_val);
                    vm_push(vm, result);
                } else {
                    vm_runtime_error(vm, "Can only index arrays and tables");
                    return VM_RUNTIME_ERROR;
                }
                
                free_value(index_val);
                free_value(container_val);
                break;
            }
            
            case OP_ARRAY_SET: {
                Value value = vm_pop(vm);
                Value index_val = vm_pop(vm);
                Value array_val = vm_peek(vm, 0);  // Keep array on stack
                
                if (array_val.type != VAL_ARRAY) {
                    vm_runtime_error(vm, "Can only set array elements");
                    return VM_RUNTIME_ERROR;
                }
                
                if (index_val.type != VAL_INTEGER) {
                    vm_runtime_error(vm, "Array index must be integer");
                    return VM_RUNTIME_ERROR;
                }
                
                int64_t index;
                switch (index_val.as.integer.num_type) {
                    case NUM_INT8:  index = index_val.as.integer.value.i8; break;
                    case NUM_INT16: index = index_val.as.integer.value.i16; break;
                    case NUM_INT32: index = index_val.as.integer.value.i32; break;
                    case NUM_INT64: index = index_val.as.integer.value.i64; break;
                    default: index = index_val.as.integer.value.i32; break;
                }
                ArrayValue* array = array_val.as.array;
                
                if (index < 0) {
                    vm_runtime_error(vm, "Array index cannot be negative");
                    return VM_RUNTIME_ERROR;
                }
                
                // Extend array if needed
                if ((size_t)index >= array->length) {
                    array_resize(array, (size_t)index + 1);
                }
                
                array_set(array, (size_t)index, value);
                
                free_value(index_val);
                // Don't free value - it's now owned by the array
                break;
            }
            
            case OP_ARRAY_PUSH: {
                Value value = vm_pop(vm);
                Value array_val = vm_peek(vm, 0);  // Keep array on stack
                
                if (array_val.type != VAL_ARRAY) {
                    vm_runtime_error(vm, "Can only push to arrays");
                    return VM_RUNTIME_ERROR;
                }
                
                array_push(array_val.as.array, value);
                // Don't free value - it's now owned by the array
                break;
            }
            
            case OP_ARRAY_POP: {
                Value array_val = vm_peek(vm, 0);  // Keep array on stack
                
                if (array_val.type != VAL_ARRAY) {
                    vm_runtime_error(vm, "Can only pop from arrays");
                    return VM_RUNTIME_ERROR;
                }
                
                Value popped = array_pop(array_val.as.array);
                vm_push(vm, popped);
                break;
            }
            
            case OP_ARRAY_LEN: {
                Value array_val = vm_pop(vm);
                
                if (array_val.type != VAL_ARRAY) {
                    vm_runtime_error(vm, "Can only get length of arrays");
                    return VM_RUNTIME_ERROR;
                }
                
                size_t length = array_length(array_val.as.array);
                vm_push(vm, make_integer_value(NUM_INT64, (int64_t)length));
                
                free_value(array_val);
                break;
            }
            
            case OP_TABLE_NEW: {
                Table* table = create_table(16);  // Initial capacity
                if (table) {
                    vm_push(vm, make_table_value(table));
                } else {
                    vm_runtime_error(vm, "Failed to create table");
                    return VM_RUNTIME_ERROR;
                }
                break;
            }
            
            case OP_TABLE_GET: {
                Value key = vm_pop(vm);
                Value table_val = vm_pop(vm);
                
                if (table_val.type != VAL_TABLE) {
                    vm_runtime_error(vm, "Can only index tables");
                    return VM_RUNTIME_ERROR;
                }
                
                Value result = table_get(table_val.as.table, key);
                vm_push(vm, result);
                
                free_value(key);
                free_value(table_val);
                break;
            }
            
            case OP_TABLE_SET: {
                Value value = vm_pop(vm);
                Value key = vm_pop(vm);
                Value table_val = vm_peek(vm, 0);  // Keep table on stack
                
                // Handle stack corruption case for nested table literals
                if (table_val.type != VAL_TABLE && value.type == VAL_TABLE && key.type == VAL_TABLE) {
                    // Stack corruption detected: we have [string, table, table] instead of [table, string, table]
                    // Reorder: value=outer_table, key=inner_table, table=string
                    // We need: value=inner_table, key=string, table=outer_table
                    
                    Value temp_table = value;     // This is the outer table
                    Value temp_inner = key;      // This is the inner table  
                    Value temp_string = vm_pop(vm); // Pop the string (was at bottom)
                    
                    // Now set: outer_table[string] = inner_table
                    table_set(temp_table.as.table, temp_string, temp_inner);
                    
                    // Push outer table back
                    vm_push(vm, temp_table);
                    
                    free_value(temp_string);
                    // Don't free temp_inner - it's now owned by the table
                    break;
                }
                
                if (table_val.type != VAL_TABLE) {
                    vm_runtime_error(vm, "Can only set table values");
                    return VM_RUNTIME_ERROR;
                }
                
                table_set(table_val.as.table, key, value);
                
                free_value(key);
                // Don't free value - it's now owned by the table
                break;
            }
            
            case OP_TABLE_HAS: {
                Value key = vm_pop(vm);
                Value table_val = vm_pop(vm);
                
                if (table_val.type != VAL_TABLE) {
                    vm_runtime_error(vm, "Can only check table keys");
                    return VM_RUNTIME_ERROR;
                }
                
                bool has_key = table_has_key(table_val.as.table, key);
                vm_push(vm, make_bool_value(has_key));
                
                free_value(key);
                free_value(table_val);
                break;
            }
            
            case OP_JUMP: {
                uint16_t offset = READ_OPERAND();
                vm->ip = chunk->instructions + offset;
                break;
            }
            
            case OP_JUMP_IF_FALSE: {
                uint16_t offset = READ_OPERAND();
                Value condition = vm_peek(vm, 0);
                if (!is_truthy(condition)) {
                    vm->ip = chunk->instructions + offset;
                } 
                break;
            }
            
            case OP_JUMP_IF_TRUE: {
                uint16_t offset = READ_OPERAND();
                Value condition = vm_peek(vm, 0);
                if (is_truthy(condition)) {
                    vm->ip = chunk->instructions + offset;
                }
                break;
            }
            
            case OP_JUMP_IF_NIL: {
                uint16_t offset = READ_OPERAND();
                Value condition = vm_peek(vm, 0);
                if (condition.type == VAL_NIL) {
                    vm->ip = chunk->instructions + offset;
                }
                break;
            }
            
            case OP_JUMP_IF_NOT_NIL: {
                uint16_t offset = READ_OPERAND();
                Value condition = vm_peek(vm, 0);
                if (condition.type != VAL_NIL) {
                    vm->ip = chunk->instructions + offset;
                }
                break;
            }
            
            case OP_LOOP: {
                uint16_t offset = READ_OPERAND();
                vm->ip = chunk->instructions + (vm->ip - chunk->instructions) - offset;
                break;
            }
            
            case OP_CALL: {
                uint16_t arg_count = READ_OPERAND();
                if (!call_value(vm, vm_peek(vm, arg_count), arg_count)) {
                    return VM_RUNTIME_ERROR;
                }
                break;
            }
            
            case OP_RETURN: {
                // Handle return properly - pop the return value and exit
                if (vm->stack_top > vm->stack) {
                    Value return_value = vm_pop(vm);
                    // For now, we'll just discard the return value
                    // In a full implementation, this would be used by the caller
                    free_value(return_value);
                }
                return VM_OK;
            }
            
            case OP_ENUM_DEF: {
                uint8_t enum_index = READ_OPERAND();
                if (enum_index >= chunk->enum_count) {
                    vm_runtime_error(vm, "Invalid enum definition index");
                    return VM_RUNTIME_ERROR;
                }
                // Enum definition is stored at compile time, nothing to do at runtime
                break;
            }
            
            
            case OP_ENUM_STORE: {
                uint8_t name_index = READ_OPERAND();
                Value enum_value = vm_pop(vm);
                
                if (name_index >= chunk->string_count) {
                    vm_runtime_error(vm, "Invalid string index for enum store");
                    return VM_RUNTIME_ERROR;
                }
                
                // Get enum variable name (for future global storage integration)
                // const char* enum_var_name = chunk->string_pool[name_index];
                
                // Store in global environment
                // For simplicity, we'll use a basic global storage mechanism
                // In a full implementation, you'd integrate with the environment system
                
                // For now, just acknowledge the storage - enum definitions are available
                // through the chunk's enum_definitions array
                (void)name_index;  // Suppress unused warning
                free_value(enum_value);
                break;
            }
                
            default:
                vm_runtime_error(vm, "Unknown instruction: %d", instruction);
                return VM_RUNTIME_ERROR;
        }
    }
    
    #undef READ_BYTE
    #undef READ_SHORT
    #undef READ_CONSTANT
    
    return VM_OK;
}

// =============================================================================
// DEBUGGING SUPPORT
// =============================================================================

static const char* opcode_names[256] = {
    [OP_PUSH_NIL] = "OP_PUSH_NIL",
    [OP_PUSH_TRUE] = "OP_PUSH_TRUE", 
    [OP_PUSH_FALSE] = "OP_PUSH_FALSE",
    [OP_PUSH_INT8] = "OP_PUSH_INT8",
    [OP_PUSH_INT16] = "OP_PUSH_INT16",
    [OP_PUSH_INT32] = "OP_PUSH_INT32",
    [OP_PUSH_INT64] = "OP_PUSH_INT64",
    [OP_PUSH_FLOAT32] = "OP_PUSH_FLOAT32",
    [OP_PUSH_FLOAT64] = "OP_PUSH_FLOAT64",
    [OP_PUSH_STRING] = "OP_PUSH_STRING",
    [OP_PUSH_CHAR] = "OP_PUSH_CHAR",
    [OP_PUSH_CONSTANT] = "OP_PUSH_CONSTANT",
    [OP_PUSH_UINT8] = "OP_PUSH_UINT8",
    [OP_PUSH_UINT16] = "OP_PUSH_UINT16",
    [OP_PUSH_UINT32] = "OP_PUSH_UINT32",
    [OP_PUSH_UINT64] = "OP_PUSH_UINT64",
    [OP_POP] = "OP_POP",
    [OP_DUP] = "OP_DUP",
    [OP_ADD] = "OP_ADD",
    [OP_SUB] = "OP_SUB",
    [OP_MUL] = "OP_MUL",
    [OP_DIV] = "OP_DIV",
    [OP_MOD] = "OP_MOD",
    [OP_NEG] = "OP_NEG",
    [OP_POS] = "OP_POS",
    [OP_LOGICAL_AND] = "OP_LOGICAL_AND",
    [OP_LOGICAL_OR] = "OP_LOGICAL_OR",
    [OP_LOGICAL_NOT] = "OP_LOGICAL_NOT",
    [OP_EQ] = "OP_EQ",
    [OP_NE] = "OP_NE",
    [OP_LT] = "OP_LT",
    [OP_LE] = "OP_LE",
    [OP_GT] = "OP_GT",
    [OP_GE] = "OP_GE",
    [OP_LOAD_GLOBAL] = "OP_LOAD_GLOBAL",
    [OP_STORE_GLOBAL] = "OP_STORE_GLOBAL",
    [OP_ARRAY_NEW] = "OP_ARRAY_NEW",
    [OP_ARRAY_GET] = "OP_ARRAY_GET",
    [OP_ARRAY_SET] = "OP_ARRAY_SET",
    [OP_ARRAY_PUSH] = "OP_ARRAY_PUSH",
    [OP_ARRAY_POP] = "OP_ARRAY_POP",
    [OP_ARRAY_LEN] = "OP_ARRAY_LEN",
    [OP_TABLE_NEW] = "OP_TABLE_NEW",
    [OP_TABLE_GET] = "OP_TABLE_GET",
    [OP_TABLE_SET] = "OP_TABLE_SET",
    [OP_TABLE_HAS] = "OP_TABLE_HAS",
    [OP_TABLE_DELETE] = "OP_TABLE_DELETE",
    [OP_TABLE_KEYS] = "OP_TABLE_KEYS",
    [OP_TABLE_VALUES] = "OP_TABLE_VALUES",
    [OP_JUMP] = "OP_JUMP",
    [OP_JUMP_IF_FALSE] = "OP_JUMP_IF_FALSE",
    [OP_JUMP_IF_TRUE] = "OP_JUMP_IF_TRUE",
    [OP_JUMP_IF_NIL] = "OP_JUMP_IF_NIL",
    [OP_JUMP_IF_NOT_NIL] = "OP_JUMP_IF_NOT_NIL",
    [OP_LOOP] = "OP_LOOP",
    [OP_CALL] = "OP_CALL",
    [OP_PRINT] = "OP_PRINT",
    [OP_RETURN] = "OP_RETURN",
};

static int simple_instruction(const char* name, int offset) {
    printf("%-16s\n", name);
    return offset + 1;
}

static int constant_instruction(const char* name, BytecodeChunk* chunk, int offset) {
    uint8_t constant = chunk->instructions[offset + 1].operand;
    printf("%-16s %4d '", name, constant);
    
    if (constant < chunk->constant_count) {
        char* str = value_to_string(chunk->constants[constant]);
        printf("%s", str ? str : "nil");
        free(str);
    } else {
        printf("INVALID");
    }
    
    printf("'\n");
    return offset + 2;
}

int bytecode_disassemble_instruction(BytecodeChunk* chunk, int offset) {
    printf("%04d ", offset);
    
    if (offset > 0 && 
        chunk->line_numbers && 
        chunk->line_numbers[offset] == chunk->line_numbers[offset - 1]) {
        printf("   | ");
    } else if (chunk->line_numbers) {
        printf("%4d ", chunk->line_numbers[offset]);
    } else {
        printf("   ? ");
    }
    
    uint8_t instruction = chunk->instructions[offset].opcode;
    
    switch (instruction) {
        case OP_PUSH_CONSTANT:
            return constant_instruction("OP_PUSH_CONSTANT", chunk, offset);
        case OP_LOAD_GLOBAL:
        case OP_STORE_GLOBAL: {
            uint16_t operand = chunk->instructions[offset].operand;
            printf("%-16s %4d", opcode_names[instruction], operand);
            if (operand < chunk->string_count && chunk->string_pool[operand]) {
                printf(" '%s'", chunk->string_pool[operand]);
            }
            printf("\n");
            return offset + 1;
        }
        case OP_JUMP:
        case OP_JUMP_IF_FALSE:
        case OP_JUMP_IF_TRUE:
        case OP_JUMP_IF_NIL:
        case OP_JUMP_IF_NOT_NIL: {
            uint16_t operand = chunk->instructions[offset].operand;
            printf("%-16s %4d -> %04d\n", opcode_names[instruction], operand, operand);
            return offset + 1;
        }
        case OP_LOOP: {
            uint16_t operand = chunk->instructions[offset].operand;
            printf("%-16s %4d (back %d)\n", opcode_names[instruction], operand, operand);
            return offset + 1;
        }
        default:
            if (opcode_names[instruction]) {
                return simple_instruction(opcode_names[instruction], offset);
            } else {
                printf("Unknown opcode %d\n", instruction);
                return offset + 1;
            }
    }
}

void bytecode_disassemble(BytecodeChunk* chunk, const char* name) {
    printf("== %s ==\n", name);
    
    for (int offset = 0; offset < (int)chunk->count;) {
        offset = bytecode_disassemble_instruction(chunk, offset);
    }
}

void vm_print_stack(MobiusVM* vm) {
    printf("          ");
    for (Value* slot = vm->stack; slot < vm->stack_top; slot++) {
        printf("[ ");
        char* str = value_to_string(*slot);
        printf("%s", str ? str : "nil");
        free(str);
        printf(" ]");
    }
    printf("\n");
}
