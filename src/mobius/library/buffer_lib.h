#ifndef MOBIUS_LIBRARY_BUFFER_LIB_H
#define MOBIUS_LIBRARY_BUFFER_LIB_H

#include "library/library.h"

class Table;

int lib_buffer_create(MobiusState* state, int arg_count);
int lib_buffer_from_string(MobiusState* state, int arg_count);

int buffer_method_get(MobiusState* state, int arg_count);
int buffer_method_set(MobiusState* state, int arg_count);
int buffer_method_length(MobiusState* state, int arg_count);
int buffer_method_resize(MobiusState* state, int arg_count);
int buffer_method_reserve(MobiusState* state, int arg_count);
int buffer_method_append(MobiusState* state, int arg_count);
int buffer_method_copy(MobiusState* state, int arg_count);
int buffer_method_slice(MobiusState* state, int arg_count);
int buffer_method_to_string(MobiusState* state, int arg_count);
int buffer_method_address(MobiusState* state, int arg_count);
int buffer_method_is_fixed(MobiusState* state, int arg_count);

Table* create_buffer_type_metatable(MobiusState* state);

#endif // MOBIUS_LIBRARY_BUFFER_LIB_H
