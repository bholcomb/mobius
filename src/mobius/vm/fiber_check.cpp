#include "vm/fiber_check.h"
#include "vm/ast_scan.h"

#include <unordered_set>


void collect_top_level_decls(Stmt** statements, size_t count, TopLevelDecls& out) {
    for (size_t i = 0; i < count; i++) {
        Stmt* s = statements[i];
        if (!s) continue;
        if (s->type == STMT_VAR && s->as.var.name.identifier) {
            Expr* init = s->as.var.initializer;
            // A const is immutable, so fibers may use it like a shared var.
            out.vars[s->as.var.name.identifier] =
                s->as.var.is_const || (init && init->type == EXPR_SHARED);
        } else if (s->type == STMT_FUNCTION && s->as.function.name.identifier) {
            out.funcs[s->as.function.name.identifier] = &s->as.function;
        }
    }
}

static bool search(const std::string& name, const TopLevelDecls& decls,
                   std::unordered_set<std::string>& visited,
                   std::vector<std::string>& path, UnsharedGlobalUse* out) {
    auto fit = decls.funcs.find(name);
    if (fit == decls.funcs.end() || !visited.insert(name).second) return false;
    path.push_back(name);

    AstNameScan scan;
    const FunctionStmt* fn = fit->second;
    for (size_t i = 0; i < fn->param_count; i++) scan.declare(fn->params[i]);
    if (fn->param_defaults)
        for (size_t i = 0; i < fn->param_count; i++) scan.expr(fn->param_defaults[i]);
    scan.stmts(fn->body, fn->body_count);

    for (const auto& ref : scan.referenced) {
        if (scan.declared.count(ref.first)) continue;
        auto vit = decls.vars.find(ref.first);
        if (vit != decls.vars.end() && !vit->second) {
            out->var = ref.first;
            out->path = path;
            out->line = ref.second;
            return true;
        }
    }
    for (const auto& ref : scan.referenced) {
        if (scan.declared.count(ref.first)) continue;
        if (search(ref.first, decls, visited, path, out)) return true;
    }
    path.pop_back();
    return false;
}

bool find_unshared_global_use(const std::string& func_name, const TopLevelDecls& decls,
                              UnsharedGlobalUse* out) {
    std::unordered_set<std::string> visited;
    std::vector<std::string> path;
    return search(func_name, decls, visited, path, out);
}
