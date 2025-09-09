#ifndef MOBIUS_BYTECODE_TABLE_H
#define MOBIUS_BYTECODE_TABLE_H

#include "value.h"

// Forward declaration
struct MobiusVM;

// Table builtin function implementations for bytecode VM
void builtin_table_insert_vm(struct MobiusVM* vm, int arg_count, void* result_ptr);
void builtin_table_remove_vm(struct MobiusVM* vm, int arg_count, void* result_ptr);
void builtin_table_has_key_vm(struct MobiusVM* vm, int arg_count, void* result_ptr);
void builtin_table_size_vm(struct MobiusVM* vm, int arg_count, void* result_ptr);
void builtin_setmetatable_vm(struct MobiusVM* vm, int arg_count, void* result_ptr);
void builtin_getmetatable_vm(struct MobiusVM* vm, int arg_count, void* result_ptr);
void builtin_pairs_vm(struct MobiusVM* vm, int arg_count, void* result_ptr);

#endif // MOBIUS_BYTECODE_TABLE_H
