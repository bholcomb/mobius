#ifndef MOBIUS_LIBRARY_UTIL_H
#define MOBIUS_LIBRARY_UTIL_H

#include "library/library.h"

// =============================================================================
// UTILITY FUNCTIONS (random, time, clock)
// =============================================================================

EvalResult lib_random(MobiusState* state, int arg_count);
EvalResult lib_time(MobiusState* state, int arg_count);
EvalResult lib_clock(MobiusState* state, int arg_count);
EvalResult lib_load(MobiusState* state, int arg_count);
EvalResult lib_id(MobiusState* state, int arg_count);

#endif // MOBIUS_LIBRARY_UTIL_H
