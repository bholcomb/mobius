#ifndef MOBIUS_LIBRARY_STRING_H
#define MOBIUS_LIBRARY_STRING_H

#include "library.h"

// =============================================================================
// UNIFIED STRING FUNCTION IMPLEMENTATIONS
// =============================================================================

// String functions using unified library interface
EvalResult lib_len(Environment* env, int arg_count);
EvalResult lib_upper(Environment* env, int arg_count);
EvalResult lib_lower(Environment* env, int arg_count);
EvalResult lib_substr(Environment* env, int arg_count);
EvalResult lib_concat(Environment* env, int arg_count);
EvalResult lib_contains(Environment* env, int arg_count);

#endif // MOBIUS_LIBRARY_STRING_H
