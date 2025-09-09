#ifndef MOBIUS_BYTECODE_TYPES_H
#define MOBIUS_BYTECODE_TYPES_H

#include "value.h"

// Forward declaration
struct MobiusVM;

// Type system builtin function implementations for bytecode VM
void builtin_set_strict_types_vm(struct MobiusVM* vm, int arg_count, void* result_ptr);
void builtin_set_type_warnings_vm(struct MobiusVM* vm, int arg_count, void* result_ptr);
void builtin_get_type_config_vm(struct MobiusVM* vm, int arg_count, void* result_ptr);

#endif // MOBIUS_BYTECODE_TYPES_H
