#ifndef MOBIUS_ENUM_VALUE_H
#define MOBIUS_ENUM_VALUE_H

#include "data/value.h"
#include "data/number.h"
#include "internal/ref_counted.h"

#include <string>
#include <vector>
#include <cstdint>

struct EnumMember {
    std::string name;
    int64_t value;
};

class EnumDefinition : public RefCounted {
public:
    EnumDefinition(const char* name, NumberType underlying_type);
    ~EnumDefinition() override = default;

    EnumDefinition* retain();

    void addMember(const char* name, int64_t value);
    void addAutoMember(const char* name);

    const EnumMember* findMember(const char* name) const;
    const EnumMember* findMemberByValue(int64_t value) const;

    const std::string& name() const { return name_; }
    NumberType underlyingType() const { return underlying_type_; }

private:
    std::string name_;
    std::vector<EnumMember> members_;
    int64_t next_auto_value_;
    NumberType underlying_type_;
};

// Enum value utility
const char* enum_value_name(const Value& enum_val);

#endif // MOBIUS_ENUM_VALUE_H
