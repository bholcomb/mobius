#ifndef MOBIUS_LIBRARY_TABLE_LIB_H
#define MOBIUS_LIBRARY_TABLE_LIB_H

#include "library.h"

// =============================================================================
// UNIFIED TABLE FUNCTION IMPLEMENTATIONS
// =============================================================================

// Table functions using unified library interface
EvalResult lib_table_insert(Environment* env, int arg_count);
EvalResult lib_table_remove(Environment* env, int arg_count);
EvalResult lib_table_has_key(Environment* env, int arg_count);
EvalResult lib_table_size(Environment* env, int arg_count);
EvalResult lib_setmetatable(Environment* env, int arg_count);
EvalResult lib_getmetatable(Environment* env, int arg_count);
EvalResult lib_pairs(Environment* env, int arg_count);

#endif // MOBIUS_LIBRARY_TABLE_LIB_H
