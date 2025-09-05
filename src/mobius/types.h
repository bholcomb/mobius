#ifndef MOBIUS_TYPES_H
#define MOBIUS_TYPES_H

#include "token.h"
#include "value.h"
#include <stdbool.h>
#include <stdint.h>

// Type system for Mobius scripting language
// Supports optional type annotations for integers and floats

// Type checking configuration
typedef struct {
    bool strict_mode;       // If true, no automatic conversions
    bool warn_on_conversion; // If true, warn when converting types
} TypeCheckConfig;

// Type conversion result
typedef struct {
    bool success;
    Value converted_value;
    char* error_message;    // NULL if successful, caller must free
    bool was_converted;     // true if conversion was needed
} TypeConversionResult;

// Type system functions
const char* mobius_type_name(MobiusType type);
bool is_integer_type(MobiusType type);
bool is_unsigned_type(MobiusType type);
bool is_float_type(MobiusType type);
MobiusType token_to_mobius_type(TokenType token_type);

// Type validation and conversion
TypeConversionResult validate_and_convert_value(Value value, TypeInfo target_type, TypeCheckConfig config);
bool types_are_compatible(MobiusType from, MobiusType to);
int64_t get_type_min_value(MobiusType type);
uint64_t get_type_max_value(MobiusType type);

// Helper functions for creating TypeInfo
TypeInfo make_unknown_type(void);
TypeInfo make_annotated_type(MobiusType type);

#endif // MOBIUS_TYPES_H
