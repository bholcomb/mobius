#include "data/number.h"

//#include <string.h>
//#include <limits.h>

// Type name mappings
const char* number_type_name(NumberType type) {
    switch (type) {
        case NUM_UNKNOWN: return "unknown";
        case NUM_INT8:    return "int8";
        case NUM_INT16:   return "int16";
        case NUM_INT32:   return "int32";
        case NUM_INT64:   return "int64";
        case NUM_UINT8:   return "uint8";
        case NUM_UINT16:  return "uint16";
        case NUM_UINT32:  return "uint32";
        case NUM_UINT64:  return "uint64";
        case NUM_FLOAT32: return "float32";
        case NUM_FLOAT64:   return "float";
        default: return "invalid";
    }
}

// Type category checks
bool is_integer_type(NumberType type) {
    return type >= NUM_INT8 && type <= NUM_UINT64;
}

bool is_unsigned_type(NumberType type) {
    return type >= NUM_UINT8 && type <= NUM_UINT64;
}

bool is_float_type(NumberType type) {
    return type == NUM_FLOAT32 || type == NUM_FLOAT64;
}

// Get type range limits
int64_t get_type_min_value(NumberType type) {
    switch (type) {
        case NUM_INT8:   return INT8_MIN;
        case NUM_INT16:  return INT16_MIN;
        case NUM_INT32:  return INT32_MIN;
        case NUM_INT64:  return INT64_MIN;
        case NUM_UINT8:  return 0;
        case NUM_UINT16: return 0;
        case NUM_UINT32: return 0;
        case NUM_UINT64: return 0;
        default: return INT64_MIN;
    }
}

uint64_t get_type_max_value(NumberType type) {
    switch (type) {
        case NUM_INT8:   return INT8_MAX;
        case NUM_INT16:  return INT16_MAX;
        case NUM_INT32:  return INT32_MAX;
        case NUM_INT64:  return INT64_MAX;
        case NUM_UINT8:  return UINT8_MAX;
        case NUM_UINT16: return UINT16_MAX;
        case NUM_UINT32: return UINT32_MAX;
        case NUM_UINT64: return UINT64_MAX;
        default: return UINT64_MAX;
    }
}

// Check if types are compatible for conversion
bool types_are_compatible(NumberType from, NumberType to) {
    // Same type is always compatible
    if (from == to) return true;
    
    // Unknown type is compatible with anything
    if (from == NUM_UNKNOWN || to == NUM_UNKNOWN) return true;
    
    // All integer types can convert to each other (with range checking)
    if (is_integer_type(from) && is_integer_type(to)) return true;
    
    // Integers can convert to float
    if (is_integer_type(from) && is_float_type(to)) return true;
    
    // Float can convert to integers (with precision loss warning)
    if (is_float_type(from) && is_integer_type(to)) return true;
    
    return false;
}

// Type conversion with range checking
bool value_fits_in_type(int64_t value, NumberType target_type) {
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
