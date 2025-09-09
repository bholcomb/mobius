#ifndef MOBIUS_VALUE_H
#define MOBIUS_VALUE_H

#include "token.h"
#include <stdbool.h>
#include <stdint.h>

// Reference counting for strings is always enabled

// Forward declarations (MobiusFunction, Table, etc. will be defined elsewhere)
typedef struct MobiusFunction MobiusFunction;
typedef struct Table Table;
typedef struct TableEntry TableEntry;
typedef struct ArrayValue ArrayValue;
typedef struct EnumValue EnumValue;
typedef struct EnumDefinition EnumDefinition;

// Reference counted string structure
typedef struct RefCountedString {
    char* data;           // Actual string data (null-terminated)
    size_t length;        // Cached string length for O(1) operations
    int ref_count;        // Reference count
    bool is_literal;      // True for string literals (never freed)
} RefCountedString;

// Dynamic array structure with reference counting (defined after Value)
struct ArrayValue;


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
    VAL_FUNCTION,           // Function (AST or builtin)
    VAL_TABLE,
    VAL_USERDATA,
    VAL_ENUM
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
        double float64_val;
        RefCountedString* string;
        char character;
        ArrayValue* array;
        MobiusFunction* function;               // Function (AST or builtin)
        Table* table;
        struct {
            void* ptr;                        // Opaque pointer to user data
            UserdataDestructor destructor;    // Cleanup function (can be NULL)
            const char* type_name;            // Type identifier for runtime checks
            size_t size;                      // Size of the data (for debugging/GC)
        } userdata;
        struct {
            EnumDefinition* definition;   // Shared enum definition
            int32_t value;               // The actual enum value
        } enum_val;
    } as;
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

// Enum value member structure
typedef struct EnumMember {
    char* name;                    // Member name (e.g., "RED")
    int64_t value;                // Member value (stored as largest type)
    struct EnumMember* next;      // Linked list for easy iteration
} EnumMember;

// Enum definition structure
struct EnumDefinition {
    char* name;                   // Enum name (e.g., "Color")
    NumericType underlying_type;  // Underlying integer type (int32 default)
    EnumMember* members;          // Linked list of enum members
    int ref_count;               // Reference count for memory management
    int64_t next_auto_value;     // Next auto-assigned value
};

// Enum utility functions
EnumDefinition* enum_definition_create(const char* name, NumericType underlying_type);
EnumDefinition* enum_definition_retain(EnumDefinition* enum_def);
void enum_definition_release(EnumDefinition* enum_def);
void enum_definition_add_member(EnumDefinition* enum_def, const char* name, int64_t value);
void enum_definition_add_auto_member(EnumDefinition* enum_def, const char* name);
EnumMember* enum_definition_find_member(EnumDefinition* enum_def, const char* name);
EnumMember* enum_definition_find_member_by_value(EnumDefinition* enum_def, int64_t value);

// Enum value functions
Value make_enum_value(EnumDefinition* definition, int64_t value);
bool enum_values_equal(Value a, Value b);
const char* enum_value_name(Value enum_val);

#endif // MOBIUS_VALUE_H

