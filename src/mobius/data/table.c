#include "data/table.h"
#include "internal/string_intern.h"
#include "state/mobius_state.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

// Hash table configuration
#define LOAD_FACTOR_THRESHOLD 0.75

// Helper function to round up to next power of 2
static size_t next_power_of_2(size_t n) {
    if (n <= 1) return 1;
    
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    if (sizeof(size_t) > 4) {
        n |= n >> 32;
    }
    n++;
    
    return n;
}

// Hash functions for different value types
size_t hash_string_for_table(const char* str) {
    // FNV-1a hash - better distribution than djb2
    size_t hash = 14695981039346656037ULL; // FNV offset basis
    while (*str) {
        hash ^= (unsigned char)*str++;
        hash *= 1099511628211ULL; // FNV prime
    }
    return hash;
}

size_t hash_integer(int64_t value) {
    // Improved integer hashing (based on splitmix64)
    uint64_t v = (uint64_t)value;
    v ^= v >> 30;
    v *= 0xbf58476d1ce4e5b9ULL;
    v ^= v >> 27;
    v *= 0x94d049bb133111ebULL;
    v ^= v >> 31;
    return (size_t)v;
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
        case VAL_FLOAT32:
            hash = hash_float((double)value.as.float32_val);
            break;
        case VAL_FLOAT64:
            hash = hash_float(value.as.float64_val);
            break;
        case VAL_STRING:
            // Use precomputed hash from MobiusString (much faster!)
            hash = value.as.string ? string_hash(value.as.string) : 0;
            break;
        case VAL_CHAR:
            hash = (size_t)value.as.character;
            break;
        case VAL_ARRAY:
            // Hash arrays by their pointer address (reference equality)
            hash = (size_t)(uintptr_t)value.as.array;
            break;
        case VAL_FUNCTION:
            hash = (size_t)(uintptr_t)value.as.function;
            break;
        case VAL_NATIVE_FUNCTION:
            hash = (size_t)(uintptr_t)value.as.native_function;
            break;
        case VAL_TABLE:
            hash = (size_t)(uintptr_t)value.as.table;
            break;
        case VAL_USERDATA:
            // Hash userdata by pointer address and type name
            hash = (size_t)(uintptr_t)value.as.userdata.ptr;
            if (value.as.userdata.type_name) {
                hash ^= hash_string_for_table(value.as.userdata.type_name);
            }
            break;
        case VAL_ENUM:
            // Hash enum by definition pointer and value
            hash = (size_t)(uintptr_t)value.as.enum_val.definition;
            hash ^= (size_t)value.as.enum_val.value;
            break;
    }
    
    // Use bitwise AND for power-of-2 capacities (faster than modulo)
    return hash & (capacity - 1);
}

bool values_equal_for_table(Value a, Value b) {
    if (a.type != b.type) return false;
    
    switch (a.type) {
        case VAL_NIL: return true;
        case VAL_BOOL: return a.as.boolean == b.as.boolean;
        case VAL_INTEGER:
            return a.as.integer.value.i64 == b.as.integer.value.i64;
        case VAL_FLOAT32: return a.as.float32_val == b.as.float32_val;
        case VAL_FLOAT64: return a.as.float64_val == b.as.float64_val;
        case VAL_STRING:
            return string_equals(a.as.string, b.as.string);
        case VAL_CHAR: return a.as.character == b.as.character;
        case VAL_ARRAY: return a.as.array == b.as.array;  // Reference equality
        case VAL_FUNCTION: return a.as.function == b.as.function;
        case VAL_NATIVE_FUNCTION: return a.as.native_function == b.as.native_function;
        case VAL_TABLE: return a.as.table == b.as.table;
        case VAL_USERDATA: 
            // Userdata equality: same pointer AND same type
            return a.as.userdata.ptr == b.as.userdata.ptr && 
                   a.as.userdata.type_name == b.as.userdata.type_name;
        case VAL_ENUM:
            // Enum equality: same definition and same value
            return a.as.enum_val.definition == b.as.enum_val.definition &&
                   a.as.enum_val.value == b.as.enum_val.value;
    }
    return false;
}

TableEntry* find_table_entry(TableEntry* entries, size_t capacity, Value key) {
    size_t index = hash_value(key, capacity);
    size_t start_index = index;
    
    do {
        if (!entries[index].is_occupied) {
            // Found empty slot
            return &entries[index];
        }
        
        if (values_equal_for_table(entries[index].key, key)) {
            // Found matching key
            return &entries[index];
        }
        
        // Linear probe to next slot
        index = (index + 1) & (capacity - 1);
    } while (index != start_index);
    
    // Table is full (this shouldn't happen with proper load factor management)
    return &entries[start_index];
}

void table_insert_entry(TableEntry* entries, size_t capacity, Value key, Value value) {
    size_t index = hash_value(key, capacity);
    size_t start_index = index;
    
    do {
        if (!entries[index].is_occupied) {
            // Found empty slot, insert here
            entries[index].key = copy_value(key);
            entries[index].value = copy_value(value);
            entries[index].is_occupied = true;
            return;
        }
        
        if (values_equal_for_table(entries[index].key, key)) {
            // Found existing key, update value
            free_value(entries[index].value);
            entries[index].value = copy_value(value);
            return;
        }
        
        // Linear probe to next slot
        index = (index + 1) & (capacity - 1);
    } while (index != start_index);
    
    // Table is full - this shouldn't happen with proper load factor management
    // For now, just overwrite the first slot we tried
    free_value(entries[start_index].value);
    entries[start_index].key = copy_value(key);
    entries[start_index].value = copy_value(value);
    entries[start_index].is_occupied = true;
}

Table* create_table(struct MobiusState* state, size_t initial_capacity) {
    if (initial_capacity < INITIAL_TABLE_CAPACITY) {
        initial_capacity = INITIAL_TABLE_CAPACITY;
    }
    
    // Ensure capacity is a power of 2 for efficient bitwise operations
    initial_capacity = next_power_of_2(initial_capacity);
    
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
    table->state = state;  // Store back-reference to state
    
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
    
    // Ensure new capacity is a power of 2
    new_capacity = next_power_of_2(new_capacity);
    
    TableEntry* old_entries = table->entries;
    size_t old_capacity = table->capacity;
    
    // Allocate new entries array
    table->entries = calloc(new_capacity, sizeof(TableEntry));
    if (!table->entries) {
        table->entries = old_entries; // Restore on failure
        return;
    }
    
    table->capacity = new_capacity;
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
    if (entry->is_occupied && values_equal_for_table(entry->key, key)) {
        return copy_value(entry->value);
    }
    
    // Check metatable for __index
    if (table->metatable && table->state) {
        Value index_method = get_table_metamethod(table, table->state->mm_index);
        if (index_method.type == VAL_FUNCTION) {
            // TODO: Call metamethod with table and key
            // For now, fall through to return nil
        } else if (index_method.type == VAL_TABLE) {
            return table_get(index_method.as.table, key);
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
        if (table->metatable && table->state) {
            Value newindex_method = get_table_metamethod(table, table->state->mm_newindex);
            if (newindex_method.type == VAL_FUNCTION) {
                // TODO: Call metamethod with table, key, and value
                return true;
            } else if (newindex_method.type == VAL_TABLE) {
                return table_set(newindex_method.as.table, key, value);
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
    return entry->is_occupied && values_equal_for_table(entry->key, key);
}

bool table_remove(Table* table, Value key) {
    if (!table || table->size == 0) return false;
    
    TableEntry* entry = find_table_entry(table->entries, table->capacity, key);
    if (!entry->is_occupied || !values_equal_for_table(entry->key, key)) return false;
    
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
    
    Table* copy = create_table(source->state, source->capacity);
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

void print_table_safe(Table* table, Table** visited, int* visited_count, int max_depth) {
    if (!table) {
        printf("(null table)");
        return;
    }
    
    // Check for circular reference
    for (int i = 0; i < *visited_count; i++) {
        if (visited[i] == table) {
            printf("{...circular...}");
            return;
        }
    }
    
    // Check max depth
    if (*visited_count >= max_depth) {
        printf("{...depth limit...}");
        return;
    }
    
    // Add to visited list
    visited[*visited_count] = table;
    (*visited_count)++;
    
    printf("{");
    bool first = true;
    
    for (size_t i = 0; i < table->capacity; i++) {
        if (table->entries[i].is_occupied) {
            if (!first) printf(", ");
            first = false;
            
            // Print key
            if (table->entries[i].key.type == VAL_STRING) {
                // Check if the string looks like a valid identifier
                const char* key_str = string_data(table->entries[i].key.as.string);
                bool is_identifier = key_str && key_str[0] && 
                    (isalpha(key_str[0]) || key_str[0] == '_');
                
                if (is_identifier) {
                    // Check if all characters are valid identifier characters
                    for (const char* c = key_str + 1; *c; c++) {
                        if (!isalnum(*c) && *c != '_') {
                            is_identifier = false;
                            break;
                        }
                    }
                }
                
                if (is_identifier) {
                    printf("%s", key_str);  // Print as identifier
                } else {
                    printf("[%s]", key_str);  // Print as string key
                }
            } else {
                printf("[");
                // For non-string keys, check if they are tables too
                if (table->entries[i].key.type == VAL_TABLE) {
                    print_table_safe(table->entries[i].key.as.table, visited, visited_count, max_depth);
                } else {
                    print_value(table->entries[i].key);
                }
                printf("]");
            }
            
            printf(": ");
            // For values, use safe printing for nested tables
            if (table->entries[i].value.type == VAL_TABLE) {
                print_table_safe(table->entries[i].value.as.table, visited, visited_count, max_depth);
            } else {
                print_value(table->entries[i].value);
            }
        }
    }
    
    printf("}");
    
    // Remove from visited list
    (*visited_count)--;
}

void print_table(Table* table) {
    // Use safe printing with reasonable limits
    Table* visited[100];  // Allow up to 100 nested tables
    int visited_count = 0;
    print_table_safe(table, visited, &visited_count, 100);
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

// =============================================================================
// BASIC METAMETHOD SUPPORT
// =============================================================================

// Validate metamethod name (must start with __)
const char* get_metamethod_name(const char* name) {
    if (name && (strncmp(name, "__", 2) == 0)) {
        return name;
    }
    return NULL;
}

// Check if table has a specific metamethod
bool has_table_metamethod(Table* table, MobiusString* method_name) {
    if (!table || !table->metatable || !method_name) {
        return false;
    }
    
    Value method = table_get(table->metatable, make_string_value(method_name));
    return method.type != VAL_NIL;
}

// Get a metamethod from table's metatable
Value get_table_metamethod(Table* table, MobiusString* method_name) {
    if (!table || !table->metatable || !method_name) {
        return make_nil_value();
    }
    
    return table_get(table->metatable, make_string_value(method_name));
}
