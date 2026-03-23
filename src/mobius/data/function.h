#ifndef MOBIUS_FUNCTION_H
#define MOBIUS_FUNCTION_H

// Forward declarations
struct Environment;
struct Stmt;

// Function representation (AST functions only - builtins are loaded as regular functions)
typedef struct MobiusFunction {
    const char* name;             // Function name (interned string pointer, not owned)
    const char** param_names;     // Parameter names (interned string pointers, not owned)
    size_t param_count;           // Parameter count
    struct Stmt** body;           // AST statements
    size_t body_count;            // Number of statements in body
    struct Environment* closure;  // Lexical scope
    int ref_count;                // Reference counter for memory management
} MobiusFunction;

#endif // MOBIUS_FUNCTION_H