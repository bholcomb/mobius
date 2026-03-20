#ifndef MOBIUS_ENVIRONMENT_H
#define MOBIUS_ENVIRONMENT_H

#include "data/value.h"

#include <string>
#include <unordered_map>

class ExecutionContext;

class Environment {
public:
    Environment(Environment* enclosing = nullptr, ExecutionContext* ctx = nullptr);
    ~Environment();

    void define(const char* name, Value value);
    Value get(const char* name, bool* found) const;
    bool assign(const char* name, Value value);
    bool isDefined(const char* name) const;
    size_t size() const;
    void print() const;

    Environment* enclosing;
    ExecutionContext* current_context;

private:
    std::unordered_map<std::string, Value> myVariables;
};

#endif /* MOBIUS_ENVIRONMENT_H */
