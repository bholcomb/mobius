#ifndef MOBIUS_LIBRARY_STRING_H
#define MOBIUS_LIBRARY_STRING_H

#include "library.h"

// =============================================================================
// UNIFIED STRING FUNCTION IMPLEMENTATIONS
// =============================================================================

// String functions using unified library interface
EvalResult lib_len(ExecutionContext* ctx, int arg_count);
EvalResult lib_upper(ExecutionContext* ctx, int arg_count);
EvalResult lib_lower(ExecutionContext* ctx, int arg_count);
EvalResult lib_substr(ExecutionContext* ctx, int arg_count);
EvalResult lib_concat(ExecutionContext* ctx, int arg_count);
EvalResult lib_contains(ExecutionContext* ctx, int arg_count);

#endif // MOBIUS_LIBRARY_STRING_H
