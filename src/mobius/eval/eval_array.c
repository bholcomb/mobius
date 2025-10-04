#include "eval/evaluator.h"
#include "data/array.h"
#include "state/mobius_state.h"


#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Array evaluation functions
EvalResult eval_array_literal_expr(ArrayLiteralExpr* expr, Environment* env) {
    // Create a new array
    ArrayValue* array = array_create(expr->element_count);
    if (!array) {
        return make_error(env, "Failed to create array", 0, 0);
    }
    
    // Evaluate and add each element
    for (size_t i = 0; i < expr->element_count; i++) {
        EvalResult element_result = evaluate_expr(expr->elements[i], env);
        if (is_error(element_result)) {
            // Clean up any elements already on stack
            for (size_t j = 0; j < i; j++) {
                ctx_pop(env->current_context);
            }
            array_release(array);
            return element_result;
        }
        // Element is now on stack
    }
    
    // Pop elements from stack in reverse order and add to array
    for (size_t i = expr->element_count; i > 0; i--) {
        Value element = ctx_pop(env->current_context);
        array_push(array, element);
        // Free the element since array_push copied it
        free_value(element);
    }
    
    // Reverse the array since we pushed in LIFO order
    for (size_t i = 0; i < array->length / 2; i++) {
        Value temp = array->elements[i];
        array->elements[i] = array->elements[array->length - 1 - i];
        array->elements[array->length - 1 - i] = temp;
    }
    
    ctx_push(env->current_context, make_array_value(array));
    return make_success(1);
}

EvalResult eval_array_index_expr(ArrayIndexExpr* expr, Environment* env) {
    // Evaluate the target expression (could be array or table)
    EvalResult target_result = evaluate_expr(expr->array, env);
    if (is_error(target_result)) {
        return target_result;
    }
    Value target_value = ctx_pop(env->current_context);
    
    // Evaluate the index expression
    EvalResult index_result = evaluate_expr(expr->index, env);
    if (is_error(index_result)) {
        free_value(target_value);
        return index_result;
    }
    Value index_value = ctx_pop(env->current_context);
    
    // Handle both arrays and tables
    if (target_value.type == VAL_ARRAY) {
        // Array indexing logic
        if (index_value.type != VAL_INTEGER) {
            free_value(target_value);
            free_value(index_value);
            return make_error(env, "Array index must be an integer", 0, 0);
        }
        
        // Get the index value
        int64_t index = index_value.as.integer.value.i64;
        ArrayValue* array = target_value.as.array;
        
        // Check bounds
        if (index < 0 || (size_t)index >= array->length) {
            free_value(target_value);
            free_value(index_value);
            return make_error(env, "Array index out of bounds", 0, 0);
        }
        
        // Get the value (array_get returns a copy)
        Value result = array_get(array, (size_t)index);
        
        // Clean up
        free_value(index_value);
        free_value(target_value);  // Free the array value we got from get_variable
        
        ctx_push(env->current_context, result);
        return make_success(1);
        
    } else if (target_value.type == VAL_TABLE) {
        // Table indexing logic (same as before)
        Value result = table_get(target_value.as.table, index_value);
        
        // Clean up
        free_value(index_value);
        free_value(target_value);  // Free the table value we got from get_variable
        
        ctx_push(env->current_context, result);
        return make_success(1);
        
    } else {
        // Neither array nor table
        free_value(target_value);
        free_value(index_value);
        return make_error(env, "Cannot index non-array/non-table value", 0, 0);
    }
}