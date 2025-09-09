#include "environment.h"
#include "ast.h"
#include "table.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Global environment instance
Environment* global_env = NULL;

// Initial stack capacity
#define INITIAL_STACK_CAPACITY 256

// Create a new environment
Environment* create_environment(Environment* enclosing) {
    Environment* env = malloc(sizeof(Environment));
    if (!env) return NULL;
    
    env->enclosing = enclosing;
    env->variables = create_table(INITIAL_TABLE_CAPACITY);
    
    if (!env->variables) {
        free(env);
        return NULL;
    }
    
    // Initialize stack
    env->stack_capacity = INITIAL_STACK_CAPACITY;
    env->stack = malloc(env->stack_capacity * sizeof(Value));
    env->stack_top = 0;
    
    if (!env->stack) {
        free_table(env->variables);
        free(env);
        return NULL;
    }
    
    return env;
}

// Free an environment and all its entries
void free_environment(Environment* env) {
    if (!env) return;
    
    // Free the table (this handles all variable cleanup)
    free_table(env->variables);
    
    // Free the stack (values are copied, so we don't need to free individual values)
    free(env->stack);
    
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
// STACK OPERATIONS FOR EXPRESSION EVALUATION
// =============================================================================

// Ensure the stack has enough capacity for additional elements
void env_ensure_stack_capacity(Environment* env, size_t needed) {
    if (!env) return;
    
    size_t required = env->stack_top + needed;
    if (required <= env->stack_capacity) {
        return;  // Already have enough capacity
    }
    
    // Double the capacity or use required size, whichever is larger
    size_t new_capacity = env->stack_capacity * 2;
    if (new_capacity < required) {
        new_capacity = required;
    }
    
    Value* new_stack = realloc(env->stack, new_capacity * sizeof(Value));
    if (!new_stack) {
        // Handle allocation failure - for now, just return
        // In production, we might want to set an error flag
        return;
    }
    
    env->stack = new_stack;
    env->stack_capacity = new_capacity;
}

// Push a value onto the stack
void env_push(Environment* env, Value value) {
    if (!env) return;
    
    env_ensure_stack_capacity(env, 1);
    env->stack[env->stack_top] = copy_value(value);
    env->stack_top++;
}

// Pop a value from the stack
Value env_pop(Environment* env) {
    if (!env || env->stack_top == 0) {
        return make_nil_value();  // Stack underflow - return nil
    }
    
    env->stack_top--;
    return env->stack[env->stack_top];
}

// Peek at a value on the stack without removing it
// offset 0 = top of stack, 1 = second from top, etc.
Value env_peek(Environment* env, size_t offset) {
    if (!env || env->stack_top == 0 || offset >= env->stack_top) {
        return make_nil_value();  // Invalid offset
    }
    
    // Return the value at the specified offset from the top
    return env->stack[env->stack_top - 1 - offset];
}

// Get current stack size
size_t env_stack_size(Environment* env) {
    if (!env) return 0;
    return env->stack_top;
}

// Clear the stack (useful for error recovery)
void env_stack_clear(Environment* env) {
    if (!env) return;
    
    // Free all values on the stack
    for (size_t i = 0; i < env->stack_top; i++) {
        free_value(env->stack[i]);
    }
    
    env->stack_top = 0;
}
