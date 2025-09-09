#ifndef MOBIUS_LIBRARY_CORE_H
#define MOBIUS_LIBRARY_CORE_H

#include "library.h"

// =============================================================================
// UNIFIED CORE FUNCTION IMPLEMENTATIONS
// =============================================================================

// Core functions using unified library interface
EvalResult lib_print(Environment* env, int arg_count);
EvalResult lib_typeof(Environment* env, int arg_count);
EvalResult lib_int(Environment* env, int arg_count);
EvalResult lib_float(Environment* env, int arg_count);
EvalResult lib_str(Environment* env, int arg_count);


#endif // MOBIUS_LIBRARY_CORE_H
