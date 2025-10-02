#ifndef MOBIUS_LIBRARY_ARRAY_H
#define MOBIUS_LIBRARY_ARRAY_H

#include "library.h"

// =============================================================================
// ARRAY FUNCTIONS (array_create, array_push, array_pop, etc)
// =============================================================================

EvalResult lib_array_create(ExecutionContext* ctx, int arg_count);
EvalResult lib_array_push(ExecutionContext* ctx, int arg_count);
EvalResult lib_array_pop(ExecutionContext* ctx, int arg_count);
EvalResult lib_array_get(ExecutionContext* ctx, int arg_count);
EvalResult lib_array_set(ExecutionContext* ctx, int arg_count);
EvalResult lib_array_length(ExecutionContext* ctx, int arg_count);
EvalResult lib_array_slice(ExecutionContext* ctx, int arg_count);
EvalResult lib_array_concat(ExecutionContext* ctx, int arg_count);
EvalResult lib_array_reverse(ExecutionContext* ctx, int arg_count);
EvalResult lib_array_find(ExecutionContext* ctx, int arg_count);


#endif // MOBIUS_LIBRARY_ARRAY_H
