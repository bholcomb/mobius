#ifndef MOBIUS_LIBRARY_TYPES_H
#define MOBIUS_LIBRARY_TYPES_H

#include "library.h"

// =============================================================================
// TYPE SYSTEM FUNCTIONS (set_strict_types, etc)
// =============================================================================

EvalResult lib_set_strict_types(ExecutionContext* ctx, int arg_count);
EvalResult lib_set_type_warnings(ExecutionContext* ctx, int arg_count);
EvalResult lib_get_type_config(ExecutionContext* ctx, int arg_count);

#endif // MOBIUS_LIBRARY_TYPES_H
