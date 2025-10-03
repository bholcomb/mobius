#ifndef MOBIUS_STDLIB_INIT_H
#define MOBIUS_STDLIB_INIT_H

#include "state/environment.h"

// Initialize standard library by registering all stdlib functions
// as VAL_NATIVE_FUNCTION values in the given environment
void register_stdlib_functions(Environment* env);

#endif // MOBIUS_STDLIB_INIT_H

