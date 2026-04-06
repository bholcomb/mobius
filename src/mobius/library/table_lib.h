#ifndef MOBIUS_LIBRARY_TABLE_LIB_H
#define MOBIUS_LIBRARY_TABLE_LIB_H

#include "library/library.h"

class Table;

// Method-style natives (called via tbl:method() with self at base)
int table_method_remove(MobiusState* state, int arg_count);
int table_method_has_key(MobiusState* state, int arg_count);
int table_method_size(MobiusState* state, int arg_count);
int table_method_pairs(MobiusState* state, int arg_count);

// Globals that remain
int lib_setmetatable(MobiusState* state, int arg_count);
int lib_getmetatable(MobiusState* state, int arg_count);

// Type metatable builder
Table* create_table_type_metatable(MobiusState* state);

#endif // MOBIUS_LIBRARY_TABLE_LIB_H
