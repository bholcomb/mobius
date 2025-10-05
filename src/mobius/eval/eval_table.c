#include "eval/evaluator.h"
#include "state/mobius_state.h"
#include "data/table.h"
#include "data/enum.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Table evaluation functions
EvalResult eval_table_literal_expr(TableLiteralExpr* expr, Environment* env) {
    MobiusState* state = env->current_context->state;
    Table* table = create_table(state, INITIAL_TABLE_CAPACITY);
    if (!table) {
        return make_error(env, "Failed to create table", 0, 0);
    }
    
    // Evaluate each key-value pair
    for (size_t i = 0; i < expr->pair_count; i++) {
        TablePair* pair = &expr->pairs[i];
        
        Value key;
        Value value;
        
        // Evaluate the value
        EvalResult value_result = evaluate_expr(pair->value, env);
        if (is_error(value_result)) {
            free_table(table);
            return value_result;
        }
        value = ctx_pop(env->current_context);
        
        // Evaluate the key (if provided, otherwise use index)
        if (pair->key) {
            EvalResult key_result = evaluate_expr(pair->key, env);
            if (is_error(key_result)) {
                free_table(table);
                free_value(value);
                return key_result;
            }
            key = ctx_pop(env->current_context);
        } else {
            // Use index as key for array-style initialization
            key = make_integer_value(NUM_INT64, (int64_t)i);
        }
        
        // Set the key-value pair in the table
        if (!table_set(table, key, value)) {
            free_table(table);
            free_value(key);
            free_value(value);
            return make_error(env, "Failed to set table entry", 0, 0);
        }
    }
    
    ctx_push(env->current_context, make_table_value(table));
    return make_success(1);
}

EvalResult eval_table_index_expr(TableIndexExpr* expr, Environment* env) {
    // Evaluate the table expression
    EvalResult table_result = evaluate_expr(expr->table, env);
    if (is_error(table_result)) {
        return table_result;
    }
    Value table_value = ctx_pop(env->current_context);
    
    if (table_value.type != VAL_TABLE) {
        free_value(table_value);
        return make_error(env, "Cannot index non-table value", 0, 0);
    }
    
    // Evaluate the index expression
    EvalResult index_result = evaluate_expr(expr->index, env);
    if (is_error(index_result)) {
        free_value(table_value);
        return index_result;
    }
    Value index_value = ctx_pop(env->current_context);
    
    // Get the value from the table
    Value result = table_get(table_value.as.table, index_value);
    
    // Clean up
    free_value(index_value);
    free_value(table_value);  // Free the table value we popped from the stack
    
    ctx_push(env->current_context, result);
    return make_success(1);
}

EvalResult eval_table_dot_expr(TableDotExpr* expr, Environment* env) {
    // Evaluate the table expression
    EvalResult table_result = evaluate_expr(expr->table, env);
    
    // Check if this might be an enum access attempt when variable lookup fails
    if (is_error(table_result) && expr->table->type == EXPR_VARIABLE) {
        // The variable doesn't exist, might be an enum name
        // Only try enum access if there's actually an enum with this name
        const char* enum_name = expr->table->as.variable.name.identifier;
        const char* member_name = expr->key.identifier;
        
        if (enum_name && member_name) {
            // First check if an enum with this name exists
            char enum_var_name[256];
            snprintf(enum_var_name, sizeof(enum_var_name), "__enum_%s", enum_name);
            
            bool enum_found = false;
            Value enum_value = get_variable(env, enum_var_name, &enum_found);
            
            // Only proceed with enum access if the enum actually exists
            if (enum_found && enum_value.type == VAL_USERDATA && 
                strcmp(enum_value.as.userdata.type_name, "enum_definition") == 0) {
                
                EnumDefinition* enum_def = (EnumDefinition*)enum_value.as.userdata.ptr;
                if (enum_def) {
                    // Find the enum member
                    EnumMember* member = enum_definition_find_member(enum_def, member_name);
                    if (member) {
                        // Create enum value
                        Value result = make_enum_value(enum_def, member->value);
                        ctx_push(env->current_context, result);
                        return make_success(1);
                    } else {
                        char error_msg[256];
                        snprintf(error_msg, sizeof(error_msg), "Undefined enum member '%s.%s'", enum_name, member_name);
                        return make_error(env, error_msg, 0, 0);
                    }
                }
            }
        }
        
        // Not an enum access or enum doesn't exist, return the original variable error
        return table_result;
    }
    
    if (is_error(table_result)) {
        return table_result;
    }
    Value table_value = ctx_pop(env->current_context);
    
    
    if (table_value.type != VAL_TABLE) {
        free_value(table_value);
        return make_error(env, "Cannot access property of non-table value", 0, 0);
    }
    
    // Create a string key from the identifier token
    const char* key_identifier = expr->key.identifier ? expr->key.identifier : "unknown";
    size_t key_len = strlen(key_identifier);
    char* key_str = malloc(key_len + 1);
    if (!key_str) {
        free_value(table_value);
        return make_error(env, "Memory allocation failed", 0, 0);
    }
    strcpy(key_str, key_identifier);
    
    Value key = make_string_value_from_cstr(env->current_context->state, key_str);
    free(key_str);
    
    // Get the value from the table
    Value result = table_get(table_value.as.table, key);
    
    // Clean up
    free_value(key);
    free_value(table_value);  // Free the table value we popped from the stack
    
    ctx_push(env->current_context, result);
    return make_success(1);
}
