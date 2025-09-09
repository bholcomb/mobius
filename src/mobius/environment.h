#ifndef MOBIUS_ENVIRONMENT_H
#define MOBIUS_ENVIRONMENT_H

#include "ast.h"
#include "table.h"
#include <stddef.h>
#include <stdbool.h>

// Environment structure for variable scoping
// Now uses Table for consistent storage with bytecode VM
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

// Environment utilities
void print_environment(Environment* env);

// Global environment management
extern Environment* global_env;
void init_global_environment();
void cleanup_global_environment();

#endif // MOBIUS_ENVIRONMENT_H
