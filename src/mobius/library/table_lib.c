#include "library/table_lib.h"
#include "data/table.h"
#include "data/array.h"
#include "data/value.h"
#include "state/environment.h"
#include "eval/evaluator.h"

#include <stdio.h>
#include <stdlib.h>

// =============================================================================
// UNIFIED TABLE FUNCTION IMPLEMENTATIONS
// =============================================================================

EvalResult lib_table_insert(MobiusState* state, int arg_count) {
    if (arg_count != 3) {
        return make_error(state->main_context->current_env, "table_insert expects exactly 3 arguments (table, key, value)", 0, 0);
    }
    
    Value value = ctx_peek(state->main_context, 0);
    Value key = ctx_peek(state->main_context, 1);
    Value table_val = ctx_peek(state->main_context, 2);
    
    // Remove arguments
    for (int i = 0; i < 3; i++) {
        ctx_pop(state->main_context);
    }
    
    if (table_val.type != VAL_TABLE) {
        return make_error(state->main_context->current_env, "table_insert first argument must be a table", 0, 0);
    }
    
    Table* table = table_val.as.table;
    if (!table_set(table, key, value)) {
        return make_error(state->main_context->current_env, "Failed to insert into table", 0, 0);
    }
    
    ctx_push(state->main_context, make_nil_value());
    return make_success(1);
}

EvalResult lib_table_remove(MobiusState* state, int arg_count) {
    if (arg_count != 2) {
        return make_error(state->main_context->current_env, "table_remove expects exactly 2 arguments (table, key)", 0, 0);
    }
    
    Value key = ctx_peek(state->main_context, 0);
    Value table_val = ctx_peek(state->main_context, 1);
    
    // Remove arguments
    ctx_pop(state->main_context);
    ctx_pop(state->main_context);
    
    if (table_val.type != VAL_TABLE) {
        return make_error(state->main_context->current_env, "table_remove first argument must be a table", 0, 0);
    }
    
    Table* table = table_val.as.table;
    bool success = table_remove(table, key);
    if (success) {
        ctx_push(state->main_context, make_bool_value(true));
    } else {
        ctx_push(state->main_context, make_bool_value(false));
    }
    
    return make_success(1);
}

EvalResult lib_table_has_key(MobiusState* state, int arg_count) {
    if (arg_count != 2) {
        return make_error(state->main_context->current_env, "table_has_key expects exactly 2 arguments (table, key)", 0, 0);
    }
    
    Value key = ctx_peek(state->main_context, 0);
    Value table_val = ctx_peek(state->main_context, 1);
    
    // Remove arguments
    ctx_pop(state->main_context);
    ctx_pop(state->main_context);
    
    if (table_val.type != VAL_TABLE) {
        return make_error(state->main_context->current_env, "table_has_key first argument must be a table", 0, 0);
    }
    
    Table* table = table_val.as.table;
    bool has_key = table_has_key(table, key);
    
    ctx_push(state->main_context, make_bool_value(has_key));
    return make_success(1);
}

EvalResult lib_table_size(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "table_size expects exactly 1 argument", 0, 0);
    }
    
    Value table_val = ctx_peek(state->main_context, 0);
    ctx_pop(state->main_context); // Remove argument
    
    if (table_val.type != VAL_TABLE) {
        return make_error(state->main_context->current_env, "table_size argument must be a table", 0, 0);
    }
    
    Table* table = table_val.as.table;
    size_t size = table_size(table);
    
    ctx_push(state->main_context, make_integer_value(NUM_INT64, (int64_t)size));
    return make_success(1);
}

EvalResult lib_setmetatable(MobiusState* state, int arg_count) {
    if (arg_count != 2) {
        return make_error(state->main_context->current_env, "setmetatable expects exactly 2 arguments (table, metatable)", 0, 0);
    }
    
    Value metatable_val = ctx_peek(state->main_context, 0);
    Value table_val = ctx_peek(state->main_context, 1);
    
    // Remove arguments
    ctx_pop(state->main_context);
    ctx_pop(state->main_context);
    
    if (table_val.type != VAL_TABLE) {
        return make_error(state->main_context->current_env, "setmetatable first argument must be a table", 0, 0);
    }
    
    if (metatable_val.type != VAL_TABLE && metatable_val.type != VAL_NIL) {
        return make_error(state->main_context->current_env, "setmetatable second argument must be a table or nil", 0, 0);
    }
    
    Table* table = table_val.as.table;
    Table* metatable = (metatable_val.type == VAL_TABLE) ? metatable_val.as.table : NULL;
    
    set_metatable(table, metatable);
    
    ctx_push(state->main_context, table_val); // Return the original table
    return make_success(1);
}

EvalResult lib_getmetatable(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "getmetatable expects exactly 1 argument", 0, 0);
    }
    
    Value table_val = ctx_peek(state->main_context, 0);
    ctx_pop(state->main_context); // Remove argument
    
    if (table_val.type != VAL_TABLE) {
        return make_error(state->main_context->current_env, "getmetatable argument must be a table", 0, 0);
    }
    
    Table* table = table_val.as.table;
    Table* metatable = get_metatable(table);
    
    if (metatable) {
        ctx_push(state->main_context, make_table_value(metatable));
    } else {
        ctx_push(state->main_context, make_nil_value());
    }
    
    return make_success(1);
}

EvalResult lib_pairs(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return make_error(state->main_context->current_env, "pairs expects exactly 1 argument", 0, 0);
    }
    
    Value table_val = ctx_peek(state->main_context, 0);
    ctx_pop(state->main_context); // Remove argument
    
    if (table_val.type != VAL_TABLE) {
        return make_error(state->main_context->current_env, "pairs argument must be a table", 0, 0);
    }
    
    Table* table = table_val.as.table;
    if (!table) {
        return make_error(state->main_context->current_env, "pairs argument is null table", 0, 0);
    }
    
    // Create an array to hold key-value pairs
    ArrayValue* pairs_array = array_create(table->size);
    if (!pairs_array) {
        return make_error(state->main_context->current_env, "Failed to create pairs array", 0, 0);
    }
    
    // Iterate through table and create [key, value] pairs
    size_t pair_index = 0;
    for (size_t i = 0; i < table->capacity; i++) {
        if (table->entries[i].is_occupied) {
            // Create a sub-array for this key-value pair
            ArrayValue* pair = array_create(2);
            if (!pair) {
                array_release(pairs_array);
                return make_error(state->main_context->current_env, "Failed to create key-value pair array", 0, 0);
            }
            
            // Set key and value in the pair array
            pair->elements[0] = copy_value(table->entries[i].key);
            pair->elements[1] = copy_value(table->entries[i].value);
            pair->length = 2;
            
            // Add this pair to the main pairs array
            pairs_array->elements[pair_index] = make_array_value(pair);
            pair_index++;
        }
    }
    pairs_array->length = pair_index;
    
    // Push the pairs array onto the stack
    ctx_push(state->main_context, make_array_value(pairs_array));
    return make_success(1);
}