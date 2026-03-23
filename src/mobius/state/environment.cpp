#include "state/environment.h"
#include "state/mobius_state.h"

#include <cstdio>
#include <cstdlib>

Environment::Environment(Environment* enclosing, ExecutionContext* ctx)
    : enclosing(enclosing)
    , current_context(ctx)
{
}

Environment::~Environment()
{
}

void Environment::define(const char* name, const Value& value) {
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

const Value* Environment::lookup(const char* name) const {
    const Environment* current = this;
    while (current) {
        auto it = current->myVariables.find(name);
        if (it != current->myVariables.end()) {
            return &it->second;
        }
        current = current->enclosing;
    }
    return nullptr;
}

bool Environment::assign(const char* name, const Value& value) {
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
    return lookup(name) != nullptr;
}

size_t Environment::size() const {
    return myVariables.size();
}

void Environment::print() const {
    printf("Environment (variables: %zu):\n", myVariables.size());
    for (const auto& [name, val] : myVariables) {
        char* str = value_to_string(val);
        printf("  %s = %s\n", name ? name : "(null)", str ? str : "(null)");
        free(str);
    }
    if (enclosing) {
        printf("Enclosing:\n");
        enclosing->print();
    }
}
