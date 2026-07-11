#ifndef MOBIUS_TABLE_H
#define MOBIUS_TABLE_H

#include "data/value.h"
#include "internal/ref_counted.h"

#include <cstddef>
#include <vector>
#include <functional>
#include <mutex>

#define INITIAL_TABLE_CAPACITY 8

struct TableEntry {
    Value key;
    Value value;
    bool occupied() const { return key.type != VAL_NIL; }
};

class MobiusState;

class Table : public RefCounted {
public:
    Table(MobiusState* state, size_t initial_capacity = INITIAL_TABLE_CAPACITY);
    ~Table() override;

    Table* retain();

    const Value& get(const Value& key) const;
    const Value& getByString(MobiusString* key) const;
    bool set(const Value& key, const Value& value);
    bool setByString(MobiusString* key, const Value& value);
    bool hasKey(const Value& key) const;
    bool remove(const Value& key);
    size_t size() const { return size_; }

    Table* copy() const;

    void setMetatable(Table* mt);
    Table* getMetatable() const { return metatable_; }

    bool hasMetamethod(MobiusString* method_name) const;
    const Value& getMetamethod(MobiusString* method_name) const;

    // Iterate over all occupied entries. Callback receives (key, value).
    void forEach(const std::function<void(const Value& key, const Value& value)>& fn) const;

    void print() const;
    void printDebug() const;

    MobiusState* getState() const { return state_; }

    void markShared();
    bool isShared() const { return shared_; }
    std::recursive_mutex& mutex() { return mutex_; }

    static constexpr uint8_t TAG_EMPTY = 0x00;

    const std::vector<TableEntry>& entries() const { return entries_; }
    const std::vector<uint8_t>& tags() const { return tags_; }

private:
    static inline uint8_t tagFromHash(size_t h) { return 0x80 | (uint8_t)(h >> 57); }

    const Value& getUnlocked(const Value& key) const;
    const Value& getByStringUnlocked(MobiusString* key) const;
    bool setUnlocked(const Value& key, const Value& value);
    bool setByStringUnlocked(MobiusString* key, const Value& value);
    bool removeUnlocked(const Value& key);

    void resize(size_t new_capacity);
    size_t findIndex(const Value& key, size_t hash) const;
    void insertEntry(const Value& key, const Value& value, size_t hash);

    // One-entry metamethod lookup cache, used when THIS table serves as a
    // metatable: getMetamethod probes the same interned name (__index, __eq,
    // ...) on every field miss / method call, so cache the last probe.
    // Interned names are pointer-unique, so identity compare suffices.
    // Any mutation of this table invalidates (mm_cache_name_ = nullptr).
    mutable MobiusString* mm_cache_name_ = nullptr;
    mutable Value mm_cache_value_;

    std::vector<TableEntry> entries_;
    std::vector<uint8_t> tags_;
    size_t size_;
    Table* metatable_;
    MobiusState* state_;
    bool shared_ = false;
    mutable std::recursive_mutex mutex_;
};

// Hash helpers
size_t hash_value_raw(const Value& value);
inline size_t hash_value(const Value& value, size_t capacity) {
    return hash_value_raw(value) & (capacity - 1);
}

// Metamethod name validation
const char* get_metamethod_name(const char* name);

#endif // MOBIUS_TABLE_H
