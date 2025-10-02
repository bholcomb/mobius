#ifndef MOBIUS_LIBRARY_UTIL_H
#define MOBIUS_LIBRARY_UTIL_H

#include "library.h"

// =============================================================================
// UTILITY FUNCTIONS (random, time, clock)
// =============================================================================

EvalResult lib_random(ExecutionContext* ctx, int arg_count);
EvalResult lib_time(ExecutionContext* ctx, int arg_count);
EvalResult lib_clock(ExecutionContext* ctx, int arg_count);
EvalResult lib_load(ExecutionContext* ctx, int arg_count);
EvalResult lib_id(ExecutionContext* ctx, int arg_count);

#endif // MOBIUS_LIBRARY_UTIL_H
