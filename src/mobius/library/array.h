#ifndef MOBIUS_LIBRARY_ARRAY_H
#define MOBIUS_LIBRARY_ARRAY_H

#include "library/library.h"

// =============================================================================
// ARRAY FUNCTIONS (array_create, array_push, array_pop, etc)
// =============================================================================

int lib_array_create(MobiusState* state, int arg_count);
int lib_array_push(MobiusState* state, int arg_count);
int lib_array_pop(MobiusState* state, int arg_count);
int lib_array_get(MobiusState* state, int arg_count);
int lib_array_set(MobiusState* state, int arg_count);
int lib_array_length(MobiusState* state, int arg_count);
int lib_array_slice(MobiusState* state, int arg_count);
int lib_array_concat(MobiusState* state, int arg_count);
int lib_array_reverse(MobiusState* state, int arg_count);
int lib_array_find(MobiusState* state, int arg_count);


#endif // MOBIUS_LIBRARY_ARRAY_H
