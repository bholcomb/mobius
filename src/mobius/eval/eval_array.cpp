#include "eval/evaluator.h"
#include "data/array.h"
#include "data/table.h"
#include "state/mobius_state.h"


#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Array evaluation functions
EvalResult eval_array_literal_expr(ArrayLiteralExpr* expr, Environment* env) {
    // Create a new array
    ArrayValue* array = new ArrayValue(expr->element_count);
    if (!array) {
        return make_error(env, "Failed to create array", 0, 0);
    }
    
    // Evaluate and add each element
    for (size_t i = 0; i < expr->element_count; i++) {
        EvalResult element_result = evaluate_expr(expr->elements[i], env);
        if (is_error(element_result)) {
            // Clean up any elements already on stack
            for (size_t j = 0; j < i; j++) {
                env->current_context->pop();
            }
            array->release();
            return element_result;
        }
        // Element is now on stack
    }
    
    // Pop elements from stack in reverse order and add to array
    for (size_t i = expr->element_count; i > 0; i--) {
        Value element = env->current_context->pop();
        array->push(element);
    }
    
    // Reverse the array since we pushed in LIFO order
    array->reverse();
    
    env->current_context->push( make_array_value(array));
    return make_success(1);
}

EvalResult eval_array_index_expr(ArrayIndexExpr* expr, Environment* env) {
    // Evaluate the target expression (could be array or table)
    EvalResult target_result = evaluate_expr(expr->array, env);
    if (is_error(target_result)) {
        return target_result;
    }
    Value target_value = env->current_context->pop();
    
    // Evaluate the index expression
    EvalResult index_result = evaluate_expr(expr->index, env);
    if (is_error(index_result)) {
        return index_result;
    }
    Value index_value = env->current_context->pop();
    
    // Handle both arrays and tables
    if (target_value.type == VAL_ARRAY) {
        // Array indexing logic
        if (index_value.type != VAL_INTEGER) {
            return make_error(env, "Array index must be an integer", 0, 0);
        }
        
        // Get the index value
        int64_t index = index_value.as.integer.value;
        ArrayValue* array = target_value.as.array;
        
        // Check bounds
        if (index < 0 || (size_t)index >= array->length()) {
            return make_error(env, "Array index out of bounds", 0, 0);
        }
        
        Value result = array->get((size_t)index);
        
        env->current_context->push( result);
        return make_success(1);
        
    } else if (target_value.type == VAL_TABLE) {
        // Table indexing logic (same as before)
        Value result = target_value.as.table->get(index_value);
        
        env->current_context->push( result);
        return make_success(1);
        
    } else {
        // Neither array nor table
        return make_error(env, "Cannot index non-array/non-table value", 0, 0);
    }
}