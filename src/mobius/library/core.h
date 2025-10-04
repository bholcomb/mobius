#ifndef MOBIUS_LIBRARY_CORE_H
#define MOBIUS_LIBRARY_CORE_H

#include "library/library.h"

// =============================================================================
// UNIFIED CORE FUNCTION IMPLEMENTATIONS
// =============================================================================

// Core functions using unified library interface
EvalResult lib_print(MobiusState* state, int arg_count);
EvalResult lib_typeof(MobiusState* state, int arg_count);
EvalResult lib_int(MobiusState* state, int arg_count);
EvalResult lib_float(MobiusState* state, int arg_count);
EvalResult lib_str(MobiusState* state, int arg_count);


#endif // MOBIUS_LIBRARY_CORE_H
