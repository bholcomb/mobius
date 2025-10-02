#include "environment.h"
#include "ast.h"
#include "table.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Global environment instance
Environment* global_env = NULL;

// Global execution context (one per thread in the future)
ExecutionContext* global_context = NULL;

// Function to get global context (for use in dynamically loaded plugins)
ExecutionContext* get_global_context(void) {
    return global_context;
}

// Initial stack capacity
#define INITIAL_STACK_CAPACITY 256

// Create execution context
ExecutionContext* create_execution_context(size_t initial_stack_capacity) {
    ExecutionContext* ctx = malloc(sizeof(ExecutionContext));
    if (!ctx) return NULL;
    
    ctx->stack_capacity = initial_stack_capacity;
    ctx->stack = malloc(ctx->stack_capacity * sizeof(Value));
    ctx->stack_top = 0;
    ctx->env = NULL;  // Will be set by evaluator before each call
    
    if (!ctx->stack) {
        free(ctx);
        return NULL;
    }
    
    return ctx;
}

// Free execution context
void free_execution_context(ExecutionContext* ctx) {
    if (!ctx) return;
    free(ctx->stack);
    free(ctx);
}

// Create a new environment (no stack - stack is global now)
Environment* create_environment(Environment* enclosing) {
    Environment* env = malloc(sizeof(Environment));
    if (!env) return NULL;
    
    env->enclosing = enclosing;
    env->variables = create_table(INITIAL_TABLE_CAPACITY);
    
    if (!env->variables) {
        free(env);
        return NULL;
    }
    
    return env;
}

// Free an environment
void free_environment(Environment* env) {
    if (!env) return;
    free_table(env->variables);
    free(env);
}


// Define a variable in the current environment
void define_variable(Environment* env, const char* name, Value value) {
    if (!env || !name) return;
    
    // Convert name to string Value for use as key
    Value key = make_string_value_from_cstr(name);
    
    // Set the variable in the table (table_set handles overwriting)
    table_set(env->variables, key, value);
    
    // Clean up the key (table_set copies it)
    free_value(key);
}

// Get a variable value (searches up the scope chain)
Value get_variable(Environment* env, const char* name, bool* found) {
    *found = false;
    
    Environment* current = env;
    while (current) {
        Value key = make_string_value_from_cstr(name);
        
        if (table_has_key(current->variables, key)) {
            Value value = table_get(current->variables, key);
            free_value(key);
            *found = true;
            return value;
        }
        
        free_value(key);
        current = current->enclosing;
    }
    
    // Variable not found
    return make_nil_value();
}

// Assign to an existing variable (searches up the scope chain)
bool assign_variable(Environment* env, const char* name, Value value) {
    Environment* current = env;
    while (current) {
        Value key = make_string_value_from_cstr(name);
        
        if (table_has_key(current->variables, key)) {
            table_set(current->variables, key, value);
            free_value(key);
            return true;
        }
        
        free_value(key);
        current = current->enclosing;
    }
    
    return false;  // Variable not found
}

// Check if a variable is defined (searches up the scope chain)
bool is_defined(Environment* env, const char* name) {
    bool found = false;
    get_variable(env, name, &found);
    return found;
}

// Print environment contents for debugging
void print_environment(Environment* env) {
    if (!env) {
        printf("Environment: (null)\n");
        return;
    }
    
    printf("Environment (variables: %zu):\n", table_size(env->variables));
    print_table(env->variables);
    
    if (env->enclosing) {
        printf("Enclosing:\n");
        print_environment(env->enclosing);
    }
}

// Initialize global environment
void init_global_environment() {
    if (global_env) {
        cleanup_global_environment();
    }
    
    global_env = create_environment(NULL);
    
    // Define built-in constants
    define_variable(global_env, "nil", make_nil_value());
    define_variable(global_env, "true", make_bool_value(true));
    define_variable(global_env, "false", make_bool_value(false));
}

// Clean up global environment
void cleanup_global_environment() {
    if (global_env) {
        free_environment(global_env);
        global_env = NULL;
    }
}

// =============================================================================
// EXECUTION CONTEXT STACK OPERATIONS
// =============================================================================

// Ensure the stack has enough capacity for additional elements
void ctx_ensure_stack_capacity(ExecutionContext* ctx, size_t needed) {
    if (!ctx) return;
    
    size_t required = ctx->stack_top + needed;
    if (required <= ctx->stack_capacity) {
        return;  // Already have enough capacity
    }
    
    // Double the capacity or use required size, whichever is larger
    size_t new_capacity = ctx->stack_capacity * 2;
    if (new_capacity < required) {
        new_capacity = required;
    }
    
    Value* new_stack = realloc(ctx->stack, new_capacity * sizeof(Value));
    if (!new_stack) {
        return;  // Handle allocation failure
    }
    
    ctx->stack = new_stack;
    ctx->stack_capacity = new_capacity;
}

// Push a value onto the execution context stack
void ctx_push(ExecutionContext* ctx, Value value) {
    if (!ctx) return;
    
    ctx_ensure_stack_capacity(ctx, 1);
    ctx->stack[ctx->stack_top] = copy_value(value);
    ctx->stack_top++;
}

// Pop a value from the execution context stack
Value ctx_pop(ExecutionContext* ctx) {
    if (!ctx || ctx->stack_top == 0) {
        return make_nil_value();  // Stack underflow - return nil
    }
    
    ctx->stack_top--;
    return ctx->stack[ctx->stack_top];
}

// Peek at a value on the stack without removing it
// offset 0 = top of stack, 1 = second from top, etc.
Value ctx_peek(ExecutionContext* ctx, size_t offset) {
    if (!ctx || ctx->stack_top == 0 || offset >= ctx->stack_top) {
        return make_nil_value();  // Invalid offset
    }
    
    return ctx->stack[ctx->stack_top - 1 - offset];
}

// Get current stack size
size_t ctx_stack_size(ExecutionContext* ctx) {
    if (!ctx) return 0;
    return ctx->stack_top;
}

// Clear the stack (useful for error recovery)
void ctx_stack_clear(ExecutionContext* ctx) {
    if (!ctx) return;
    
    // Free all values on the stack
    for (size_t i = 0; i < ctx->stack_top; i++) {
        free_value(ctx->stack[i]);
    }
    
    ctx->stack_top = 0;
}
