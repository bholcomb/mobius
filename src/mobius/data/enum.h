#ifndef MOBIUS_ENUM_VALUE_H
#define MOBIUS_ENUM_VALUE_H

#include "data/value.h"
#include "data/number.h"

// Enum value member structure
typedef struct EnumMember {
    char* name;                    // Member name (e.g., "RED")
    int64_t value;                // Member value (stored as largest type)
} EnumMember;

struct EnumMemberArray;  //defined in the implementation file

// Enum definition structure
typedef struct EnumDefinition {
    char* name;                   // Enum name (e.g., "Color")
    struct EnumMemberArray* members;     // array of enum members
    int64_t next_auto_value;      // Next auto-assigned value
    int ref_count;                // Reference count for memory management
    NumberType underlying_type;  // Underlying integer type (int32 default)
} EnumDefinition;

// Enum utility functions
EnumDefinition* enum_definition_create(const char* name, NumberType underlying_type);
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

#endif // MOBIUS_ENUM_VALUE_H