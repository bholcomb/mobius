#ifndef MOBIUS_LIBRARY_FIBER_H
#define MOBIUS_LIBRARY_FIBER_H

#include "library/library.h"

int lib_fiber_channel(MobiusState* state, int arg_count);
int lib_fiber_send(MobiusState* state, int arg_count);
int lib_fiber_recv(MobiusState* state, int arg_count);
int lib_fiber_try_send(MobiusState* state, int arg_count);
int lib_fiber_try_recv(MobiusState* state, int arg_count);
int lib_fiber_close(MobiusState* state, int arg_count);
int lib_fiber_cancel(MobiusState* state, int arg_count);
int lib_fiber_all(MobiusState* state, int arg_count);
int lib_fiber_any(MobiusState* state, int arg_count);
int lib_fiber_sleep(MobiusState* state, int arg_count);
int lib_fiber_slice(MobiusState* state, int arg_count);

#endif // MOBIUS_LIBRARY_FIBER_H
