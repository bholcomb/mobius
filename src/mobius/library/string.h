#ifndef MOBIUS_LIBRARY_STRING_H
#define MOBIUS_LIBRARY_STRING_H

#include "library/library.h"

// =============================================================================
// UNIFIED STRING FUNCTION IMPLEMENTATIONS
// =============================================================================

// String functions using unified library interface
int lib_len(MobiusState* state, int arg_count);
int lib_upper(MobiusState* state, int arg_count);
int lib_lower(MobiusState* state, int arg_count);
int lib_substr(MobiusState* state, int arg_count);
int lib_concat(MobiusState* state, int arg_count);
int lib_contains(MobiusState* state, int arg_count);
int lib_split(MobiusState* state, int arg_count);
int lib_join(MobiusState* state, int arg_count);
int lib_trim(MobiusState* state, int arg_count);
int lib_startswith(MobiusState* state, int arg_count);
int lib_endswith(MobiusState* state, int arg_count);
int lib_replace(MobiusState* state, int arg_count);
int lib_find(MobiusState* state, int arg_count);
int lib_repeat(MobiusState* state, int arg_count);

#endif // MOBIUS_LIBRARY_STRING_H
