#include "environment.h"
#include "ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INITIAL_ENV_CAPACITY 16
#define LOAD_FACTOR_THRESHOLD 0.75

// Global environment instance
Environment* global_env = NULL;

// Hash function for string keys
size_t hash_string(const char* str, size_t capacity) {
    size_t hash = 5381;  // djb2 hash algorithm
    int c;
    
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;  // hash * 33 + c
    }
    
    return hash % capacity;
}

// Create a new environment
Environment* create_environment(Environment* enclosing) {
    Environment* env = malloc(sizeof(Environment));
    if (!env) return NULL;
    
    env->capacity = INITIAL_ENV_CAPACITY;
    env->count = 0;
    env->enclosing = enclosing;
    env->table = calloc(env->capacity, sizeof(EnvEntry*));
    
    if (!env->table) {
        free(env);
        return NULL;
    }
    
    return env;
}

// Free an environment and all its entries
void free_environment(Environment* env) {
    if (!env) return;
    
    // Free all entries in the hash table
    for (size_t i = 0; i < env->capacity; i++) {
        EnvEntry* entry = env->table[i];
        while (entry) {
            EnvEntry* next = entry->next;
            free(entry->name);
            free_value(entry->value);
            free(entry);
            entry = next;
        }
    }
    
    free(env->table);
    free(env);
}

// Resize the hash table when load factor gets too high
static void resize_environment(Environment* env) {
    size_t old_capacity = env->capacity;
    EnvEntry** old_table = env->table;
    
    // Double the capacity
    env->capacity *= 2;
    env->table = calloc(env->capacity, sizeof(EnvEntry*));
    env->count = 0;
    
    if (!env->table) {
        // Revert on failure
        env->capacity = old_capacity;
        env->table = old_table;
        return;
    }
    
    // Rehash all existing entries
    for (size_t i = 0; i < old_capacity; i++) {
        EnvEntry* entry = old_table[i];
        while (entry) {
            EnvEntry* next = entry->next;
            
            // Rehash and insert into new table
            size_t index = hash_string(entry->name, env->capacity);
            entry->next = env->table[index];
            env->table[index] = entry;
            env->count++;
            
            entry = next;
        }
    }
    
    free(old_table);
}

// Find an entry in the environment (local search only)
static EnvEntry* find_entry(Environment* env, const char* name) {
    size_t index = hash_string(name, env->capacity);
    EnvEntry* entry = env->table[index];
    
    while (entry) {
        if (strcmp(entry->name, name) == 0) {
            return entry;
        }
        entry = entry->next;
    }
    
    return NULL;
}

// Define a variable in the current environment
void define_variable(Environment* env, const char* name, Value value) {
    if (!env || !name) return;
    
    // Check if we need to resize
    if ((double)env->count / env->capacity > LOAD_FACTOR_THRESHOLD) {
        resize_environment(env);
    }
    
    // Check if variable already exists in current scope
    EnvEntry* existing = find_entry(env, name);
    if (existing) {
        // Update existing variable - free old value and copy new one
        free_value(existing->value);
        existing->value = copy_value(value);
        existing->is_defined = true;
        return;
    }
    
    // Create new entry
    EnvEntry* entry = malloc(sizeof(EnvEntry));
    if (!entry) return;
    
    entry->name = malloc(strlen(name) + 1);
    if (!entry->name) {
        free(entry);
        return;
    }
    strcpy(entry->name, name);
    
    entry->value = copy_value(value);
    entry->is_defined = true;
    
    // Insert into hash table
    size_t index = hash_string(name, env->capacity);
    entry->next = env->table[index];
    env->table[index] = entry;
    env->count++;
}

// Get a variable value (searches up the scope chain)
Value get_variable(Environment* env, const char* name, bool* found) {
    *found = false;
    
    Environment* current = env;
    while (current) {
        EnvEntry* entry = find_entry(current, name);
        if (entry) {
            if (entry->is_defined) {
                *found = true;
                return entry->value;
            } else {
                // Variable declared but not initialized
                *found = false;
                return make_nil_value();
            }
        }
        current = current->enclosing;
    }
    
    // Variable not found
    return make_nil_value();
}

// Assign to an existing variable (searches up the scope chain)
bool assign_variable(Environment* env, const char* name, Value value) {
    Environment* current = env;
    while (current) {
        EnvEntry* entry = find_entry(current, name);
        if (entry) {
            free_value(entry->value);
            entry->value = copy_value(value);
            entry->is_defined = true;
            return true;
        }
        current = current->enclosing;
    }
    
    return false;  // Variable not found
}

// Check if a variable is defined (searches up the scope chain)
bool is_defined(Environment* env, const char* name) {
    Environment* current = env;
    while (current) {
        EnvEntry* entry = find_entry(current, name);
        if (entry) {
            return entry->is_defined;
        }
        current = current->enclosing;
    }
    
    return false;
}

// Print environment contents for debugging
void print_environment(Environment* env) {
    if (!env) {
        printf("Environment: (null)\n");
        return;
    }
    
    printf("Environment (count: %zu, capacity: %zu):\n", env->count, env->capacity);
    for (size_t i = 0; i < env->capacity; i++) {
        EnvEntry* entry = env->table[i];
        while (entry) {
            printf("  %s = ", entry->name);
            if (entry->is_defined) {
                print_value(entry->value);
            } else {
                printf("(undefined)");
            }
            printf("\n");
            entry = entry->next;
        }
    }
    
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
