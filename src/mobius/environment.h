#ifndef MOBIUS_ENVIRONMENT_H
#define MOBIUS_ENVIRONMENT_H

#include "ast.h"
#include <stddef.h>
#include <stdbool.h>

// Hash table entry for variable storage
typedef struct EnvEntry {
    char* name;             // Variable name (owned by this entry)
    Value value;            // Variable value
    bool is_defined;        // Whether the variable has been initialized
    struct EnvEntry* next;  // For hash collision chaining
} EnvEntry;

// Environment structure for variable scoping
typedef struct Environment {
    EnvEntry** table;       // Hash table of variables
    size_t capacity;        // Hash table capacity
    size_t count;           // Number of variables in this environment
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

// Environment utilities
void print_environment(Environment* env);
size_t hash_string(const char* str, size_t capacity);

// Global environment management
extern Environment* global_env;
void init_global_environment();
void cleanup_global_environment();

#endif // MOBIUS_ENVIRONMENT_H
