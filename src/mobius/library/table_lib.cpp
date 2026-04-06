#include "library/table_lib.h"
#include "data/table.h"
#include "data/array.h"
#include "data/value.h"
#include "state/mobius_state.h"

#include <stdio.h>
#include <stdlib.h>

// =============================================================================
// HELPER: extract Table* from self (first argument via : syntax)
// =============================================================================

static Table* extract_table_self(MobiusState* state, const char* err_msg) {
    const Value& self = state->npeek_self();
    if (self.type != VAL_TABLE || !self.as.table) {
        state->error(err_msg);
        return nullptr;
    }
    return self.as.table;
}

// =============================================================================
// METHOD-STYLE TABLE FUNCTIONS (called via tbl:method() with self at base)
// =============================================================================

int table_method_remove(MobiusState* state, int arg_count) {
    if (arg_count != 2) return state->error("tbl:remove expects 1 argument (key)");

    Table* tbl = extract_table_self(state, "tbl:remove: self is not a table");
    if (!tbl) return -1;

    Value key = state->npop();
    state->npop();

    bool success = tbl->remove(key);
    state->npush(make_bool_value(success));
    return 1;
}

int table_method_has_key(MobiusState* state, int arg_count) {
    if (arg_count != 2) return state->error("tbl:has_key expects 1 argument (key)");

    Table* tbl = extract_table_self(state, "tbl:has_key: self is not a table");
    if (!tbl) return -1;

    Value key = state->npop();
    state->npop();

    state->npush(make_bool_value(tbl->hasKey(key)));
    return 1;
}

int table_method_size(MobiusState* state, int arg_count) {
    if (arg_count != 1) return state->error("tbl:size expects 0 arguments");

    Table* tbl = extract_table_self(state, "tbl:size: self is not a table");
    if (!tbl) return -1;

    state->npop();

    state->npush(make_int64_value((int64_t)tbl->size()));
    return 1;
}

int table_method_pairs(MobiusState* state, int arg_count) {
    if (arg_count != 1) return state->error("tbl:pairs expects 0 arguments");

    Table* tbl = extract_table_self(state, "tbl:pairs: self is not a table");
    if (!tbl) return -1;

    state->npop();

    ArrayValue* pairs_array = new ArrayValue(tbl->size());
    if (!pairs_array) {
        return state->error("Failed to create pairs array");
    }

    bool alloc_failed = false;
    tbl->forEach([&](const Value& key, const Value& value) {
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

    state->npush(make_array_value(pairs_array));
    return 1;
}

// =============================================================================
// GLOBAL TABLE FUNCTIONS (remain as globals)
// =============================================================================

int lib_setmetatable(MobiusState* state, int arg_count) {
    if (arg_count != 2) {
        return state->error("setmetatable expects exactly 2 arguments (table, metatable)");
    }
    
    Value metatable_val = state->npeek(0);
    Value table_val = state->npeek(1);
    
    state->npop();
    state->npop();
    
    if (table_val.type != VAL_TABLE) {
        return state->error("setmetatable first argument must be a table");
    }
    
    if (metatable_val.type != VAL_TABLE && metatable_val.type != VAL_NIL) {
        return state->error("setmetatable second argument must be a table or nil");
    }
    
    Table* table = table_val.as.table;
    Table* metatable = (metatable_val.type == VAL_TABLE) ? metatable_val.as.table : NULL;
    
    table->setMetatable(metatable);
    
    state->npush(table_val);
    return 1;
}

int lib_getmetatable(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("getmetatable expects exactly 1 argument");
    }
    
    Value table_val = state->npeek(0);
    state->npop();
    
    if (table_val.type != VAL_TABLE) {
        return state->error("getmetatable argument must be a table");
    }
    
    Table* table = table_val.as.table;
    Table* metatable = table->getMetatable();
    
    if (metatable) {
        metatable->retain();
        state->npush(make_table_value(metatable));
    } else {
        state->npush(make_nil_value());
    }
    
    return 1;
}

// =============================================================================
// TYPE METATABLE BUILDER
// =============================================================================

Table* create_table_type_metatable(MobiusState* state) {
    Table* mt = new Table(state, 8);
    mt->setByString(state->stringPool()->intern("remove"),  make_native_function_value(table_method_remove));
    mt->setByString(state->stringPool()->intern("has_key"), make_native_function_value(table_method_has_key));
    mt->setByString(state->stringPool()->intern("size"),    make_native_function_value(table_method_size));
    mt->setByString(state->stringPool()->intern("pairs"),   make_native_function_value(table_method_pairs));
    return mt;
}
