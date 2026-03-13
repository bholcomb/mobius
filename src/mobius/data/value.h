#ifndef MOBIUS_VALUE_H
#define MOBIUS_VALUE_H

#include "internal/string_intern.h"
#include "eval/evalResult.h"
#include "data/number.h"

// Forward declarations (MobiusFunction, Table, etc. will be defined elsewhere)
struct MobiusFunction;
struct ArrayValue;
struct EnumValue;
struct EnumDefinition;
struct Table;
struct MobiusState;

// Native function signature: return number of values pushed (>= 0) on success,
// or a negative value (via mobius_error()) on failure.
typedef int (*MobiusCFunction)(struct MobiusState* ctx, int arg_count);

// Forward declaration for userdata destructor
typedef void (*UserdataDestructor)(void* ptr);

// Value types for literals 
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

// Runtime value representation
typedef struct {
    union {
        bool boolean;
        struct {
            NumberType num_type;
            union {
                int8_t   i8;    uint8_t  u8;
                int16_t  i16;   uint16_t u16;
                int32_t  i32;   uint32_t u32;
                int64_t  i64;   uint64_t u64;
            } value;
        } integer;                              // 8 bytes

        float float32_val;                      // 4 bytes
        double float64_val;                     // 8 bytes

        MobiusString* string;                   // 8 bytes (interned, immutable)
        char character;                         // 1 byte

        struct ArrayValue* array;                      // 8 bytes
        struct MobiusFunction* function;               // 8 bytes
        MobiusCFunction native_function;        // 8 bytes
        struct Table* table;                    // 8 bytes
        struct {
            void* ptr;                        // Opaque pointer to user data 
            UserdataDestructor destructor;    // Cleanup function (can be NULL)
            const char* type_name;            // Type identifier for runtime checks
            size_t size;                      // Size of the data (for debugging/GC)
        } userdata;                           // 32 bytes

        struct {
            struct EnumDefinition* definition;   // Shared enum definition
            int32_t value;                // The actual enum value
        } enum_val;                       //12 bytes

    } as;
    ValueType type;                        //1 byte
} Value;

// Value creation functions
Value make_nil_value();
Value make_bool_value(bool value);
Value make_char_value(char value);
Value make_integer_value(NumberType type, int64_t value);
Value make_float32_value(float value);
Value make_float_value(double value);
Value make_string_value(MobiusString* string);
Value make_string_value_from_cstr(struct MobiusState* state, const char* cstr);
Value make_function_value(struct MobiusFunction* function);
Value make_native_function_value(MobiusCFunction function);
Value make_userdata_value(void* ptr, UserdataDestructor destructor, const char* type_name, size_t size);
Value make_array_value(struct ArrayValue* array);
Value make_table_value(struct Table* table);

// Value utility functions
bool is_truthy(Value value);
bool values_equal(Value a, Value b);
void print_value(Value value);
char* value_to_string(Value value);
const char* value_type_name(ValueType type);

// Memory management
Value copy_value(Value value);
void free_value(Value value);

// Type conversion result
typedef struct {
    bool success;
    Value converted_value;
    char* error_message;    // NULL if successful, caller must free
    bool was_converted;     // true if conversion was needed
} TypeConversionResult;

typedef struct {
    bool strict_mode;
    bool warn_on_conversion;
} TypeCheckConfig;

// Type validation and conversion
TypeConversionResult validate_and_convert_value(Value value, NumberType target_type, bool is_annotated, TypeCheckConfig config);



#endif // MOBIUS_VALUE_H

