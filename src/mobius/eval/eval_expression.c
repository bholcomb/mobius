#include "eval/evaluator.h"
#include "plugin/module_registry.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Call a user-defined function
EvalResult call_user_function(MobiusFunction* function, Expr** arguments, size_t arg_count, Environment* env) {
    // Check argument count
    if (arg_count != function->param_count) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), 
                "Function '%s' expects %zu arguments but got %zu",
                function->name ? function->name : "anonymous",
                function->param_count, arg_count);
                
        return make_error_detailed(
            error_msg,
            "Check the function definition for the correct number of parameters",
            ERROR_ARGUMENT,
            0, 0,
            function->name,
            NULL
        );
    }
    
    // Push function call onto stack trace
    stack_trace_push(function->name ? function->name : "anonymous", 
                     NULL, // filename - TODO: add filename tracking
                     0, 0, // line, column - TODO: add call site tracking
                     false, false, NULL); // not builtin, not plugin
    
    // Create new environment for function execution (with closure as parent)
    Environment* func_env = create_environment(function->closure);
    
    // Evaluate and bind arguments to parameters using stack-based evaluation
    for (size_t i = 0; i < arg_count; i++) {
        EvalResult arg_result = evaluate_expr(arguments[i], env);
        if (is_error(arg_result)) {
            // Clean up any arguments already on stack
            for (size_t j = 0; j < i; j++) {
                ctx_pop(global_context);
            }
            stack_trace_pop();
            free_environment(func_env);
            return arg_result;
        }
        // Argument is now on stack
    }
    
    // Pop arguments from stack and bind to parameters
    // Pop in reverse order since stack is LIFO
    for (size_t i = arg_count; i > 0; i--) {
        Value arg_value = ctx_pop(global_context);
        define_variable(func_env, function->param_names[i-1], arg_value);
    }
    
    // Execute function body
    EvalResult result = make_success(0);
    
    for (size_t i = 0; i < function->body_count; i++) {
        result = evaluate_stmt(function->body[i], func_env);
        if (is_error(result)) {
            stack_trace_pop();
            free_environment(func_env);
            return result;
        }
        
        // Check if a return statement was executed (including nested in control structures)
        if (result.has_returned) {
            break;  // Return statement found, exit function body execution
        }
    }
    
    // Pop function call from stack trace
    stack_trace_pop();
    
    // Return value is already on func_env's stack (if any)
    // Transfer it to the caller's stack
    if (!is_error(result) && result.has_returned && result.return_count > 0) {
        // Return value is already on global stack from evaluate_expr
    free_environment(func_env);
        return make_success(1);  // Indicate 1 value on stack
    } else {
        free_environment(func_env);
        return make_success(0);  // No return value
    }
}


// Expression evaluation
EvalResult eval_literal_expr(LiteralExpr* expr, Environment* env) {
    (void)env;
    return make_success_with_value(copy_value(expr->value));
}

// Stack-based function call evaluation
EvalResult eval_call_expr(CallExpr* expr, Environment* env) {
    char full_name[256];
    int call_line = 0;
    int call_column = 0;
    
    // Handle module.function() syntax (TABLE_DOT expression)
    if (expr->callee->type == EXPR_TABLE_DOT) {
        TableDotExpr* dot_expr = &expr->callee->as.table_dot;
        
        // Check if the table part is a module name (VARIABLE expression)
        if (dot_expr->table->type == EXPR_VARIABLE) {
            const char* table_name = dot_expr->table->as.variable.name.identifier;
            const char* func_name = dot_expr->key.identifier;
            
            // Look up the table (module) in the environment
            // Modules must be imported first, which creates a table with functions
            bool found = false;
            Value table_value = get_variable(env, table_name, &found);
            if (found && table_value.type == VAL_TABLE) {
                Value func_key = make_string_value_from_cstr(func_name);
                Value func_value = table_get(table_value.as.table, func_key);
                free_value(func_key);
                
                // If we found a native function in the table, call it
                if (func_value.type == VAL_NATIVE_FUNCTION && func_value.as.native_function) {
                    // Evaluate arguments onto stack
                    for (size_t i = 0; i < expr->arg_count; i++) {
                        EvalResult arg_result = evaluate_expr(expr->arguments[i], env);
                        if (is_error(arg_result)) {
                            // Clean up any arguments already on stack
                            for (size_t j = 0; j < i; j++) {
                                ctx_pop(global_context);
                            }
                            return arg_result;
                        }
                    }
                    
                    // Call the native function
                    stack_trace_push(func_name, NULL, dot_expr->key.line, dot_expr->key.column, true, false, NULL);
                    MobiusCFunction native_func = func_value.as.native_function;
                    global_context->env = env;  // Set current environment in context
                    EvalResult result = native_func(global_context, expr->arg_count);
                    stack_trace_pop();
                    
                    return result;
                } else if (func_value.type == VAL_FUNCTION) {
                    // User-defined function in table
                    return call_user_function(func_value.as.function, expr->arguments, expr->arg_count, env);
                } else if (func_value.type != VAL_NIL) {
                    // Found something but it's not a function
                    char error_msg[256];
                    snprintf(error_msg, sizeof(error_msg), 
                        "'%s.%s' is not a function (type: %s)", 
                        table_name, func_name, value_type_name(func_value.type));
                    return make_error(error_msg, dot_expr->key.line, dot_expr->key.column);
                }
                
                // Function not found in table
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg), 
                    "Function '%s' not found in '%s'", func_name, table_name);
                return make_error(error_msg, dot_expr->key.line, dot_expr->key.column);
            }
            
            // Table not found or not a table
            if (found) {
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg), 
                    "'%s' is not a table or module (type: %s)", 
                    table_name, value_type_name(table_value.type));
                return make_error(error_msg, dot_expr->key.line, dot_expr->key.column);
            } else {
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg), 
                    "Undefined module or table '%s'. Did you forget to import it?", table_name);
                return make_error(error_msg, dot_expr->key.line, dot_expr->key.column);
            }
        } else {
            // Handle nested table access like math.trig.sin()
            // Evaluate the table part first (e.g., math.trig)
            EvalResult table_result = evaluate_expr(dot_expr->table, env);
            if (is_error(table_result)) {
                return table_result;
            }
            
            // Pop the table from the stack
            if (table_result.return_count > 0) {
                Value table_value = ctx_pop(global_context);
                
                if (table_value.type != VAL_TABLE) {
                    free_value(table_value);
                    return make_error("Cannot call method on non-table value", 
                        dot_expr->key.line, dot_expr->key.column);
                }
                
                // Look up the function in the table
                const char* func_name = dot_expr->key.identifier;
                Value func_key = make_string_value_from_cstr(func_name);
                Value func_value = table_get(table_value.as.table, func_key);
                free_value(func_key);
                free_value(table_value);
                
                // If we found a native function, call it
                if (func_value.type == VAL_NATIVE_FUNCTION && func_value.as.native_function) {
                    // Evaluate arguments onto stack
                    for (size_t i = 0; i < expr->arg_count; i++) {
                        EvalResult arg_result = evaluate_expr(expr->arguments[i], env);
                        if (is_error(arg_result)) {
                            // Clean up any arguments already on stack
                            for (size_t j = 0; j < i; j++) {
                                ctx_pop(global_context);
                            }
                            return arg_result;
                        }
                    }
                    
                    // Call the native function directly
                    stack_trace_push(func_name, NULL, dot_expr->key.line, dot_expr->key.column, true, false, NULL);
                    MobiusCFunction native_func = func_value.as.native_function;
                    global_context->env = env;  // Set current environment in context
                    EvalResult result = native_func(global_context, expr->arg_count);
                    stack_trace_pop();
                    
                    return result;
                } else if (func_value.type == VAL_FUNCTION) {
                    // User-defined function - call it
                    return call_user_function(func_value.as.function, expr->arguments, expr->arg_count, env);
                } else {
                    char error_msg[256];
                    snprintf(error_msg, sizeof(error_msg), 
                        "'%s' is not a function (type: %s)", 
                        func_name, value_type_name(func_value.type));
                    return make_error(error_msg, dot_expr->key.line, dot_expr->key.column);
                }
            }
            
            return make_error("Table expression did not return a value", 
                dot_expr->key.line, dot_expr->key.column);
        }
    }
    // Handle simple function() or qualified_name() syntax  
    else if (expr->callee->type == EXPR_VARIABLE) {
    VariableExpr* var_expr = &expr->callee->as.variable;
    const char* identifier = var_expr->name.identifier ? var_expr->name.identifier : "unknown";
    snprintf(full_name, sizeof(full_name), "%s", identifier);
        call_line = var_expr->name.line;
        call_column = var_expr->name.column;
    }
    else {
        return make_error("Only variable and module.function calls supported", 0, 0);
    }
    
    // All function calls go through the environment
    // Module functions must be imported first using: import "module_name";
    // Then called via: module_name.function() which is handled as TABLE_DOT above
    
    // Check if it's a user-defined function or native function in the environment
    bool found = false;
    Value func_value = get_variable(env, full_name, &found);
    if (found) {
        if (func_value.type == VAL_FUNCTION && func_value.as.function) {
            // Call user-defined function
            return call_user_function(func_value.as.function, expr->arguments, expr->arg_count, env);
        } else if (func_value.type == VAL_NATIVE_FUNCTION && func_value.as.native_function) {
            // Call native function (stdlib or imported from module)
            // Evaluate arguments onto stack
            for (size_t i = 0; i < expr->arg_count; i++) {
                EvalResult arg_result = evaluate_expr(expr->arguments[i], env);
                if (is_error(arg_result)) {
                    // Clean up any arguments already on stack
                    for (size_t j = 0; j < i; j++) {
                        ctx_pop(global_context);
                    }
                    return arg_result;
                }
            }
            
            // Call the native function
            stack_trace_push(full_name, NULL, call_line, call_column, true, false, NULL);
            MobiusCFunction native_func = func_value.as.native_function;
            global_context->env = env;
            EvalResult result = native_func(global_context, expr->arg_count);
            stack_trace_pop();
            
            return result;
        }
    }
    
    // Function not found - provide helpful error message
    // Check if this function exists in any loaded module (to suggest import)
    char suggested_import[256] = {0};
    ModuleRegistry* reg = get_global_module_registry();
    if (reg) {
        for (size_t i = 0; i < reg->function_count; i++) {
            const char* qualified = reg->function_table[i].qualified_name;
            // Check if the qualified name ends with our function name
            size_t qual_len = strlen(qualified);
            size_t func_len = strlen(full_name);
            if (qual_len > func_len && 
                qualified[qual_len - func_len - 1] == '.' &&
                strcmp(qualified + qual_len - func_len, full_name) == 0) {
                // Extract module name
                size_t module_len = qual_len - func_len - 1;
                strncpy(suggested_import, qualified, module_len);
                suggested_import[module_len] = '\0';
                break;
            }
        }
    }
    
    char error_msg[512];
    char suggestion[512];
    
    if (suggested_import[0]) {
        // Function exists in a module - suggest import
        snprintf(error_msg, sizeof(error_msg), 
                "Function '%s' requires importing module first", full_name);
        snprintf(suggestion, sizeof(suggestion),
                "Add: import \"%s\"; then call as: %s.%s()", 
                suggested_import, suggested_import, full_name);
    } else {
        // Function truly doesn't exist
        snprintf(error_msg, sizeof(error_msg), "Unknown function '%s'", full_name);
        snprintf(suggestion, sizeof(suggestion),
                "Check the function name spelling or make sure it's declared before use");
    }
    
    return make_error_detailed(
        error_msg,
        suggestion,
        ERROR_UNDEFINED,
        call_line,
        call_column,
        NULL,
        NULL
    );
}

EvalResult eval_variable_expr(VariableExpr* expr, Environment* env) {
    // Use the extracted identifier string from the token's identifier field
    const char* name = expr->name.identifier ? expr->name.identifier : "unknown";
    
    bool found;
    Value value = get_variable(env, name, &found);
    
    if (!found) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Undefined variable '%s'", name);
        
        return make_error_detailed(
            error_msg,
            "Make sure the variable is declared before use",
            ERROR_UNDEFINED,
            0, 0,  // No line/column info available
            name,
            NULL
        );
    }
    
    return make_success_with_value(value);
}

EvalResult eval_assignment_expr(AssignmentExpr* expr, Environment* env) {
    EvalResult value_result = evaluate_expr(expr->value, env);
    if (is_error(value_result)) {
        return value_result;
    }
    Value value = ctx_pop(global_context);
    
    char name[256];
    const char* identifier = expr->name.identifier ? expr->name.identifier : "unknown";
    snprintf(name, sizeof(name), "%s", identifier);
    
    if (!assign_variable(env, name, value)) {
        free_value(value);
        return make_error_detailed_with_source("Undefined variable in assignment", 
                                               "Make sure the variable is declared before use",
                                               ERROR_UNDEFINED, expr->name.line, expr->name.column, NULL);
    }
    
    return make_success_with_value(value);
}

EvalResult eval_grouping_expr(GroupingExpr* expr, Environment* env) {
    return evaluate_expr(expr->expression, env);
}

EvalResult eval_binary_expr(BinaryExpr* expr, Environment* env) {
    EvalResult left_result = evaluate_expr(expr->left, env);
    if (is_error(left_result)) {
        return left_result;
    }
    Value left = ctx_pop(global_context);
    
    EvalResult right_result = evaluate_expr(expr->right, env);
    if (is_error(right_result)) {
        free_value(left);  // Clean up left operand
        return right_result;
    }
    Value right = ctx_pop(global_context);
    
    switch (expr->op.type) {
        case TOKEN_PLUS:
            return add_values(env, left, right);
        case TOKEN_MINUS:
            return subtract_values(env, left, right);
        case TOKEN_STAR:
            return multiply_values(env, left, right);
        case TOKEN_SLASH:
                return divide_values(env, left, right, expr->op.line, expr->op.column);
        case TOKEN_PERCENT:
                return modulo_values(env, left, right, expr->op.line, expr->op.column);
        case TOKEN_EQUAL_EQUAL:
        case TOKEN_BANG_EQUAL:
        case TOKEN_GREATER:
        case TOKEN_GREATER_EQUAL:
        case TOKEN_LESS:
        case TOKEN_LESS_EQUAL:
            return compare_values(env, left, right, expr->op.type);
        case TOKEN_AND:
        case TOKEN_AND_AND:
            if (!is_truthy(left)) {
                free_value(right);  // Clean up unused value
                return make_success_with_value(left);
            }
            free_value(left);  // Clean up unused value
            return make_success_with_value(right);
        case TOKEN_OR:
        case TOKEN_OR_OR:
            if (is_truthy(left)) {
                free_value(right);  // Clean up unused value
                return make_success_with_value(left);
            }
            free_value(left);  // Clean up unused value
            return make_success_with_value(right);
        default:
            free_value(left);
            free_value(right);
            return make_error_with_source("Unknown binary operator", expr->op.line, expr->op.column);
    }
}

EvalResult eval_unary_expr(UnaryExpr* expr, Environment* env) {
    EvalResult operand_result = evaluate_expr(expr->right, env);
    if (is_error(operand_result)) {
        return operand_result;
    }
    Value operand = ctx_pop(global_context);
    
    switch (expr->op.type) {
        case TOKEN_MINUS:
            if (operand.type == VAL_FLOAT64) {
                return make_success_with_value(make_float_value(-operand.as.float64_val));
            } else if (operand.type == VAL_INTEGER) {
                // Extract value properly based on the actual type, but result should be int64_t
                int64_t value = 0;
                switch (operand.as.integer.num_type) {
                    case NUM_INT8:   value = operand.as.integer.value.i8; break;
                    case NUM_UINT8:  value = operand.as.integer.value.u8; break;
                    case NUM_INT16:  value = operand.as.integer.value.i16; break;
                    case NUM_UINT16: value = operand.as.integer.value.u16; break;
                    case NUM_INT32:  value = operand.as.integer.value.i32; break;
                    case NUM_UINT32: value = operand.as.integer.value.u32; break;
                    case NUM_INT64:  value = operand.as.integer.value.i64; break;
                    case NUM_UINT64: value = operand.as.integer.value.u64; break;
                    default: value = operand.as.integer.value.i32; break;
                }
                free_value(operand);
                return make_success_with_value(make_integer_value(NUM_INT64, -value));
            } else {
                free_value(operand);
                return make_error("Cannot negate non-numeric value", expr->op.line, expr->op.column);
            }
        case TOKEN_PLUS:
            // Unary plus is identity for numbers
            if (operand.type == VAL_FLOAT64 || operand.type == VAL_INTEGER) {
                return make_success_with_value(operand);
            } else {
                free_value(operand);
                return make_error("Cannot apply unary plus to non-numeric value", expr->op.line, expr->op.column);
            }
        case TOKEN_BANG:
        case TOKEN_NOT: {
            bool truthy = is_truthy(operand);
            free_value(operand);
            return make_success_with_value(make_bool_value(!truthy));
        }
        default:
            free_value(operand);
            return make_error("Unknown unary operator", expr->op.line, expr->op.column);
    }
}