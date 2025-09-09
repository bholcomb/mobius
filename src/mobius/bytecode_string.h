#ifndef MOBIUS_BYTECODE_STRING_H
#define MOBIUS_BYTECODE_STRING_H

#include "value.h"

// Forward declaration
struct MobiusVM;

// String builtin function implementations for bytecode VM
void builtin_len_vm(struct MobiusVM* vm, int arg_count, void* result_ptr);
void builtin_upper_vm(struct MobiusVM* vm, int arg_count, void* result_ptr);
void builtin_lower_vm(struct MobiusVM* vm, int arg_count, void* result_ptr);
void builtin_substr_vm(struct MobiusVM* vm, int arg_count, void* result_ptr);
void builtin_concat_vm(struct MobiusVM* vm, int arg_count, void* result_ptr);
void builtin_contains_vm(struct MobiusVM* vm, int arg_count, void* result_ptr);

#endif // MOBIUS_BYTECODE_STRING_H
