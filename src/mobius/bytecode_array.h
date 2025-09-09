#ifndef MOBIUS_BYTECODE_ARRAY_H
#define MOBIUS_BYTECODE_ARRAY_H

#include "value.h"

// Forward declaration
struct MobiusVM;

// Array builtin function implementations for bytecode VM
void builtin_array_create_vm(struct MobiusVM* vm, int arg_count, void* result_ptr);
void builtin_array_push_vm(struct MobiusVM* vm, int arg_count, void* result_ptr);
void builtin_array_pop_vm(struct MobiusVM* vm, int arg_count, void* result_ptr);
void builtin_array_get_vm(struct MobiusVM* vm, int arg_count, void* result_ptr);
void builtin_array_set_vm(struct MobiusVM* vm, int arg_count, void* result_ptr);
void builtin_array_length_vm(struct MobiusVM* vm, int arg_count, void* result_ptr);
void builtin_array_slice_vm(struct MobiusVM* vm, int arg_count, void* result_ptr);
void builtin_array_concat_vm(struct MobiusVM* vm, int arg_count, void* result_ptr);
void builtin_array_reverse_vm(struct MobiusVM* vm, int arg_count, void* result_ptr);
void builtin_array_find_vm(struct MobiusVM* vm, int arg_count, void* result_ptr);

#endif // MOBIUS_BYTECODE_ARRAY_H
