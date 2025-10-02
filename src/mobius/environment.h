#ifndef MOBIUS_ENVIRONMENT_H
#define MOBIUS_ENVIRONMENT_H

#include "ast.h"
#include "table.h"
#include <stddef.h>
#include <stdbool.h>

// Execution context - encapsulates all execution state
// This allows for multi-threading and coroutines in the future
typedef struct ExecutionContext {
    // Execution stack
    Value* stack;           // Stack array
    size_t stack_top;       // Current stack position (0 = empty)
    size_t stack_capacity;  // Allocated stack size
    
    // Future extensions:
    // - Call frame stack for debugging/profiling
    // - Thread ID
    // - Coroutine state
    // - Exception handling state
} ExecutionContext;

// Environment structure for variable scoping only
// Stack is now global in ExecutionStack
typedef struct Environment {
    Table* variables;       // Table storing variable name->value mappings
    struct Environment* enclosing;  // Parent environment for scoping
} Environment;

// Environment creation and management
Environment* create_environment(Environment* enclosing);
void free_environment(Environment* env);

// Variable operations
void define_variable(Environment* env, const char* name, Value value);
Value get_variable(Environment* env, const char* name, bool* found);
bool assign_variable(Environment* env, const char* name, Value value);
bool is_defined(Environment* env, const char* name);

// Execution context operations
ExecutionContext* create_execution_context(size_t initial_stack_capacity);
void free_execution_context(ExecutionContext* ctx);

// Stack operations on execution context
void ctx_push(ExecutionContext* ctx, Value value);
Value ctx_pop(ExecutionContext* ctx);
Value ctx_peek(ExecutionContext* ctx, size_t offset);  // 0 = top, 1 = second from top
void ctx_ensure_stack_capacity(ExecutionContext* ctx, size_t needed);
size_t ctx_stack_size(ExecutionContext* ctx);
void ctx_stack_clear(ExecutionContext* ctx);

// Global execution context (one per thread in the future)
extern ExecutionContext* global_context;

// Environment utilities
void print_environment(Environment* env);

// Global environment management
extern Environment* global_env;
void init_global_environment();
void cleanup_global_environment();

#endif // MOBIUS_ENVIRONMENT_H
