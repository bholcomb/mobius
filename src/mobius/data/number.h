#ifndef MOBIUS_NUMBER_H
#define MOBIUS_NUMBER_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    NUM_UNKNOWN,  // No type specified/inferred
    NUM_INT64,    // int64_t
    NUM_UINT64,   // uint64_t
    NUM_FLOAT64   // double (64-bit float)
} NumberType;

const char* number_type_name(NumberType type);
bool is_integer_type(NumberType type);
bool is_unsigned_type(NumberType type);
bool is_float_type(NumberType type);

#endif // MOBIUS_NUMBER_H
