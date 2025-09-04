#ifndef MOBIUS_STDLIB_H
#define MOBIUS_STDLIB_H

#include "evaluator.h"

// Standard Library Categories:
// - Core: print, typeof, str, int, float
// - Math: abs, min, max, pow, sqrt, floor, ceil, round
// - String: len, substr, concat, upper, lower, contains
// - Utility: random, time, clock

// Core conversion and utility functions
EvalResult builtin_print(Value* args, size_t arg_count);
EvalResult builtin_typeof(Value* args, size_t arg_count);
EvalResult builtin_str(Value* args, size_t arg_count);
EvalResult builtin_int(Value* args, size_t arg_count);
EvalResult builtin_float(Value* args, size_t arg_count);

// Math functions
EvalResult builtin_abs(Value* args, size_t arg_count);
EvalResult builtin_min(Value* args, size_t arg_count);
EvalResult builtin_max(Value* args, size_t arg_count);
EvalResult builtin_pow(Value* args, size_t arg_count);
EvalResult builtin_sqrt(Value* args, size_t arg_count);
EvalResult builtin_floor(Value* args, size_t arg_count);
EvalResult builtin_ceil(Value* args, size_t arg_count);

EvalResult builtin_round(Value* args, size_t arg_count);

// String manipulation functions
EvalResult builtin_len(Value* args, size_t arg_count);
EvalResult builtin_substr(Value* args, size_t arg_count);
EvalResult builtin_concat(Value* args, size_t arg_count);
EvalResult builtin_upper(Value* args, size_t arg_count);
EvalResult builtin_lower(Value* args, size_t arg_count);
EvalResult builtin_contains(Value* args, size_t arg_count);

// Utility functions
EvalResult builtin_random(Value* args, size_t arg_count);
EvalResult builtin_time(Value* args, size_t arg_count);
EvalResult builtin_clock(Value* args, size_t arg_count);

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
