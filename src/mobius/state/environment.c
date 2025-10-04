#include "state/environment.h"
#include "data/table.h"
#include "state/mobius_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Create a new environment (no stack - stack is global now)
Environment* create_environment(Environment* enclosing) {
    Environment* env = malloc(sizeof(Environment));
    if (!env) return NULL;
    
    env->enclosing = enclosing;
    env->variables = create_table(INITIAL_TABLE_CAPACITY);
    env->current_context = enclosing ? enclosing->current_context : NULL;
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
