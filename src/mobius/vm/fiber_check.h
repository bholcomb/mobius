#ifndef MOBIUS_VM_FIBER_CHECK_H
#define MOBIUS_VM_FIBER_CHECK_H

#include "frontend/ast.h"

#include <string>
#include <unordered_map>
#include <vector>

// Compile-time half of the fiber rule: a spawned fiber may use only
// top-level values that are shared or cannot change (the runtime check in
// the VM's global instructions catches everything this misses).
//
// At `spawn f(...)`, when f is a top-level function of the same chunk, the
// compiler asks whether f, or any top-level function it references, uses a
// non-shared top-level `var`.

struct TopLevelDecls {
    std::unordered_map<std::string, bool> vars;                  // name -> declared `shared`
    std::unordered_map<std::string, const FunctionStmt*> funcs;  // top-level `func`s
};

struct UnsharedGlobalUse {
    std::string var;                // the offending top-level var
    std::vector<std::string> path;  // spawned function, then functions down to the use
    int line = 0;                   // line of the use
};

// Record the top-level declarations of a chunk.
void collect_top_level_decls(Stmt** statements, size_t count, TopLevelDecls& out);

// First non-shared top-level var used by `func_name` or the top-level
// functions it references, if any.
bool find_unshared_global_use(const std::string& func_name, const TopLevelDecls& decls,
                              UnsharedGlobalUse* out);

#endif // MOBIUS_VM_FIBER_CHECK_H
