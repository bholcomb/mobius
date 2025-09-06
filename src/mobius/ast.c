#include "ast.h"
#include "table.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Expression constructors
Expr* make_binary_expr(Expr* left, Token op, Expr* right) {
    Expr* expr = calloc(1, sizeof(Expr));
    if (!expr) return NULL;
    
    expr->type = EXPR_BINARY;
    expr->ref_count = 1;  // Initialize reference count
    expr->as.binary.left = left;
    expr->as.binary.op = op;
    expr->as.binary.right = right;
    return expr;
}

Expr* make_unary_expr(Token op, Expr* right) {
    Expr* expr = calloc(1, sizeof(Expr));
    if (!expr) return NULL;
    
    expr->type = EXPR_UNARY;
    expr->ref_count = 1;  // Initialize reference count
    expr->as.unary.op = op;
    expr->as.unary.right = right;
    return expr;
}

Expr* make_literal_expr(Value value) {
    Expr* expr = calloc(1, sizeof(Expr));
    if (!expr) return NULL;
    
    expr->type = EXPR_LITERAL;
    expr->ref_count = 1;  // Initialize reference count
    expr->as.literal.value = value;
    return expr;
}

Expr* make_variable_expr(Token name) {
    Expr* expr = calloc(1, sizeof(Expr));
    if (!expr) return NULL;
    
    expr->type = EXPR_VARIABLE;
    expr->ref_count = 1;  // Initialize reference count
    expr->as.variable.name = name;
    return expr;
}

Expr* make_assignment_expr(Token name, Expr* value) {
    Expr* expr = calloc(1, sizeof(Expr));
    if (!expr) return NULL;
    
    expr->type = EXPR_ASSIGNMENT;
    expr->ref_count = 1;  // Initialize reference count
    expr->as.assignment.name = name;
    expr->as.assignment.value = value;
    return expr;
}

Expr* make_call_expr(Expr* callee, Token paren, Expr** arguments, size_t arg_count) {
    Expr* expr = calloc(1, sizeof(Expr));
    if (!expr) return NULL;
    
    expr->type = EXPR_CALL;
    expr->ref_count = 1;  // Initialize reference count
    expr->as.call.callee = callee;
    expr->as.call.paren = paren;
    expr->as.call.arguments = arguments;
    expr->as.call.arg_count = arg_count;
    return expr;
}

Expr* make_grouping_expr(Expr* expression) {
    Expr* expr = calloc(1, sizeof(Expr));
    if (!expr) return NULL;
    
    expr->type = EXPR_GROUPING;
    expr->ref_count = 1;  // Initialize reference count
    expr->as.grouping.expression = expression;
    return expr;
}

Expr* make_table_literal_expr(TablePair* pairs, size_t pair_count) {
    Expr* expr = calloc(1, sizeof(Expr));
    if (!expr) return NULL;
    
    expr->type = EXPR_TABLE_LITERAL;
    expr->ref_count = 1;  // Initialize reference count
    expr->as.table_literal.pairs = pairs;
    expr->as.table_literal.pair_count = pair_count;
    return expr;
}

Expr* make_table_index_expr(Expr* table, Expr* index) {
    Expr* expr = calloc(1, sizeof(Expr));
    if (!expr) return NULL;
    
    expr->type = EXPR_TABLE_INDEX;
    expr->ref_count = 1;  // Initialize reference count
    expr->as.table_index.table = table;
    expr->as.table_index.index = index;
    return expr;
}

Expr* make_table_dot_expr(Expr* table, Token key) {
    Expr* expr = calloc(1, sizeof(Expr));
    if (!expr) return NULL;
    
    expr->type = EXPR_TABLE_DOT;
    expr->ref_count = 1;  // Initialize reference count
    expr->as.table_dot.table = table;
    expr->as.table_dot.key = key;
    return expr;
}

// Statement constructors
Stmt* make_expression_stmt(Expr* expression) {
    Stmt* stmt = calloc(1, sizeof(Stmt));
    if (!stmt) return NULL;
    
    stmt->type = STMT_EXPRESSION;
    stmt->ref_count = 1;  // Initialize reference count
    stmt->as.expression.expression = expression;
    return stmt;
}

Stmt* make_print_stmt(Expr* expression) {
    Stmt* stmt = calloc(1, sizeof(Stmt));
    if (!stmt) return NULL;
    
    stmt->type = STMT_PRINT;
    stmt->ref_count = 1;  // Initialize reference count
    stmt->as.print.expression = expression;
    return stmt;
}

Stmt* make_var_stmt(Token name, Expr* initializer, TypeInfo type_hint) {
    Stmt* stmt = calloc(1, sizeof(Stmt));
    if (!stmt) return NULL;
    
    stmt->type = STMT_VAR;
    stmt->ref_count = 1;  // Initialize reference count
    stmt->as.var.name = name;
    stmt->as.var.initializer = initializer;
    stmt->as.var.type_hint = type_hint;
    return stmt;
}

Stmt* make_block_stmt(Stmt** statements, size_t count) {
    Stmt* stmt = calloc(1, sizeof(Stmt));
    if (!stmt) return NULL;
    
    stmt->type = STMT_BLOCK;
    stmt->ref_count = 1;  // Initialize reference count
    stmt->as.block.statements = statements;
    stmt->as.block.count = count;
    return stmt;
}

Stmt* make_if_stmt(Expr* condition, Stmt* then_branch, Stmt* else_branch) {
    Stmt* stmt = calloc(1, sizeof(Stmt));
    if (!stmt) return NULL;
    
    stmt->type = STMT_IF;
    stmt->ref_count = 1;  // Initialize reference count
    stmt->as.if_stmt.condition = condition;
    stmt->as.if_stmt.then_branch = then_branch;
    stmt->as.if_stmt.else_branch = else_branch;
    return stmt;
}

Stmt* make_while_stmt(Expr* condition, Stmt* body) {
    Stmt* stmt = calloc(1, sizeof(Stmt));
    if (!stmt) return NULL;
    
    stmt->type = STMT_WHILE;
    stmt->ref_count = 1;  // Initialize reference count
    stmt->as.while_stmt.condition = condition;
    stmt->as.while_stmt.body = body;
    return stmt;
}

Stmt* make_for_stmt(Stmt* initializer, Expr* condition, Expr* increment, Stmt* body) {
    Stmt* stmt = calloc(1, sizeof(Stmt));
    if (!stmt) return NULL;
    
    stmt->type = STMT_FOR;
    stmt->ref_count = 1;  // Initialize reference count
    stmt->as.for_stmt.initializer = initializer;
    stmt->as.for_stmt.condition = condition;
    stmt->as.for_stmt.increment = increment;
    stmt->as.for_stmt.body = body;
    return stmt;
}

Stmt* make_function_stmt(Token name, Token* params, size_t param_count, 
                        Stmt** body, size_t body_count) {
    Stmt* stmt = calloc(1, sizeof(Stmt));
    if (!stmt) return NULL;
    
    stmt->type = STMT_FUNCTION;
    stmt->ref_count = 1;  // Initialize reference count
    stmt->as.function.name = name;
    stmt->as.function.params = params;
    stmt->as.function.param_count = param_count;
    stmt->as.function.body = body;
    stmt->as.function.body_count = body_count;
    return stmt;
}

Stmt* make_class_stmt(Token name, VariableExpr* superclass, 
                     FunctionStmt** methods, size_t method_count) {
    Stmt* stmt = calloc(1, sizeof(Stmt));
    if (!stmt) return NULL;
    
    stmt->type = STMT_CLASS;
    stmt->ref_count = 1;  // Initialize reference count
    stmt->as.class_stmt.name = name;
    stmt->as.class_stmt.superclass = superclass;
    stmt->as.class_stmt.methods = methods;
    stmt->as.class_stmt.method_count = method_count;
    return stmt;
}

Stmt* make_return_stmt(Token keyword, Expr* value) {
    Stmt* stmt = calloc(1, sizeof(Stmt));
    if (!stmt) return NULL;
    
    stmt->type = STMT_RETURN;
    stmt->ref_count = 1;  // Initialize reference count
    stmt->as.return_stmt.keyword = keyword;
    stmt->as.return_stmt.value = value;
    return stmt;
}


// Basic AST printing for debugging
void print_expr(Expr* expr) {
    if (!expr) {
        printf("(null)");
        return;
    }
    
    switch (expr->type) {
        case EXPR_BINARY:
            printf("(");
            print_expr(expr->as.binary.left);
            printf(" OP ");
            print_expr(expr->as.binary.right);
            printf(")");
            break;
        case EXPR_UNARY:
            printf("(UNARY ");
            print_expr(expr->as.unary.right);
            printf(")");
            break;
        case EXPR_LITERAL:
            print_value(expr->as.literal.value);
            break;
        case EXPR_VARIABLE:
            printf("%s", expr->as.variable.name.identifier ? expr->as.variable.name.identifier : "unknown");
            break;
        case EXPR_ASSIGNMENT:
            printf("(%s = ", expr->as.assignment.name.identifier ? expr->as.assignment.name.identifier : "unknown");
            print_expr(expr->as.assignment.value);
            printf(")");
            break;
        case EXPR_CALL:
            print_expr(expr->as.call.callee);
            printf("(");
            for (size_t i = 0; i < expr->as.call.arg_count; i++) {
                if (i > 0) printf(", ");
                print_expr(expr->as.call.arguments[i]);
            }
            printf(")");
            break;
        case EXPR_GROUPING:
            printf("(group ");
            print_expr(expr->as.grouping.expression);
            printf(")");
            break;
        case EXPR_TABLE_LITERAL:
            printf("(table ");
            for (size_t i = 0; i < expr->as.table_literal.pair_count; i++) {
                if (i > 0) printf(" ");
                if (expr->as.table_literal.pairs[i].key) {
                    if (expr->as.table_literal.pairs[i].is_computed_key) {
                        printf("[");
                        print_expr(expr->as.table_literal.pairs[i].key);
                        printf("]=");
                    } else {
                        print_expr(expr->as.table_literal.pairs[i].key);
                        printf(":");
                    }
                }
                print_expr(expr->as.table_literal.pairs[i].value);
            }
            printf(")");
            break;
        case EXPR_TABLE_INDEX:
            printf("(index ");
            print_expr(expr->as.table_index.table);
            printf("[");
            print_expr(expr->as.table_index.index);
            printf("])");
            break;
        case EXPR_TABLE_DOT:
            printf("(dot ");
            print_expr(expr->as.table_dot.table);
            printf(".%s)", expr->as.table_dot.key.identifier ? expr->as.table_dot.key.identifier : "unknown");
            break;
    }
}

void print_stmt(Stmt* stmt) {
    if (!stmt) {
        printf("(null statement)");
        return;
    }
    
    switch (stmt->type) {
        case STMT_EXPRESSION:
            print_expr(stmt->as.expression.expression);
            printf(";");
            break;
        case STMT_PRINT:
            printf("print ");
            print_expr(stmt->as.print.expression);
            printf(";");
            break;
        case STMT_VAR:
            printf("var %s", stmt->as.var.name.identifier ? stmt->as.var.name.identifier : "unknown");
            if (stmt->as.var.initializer) {
                printf(" = ");
                print_expr(stmt->as.var.initializer);
            }
            printf(";");
            break;
        case STMT_BLOCK:
            printf("{\n");
            for (size_t i = 0; i < stmt->as.block.count; i++) {
                printf("  ");
                print_stmt(stmt->as.block.statements[i]);
                printf("\n");
            }
            printf("}");
            break;
        case STMT_IF:
            printf("if (");
            print_expr(stmt->as.if_stmt.condition);
            printf(") ");
            print_stmt(stmt->as.if_stmt.then_branch);
            if (stmt->as.if_stmt.else_branch) {
                printf(" else ");
                print_stmt(stmt->as.if_stmt.else_branch);
            }
            break;
        case STMT_WHILE:
            printf("while (");
            print_expr(stmt->as.while_stmt.condition);
            printf(") ");
            print_stmt(stmt->as.while_stmt.body);
            break;
        case STMT_FOR:
            printf("for (");
            if (stmt->as.for_stmt.initializer) print_stmt(stmt->as.for_stmt.initializer);
            printf(" ");
            if (stmt->as.for_stmt.condition) print_expr(stmt->as.for_stmt.condition);
            printf("; ");
            if (stmt->as.for_stmt.increment) print_expr(stmt->as.for_stmt.increment);
            printf(") ");
            print_stmt(stmt->as.for_stmt.body);
            break;
        case STMT_FUNCTION:
            printf("func %s(", stmt->as.function.name.identifier ? stmt->as.function.name.identifier : "unknown");
            for (size_t i = 0; i < stmt->as.function.param_count; i++) {
                if (i > 0) printf(", ");
                printf("%s", stmt->as.function.params[i].identifier ? stmt->as.function.params[i].identifier : "unknown");
            }
            printf(") { ... }");
            break;
        case STMT_CLASS:
            printf("class %s", stmt->as.class_stmt.name.identifier ? stmt->as.class_stmt.name.identifier : "unknown");
            if (stmt->as.class_stmt.superclass) {
                printf(" < %s", stmt->as.class_stmt.superclass->name.identifier ? stmt->as.class_stmt.superclass->name.identifier : "unknown");
            }
            printf(" { ... }");
            break;
        case STMT_RETURN:
            printf("return");
            if (stmt->as.return_stmt.value) {
                printf(" ");
                print_expr(stmt->as.return_stmt.value);
            }
            printf(";");
            break;
    }
}

// Memory management functions
void free_expr(Expr* expr) {
    if (!expr) return;
    
    switch (expr->type) {
        case EXPR_BINARY:
            free_expr(expr->as.binary.left);
            free_expr(expr->as.binary.right);
            break;
        case EXPR_UNARY:
            free_expr(expr->as.unary.right);
            break;
        case EXPR_CALL:
            free_expr(expr->as.call.callee);
            for (size_t i = 0; i < expr->as.call.arg_count; i++) {
                free_expr(expr->as.call.arguments[i]);
            }
            if (expr->as.call.arguments) free(expr->as.call.arguments);
            break;
        case EXPR_GROUPING:
            free_expr(expr->as.grouping.expression);
            break;
        case EXPR_LITERAL:
            free_value(expr->as.literal.value);
            break;
        case EXPR_VARIABLE:
            // Token doesn't need freeing
            break;
        case EXPR_ASSIGNMENT:
            free_expr(expr->as.assignment.value);
            break;
        case EXPR_TABLE_LITERAL:
            for (size_t i = 0; i < expr->as.table_literal.pair_count; i++) {
                if (expr->as.table_literal.pairs[i].key) {
                    free_expr(expr->as.table_literal.pairs[i].key);
                }
                free_expr(expr->as.table_literal.pairs[i].value);
            }
            if (expr->as.table_literal.pairs) free(expr->as.table_literal.pairs);
            break;
        case EXPR_TABLE_INDEX:
            free_expr(expr->as.table_index.table);
            free_expr(expr->as.table_index.index);
            break;
        case EXPR_TABLE_DOT:
            free_expr(expr->as.table_dot.table);
            break;
    }
    free(expr);
}

void free_stmt(Stmt* stmt) {
    if (!stmt) return;
    
    switch (stmt->type) {
        case STMT_EXPRESSION:
            free_expr(stmt->as.expression.expression);
            break;
        case STMT_PRINT:
            free_expr(stmt->as.print.expression);
            break;
        case STMT_VAR:
            if (stmt->as.var.initializer) {
                free_expr(stmt->as.var.initializer);
            }
            break;
        case STMT_BLOCK:
            for (size_t i = 0; i < stmt->as.block.count; i++) {
                free_stmt(stmt->as.block.statements[i]);
            }
            if (stmt->as.block.statements) free(stmt->as.block.statements);
            break;
        case STMT_IF:
            free_expr(stmt->as.if_stmt.condition);
            free_stmt(stmt->as.if_stmt.then_branch);
            if (stmt->as.if_stmt.else_branch) {
                free_stmt(stmt->as.if_stmt.else_branch);
            }
            break;
        case STMT_WHILE:
            free_expr(stmt->as.while_stmt.condition);
            free_stmt(stmt->as.while_stmt.body);
            break;
        case STMT_FOR:
            if (stmt->as.for_stmt.initializer) {
                free_stmt(stmt->as.for_stmt.initializer);
            }
            if (stmt->as.for_stmt.condition) {
                free_expr(stmt->as.for_stmt.condition);
            }
            if (stmt->as.for_stmt.increment) {
                free_expr(stmt->as.for_stmt.increment);
            }
            free_stmt(stmt->as.for_stmt.body);
            break;
        case STMT_FUNCTION:
            if (stmt->as.function.params) free(stmt->as.function.params);
            for (size_t i = 0; i < stmt->as.function.body_count; i++) {
                free_stmt(stmt->as.function.body[i]);
            }
            if (stmt->as.function.body) free(stmt->as.function.body);
            break;
        case STMT_CLASS:
            if (stmt->as.class_stmt.superclass) {
                free_expr((Expr*)stmt->as.class_stmt.superclass);
            }
            for (size_t i = 0; i < stmt->as.class_stmt.method_count; i++) {
                free_stmt((Stmt*)stmt->as.class_stmt.methods[i]);
            }
            if (stmt->as.class_stmt.methods) free(stmt->as.class_stmt.methods);
            break;
        case STMT_RETURN:
            if (stmt->as.return_stmt.value) {
                free_expr(stmt->as.return_stmt.value);
            }
            break;
    }
    free(stmt);
}

// ============================================================================
// AST Reference Counting Implementation
// ============================================================================

Expr* ast_retain_expr(Expr* expr) {
    if (!expr) return NULL;
    
    expr->ref_count++;
    return expr;
}

void ast_release_expr(Expr* expr) {
    if (!expr) return;
    
    expr->ref_count--;
    if (expr->ref_count <= 0) {
        // Recursively release referenced expressions
        switch (expr->type) {
            case EXPR_BINARY:
                ast_release_expr(expr->as.binary.left);
                ast_release_expr(expr->as.binary.right);
                break;
            case EXPR_UNARY:
                ast_release_expr(expr->as.unary.right);
                break;
            case EXPR_ASSIGNMENT:
                ast_release_expr(expr->as.assignment.value);
                break;
            case EXPR_CALL:
                ast_release_expr(expr->as.call.callee);
                if (expr->as.call.arguments) {
                    for (size_t i = 0; i < expr->as.call.arg_count; i++) {
                        ast_release_expr(expr->as.call.arguments[i]);
                    }
                    free(expr->as.call.arguments);
                }
                break;
            case EXPR_GROUPING:
                ast_release_expr(expr->as.grouping.expression);
                break;
            case EXPR_TABLE_LITERAL:
                if (expr->as.table_literal.pairs) {
                    for (size_t i = 0; i < expr->as.table_literal.pair_count; i++) {
                        if (expr->as.table_literal.pairs[i].key) {
                            ast_release_expr(expr->as.table_literal.pairs[i].key);
                        }
                        ast_release_expr(expr->as.table_literal.pairs[i].value);
                    }
                    free(expr->as.table_literal.pairs);
                }
                break;
            case EXPR_TABLE_INDEX:
                ast_release_expr(expr->as.table_index.table);
                ast_release_expr(expr->as.table_index.index);
                break;
            case EXPR_TABLE_DOT:
                ast_release_expr(expr->as.table_dot.table);
                break;
            case EXPR_LITERAL:
            case EXPR_VARIABLE:
                // These don't reference other expressions
                break;
        }
        
        // Free the literal value if it exists
        if (expr->type == EXPR_LITERAL) {
            free_value(expr->as.literal.value);
        }
        
        free(expr);
    }
}

Stmt* ast_retain_stmt(Stmt* stmt) {
    if (!stmt) return NULL;
    
    stmt->ref_count++;
    return stmt;
}

void ast_release_stmt(Stmt* stmt) {
    if (!stmt) return;
    
    stmt->ref_count--;
    if (stmt->ref_count <= 0) {
        // Recursively release referenced statements and expressions
        switch (stmt->type) {
            case STMT_EXPRESSION:
                ast_release_expr(stmt->as.expression.expression);
                break;
            case STMT_PRINT:
                ast_release_expr(stmt->as.print.expression);
                break;
            case STMT_VAR:
                if (stmt->as.var.initializer) {
                    ast_release_expr(stmt->as.var.initializer);
                }
                break;
            case STMT_BLOCK:
                if (stmt->as.block.statements) {
                    for (size_t i = 0; i < stmt->as.block.count; i++) {
                        ast_release_stmt(stmt->as.block.statements[i]);
                    }
                    free(stmt->as.block.statements);
                }
                break;
            case STMT_IF:
                ast_release_expr(stmt->as.if_stmt.condition);
                ast_release_stmt(stmt->as.if_stmt.then_branch);
                if (stmt->as.if_stmt.else_branch) {
                    ast_release_stmt(stmt->as.if_stmt.else_branch);
                }
                break;
            case STMT_WHILE:
                ast_release_expr(stmt->as.while_stmt.condition);
                ast_release_stmt(stmt->as.while_stmt.body);
                break;
            case STMT_FOR:
                if (stmt->as.for_stmt.initializer) {
                    ast_release_stmt(stmt->as.for_stmt.initializer);
                }
                if (stmt->as.for_stmt.condition) {
                    ast_release_expr(stmt->as.for_stmt.condition);
                }
                if (stmt->as.for_stmt.increment) {
                    ast_release_expr(stmt->as.for_stmt.increment);
                }
                ast_release_stmt(stmt->as.for_stmt.body);
                break;
            case STMT_FUNCTION:
                if (stmt->as.function.body) {
                    for (size_t i = 0; i < stmt->as.function.body_count; i++) {
                        ast_release_stmt(stmt->as.function.body[i]);
                    }
                    free(stmt->as.function.body);
                }
                if (stmt->as.function.params) {
                    free(stmt->as.function.params);
                }
                break;
            case STMT_CLASS:
                if (stmt->as.class_stmt.superclass) {
                    ast_release_expr((Expr*)stmt->as.class_stmt.superclass);
                }
                if (stmt->as.class_stmt.methods) {
                    for (size_t i = 0; i < stmt->as.class_stmt.method_count; i++) {
                        ast_release_stmt((Stmt*)stmt->as.class_stmt.methods[i]);
                    }
                    free(stmt->as.class_stmt.methods);
                }
                break;
            case STMT_RETURN:
                if (stmt->as.return_stmt.value) {
                    ast_release_expr(stmt->as.return_stmt.value);
                }
                break;
        }
        
        free(stmt);
    }
}

// Helper functions for arrays and complex structures
void ast_retain_stmt_array(Stmt** stmts, size_t count) {
    if (!stmts) return;
    
    for (size_t i = 0; i < count; i++) {
        ast_retain_stmt(stmts[i]);
    }
}

void ast_release_stmt_array(Stmt** stmts, size_t count) {
    if (!stmts) return;
    
    for (size_t i = 0; i < count; i++) {
        ast_release_stmt(stmts[i]);
    }
}

void ast_retain_expr_array(Expr** exprs, size_t count) {
    if (!exprs) return;
    
    for (size_t i = 0; i < count; i++) {
        ast_retain_expr(exprs[i]);
    }
}

void ast_release_expr_array(Expr** exprs, size_t count) {
    if (!exprs) return;
    
    for (size_t i = 0; i < count; i++) {
        ast_release_expr(exprs[i]);
    }
}
