#ifndef MOBIUS_LIBRARY_MATH_H
#define MOBIUS_LIBRARY_MATH_H

#include "library/library.h"

// =============================================================================
// UNIFIED MATH FUNCTION IMPLEMENTATIONS
// =============================================================================

// Math functions using unified library interface
EvalResult lib_abs(MobiusState* state, int arg_count);
EvalResult lib_min(MobiusState* state, int arg_count);
EvalResult lib_max(MobiusState* state, int arg_count);
EvalResult lib_pow(MobiusState* state, int arg_count);
EvalResult lib_sqrt(MobiusState* state, int arg_count);
EvalResult lib_floor(MobiusState* state, int arg_count);
EvalResult lib_ceil(MobiusState* state, int arg_count);
EvalResult lib_round(MobiusState* state, int arg_count);


#endif // MOBIUS_LIBRARY_MATH_H
