#ifndef MOBIUS_FUNCTION_H
#define MOBIUS_FUNCTION_H

// Forward declarations
struct Environment;
struct Stmt;
struct Prototype;

// Function representation — works for both AST-based (tree-walker) and
// bytecode-compiled (VM) functions.  When `proto` is non-null the VM
// executes bytecode; otherwise the tree-walker uses `body`/`body_count`.
typedef struct MobiusFunction {
    const char* name;             // Function name (interned string pointer, not owned)
    const char** param_names;     // Parameter names (interned string pointers, not owned)
    size_t param_count;           // Parameter count
    struct Stmt** body;           // AST statements (tree-walker path)
    size_t body_count;            // Number of statements in body
    struct Environment* closure;  // Lexical scope
    int ref_count;                // Reference counter for memory management
    struct Prototype* proto;      // Bytecode prototype (VM path, nullptr for AST functions)
} MobiusFunction;

#endif // MOBIUS_FUNCTION_H