#ifndef MOBIUS_ENVIRONMENT_H
#define MOBIUS_ENVIRONMENT_H

#include "data/value.h"
#include "internal/string_intern.h"

#include <unordered_map>
#include <cstdint>

class ExecutionContext;

struct MobiusStringHash {
    size_t operator()(MobiusString* s) const noexcept {
        return s ? s->hash : 0;
    }
};
struct MobiusStringEqual {
    bool operator()(MobiusString* a, MobiusString* b) const noexcept {
        return a == b;
    }
};

class MOBIUS_API Environment {
public:
    Environment(Environment* enclosing = nullptr, ExecutionContext* ctx = nullptr);
    ~Environment();

    void define(MobiusString* name, const Value& value);
    Value get(MobiusString* name, bool* found) const;
    const Value* lookup(MobiusString* name) const;
    bool assign(MobiusString* name, const Value& value);
    bool isDefined(MobiusString* name) const;
    size_t size() const;
    void print() const;

    void retain();
    void release();

    Environment* enclosing;
    ExecutionContext* current_context;

private:
    std::unordered_map<MobiusString*, Value, MobiusStringHash, MobiusStringEqual> myVariables;
    int ref_count_ = 1;
};

#endif /* MOBIUS_ENVIRONMENT_H */
