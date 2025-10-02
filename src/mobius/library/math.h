#ifndef MOBIUS_LIBRARY_MATH_H
#define MOBIUS_LIBRARY_MATH_H

#include "library.h"

// =============================================================================
// UNIFIED MATH FUNCTION IMPLEMENTATIONS
// =============================================================================

// Math functions using unified library interface
EvalResult lib_abs(ExecutionContext* ctx, int arg_count);
EvalResult lib_min(ExecutionContext* ctx, int arg_count);
EvalResult lib_max(ExecutionContext* ctx, int arg_count);
EvalResult lib_pow(ExecutionContext* ctx, int arg_count);
EvalResult lib_sqrt(ExecutionContext* ctx, int arg_count);
EvalResult lib_floor(ExecutionContext* ctx, int arg_count);
EvalResult lib_ceil(ExecutionContext* ctx, int arg_count);
EvalResult lib_round(ExecutionContext* ctx, int arg_count);


#endif // MOBIUS_LIBRARY_MATH_H
