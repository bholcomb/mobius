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

int lib_table_remove(MobiusState* state, int arg_count) {
    if (arg_count != 2) {
        return state->error("table_remove expects exactly 2 arguments (table, key)");
    }
    
    Value key = state->mainContext()->peek( 0);
    Value table_val = state->mainContext()->peek( 1);
    
    // Remove arguments
    state->mainContext()->pop();
    state->mainContext()->pop();
    
    if (table_val.type != VAL_TABLE) {
        return state->error("table_remove first argument must be a table");
    }
    
    Table* table = table_val.as.table;
    bool success = table->remove(key);
    
    if (success) {
        state->mainContext()->push( make_bool_value(true));
    } else {
        state->mainContext()->push( make_bool_value(false));
    }
    
    return 1;
}

int lib_table_has_key(MobiusState* state, int arg_count) {
    if (arg_count != 2) {
        return state->error("table_has_key expects exactly 2 arguments (table, key)");
    }
    
    Value key = state->mainContext()->peek( 0);
    Value table_val = state->mainContext()->peek( 1);
    
    // Remove arguments
    state->mainContext()->pop();
    state->mainContext()->pop();
    
    if (table_val.type != VAL_TABLE) {
        return state->error("table_has_key first argument must be a table");
    }
    
    Table* table = table_val.as.table;
    bool has_key = table->hasKey(key);
    
    state->mainContext()->push( make_bool_value(has_key));
    return 1;
}

int lib_table_size(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("table_size expects exactly 1 argument");
    }
    
    Value table_val = state->mainContext()->peek( 0);
    state->mainContext()->pop(); // Remove argument
    
    if (table_val.type != VAL_TABLE) {
        return state->error("table_size argument must be a table");
    }
    
    Table* table = table_val.as.table;
    size_t size = table->size();
    
    state->mainContext()->push( make_integer_value(NUM_INT64, (int64_t)size));
    return 1;
}

int lib_setmetatable(MobiusState* state, int arg_count) {
    if (arg_count != 2) {
        return state->error("setmetatable expects exactly 2 arguments (table, metatable)");
    }
    
    Value metatable_val = state->mainContext()->peek( 0);
    Value table_val = state->mainContext()->peek( 1);
    
    // Remove arguments
    state->mainContext()->pop();
    state->mainContext()->pop();
    
    if (table_val.type != VAL_TABLE) {
        return state->error("setmetatable first argument must be a table");
    }
    
    if (metatable_val.type != VAL_TABLE && metatable_val.type != VAL_NIL) {
        return state->error("setmetatable second argument must be a table or nil");
    }
    
    Table* table = table_val.as.table;
    Table* metatable = (metatable_val.type == VAL_TABLE) ? metatable_val.as.table : NULL;
    
    table->setMetatable(metatable);
    
    state->mainContext()->push( table_val); // Return the original table
    return 1;
}

int lib_getmetatable(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("getmetatable expects exactly 1 argument");
    }
    
    Value table_val = state->mainContext()->peek( 0);
    state->mainContext()->pop(); // Remove argument
    
    if (table_val.type != VAL_TABLE) {
        return state->error("getmetatable argument must be a table");
    }
    
    Table* table = table_val.as.table;
    Table* metatable = table->getMetatable();
    
    if (metatable) {
        metatable->retain();
        state->mainContext()->push( make_table_value(metatable));
    } else {
        state->mainContext()->push( make_nil_value());
    }
    
    return 1;
}

int lib_pairs(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("pairs expects exactly 1 argument");
    }
    
    Value table_val = state->mainContext()->peek( 0);
    state->mainContext()->pop(); // Remove argument
    
    if (table_val.type != VAL_TABLE) {
        return state->error("pairs argument must be a table");
    }
    
    Table* table = table_val.as.table;
    if (!table) {
        return state->error("pairs argument is null table");
    }
    
    ArrayValue* pairs_array = new ArrayValue(table->size());
    if (!pairs_array) {
        return state->error("Failed to create pairs array");
    }
    
    bool alloc_failed = false;
    table->forEach([&](const Value& key, const Value& value) {
        if (alloc_failed) return;
        ArrayValue* pair = new ArrayValue(2);
        if (!pair) {
            alloc_failed = true;
            return;
        }
        pair->push(key);
        pair->push(value);
        pairs_array->push(make_array_value(pair));
    });
    
    if (alloc_failed) {
        pairs_array->release();
        return state->error("Failed to create key-value pair array");
    }
    
    // Push the pairs array onto the stack
    state->mainContext()->push( make_array_value(pairs_array));
    return 1;
}