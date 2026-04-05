#ifndef MOBIUS_FUNCTION_H
#define MOBIUS_FUNCTION_H

#include <atomic>

struct Stmt;
struct Prototype;
struct MobiusString;
struct Upvalue;

typedef struct MobiusFunction {
    MobiusString* name;           // Function name (interned)
    MobiusString** param_names;   // Parameter names (interned)
    size_t param_count;           // Parameter count
    struct Stmt** body;           // AST statements (tree-walker path)
    size_t body_count;            // Number of statements in body
    std::atomic<int> ref_count;   // Reference counter for memory management
    struct Prototype* proto;      // Bytecode prototype (VM path, nullptr for AST functions)
    struct Upvalue** upvalues;    // Captured upvalues (VM closures)
    int upvalue_count;            // Number of upvalues
} MobiusFunction;

#endif // MOBIUS_FUNCTION_H
