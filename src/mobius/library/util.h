#ifndef MOBIUS_LIBRARY_UTIL_H
#define MOBIUS_LIBRARY_UTIL_H

#include "library/library.h"

// =============================================================================
// UTILITY FUNCTIONS (random, time, clock)
// =============================================================================

int lib_random(MobiusState* state, int arg_count);
int lib_randomseed(MobiusState* state, int arg_count);
int lib_clock(MobiusState* state, int arg_count);
int lib_load(MobiusState* state, int arg_count);
int lib_id(MobiusState* state, int arg_count);
int lib_isnan(MobiusState* state, int arg_count);
int lib_isinf(MobiusState* state, int arg_count);
int lib_isfinite(MobiusState* state, int arg_count);
int lib_time(MobiusState* state, int arg_count);

#endif // MOBIUS_LIBRARY_UTIL_H
