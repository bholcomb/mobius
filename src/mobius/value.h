#ifndef MOBIUS_VALUE_H
#define MOBIUS_VALUE_H

#include "token.h"
#include <stdbool.h>
#include <stdint.h>

// Forward declarations (MobiusFunction, Table, etc. will be defined elsewhere)
typedef struct MobiusFunction MobiusFunction;
typedef struct Table Table;
typedef struct TableEntry TableEntry;

// Value types for literals
typedef enum {
    VAL_NIL,
    VAL_BOOL,
    VAL_INTEGER,
    VAL_FLOAT32,
    VAL_FLOAT,
    VAL_STRING,
    VAL_CHAR,
    VAL_FUNCTION,
    VAL_TABLE,
    VAL_USERDATA
} ValueType;

// Forward declaration for userdata destructor
typedef void (*UserdataDestructor)(void* ptr);

// Type system enums (for type annotations)
typedef enum {
    MOBIUS_TYPE_UNKNOWN,    // No type specified/inferred
    MOBIUS_TYPE_INT8,       // int8: -128 to 127
    MOBIUS_TYPE_INT16,      // int16: -32,768 to 32,767
    MOBIUS_TYPE_INT32,      // int32: ~2.1 billion range
    MOBIUS_TYPE_INT64,      // int64: very large range
    MOBIUS_TYPE_UINT8,      // uint8: 0 to 255
    MOBIUS_TYPE_UINT16,     // uint16: 0 to 65,535
    MOBIUS_TYPE_UINT32,     // uint32: ~4.3 billion
    MOBIUS_TYPE_UINT64,     // uint64: very large positive range
    MOBIUS_TYPE_FLOAT32,    // float32: single precision
    MOBIUS_TYPE_FLOAT,      // float: double precision (alias for float64)
} MobiusType;

// Type information structure
typedef struct {
    MobiusType type;
    bool is_annotated;      // true if explicitly specified by user
} TypeInfo;

// Runtime value representation
typedef struct {
    ValueType type;
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
        } integer;
        float float32_val;
        double float_val;
        char* string;
        char character;
        MobiusFunction* function;
        Table* table;
        struct {
            void* ptr;                        // Opaque pointer to user data
            UserdataDestructor destructor;    // Cleanup function (can be NULL)
            const char* type_name;            // Type identifier for runtime checks
            size_t size;                      // Size of the data (for debugging/GC)
        } userdata;
    } as;
} Value;

// Value creation functions
Value make_nil_value();
Value make_bool_value(bool value);
Value make_integer_value(NumericType type, int64_t value);
Value make_float32_value(float value);
Value make_float_value(double value);
Value make_string_value(char* string);
Value make_char_value(char value);
Value make_function_value(MobiusFunction* function);
Value make_table_value(Table* table);
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

#endif // MOBIUS_VALUE_H

