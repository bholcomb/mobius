#ifndef MOBIUS_LIBRARY_UTIL_H
#define MOBIUS_LIBRARY_UTIL_H

#include "library.h"

// =============================================================================
// UTILITY FUNCTIONS (random, time, clock)
// =============================================================================

EvalResult lib_random(Environment* env, int arg_count);
EvalResult lib_time(Environment* env, int arg_count);
EvalResult lib_clock(Environment* env, int arg_count);
EvalResult lib_load(Environment* env, int arg_count);

#endif // MOBIUS_LIBRARY_UTIL_H
