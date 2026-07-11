#include "data/table.h"
#include "data/array.h"
#include "data/metamethods.h"
#include "internal/string_intern.h"
#include "state/mobius_state.h"

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cctype>

static const Value kNilValue;

// Compare a stored table key against the string being looked up.
//
// Interned strings are pointer-equal when they are content-equal, and that
// single-compare fast path is kept. But it must not be the *only* path: it is
// only sound while every string in the process is interned. Falling back to
// (hash, length, bytes) keeps lookup correct for strings that were never
// interned, which is what lets the pool skip interning computed strings.
//
// The full-hash check makes the byte compare rare: the probe already matched on
// the 7-bit tag, so reaching memcmp requires a 64-bit hash collision.
// Build with -DMOBIUS_TABLE_NO_PTR_FASTPATH to force every lookup down the
// content-comparison path. While all strings are interned the fallback is
// otherwise unreachable, so this is how it gets test coverage.
static MOBIUS_FORCEINLINE bool string_key_equals(const Value& stored, const MobiusString* key) {
    if (stored.type != VAL_STRING) return false;
    const MobiusString* s = stored.as.string;
#ifndef MOBIUS_TABLE_NO_PTR_FASTPATH
    if (MOBIUS_LIKELY(s == key)) return true;
#endif
    if (!s || !key) return false;
    return s->hash == key->hash &&
           s->length == key->length &&
           memcmp(s->data, key->data, s->length) == 0;
}

static size_t next_power_of_2(size_t n) {
    if (n <= 1) return 1;
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    if (sizeof(size_t) > 4) {
        n |= n >> 32;
    }
    n++;
    return n;
}

static size_t hash_string_for_table(const char* str) {
    size_t hash = 14695981039346656037ULL;
    while (*str) {
        hash ^= (unsigned char)*str++;
        hash *= 1099511628211ULL;
    }
    return hash;
}

static size_t hash_integer(int64_t value) {
    uint64_t v = (uint64_t)value;
    v ^= v >> 30;
    v *= 0xbf58476d1ce4e5b9ULL;
    v ^= v >> 27;
    v *= 0x94d049bb133111ebULL;
    v ^= v >> 31;
    return (size_t)v;
}

static size_t hash_float(double value) {
    union { double d; uint64_t i; } u;
    u.d = value;
    return (size_t)(u.i ^ (u.i >> 32));
}

size_t hash_value_raw(const Value& value) {
    size_t hash = 0;

    // Everything funnels through the hash_integer mixer: Table derives both
    // the bucket index (low bits) and the 7-bit tag byte (bits 57..63) from
    // this hash, so raw values — bools, chars, aligned pointers, float bit
    // patterns — cluster the buckets AND make the tag filter useless (e.g.
    // every bool/char key had tag 0x80).
    switch (value.type) {
        case VAL_NIL:    hash = hash_integer(0x9E3779B9); break;
        case VAL_BOOL:   hash = hash_integer(value.as.boolean ? 1 : 2); break;
        case VAL_INT64: hash = hash_integer(value.as.i64); break;
        case VAL_UINT64:  hash = hash_integer((int64_t)value.as.u64); break;
        case VAL_FLOAT64: hash = hash_integer((int64_t)hash_float(value.as.double_val)); break;
        case VAL_STRING:
            hash = value.as.string ? value.as.string->hash : 0;
            break;
        case VAL_CHAR:   hash = hash_integer((int64_t)(unsigned char)value.as.character); break;
        case VAL_ARRAY:  hash = hash_integer((int64_t)(uintptr_t)value.as.array); break;
        case VAL_FUNCTION: hash = hash_integer((int64_t)(uintptr_t)value.as.function); break;
        case VAL_NATIVE_FUNCTION: hash = hash_integer((int64_t)(uintptr_t)value.as.native_function); break;
        case VAL_TABLE:  hash = hash_integer((int64_t)(uintptr_t)value.as.table); break;
        case VAL_USERDATA:
            if (value.as.userdata) {
                hash = (size_t)(uintptr_t)value.as.userdata->ptr;
                if (value.as.userdata->type_tag)
                    hash ^= (size_t)value.as.userdata->type_tag->hash;
                else if (value.as.userdata->type_name)
                    hash ^= hash_string_for_table(value.as.userdata->type_name);
            }
            break;
        case VAL_ENUM:
            hash = (size_t)(uintptr_t)value.as.enum_def;
            hash ^= (size_t)value.aux;
            break;
        case VAL_FUTURE:
            hash = (size_t)(uintptr_t)value.as.future;
            break;
        case VAL_ARRAY_SLICE:
            hash = (size_t)(uintptr_t)value.as.array_slice;
            break;
        case VAL_CHANNEL:
            hash = (size_t)(uintptr_t)value.as.channel;
            break;
        case VAL_SHARED_CELL:
            hash = (size_t)(uintptr_t)value.as.shared_cell;
            break;
        case VAL_BUFFER:
            hash = (size_t)(uintptr_t)value.as.buffer;
            break;
    }

    return hash;
}


// ============================================================================
// Table implementation
// ============================================================================

Table::Table(MobiusState* state, size_t initial_capacity)
    : size_(0)
    , metatable_(nullptr)
    , state_(state)
{
    if (initial_capacity < INITIAL_TABLE_CAPACITY)
        initial_capacity = INITIAL_TABLE_CAPACITY;
    initial_capacity = next_power_of_2(initial_capacity);
    entries_.resize(initial_capacity);
    tags_.resize(initial_capacity, TAG_EMPTY);
}

Table::~Table() {
    if (metatable_) {
        metatable_->RefCounted::release();
    }
}

Table* Table::retain() {
    RefCounted::retain();
    return this;
}

void Table::setMetatable(Table* mt) {
    if (mt == metatable_) return;
    if (mt) mt->RefCounted::retain();
    if (metatable_) metatable_->RefCounted::release();
    metatable_ = mt;
}

size_t Table::findIndex(const Value& key, size_t hash) const {
    size_t mask = entries_.size() - 1;
    size_t index = hash & mask;
    uint8_t tag = tagFromHash(hash);
    size_t start = index;

    do {
        uint8_t t = tags_[index];
        if (t == TAG_EMPTY)
            return index;
        if (t == tag && entries_[index].key.exactlyEqual(key))
            return index;
        index = (index + 1) & mask;
    } while (index != start);

    return start;
}

void Table::insertEntry(const Value& key, const Value& value, size_t hash) {
    size_t mask = entries_.size() - 1;
    size_t index = hash & mask;
    uint8_t tag = tagFromHash(hash);
    size_t start = index;

    do {
        uint8_t t = tags_[index];
        if (t == TAG_EMPTY) {
            entries_[index].key = key;
            entries_[index].value = value;
            tags_[index] = tag;
            return;
        }
        if (t == tag && entries_[index].key.exactlyEqual(key)) {
            entries_[index].value = value;
            return;
        }
        index = (index + 1) & mask;
    } while (index != start);

    entries_[start].key = key;
    entries_[start].value = value;
    tags_[start] = tag;
}

void Table::resize(size_t new_capacity) {
    if (new_capacity <= entries_.size()) return;
    new_capacity = next_power_of_2(new_capacity);

    std::vector<TableEntry> old_entries = std::move(entries_);
    std::vector<uint8_t> old_tags = std::move(tags_);
    entries_.clear();
    entries_.resize(new_capacity);
    tags_.assign(new_capacity, TAG_EMPTY);
    size_ = 0;

    for (size_t i = 0; i < old_entries.size(); i++) {
        if (old_tags[i] != TAG_EMPTY) {
            size_t h = hash_value_raw(old_entries[i].key);
            insertEntry(old_entries[i].key, old_entries[i].value, h);
            size_++;
        }
    }
}

const Value& Table::get(const Value& key) const {
    if (MOBIUS_UNLIKELY(shared_)) {
        std::lock_guard lock(mutex_);
        return getUnlocked(key);
    }
    return getUnlocked(key);
}

const Value& Table::getUnlocked(const Value& key) const {
    if (size_ == 0) {
        if (metatable_) {
            const Value& index_method = getMetamethod(state_->metamethods()->index());
            if (index_method.type == VAL_TABLE)
                return index_method.as.table->get(key);
        }
        return kNilValue;
    }

    size_t h = hash_value_raw(key);
    size_t index = findIndex(key, h);
    if (tags_[index] != TAG_EMPTY && entries_[index].key.exactlyEqual(key)) {
        return entries_[index].value;
    }

    if (metatable_) {
        const Value& index_method = getMetamethod(state_->metamethods()->index());
        if (index_method.type == VAL_TABLE) {
            return index_method.as.table->get(key);
        }
    }

    return kNilValue;
}

const Value& Table::getByString(MobiusString* key) const {
    if (MOBIUS_UNLIKELY(shared_)) {
        std::lock_guard lock(mutex_);
        return getByStringUnlocked(key);
    }
    return getByStringUnlocked(key);
}

const Value& Table::getByStringUnlocked(MobiusString* key) const {
    if (MOBIUS_UNLIKELY(!key)) return kNilValue;
    if (MOBIUS_UNLIKELY(size_ == 0)) {
        if (metatable_) {
            const Value& index_method = getMetamethod(state_->metamethods()->index());
            if (index_method.type == VAL_TABLE) {
                Value key_val = make_string_value(key);
                return index_method.as.table->get(key_val);
            }
        }
        return kNilValue;
    }

    size_t h = (size_t)key->hash;
    size_t mask = entries_.size() - 1;
    size_t index = h & mask;
    uint8_t tag = tagFromHash(h);
    size_t start = index;

    do {
        uint8_t t = tags_[index];
        if (t == TAG_EMPTY) break;
        if (t == tag) {
            if (string_key_equals(entries_[index].key, key))
                return entries_[index].value;
        }
        index = (index + 1) & mask;
    } while (index != start);

    if (metatable_) {
        const Value& index_method = getMetamethod(state_->metamethods()->index());
        if (index_method.type == VAL_TABLE) {
            Value key_val = make_string_value(key);
            return index_method.as.table->get(key_val);
        }
    }

    return kNilValue;
}

bool Table::set(const Value& key, const Value& value) {
    if (MOBIUS_UNLIKELY(shared_)) {
        std::lock_guard lock(mutex_);
        return setUnlocked(key, value);
    }
    return setUnlocked(key, value);
}

bool Table::setUnlocked(const Value& key, const Value& value) {
    mm_cache_name_ = nullptr;   // this table may be someone's metatable
    if (size_ * 4 >= entries_.size() * 3) {
        resize(entries_.size() * 2);
    }

    size_t h = hash_value_raw(key);
    size_t index = findIndex(key, h);
    bool is_new = (tags_[index] == TAG_EMPTY);

    if (is_new) {
        if (metatable_) {
            const Value& newindex_method = getMetamethod(state_->metamethods()->newindex());
            if (newindex_method.type == VAL_TABLE) {
                return newindex_method.as.table->set(key, value);
            }
        }

        entries_[index].key = key;
        tags_[index] = tagFromHash(h);
        size_++;
    }

    entries_[index].value = value;
    return true;
}

bool Table::setByString(MobiusString* key, const Value& value) {
    if (MOBIUS_UNLIKELY(shared_)) {
        std::lock_guard lock(mutex_);
        return setByStringUnlocked(key, value);
    }
    return setByStringUnlocked(key, value);
}

bool Table::setByStringUnlocked(MobiusString* key, const Value& value) {
    mm_cache_name_ = nullptr;   // this table may be someone's metatable
    if (!key) return false;

    if (size_ * 4 >= entries_.size() * 3) {
        resize(entries_.size() * 2);
    }

    size_t h = (size_t)key->hash;
    size_t mask = entries_.size() - 1;
    size_t index = h & mask;
    uint8_t tag = tagFromHash(h);
    size_t start = index;

    do {
        uint8_t t = tags_[index];
        if (t == TAG_EMPTY) {
            if (metatable_) {
                const Value& newindex_method = getMetamethod(state_->metamethods()->newindex());
                if (newindex_method.type == VAL_TABLE) {
                    Value key_val = make_string_value(key);
                    return newindex_method.as.table->set(key_val, value);
                }
            }

            TableEntry& e = entries_[index];
            e.key = make_string_value(key);   // retains: the table owns its key
            e.value = value;
            tags_[index] = tag;
            size_++;
            return true;
        }
        if (t == tag) {
            if (string_key_equals(entries_[index].key, key)) {
                entries_[index].value = value;
                return true;
            }
        }
        index = (index + 1) & mask;
    } while (index != start);

    TableEntry& e = entries_[start];
    e.key = make_string_value(key);   // retains: the table owns its key
    e.value = value;
    tags_[start] = tag;
    size_++;
    return true;
}

bool Table::hasKey(const Value& key) const {
    if (MOBIUS_UNLIKELY(shared_)) {
        std::lock_guard lock(mutex_);
        if (size_ == 0) return false;
        size_t h = hash_value_raw(key);
        size_t index = findIndex(key, h);
        return tags_[index] != TAG_EMPTY && entries_[index].key.exactlyEqual(key);
    }
    if (size_ == 0) return false;
    size_t h = hash_value_raw(key);
    size_t index = findIndex(key, h);
    return tags_[index] != TAG_EMPTY && entries_[index].key.exactlyEqual(key);
}

bool Table::remove(const Value& key) {
    if (MOBIUS_UNLIKELY(shared_)) {
        std::lock_guard lock(mutex_);
        return removeUnlocked(key);
    }
    return removeUnlocked(key);
}

bool Table::removeUnlocked(const Value& key) {
    mm_cache_name_ = nullptr;   // this table may be someone's metatable
    if (size_ == 0) return false;

    size_t h = hash_value_raw(key);
    size_t index = findIndex(key, h);
    if (tags_[index] == TAG_EMPTY || !entries_[index].key.exactlyEqual(key))
        return false;

    tags_[index] = TAG_EMPTY;
    entries_[index].key = make_nil_value();
    entries_[index].value = make_nil_value();
    size_--;

    size_t mask = entries_.size() - 1;
    for (;;) {
        index = (index + 1) & mask;
        if (tags_[index] == TAG_EMPTY) break;

        Value k = std::move(entries_[index].key);
        Value v = std::move(entries_[index].value);
        tags_[index] = TAG_EMPTY;
        size_--;
        // Raw re-insert: this is probe-cluster repair, not a user-level set.
        // Going through setUnlocked() would treat each displaced neighbor as a
        // brand-new insert and divert it into a `__newindex` metamethod —
        // silently teleporting unrelated entries out of the table on remove().
        // No resize can be needed here (occupancy only decreased).
        insertEntry(k, v, hash_value_raw(k));
        size_++;
    }

    return true;
}

Table* Table::copy() const {
    if (MOBIUS_UNLIKELY(shared_)) {
        std::lock_guard lock(mutex_);
        Table* c = new (std::nothrow) Table(state_, entries_.size());
        if (!c) return nullptr;

        for (size_t i = 0; i < entries_.size(); i++) {
            if (tags_[i] != TAG_EMPTY) {
                c->set(entries_[i].key, entries_[i].value);
            }
        }
        c->setMetatable(metatable_);
        return c;
    }

    Table* c = new (std::nothrow) Table(state_, entries_.size());
    if (!c) return nullptr;

    for (size_t i = 0; i < entries_.size(); i++) {
        if (tags_[i] != TAG_EMPTY) {
            c->set(entries_[i].key, entries_[i].value);
        }
    }
    c->setMetatable(metatable_);
    return c;
}

void Table::forEach(const std::function<void(const Value& key, const Value& value)>& fn) const {
    if (MOBIUS_UNLIKELY(shared_)) {
        std::lock_guard lock(mutex_);
        for (size_t i = 0; i < entries_.size(); i++) {
            if (tags_[i] != TAG_EMPTY) {
                fn(entries_[i].key, entries_[i].value);
            }
        }
        return;
    }
    for (size_t i = 0; i < entries_.size(); i++) {
        if (tags_[i] != TAG_EMPTY) {
            fn(entries_[i].key, entries_[i].value);
        }
    }
}

void Table::markShared() {
    if (shared_) return;
    shared_ = true;
    for (size_t i = 0; i < entries_.size(); i++) {
        if (tags_[i] != TAG_EMPTY) {
            Value& val = entries_[i].value;
            if (val.type == VAL_ARRAY && val.as.array) {
                val.as.array->markShared();
                val.flags |= VAL_FLAG_SHARED;
            } else if (val.type == VAL_TABLE && val.as.table) {
                val.as.table->markShared();
                val.flags |= VAL_FLAG_SHARED;
            }
        }
    }
}

bool Table::hasMetamethod(MobiusString* method_name) const {
    if (!metatable_ || !method_name) return false;
    Value method = metatable_->getByString(method_name);
    return method.type != VAL_NIL;
}

const Value& Table::getMetamethod(MobiusString* method_name) const {
    if (!metatable_ || !method_name) return kNilValue;
    // Serve repeated probes of the same metamethod name from the metatable's
    // one-entry cache: every field miss and method call on an object re-looks
    // up __index, which cost a hash probe per access.
    Table* mt = metatable_;
    if (MOBIUS_LIKELY(mt->mm_cache_name_ == method_name))
        return mt->mm_cache_value_;
    const Value& v = mt->getByString(method_name);
    mt->mm_cache_value_ = v;
    mt->mm_cache_name_ = method_name;
    return mt->mm_cache_value_;
}

// ============================================================================
// Print
// ============================================================================

static void print_table_safe_impl(const Table* table, const Table** visited, int* visited_count, int max_depth) {
    if (!table) {
        printf("(null table)");
        return;
    }

    for (int i = 0; i < *visited_count; i++) {
        if (visited[i] == table) {
            printf("{...circular...}");
            return;
        }
    }

    if (*visited_count >= max_depth) {
        printf("{...depth limit...}");
        return;
    }

    visited[*visited_count] = table;
    (*visited_count)++;

    printf("{");
    bool first = true;

    table->forEach([&](const Value& key, const Value& val) {
        if (!first) printf(", ");
        first = false;

        if (key.type == VAL_STRING) {
            const char* key_str = key.as.string ? key.as.string->data : nullptr;
            bool is_ident = key_str && key_str[0] && (isalpha(key_str[0]) || key_str[0] == '_');
            if (is_ident) {
                for (const char* c = key_str + 1; *c; c++) {
                    if (!isalnum(*c) && *c != '_') { is_ident = false; break; }
                }
            }
            if (is_ident) printf("%s", key_str);
            else printf("[%s]", key_str);
        } else {
            printf("[");
            if (key.type == VAL_TABLE)
                print_table_safe_impl(key.as.table, visited, visited_count, max_depth);
            else
                print_value(key);
            printf("]");
        }

        printf(": ");
        if (val.type == VAL_TABLE)
            print_table_safe_impl(val.as.table, visited, visited_count, max_depth);
        else
            print_value(val);
    });

    printf("}");
    (*visited_count)--;
}

void Table::print() const {
    const Table* visited[100];
    int visited_count = 0;
    print_table_safe_impl(this, visited, &visited_count, 100);
}

void Table::printDebug() const {
    printf("Table (size: %zu, capacity: %zu, refCount: %d)\n",
           size_, entries_.size(), refCount());

    for (size_t i = 0; i < entries_.size(); i++) {
        printf("[%zu] ", i);
        if (tags_[i] != TAG_EMPTY) {
            printf("Key: ");
            print_value(entries_[i].key);
            printf(" => Value: ");
            print_value(entries_[i].value);
            printf("\n");
        } else {
            printf("(empty)\n");
        }
    }
}

// ============================================================================
// Metamethod helpers
// ============================================================================

const char* get_metamethod_name(const char* name) {
    if (name && strncmp(name, "__", 2) == 0)
        return name;
    return nullptr;
}
