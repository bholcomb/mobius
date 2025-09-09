#ifndef MOBIUS_BYTECODE_UTIL_H
#define MOBIUS_BYTECODE_UTIL_H

#include "value.h"

// Forward declaration
struct MobiusVM;

// Utility builtin function implementations for bytecode VM
void builtin_random_vm(struct MobiusVM* vm, int arg_count, void* result_ptr);
void builtin_time_vm(struct MobiusVM* vm, int arg_count, void* result_ptr);
void builtin_clock_vm(struct MobiusVM* vm, int arg_count, void* result_ptr);

#endif // MOBIUS_BYTECODE_UTIL_H
