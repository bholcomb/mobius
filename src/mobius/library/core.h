#ifndef MOBIUS_LIBRARY_CORE_H
#define MOBIUS_LIBRARY_CORE_H

#include "library/library.h"

// =============================================================================
// UNIFIED CORE FUNCTION IMPLEMENTATIONS
// =============================================================================

// Core functions using unified library interface
int lib_print(MobiusState* state, int arg_count);
int lib_typeof(MobiusState* state, int arg_count);
int lib_int(MobiusState* state, int arg_count);
int lib_float(MobiusState* state, int arg_count);
int lib_str(MobiusState* state, int arg_count);
int lib_gc_objects(MobiusState* state, int arg_count);
int lib_gc_verify(MobiusState* state, int arg_count);
int lib_exit(MobiusState* state, int arg_count);

#endif // MOBIUS_LIBRARY_CORE_H
