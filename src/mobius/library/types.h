#ifndef MOBIUS_LIBRARY_TYPES_H
#define MOBIUS_LIBRARY_TYPES_H

#include "library/library.h"

// =============================================================================
// TYPE SYSTEM FUNCTIONS
// =============================================================================
// Note: set_strict_types() and set_type_warnings() have been removed.
// Use #pragma strict_types true/false instead.

EvalResult lib_get_type_config(MobiusState* state, int arg_count);

#endif // MOBIUS_LIBRARY_TYPES_H
