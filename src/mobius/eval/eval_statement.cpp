#include "eval/evaluator.h"
#include "state/mobius_state.h"

#include <new>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Statement evaluation
EvalResult eval_expression_stmt(ExpressionStmt* stmt, Environment* env) {
    EvalResult result = evaluate_expr(stmt->expression, env);
    // Pop the expression result off the stack (we don't need it for statements)
    if (!is_error(result) && result.return_count > 0) {
        env->current_context->pop();
        return make_success(0);
    }
    // If there was an error, propagate it
    return result;
}

EvalResult eval_var_stmt(VarStmt* stmt, Environment* env) {
    Value value = make_nil_value();
    
    if (stmt->initializer) {
        EvalResult init_result = evaluate_expr(stmt->initializer, env);
        if (is_error(init_result)) {
            return init_result;
        }
        // Stack-based evaluation: pop the value from the stack
        if (init_result.return_count > 0) {
            value = env->current_context->pop();
        }
    }
    
    // Validate and convert type if annotation is provided
    if (stmt->is_annotated) {
        MobiusState* state = env->current_context->state;
        TypeCheckConfig type_config = {
            state->config().strict_mode,
            state->config().warn_on_conversion
        };
        
        TypeConversionResult conversion = validate_and_convert_value(value, stmt->type_hint, stmt->is_annotated, type_config);
        if (!conversion.success) {
            return make_error_detailed(
                env,
                conversion.error_message ? conversion.error_message : "Type validation failed",
                "Check that the value matches the declared type",
                ERROR_TYPE,
                stmt->name.line,
                stmt->name.column,
                "eval_var_stmt",
                NULL
            );
        }
        
        // Use the converted value
        value = conversion.converted_value;
        
        // Warn about conversions if enabled
        if (conversion.was_converted && state->config().warn_on_conversion) {
            printf("Warning: Implicit type conversion in variable declaration at line %d\n", stmt->name.line);
        }
        
        // Free error message if any
        free(conversion.error_message);
    }
    
    StringInternPool* pool = env->current_context->state->stringPool();
    MobiusString* interned_name = stmt->name.interned;
    if (!interned_name) interned_name = pool->intern("unknown");
    
    // Check for namespace collision: an enum with the same name shouldn't exist
    char enum_var_buf[256];
    snprintf(enum_var_buf, sizeof(enum_var_buf), "__enum_%s", interned_name->data);
    MobiusString* enum_key = pool->intern(enum_var_buf);
    bool enum_exists = false;
    env->get(enum_key, &enum_exists);
    
    if (enum_exists) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Name collision: enum '%s' already exists, cannot declare variable with the same name", interned_name->data);
        return make_error(env, error_msg, stmt->name.line, stmt->name.column);
    }
    
    env->define(interned_name, value);
    
    return make_success(0);
}

EvalResult eval_block_stmt(BlockStmt* stmt, Environment* env) {
    Environment* block_env = new (std::nothrow) Environment(env, env->current_context);
    if (!block_env) {
        return make_error(env, "Memory allocation failed", 0, 0);
    }
    
    EvalResult result = make_success(0);
    for (size_t i = 0; i < stmt->count; i++) {
        result = evaluate_stmt(stmt->statements[i], block_env);
        if (is_error(result)) {
            break;
        }
        
        // If a return statement was executed, break out of the block
        if (result.has_returned) {
            break;
        }
        
        // If a break or continue statement was executed, break out of the block
        // These will be handled by the containing loop
        if (result.has_break || result.has_continue) {
            break;
        }
    }
    
    block_env->release();
    return result;  // Preserve has_returned, has_break, has_continue flags
}

EvalResult eval_if_stmt(IfStmt* stmt, Environment* env) {
    EvalResult condition_result = evaluate_expr(stmt->condition, env);
    if (is_error(condition_result)) {
        return condition_result;
    }
    
    Value condition_val = env->current_context->pop();
    bool condition = is_truthy(condition_val);
    
    if (condition) {
        return evaluate_stmt(stmt->then_branch, env);  // This will propagate has_returned
    } else if (stmt->else_branch) {
        return evaluate_stmt(stmt->else_branch, env);  // This will propagate has_returned
    }
    
    return make_success(0);
}

EvalResult eval_while_stmt(WhileStmt* stmt, Environment* env) {
    while (true) {
        EvalResult condition_result = evaluate_expr(stmt->condition, env);
        if (is_error(condition_result)) {
            return condition_result;
        }
        
        Value condition_val = env->current_context->pop();
        bool condition = is_truthy(condition_val);
        
        if (!condition) {
            break;
        }
        
        EvalResult result = evaluate_stmt(stmt->body, env);
        if (is_error(result)) {
            return result;
        }
        
        // Handle break statement
        if (result.has_break) {
            result.has_break = false;  // Reset break flag
            break;
        }
        
        // Handle continue statement
        if (result.has_continue) {
            result.has_continue = false;  // Reset continue flag
            continue;  // Go to next iteration
        }
        
        // Handle return statement
        if (result.has_returned) {
            return result;  // Propagate return
        }
    }
    
    return make_success(0);
}

EvalResult eval_for_stmt(ForStmt* stmt, Environment* env) {
    // Create new environment for the for loop scope
    Environment* for_env = new (std::nothrow) Environment(env, env->current_context);
    if (!for_env) {
        return make_error(env, "Memory allocation failed", 0, 0);
    }
    
    // Execute initializer
    if (stmt->initializer) {
        EvalResult result = evaluate_stmt(stmt->initializer, for_env);
        if (is_error(result)) {
            for_env->release();
            return result;
        }
    }
    
    // Loop
    while (true) {
        // Check condition
        if (stmt->condition) {
            EvalResult condition_result = evaluate_expr(stmt->condition, for_env);
            if (is_error(condition_result)) {
                for_env->release();
                return condition_result;
            }
            
            Value condition_val = env->current_context->pop();
            bool condition = is_truthy(condition_val);
            
            if (!condition) {
                break;
            }
        }
        
        // Execute body
        EvalResult result = evaluate_stmt(stmt->body, for_env);
        if (is_error(result)) {
            for_env->release();
            return result;
        }
        
        // Handle break statement
        if (result.has_break) {
            result.has_break = false;  // Reset break flag
            break;
        }
        
        // Handle return statement
        if (result.has_returned) {
            for_env->release();
            return result;  // Propagate return
        }
        
        // Execute increment (always executed, even on continue)
        if (stmt->increment) {
            EvalResult increment_result = evaluate_expr(stmt->increment, for_env);
            if (is_error(increment_result)) {
                for_env->release();
                return increment_result;
            }
        }
        
        // Handle continue statement (after increment)
        if (result.has_continue) {
            result.has_continue = false;  // Reset continue flag
            continue;  // Go to next iteration
        }
    }
    
    for_env->release();
    return make_success(0);
}

// Function statement evaluation: func name(params) { body }
EvalResult eval_function_stmt(FunctionStmt* stmt, Environment* env) {
    MobiusFunction* function = (MobiusFunction*)malloc(sizeof(MobiusFunction));
    if (!function) {
        return make_error(env, "Memory allocation failed", 0, 0);
    }
    
    function->name = stmt->name.interned;
    if (!function->name) {
        free(function);
        return make_error(env, "Failed to extract function name", 0, 0);
    }
    
    function->param_count = stmt->param_count;
    if (stmt->param_count > 0) {
        function->param_names = (MobiusString**)malloc(stmt->param_count * sizeof(MobiusString*));
        if (!function->param_names) {
            free(function);
            return make_error(env, "Memory allocation failed", 0, 0);
        }
        
        for (size_t i = 0; i < stmt->param_count; i++) {
            function->param_names[i] = stmt->params[i].interned;
            if (!function->param_names[i]) {
                free(function->param_names);
                free(function);
                return make_error(env, "Failed to extract parameter name", 0, 0);
            }
        }
    } else {
        function->param_names = NULL;
    }
    
    function->body_count = stmt->body_count;
    if (stmt->body_count > 0) {
        function->body = (Stmt**)malloc(stmt->body_count * sizeof(Stmt*));
        if (!function->body) {
            free(function->param_names);
            free(function);
            return make_error_detailed(env, "Memory allocation failed for function body", NULL, ERROR_RUNTIME, 0, 0, NULL, NULL);
        }
        
        for (size_t i = 0; i < stmt->body_count; i++) {
            function->body[i] = stmt->body[i];
            ast_retain_stmt(stmt->body[i]);
        }
    } else {
        function->body = NULL;
    }
    function->closure = env;
    env->retain();
    function->ref_count = 1;
    function->upvalues = nullptr;
    function->upvalue_count = 0;
    
    Value func_value = make_function_value(function);
    
    env->define(function->name, func_value);
    return make_success(0);
}

// Return statement evaluation: return [expression]
EvalResult eval_return_stmt(ReturnStmt* stmt, Environment* env) {
    if (stmt->value) {
        EvalResult result = evaluate_expr(stmt->value, env);
        if (is_error(result)) {
            return result;
        }
        // Value is already on stack from evaluate_expr, don't pop it
        // Just mark that we're returning
        result.has_returned = true;
        return result;
    } else {
        // No return value
        EvalResult result = make_success(0);
    result.has_returned = true;
    return result;
    }
}

// Break statement evaluation
EvalResult eval_break_stmt(BreakStmt* stmt, Environment* env) {
    (void)stmt;  // Unused parameter
    (void)env;   // Unused parameter
    
    // Break statements are handled by the control flow structures (loops, switch)
    EvalResult result;
    result.has_error = false;
    result.has_returned = false;
    result.has_break = true;
    result.has_continue = false;
    result.return_count = 0;
    return result;
}

// Continue statement evaluation
EvalResult eval_continue_stmt(ContinueStmt* stmt, Environment* env) {
    (void)stmt;  // Unused parameter
    (void)env;   // Unused parameter
    
    // Continue statements are handled by the control flow structures (loops)
    EvalResult result;
    result.has_error = false;
    result.has_returned = false;
    result.has_break = false;
    result.has_continue = true;
    result.return_count = 0;
    return result;
}
