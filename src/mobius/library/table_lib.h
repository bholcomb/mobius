#ifndef MOBIUS_LIBRARY_TABLE_LIB_H
#define MOBIUS_LIBRARY_TABLE_LIB_H

#include "library/library.h"

// =============================================================================
// UNIFIED TABLE FUNCTION IMPLEMENTATIONS
// =============================================================================

// Table functions using unified library interface
EvalResult lib_table_insert(MobiusState* state, int arg_count);
EvalResult lib_table_remove(MobiusState* state, int arg_count);
EvalResult lib_table_has_key(MobiusState* state, int arg_count);
EvalResult lib_table_size(MobiusState* state, int arg_count);
EvalResult lib_setmetatable(MobiusState* state, int arg_count);
EvalResult lib_getmetatable(MobiusState* state, int arg_count);
EvalResult lib_pairs(MobiusState* state, int arg_count);

#endif // MOBIUS_LIBRARY_TABLE_LIB_H
