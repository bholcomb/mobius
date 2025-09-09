#ifndef MOBIUS_BYTECODE_CORE_H
#define MOBIUS_BYTECODE_CORE_H

#include "value.h"

// Forward declaration
struct MobiusVM;

// Core builtin function implementations for bytecode VM
void builtin_print_vm(struct MobiusVM* vm, int arg_count, void* result_ptr);
void builtin_typeof_vm(struct MobiusVM* vm, int arg_count, void* result_ptr);
void builtin_int_vm(struct MobiusVM* vm, int arg_count, void* result_ptr);
void builtin_float_vm(struct MobiusVM* vm, int arg_count, void* result_ptr);
void builtin_str_vm(struct MobiusVM* vm, int arg_count, void* result_ptr);

#endif // MOBIUS_BYTECODE_CORE_H
