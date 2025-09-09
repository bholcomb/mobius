#ifndef MOBIUS_LIBRARY_ARRAY_H
#define MOBIUS_LIBRARY_ARRAY_H

#include "library.h"

// =============================================================================
// ARRAY FUNCTIONS (array_create, array_push, array_pop, etc)
// =============================================================================

EvalResult lib_array_create(Environment* env, int arg_count);
EvalResult lib_array_push(Environment* env, int arg_count);
EvalResult lib_array_pop(Environment* env, int arg_count);
EvalResult lib_array_get(Environment* env, int arg_count);
EvalResult lib_array_set(Environment* env, int arg_count);
EvalResult lib_array_length(Environment* env, int arg_count);
EvalResult lib_array_slice(Environment* env, int arg_count);
EvalResult lib_array_concat(Environment* env, int arg_count);
EvalResult lib_array_reverse(Environment* env, int arg_count);
EvalResult lib_array_find(Environment* env, int arg_count);


#endif // MOBIUS_LIBRARY_ARRAY_H
