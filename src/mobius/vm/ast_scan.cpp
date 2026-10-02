#include "vm/ast_scan.h"

void AstNameScan::declare(const Token& t) {
    if (t.identifier) declared.insert(t.identifier);
}
void AstNameScan::declare(const char* name) {
    if (name) declared.insert(name);
}
void AstNameScan::reference(const Token& t) {
    if (t.identifier && seen_refs.insert(t.identifier).second)
        referenced.emplace_back(t.identifier, t.line);
}

void AstNameScan::stmts(Stmt** list, size_t count) {
    for (size_t i = 0; i < count; i++) stmt(list[i]);
}

void AstNameScan::pattern(const CasePattern* p) {
    if (!p) return;
    switch (p->type) {
        case PATTERN_EXPRESSION:
            expr(p->as.expr_pattern.expression);
            break;
        case PATTERN_RANGE:
            expr(p->as.range_pattern.start);
            expr(p->as.range_pattern.end);
            break;
        case PATTERN_ARRAY:
            for (size_t i = 0; i < p->as.array_pattern.element_count; i++) {
                declare(p->as.array_pattern.elements[i].name);
                pattern(p->as.array_pattern.elements[i].pattern);
            }
            declare(p->as.array_pattern.rest_name);
            break;
        case PATTERN_TABLE:
            for (size_t i = 0; i < p->as.table_pattern.field_count; i++) {
                const TablePattern& f = p->as.table_pattern.fields[i];
                declare(f.bind_name ? f.bind_name : f.key);
                pattern(f.pattern);
            }
            break;
        default:
            break;
    }
}

void AstNameScan::stmt(Stmt* s) {
    if (!s) return;
    switch (s->type) {
        case STMT_EXPRESSION: expr(s->as.expression.expression); break;
        case STMT_PRINT:      expr(s->as.print.expression); break;
        case STMT_VAR:
            declare(s->as.var.name);
            expr(s->as.var.initializer);
            break;
        case STMT_BLOCK:      stmts(s->as.block.statements, s->as.block.count); break;
        case STMT_IF:
            expr(s->as.if_stmt.condition);
            stmt(s->as.if_stmt.then_branch);
            stmt(s->as.if_stmt.else_branch);
            break;
        case STMT_WHILE:
            expr(s->as.while_stmt.condition);
            stmt(s->as.while_stmt.body);
            break;
        case STMT_FOR:
            stmt(s->as.for_stmt.initializer);
            expr(s->as.for_stmt.condition);
            expr(s->as.for_stmt.increment);
            stmt(s->as.for_stmt.body);
            break;
        case STMT_FOR_IN:
            declare(s->as.for_in_stmt.var_name);
            if (s->as.for_in_stmt.has_two_vars) declare(s->as.for_in_stmt.var_name2);
            expr(s->as.for_in_stmt.iterable);
            stmt(s->as.for_in_stmt.body);
            break;
        case STMT_FUNCTION: {
            const FunctionStmt& fn = s->as.function;
            declare(fn.name);
            for (size_t i = 0; i < fn.param_count; i++) declare(fn.params[i]);
            stmts(fn.body, fn.body_count);
            break;
        }
        case STMT_RETURN:     expr(s->as.return_stmt.value); break;
        case STMT_THROW:      expr(s->as.throw_stmt.value); break;
        case STMT_TRY_CATCH: {
            const TryCatchStmt& t = s->as.try_catch_stmt;
            stmts(t.try_body, t.try_body_count);
            declare(t.catch_var);
            stmts(t.catch_body, t.catch_body_count);
            stmts(t.finally_body, t.finally_body_count);
            break;
        }
        case STMT_SWITCH: {
            const SwitchStmt& sw = s->as.switch_stmt;
            expr(sw.discriminant);
            for (size_t i = 0; i < sw.case_count; i++) {
                SwitchCase* c = sw.cases[i];
                for (size_t p = 0; p < c->pattern_count; p++) pattern(c->patterns[p]);
                expr(c->guard);
                stmts(c->body, c->body_count);
            }
            stmts(sw.default_body, sw.default_body_count);
            break;
        }
        default:
            break;   // imports, enums, structs, pragmas, break/continue
    }
}

void AstNameScan::expr(Expr* e) {
    if (!e) return;
    switch (e->type) {
        case EXPR_VARIABLE:   reference(e->as.variable.name); break;
        case EXPR_INCREMENT:
        case EXPR_DECREMENT:  reference(e->as.increment.name); break;
        case EXPR_BINARY:
            expr(e->as.binary.left);
            expr(e->as.binary.right);
            break;
        case EXPR_UNARY:      expr(e->as.unary.right); break;
        case EXPR_ASSIGNMENT:
            expr(e->as.assignment.target);
            expr(e->as.assignment.value);
            break;
        case EXPR_CALL:
            expr(e->as.call.callee);
            for (size_t i = 0; i < e->as.call.arg_count; i++) expr(e->as.call.arguments[i]);
            break;
        case EXPR_GROUPING:   expr(e->as.grouping.expression); break;
        case EXPR_ARRAY_LITERAL:
            for (size_t i = 0; i < e->as.array_literal.element_count; i++)
                expr(e->as.array_literal.elements[i]);
            break;
        case EXPR_ARRAY_INDEX:
            expr(e->as.array_index.array);
            expr(e->as.array_index.index);
            break;
        case EXPR_TABLE_LITERAL:
            for (size_t i = 0; i < e->as.table_literal.pair_count; i++) {
                expr(e->as.table_literal.pairs[i].key);
                expr(e->as.table_literal.pairs[i].value);
            }
            break;
        case EXPR_TABLE_INDEX:
            expr(e->as.table_index.table);
            expr(e->as.table_index.index);
            break;
        case EXPR_TABLE_DOT:
        case EXPR_METHOD_DOT: expr(e->as.table_dot.table); break;
        case EXPR_TERNARY:
            expr(e->as.ternary.condition);
            expr(e->as.ternary.then_expr);
            expr(e->as.ternary.else_expr);
            break;
        case EXPR_FUNCTION: {
            // A closure created in the fiber runs in the fiber too.
            const FunctionExpr& fn = e->as.function_expr;
            declare(fn.name);
            for (size_t i = 0; i < fn.param_count; i++) declare(fn.params[i]);
            stmts(fn.body, fn.body_count);
            break;
        }
        case EXPR_SPAWN:
            // A nested spawn starts its own fiber, which follows the same
            // rule; its arguments are evaluated here.
            expr(e->as.spawn.callee);
            for (size_t i = 0; i < e->as.spawn.arg_count; i++) expr(e->as.spawn.arguments[i]);
            break;
        case EXPR_AWAIT:      expr(e->as.await.operand); break;
        case EXPR_SHARED:     expr(e->as.shared.operand); break;
        case EXPR_ATOMIC:     expr(e->as.atomic.body); break;
        default:
            break;   // literals, enum access
    }
}

bool expr_mentions_name(Expr* e, const char* name) {
    if (!e || !name) return false;
    AstNameScan scan;
    scan.expr(e);
    return scan.seen_refs.count(name) > 0;
}
