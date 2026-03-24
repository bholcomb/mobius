#include "data/table.h"
#include "data/metamethods.h"
#include "internal/string_intern.h"
#include "state/mobius_state.h"

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cctype>

#define LOAD_FACTOR_THRESHOLD 0.75

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

size_t hash_value(const Value& value, size_t capacity) {
    size_t hash = 0;

    switch (value.type) {
        case VAL_NIL:    hash = 0; break;
        case VAL_BOOL:   hash = value.as.boolean ? 1 : 0; break;
        case VAL_INTEGER: hash = hash_integer(value.as.integer.value); break;
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
            hash = (size_t)(uintptr_t)value.as.userdata.ptr;
            if (value.as.userdata.type_name)
                hash ^= hash_string_for_table(value.as.userdata.type_name);
            break;
        case VAL_ENUM:
            hash = (size_t)(uintptr_t)value.as.enum_val.definition;
            hash ^= (size_t)value.as.enum_val.value;
            break;
    }

    return hash & (capacity - 1);
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

size_t Table::findIndex(const Value& key) const {
    size_t index = hash_value(key, entries_.size());
    size_t start = index;

    do {
        if (!entries_[index].is_occupied)
            return index;
        if (entries_[index].key.exactlyEqual(key))
            return index;
        index = (index + 1) & (entries_.size() - 1);
    } while (index != start);

    return start;
}

void Table::insertEntry(const Value& key, const Value& value) {
    size_t index = hash_value(key, entries_.size());
    size_t start = index;

    do {
        if (!entries_[index].is_occupied) {
            entries_[index].key = key;
            entries_[index].value = value;
            entries_[index].is_occupied = true;
            return;
        }
        if (entries_[index].key.exactlyEqual(key)) {
            entries_[index].value = value;
            return;
        }
        index = (index + 1) & (entries_.size() - 1);
    } while (index != start);

    entries_[start].key = key;
    entries_[start].value = value;
    entries_[start].is_occupied = true;
}

void Table::resize(size_t new_capacity) {
    if (new_capacity <= entries_.size()) return;
    new_capacity = next_power_of_2(new_capacity);

    std::vector<TableEntry> old_entries = std::move(entries_);
    entries_.clear();
    entries_.resize(new_capacity);
    size_ = 0;

    for (auto& entry : old_entries) {
        if (entry.is_occupied) {
            insertEntry(entry.key, entry.value);
            size_++;
        }
    }
}

Value Table::get(const Value& key) const {
    if (size_ == 0) return make_nil_value();

    size_t index = findIndex(key);
    if (entries_[index].is_occupied && entries_[index].key.exactlyEqual(key)) {
        return entries_[index].value;
    }

    if (metatable_ && state_) {
        Value index_method = getMetamethod(state_->metamethods()->index());
        if (index_method.type == VAL_TABLE) {
            return index_method.as.table->get(key);
        }
    }

    return make_nil_value();
}

bool Table::set(const Value& key, const Value& value) {
    if ((double)size_ / entries_.size() >= LOAD_FACTOR_THRESHOLD) {
        resize(entries_.size() * 2);
    }

    size_t index = findIndex(key);
    bool is_new = !entries_[index].is_occupied;

    if (is_new) {
        if (metatable_ && state_) {
            Value newindex_method = getMetamethod(state_->metamethods()->newindex());
            if (newindex_method.type == VAL_TABLE) {
                return newindex_method.as.table->set(key, value);
            }
        }

        entries_[index].key = key;
        entries_[index].is_occupied = true;
        size_++;
    }

    entries_[index].value = value;
    return true;
}

bool Table::hasKey(const Value& key) const {
    if (size_ == 0) return false;
    size_t index = findIndex(key);
    return entries_[index].is_occupied && entries_[index].key.exactlyEqual(key);
}

bool Table::remove(const Value& key) {
    if (size_ == 0) return false;

    size_t index = findIndex(key);
    if (!entries_[index].is_occupied || !entries_[index].key.exactlyEqual(key))
        return false;

    entries_[index].is_occupied = false;
    entries_[index].key = make_nil_value();
    entries_[index].value = make_nil_value();
    size_--;

    // Rehash following entries to maintain probe sequence
    for (;;) {
        index = (index + 1) & (entries_.size() - 1);
        if (!entries_[index].is_occupied) break;

        Value k = std::move(entries_[index].key);
        Value v = std::move(entries_[index].value);
        entries_[index].is_occupied = false;
        size_--;
        set(k, v);
    }

    return true;
}

Table* Table::copy() const {
    Table* c = new (std::nothrow) Table(state_, entries_.size());
    if (!c) return nullptr;

    for (const auto& entry : entries_) {
        if (entry.is_occupied) {
            c->set(entry.key, entry.value);
        }
    }
    c->setMetatable(metatable_);
    return c;
}

void Table::forEach(const std::function<void(const Value& key, const Value& value)>& fn) const {
    for (const auto& entry : entries_) {
        if (entry.is_occupied) {
            fn(entry.key, entry.value);
        }
    }
}

bool Table::hasMetamethod(MobiusString* method_name) const {
    if (!metatable_ || !method_name) return false;
    Value method = metatable_->get(make_string_value(method_name));
    return method.type != VAL_NIL;
}

Value Table::getMetamethod(MobiusString* method_name) const {
    if (!metatable_ || !method_name) return make_nil_value();
    return metatable_->get(make_string_value(method_name));
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
        if (entries_[i].is_occupied) {
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
