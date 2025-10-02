#ifndef MOBIUS_LIBRARY_TABLE_LIB_H
#define MOBIUS_LIBRARY_TABLE_LIB_H

#include "library.h"

// =============================================================================
// UNIFIED TABLE FUNCTION IMPLEMENTATIONS
// =============================================================================

// Table functions using unified library interface
EvalResult lib_table_insert(ExecutionContext* ctx, int arg_count);
EvalResult lib_table_remove(ExecutionContext* ctx, int arg_count);
EvalResult lib_table_has_key(ExecutionContext* ctx, int arg_count);
EvalResult lib_table_size(ExecutionContext* ctx, int arg_count);
EvalResult lib_setmetatable(ExecutionContext* ctx, int arg_count);
EvalResult lib_getmetatable(ExecutionContext* ctx, int arg_count);
EvalResult lib_pairs(ExecutionContext* ctx, int arg_count);

#endif // MOBIUS_LIBRARY_TABLE_LIB_H
