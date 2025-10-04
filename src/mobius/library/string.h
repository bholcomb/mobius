#ifndef MOBIUS_LIBRARY_STRING_H
#define MOBIUS_LIBRARY_STRING_H

#include "library/library.h"

// =============================================================================
// UNIFIED STRING FUNCTION IMPLEMENTATIONS
// =============================================================================

// String functions using unified library interface
EvalResult lib_len(MobiusState* state, int arg_count);
EvalResult lib_upper(MobiusState* state, int arg_count);
EvalResult lib_lower(MobiusState* state, int arg_count);
EvalResult lib_substr(MobiusState* state, int arg_count);
EvalResult lib_concat(MobiusState* state, int arg_count);
EvalResult lib_contains(MobiusState* state, int arg_count);

#endif // MOBIUS_LIBRARY_STRING_H
