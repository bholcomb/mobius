#include "eval/evaluator.h"
#include "state/mobius_state.h"
#include "data/table.h"
#include "plugin/module_registry.h"

#include <new>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static EvalResult eval_call_native_func(MobiusCFunction func, MobiusState* state,
                                        int arg_count, Environment* env,
                                        const char* name, int line, int column) {
    env->current_context->pushFrame(name, NULL, line, column,
                     FUNCTION_TYPE_NATIVE, NULL, NULL);
    int rc = func(state, arg_count);
    env->current_context->popFrame();

    if (rc >= 0) {
        return make_success(rc);
    }
    if (state->lastError()) {
        return make_error(env, state->lastError()->message, line, column);
    }
    return make_error(env, "native function error", line, column);
}

// Call a user-defined function
EvalResult call_user_function(MobiusFunction* function, Expr** arguments, size_t arg_count, Environment* env) {
    const char* fname = function->name ? function->name->data : "anonymous";
    // Check argument count
    if (arg_count != function->param_count) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), 
                "Function '%s' expects %zu arguments but got %zu",
                fname, function->param_count, arg_count);
                
        return make_error_detailed(
            env,
            error_msg,
            "Check the function definition for the correct number of parameters",
            ERROR_ARGUMENT,
            0, 0,
            fname,
            NULL
        );
    }
    
    // Push function call onto stack trace
    env->current_context->pushFrame(fname,
                     NULL, // filename - TODO: add filename tracking
                     0, 0, // line, column - TODO: add call site tracking
                     FUNCTION_TYPE_SCRIPT, NULL, NULL);
    
    // Check for stack overflow
    if (env->current_context->isStackOverflow()) {
        return make_error(env, "Stack overflow: maximum call depth exceeded", 0, 0);
    }
    
    // Create new environment for function execution (with closure as parent)
    ExecutionContext* ctx = function->closure ? function->closure->current_context : env->current_context;
    Environment* func_env = new (std::nothrow) Environment(function->closure, ctx);
    
    // Evaluate and bind arguments to parameters using stack-based evaluation
    for (size_t i = 0; i < arg_count; i++) {
        EvalResult arg_result = evaluate_expr(arguments[i], env);
        if (is_error(arg_result)) {
            // Clean up any arguments already on stack
            for (size_t j = 0; j < i; j++) {
                env->current_context->pop();
            }
            env->current_context->popFrame();
            func_env->release();
            return arg_result;
        }
        // Argument is now on stack
    }
    
    // Pop arguments from stack and bind to parameters
    // Pop in reverse order since stack is LIFO
    for (size_t i = arg_count; i > 0; i--) {
        Value arg_value = env->current_context->pop();
        func_env->define(function->param_names[i-1], arg_value);
    }
    
    // Execute function body
    EvalResult result = make_success(0);
    
    for (size_t i = 0; i < function->body_count; i++) {
        result = evaluate_stmt(function->body[i], func_env);
        if (is_error(result)) {
            env->current_context->popFrame();
            func_env->release();
            return result;
        }
        
        // Check if a return statement was executed (including nested in control structures)
        if (result.has_returned) {
            break;  // Return statement found, exit function body execution
        }
    }
    
    // Pop function call from stack trace
    env->current_context->popFrame();
    
    // Return value is already on func_env's stack (if any)
    // Transfer it to the caller's stack
    if (!is_error(result) && result.has_returned && result.return_count > 0) {
        func_env->release();
        return make_success(1);  // Indicate 1 value on stack
    } else {
        func_env->release();
        return make_success(0);  // No return value
    }
}


// Expression evaluation
EvalResult eval_literal_expr(LiteralExpr* expr, Environment* env) {
    env->current_context->push( expr->value);
    return make_success(1);
}

// Stack-based function call evaluation
EvalResult eval_call_expr(CallExpr* expr, Environment* env) {
    const char* full_name = nullptr;
    MobiusString* full_name_interned = nullptr;
    int call_line = 0;
    int call_column = 0;
    
    // Handle module.function() syntax (TABLE_DOT expression)
    if (expr->callee->type == EXPR_TABLE_DOT) {
        TableDotExpr* dot_expr = &expr->callee->as.table_dot;
        
        // Check if the table part is a module name (VARIABLE expression)
        if (dot_expr->table->type == EXPR_VARIABLE) {
            const char* table_name = dot_expr->table->as.variable.name.identifier;
            const char* func_name = dot_expr->key.identifier;
            
            bool found = false;
            Value table_value = env->get(dot_expr->table->as.variable.name.interned, &found);
            if (found && table_value.type == VAL_TABLE) {
                MobiusState* state = env->current_context->state;
                Value func_key = make_string_value_from_cstr(state, func_name);
                Value func_value = table_value.as.table->get(func_key);
                
                // If we found a native function in the table, call it
                if (func_value.type == VAL_NATIVE_FUNCTION && func_value.as.native_function) {
                    // Evaluate arguments onto stack
                    for (size_t i = 0; i < expr->arg_count; i++) {
                        EvalResult arg_result = evaluate_expr(expr->arguments[i], env);
                        if (is_error(arg_result)) {
                            // Clean up any arguments already on stack
                            for (size_t j = 0; j < i; j++) {
                                env->current_context->pop();
                            }
                            return arg_result;
                        }
                    }
                    
                    return eval_call_native_func(func_value.as.native_function,
                                                env->current_context->state,
                                                expr->arg_count, env, func_name,
                                                dot_expr->key.line, dot_expr->key.column);
                } else if (func_value.type == VAL_FUNCTION) {
                    // User-defined function in table
                    return call_user_function(func_value.as.function, expr->arguments, expr->arg_count, env);
                } else if (func_value.type != VAL_NIL) {
                    // Found something but it's not a function
                    char error_msg[256];
                    snprintf(error_msg, sizeof(error_msg), 
                        "'%s.%s' is not a function (type: %s)", 
                        table_name, func_name, value_type_name(func_value.type));
                    return make_error(env, error_msg, dot_expr->key.line, dot_expr->key.column);
                }
                
                // Function not found in table
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg), 
                    "Function '%s' not found in '%s'", func_name, table_name);
                return make_error(env, error_msg, dot_expr->key.line, dot_expr->key.column);
            }
            
            // Table not found or not a table
            if (found) {
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg), 
                    "'%s' is not a table or module (type: %s)", 
                    table_name, value_type_name(table_value.type));
                return make_error(env, error_msg, dot_expr->key.line, dot_expr->key.column);
            } else {
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg), 
                    "Undefined module or table '%s'. Did you forget to import it?", table_name);
                return make_error(env, error_msg, dot_expr->key.line, dot_expr->key.column);
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
                Value table_value = env->current_context->pop();
                
                if (table_value.type != VAL_TABLE) {
                    return make_error(env, "Cannot call method on non-table value", 
                        dot_expr->key.line, dot_expr->key.column);
                }
                
                // Look up the function in the table
                const char* func_name = dot_expr->key.identifier;
                MobiusState* state = env->current_context->state;
                Value func_key = make_string_value_from_cstr(state, func_name);
                Value func_value = table_value.as.table->get(func_key);
                
                // If we found a native function, call it
                if (func_value.type == VAL_NATIVE_FUNCTION && func_value.as.native_function) {
                    // Evaluate arguments onto stack
                    for (size_t i = 0; i < expr->arg_count; i++) {
                        EvalResult arg_result = evaluate_expr(expr->arguments[i], env);
                        if (is_error(arg_result)) {
                            // Clean up any arguments already on stack
                            for (size_t j = 0; j < i; j++) {
                                env->current_context->pop();
                            }
                            return arg_result;
                        }
                    }
                    
                    return eval_call_native_func(func_value.as.native_function,
                                                env->current_context->state,
                                                expr->arg_count, env, func_name,
                                                dot_expr->key.line, dot_expr->key.column);
                } else if (func_value.type == VAL_FUNCTION) {
                    // User-defined function - call it
                    return call_user_function(func_value.as.function, expr->arguments, expr->arg_count, env);
                } else {
                    char error_msg[256];
                    snprintf(error_msg, sizeof(error_msg), 
                        "'%s' is not a function (type: %s)", 
                        func_name, value_type_name(func_value.type));
                    return make_error(env, error_msg, dot_expr->key.line, dot_expr->key.column);
                }
            }
            
            return make_error(env, "Table expression did not return a value", 
                dot_expr->key.line, dot_expr->key.column);
        }
    }
    // Handle simple function() or qualified_name() syntax  
    else if (expr->callee->type == EXPR_VARIABLE) {
    VariableExpr* var_expr = &expr->callee->as.variable;
    full_name = var_expr->name.identifier ? var_expr->name.identifier : "unknown";
    full_name_interned = var_expr->name.interned;
        call_line = var_expr->name.line;
        call_column = var_expr->name.column;
    }
    else {
        return make_error(env, "Only variable and module.function calls supported", 0, 0);
    }
    
    // All function calls go through the environment
    // Module functions must be imported first using: import "module_name";
    // Then called via: module_name.function() which is handled as TABLE_DOT above
    
    // Check if it's a user-defined function or native function in the environment
    bool found = false;
    if (!full_name_interned) {
        full_name_interned = env->current_context->state->stringPool()->intern(full_name);
    }
    Value func_value = env->get(full_name_interned, &found);
    if (found) {
        if (func_value.type == VAL_FUNCTION && func_value.as.function) {
            // Call user-defined function
            EvalResult result = call_user_function(func_value.as.function, expr->arguments, expr->arg_count, env);
            return result;
        } else if (func_value.type == VAL_NATIVE_FUNCTION && func_value.as.native_function) {
            // Call native function (stdlib or imported from module)
            // Evaluate arguments onto stack
            for (size_t i = 0; i < expr->arg_count; i++) {
                EvalResult arg_result = evaluate_expr(expr->arguments[i], env);
                if (is_error(arg_result)) {
                    // Clean up any arguments already on stack
                    for (size_t j = 0; j < i; j++) {
                        env->current_context->pop();
                    }
                    return arg_result;
                }
            }
            
            EvalResult result = eval_call_native_func(func_value.as.native_function,
                                                      env->current_context->state,
                                                      expr->arg_count, env, full_name,
                                                      call_line, call_column);
            return result;
        }
    }
    
    // Function not found - provide helpful error message
    // Check if this function might exist in any loaded plugin module
    char suggested_import[256] = {0};
    ModuleRegistry* reg = getGlobalRegistry();
    if (reg) {
        for (const auto& mod : reg->modules()) {
            if (mod.plugin && mod.plugin->functions) {
                for (size_t i = 0; i < mod.plugin->function_count; i++) {
                    if (strcmp(mod.plugin->functions[i].name, full_name) == 0) {
                        snprintf(suggested_import, sizeof(suggested_import), "%s", mod.name.c_str());
                        break;
                    }
                }
                if (suggested_import[0]) break;
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
        env,
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
    const char* name = expr->name.identifier ? expr->name.identifier : "unknown";
    const Value* value = env->lookup(expr->name.interned);
    
    if (!value) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Undefined variable '%s'", name);
        
        return make_error_detailed(
            env,
            error_msg,
            "Make sure the variable is declared before use",
            ERROR_UNDEFINED,
            0, 0,
            name,
            NULL
        );
    }
    
    env->current_context->push(*value);
    return make_success(1);
}

EvalResult eval_assignment_expr(AssignmentExpr* expr, Environment* env) {
    EvalResult value_result = evaluate_expr(expr->value, env);
    if (is_error(value_result)) {
        return value_result;
    }
    Value value = env->current_context->pop();
    
    const char* name = expr->name.identifier ? expr->name.identifier : "unknown";
    if (!env->assign(expr->name.interned, value)) {
        return make_error_detailed(env, "Undefined variable in assignment", 
                                               "Make sure the variable is declared before use",
                                               ERROR_UNDEFINED, expr->name.line, expr->name.column, NULL, NULL);
    }
    
    // Push the value back to stack (assignment expressions return the assigned value)
    // Note: We push the same value, not a copy, since we want to return the assigned value
    // But we don't free it here because it's being returned on the stack
    env->current_context->push( value);
    return make_success(1);
}

EvalResult eval_grouping_expr(GroupingExpr* expr, Environment* env) {
    return evaluate_expr(expr->expression, env);
}

static inline int64_t integer_extract(const Value& v) {
    return v.as.integer.value;
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

    auto& stk = env->current_context->stack;
    size_t sz = stk.size();
    Value& left  = stk[sz - 2];
    Value& right = stk[sz - 1];

    TokenType op = expr->op.type;

    // ---- Integer fast path: both operands are integers ----
    if (left.type == VAL_INTEGER && right.type == VAL_INTEGER) {
        int64_t lv = integer_extract(left);
        int64_t rv = integer_extract(right);

        switch (op) {
            case TOKEN_PLUS:
                left.as.integer.num_type = NUM_INT64;
                left.as.integer.value = lv + rv;
                stk.pop_back();
                return make_success(1);
            case TOKEN_MINUS:
                left.as.integer.num_type = NUM_INT64;
                left.as.integer.value = lv - rv;
                stk.pop_back();
                return make_success(1);
            case TOKEN_STAR:
                left.as.integer.num_type = NUM_INT64;
                left.as.integer.value = lv * rv;
                stk.pop_back();
                return make_success(1);
            case TOKEN_PERCENT:
                if (rv == 0) {
                    stk.pop_back();
                    stk.pop_back();
                    return make_error_detailed(env, "Modulo by zero",
                        "Check that the divisor is not zero before performing modulo",
                        ERROR_DIVISION, expr->op.line, expr->op.column, NULL, NULL);
                }
                left.as.integer.num_type = NUM_INT64;
                left.as.integer.value = lv % rv;
                stk.pop_back();
                return make_success(1);
            case TOKEN_SLASH: {
                if ((double)rv == 0.0) {
                    stk.pop_back();
                    stk.pop_back();
                    return make_error_detailed(env, "Division by zero",
                        "Check that the divisor is not zero before performing division",
                        ERROR_DIVISION, expr->op.line, expr->op.column, NULL, NULL);
                }
                left.type = VAL_FLOAT64;
                left.as.double_val = (double)lv / (double)rv;
                stk.pop_back();
                return make_success(1);
            }
            case TOKEN_LESS:
                left.type = VAL_BOOL;
                left.as.boolean = lv < rv;
                stk.pop_back();
                return make_success(1);
            case TOKEN_LESS_EQUAL:
                left.type = VAL_BOOL;
                left.as.boolean = lv <= rv;
                stk.pop_back();
                return make_success(1);
            case TOKEN_GREATER:
                left.type = VAL_BOOL;
                left.as.boolean = lv > rv;
                stk.pop_back();
                return make_success(1);
            case TOKEN_GREATER_EQUAL:
                left.type = VAL_BOOL;
                left.as.boolean = lv >= rv;
                stk.pop_back();
                return make_success(1);
            case TOKEN_EQUAL_EQUAL:
                left.type = VAL_BOOL;
                left.as.boolean = lv == rv;
                stk.pop_back();
                return make_success(1);
            case TOKEN_BANG_EQUAL:
                left.type = VAL_BOOL;
                left.as.boolean = lv != rv;
                stk.pop_back();
                return make_success(1);
            default:
                break;
        }
    }

    // ---- Slow path: pop both operands and delegate ----
    Value right_val = std::move(stk.back());
    stk.pop_back();
    Value left_val = std::move(stk.back());
    stk.pop_back();

    switch (op) {
        case TOKEN_PLUS:
            return add_values(env, left_val, right_val);
        case TOKEN_MINUS:
            return subtract_values(env, left_val, right_val);
        case TOKEN_STAR:
            return multiply_values(env, left_val, right_val);
        case TOKEN_SLASH:
            return divide_values(env, left_val, right_val, expr->op.line, expr->op.column);
        case TOKEN_PERCENT:
            return modulo_values(env, left_val, right_val, expr->op.line, expr->op.column);
        case TOKEN_EQUAL_EQUAL:
        case TOKEN_BANG_EQUAL:
        case TOKEN_GREATER:
        case TOKEN_GREATER_EQUAL:
        case TOKEN_LESS:
        case TOKEN_LESS_EQUAL:
            return compare_values(env, left_val, right_val, op);
        case TOKEN_AND:
        case TOKEN_AND_AND:
            if (!is_truthy(left_val)) {
                env->current_context->push(std::move(left_val));
                return make_success(1);
            }
            env->current_context->push(std::move(right_val));
            return make_success(1);
        case TOKEN_OR:
        case TOKEN_OR_OR:
            if (is_truthy(left_val)) {
                env->current_context->push(std::move(left_val));
                return make_success(1);
            }
            env->current_context->push(std::move(right_val));
            return make_success(1);
        default:
            return make_error(env, "Unknown binary operator", expr->op.line, expr->op.column);
    }
}

EvalResult eval_unary_expr(UnaryExpr* expr, Environment* env) {
    EvalResult operand_result = evaluate_expr(expr->right, env);
    if (is_error(operand_result)) {
        return operand_result;
    }
    Value operand = env->current_context->pop();
    
    switch (expr->op.type) {
        case TOKEN_MINUS:
            if (operand.type == VAL_FLOAT64) {
                env->current_context->push(make_float_value(-operand.as.double_val));
                return make_success(1);
            } else if (operand.type == VAL_INTEGER) {
                env->current_context->push(make_integer_value(NUM_INT64, -operand.as.integer.value));
                return make_success(1);
            } else {
                return make_error(env, "Cannot negate non-numeric value", expr->op.line, expr->op.column);
            }
        case TOKEN_PLUS:
            // Unary plus is identity for numbers
            if (operand.type == VAL_FLOAT64 || operand.type == VAL_INTEGER) {
                env->current_context->push( operand);
                return make_success(1);
            } else {
                return make_error(env, "Cannot apply unary plus to non-numeric value", expr->op.line, expr->op.column);
            }
        case TOKEN_BANG:
        case TOKEN_NOT: {
            bool truthy = is_truthy(operand);
            env->current_context->push( make_bool_value(!truthy));
            return make_success(1);
        }
        default:
            return make_error(env, "Unknown unary operator", expr->op.line, expr->op.column);
    }
}