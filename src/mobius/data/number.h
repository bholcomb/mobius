#ifndef MOBIUS_NUMBER_H
#define MOBIUS_NUMBER_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    NUM_UNKNOWN,  // No type specified/inferred
    NUM_INT8,     // int8_t
    NUM_INT16,    // int16_t
    NUM_INT32,    // int32_t
    NUM_INT64,    // int64_t
    NUM_UINT8,    // uint8_t
    NUM_UINT16,   // uint16_t
    NUM_UINT32,   // uint32_t
    NUM_UINT64,   // uint64_t
    NUM_FLOAT32,  // float (32-bit float)
    NUM_FLOAT64   // double (64-bit float)
} NumberType;

const char* number_type_name(NumberType type);
bool is_integer_type(NumberType type);
bool is_unsigned_type(NumberType type);
bool is_float_type(NumberType type);
int64_t get_type_min_value(NumberType type);
uint64_t get_type_max_value(NumberType type);
bool types_are_compatible(NumberType from, NumberType to);
bool value_fits_in_type(int64_t value, NumberType target_type);

#endif // MOBIUS_NUMBER_H