#ifndef MOBIUS_LIBRARY_CORE_H
#define MOBIUS_LIBRARY_CORE_H

#include "library.h"

// =============================================================================
// UNIFIED CORE FUNCTION IMPLEMENTATIONS
// =============================================================================

// Core functions using unified library interface
EvalResult lib_print(ExecutionContext* ctx, int arg_count);
EvalResult lib_typeof(ExecutionContext* ctx, int arg_count);
EvalResult lib_int(ExecutionContext* ctx, int arg_count);
EvalResult lib_float(ExecutionContext* ctx, int arg_count);
EvalResult lib_str(ExecutionContext* ctx, int arg_count);


#endif // MOBIUS_LIBRARY_CORE_H
