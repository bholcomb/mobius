#ifndef MOBIUS_LIBRARY_H
#define MOBIUS_LIBRARY_H

#include "data/value.h"
#include "state/environment.h"
#include "eval/evaluator.h"

// =============================================================================
// UNIFIED LIBRARY INTERFACE
// =============================================================================

// Unified builtin function signature that works for both AST and Bytecode
// - For AST: Uses Environment* with stack operations (env_peek, env_pop, env_push)
// LibraryFunction typedef is defined in evaluator.h

// Library function registry entry
typedef struct {
    const char* name;
    MobiusCFunction func;
    int min_args;      // -1 for variadic
    int max_args;      // -1 for unlimited
} LibraryEntry;


// =============================================================================
// LIBRARY REGISTRY
// =============================================================================

// Get the complete library function registry
const LibraryEntry* get_library_registry(void);
size_t get_library_registry_size(void);

// Lookup a library function by name
MobiusCFunction lookup_library_function(const char* name);

#endif // MOBIUS_LIBRARY_H
