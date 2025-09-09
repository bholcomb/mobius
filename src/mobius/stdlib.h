#ifndef MOBIUS_STDLIB_H
#define MOBIUS_STDLIB_H

#include "evaluator.h"

// Standard Library Categories:
// - Core: print, typeof, str, int, float
// - Math: abs, min, max, pow, sqrt, floor, ceil, round
// - String: len, substr, concat, upper, lower, contains
// - Utility: random, time, clock

// Core conversion and utility functions
EvalResult builtin_print(Environment* env, Value* args, size_t arg_count);
EvalResult builtin_typeof(Environment* env, Value* args, size_t arg_count);
EvalResult builtin_str(Environment* env, Value* args, size_t arg_count);
EvalResult builtin_int(Environment* env, Value* args, size_t arg_count);
EvalResult builtin_float(Environment* env, Value* args, size_t arg_count);

// Math functions
EvalResult builtin_abs(Environment* env, Value* args, size_t arg_count);
EvalResult builtin_min(Environment* env, Value* args, size_t arg_count);
EvalResult builtin_max(Environment* env, Value* args, size_t arg_count);
EvalResult builtin_pow(Environment* env, Value* args, size_t arg_count);
EvalResult builtin_sqrt(Environment* env, Value* args, size_t arg_count);
EvalResult builtin_floor(Environment* env, Value* args, size_t arg_count);
EvalResult builtin_ceil(Environment* env, Value* args, size_t arg_count);

EvalResult builtin_round(Environment* env, Value* args, size_t arg_count);

// String manipulation functions
EvalResult builtin_len(Environment* env, Value* args, size_t arg_count);
EvalResult builtin_substr(Environment* env, Value* args, size_t arg_count);
EvalResult builtin_concat(Environment* env, Value* args, size_t arg_count);
EvalResult builtin_upper(Environment* env, Value* args, size_t arg_count);
EvalResult builtin_lower(Environment* env, Value* args, size_t arg_count);
EvalResult builtin_contains(Environment* env, Value* args, size_t arg_count);

// Utility functions
EvalResult builtin_random(Environment* env, Value* args, size_t arg_count);
EvalResult builtin_time(Environment* env, Value* args, size_t arg_count);
EvalResult builtin_clock(Environment* env, Value* args, size_t arg_count);

// File I/O functions
EvalResult builtin_load(Environment* env, Value* args, size_t arg_count);

// Type system functions
EvalResult builtin_set_strict_types(Environment* env, Value* args, size_t arg_count);
EvalResult builtin_set_type_warnings(Environment* env, Value* args, size_t arg_count);
EvalResult builtin_get_type_config(Environment* env, Value* args, size_t arg_count);

// Module import function
EvalResult builtin_import(Environment* env, Value* args, size_t arg_count);

// Table functions
EvalResult builtin_table_insert(Environment* env, Value* args, size_t arg_count);
EvalResult builtin_table_remove(Environment* env, Value* args, size_t arg_count);
EvalResult builtin_table_has_key(Environment* env, Value* args, size_t arg_count);
EvalResult builtin_table_size(Environment* env, Value* args, size_t arg_count);
EvalResult builtin_setmetatable(Environment* env, Value* args, size_t arg_count);
EvalResult builtin_getmetatable(Environment* env, Value* args, size_t arg_count);
EvalResult builtin_pairs(Environment* env, Value* args, size_t arg_count);

// Array functions
EvalResult builtin_array_create(Environment* env, Value* args, size_t arg_count);
EvalResult builtin_array_push(Environment* env, Value* args, size_t arg_count);
EvalResult builtin_array_pop(Environment* env, Value* args, size_t arg_count);
EvalResult builtin_array_get(Environment* env, Value* args, size_t arg_count);
EvalResult builtin_array_set(Environment* env, Value* args, size_t arg_count);
EvalResult builtin_array_length(Environment* env, Value* args, size_t arg_count);
EvalResult builtin_array_slice(Environment* env, Value* args, size_t arg_count);
EvalResult builtin_array_concat(Environment* env, Value* args, size_t arg_count);
EvalResult builtin_array_reverse(Environment* env, Value* args, size_t arg_count);
EvalResult builtin_array_find(Environment* env, Value* args, size_t arg_count);

// Standard library management
typedef struct {
    const char* name;
    BuiltinFunction function;
    size_t arity;  // Expected number of arguments (SIZE_MAX for variadic)
    const char* description;
    const char* category;
} StdlibEntry;

// Get all standard library functions
const StdlibEntry* get_stdlib_functions(void);
size_t get_stdlib_count(void);

// Lookup functions
BuiltinFunction lookup_stdlib_function(const char* name);
void register_stdlib_functions(Environment* env);

// Help and documentation
void print_stdlib_help(void);
void print_function_help(const char* function_name);

#endif // MOBIUS_STDLIB_H
