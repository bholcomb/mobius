#include "types.h"
#include "ast.h"
#include "token.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

#define _DEFAULT_SOURCE  // For strdup on some systems

// Define strdup if not available
#ifndef strdup
char* strdup(const char* s) {
    size_t len = strlen(s) + 1;
    char* copy = malloc(len);
    if (copy) {
        memcpy(copy, s, len);
    }
    return copy;
}
#endif

// Type name mappings
const char* mobius_type_name(MobiusType type) {
    switch (type) {
        case MOBIUS_TYPE_UNKNOWN: return "unknown";
        case MOBIUS_TYPE_INT8:    return "int8";
        case MOBIUS_TYPE_INT16:   return "int16";
        case MOBIUS_TYPE_INT32:   return "int32";
        case MOBIUS_TYPE_INT64:   return "int64";
        case MOBIUS_TYPE_UINT8:   return "uint8";
        case MOBIUS_TYPE_UINT16:  return "uint16";
        case MOBIUS_TYPE_UINT32:  return "uint32";
        case MOBIUS_TYPE_UINT64:  return "uint64";
        case MOBIUS_TYPE_FLOAT32: return "float32";
        case MOBIUS_TYPE_FLOAT:   return "float";
        default: return "invalid";
    }
}

// Type category checks
bool is_integer_type(MobiusType type) {
    return type >= MOBIUS_TYPE_INT8 && type <= MOBIUS_TYPE_UINT64;
}

bool is_unsigned_type(MobiusType type) {
    return type >= MOBIUS_TYPE_UINT8 && type <= MOBIUS_TYPE_UINT64;
}

bool is_float_type(MobiusType type) {
    return type == MOBIUS_TYPE_FLOAT32 || type == MOBIUS_TYPE_FLOAT;
}

// Convert token type to Mobius type
MobiusType token_to_mobius_type(TokenType token_type) {
    switch (token_type) {
        case TOKEN_TYPE_INT8:   return MOBIUS_TYPE_INT8;
        case TOKEN_TYPE_INT16:  return MOBIUS_TYPE_INT16;
        case TOKEN_TYPE_INT32:  return MOBIUS_TYPE_INT32;
        case TOKEN_TYPE_INT64:  return MOBIUS_TYPE_INT64;
        case TOKEN_TYPE_UINT8:  return MOBIUS_TYPE_UINT8;
        case TOKEN_TYPE_UINT16: return MOBIUS_TYPE_UINT16;
        case TOKEN_TYPE_UINT32: return MOBIUS_TYPE_UINT32;
        case TOKEN_TYPE_UINT64: return MOBIUS_TYPE_UINT64;
        case TOKEN_TYPE_FLOAT32: return MOBIUS_TYPE_FLOAT32;
        case TOKEN_TYPE_FLOAT64: return MOBIUS_TYPE_FLOAT;
        default: return MOBIUS_TYPE_UNKNOWN;
    }
}

// Get type range limits
int64_t get_type_min_value(MobiusType type) {
    switch (type) {
        case MOBIUS_TYPE_INT8:   return INT8_MIN;
        case MOBIUS_TYPE_INT16:  return INT16_MIN;
        case MOBIUS_TYPE_INT32:  return INT32_MIN;
        case MOBIUS_TYPE_INT64:  return INT64_MIN;
        case MOBIUS_TYPE_UINT8:  return 0;
        case MOBIUS_TYPE_UINT16: return 0;
        case MOBIUS_TYPE_UINT32: return 0;
        case MOBIUS_TYPE_UINT64: return 0;
        default: return INT64_MIN;
    }
}

uint64_t get_type_max_value(MobiusType type) {
    switch (type) {
        case MOBIUS_TYPE_INT8:   return INT8_MAX;
        case MOBIUS_TYPE_INT16:  return INT16_MAX;
        case MOBIUS_TYPE_INT32:  return INT32_MAX;
        case MOBIUS_TYPE_INT64:  return INT64_MAX;
        case MOBIUS_TYPE_UINT8:  return UINT8_MAX;
        case MOBIUS_TYPE_UINT16: return UINT16_MAX;
        case MOBIUS_TYPE_UINT32: return UINT32_MAX;
        case MOBIUS_TYPE_UINT64: return UINT64_MAX;
        default: return UINT64_MAX;
    }
}

// Helper functions for creating TypeInfo
TypeInfo make_unknown_type(void) {
    TypeInfo type_info = {MOBIUS_TYPE_UNKNOWN, false};
    return type_info;
}

TypeInfo make_annotated_type(MobiusType type) {
    TypeInfo type_info = {type, true};
    return type_info;
}

// Check if types are compatible for conversion
bool types_are_compatible(MobiusType from, MobiusType to) {
    // Same type is always compatible
    if (from == to) return true;
    
    // Unknown type is compatible with anything
    if (from == MOBIUS_TYPE_UNKNOWN || to == MOBIUS_TYPE_UNKNOWN) return true;
    
    // All integer types can convert to each other (with range checking)
    if (is_integer_type(from) && is_integer_type(to)) return true;
    
    // Integers can convert to float
    if (is_integer_type(from) && is_float_type(to)) return true;
    
    // Float can convert to integers (with precision loss warning)
    if (is_float_type(from) && is_integer_type(to)) return true;
    
    return false;
}

// Type conversion with range checking
static bool value_fits_in_type(int64_t value, MobiusType target_type) {
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

static NumericType mobius_type_to_numeric_type(MobiusType type) {
    switch (type) {
        case MOBIUS_TYPE_INT8:   return NUM_INT8;
        case MOBIUS_TYPE_INT16:  return NUM_INT16;
        case MOBIUS_TYPE_INT32:  return NUM_INT32;
        case MOBIUS_TYPE_INT64:  return NUM_INT64;
        case MOBIUS_TYPE_UINT8:  return NUM_UINT8;
        case MOBIUS_TYPE_UINT16: return NUM_UINT16;
        case MOBIUS_TYPE_UINT32: return NUM_UINT32;
        case MOBIUS_TYPE_UINT64: return NUM_UINT64;
        default: return NUM_INT64; // Default fallback
    }
}

// Main type validation and conversion function
TypeConversionResult validate_and_convert_value(Value value, TypeInfo target_type, TypeCheckConfig config) {
    TypeConversionResult result = {false, {0}, NULL, false};
    
    // If no type annotation, accept any value
    if (!target_type.is_annotated || target_type.type == MOBIUS_TYPE_UNKNOWN) {
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
            conversion_needed = (target_type.type != MOBIUS_TYPE_INT64);
        } else if (value.type == VAL_FLOAT32) {
            if (config.strict_mode) {
                result.error_message = strdup("Cannot convert float32 to integer in strict mode");
                return result;
            }
            int_value = (int64_t)value.as.float32_val;
            conversion_needed = true;
        } else if (value.type == VAL_FLOAT) {
            if (config.strict_mode) {
                result.error_message = strdup("Cannot convert float to integer in strict mode");
                return result;
            }
            int_value = (int64_t)value.as.float_val;
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
        
        if (target_type.type == MOBIUS_TYPE_FLOAT32) {
            float float32_value = 0.0f;
            
            if (value.type == VAL_FLOAT32) {
                float32_value = value.as.float32_val;
            } else if (value.type == VAL_FLOAT) {
                if (config.strict_mode) {
                    result.error_message = strdup("Cannot convert float64 to float32 in strict mode");
                    return result;
                }
                float32_value = (float)value.as.float_val;
                conversion_needed = true;
            } else if (value.type == VAL_INTEGER) {
                if (config.strict_mode) {
                    result.error_message = strdup("Cannot convert integer to float32 in strict mode");
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
        } else { // MOBIUS_TYPE_FLOAT (float64)
            double float_value = 0.0;
            
            if (value.type == VAL_FLOAT) {
                float_value = value.as.float_val;
            } else if (value.type == VAL_FLOAT32) {
                float_value = (double)value.as.float32_val;
                conversion_needed = true;
            } else if (value.type == VAL_INTEGER) {
                if (config.strict_mode) {
                    result.error_message = strdup("Cannot convert integer to float in strict mode");
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
    result.error_message = strdup("Unknown target type");
    return result;
}
