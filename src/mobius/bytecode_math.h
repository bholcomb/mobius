#ifndef MOBIUS_BYTECODE_MATH_H
#define MOBIUS_BYTECODE_MATH_H

#include "value.h"

// Forward declaration
struct MobiusVM;

// Math builtin function implementations for bytecode VM
void builtin_abs_vm(struct MobiusVM* vm, int arg_count, void* result_ptr);
void builtin_min_vm(struct MobiusVM* vm, int arg_count, void* result_ptr);
void builtin_max_vm(struct MobiusVM* vm, int arg_count, void* result_ptr);
void builtin_pow_vm(struct MobiusVM* vm, int arg_count, void* result_ptr);
void builtin_sqrt_vm(struct MobiusVM* vm, int arg_count, void* result_ptr);
void builtin_floor_vm(struct MobiusVM* vm, int arg_count, void* result_ptr);
void builtin_ceil_vm(struct MobiusVM* vm, int arg_count, void* result_ptr);
void builtin_round_vm(struct MobiusVM* vm, int arg_count, void* result_ptr);

#endif // MOBIUS_BYTECODE_MATH_H
