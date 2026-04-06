#ifndef MOBIUS_LIBRARY_ARRAY_H
#define MOBIUS_LIBRARY_ARRAY_H

#include "library/library.h"

class Table;

// Global: array_create(capacity [, fill_value])
int lib_array_create(MobiusState* state, int arg_count);

// Method-style natives (called via arr:method() with self at base)
int array_method_push(MobiusState* state, int arg_count);
int array_method_pop(MobiusState* state, int arg_count);
int array_method_get(MobiusState* state, int arg_count);
int array_method_set(MobiusState* state, int arg_count);
int array_method_length(MobiusState* state, int arg_count);
int array_method_slice(MobiusState* state, int arg_count);
int array_method_concat(MobiusState* state, int arg_count);
int array_method_reverse(MobiusState* state, int arg_count);
int array_method_find(MobiusState* state, int arg_count);
int array_method_sort(MobiusState* state, int arg_count);
int array_method_map(MobiusState* state, int arg_count);
int array_method_filter(MobiusState* state, int arg_count);
int array_method_reduce(MobiusState* state, int arg_count);
int array_method_foreach(MobiusState* state, int arg_count);
int array_method_any(MobiusState* state, int arg_count);
int array_method_all(MobiusState* state, int arg_count);

// Type metatable builder
Table* create_array_type_metatable(MobiusState* state);

#endif // MOBIUS_LIBRARY_ARRAY_H
