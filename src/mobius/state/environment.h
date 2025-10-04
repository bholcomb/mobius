#ifndef MOBIUS_ENVIRONMENT_H
#define MOBIUS_ENVIRONMENT_H

#include "frontend/ast.h"
#include "data/table.h"

#include <stddef.h>
#include <stdbool.h>

// Forward declarations
struct ExecutionContext;

// Environment structure for variable scoping only
// Stack is now global in ExecutionStack
typedef struct Environment {
    Table* variables;       // Table storing variable name->value mappings
    struct Environment* enclosing;  // Parent environment for scoping
    struct ExecutionContext* current_context;  // Current execution context
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


#endif // MOBIUS_ENVIRONMENT_H
