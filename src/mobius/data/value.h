#ifndef MOBIUS_VALUE_H
#define MOBIUS_VALUE_H

#include "internal/string_intern.h"
#include "eval/evalResult.h"
#include "data/number.h"

#include <cstring>
#include <mobius/mobius.h>

struct MobiusFunction;
class ArrayValue;
struct EnumValue;
class EnumDefinition;
class Table;
class MobiusState;

typedef int (*MobiusCFunction)(MobiusState* ctx, int arg_count);
typedef void (*UserdataDestructor)(void* ptr);

typedef enum {
    // Non-refcounted (inline) types — must stay below VAL_STRING
    VAL_NIL,
    VAL_BOOL,
    VAL_INTEGER,
    VAL_FLOAT32,
    VAL_FLOAT64,
    VAL_CHAR,
    VAL_NATIVE_FUNCTION,
    // Refcounted (heap-allocated) types — must stay at VAL_STRING or above
    VAL_STRING,
    VAL_ARRAY,
    VAL_FUNCTION,
    VAL_TABLE,
    VAL_USERDATA,
    VAL_ENUM
} ValueType;

class Value {
public:
    union ValueData {
        bool boolean;
        struct {
            NumberType num_type;
            union {
                int8_t   i8;    uint8_t  u8;
                int16_t  i16;   uint16_t u16;
                int32_t  i32;   uint32_t u32;
                int64_t  i64;   uint64_t u64;
            } value;
        } integer;

        float float32_val;
        double float64_val;

        MobiusString* string;
        char character;

        ArrayValue* array;
        struct MobiusFunction* function;
        MobiusCFunction native_function;
        struct Table* table;
        struct {
            void* ptr;
            UserdataDestructor destructor;
            const char* type_name;
            size_t size;
        } userdata;

        struct {
            struct EnumDefinition* definition;
            int32_t value;
        } enum_val;

        ValueData() : boolean(false) {}
    } as;
    ValueType type;

    Value() : type(VAL_NIL) { as.boolean = false; }

    ~Value() { releaseRef(); }

    Value(const Value& other) : type(other.type) {
        memcpy(&as, &other.as, sizeof(as));
        retain();
    }

    Value& operator=(const Value& other) {
        if (this != &other) {
            releaseRef();
            type = other.type;
            memcpy(&as, &other.as, sizeof(as));
            retain();
        }
        return *this;
    }

    Value(Value&& other) noexcept : type(other.type) {
        memcpy(&as, &other.as, sizeof(as));
        other.type = VAL_NIL;
    }

    Value& operator=(Value&& other) noexcept {
        if (this != &other) {
            releaseRef();
            type = other.type;
            memcpy(&as, &other.as, sizeof(as));
            other.type = VAL_NIL;
        }
        return *this;
    }

    MOBIUS_API bool operator==(const Value& other) const;
    bool operator!=(const Value& other) const { return !(*this == other); }

    bool exactlyEqual(const Value& other) const;

    static Value makeEnum(EnumDefinition* definition, int64_t value);

private:
    void retain() const {
        if (type < VAL_STRING) return;
        retainSlow();
    }
    void releaseRef() {
        if (type < VAL_STRING) return;
        releaseRefSlow();
    }
    MOBIUS_API void retainSlow() const;
    MOBIUS_API void releaseRefSlow();
};

// Value creation functions
MOBIUS_API Value make_nil_value();
MOBIUS_API Value make_bool_value(bool value);
MOBIUS_API Value make_char_value(char value);

inline Value make_integer_value(NumberType numtype, int64_t val) {
    Value value;
    value.type = VAL_INTEGER;
    value.as.integer.num_type = numtype;
    switch (numtype) {
        case NUM_INT8:   value.as.integer.value.i8  = (int8_t)val; break;
        case NUM_UINT8:  value.as.integer.value.u8  = (uint8_t)val; break;
        case NUM_INT16:  value.as.integer.value.i16 = (int16_t)val; break;
        case NUM_UINT16: value.as.integer.value.u16 = (uint16_t)val; break;
        case NUM_INT32:  value.as.integer.value.i32 = (int32_t)val; break;
        case NUM_UINT32: value.as.integer.value.u32 = (uint32_t)val; break;
        case NUM_INT64:  value.as.integer.value.i64 = val; break;
        case NUM_UINT64: value.as.integer.value.u64 = (uint64_t)val; break;
        default: break;
    }
    return value;
}

MOBIUS_API Value make_float32_value(float value);

inline Value make_float_value(double val) {
    Value value;
    value.type = VAL_FLOAT64;
    value.as.float64_val = val;
    return value;
}

MOBIUS_API Value make_string_value(MobiusString* string);
MOBIUS_API Value make_string_value_from_cstr(MobiusState* state, const char* cstr);
MOBIUS_API Value make_function_value(struct MobiusFunction* function);
MOBIUS_API Value make_native_function_value(MobiusCFunction function);
MOBIUS_API Value make_userdata_value(void* ptr, UserdataDestructor destructor, const char* type_name, size_t size);
MOBIUS_API Value make_array_value(ArrayValue* array);
MOBIUS_API Value make_table_value(struct Table* table);

// Value utility functions
inline bool is_truthy(const Value& value) {
    switch (value.type) {
        case VAL_NIL: return false;
        case VAL_BOOL: return value.as.boolean;
        case VAL_INTEGER: {
            switch (value.as.integer.num_type) {
                case NUM_INT8:   return value.as.integer.value.i8 != 0;
                case NUM_UINT8:  return value.as.integer.value.u8 != 0;
                case NUM_INT16:  return value.as.integer.value.i16 != 0;
                case NUM_UINT16: return value.as.integer.value.u16 != 0;
                case NUM_INT32:  return value.as.integer.value.i32 != 0;
                case NUM_UINT32: return value.as.integer.value.u32 != 0;
                case NUM_INT64:  return value.as.integer.value.i64 != 0;
                case NUM_UINT64: return value.as.integer.value.u64 != 0;
                default: return false;
            }
        }
        case VAL_FLOAT32: return value.as.float32_val != 0.0f;
        case VAL_FLOAT64: return value.as.float64_val != 0.0;
        case VAL_STRING: return value.as.string != nullptr && value.as.string->length > 0;
        case VAL_CHAR: return value.as.character != '\0';
        case VAL_ARRAY: return value.as.array != nullptr;
        case VAL_FUNCTION: return value.as.function != nullptr;
        case VAL_NATIVE_FUNCTION: return value.as.native_function != nullptr;
        case VAL_TABLE: return value.as.table != nullptr;
        case VAL_USERDATA: return value.as.userdata.ptr != nullptr;
        case VAL_ENUM: return true;
        default: return false;
    }
}

MOBIUS_API void print_value(const Value& value);
MOBIUS_API char* value_to_string(const Value& value);
MOBIUS_API const char* value_type_name(ValueType type);
Value increment_integer(Value val, bool is_increment, bool* success);

// Type conversion result
typedef struct {
    bool success;
    Value converted_value;
    char* error_message;
    bool was_converted;
} TypeConversionResult;

typedef struct {
    bool strict_mode;
    bool warn_on_conversion;
} TypeCheckConfig;

TypeConversionResult validate_and_convert_value(const Value& value, NumberType target_type, bool is_annotated, TypeCheckConfig config);

#endif // MOBIUS_VALUE_H
