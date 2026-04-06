#ifndef MOBIUS_LIBRARY_FIBER_H
#define MOBIUS_LIBRARY_FIBER_H

#include "library/library.h"

class Table;

// Module-level functions (fiber.channel, fiber.all, etc.)
int lib_fiber_channel(MobiusState* state, int arg_count);
int lib_fiber_cancel(MobiusState* state, int arg_count);
int lib_fiber_all(MobiusState* state, int arg_count);
int lib_fiber_any(MobiusState* state, int arg_count);
int lib_fiber_sleep(MobiusState* state, int arg_count);
int lib_fiber_slice(MobiusState* state, int arg_count);

// Channel methods (ch.send, ch.recv, etc.) — use npeek_self(arg_count) for receiver
int channel_method_send(MobiusState* state, int arg_count);
int channel_method_recv(MobiusState* state, int arg_count);
int channel_method_try_send(MobiusState* state, int arg_count);
int channel_method_try_recv(MobiusState* state, int arg_count);
int channel_method_close(MobiusState* state, int arg_count);
int channel_method_is_closed(MobiusState* state, int arg_count);

// Register the fiber module table and channel type metatable
Table* register_fiber_module(MobiusState* state);
Table* create_channel_type_metatable(MobiusState* state);

#endif // MOBIUS_LIBRARY_FIBER_H
