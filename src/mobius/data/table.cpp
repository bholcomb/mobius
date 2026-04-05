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

    switch (value.type) {
        case VAL_NIL:    hash = 0; break;
        case VAL_BOOL:   hash = value.as.boolean ? 1 : 0; break;
        case VAL_INT64: hash = hash_integer(value.as.i64); break;
        case VAL_UINT64:  hash = hash_integer((int64_t)value.as.u64); break;
        case VAL_FLOAT64: hash = hash_float(value.as.double_val); break;
        case VAL_STRING:
            hash = value.as.string ? value.as.string->hash : 0;
            break;
        case VAL_CHAR:   hash = (size_t)value.as.character; break;
        case VAL_ARRAY:  hash = (size_t)(uintptr_t)value.as.array; break;
        case VAL_FUNCTION: hash = (size_t)(uintptr_t)value.as.function; break;
        case VAL_NATIVE_FUNCTION: hash = (size_t)(uintptr_t)value.as.native_function; break;
        case VAL_TABLE:  hash = (size_t)(uintptr_t)value.as.table; break;
        case VAL_USERDATA:
            if (value.as.userdata) {
                hash = (size_t)(uintptr_t)value.as.userdata->ptr;
                if (value.as.userdata->type_name)
                    hash ^= hash_string_for_table(value.as.userdata->type_name);
            }
            break;
        case VAL_ENUM:
            hash = (size_t)(uintptr_t)value.as.enum_def;
            hash ^= (size_t)value.aux;
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
        std::shared_lock lock(mutex_);
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
        std::shared_lock lock(mutex_);
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
            const Value& k = entries_[index].key;
            if (k.type == VAL_STRING && k.as.string == key)
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
        std::unique_lock lock(mutex_);
        return setUnlocked(key, value);
    }
    return setUnlocked(key, value);
}

bool Table::setUnlocked(const Value& key, const Value& value) {
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
        std::unique_lock lock(mutex_);
        return setByStringUnlocked(key, value);
    }
    return setByStringUnlocked(key, value);
}

bool Table::setByStringUnlocked(MobiusString* key, const Value& value) {
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
            e.key.type = VAL_STRING;
            e.key.as.string = key;
            e.key.flags = 0;
            key->retain();
            e.value = value;
            tags_[index] = tag;
            size_++;
            return true;
        }
        if (t == tag) {
            const Value& k = entries_[index].key;
            if (k.type == VAL_STRING && k.as.string == key) {
                entries_[index].value = value;
                return true;
            }
        }
        index = (index + 1) & mask;
    } while (index != start);

    TableEntry& e = entries_[start];
    e.key.type = VAL_STRING;
    e.key.as.string = key;
    e.key.flags = 0;
    key->retain();
    e.value = value;
    tags_[start] = tag;
    size_++;
    return true;
}

bool Table::hasKey(const Value& key) const {
    if (MOBIUS_UNLIKELY(shared_)) {
        std::shared_lock lock(mutex_);
    }
    if (size_ == 0) return false;
    size_t h = hash_value_raw(key);
    size_t index = findIndex(key, h);
    return tags_[index] != TAG_EMPTY && entries_[index].key.exactlyEqual(key);
}

bool Table::remove(const Value& key) {
    if (MOBIUS_UNLIKELY(shared_)) {
        std::unique_lock lock(mutex_);
        return removeUnlocked(key);
    }
    return removeUnlocked(key);
}

bool Table::removeUnlocked(const Value& key) {
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
        setUnlocked(k, v);
    }

    return true;
}

Table* Table::copy() const {
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
        std::shared_lock lock(mutex_);
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
    return metatable_->getByString(method_name);
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
