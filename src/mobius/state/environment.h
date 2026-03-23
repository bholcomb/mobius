#ifndef MOBIUS_ENVIRONMENT_H
#define MOBIUS_ENVIRONMENT_H

#include "data/value.h"
#include "internal/string_intern.h"

#include <unordered_map>
#include <cstdint>

class ExecutionContext;

struct InternedKeyHash {
    size_t operator()(const char* p) const noexcept {
        return reinterpret_cast<uintptr_t>(p);
    }
};
struct InternedKeyEqual {
    bool operator()(const char* a, const char* b) const noexcept {
        return a == b;
    }
};

class MOBIUS_API Environment {
public:
    Environment(Environment* enclosing = nullptr, ExecutionContext* ctx = nullptr);
    ~Environment();

    void define(const char* name, const Value& value);
    Value get(const char* name, bool* found) const;
    const Value* lookup(const char* name) const;
    bool assign(const char* name, const Value& value);
    bool isDefined(const char* name) const;
    size_t size() const;
    void print() const;

    Environment* enclosing;
    ExecutionContext* current_context;

private:
    std::unordered_map<const char*, Value, InternedKeyHash, InternedKeyEqual> myVariables;
};

#endif /* MOBIUS_ENVIRONMENT_H */
