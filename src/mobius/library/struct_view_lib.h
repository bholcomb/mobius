#ifndef MOBIUS_LIBRARY_STRUCT_VIEW_LIB_H
#define MOBIUS_LIBRARY_STRUCT_VIEW_LIB_H

#include "library/library.h"

class Table;

int lib_define_struct(MobiusState* state, int arg_count);
int buffer_method_view_as(MobiusState* state, int arg_count);
int buffer_method_array_view_as(MobiusState* state, int arg_count);

Table* create_struct_layout_metatable(MobiusState* state);
Table* create_struct_view_metatable(MobiusState* state);
Table* create_struct_array_view_metatable(MobiusState* state);

#endif // MOBIUS_LIBRARY_STRUCT_VIEW_LIB_H
