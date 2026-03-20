#ifndef MOBIUS_VALUE_H
#define MOBIUS_VALUE_H

#include "internal/string_intern.h"
#include "eval/evalResult.h"
#include "data/number.h"

struct MobiusFunction;
class ArrayValue;
struct EnumValue;
class EnumDefinition;
class Table;
class MobiusState;

typedef int (*MobiusCFunction)(MobiusState* ctx, int arg_count);
typedef void (*UserdataDestructor)(void* ptr);

typedef enum {
    VAL_NIL,
    VAL_BOOL,
    VAL_INTEGER,
    VAL_FLOAT32,
    VAL_FLOAT64,
    VAL_STRING,
    VAL_CHAR,
    VAL_ARRAY,
    VAL_FUNCTION,
    VAL_NATIVE_FUNCTION,
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

    Value();
    ~Value();
    Value(const Value& other);
    Value& operator=(const Value& other);
    Value(Value&& other) noexcept;
    Value& operator=(Value&& other) noexcept;

    bool operator==(const Value& other) const;
    bool operator!=(const Value& other) const { return !(*this == other); }

    bool exactlyEqual(const Value& other) const;

    static Value makeEnum(EnumDefinition* definition, int64_t value);

private:
    void retain() const;
    void releaseRef();
};

// Value creation functions
Value make_nil_value();
Value make_bool_value(bool value);
Value make_char_value(char value);
Value make_integer_value(NumberType type, int64_t value);
Value make_float32_value(float value);
Value make_float_value(double value);
Value make_string_value(MobiusString* string);
Value make_string_value_from_cstr(MobiusState* state, const char* cstr);
Value make_function_value(struct MobiusFunction* function);
Value make_native_function_value(MobiusCFunction function);
Value make_userdata_value(void* ptr, UserdataDestructor destructor, const char* type_name, size_t size);
Value make_array_value(ArrayValue* array);
Value make_table_value(struct Table* table);

// Value utility functions
bool is_truthy(const Value& value);
void print_value(const Value& value);
char* value_to_string(const Value& value);
const char* value_type_name(ValueType type);

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
