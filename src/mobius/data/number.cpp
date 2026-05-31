#include "data/number.h"

const char* number_type_name(NumberType type) {
    switch (type) {
        case NUM_UNKNOWN: return "unknown";
        case NUM_INT64:   return "int64";
        case NUM_UINT64:  return "uint64";
        case NUM_FLOAT64: return "float64";
        default: return "invalid";
    }
}

bool is_integer_type(NumberType type) {
    return type == NUM_INT64 || type == NUM_UINT64;
}

bool is_unsigned_type(NumberType type) {
    return type == NUM_UINT64;
}

bool is_float_type(NumberType type) {
    return type == NUM_FLOAT64;
}
