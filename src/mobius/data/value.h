#ifndef MOBIUS_VALUE_H
#define MOBIUS_VALUE_H

#include "internal/string_intern.h"
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
    VAL_INT64,   // signed int64_t  — stored in as.i64
    VAL_UINT64,    // unsigned uint64_t — stored in as.u64
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
        int64_t  i64;    // VAL_INT64
        uint64_t u64;    // VAL_UINT64
        double double_val;

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

inline Value make_int64_value(int64_t val) {
    Value value;
    value.type = VAL_INT64;
    value.as.i64 = val;
    return value;
}

inline Value make_uint64_value(uint64_t val) {
    Value value;
    value.type = VAL_UINT64;
    value.as.u64 = val;
    return value;
}

// Compatibility shim — use make_int64_value / make_uint64_value in new code.
inline Value make_integer_value(NumberType numtype, int64_t val) {
    if (numtype == NUM_UINT64) return make_uint64_value((uint64_t)val);
    return make_int64_value(val);
}

inline Value make_float_value(double val) {
    Value value;
    value.type = VAL_FLOAT64;
    value.as.double_val = val;
    return value;
}

MOBIUS_API Value make_string_value(MobiusString* string);
MOBIUS_API Value make_string_value_from_cstr(MobiusState* state, const char* cstr);
MOBIUS_API Value make_function_value(struct MobiusFunction* function);
MOBIUS_API Value make_native_function_value(MobiusCFunction function);
MOBIUS_API Value make_userdata_value(void* ptr, UserdataDestructor destructor, const char* type_name, size_t size);
MOBIUS_API Value make_array_value(ArrayValue* array);
MOBIUS_API Value make_table_value(struct Table* table);

inline bool is_truthy(const Value& value) {
    switch (value.type) {
        case VAL_NIL: return false;
        case VAL_BOOL: return value.as.boolean;
        case VAL_INT64: return value.as.i64 != 0;
        case VAL_UINT64:  return value.as.u64 != 0;
        case VAL_FLOAT64: return value.as.double_val != 0.0;
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
