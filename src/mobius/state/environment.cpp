#include "state/environment.h"
#include "state/mobius_state.h"

#include <cstdio>

Environment::Environment(Environment* enclosing, ExecutionContext* ctx)
    : enclosing(enclosing)
    , current_context(ctx)
{
}

Environment::~Environment()
{
}

void Environment::define(const char* name, Value value) {
    auto it = myVariables.find(name);
    if (it != myVariables.end()) {
        it->second = value;
    } else {
        myVariables.emplace(name, value);
    }
}

Value Environment::get(const char* name, bool* found) const {
    const Environment* current = this;
    while (current) {
        auto it = current->myVariables.find(name);
        if (it != current->myVariables.end()) {
            *found = true;
            return it->second;
        }
        current = current->enclosing;
    }
    *found = false;
    return make_nil_value();
}

bool Environment::assign(const char* name, Value value) {
    Environment* current = this;
    while (current) {
        auto it = current->myVariables.find(name);
        if (it != current->myVariables.end()) {
            it->second = value;
            return true;
        }
        current = current->enclosing;
    }
    return false;
}

bool Environment::isDefined(const char* name) const {
    bool found = false;
    Value v = get(name, &found);
    return found;
}

size_t Environment::size() const {
    return myVariables.size();
}

void Environment::print() const {
    printf("Environment (variables: %zu):\n", myVariables.size());
    for (const auto& [name, val] : myVariables) {
        char* str = value_to_string(val);
        printf("  %s = %s\n", name.c_str(), str ? str : "(null)");
        free(str);
    }
    if (enclosing) {
        printf("Enclosing:\n");
        enclosing->print();
    }
}
