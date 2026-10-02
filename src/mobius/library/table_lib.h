#ifndef MOBIUS_LIBRARY_TABLE_LIB_H
#define MOBIUS_LIBRARY_TABLE_LIB_H

#include "library/library.h"

class Table;

// Method-style natives (called via tbl:method() with self at base)
int table_method_remove(MobiusState* state, int arg_count, void* /*userdata*/);
int table_method_has_key(MobiusState* state, int arg_count, void* /*userdata*/);
int table_method_size(MobiusState* state, int arg_count, void* /*userdata*/);
int table_method_pairs(MobiusState* state, int arg_count, void* /*userdata*/);

// Globals that remain
int lib_setmetatable(MobiusState* state, int arg_count, void* /*userdata*/);
int lib_getmetatable(MobiusState* state, int arg_count, void* /*userdata*/);

// Type metatable builder
Table* create_table_type_metatable(MobiusState* state);

#endif // MOBIUS_LIBRARY_TABLE_LIB_H
