#ifndef MOBIUS_TABLE_H
#define MOBIUS_TABLE_H

#include "data/value.h"
#include <stdbool.h>
#include <stddef.h>

#define INITIAL_TABLE_CAPACITY 8

// Table entry for hash table
typedef struct TableEntry {
    Value key;
    Value value;
    bool is_occupied;
} TableEntry;

// Forward declaration
struct MobiusState;

// Table structure - pure hash table
typedef struct Table {
    TableEntry* entries;     // Hash table entries
    size_t size;             // Number of key-value pairs
    size_t capacity;         // Size of entries array
    struct Table* metatable; // For operator overloading
    int ref_count;           // Reference counting for memory management
    struct MobiusState* state; // Back-reference to owning state (for string interning)
} Table;

// Table function declarations
Table* create_table(struct MobiusState* state, size_t initial_capacity);
void free_table(Table* table);
Table* table_copy(Table* source);

// Table operations
Value table_get(Table* table, Value key);
bool table_set(Table* table, Value key, Value value);
bool table_has_key(Table* table, Value key);
bool table_remove(Table* table, Value key);
size_t table_size(Table* table);

// Hash functions
size_t hash_value(Value value, size_t capacity);
bool values_equal_for_table(Value a, Value b);

// Table resizing
void table_resize(Table* table, size_t new_capacity);

// Table entry management
TableEntry* find_table_entry(TableEntry* entries, size_t capacity, Value key);
void table_insert_entry(TableEntry* entries, size_t capacity, Value key, Value value);

// Metatable operations
void set_metatable(Table* table, Table* metatable);
Table* get_metatable(Table* table);

// Basic metatable functions (advanced metamethods in evaluator.h)
const char* get_metamethod_name(const char* name);
bool has_table_metamethod(Table* table, MobiusString* method_name);
Value get_table_metamethod(Table* table, MobiusString* method_name);

// Table debugging
void print_table(Table* table);
void print_table_safe(Table* table, Table** visited, int* visited_count, int max_depth);
void print_table_debug(Table* table);

#endif // MOBIUS_TABLE_H
