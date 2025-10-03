#include "eval/evaluator.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Evaluate a program (array of statements)
EvalResult evaluate_program(Stmt** statements, size_t count, Environment* env) {
    EvalResult result = make_success(0);
    
    for (size_t i = 0; i < count; i++) {
        result = evaluate_stmt(statements[i], env);
        if (is_error(result)) {
            print_runtime_error(result.error);
            // Continue execution instead of breaking for better error recovery
            // In a real language, you might want to break on certain error types
            result = make_success_with_value(make_nil_value()); // Reset result for next statement
        }
    }
    
    return result;
}

// Main statement evaluator
EvalResult evaluate_stmt(Stmt* stmt, Environment* env) {
    if (!stmt) {
        return make_error("Null statement", 0, 0);
    }
    
    switch (stmt->type) {
        case STMT_EXPRESSION:
            return eval_expression_stmt(&stmt->as.expression, env);
        case STMT_VAR:
            return eval_var_stmt(&stmt->as.var, env);
        case STMT_BLOCK:
            return eval_block_stmt(&stmt->as.block, env);
        case STMT_IF:
            return eval_if_stmt(&stmt->as.if_stmt, env);
        case STMT_WHILE:
            return eval_while_stmt(&stmt->as.while_stmt, env);
        case STMT_FOR:
            return eval_for_stmt(&stmt->as.for_stmt, env);
        case STMT_FUNCTION:
            return eval_function_stmt(&stmt->as.function, env);
        case STMT_RETURN:
            return eval_return_stmt(&stmt->as.return_stmt, env);
        case STMT_SWITCH:
            return eval_switch_stmt(&stmt->as.switch_stmt, env);
        case STMT_BREAK:
            return eval_break_stmt(&stmt->as.break_stmt, env);
        case STMT_CONTINUE:
            return eval_continue_stmt(&stmt->as.continue_stmt, env);
        case STMT_IMPORT:
            return eval_import_stmt(&stmt->as.import_stmt, env);
        case STMT_PRAGMA:
            return eval_pragma_stmt(&stmt->as.pragma_stmt, env);
        case STMT_ENUM:
            return eval_enum_stmt(&stmt->as.enum_stmt, env);
        default:
            return make_error("Unknown statement type", 0, 0);
    }
}


// Main expression evaluator (stack-based)
EvalResult evaluate_expr(Expr* expr, Environment* env) {
    if (!expr) {
        return make_error("Null expression", 0, 0);
    }
    
    switch (expr->type) {
        case EXPR_LITERAL:
            return eval_literal_expr(&expr->as.literal, env);
        case EXPR_VARIABLE:
            return eval_variable_expr(&expr->as.variable, env);
        case EXPR_BINARY:
            return eval_binary_expr(&expr->as.binary, env);
        case EXPR_UNARY:
            return eval_unary_expr(&expr->as.unary, env);
        case EXPR_ASSIGNMENT:
            return eval_assignment_expr(&expr->as.assignment, env);
        case EXPR_GROUPING:
            return eval_grouping_expr(&expr->as.grouping, env);
        case EXPR_CALL:
            return eval_call_expr(&expr->as.call, env);
        case EXPR_TABLE_LITERAL:
            return eval_table_literal_expr(&expr->as.table_literal, env);
        case EXPR_TABLE_INDEX:
            return eval_table_index_expr(&expr->as.table_index, env);
        case EXPR_TABLE_DOT:
            return eval_table_dot_expr(&expr->as.table_dot, env);
        case EXPR_ARRAY_LITERAL:
            return eval_array_literal_expr(&expr->as.array_literal, env);
        case EXPR_ARRAY_INDEX:
            return eval_array_index_expr(&expr->as.array_index, env);
        case EXPR_ENUM_ACCESS:
            return eval_enum_access_expr(&expr->as.enum_access, env);
        case EXPR_INCREMENT:
        case EXPR_DECREMENT:
            return eval_increment_expr(&expr->as.increment, env);
        default:
            return make_error("Unknown expression type", 0, 0);
    }
}