#ifndef MOBIUS_TABLE_H
#define MOBIUS_TABLE_H

#include "data/value.h"
#include "internal/ref_counted.h"

#include <cstddef>
#include <vector>
#include <functional>

#define INITIAL_TABLE_CAPACITY 8

struct TableEntry {
    Value key;
    Value value;
    bool is_occupied = false;
};

class MobiusState;

class Table : public RefCounted {
public:
    Table(MobiusState* state, size_t initial_capacity = INITIAL_TABLE_CAPACITY);
    ~Table() override;

    Table* retain();

    Value get(const Value& key) const;
    bool set(const Value& key, const Value& value);
    bool hasKey(const Value& key) const;
    bool remove(const Value& key);
    size_t size() const { return size_; }

    Table* copy() const;

    void setMetatable(Table* mt);
    Table* getMetatable() const { return metatable_; }

    bool hasMetamethod(MobiusString* method_name) const;
    Value getMetamethod(MobiusString* method_name) const;

    // Iterate over all occupied entries. Callback receives (key, value).
    void forEach(const std::function<void(const Value& key, const Value& value)>& fn) const;

    void print() const;
    void printDebug() const;

    MobiusState* getState() const { return state_; }

private:
    void resize(size_t new_capacity);
    size_t findIndex(const Value& key) const;
    void insertEntry(const Value& key, const Value& value);

    std::vector<TableEntry> entries_;
    size_t size_;
    Table* metatable_;
    MobiusState* state_;
};

// Hash helper
size_t hash_value(const Value& value, size_t capacity);

// Metamethod name validation
const char* get_metamethod_name(const char* name);

#endif // MOBIUS_TABLE_H
