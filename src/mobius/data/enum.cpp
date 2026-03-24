#include "data/enum.h"

#include <cstring>

// ============================================================================
// EnumDefinition implementation
// ============================================================================

EnumDefinition::EnumDefinition(const char* name, NumberType underlying_type)
    : name_(name)
    , next_auto_value_(0)
    , underlying_type_(underlying_type)
{
    members_.reserve(5);
}

EnumDefinition* EnumDefinition::retain() {
    RefCounted::retain();
    return this;
}

void EnumDefinition::addMember(const char* name, int64_t value) {
    if (!name) return;

    members_.push_back({name, value});
    next_auto_value_ = value + 1;
}

void EnumDefinition::addAutoMember(const char* name) {
    if (!name) return;

    addMember(name, next_auto_value_);
}

const EnumMember* EnumDefinition::findMember(const char* name) const {
    if (!name) return nullptr;

    for (const auto& member : members_) {
        if (member.name == name) {
            return &member;
        }
    }
    return nullptr;
}

const EnumMember* EnumDefinition::findMemberByValue(int64_t value) const {
    for (const auto& member : members_) {
        if (member.value == value) {
            return &member;
        }
    }
    return nullptr;
}

const char* enum_value_name(const Value& enum_val) {
    if (enum_val.type != VAL_ENUM || !enum_val.as.enum_def) return nullptr;

    const EnumMember* member = enum_val.as.enum_def->findMemberByValue(
        enum_val.aux
    );

    return member ? member->name.c_str() : nullptr;
}
