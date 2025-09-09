#include "types.h"
#include "ast.h"
#include "token.h"
#include "utility.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

// Type name mappings
const char* mobius_type_name(NumberType type) {
    switch (type) {
        case NUMBER_TYPE_UNKNOWN: return "unknown";
        case NUMBER_TYPE_INT8:    return "int8";
        case NUMBER_TYPE_INT16:   return "int16";
        case NUMBER_TYPE_INT32:   return "int32";
        case NUMBER_TYPE_INT64:   return "int64";
        case NUMBER_TYPE_UINT8:   return "uint8";
        case NUMBER_TYPE_UINT16:  return "uint16";
        case NUMBER_TYPE_UINT32:  return "uint32";
        case NUMBER_TYPE_UINT64:  return "uint64";
        case NUMBER_TYPE_FLOAT32: return "float32";
        case NUMBER_TYPE_FLOAT64:   return "float";
        default: return "invalid";
    }
}

// Type category checks
bool is_integer_type(NumberType type) {
    return type >= NUMBER_TYPE_INT8 && type <= NUMBER_TYPE_UINT64;
}

bool is_unsigned_type(NumberType type) {
    return type >= NUMBER_TYPE_UINT8 && type <= NUMBER_TYPE_UINT64;
}

bool is_float_type(NumberType type) {
    return type == NUMBER_TYPE_FLOAT32 || type == NUMBER_TYPE_FLOAT64;
}

// Convert token type to Mobius type
NumberType token_to_mobius_type(TokenType token_type) {
    switch (token_type) {
        case TOKEN_TYPE_INT8:   return NUMBER_TYPE_INT8;
        case TOKEN_TYPE_INT16:  return NUMBER_TYPE_INT16;
        case TOKEN_TYPE_INT32:  return NUMBER_TYPE_INT32;
        case TOKEN_TYPE_INT64:  return NUMBER_TYPE_INT64;
        case TOKEN_TYPE_UINT8:  return NUMBER_TYPE_UINT8;
        case TOKEN_TYPE_UINT16: return NUMBER_TYPE_UINT16;
        case TOKEN_TYPE_UINT32: return NUMBER_TYPE_UINT32;
        case TOKEN_TYPE_UINT64: return NUMBER_TYPE_UINT64;
        case TOKEN_TYPE_FLOAT32: return NUMBER_TYPE_FLOAT32;
        case TOKEN_TYPE_FLOAT64: return NUMBER_TYPE_FLOAT64;
        default: return NUMBER_TYPE_UNKNOWN;
    }
}

// Get type range limits
int64_t get_type_min_value(NumberType type) {
    switch (type) {
        case NUMBER_TYPE_INT8:   return INT8_MIN;
        case NUMBER_TYPE_INT16:  return INT16_MIN;
        case NUMBER_TYPE_INT32:  return INT32_MIN;
        case NUMBER_TYPE_INT64:  return INT64_MIN;
        case NUMBER_TYPE_UINT8:  return 0;
        case NUMBER_TYPE_UINT16: return 0;
        case NUMBER_TYPE_UINT32: return 0;
        case NUMBER_TYPE_UINT64: return 0;
        default: return INT64_MIN;
    }
}

uint64_t get_type_max_value(NumberType type) {
    switch (type) {
        case NUMBER_TYPE_INT8:   return INT8_MAX;
        case NUMBER_TYPE_INT16:  return INT16_MAX;
        case NUMBER_TYPE_INT32:  return INT32_MAX;
        case NUMBER_TYPE_INT64:  return INT64_MAX;
        case NUMBER_TYPE_UINT8:  return UINT8_MAX;
        case NUMBER_TYPE_UINT16: return UINT16_MAX;
        case NUMBER_TYPE_UINT32: return UINT32_MAX;
        case NUMBER_TYPE_UINT64: return UINT64_MAX;
        default: return UINT64_MAX;
    }
}

// Helper functions for creating NumberInfo
NumberInfo make_unknown_type(void) {
    NumberInfo type_info = {NUMBER_TYPE_UNKNOWN, false};
    return type_info;
}

NumberInfo make_annotated_type(NumberType type) {
    NumberInfo type_info = {type, true};
    return type_info;
}

// Check if types are compatible for conversion
bool types_are_compatible(NumberType from, NumberType to) {
    // Same type is always compatible
    if (from == to) return true;
    
    // Unknown type is compatible with anything
    if (from == NUMBER_TYPE_UNKNOWN || to == NUMBER_TYPE_UNKNOWN) return true;
    
    // All integer types can convert to each other (with range checking)
    if (is_integer_type(from) && is_integer_type(to)) return true;
    
    // Integers can convert to float
    if (is_integer_type(from) && is_float_type(to)) return true;
    
    // Float can convert to integers (with precision loss warning)
    if (is_float_type(from) && is_integer_type(to)) return true;
    
    return false;
}

// Type conversion with range checking
static bool value_fits_in_type(int64_t value, NumberType target_type) {
    int64_t min_val = get_type_min_value(target_type);
    uint64_t max_val = get_type_max_value(target_type);
    
    if (is_unsigned_type(target_type)) {
        // For unsigned types, check if value is negative or too large
        return value >= 0 && (uint64_t)value <= max_val;
    } else {
        // For signed types, check range
        return value >= min_val && value <= (int64_t)max_val;
    }
}

static NumericType mobius_type_to_numeric_type(NumberType type) {
    switch (type) {
        case NUMBER_TYPE_INT8:   return NUM_INT8;
        case NUMBER_TYPE_INT16:  return NUM_INT16;
        case NUMBER_TYPE_INT32:  return NUM_INT32;
        case NUMBER_TYPE_INT64:  return NUM_INT64;
        case NUMBER_TYPE_UINT8:  return NUM_UINT8;
        case NUMBER_TYPE_UINT16: return NUM_UINT16;
        case NUMBER_TYPE_UINT32: return NUM_UINT32;
        case NUMBER_TYPE_UINT64: return NUM_UINT64;
        default: return NUM_INT64; // Default fallback
    }
}

// Main type validation and conversion function
TypeConversionResult validate_and_convert_value(Value value, NumberInfo target_type, TypeCheckConfig config) {
    TypeConversionResult result = {false, {0}, NULL, false};
    
    // If no type annotation, accept any value
    if (!target_type.is_annotated || target_type.type == NUMBER_TYPE_UNKNOWN) {
        result.success = true;
        result.converted_value = copy_value(value);
        result.was_converted = false;
        return result;
    }
    
    // Handle integer target types
    if (is_integer_type(target_type.type)) {
        int64_t int_value = 0;
        bool conversion_needed = true;
        
        if (value.type == VAL_INTEGER) {
            int_value = value.as.integer.value.i64;
            conversion_needed = (target_type.type != NUMBER_TYPE_INT64);
        } else if (value.type == VAL_FLOAT32) {
            if (config.strict_mode) {
                result.error_message = mobius_strdup("Cannot convert float32 to integer in strict mode");
                return result;
            }
            int_value = (int64_t)value.as.float32_val;
            conversion_needed = true;
        } else if (value.type == VAL_FLOAT64) {
            if (config.strict_mode) {
                result.error_message = mobius_strdup("Cannot convert float to integer in strict mode");
                return result;
            }
            int_value = (int64_t)value.as.float64_val;
            conversion_needed = true;
        } else {
            result.error_message = malloc(256);
            snprintf(result.error_message, 256, "Cannot convert %s to %s", 
                    value_type_name(value.type), mobius_type_name(target_type.type));
            return result;
        }
        
        // Check if value fits in target type range
        if (!value_fits_in_type(int_value, target_type.type)) {
            result.error_message = malloc(256);
            snprintf(result.error_message, 256, "Value %ld out of range for %s", 
                    int_value, mobius_type_name(target_type.type));
            return result;
        }
        
        // Create the converted value
        NumericType num_type = mobius_type_to_numeric_type(target_type.type);
        result.converted_value = make_integer_value(num_type, int_value);
        result.success = true;
        result.was_converted = conversion_needed;
        return result;
    }
    
    // Handle float target types
    if (is_float_type(target_type.type)) {
        bool conversion_needed = false;
        
        if (target_type.type == NUMBER_TYPE_FLOAT32) {
            float float32_value = 0.0f;
            
            if (value.type == VAL_FLOAT32) {
                float32_value = value.as.float32_val;
            } else if (value.type == VAL_FLOAT64) {
                if (config.strict_mode) {
                    result.error_message = mobius_strdup("Cannot convert float64 to float32 in strict mode");
                    return result;
                }
                float32_value = (float)value.as.float64_val;
                conversion_needed = true;
            } else if (value.type == VAL_INTEGER) {
                if (config.strict_mode) {
                    result.error_message = mobius_strdup("Cannot convert integer to float32 in strict mode");
                    return result;
                }
                float32_value = (float)value.as.integer.value.i64;
                conversion_needed = true;
            } else {
                result.error_message = malloc(256);
                snprintf(result.error_message, 256, "Cannot convert %s to %s", 
                        value_type_name(value.type), mobius_type_name(target_type.type));
                return result;
            }
            
            result.converted_value = make_float32_value(float32_value);
        } else { // NUMBER_TYPE_FLOAT64 (float64)
            double float_value = 0.0;
            
            if (value.type == VAL_FLOAT64) {
                float_value = value.as.float64_val;
            } else if (value.type == VAL_FLOAT32) {
                float_value = (double)value.as.float32_val;
                conversion_needed = true;
            } else if (value.type == VAL_INTEGER) {
                if (config.strict_mode) {
                    result.error_message = mobius_strdup("Cannot convert integer to float in strict mode");
                    return result;
                }
                float_value = (double)value.as.integer.value.i64;
                conversion_needed = true;
            } else {
                result.error_message = malloc(256);
                snprintf(result.error_message, 256, "Cannot convert %s to %s", 
                        value_type_name(value.type), mobius_type_name(target_type.type));
                return result;
            }
            
            result.converted_value = make_float_value(float_value);
        }
        
        result.success = true;
        result.was_converted = conversion_needed;
        return result;
    }
    
    // Unknown target type - shouldn't happen
    result.error_message = mobius_strdup("Unknown target type");
    return result;
}
