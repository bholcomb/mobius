#include "eval/evaluator.h"
#include "state/mobius_state.h"
#include "data/table.h"
#include "data/enum.h"

#include <new>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Table evaluation functions
EvalResult eval_table_literal_expr(TableLiteralExpr* expr, Environment* env) {
    MobiusState* state = env->current_context->state;
    Table* table = new (std::nothrow) Table(state, INITIAL_TABLE_CAPACITY);
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
            table->release();
            return value_result;
        }
        value = env->current_context->pop();
        
        // Evaluate the key (if provided, otherwise use index)
        if (pair->key) {
            EvalResult key_result = evaluate_expr(pair->key, env);
            if (is_error(key_result)) {
                table->release();
                return key_result;
            }
            key = env->current_context->pop();
        } else {
            // Use index as key for array-style initialization
            key = make_integer_value(NUM_INT64, (int64_t)i);
        }
        
        // Set the key-value pair in the table
        if (!table->set(key, value)) {
            table->release();
            return make_error(env, "Failed to set table entry", 0, 0);
        }
    }
    
    env->current_context->push( make_table_value(table));
    return make_success(1);
}

EvalResult eval_table_index_expr(TableIndexExpr* expr, Environment* env) {
    // Evaluate the table expression
    EvalResult table_result = evaluate_expr(expr->table, env);
    if (is_error(table_result)) {
        return table_result;
    }
    Value table_value = env->current_context->pop();
    
    if (table_value.type != VAL_TABLE) {
        return make_error(env, "Cannot index non-table value", 0, 0);
    }
    
    // Evaluate the index expression
    EvalResult index_result = evaluate_expr(expr->index, env);
    if (is_error(index_result)) {
        return index_result;
    }
    Value index_value = env->current_context->pop();
    
    // Get the value from the table
    Value result = table_value.as.table->get(index_value);
    
    env->current_context->push( result);
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
            char enum_buf[256];
            snprintf(enum_buf, sizeof(enum_buf), "__enum_%s", enum_name);
            StringInternPool* pool = env->current_context->state->stringPool();
            const char* enum_key = pool->intern(enum_buf)->data;
            
            bool enum_found = false;
            Value enum_value = env->get(enum_key, &enum_found);
            
            // Only proceed with enum access if the enum actually exists
            if (enum_found && enum_value.type == VAL_USERDATA && 
                strcmp(enum_value.as.userdata.type_name, "enum_definition") == 0) {
                
                EnumDefinition* enum_def = static_cast<EnumDefinition*>(enum_value.as.userdata.ptr);
                if (enum_def) {
                    const EnumMember* member = enum_def->findMember(member_name);
                    if (member) {
                        // Create enum value
                        Value result = Value::makeEnum(enum_def, member->value);
                        env->current_context->push( result);
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
    Value table_value = env->current_context->pop();
    
    
    if (table_value.type != VAL_TABLE) {
        return make_error(env, "Cannot access property of non-table value", 0, 0);
    }
    
    // Create a string key from the identifier token
    const char* key_identifier = expr->key.identifier ? expr->key.identifier : "unknown";
    size_t key_len = strlen(key_identifier);
    char* key_str = malloc(key_len + 1);
    if (!key_str) {
        return make_error(env, "Memory allocation failed", 0, 0);
    }
    strcpy(key_str, key_identifier);
    
    Value key = make_string_value_from_cstr(env->current_context->state, key_str);
    free(key_str);
    
    // Get the value from the table
    Value result = table_value.as.table->get(key);
    
    env->current_context->push( result);
    return make_success(1);
}
