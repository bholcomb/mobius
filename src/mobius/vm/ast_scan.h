#ifndef MOBIUS_VM_AST_SCAN_H
#define MOBIUS_VM_AST_SCAN_H

#include "frontend/ast.h"

#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

// Walks statements and expressions collecting the names they declare
// (parameters, locals, loop and catch variables, pattern bindings, nested
// functions and their parameters) and the names they reference, with the
// line of each first reference. Nested function bodies are included.
struct AstNameScan {
    std::unordered_set<std::string> declared;
    std::vector<std::pair<std::string, int>> referenced;
    std::unordered_set<std::string> seen_refs;
    // Names assigned (`x = ...`, `x op= ...`, `x++`, `x--`), and names
    // referenced from inside a nested function or closure.
    std::unordered_set<std::string> assigned;
    std::unordered_set<std::string> used_in_closures;
    int function_depth = 0;

    void declare(const Token& t);
    void declare(const char* name);
    void reference(const Token& t);
    void stmts(Stmt** list, size_t count);
    void stmt(Stmt* s);
    void expr(Expr* e);
    void pattern(const CasePattern* p);
};

// Whether expression e references the variable `name` anywhere, including
// inside nested function literals.
bool expr_mentions_name(Expr* e, const char* name);

#endif // MOBIUS_VM_AST_SCAN_H
