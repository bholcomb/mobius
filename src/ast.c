#include "ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Expression constructors
Expr* make_binary_expr(Expr* left, Token operator, Expr* right) {
    Expr* expr = malloc(sizeof(Expr));
    if (!expr) return NULL;
    
    expr->type = EXPR_BINARY;
    expr->as.binary.left = left;
    expr->as.binary.operator = operator;
    expr->as.binary.right = right;
    return expr;
}

Expr* make_unary_expr(Token operator, Expr* right) {
    Expr* expr = malloc(sizeof(Expr));
    if (!expr) return NULL;
    
    expr->type = EXPR_UNARY;
    expr->as.unary.operator = operator;
    expr->as.unary.right = right;
    return expr;
}

Expr* make_literal_expr(Value value) {
    Expr* expr = malloc(sizeof(Expr));
    if (!expr) return NULL;
    
    expr->type = EXPR_LITERAL;
    expr->as.literal.value = value;
    return expr;
}

Expr* make_variable_expr(Token name) {
    Expr* expr = malloc(sizeof(Expr));
    if (!expr) return NULL;
    
    expr->type = EXPR_VARIABLE;
    expr->as.variable.name = name;
    return expr;
}

Expr* make_assignment_expr(Token name, Expr* value) {
    Expr* expr = malloc(sizeof(Expr));
    if (!expr) return NULL;
    
    expr->type = EXPR_ASSIGNMENT;
    expr->as.assignment.name = name;
    expr->as.assignment.value = value;
    return expr;
}

Expr* make_call_expr(Expr* callee, Token paren, Expr** arguments, size_t arg_count) {
    Expr* expr = malloc(sizeof(Expr));
    if (!expr) return NULL;
    
    expr->type = EXPR_CALL;
    expr->as.call.callee = callee;
    expr->as.call.paren = paren;
    expr->as.call.arguments = arguments;
    expr->as.call.arg_count = arg_count;
    return expr;
}

Expr* make_grouping_expr(Expr* expression) {
    Expr* expr = malloc(sizeof(Expr));
    if (!expr) return NULL;
    
    expr->type = EXPR_GROUPING;
    expr->as.grouping.expression = expression;
    return expr;
}

// Statement constructors
Stmt* make_expression_stmt(Expr* expression) {
    Stmt* stmt = malloc(sizeof(Stmt));
    if (!stmt) return NULL;
    
    stmt->type = STMT_EXPRESSION;
    stmt->as.expression.expression = expression;
    return stmt;
}

Stmt* make_print_stmt(Expr* expression) {
    Stmt* stmt = malloc(sizeof(Stmt));
    if (!stmt) return NULL;
    
    stmt->type = STMT_PRINT;
    stmt->as.print.expression = expression;
    return stmt;
}

Stmt* make_var_stmt(Token name, Expr* initializer) {
    Stmt* stmt = malloc(sizeof(Stmt));
    if (!stmt) return NULL;
    
    stmt->type = STMT_VAR;
    stmt->as.var.name = name;
    stmt->as.var.initializer = initializer;
    return stmt;
}

Stmt* make_block_stmt(Stmt** statements, size_t count) {
    Stmt* stmt = malloc(sizeof(Stmt));
    if (!stmt) return NULL;
    
    stmt->type = STMT_BLOCK;
    stmt->as.block.statements = statements;
    stmt->as.block.count = count;
    return stmt;
}

Stmt* make_if_stmt(Expr* condition, Stmt* then_branch, Stmt* else_branch) {
    Stmt* stmt = malloc(sizeof(Stmt));
    if (!stmt) return NULL;
    
    stmt->type = STMT_IF;
    stmt->as.if_stmt.condition = condition;
    stmt->as.if_stmt.then_branch = then_branch;
    stmt->as.if_stmt.else_branch = else_branch;
    return stmt;
}

Stmt* make_while_stmt(Expr* condition, Stmt* body) {
    Stmt* stmt = malloc(sizeof(Stmt));
    if (!stmt) return NULL;
    
    stmt->type = STMT_WHILE;
    stmt->as.while_stmt.condition = condition;
    stmt->as.while_stmt.body = body;
    return stmt;
}

Stmt* make_for_stmt(Stmt* initializer, Expr* condition, Expr* increment, Stmt* body) {
    Stmt* stmt = malloc(sizeof(Stmt));
    if (!stmt) return NULL;
    
    stmt->type = STMT_FOR;
    stmt->as.for_stmt.initializer = initializer;
    stmt->as.for_stmt.condition = condition;
    stmt->as.for_stmt.increment = increment;
    stmt->as.for_stmt.body = body;
    return stmt;
}

Stmt* make_function_stmt(Token name, Token* params, size_t param_count, 
                        Stmt** body, size_t body_count) {
    Stmt* stmt = malloc(sizeof(Stmt));
    if (!stmt) return NULL;
    
    stmt->type = STMT_FUNCTION;
    stmt->as.function.name = name;
    stmt->as.function.params = params;
    stmt->as.function.param_count = param_count;
    stmt->as.function.body = body;
    stmt->as.function.body_count = body_count;
    return stmt;
}

Stmt* make_class_stmt(Token name, VariableExpr* superclass, 
                     FunctionStmt** methods, size_t method_count) {
    Stmt* stmt = malloc(sizeof(Stmt));
    if (!stmt) return NULL;
    
    stmt->type = STMT_CLASS;
    stmt->as.class.name = name;
    stmt->as.class.superclass = superclass;
    stmt->as.class.methods = methods;
    stmt->as.class.method_count = method_count;
    return stmt;
}

Stmt* make_return_stmt(Token keyword, Expr* value) {
    Stmt* stmt = malloc(sizeof(Stmt));
    if (!stmt) return NULL;
    
    stmt->type = STMT_RETURN;
    stmt->as.return_stmt.keyword = keyword;
    stmt->as.return_stmt.value = value;
    return stmt;
}

// Value constructors
Value make_nil_value() {
    Value value = {0};
    value.type = VAL_NIL;
    return value;
}

Value make_bool_value(bool val) {
    Value value = {0};
    value.type = VAL_BOOL;
    value.as.boolean = val;
    return value;
}

Value make_integer_value(NumericType type, int64_t val) {
    Value value = {0};
    value.type = VAL_INTEGER;
    value.as.integer.num_type = type;
    
    switch (type) {
        case NUM_INT8:   value.as.integer.value.i8  = (int8_t)val; break;
        case NUM_UINT8:  value.as.integer.value.u8  = (uint8_t)val; break;
        case NUM_INT16:  value.as.integer.value.i16 = (int16_t)val; break;
        case NUM_UINT16: value.as.integer.value.u16 = (uint16_t)val; break;
        case NUM_INT32:  value.as.integer.value.i32 = (int32_t)val; break;
        case NUM_UINT32: value.as.integer.value.u32 = (uint32_t)val; break;
        case NUM_INT64:  value.as.integer.value.i64 = val; break;
        case NUM_UINT64: value.as.integer.value.u64 = (uint64_t)val; break;
        default: break;
    }
    
    return value;
}

Value make_float_value(double val) {
    Value value = {0};
    value.type = VAL_FLOAT;
    value.as.float_val = val;
    return value;
}

Value make_string_value(char* string) {
    Value value = {0};
    value.type = VAL_STRING;
    value.as.string = string;  // Takes ownership of the string
    return value;
}

Value make_char_value(char val) {
    Value value = {0};
    value.type = VAL_CHAR;
    value.as.character = val;
    return value;
}

// Value utility functions
bool is_truthy(Value value) {
    switch (value.type) {
        case VAL_NIL: return false;
        case VAL_BOOL: return value.as.boolean;
        case VAL_INTEGER: {
            switch (value.as.integer.num_type) {
                case NUM_INT8:   return value.as.integer.value.i8 != 0;
                case NUM_UINT8:  return value.as.integer.value.u8 != 0;
                case NUM_INT16:  return value.as.integer.value.i16 != 0;
                case NUM_UINT16: return value.as.integer.value.u16 != 0;
                case NUM_INT32:  return value.as.integer.value.i32 != 0;
                case NUM_UINT32: return value.as.integer.value.u32 != 0;
                case NUM_INT64:  return value.as.integer.value.i64 != 0;
                case NUM_UINT64: return value.as.integer.value.u64 != 0;
                default: return false;
            }
        }
        case VAL_FLOAT: return value.as.float_val != 0.0;
        case VAL_STRING: return value.as.string != NULL && strlen(value.as.string) > 0;
        case VAL_CHAR: return value.as.character != '\0';
        default: return false;
    }
}

bool values_equal(Value a, Value b) {
    if (a.type != b.type) return false;
    
    switch (a.type) {
        case VAL_NIL: return true;
        case VAL_BOOL: return a.as.boolean == b.as.boolean;
        case VAL_INTEGER: {
            if (a.as.integer.num_type != b.as.integer.num_type) return false;
            switch (a.as.integer.num_type) {
                case NUM_INT8:   return a.as.integer.value.i8 == b.as.integer.value.i8;
                case NUM_UINT8:  return a.as.integer.value.u8 == b.as.integer.value.u8;
                case NUM_INT16:  return a.as.integer.value.i16 == b.as.integer.value.i16;
                case NUM_UINT16: return a.as.integer.value.u16 == b.as.integer.value.u16;
                case NUM_INT32:  return a.as.integer.value.i32 == b.as.integer.value.i32;
                case NUM_UINT32: return a.as.integer.value.u32 == b.as.integer.value.u32;
                case NUM_INT64:  return a.as.integer.value.i64 == b.as.integer.value.i64;
                case NUM_UINT64: return a.as.integer.value.u64 == b.as.integer.value.u64;
                default: return false;
            }
        }
        case VAL_FLOAT: return a.as.float_val == b.as.float_val;
        case VAL_STRING: 
            return (a.as.string == b.as.string) || 
                   (a.as.string && b.as.string && strcmp(a.as.string, b.as.string) == 0);
        case VAL_CHAR: return a.as.character == b.as.character;
        default: return false;
    }
}

void print_value(Value value) {
    switch (value.type) {
        case VAL_NIL:
            printf("nil");
            break;
        case VAL_BOOL:
            printf(value.as.boolean ? "true" : "false");
            break;
        case VAL_INTEGER:
            switch (value.as.integer.num_type) {
                case NUM_INT8:   printf("%d", value.as.integer.value.i8); break;
                case NUM_UINT8:  printf("%u", value.as.integer.value.u8); break;
                case NUM_INT16:  printf("%d", value.as.integer.value.i16); break;
                case NUM_UINT16: printf("%u", value.as.integer.value.u16); break;
                case NUM_INT32:  printf("%d", value.as.integer.value.i32); break;
                case NUM_UINT32: printf("%u", value.as.integer.value.u32); break;
                case NUM_INT64:  printf("%ld", value.as.integer.value.i64); break;
                case NUM_UINT64: printf("%lu", value.as.integer.value.u64); break;
                default: printf("unknown_int"); break;
            }
            break;
        case VAL_FLOAT:
            printf("%g", value.as.float_val);
            break;
        case VAL_STRING:
            printf("%s", value.as.string ? value.as.string : "(null)");
            break;
        case VAL_CHAR:
            printf("'%c'", value.as.character);
            break;
        default:
            printf("unknown_value");
            break;
    }
}

char* value_to_string(Value value) {
    char* result = NULL;
    char buffer[64];
    
    switch (value.type) {
        case VAL_NIL:
            result = malloc(4);
            if (result) strcpy(result, "nil");
            break;
        case VAL_BOOL:
            result = malloc(6);
            if (result) strcpy(result, value.as.boolean ? "true" : "false");
            break;
        case VAL_INTEGER:
            switch (value.as.integer.num_type) {
                case NUM_INT8:   snprintf(buffer, sizeof(buffer), "%d", value.as.integer.value.i8); break;
                case NUM_UINT8:  snprintf(buffer, sizeof(buffer), "%u", value.as.integer.value.u8); break;
                case NUM_INT16:  snprintf(buffer, sizeof(buffer), "%d", value.as.integer.value.i16); break;
                case NUM_UINT16: snprintf(buffer, sizeof(buffer), "%u", value.as.integer.value.u16); break;
                case NUM_INT32:  snprintf(buffer, sizeof(buffer), "%d", value.as.integer.value.i32); break;
                case NUM_UINT32: snprintf(buffer, sizeof(buffer), "%u", value.as.integer.value.u32); break;
                case NUM_INT64:  snprintf(buffer, sizeof(buffer), "%ld", value.as.integer.value.i64); break;
                case NUM_UINT64: snprintf(buffer, sizeof(buffer), "%lu", value.as.integer.value.u64); break;
                default: strcpy(buffer, "0"); break;
            }
            result = malloc(strlen(buffer) + 1);
            if (result) strcpy(result, buffer);
            break;
        case VAL_FLOAT:
            snprintf(buffer, sizeof(buffer), "%g", value.as.float_val);
            result = malloc(strlen(buffer) + 1);
            if (result) strcpy(result, buffer);
            break;
        case VAL_STRING:
            if (value.as.string) {
                result = malloc(strlen(value.as.string) + 1);
                if (result) strcpy(result, value.as.string);
            } else {
                result = malloc(7);
                if (result) strcpy(result, "(null)");
            }
            break;
        case VAL_CHAR:
            result = malloc(2);
            if (result) {
                result[0] = value.as.character;
                result[1] = '\0';
            }
            break;
        default:
            result = malloc(8);
            if (result) strcpy(result, "unknown");
            break;
    }
    
    return result;
}

const char* value_type_name(ValueType type) {
    switch (type) {
        case VAL_NIL: return "nil";
        case VAL_BOOL: return "bool";
        case VAL_INTEGER: return "integer";
        case VAL_FLOAT: return "float";
        case VAL_STRING: return "string";
        case VAL_CHAR: return "char";
        default: return "unknown";
    }
}

// Memory management
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
        case EXPR_LITERAL:
            free_value(expr->as.literal.value);
            break;
        case EXPR_VARIABLE:
            // Token doesn't own memory
            break;
        case EXPR_ASSIGNMENT:
            free_expr(expr->as.assignment.value);
            break;
        case EXPR_CALL:
            free_expr(expr->as.call.callee);
            for (size_t i = 0; i < expr->as.call.arg_count; i++) {
                free_expr(expr->as.call.arguments[i]);
            }
            free(expr->as.call.arguments);
            break;
        case EXPR_GROUPING:
            free_expr(expr->as.grouping.expression);
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
            free_expr(stmt->as.var.initializer);
            break;
        case STMT_BLOCK:
            for (size_t i = 0; i < stmt->as.block.count; i++) {
                free_stmt(stmt->as.block.statements[i]);
            }
            free(stmt->as.block.statements);
            break;
        case STMT_IF:
            free_expr(stmt->as.if_stmt.condition);
            free_stmt(stmt->as.if_stmt.then_branch);
            free_stmt(stmt->as.if_stmt.else_branch);
            break;
        case STMT_WHILE:
            free_expr(stmt->as.while_stmt.condition);
            free_stmt(stmt->as.while_stmt.body);
            break;
        case STMT_FOR:
            free_stmt(stmt->as.for_stmt.initializer);
            free_expr(stmt->as.for_stmt.condition);
            free_expr(stmt->as.for_stmt.increment);
            free_stmt(stmt->as.for_stmt.body);
            break;
        case STMT_FUNCTION:
            free(stmt->as.function.params);
            for (size_t i = 0; i < stmt->as.function.body_count; i++) {
                free_stmt(stmt->as.function.body[i]);
            }
            free(stmt->as.function.body);
            break;
        case STMT_CLASS:
            for (size_t i = 0; i < stmt->as.class.method_count; i++) {
                free_stmt((Stmt*)stmt->as.class.methods[i]);
            }
            free(stmt->as.class.methods);
            break;
        case STMT_RETURN:
            free_expr(stmt->as.return_stmt.value);
            break;
    }
    
    free(stmt);
}

void free_value(Value value) {
    if (value.type == VAL_STRING && value.as.string) {
        free(value.as.string);
        // Note: We can't set value.as.string to NULL here since value is passed by copy
        // The caller should manage this to prevent double free
    }
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
            printf(" %.*s ", expr->as.binary.operator.length, expr->as.binary.operator.start);
            print_expr(expr->as.binary.right);
            printf(")");
            break;
        case EXPR_UNARY:
            printf("(%.*s ", expr->as.unary.operator.length, expr->as.unary.operator.start);
            print_expr(expr->as.unary.right);
            printf(")");
            break;
        case EXPR_LITERAL:
            print_value(expr->as.literal.value);
            break;
        case EXPR_VARIABLE:
            printf("%.*s", expr->as.variable.name.length, expr->as.variable.name.start);
            break;
        case EXPR_ASSIGNMENT:
            printf("(%.*s = ", expr->as.assignment.name.length, expr->as.assignment.name.start);
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
            printf("var %.*s", stmt->as.var.name.length, stmt->as.var.name.start);
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
            printf("func %.*s(", stmt->as.function.name.length, stmt->as.function.name.start);
            for (size_t i = 0; i < stmt->as.function.param_count; i++) {
                if (i > 0) printf(", ");
                printf("%.*s", stmt->as.function.params[i].length, stmt->as.function.params[i].start);
            }
            printf(") { ... }");
            break;
        case STMT_CLASS:
            printf("class %.*s", stmt->as.class.name.length, stmt->as.class.name.start);
            if (stmt->as.class.superclass) {
                printf(" < %.*s", stmt->as.class.superclass->name.length, 
                       stmt->as.class.superclass->name.start);
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
