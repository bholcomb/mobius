#include "table.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// Hash functions for different value types
size_t hash_string_for_table(const char* str) {
    size_t hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c; // hash * 33 + c
    }
    return hash;
}

size_t hash_integer(int64_t value) {
    // Simple hash for integers
    return (size_t)(value ^ (value >> 32));
}

size_t hash_float(double value) {
    // Hash the bit representation of the float
    union { double d; uint64_t i; } u;
    u.d = value;
    return (size_t)(u.i ^ (u.i >> 32));
}

size_t hash_value(Value value, size_t capacity) {
    size_t hash = 0;
    
    switch (value.type) {
        case VAL_NIL:
            hash = 0;
            break;
        case VAL_BOOL:
            hash = value.as.boolean ? 1 : 0;
            break;
        case VAL_INTEGER:
            hash = hash_integer(value.as.integer.value.i64);
            break;
        case VAL_FLOAT:
            hash = hash_float(value.as.float_val);
            break;
        case VAL_STRING:
            hash = value.as.string ? hash_string_for_table(value.as.string) : 0;
            break;
        case VAL_CHAR:
            hash = (size_t)value.as.character;
            break;
        case VAL_FUNCTION:
            hash = (size_t)(uintptr_t)value.as.function;
            break;
        case VAL_TABLE:
            hash = (size_t)(uintptr_t)value.as.table;
            break;
    }
    
    return hash % capacity;
}

bool values_equal_for_table(Value a, Value b) {
    if (a.type != b.type) return false;
    
    switch (a.type) {
        case VAL_NIL: return true;
        case VAL_BOOL: return a.as.boolean == b.as.boolean;
        case VAL_INTEGER:
            return a.as.integer.value.i64 == b.as.integer.value.i64;
        case VAL_FLOAT: return a.as.float_val == b.as.float_val;
        case VAL_STRING:
            if (!a.as.string || !b.as.string) return a.as.string == b.as.string;
            return strcmp(a.as.string, b.as.string) == 0;
        case VAL_CHAR: return a.as.character == b.as.character;
        case VAL_FUNCTION: return a.as.function == b.as.function;
        case VAL_TABLE: return a.as.table == b.as.table;
    }
    return false;
}

TableEntry* find_table_entry(TableEntry* entries, size_t capacity, Value key) {
    size_t index = hash_value(key, capacity);
    
    while (entries[index].is_occupied) {
        if (values_equal_for_table(entries[index].key, key)) {
            return &entries[index];
        }
        index = (index + 1) % capacity; // Linear probing
    }
    
    return &entries[index];
}

void table_insert_entry(TableEntry* entries, size_t capacity, Value key, Value value) {
    TableEntry* entry = find_table_entry(entries, capacity, key);
    
    if (!entry->is_occupied) {
        entry->key = copy_value(key);
        entry->is_occupied = true;
    } else {
        // Update existing entry - free old value
        free_value(entry->value);
    }
    
    entry->value = copy_value(value);
}

Table* create_table(size_t initial_capacity) {
    if (initial_capacity < INITIAL_TABLE_CAPACITY) {
        initial_capacity = INITIAL_TABLE_CAPACITY;
    }
    
    Table* table = malloc(sizeof(Table));
    if (!table) return NULL;
    
    table->entries = calloc(initial_capacity, sizeof(TableEntry));
    if (!table->entries) {
        free(table);
        return NULL;
    }
    
    table->size = 0;
    table->capacity = initial_capacity;
    table->metatable = NULL;
    table->ref_count = 1;
    
    return table;
}

void free_table(Table* table) {
    if (!table) return;
    
    // Free all entries
    for (size_t i = 0; i < table->capacity; i++) {
        if (table->entries[i].is_occupied) {
            free_value(table->entries[i].key);
            free_value(table->entries[i].value);
        }
    }
    
    free(table->entries);
    
    // Don't free metatable - it's managed separately
    
    free(table);
}

void table_resize(Table* table, size_t new_capacity) {
    if (!table || new_capacity <= table->capacity) return;
    
    TableEntry* old_entries = table->entries;
    size_t old_capacity = table->capacity;
    
    // Allocate new entries array
    table->entries = calloc(new_capacity, sizeof(TableEntry));
    if (!table->entries) {
        table->entries = old_entries; // Restore on failure
        return;
    }
    
    table->capacity = new_capacity;
    size_t old_size = table->size;
    table->size = 0;
    
    // Rehash all existing entries
    for (size_t i = 0; i < old_capacity; i++) {
        if (old_entries[i].is_occupied) {
            table_insert_entry(table->entries, table->capacity, 
                             old_entries[i].key, old_entries[i].value);
            table->size++;
            
            // Free the old entry values since they were copied
            free_value(old_entries[i].key);
            free_value(old_entries[i].value);
        }
    }
    
    free(old_entries);
}

Value table_get(Table* table, Value key) {
    if (!table || table->size == 0) {
        return make_nil_value();
    }
    
    TableEntry* entry = find_table_entry(table->entries, table->capacity, key);
    if (entry->is_occupied) {
        return entry->value;
    }
    
    // Check metatable for __index
    if (table->metatable) {
        char* index_str = malloc(8);
        if (index_str) {
            strcpy(index_str, "__index");
            Value index_method = table_get(table->metatable, make_string_value(index_str));
            if (index_method.type == VAL_FUNCTION) {
                // TODO: Call metamethod with table and key
            } else if (index_method.type == VAL_TABLE) {
                return table_get(index_method.as.table, key);
            }
        }
    }
    
    return make_nil_value();
}

bool table_set(Table* table, Value key, Value value) {
    if (!table) return false;
    
    // Check if we need to resize
    if ((double)table->size / table->capacity >= LOAD_FACTOR_THRESHOLD) {
        table_resize(table, table->capacity * 2);
    }
    
    TableEntry* entry = find_table_entry(table->entries, table->capacity, key);
    bool is_new_key = !entry->is_occupied;
    
    if (is_new_key) {
        // Check metatable for __newindex
        if (table->metatable) {
            char* newindex_str = malloc(11);
            if (newindex_str) {
                strcpy(newindex_str, "__newindex");
                Value newindex_method = table_get(table->metatable, make_string_value(newindex_str));
                if (newindex_method.type == VAL_FUNCTION) {
                    // TODO: Call metamethod with table, key, and value
                    return true;
                } else if (newindex_method.type == VAL_TABLE) {
                    return table_set(newindex_method.as.table, key, value);
                }
            }
        }
        table->size++;
    }
    
    table_insert_entry(table->entries, table->capacity, key, value);
    return true;
}

bool table_has_key(Table* table, Value key) {
    if (!table || table->size == 0) return false;
    
    TableEntry* entry = find_table_entry(table->entries, table->capacity, key);
    return entry->is_occupied;
}

bool table_remove(Table* table, Value key) {
    if (!table || table->size == 0) return false;
    
    TableEntry* entry = find_table_entry(table->entries, table->capacity, key);
    if (!entry->is_occupied) return false;
    
    // Free the entry
    free_value(entry->key);
    free_value(entry->value);
    entry->is_occupied = false;
    table->size--;
    
    // Rehash following entries to maintain probe sequence
    size_t index = entry - table->entries;
    for (;;) {
        index = (index + 1) % table->capacity;
        
        if (!table->entries[index].is_occupied) break;
        
        Value key_to_move = table->entries[index].key;
        Value value_to_move = table->entries[index].value;
        
        table->entries[index].is_occupied = false;
        table->size--;
        
        table_set(table, key_to_move, value_to_move);
    }
    
    return true;
}

size_t table_size(Table* table) {
    return table ? table->size : 0;
}

Table* table_copy(Table* source) {
    if (!source) return NULL;
    
    Table* copy = create_table(source->capacity);
    if (!copy) return NULL;
    
    // Copy all entries
    for (size_t i = 0; i < source->capacity; i++) {
        if (source->entries[i].is_occupied) {
            table_set(copy, source->entries[i].key, source->entries[i].value);
        }
    }
    
    // Copy metatable reference
    copy->metatable = source->metatable;
    
    return copy;
}

void set_metatable(Table* table, Table* metatable) {
    if (!table) return;
    table->metatable = metatable;
}

Table* get_metatable(Table* table) {
    return table ? table->metatable : NULL;
}

void print_table(Table* table) {
    if (!table) {
        printf("(null table)");
        return;
    }
    
    printf("{");
    bool first = true;
    
    for (size_t i = 0; i < table->capacity; i++) {
        if (table->entries[i].is_occupied) {
            if (!first) printf(", ");
            first = false;
            
            // Print key
            if (table->entries[i].key.type == VAL_STRING) {
                printf("%s", table->entries[i].key.as.string);
            } else {
                printf("[");
                print_value(table->entries[i].key);
                printf("]");
            }
            
            printf(": ");
            print_value(table->entries[i].value);
        }
    }
    
    printf("}");
}

void print_table_debug(Table* table) {
    if (!table) {
        printf("(null table)\n");
        return;
    }
    
    printf("Table (size: %zu, capacity: %zu, ref_count: %d)\n", 
           table->size, table->capacity, table->ref_count);
    
    for (size_t i = 0; i < table->capacity; i++) {
        printf("[%zu] ", i);
        if (table->entries[i].is_occupied) {
            printf("Key: ");
            print_value(table->entries[i].key);
            printf(" => Value: ");
            print_value(table->entries[i].value);
            printf("\n");
        } else {
            printf("(empty)\n");
        }
    }
}
