#include "data/enum.h"



// instantiate the enum member array by #including this file
#define DARRAY_T EnumMember
#define DARRAY_PREFIX enumMember_
#define DARRAY_NAME EnumMemberArray
#include "internal/data_array.h"


// ============================================================================
// ENUM IMPLEMENTATION
// ============================================================================

EnumDefinition* enum_definition_create(const char* name, NumberType underlying_type) {
    EnumDefinition* enum_def = calloc(1, sizeof(EnumDefinition));
    if (!enum_def) return NULL;
    
    enum_def->name = malloc(strlen(name) + 1);
    if (!enum_def->name) {
        free(enum_def);
        return NULL;
    }
    strcpy(enum_def->name, name);

    EnumMemberArray* members = calloc(1, sizeof(EnumMemberArray));
    if(!members) {
        free(enum_def->name);
        free(enum_def);
        return NULL;
    }

    //expect about 5 members for enums
    enumMember_reserve(members, 5);
    
    enum_def->underlying_type = underlying_type;
    enum_def->members = members;
    enum_def->ref_count = 1;
    enum_def->next_auto_value = 0;
    
    return enum_def;
}

EnumDefinition* enum_definition_retain(EnumDefinition* enum_def) {
    if (enum_def) {
        enum_def->ref_count++;
    }
    return enum_def;
}

void enum_definition_release(EnumDefinition* enum_def) {
    if (!enum_def) return;
    
    enum_def->ref_count--;
    if (enum_def->ref_count <= 0) {
        // Free all members
        EnumMemberArray* members = enum_def->members;
        for( size_t i = 0; i < members->count; i++)
        {
            free(members->items[i].name);
        }
        free(members->items);
        free(members);
        
        // Free the name
        free(enum_def->name);
        
        // Free the definition
        free(enum_def);
    }
}

void enum_definition_add_member(EnumDefinition* enum_def, const char* name, int64_t value) {
    if (!enum_def || !name) return;
    
    EnumMember member;    
    member.name = malloc(strlen(name) + 1);
    if (!member.name) {
        return;
    }
    strcpy(member.name, name);
    
    member.value = value;
    enumMember_push(enum_def->members, member);
    
    // Update next auto value
    enum_def->next_auto_value = value + 1;
}

void enum_definition_add_auto_member(EnumDefinition* enum_def, const char* name) {
    if (!enum_def || !name) return;
    
    enum_definition_add_member(enum_def, name, enum_def->next_auto_value);
}

EnumMember* enum_definition_find_member(EnumDefinition* enum_def, const char* name) {
    if (!enum_def || !name) return NULL;

    EnumMemberArray* members = enum_def->members;
    for( size_t i = 0; i < members->count; i++)
    {
        if (strcmp(members->items[i].name, name) == 0) {
            return &(members->items[i]);
        }
    }
    
    return NULL;
}

EnumMember* enum_definition_find_member_by_value(EnumDefinition* enum_def, int64_t value) {
    if (!enum_def) return NULL;
    
    EnumMemberArray* members = enum_def->members;
    for( size_t i = 0; i < members->count; i++)
    {
        if(members->items[i].value == value)
        {
            return &(members->items[i]);
        }
    }

    return NULL;
}

bool enum_values_equal(Value a, Value b) {
    if (a.type != VAL_ENUM || b.type != VAL_ENUM) return false;
    return a.as.enum_val.definition == b.as.enum_val.definition &&
           a.as.enum_val.value == b.as.enum_val.value;
}

const char* enum_value_name(Value enum_val) {
    if (enum_val.type != VAL_ENUM || !enum_val.as.enum_val.definition) return NULL;
    
    EnumMember* member = enum_definition_find_member_by_value(
        enum_val.as.enum_val.definition, 
        enum_val.as.enum_val.value
    );
    
    return member ? member->name : NULL;
}
