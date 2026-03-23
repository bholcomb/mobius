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
MOBIUS_API Value make_integer_value(NumberType type, int64_t value);
MOBIUS_API Value make_float32_value(float value);
MOBIUS_API Value make_float_value(double value);
MOBIUS_API Value make_string_value(MobiusString* string);
MOBIUS_API Value make_string_value_from_cstr(MobiusState* state, const char* cstr);
MOBIUS_API Value make_function_value(struct MobiusFunction* function);
MOBIUS_API Value make_native_function_value(MobiusCFunction function);
MOBIUS_API Value make_userdata_value(void* ptr, UserdataDestructor destructor, const char* type_name, size_t size);
MOBIUS_API Value make_array_value(ArrayValue* array);
MOBIUS_API Value make_table_value(struct Table* table);

// Value utility functions
MOBIUS_API bool is_truthy(const Value& value);
MOBIUS_API void print_value(const Value& value);
MOBIUS_API char* value_to_string(const Value& value);
MOBIUS_API const char* value_type_name(ValueType type);

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
