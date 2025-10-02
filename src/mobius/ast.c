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

Expr* make_array_literal_expr(Expr** elements, size_t element_count) {
    Expr* expr = calloc(1, sizeof(Expr));
    if (!expr) return NULL;
    
    expr->type = EXPR_ARRAY_LITERAL;
    expr->ref_count = 1;  // Initialize reference count
    expr->as.array_literal.elements = elements;
    expr->as.array_literal.element_count = element_count;
    return expr;
}

Expr* make_array_index_expr(Expr* array, Expr* index) {
    Expr* expr = calloc(1, sizeof(Expr));
    if (!expr) return NULL;
    
    expr->type = EXPR_ARRAY_INDEX;
    expr->ref_count = 1;  // Initialize reference count
    expr->as.array_index.array = array;
    expr->as.array_index.index = index;
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

Expr* make_enum_access_expr(Token enum_name, Token member_name) {
    Expr* expr = calloc(1, sizeof(Expr));
    if (!expr) return NULL;
    
    expr->type = EXPR_ENUM_ACCESS;
    expr->ref_count = 1;  // Initialize reference count
    expr->as.enum_access.enum_name = enum_name;
    expr->as.enum_access.member_name = member_name;
    return expr;
}

Expr* make_increment_expr(Token name, bool is_prefix, bool is_increment, Token op) {
    Expr* expr = calloc(1, sizeof(Expr));
    if (!expr) return NULL;
    
    expr->type = is_increment ? EXPR_INCREMENT : EXPR_DECREMENT;
    expr->ref_count = 1;  // Initialize reference count
    expr->as.increment.name = name;
    expr->as.increment.is_prefix = is_prefix;
    expr->as.increment.is_increment = is_increment;
    expr->as.increment.op = op;
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

Stmt* make_var_stmt(Token name, Expr* initializer, NumberInfo type_hint) {
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


Stmt* make_return_stmt(Token keyword, Expr* value) {
    Stmt* stmt = calloc(1, sizeof(Stmt));
    if (!stmt) return NULL;
    
    stmt->type = STMT_RETURN;
    stmt->ref_count = 1;  // Initialize reference count
    stmt->as.return_stmt.keyword = keyword;
    stmt->as.return_stmt.value = value;
    return stmt;
}

Stmt* make_enum_stmt(Token keyword, Token name, NumericType underlying_type, 
                     bool has_explicit_type, EnumMemberDef* members) {
    Stmt* stmt = calloc(1, sizeof(Stmt));
    if (!stmt) return NULL;
    
    stmt->type = STMT_ENUM;
    stmt->ref_count = 1;  // Initialize reference count
    stmt->as.enum_stmt.keyword = keyword;
    stmt->as.enum_stmt.name = name;
    stmt->as.enum_stmt.underlying_type = underlying_type;
    stmt->as.enum_stmt.has_explicit_type = has_explicit_type;
    stmt->as.enum_stmt.members = members;
    return stmt;
}

EnumMemberDef* make_enum_member(Token name, Expr* value) {
    EnumMemberDef* member = calloc(1, sizeof(EnumMemberDef));
    if (!member) return NULL;
    
    member->name = name;
    member->value = value;
    member->next = NULL;
    return member;
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
        case EXPR_ARRAY_LITERAL:
            printf("[");
            for (size_t i = 0; i < expr->as.array_literal.element_count; i++) {
                if (i > 0) printf(", ");
                print_expr(expr->as.array_literal.elements[i]);
            }
            printf("]");
            break;
        case EXPR_ARRAY_INDEX:
            print_expr(expr->as.array_index.array);
            printf("[");
            print_expr(expr->as.array_index.index);
            printf("]");
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
        case EXPR_ENUM_ACCESS:
            printf("(enum %s.%s)", 
                   expr->as.enum_access.enum_name.identifier ? expr->as.enum_access.enum_name.identifier : "unknown",
                   expr->as.enum_access.member_name.identifier ? expr->as.enum_access.member_name.identifier : "unknown");
            break;
        case EXPR_INCREMENT:
            printf("(%s%s)", 
                   expr->as.increment.is_prefix ? "++" : "",
                   expr->as.increment.name.identifier ? expr->as.increment.name.identifier : "unknown");
            if (!expr->as.increment.is_prefix) printf("++");
            break;
        case EXPR_DECREMENT:
            printf("(%s%s)", 
                   expr->as.increment.is_prefix ? "--" : "",
                   expr->as.increment.name.identifier ? expr->as.increment.name.identifier : "unknown");
            if (!expr->as.increment.is_prefix) printf("--");
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
        case STMT_RETURN:
            printf("return");
            if (stmt->as.return_stmt.value) {
                printf(" ");
                print_expr(stmt->as.return_stmt.value);
            }
            printf(";");
            break;
        case STMT_SWITCH:
            printf("switch (");
            if (stmt->as.switch_stmt.discriminant) {
                print_expr(stmt->as.switch_stmt.discriminant);
            }
            printf(") { %zu cases }", stmt->as.switch_stmt.case_count);
            break;
        case STMT_BREAK:
            printf("break");
            break;
        case STMT_CONTINUE:
            printf("continue");
            break;
        case STMT_IMPORT:
            printf("import \"%s\"", stmt->as.import_stmt.module_name.literal.string ? stmt->as.import_stmt.module_name.literal.string : "unknown");
            if (stmt->as.import_stmt.has_alias) {
                printf(" as %s", stmt->as.import_stmt.alias.identifier ? stmt->as.import_stmt.alias.identifier : stmt->as.import_stmt.alias.literal.string);
            }
            break;
        case STMT_PRAGMA:
            printf("#pragma %s %s", 
                stmt->as.pragma_stmt.name.identifier ? stmt->as.pragma_stmt.name.identifier : "unknown",
                stmt->as.pragma_stmt.value.identifier ? stmt->as.pragma_stmt.value.identifier : 
                    (stmt->as.pragma_stmt.value.literal.string ? stmt->as.pragma_stmt.value.literal.string : "unknown"));
            break;
        case STMT_ENUM:
            printf("enum %s", stmt->as.enum_stmt.name.identifier ? stmt->as.enum_stmt.name.identifier : "unknown");
            if (stmt->as.enum_stmt.has_explicit_type) {
                printf(" : %s", numeric_type_name(stmt->as.enum_stmt.underlying_type));
            }
            printf(" { ... }");
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
        case EXPR_ARRAY_LITERAL:
            for (size_t i = 0; i < expr->as.array_literal.element_count; i++) {
                free_expr(expr->as.array_literal.elements[i]);
            }
            if (expr->as.array_literal.elements) free(expr->as.array_literal.elements);
            break;
        case EXPR_ARRAY_INDEX:
            free_expr(expr->as.array_index.array);
            free_expr(expr->as.array_index.index);
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
        case EXPR_ENUM_ACCESS:
            // No dynamic allocations to free for enum access
            break;
        case EXPR_INCREMENT:
        case EXPR_DECREMENT:
            // Token doesn't need freeing
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
        case STMT_RETURN:
            if (stmt->as.return_stmt.value) {
                free_expr(stmt->as.return_stmt.value);
            }
            break;
        case STMT_SWITCH:
            if (stmt->as.switch_stmt.discriminant) {
                free_expr(stmt->as.switch_stmt.discriminant);
            }
            // Free all case patterns and bodies
            for (size_t i = 0; i < stmt->as.switch_stmt.case_count; i++) {
                SwitchCase* case_clause = stmt->as.switch_stmt.cases[i];
                if (case_clause) {
                    // Free patterns
                    for (size_t j = 0; j < case_clause->pattern_count; j++) {
                        if (case_clause->patterns[j]) {
                            free_case_pattern(case_clause->patterns[j]);
                        }
                    }
                    if (case_clause->patterns) free(case_clause->patterns);
                    
                    // Free guard
                    if (case_clause->guard) {
                        free_expr(case_clause->guard);
                    }
                    
                    // Free body
                    for (size_t j = 0; j < case_clause->body_count; j++) {
                        if (case_clause->body[j]) {
                            free_stmt(case_clause->body[j]);
                        }
                    }
                    if (case_clause->body) free(case_clause->body);
                    
                    free(case_clause);
                }
            }
            if (stmt->as.switch_stmt.cases) free(stmt->as.switch_stmt.cases);
            
            // Free default body
            for (size_t i = 0; i < stmt->as.switch_stmt.default_body_count; i++) {
                if (stmt->as.switch_stmt.default_body[i]) {
                    free_stmt(stmt->as.switch_stmt.default_body[i]);
                }
            }
            if (stmt->as.switch_stmt.default_body) free(stmt->as.switch_stmt.default_body);
            break;
        case STMT_BREAK:
            // Nothing to free for break statements
            break;
        case STMT_CONTINUE:
            // Nothing to free for continue statements
            break;
        case STMT_IMPORT:
            // Nothing to free for import statements (tokens are not owned)
            break;
        case STMT_PRAGMA:
            // Nothing to free for pragma statements (tokens are not owned)
            break;
        case STMT_ENUM: {
            // Free enum members
            EnumMemberDef* member = stmt->as.enum_stmt.members;
            while (member) {
                EnumMemberDef* next = member->next;
                if (member->value) {
                    free_expr(member->value);
                }
                free(member);
                member = next;
            }
            break;
        }
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
            case EXPR_ARRAY_LITERAL:
                if (expr->as.array_literal.elements) {
                    for (size_t i = 0; i < expr->as.array_literal.element_count; i++) {
                        ast_release_expr(expr->as.array_literal.elements[i]);
                    }
                    free(expr->as.array_literal.elements);
                }
                break;
            case EXPR_ARRAY_INDEX:
                ast_release_expr(expr->as.array_index.array);
                ast_release_expr(expr->as.array_index.index);
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
            case EXPR_ENUM_ACCESS:
            case EXPR_INCREMENT:
            case EXPR_DECREMENT:
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
            case STMT_RETURN:
                if (stmt->as.return_stmt.value) {
                    ast_release_expr(stmt->as.return_stmt.value);
                }
                break;
            case STMT_SWITCH:
                if (stmt->as.switch_stmt.discriminant) {
                    ast_release_expr(stmt->as.switch_stmt.discriminant);
                }
                // Release all case patterns and bodies
                for (size_t i = 0; i < stmt->as.switch_stmt.case_count; i++) {
                    SwitchCase* case_clause = stmt->as.switch_stmt.cases[i];
                    if (case_clause) {
                        // Release patterns
                        for (size_t j = 0; j < case_clause->pattern_count; j++) {
                            if (case_clause->patterns[j]) {
                                free_case_pattern(case_clause->patterns[j]);
                            }
                        }
                        if (case_clause->patterns) free(case_clause->patterns);
                        
                        // Release guard
                        if (case_clause->guard) {
                            ast_release_expr(case_clause->guard);
                        }
                        
                        // Release body
                        for (size_t j = 0; j < case_clause->body_count; j++) {
                            if (case_clause->body[j]) {
                                ast_release_stmt(case_clause->body[j]);
                            }
                        }
                        if (case_clause->body) free(case_clause->body);
                        
                        free(case_clause);
                    }
                }
                if (stmt->as.switch_stmt.cases) free(stmt->as.switch_stmt.cases);
                
                // Release default body
                for (size_t i = 0; i < stmt->as.switch_stmt.default_body_count; i++) {
                    if (stmt->as.switch_stmt.default_body[i]) {
                        ast_release_stmt(stmt->as.switch_stmt.default_body[i]);
                    }
                }
                if (stmt->as.switch_stmt.default_body) free(stmt->as.switch_stmt.default_body);
                break;
            case STMT_BREAK:
                // Nothing to release for break statements
                break;
            case STMT_CONTINUE:
                // Nothing to release for continue statements
                break;
            case STMT_IMPORT:
                // Nothing to release for import statements (tokens are not owned)
                break;
            case STMT_PRAGMA:
                // Nothing to release for pragma statements (tokens are not owned)
                break;
            case STMT_ENUM: {
                // Release enum members
                EnumMemberDef* member = stmt->as.enum_stmt.members;
                while (member) {
                    EnumMemberDef* next = member->next;
                    if (member->value) {
                        ast_release_expr(member->value);
                    }
                    free(member);
                    member = next;
                }
                break;
            }
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

// Switch statement creation functions
Stmt* make_switch_stmt(Expr* discriminant, SwitchCase** cases, size_t case_count,
                      Stmt** default_body, size_t default_body_count) {
    Stmt* stmt = calloc(1, sizeof(Stmt));
    if (!stmt) return NULL;
    
    stmt->type = STMT_SWITCH;
    stmt->ref_count = 1;  // Initialize reference count
    stmt->as.switch_stmt.discriminant = discriminant;
    stmt->as.switch_stmt.cases = cases;
    stmt->as.switch_stmt.case_count = case_count;
    stmt->as.switch_stmt.default_body = default_body;
    stmt->as.switch_stmt.default_body_count = default_body_count;
    return stmt;
}

Stmt* make_break_stmt(Token keyword) {
    Stmt* stmt = calloc(1, sizeof(Stmt));
    if (!stmt) return NULL;
    
    stmt->type = STMT_BREAK;
    stmt->ref_count = 1;  // Initialize reference count
    stmt->as.break_stmt.keyword = keyword;
    return stmt;
}

Stmt* make_continue_stmt(Token keyword) {
    Stmt* stmt = calloc(1, sizeof(Stmt));
    if (!stmt) return NULL;
    
    stmt->type = STMT_CONTINUE;
    stmt->ref_count = 1;  // Initialize reference count
    stmt->as.continue_stmt.keyword = keyword;
    return stmt;
}

Stmt* make_import_stmt(Token keyword, Token module_name, Token alias, bool has_alias) {
    Stmt* stmt = calloc(1, sizeof(Stmt));
    if (!stmt) return NULL;
    
    stmt->type = STMT_IMPORT;
    stmt->ref_count = 1;  // Initialize reference count
    stmt->as.import_stmt.keyword = keyword;
    stmt->as.import_stmt.module_name = module_name;
    stmt->as.import_stmt.alias = alias;
    stmt->as.import_stmt.has_alias = has_alias;
    return stmt;
}

Stmt* make_pragma_stmt(Token keyword, Token name, Token value) {
    Stmt* stmt = calloc(1, sizeof(Stmt));
    if (!stmt) return NULL;
    
    stmt->type = STMT_PRAGMA;
    stmt->ref_count = 1;  // Initialize reference count
    stmt->as.pragma_stmt.keyword = keyword;
    stmt->as.pragma_stmt.name = name;
    stmt->as.pragma_stmt.value = value;
    return stmt;
}

// Pattern creation functions
CasePattern* make_value_pattern(Value literal) {
    CasePattern* pattern = calloc(1, sizeof(CasePattern));
    if (!pattern) return NULL;
    
    pattern->type = PATTERN_VALUE;
    pattern->as.literal = literal;
    return pattern;
}

CasePattern* make_expression_pattern(TokenType op, Expr* expression) {
    CasePattern* pattern = calloc(1, sizeof(CasePattern));
    if (!pattern) return NULL;
    
    pattern->type = PATTERN_EXPRESSION;
    pattern->as.expr_pattern.op = op;
    pattern->as.expr_pattern.expression = expression;
    return pattern;
}

CasePattern* make_range_pattern(Expr* start, Expr* end, bool inclusive) {
    CasePattern* pattern = calloc(1, sizeof(CasePattern));
    if (!pattern) return NULL;
    
    pattern->type = PATTERN_RANGE;
    pattern->as.range_pattern.start = start;
    pattern->as.range_pattern.end = end;
    pattern->as.range_pattern.inclusive = inclusive;
    return pattern;
}

CasePattern* make_type_pattern(ValueType value_type) {
    CasePattern* pattern = calloc(1, sizeof(CasePattern));
    if (!pattern) return NULL;
    
    pattern->type = PATTERN_TYPE;
    pattern->as.type_pattern.value_type = value_type;
    return pattern;
}

CasePattern* make_array_pattern(ArrayPattern* elements, size_t element_count, 
                               bool has_rest, char* rest_name) {
    CasePattern* pattern = calloc(1, sizeof(CasePattern));
    if (!pattern) return NULL;
    
    pattern->type = PATTERN_ARRAY;
    pattern->as.array_pattern.elements = elements;
    pattern->as.array_pattern.element_count = element_count;
    pattern->as.array_pattern.has_rest = has_rest;
    pattern->as.array_pattern.rest_name = rest_name;
    return pattern;
}

CasePattern* make_table_pattern(TablePattern* fields, size_t field_count, bool is_exhaustive) {
    CasePattern* pattern = calloc(1, sizeof(CasePattern));
    if (!pattern) return NULL;
    
    pattern->type = PATTERN_TABLE;
    pattern->as.table_pattern.fields = fields;
    pattern->as.table_pattern.field_count = field_count;
    pattern->as.table_pattern.is_exhaustive = is_exhaustive;
    return pattern;
}

CasePattern* make_wildcard_pattern(void) {
    CasePattern* pattern = calloc(1, sizeof(CasePattern));
    if (!pattern) return NULL;
    
    pattern->type = PATTERN_WILDCARD;
    return pattern;
}

SwitchCase* make_switch_case(CasePattern** patterns, size_t pattern_count,
                            Expr* guard, Stmt** body, size_t body_count, bool has_break) {
    SwitchCase* switch_case = calloc(1, sizeof(SwitchCase));
    if (!switch_case) return NULL;
    
    switch_case->patterns = patterns;
    switch_case->pattern_count = pattern_count;
    switch_case->guard = guard;
    switch_case->body = body;
    switch_case->body_count = body_count;
    switch_case->has_break = has_break;
    return switch_case;
}

// Pattern cleanup function
void free_case_pattern(CasePattern* pattern) {
    if (!pattern) return;
    
    switch (pattern->type) {
        case PATTERN_VALUE:
            // Value patterns don't need special cleanup
            break;
        case PATTERN_EXPRESSION:
            if (pattern->as.expr_pattern.expression) {
                free_expr(pattern->as.expr_pattern.expression);
            }
            break;
        case PATTERN_RANGE:
            if (pattern->as.range_pattern.start) {
                free_expr(pattern->as.range_pattern.start);
            }
            if (pattern->as.range_pattern.end) {
                free_expr(pattern->as.range_pattern.end);
            }
            break;
        case PATTERN_TYPE:
            // Type patterns don't need special cleanup
            break;
        case PATTERN_ARRAY:
            if (pattern->as.array_pattern.elements) {
                for (size_t i = 0; i < pattern->as.array_pattern.element_count; i++) {
                    ArrayPattern* elem = &pattern->as.array_pattern.elements[i];
                    if (elem->name) free(elem->name);
                    if (elem->pattern) free_case_pattern(elem->pattern);
                }
                free(pattern->as.array_pattern.elements);
            }
            if (pattern->as.array_pattern.rest_name) {
                free(pattern->as.array_pattern.rest_name);
            }
            break;
        case PATTERN_TABLE:
            if (pattern->as.table_pattern.fields) {
                for (size_t i = 0; i < pattern->as.table_pattern.field_count; i++) {
                    TablePattern* field = &pattern->as.table_pattern.fields[i];
                    if (field->key) free(field->key);
                    if (field->bind_name) free(field->bind_name);
                    if (field->pattern) free_case_pattern(field->pattern);
                }
                free(pattern->as.table_pattern.fields);
            }
            break;
        case PATTERN_WILDCARD:
            // Wildcard patterns don't need special cleanup
            break;
    }
    
    free(pattern);
}
