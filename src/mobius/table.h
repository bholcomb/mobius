#ifndef MOBIUS_TABLE_H
#define MOBIUS_TABLE_H

#include "ast.h"
#include <stddef.h>
#include <stdbool.h>

// Hash table configuration
#define INITIAL_TABLE_CAPACITY 8
#define LOAD_FACTOR_THRESHOLD 0.75

// Table function declarations
Table* create_table(size_t initial_capacity);
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
bool has_table_metamethod(Table* table, const char* method_name);
Value get_table_metamethod(Table* table, const char* method_name);

// Table debugging
void print_table(Table* table);
void print_table_safe(Table* table, Table** visited, int* visited_count, int max_depth);
void print_table_debug(Table* table);

#endif // MOBIUS_TABLE_H
