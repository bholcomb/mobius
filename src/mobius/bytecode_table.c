#include "bytecode_table.h"
#include "bytecode.h"
#include "value.h"
#include "table.h"
#include <stdio.h>
#include <stdlib.h>

// =============================================================================
// TABLE BUILTIN FUNCTIONS FOR BYTECODE VM
// =============================================================================

void builtin_table_insert_vm(MobiusVM* vm, int arg_count, void* result_ptr) {
    Value* result = (Value*)result_ptr;
    if (arg_count != 3) {
        vm_runtime_error(vm, "table_insert expects 3 arguments (table, key, value)");
        *result = make_nil_value();
        return;
    }

    Value table_val = vm_peek(vm, 2);
    Value key = vm_peek(vm, 1);
    Value value = vm_peek(vm, 0);
    
    if (table_val.type != VAL_TABLE) {
        vm_runtime_error(vm, "table_insert first argument must be a table");
        *result = make_nil_value();
        return;
    }

    Table* table = table_val.as.table;
    if (!table_set(table, key, value)) {
        vm_runtime_error(vm, "Failed to insert into table");
        *result = make_nil_value();
        return;
    }

    *result = make_nil_value();
}

void builtin_table_remove_vm(MobiusVM* vm, int arg_count, void* result_ptr) {
    Value* result = (Value*)result_ptr;
    if (arg_count != 2) {
        vm_runtime_error(vm, "table_remove expects 2 arguments (table, key)");
        *result = make_nil_value();
        return;
    }

    Value table_val = vm_peek(vm, 1);
    Value key = vm_peek(vm, 0);
    
    if (table_val.type != VAL_TABLE) {
        vm_runtime_error(vm, "table_remove first argument must be a table");
        *result = make_nil_value();
        return;
    }

    Table* table = table_val.as.table;
    Value old_value = table_get(table, key);
    bool removed = table_remove(table, key);
    
    if (removed) {
        *result = old_value;
    } else {
        *result = make_nil_value();
    }
}

void builtin_table_has_key_vm(MobiusVM* vm, int arg_count, void* result_ptr) {
    Value* result = (Value*)result_ptr;
    if (arg_count != 2) {
        vm_runtime_error(vm, "table_has_key expects 2 arguments (table, key)");
        *result = make_nil_value();
        return;
    }

    Value table_val = vm_peek(vm, 1);
    Value key = vm_peek(vm, 0);
    
    if (table_val.type != VAL_TABLE) {
        vm_runtime_error(vm, "table_has_key first argument must be a table");
        *result = make_nil_value();
        return;
    }

    Table* table = table_val.as.table;
    bool has_key = table_has_key(table, key);
    *result = make_bool_value(has_key);
}

void builtin_table_size_vm(MobiusVM* vm, int arg_count, void* result_ptr) {
    Value* result = (Value*)result_ptr;
    if (arg_count != 1) {
        vm_runtime_error(vm, "table_size expects 1 argument (table)");
        *result = make_nil_value();
        return;
    }

    Value table_val = vm_peek(vm, 0);
    
    if (table_val.type != VAL_TABLE) {
        vm_runtime_error(vm, "table_size argument must be a table");
        *result = make_nil_value();
        return;
    }

    Table* table = table_val.as.table;
    size_t size = table_size(table);
    *result = make_integer_value(NUM_INT64, (int64_t)size);
}

void builtin_setmetatable_vm(MobiusVM* vm, int arg_count, void* result_ptr) {
    Value* result = (Value*)result_ptr;
    if (arg_count != 2) {
        vm_runtime_error(vm, "setmetatable expects 2 arguments (table, metatable)");
        *result = make_nil_value();
        return;
    }

    Value table_val = vm_peek(vm, 1);
    Value metatable_val = vm_peek(vm, 0);
    
    if (table_val.type != VAL_TABLE) {
        vm_runtime_error(vm, "setmetatable first argument must be a table");
        *result = make_nil_value();
        return;
    }

    Table* table = table_val.as.table;
    
    if (metatable_val.type == VAL_NIL) {
        set_metatable(table, NULL);
    } else if (metatable_val.type == VAL_TABLE) {
        set_metatable(table, metatable_val.as.table);
    } else {
        vm_runtime_error(vm, "setmetatable second argument must be a table or nil");
        *result = make_nil_value();
        return;
    }
    
    *result = table_val; // Return the original table
}

void builtin_getmetatable_vm(MobiusVM* vm, int arg_count, void* result_ptr) {
    Value* result = (Value*)result_ptr;
    if (arg_count != 1) {
        vm_runtime_error(vm, "getmetatable expects 1 argument (table)");
        *result = make_nil_value();
        return;
    }

    Value table_val = vm_peek(vm, 0);
    
    if (table_val.type != VAL_TABLE) {
        vm_runtime_error(vm, "getmetatable argument must be a table");
        *result = make_nil_value();
        return;
    }

    Table* table = table_val.as.table;
    Table* metatable = get_metatable(table);
    
    if (metatable) {
        *result = make_table_value(metatable);
    } else {
        *result = make_nil_value();
    }
}

void builtin_pairs_vm(MobiusVM* vm, int arg_count, void* result_ptr) {
    Value* result = (Value*)result_ptr;
    if (arg_count != 1) {
        vm_runtime_error(vm, "pairs expects 1 argument (table)");
        *result = make_nil_value();
        return;
    }

    Value table_val = vm_peek(vm, 0);
    
    if (table_val.type != VAL_TABLE) {
        vm_runtime_error(vm, "pairs argument must be a table");
        *result = make_nil_value();
        return;
    }

    Table* table = table_val.as.table;
    
    // Create a table to hold the array of pairs
    Table* pairs_table = create_table(table->size * 2);
    if (!pairs_table) {
        vm_runtime_error(vm, "Failed to create pairs table");
        *result = make_nil_value();
        return;
    }
    
    // Iterate through all entries and create [key, value] pairs
    size_t pair_index = 0;
    for (size_t i = 0; i < table->capacity; i++) {
        if (table->entries[i].is_occupied) {
            // Create a sub-table for this [key, value] pair
            Table* pair = create_table(2);
            if (!pair) {
                free_table(pairs_table);
                vm_runtime_error(vm, "Failed to create pair entry");
                *result = make_nil_value();
                return;
            }
            
            // Set key as index 0, value as index 1
            Value key_index = make_integer_value(NUM_INT64, 0);
            Value value_index = make_integer_value(NUM_INT64, 1);
            
            if (!table_set(pair, key_index, copy_value(table->entries[i].key)) ||
                !table_set(pair, value_index, copy_value(table->entries[i].value))) {
                free_table(pair);
                free_table(pairs_table);
                vm_runtime_error(vm, "Failed to set pair values");
                *result = make_nil_value();
                return;
            }
            
            // Add this pair to the main pairs table
            Value pair_index_val = make_integer_value(NUM_INT64, (int64_t)pair_index);
            if (!table_set(pairs_table, pair_index_val, make_table_value(pair))) {
                free_table(pair);
                free_table(pairs_table);
                vm_runtime_error(vm, "Failed to add pair to result");
                *result = make_nil_value();
                return;
            }
            
            pair_index++;
        }
    }
    
    *result = make_table_value(pairs_table);
}