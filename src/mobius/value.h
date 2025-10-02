#ifndef MOBIUS_VALUE_H
#define MOBIUS_VALUE_H

#include "token.h"
#include <stdbool.h>
#include <stdint.h>

// Reference counting for strings is always enabled

// Forward declarations (MobiusFunction, Table, etc. will be defined elsewhere)
typedef struct MobiusFunction MobiusFunction;
typedef struct ArrayValue ArrayValue;
typedef struct EnumValue EnumValue;
typedef struct EnumDefinition EnumDefinition;
struct Table;
struct TableEntry;

// Reference counted string structure
typedef struct RefCountedString {
    char* data;           // Actual string data (null-terminated)
    size_t length;        // Cached string length for O(1) operations
    int ref_count;        // Reference count
    bool is_literal;      // True for string literals (never freed)
} RefCountedString;

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
    VAL_TABLE,
    VAL_USERDATA,
    VAL_ENUM
} ValueType;

// Forward declaration for userdata destructor
typedef void (*UserdataDestructor)(void* ptr);


// Type system enums (for type annotations)
typedef enum {
    NUMBER_TYPE_UNKNOWN,    // No type specified/inferred
    NUMBER_TYPE_INT8,       // int8: -128 to 127
    NUMBER_TYPE_INT16,      // int16: -32,768 to 32,767
    NUMBER_TYPE_INT32,      // int32: ~2.1 billion range
    NUMBER_TYPE_INT64,      // int64: very large range
    NUMBER_TYPE_UINT8,      // uint8: 0 to 255
    NUMBER_TYPE_UINT16,     // uint16: 0 to 65,535
    NUMBER_TYPE_UINT32,     // uint32: ~4.3 billion
    NUMBER_TYPE_UINT64,     // uint64: very large positive range
    NUMBER_TYPE_FLOAT32,    // float32: single precision
    NUMBER_TYPE_FLOAT64,      // float: double precision (alias for float64)
} NumberType;

// Type information structure
typedef struct {
    NumberType type;
    bool is_annotated;      // true if explicitly specified by user
} NumberInfo;

// Runtime value representation
typedef struct {
    union {
        bool boolean;
        struct {
            NumericType num_type;
            union {
                int8_t   i8;    uint8_t  u8;
                int16_t  i16;   uint16_t u16;
                int32_t  i32;   uint32_t u32;
                int64_t  i64;   uint64_t u64;
            } value;
        } integer;                              // 8 bytes

        float float32_val;                      // 4 bytes
        double float64_val;                     // 8 bytes

        RefCountedString* string;               // 8 bytes
        char character;                         // 1 byte

        ArrayValue* array;                      // 8 bytes
        MobiusFunction* function;               // 8 bytes
        struct Table* table;                           // 8 bytes
        struct {
            void* ptr;                        // Opaque pointer to user data 
            UserdataDestructor destructor;    // Cleanup function (can be NULL)
            const char* type_name;            // Type identifier for runtime checks
            size_t size;                      // Size of the data (for debugging/GC)
        } userdata;                             // 32 bytes

        struct {
            EnumDefinition* definition;   // Shared enum definition
            int32_t value;               // The actual enum value
        } enum_val;                             //12 bytes

    } as;
    ValueType type;                             //1 byte
} Value;

// Dynamic array structure with reference counting
struct ArrayValue {
    Value* elements;      // Array of values
    size_t length;        // Current number of elements
    size_t capacity;      // Allocated capacity
    int ref_count;        // Reference count for memory management
};

// Value creation functions
Value make_nil_value();
Value make_bool_value(bool value);
Value make_integer_value(NumericType type, int64_t value);
Value make_float32_value(float value);
Value make_float_value(double value);
Value make_string_value(RefCountedString* string);
Value make_char_value(char value);
Value make_array_value(ArrayValue* array);
Value make_function_value(MobiusFunction* function);
Value make_table_value(struct Table* table);
Value make_userdata_value(void* ptr, UserdataDestructor destructor, const char* type_name, size_t size);

// Value utility functions
bool is_truthy(Value value);
bool values_equal(Value a, Value b);
void print_value(Value value);
char* value_to_string(Value value);
const char* value_type_name(ValueType type);

// Memory management
Value copy_value(Value value);
void free_value(Value value);

// String reference counting functions
RefCountedString* string_create(const char* data);
RefCountedString* string_create_literal(const char* data);
RefCountedString* string_retain(RefCountedString* str);
void string_release(RefCountedString* str);
size_t string_length(RefCountedString* str);
const char* string_data(RefCountedString* str);
bool string_equals(RefCountedString* a, RefCountedString* b);

// Helper function to create Value from C string
Value make_string_value_from_cstr(const char* cstr);

// Array management functions
ArrayValue* array_create(size_t initial_capacity);
ArrayValue* array_retain(ArrayValue* array);
void array_release(ArrayValue* array);
void array_push(ArrayValue* array, Value value);
Value array_pop(ArrayValue* array);
Value array_get(ArrayValue* array, size_t index);
void array_set(ArrayValue* array, size_t index, Value value);
size_t array_length(ArrayValue* array);
void array_resize(ArrayValue* array, size_t new_capacity);

#endif // MOBIUS_VALUE_H

