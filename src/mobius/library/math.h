#ifndef MOBIUS_LIBRARY_MATH_H
#define MOBIUS_LIBRARY_MATH_H

#include "library.h"

// =============================================================================
// UNIFIED MATH FUNCTION IMPLEMENTATIONS
// =============================================================================

// Math functions using unified library interface
EvalResult lib_abs(Environment* env, int arg_count);
EvalResult lib_min(Environment* env, int arg_count);
EvalResult lib_max(Environment* env, int arg_count);
EvalResult lib_pow(Environment* env, int arg_count);
EvalResult lib_sqrt(Environment* env, int arg_count);
EvalResult lib_floor(Environment* env, int arg_count);
EvalResult lib_ceil(Environment* env, int arg_count);
EvalResult lib_round(Environment* env, int arg_count);


#endif // MOBIUS_LIBRARY_MATH_H
