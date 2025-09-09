#ifndef MOBIUS_ENVIRONMENT_H
#define MOBIUS_ENVIRONMENT_H

#include "ast.h"
#include "table.h"
#include <stddef.h>
#include <stdbool.h>

// Environment structure for variable scoping and stack-based execution
// Now uses Table for consistent storage with bytecode VM
typedef struct Environment {
    Table* variables;       // Table storing variable name->value mappings
    struct Environment* enclosing;  // Parent environment for scoping
    
    // Stack-based execution (like bytecode VM)
    Value* stack;           // Execution stack for expressions and function calls
    size_t stack_top;       // Current stack position (0 = empty)
    size_t stack_capacity;  // Allocated stack size
} Environment;

// Environment creation and management
Environment* create_environment(Environment* enclosing);
void free_environment(Environment* env);

// Variable operations
void define_variable(Environment* env, const char* name, Value value);
Value get_variable(Environment* env, const char* name, bool* found);
bool assign_variable(Environment* env, const char* name, Value value);
bool is_defined(Environment* env, const char* name);

// Stack operations for expression evaluation
void env_push(Environment* env, Value value);
Value env_pop(Environment* env);
Value env_peek(Environment* env, size_t offset);  // 0 = top, 1 = second from top
void env_ensure_stack_capacity(Environment* env, size_t needed);
size_t env_stack_size(Environment* env);
void env_stack_clear(Environment* env);

// Environment utilities
void print_environment(Environment* env);

// Global environment management
extern Environment* global_env;
void init_global_environment();
void cleanup_global_environment();

#endif // MOBIUS_ENVIRONMENT_H
