#include "evaluator.h"
#include "token.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Utility functions
EvalResult make_success(Value value) {
    EvalResult result = {0};
    result.value = value;
    result.has_error = false;
    return result;
}

EvalResult make_error(const char* message, int line, int column) {
    EvalResult result = {0};
    result.value = make_nil_value();
    result.has_error = true;
    result.error.message = message;
    result.error.line = line;
    result.error.column = column;
    return result;
}

bool is_error(EvalResult result) {
    return result.has_error;
}

void print_runtime_error(RuntimeError error) {
    fprintf(stderr, "[line %d] Runtime Error: %s\n", error.line, error.message);
}

// Expression evaluation
EvalResult eval_literal_expr(LiteralExpr* expr, Environment* env) {
    (void)env;  // Unused parameter
    return make_success(expr->value);
}

EvalResult eval_variable_expr(VariableExpr* expr, Environment* env) {
    char name[256];
    snprintf(name, sizeof(name), "%.*s", expr->name.length, expr->name.start);
    
    bool found;
    Value value = get_variable(env, name, &found);
    
    if (!found) {
        return make_error("Undefined variable", expr->name.line, expr->name.column);
    }
    
    return make_success(value);
}

EvalResult eval_assignment_expr(AssignmentExpr* expr, Environment* env) {
    EvalResult value_result = evaluate_expr(expr->value, env);
    if (is_error(value_result)) {
        return value_result;
    }
    
    char name[256];
    snprintf(name, sizeof(name), "%.*s", expr->name.length, expr->name.start);
    
    if (!assign_variable(env, name, value_result.value)) {
        return make_error("Undefined variable in assignment", expr->name.line, expr->name.column);
    }
    
    return make_success(value_result.value);
}

EvalResult eval_grouping_expr(GroupingExpr* expr, Environment* env) {
    return evaluate_expr(expr->expression, env);
}

// Arithmetic operations
EvalResult add_values(Value left, Value right) {
    // String concatenation
    if (left.type == VAL_STRING || right.type == VAL_STRING) {
        char* left_str = value_to_string(left);
        char* right_str = value_to_string(right);
        
        if (!left_str || !right_str) {
            free(left_str);
            free(right_str);
            return make_error("Memory allocation failed in string concatenation", 0, 0);
        }
        
        size_t len = strlen(left_str) + strlen(right_str) + 1;
        char* result = malloc(len);
        if (!result) {
            free(left_str);
            free(right_str);
            return make_error("Memory allocation failed", 0, 0);
        }
        
        strcpy(result, left_str);
        strcat(result, right_str);
        
        free(left_str);
        free(right_str);
        
        return make_success(make_string_value(result));
    }
    
    // Numeric addition
    if (left.type == VAL_FLOAT || right.type == VAL_FLOAT) {
        double left_val = (left.type == VAL_FLOAT) ? left.as.float_val : 0.0;
        double right_val = (right.type == VAL_FLOAT) ? right.as.float_val : 0.0;
        
        // Convert integers to double if needed
        if (left.type == VAL_INTEGER) {
            switch (left.as.integer.num_type) {
                case NUM_INT8:   left_val = left.as.integer.value.i8; break;
                case NUM_UINT8:  left_val = left.as.integer.value.u8; break;
                case NUM_INT16:  left_val = left.as.integer.value.i16; break;
                case NUM_UINT16: left_val = left.as.integer.value.u16; break;
                case NUM_INT32:  left_val = left.as.integer.value.i32; break;
                case NUM_UINT32: left_val = left.as.integer.value.u32; break;
                case NUM_INT64:  left_val = left.as.integer.value.i64; break;
                case NUM_UINT64: left_val = left.as.integer.value.u64; break;
                default: left_val = 0.0; break;
            }
        }
        
        if (right.type == VAL_INTEGER) {
            switch (right.as.integer.num_type) {
                case NUM_INT8:   right_val = right.as.integer.value.i8; break;
                case NUM_UINT8:  right_val = right.as.integer.value.u8; break;
                case NUM_INT16:  right_val = right.as.integer.value.i16; break;
                case NUM_UINT16: right_val = right.as.integer.value.u16; break;
                case NUM_INT32:  right_val = right.as.integer.value.i32; break;
                case NUM_UINT32: right_val = right.as.integer.value.u32; break;
                case NUM_INT64:  right_val = right.as.integer.value.i64; break;
                case NUM_UINT64: right_val = right.as.integer.value.u64; break;
                default: right_val = 0.0; break;
            }
        }
        
        return make_success(make_float_value(left_val + right_val));
    }
    
    // Integer addition
    if (left.type == VAL_INTEGER && right.type == VAL_INTEGER) {
        // For simplicity, promote to int64 for arithmetic
        int64_t left_val = 0, right_val = 0;
        
        switch (left.as.integer.num_type) {
            case NUM_INT8:   left_val = left.as.integer.value.i8; break;
            case NUM_UINT8:  left_val = left.as.integer.value.u8; break;
            case NUM_INT16:  left_val = left.as.integer.value.i16; break;
            case NUM_UINT16: left_val = left.as.integer.value.u16; break;
            case NUM_INT32:  left_val = left.as.integer.value.i32; break;
            case NUM_UINT32: left_val = left.as.integer.value.u32; break;
            case NUM_INT64:  left_val = left.as.integer.value.i64; break;
            case NUM_UINT64: left_val = (int64_t)left.as.integer.value.u64; break;
            default: left_val = 0; break;
        }
        
        switch (right.as.integer.num_type) {
            case NUM_INT8:   right_val = right.as.integer.value.i8; break;
            case NUM_UINT8:  right_val = right.as.integer.value.u8; break;
            case NUM_INT16:  right_val = right.as.integer.value.i16; break;
            case NUM_UINT16: right_val = right.as.integer.value.u16; break;
            case NUM_INT32:  right_val = right.as.integer.value.i32; break;
            case NUM_UINT32: right_val = right.as.integer.value.u32; break;
            case NUM_INT64:  right_val = right.as.integer.value.i64; break;
            case NUM_UINT64: right_val = (int64_t)right.as.integer.value.u64; break;
            default: right_val = 0; break;
        }
        
        return make_success(make_integer_value(NUM_INT64, left_val + right_val));
    }
    
    return make_error("Cannot add these types", 0, 0);
}

EvalResult subtract_values(Value left, Value right) {
    // Numeric subtraction only
    if (left.type == VAL_FLOAT || right.type == VAL_FLOAT) {
        double left_val = (left.type == VAL_FLOAT) ? left.as.float_val : 0.0;
        double right_val = (right.type == VAL_FLOAT) ? right.as.float_val : 0.0;
        
        // Convert integers to double if needed (similar to add_values)
        if (left.type == VAL_INTEGER) {
            switch (left.as.integer.num_type) {
                case NUM_INT32:  left_val = left.as.integer.value.i32; break;
                // ... other cases similar to add_values
                default: left_val = 0.0; break;
            }
        }
        
        if (right.type == VAL_INTEGER) {
            switch (right.as.integer.num_type) {
                case NUM_INT32:  right_val = right.as.integer.value.i32; break;
                // ... other cases similar to add_values
                default: right_val = 0.0; break;
            }
        }
        
        return make_success(make_float_value(left_val - right_val));
    }
    
    if (left.type == VAL_INTEGER && right.type == VAL_INTEGER) {
        // Simplified integer subtraction (assuming int32 for now)
        int64_t left_val = left.as.integer.value.i32;
        int64_t right_val = right.as.integer.value.i32;
        return make_success(make_integer_value(NUM_INT32, left_val - right_val));
    }
    
    return make_error("Cannot subtract these types", 0, 0);
}

EvalResult multiply_values(Value left, Value right) {
    // Similar pattern to add_values for numeric multiplication
    if (left.type == VAL_FLOAT || right.type == VAL_FLOAT) {
        double left_val = (left.type == VAL_FLOAT) ? left.as.float_val : left.as.integer.value.i32;
        double right_val = (right.type == VAL_FLOAT) ? right.as.float_val : right.as.integer.value.i32;
        return make_success(make_float_value(left_val * right_val));
    }
    
    if (left.type == VAL_INTEGER && right.type == VAL_INTEGER) {
        int64_t left_val = left.as.integer.value.i32;
        int64_t right_val = right.as.integer.value.i32;
        return make_success(make_integer_value(NUM_INT32, left_val * right_val));
    }
    
    return make_error("Cannot multiply these types", 0, 0);
}

EvalResult divide_values(Value left, Value right) {
    // Division always returns float to handle fractions
    double left_val = (left.type == VAL_FLOAT) ? left.as.float_val : 
                      (left.type == VAL_INTEGER) ? left.as.integer.value.i32 : 0.0;
    double right_val = (right.type == VAL_FLOAT) ? right.as.float_val : 
                       (right.type == VAL_INTEGER) ? right.as.integer.value.i32 : 0.0;
    
    if (right_val == 0.0) {
        return make_error("Division by zero", 0, 0);
    }
    
    return make_success(make_float_value(left_val / right_val));
}

EvalResult compare_values(Value left, Value right, TokenType operator) {
    bool result = false;
    
    // Equality comparison
    if (operator == TOKEN_EQUAL_EQUAL) {
        result = values_equal(left, right);
    } else if (operator == TOKEN_BANG_EQUAL) {
        result = !values_equal(left, right);
    } else {
        // Numeric comparison
        double left_val = 0.0, right_val = 0.0;
        
        if (left.type == VAL_FLOAT) {
            left_val = left.as.float_val;
        } else if (left.type == VAL_INTEGER) {
            left_val = left.as.integer.value.i32; // Simplified
        } else {
            return make_error("Cannot compare non-numeric types", 0, 0);
        }
        
        if (right.type == VAL_FLOAT) {
            right_val = right.as.float_val;
        } else if (right.type == VAL_INTEGER) {
            right_val = right.as.integer.value.i32; // Simplified
        } else {
            return make_error("Cannot compare non-numeric types", 0, 0);
        }
        
        switch (operator) {
            case TOKEN_GREATER:       result = left_val > right_val; break;
            case TOKEN_GREATER_EQUAL: result = left_val >= right_val; break;
            case TOKEN_LESS:          result = left_val < right_val; break;
            case TOKEN_LESS_EQUAL:    result = left_val <= right_val; break;
            default:
                return make_error("Unknown comparison operator", 0, 0);
        }
    }
    
    return make_success(make_bool_value(result));
}

EvalResult eval_binary_expr(BinaryExpr* expr, Environment* env) {
    EvalResult left_result = evaluate_expr(expr->left, env);
    if (is_error(left_result)) {
        return left_result;
    }
    
    EvalResult right_result = evaluate_expr(expr->right, env);
    if (is_error(right_result)) {
        return right_result;
    }
    
    switch (expr->operator.type) {
        case TOKEN_PLUS:
            return add_values(left_result.value, right_result.value);
        case TOKEN_MINUS:
            return subtract_values(left_result.value, right_result.value);
        case TOKEN_STAR:
            return multiply_values(left_result.value, right_result.value);
        case TOKEN_SLASH:
            return divide_values(left_result.value, right_result.value);
        case TOKEN_EQUAL_EQUAL:
        case TOKEN_BANG_EQUAL:
        case TOKEN_GREATER:
        case TOKEN_GREATER_EQUAL:
        case TOKEN_LESS:
        case TOKEN_LESS_EQUAL:
            return compare_values(left_result.value, right_result.value, expr->operator.type);
        case TOKEN_AND:
        case TOKEN_AND_AND:
            if (!is_truthy(left_result.value)) {
                return make_success(left_result.value);
            }
            return make_success(right_result.value);
        case TOKEN_OR:
        case TOKEN_OR_OR:
            if (is_truthy(left_result.value)) {
                return make_success(left_result.value);
            }
            return make_success(right_result.value);
        default:
            return make_error("Unknown binary operator", expr->operator.line, expr->operator.column);
    }
}

EvalResult eval_unary_expr(UnaryExpr* expr, Environment* env) {
    EvalResult operand_result = evaluate_expr(expr->right, env);
    if (is_error(operand_result)) {
        return operand_result;
    }
    
    switch (expr->operator.type) {
        case TOKEN_MINUS:
            if (operand_result.value.type == VAL_FLOAT) {
                return make_success(make_float_value(-operand_result.value.as.float_val));
            } else if (operand_result.value.type == VAL_INTEGER) {
                return make_success(make_integer_value(NUM_INT32, -operand_result.value.as.integer.value.i32));
            } else {
                return make_error("Cannot negate non-numeric value", expr->operator.line, expr->operator.column);
            }
        case TOKEN_BANG:
        case TOKEN_NOT:
            return make_success(make_bool_value(!is_truthy(operand_result.value)));
        default:
            return make_error("Unknown unary operator", expr->operator.line, expr->operator.column);
    }
}

// Main expression evaluator
EvalResult evaluate_expr(Expr* expr, Environment* env) {
    if (!expr) {
        return make_error("Null expression", 0, 0);
    }
    
    switch (expr->type) {
        case EXPR_LITERAL:
            return eval_literal_expr(&expr->as.literal, env);
        case EXPR_VARIABLE:
            return eval_variable_expr(&expr->as.variable, env);
        case EXPR_ASSIGNMENT:
            return eval_assignment_expr(&expr->as.assignment, env);
        case EXPR_BINARY:
            return eval_binary_expr(&expr->as.binary, env);
        case EXPR_UNARY:
            return eval_unary_expr(&expr->as.unary, env);
        case EXPR_GROUPING:
            return eval_grouping_expr(&expr->as.grouping, env);
        case EXPR_CALL:
            return eval_call_expr(&expr->as.call, env);
        default:
            return make_error("Unknown expression type", 0, 0);
    }
}

// Statement evaluation
EvalResult eval_expression_stmt(ExpressionStmt* stmt, Environment* env) {
    return evaluate_expr(stmt->expression, env);
}

EvalResult eval_var_stmt(VarStmt* stmt, Environment* env) {
    Value value = make_nil_value();
    
    if (stmt->initializer) {
        EvalResult init_result = evaluate_expr(stmt->initializer, env);
        if (is_error(init_result)) {
            return init_result;
        }
        value = init_result.value;
    }
    
    char name[256];
    snprintf(name, sizeof(name), "%.*s", stmt->name.length, stmt->name.start);
    define_variable(env, name, value);
    
    return make_success(make_nil_value());
}

EvalResult eval_block_stmt(BlockStmt* stmt, Environment* env) {
    Environment* block_env = create_environment(env);
    if (!block_env) {
        return make_error("Memory allocation failed", 0, 0);
    }
    
    EvalResult result = make_success(make_nil_value());
    
    for (size_t i = 0; i < stmt->count; i++) {
        result = evaluate_stmt(stmt->statements[i], block_env);
        if (is_error(result)) {
            break;
        }
    }
    
    free_environment(block_env);
    return result;
}

EvalResult eval_if_stmt(IfStmt* stmt, Environment* env) {
    EvalResult condition_result = evaluate_expr(stmt->condition, env);
    if (is_error(condition_result)) {
        return condition_result;
    }
    
    if (is_truthy(condition_result.value)) {
        return evaluate_stmt(stmt->then_branch, env);
    } else if (stmt->else_branch) {
        return evaluate_stmt(stmt->else_branch, env);
    }
    
    return make_success(make_nil_value());
}

EvalResult eval_while_stmt(WhileStmt* stmt, Environment* env) {
    EvalResult result = make_success(make_nil_value());
    
    while (true) {
        EvalResult condition_result = evaluate_expr(stmt->condition, env);
        if (is_error(condition_result)) {
            return condition_result;
        }
        
        if (!is_truthy(condition_result.value)) {
            break;
        }
        
        result = evaluate_stmt(stmt->body, env);
        if (is_error(result)) {
            return result;
        }
    }
    
    return result;
}

// Built-in functions
EvalResult builtin_print(Value* args, size_t arg_count) {
    for (size_t i = 0; i < arg_count; i++) {
        if (i > 0) printf(" ");
        print_value(args[i]);
    }
    printf("\n");
    return make_success(make_nil_value());
}

EvalResult builtin_typeof(Value* args, size_t arg_count) {
    if (arg_count != 1) {
        return make_error("typeof() expects exactly 1 argument", 0, 0);
    }
    
    const char* type_name = value_type_name(args[0].type);
    char* result = malloc(strlen(type_name) + 1);
    if (result) {
        strcpy(result, type_name);
    }
    return make_success(make_string_value(result));
}

EvalResult builtin_str(Value* args, size_t arg_count) {
    if (arg_count != 1) {
        return make_error("str() expects exactly 1 argument", 0, 0);
    }
    
    char* str = value_to_string(args[0]);
    return make_success(make_string_value(str));
}

EvalResult builtin_int(Value* args, size_t arg_count) {
    if (arg_count != 1) {
        return make_error("int() expects exactly 1 argument", 0, 0);
    }
    
    Value arg = args[0];
    switch (arg.type) {
        case VAL_INTEGER:
            return make_success(arg);  // Already an integer
        case VAL_FLOAT:
            return make_success(make_integer_value(NUM_INT32, (int64_t)arg.as.float_val));
        case VAL_STRING: {
            if (arg.as.string) {
                char* endptr;
                long val = strtol(arg.as.string, &endptr, 10);
                if (*endptr == '\0') {  // Successful conversion
                    return make_success(make_integer_value(NUM_INT32, val));
                }
            }
            return make_error("Cannot convert string to integer", 0, 0);
        }
        case VAL_BOOL:
            return make_success(make_integer_value(NUM_INT32, arg.as.boolean ? 1 : 0));
        default:
            return make_error("Cannot convert value to integer", 0, 0);
    }
}

EvalResult builtin_float(Value* args, size_t arg_count) {
    if (arg_count != 1) {
        return make_error("float() expects exactly 1 argument", 0, 0);
    }
    
    Value arg = args[0];
    switch (arg.type) {
        case VAL_FLOAT:
            return make_success(arg);  // Already a float
        case VAL_INTEGER: {
            double val = 0.0;
            switch (arg.as.integer.num_type) {
                case NUM_INT8:   val = arg.as.integer.value.i8; break;
                case NUM_UINT8:  val = arg.as.integer.value.u8; break;
                case NUM_INT16:  val = arg.as.integer.value.i16; break;
                case NUM_UINT16: val = arg.as.integer.value.u16; break;
                case NUM_INT32:  val = arg.as.integer.value.i32; break;
                case NUM_UINT32: val = arg.as.integer.value.u32; break;
                case NUM_INT64:  val = arg.as.integer.value.i64; break;
                case NUM_UINT64: val = arg.as.integer.value.u64; break;
                default: val = 0.0; break;
            }
            return make_success(make_float_value(val));
        }
        case VAL_STRING: {
            if (arg.as.string) {
                char* endptr;
                double val = strtod(arg.as.string, &endptr);
                if (*endptr == '\0') {  // Successful conversion
                    return make_success(make_float_value(val));
                }
            }
            return make_error("Cannot convert string to float", 0, 0);
        }
        default:
            return make_error("Cannot convert value to float", 0, 0);
    }
}

// Built-in function lookup table
static const BuiltinEntry builtins[] = {
    {"print", builtin_print, SIZE_MAX},    // Variadic
    {"typeof", builtin_typeof, 1},
    {"str", builtin_str, 1},
    {"int", builtin_int, 1},
    {"float", builtin_float, 1},
};

static const size_t builtin_count = sizeof(builtins) / sizeof(builtins[0]);

BuiltinFunction lookup_builtin(const char* name) {
    for (size_t i = 0; i < builtin_count; i++) {
        if (strcmp(builtins[i].name, name) == 0) {
            return builtins[i].function;
        }
    }
    return NULL;
}

void register_builtins(Environment* env) {
    // Built-ins are looked up dynamically, so we don't need to register them
    // in the environment. They're handled specially in eval_call_expr.
    (void)env;
}

EvalResult eval_call_expr(CallExpr* expr, Environment* env) {
    // For now, only support built-in function calls
    if (expr->callee->type != EXPR_VARIABLE) {
        return make_error("Only built-in function calls supported", 0, 0);
    }
    
    VariableExpr* var_expr = &expr->callee->as.variable;
    char name[256];
    snprintf(name, sizeof(name), "%.*s", var_expr->name.length, var_expr->name.start);
    
    BuiltinFunction builtin = lookup_builtin(name);
    if (!builtin) {
        return make_error("Unknown function", var_expr->name.line, var_expr->name.column);
    }
    
    // Evaluate arguments
    Value* args = NULL;
    if (expr->arg_count > 0) {
        args = malloc(expr->arg_count * sizeof(Value));
        if (!args) {
            return make_error("Memory allocation failed", 0, 0);
        }
        
        for (size_t i = 0; i < expr->arg_count; i++) {
            EvalResult arg_result = evaluate_expr(expr->arguments[i], env);
            if (is_error(arg_result)) {
                free(args);
                return arg_result;
            }
            args[i] = arg_result.value;
        }
    }
    
    // Call the built-in function
    EvalResult result = builtin(args, expr->arg_count);
    
    free(args);
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
        default:
            return make_error("Unknown statement type", 0, 0);
    }
}

EvalResult eval_for_stmt(ForStmt* stmt, Environment* env) {
    // Create new environment for the for loop scope
    Environment* for_env = create_environment(env);
    if (!for_env) {
        return make_error("Memory allocation failed", 0, 0);
    }
    
    EvalResult result = make_success(make_nil_value());
    
    // Execute initializer
    if (stmt->initializer) {
        result = evaluate_stmt(stmt->initializer, for_env);
        if (is_error(result)) {
            free_environment(for_env);
            return result;
        }
    }
    
    // Loop
    while (true) {
        // Check condition
        if (stmt->condition) {
            EvalResult condition_result = evaluate_expr(stmt->condition, for_env);
            if (is_error(condition_result)) {
                free_environment(for_env);
                return condition_result;
            }
            
            if (!is_truthy(condition_result.value)) {
                break;
            }
        }
        
        // Execute body
        result = evaluate_stmt(stmt->body, for_env);
        if (is_error(result)) {
            free_environment(for_env);
            return result;
        }
        
        // Execute increment
        if (stmt->increment) {
            EvalResult increment_result = evaluate_expr(stmt->increment, for_env);
            if (is_error(increment_result)) {
                free_environment(for_env);
                return increment_result;
            }
        }
    }
    
    free_environment(for_env);
    return result;
}

// Evaluate a program (array of statements)
EvalResult evaluate_program(Stmt** statements, size_t count, Environment* env) {
    EvalResult result = make_success(make_nil_value());
    
    for (size_t i = 0; i < count; i++) {
        result = evaluate_stmt(statements[i], env);
        if (is_error(result)) {
            print_runtime_error(result.error);
            break;
        }
    }
    
    return result;
}
