#ifndef MOBIUS_TABLE_H
#define MOBIUS_TABLE_H

#include "data/value.h"
#include "internal/ref_counted.h"
#include "internal/gc.h"

#include <cstddef>
#include <cstring>
#include <new>
#include "internal/small_vec.h"
#include <functional>

#define INITIAL_TABLE_CAPACITY 8

struct TableEntry {
    Value key;
    Value value;
    // NOTE: there is deliberately no occupied() helper — empty slots hold
    // UNINITIALIZED memory; only the owning Table's tag bytes say whether a
    // slot is live. Iterate with entries()+tags(), never by inspecting keys.
};

class SharedCell;

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

    // The SharedCell this container belongs to, if it is part of a shared
    // value (non-owning; the cell clears it when it lets go). Containers
    // stored into an owned container are shared too (share_for_cell).
    SharedCell* ownerCell() const { return owner_cell_; }
    void setOwnerCell(SharedCell* cell) { owner_cell_ = cell; }

    // Part of a `const` value: deeply immutable. Every mutation path checks
    // this and raises an error (see freeze_for_const).
    bool isFrozen() const { return frozen_; }
    void freeze() { frozen_ = true; }
    // Fast-path writes are allowed only on plain tables: not frozen, and
    // not inside a shared value (whose stores must share containers).
    bool allowsFastWrite() const { return !owner_cell_ && !frozen_; }


    void setMetatable(Table* mt);
    Table* getMetatable() const { return metatable_; }

    bool hasMetamethod(MobiusString* method_name) const;
    const Value& getMetamethod(MobiusString* method_name) const;

    // Iterate over all occupied entries. Callback receives (key, value).
    void forEach(const std::function<void(const Value& key, const Value& value)>& fn) const;

    void print() const;
    void formatTo(std::string& out) const;   // the text print() shows
    void printDebug() const;

    MobiusState* getState() const { return state_; }

    static constexpr uint8_t TAG_EMPTY = 0x00;
    // A removed entry. Probes continue past it (it was part of a probe
    // chain) and inserts may reuse it; like an empty slot its memory is
    // uninitialized. Removing marks the slot instead of re-inserting the
    // rest of the chain, which moved entries behind a running iteration
    // (for-in skipped keys when the body removed the current one).
    static constexpr uint8_t TAG_DELETED = 0x01;
    // Live entries carry a tag with the high bit set (tagFromHash).
    static MOBIUS_FORCEINLINE bool isLive(uint8_t tag) { return (tag & 0x80) != 0; }

    // Keys follow ==: a float with an integer value (1.0, -0.0) and a uint64
    // that fits in int64 are the same key as the equal int64. Returns the
    // key to use; `tmp` holds a converted one.
    static MOBIUS_FORCEINLINE const Value& canonicalKey(const Value& key, Value& tmp) {
        if (MOBIUS_LIKELY(key.type != VAL_FLOAT64 && key.type != VAL_UINT64)) return key;
        return canonicalKeySlow(key, tmp);
    }
    static const Value& canonicalKeySlow(const Value& key, Value& tmp);
    // nil and NaN cannot be keys: storing with one is an error (set returns
    // false), and looking one up finds nothing.
    static MOBIUS_FORCEINLINE bool isValidKey(const Value& key) {
        return key.type != VAL_NIL &&
               !(key.type == VAL_FLOAT64 && key.as.double_val != key.as.double_val);
    }

    // Inline-capacity storage: a table at the default capacity (8 slots)
    // performs no heap allocation beyond the pooled Table object itself.
    using EntryStorage = SmallVec<TableEntry, INITIAL_TABLE_CAPACITY>;
    using TagStorage   = SmallVec<uint8_t, INITIAL_TABLE_CAPACITY>;
    const EntryStorage& entries() const { return entries_; }
    const TagStorage& tags() const { return tags_; }

    GcHeader* gcHeader() { return &gc_; }

    // Pool-backed allocation (fixed-size chunks, per-thread free lists).
    // Size-routed: a hypothetical subclass falls back to the global heap in
    // both directions. Definitions live in table.cpp (core), so plugins bind
    // exported functions rather than inlining pool internals.
    static void* operator new(size_t sz);
    static void* operator new(size_t sz, const std::nothrow_t&) noexcept;
    static void  operator delete(void* p, size_t sz) noexcept;
    static void  operator delete(void* p) noexcept;
    static void  operator delete(void* p, const std::nothrow_t&) noexcept;
    // GC traversal must see the metamethod cache: it holds a counted
    // reference that invalidation does not clear.
    const Value& mmCacheValue() const { return mm_cache_value_; }

    // ------------------------------------------------------------------
    // Inline string-key probe, for the VM's field-access fast paths. The
    // out-of-line getByString/setByString remain the complete semantics
    // (metatable __index/__newindex, load-factor growth); these helpers
    // handle only the existing-slot hit, which is the hot case in loops.
    // A miss (or an empty table) returns nullptr and the caller falls
    // back to the full path. `stringEquals` mirrors table.cpp's
    // string_key_equals: tag match already filtered on 7 hash bits, so a
    // pointer compare nearly always decides.
    // ------------------------------------------------------------------
    MOBIUS_FORCEINLINE const Value* findString(const MobiusString* key) const {
        if (MOBIUS_UNLIKELY(size_ == 0)) return nullptr;
        size_t h = (size_t)key->hash;
        size_t mask = entries_.size() - 1;
        size_t index = h & mask;
        uint8_t tag = tagFromHash(h);
        size_t start = index;
        do {
            uint8_t t = tags_[index];
            if (t == TAG_EMPTY) return nullptr;
            if (t == tag) {
                const Value& stored = entries_[index].key;
                if (stored.type == VAL_STRING &&
                    (stored.as.string == key ||
                     (stored.as.string && stored.as.string->hash == key->hash &&
                      stored.as.string->length == key->length &&
                      memcmp(stored.as.string->data, key->data, key->length) == 0)))
                    return &entries_[index].value;
            }
            index = (index + 1) & mask;
        } while (index != start);
        return nullptr;
    }

    // Mutable variant for the SETFIELD overwrite fast path. Every mutation
    // must invalidate the metamethod cache (this table may be someone's
    // metatable), exactly as setByStringUnlocked does.
    MOBIUS_FORCEINLINE Value* findStringSlot(const MobiusString* key) {
        const Value* v = findString(key);
        if (v) mm_cache_name_ = nullptr;
        return const_cast<Value*>(v);
    }

private:
    SharedCell* owner_cell_ = nullptr;
    bool frozen_ = false;
    static inline uint8_t tagFromHash(size_t h) { return 0x80 | (uint8_t)(h >> 57); }

    const Value& getUnlocked(const Value& key) const;
    const Value& getByStringUnlocked(MobiusString* key) const;
    // This table's own entry for key (no __index), or nullptr.
    const Value* findRaw(const Value& key) const;
    const Value* findRawString(MobiusString* key) const;
    bool setUnlocked(const Value& key, const Value& value);
    bool setByStringUnlocked(MobiusString* key, const Value& value);
    bool removeUnlocked(const Value& key);

    void resize(size_t new_capacity);
    void rehash(size_t new_capacity);
    void growForInsert();
    size_t findIndex(const Value& key, size_t hash) const;

    // One-entry metamethod lookup cache, used when THIS table serves as a
    // metatable: getMetamethod probes the same interned name (__index, __eq,
    // ...) on every field miss / method call, so cache the last probe.
    // Interned names are pointer-unique, so identity compare suffices.
    // Any mutation of this table invalidates (mm_cache_name_ = nullptr).
    mutable MobiusString* mm_cache_name_ = nullptr;
    mutable Value mm_cache_value_;

    GcHeader gc_;   // tracing-GC registry link (see internal/gc.h)

    EntryStorage entries_;
    TagStorage tags_;
    size_t size_;
    size_t deleted_ = 0;   // TAG_DELETED slots; they count toward the load factor
    Table* metatable_;
    MobiusState* state_;
};

// Hash helpers
size_t hash_value_raw(const Value& value);
inline size_t hash_value(const Value& value, size_t capacity) {
    return hash_value_raw(value) & (capacity - 1);
}

// Metamethod name validation
const char* get_metamethod_name(const char* name);

#endif // MOBIUS_TABLE_H
