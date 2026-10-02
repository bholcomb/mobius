#include "data/table.h"
#include "data/shared_cell.h"
#include "data/array.h"
#include "data/metamethods.h"
#include "internal/string_intern.h"
#include "state/mobius_state.h"

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cctype>


// ----------------------------------------------------------------------------
// Pool-backed operator new/delete. Exact-size allocations use the per-thread
// GC pools; any other size (a subclass) uses the global heap. The unsized /
// nothrow deletes assume the class size — a larger (glibc-origin) chunk that
// reaches the pool is absorbed safely: chunks never return to the global
// heap, so the size routing can never mismatch an actual glibc free.
// ----------------------------------------------------------------------------
void* Table::operator new(size_t sz) {
    if (sz == sizeof(Table))
        if (void* p = gc_object_alloc(GC_TABLE, sz)) return p;
    return ::operator new(sz);
}
void* Table::operator new(size_t sz, const std::nothrow_t&) noexcept {
    if (sz == sizeof(Table))
        if (void* p = gc_object_alloc(GC_TABLE, sz)) return p;
    return ::operator new(sz, std::nothrow);
}
void Table::operator delete(void* p, size_t sz) noexcept {
    (void)sz;
    if (p) gc_object_free(GC_TABLE, p);
}
void Table::operator delete(void* p) noexcept {
    if (p) gc_object_free(GC_TABLE, p);
}
void Table::operator delete(void* p, const std::nothrow_t&) noexcept {
    if (p) gc_object_free(GC_TABLE, p);
}
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
    // Empty slots hold UNINITIALIZED memory; the tag byte is the single
    // source of truth for occupancy. Every write into an empty slot is a
    // placement-new; destruction walks the tags.
    entries_.resizeNoInit(initial_capacity);
    tags_.resize(initial_capacity, TAG_EMPTY);
    setGcManaged();
    gc_track(&gc_, GC_TABLE, this);
}

Table::~Table() {
    gc_untrack(&gc_);
    // Destroy only occupied entries — empty slots are uninitialized memory
    // (see the constructor). The tag scan touches 1 byte per slot instead of
    // letting the storage destructor walk 32.
    for (size_t i = 0; i < entries_.size(); i++) {
        if (isLive(tags_[i])) {
            entries_[i].key.~Value();
            entries_[i].value.~Value();
        }
    }
    entries_.clearNoDestroy();
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

// The slot holding `key`, or else the slot an insert of it should use: the
// first deleted slot on its probe path, or the empty slot that ends it.
// Callers tell the cases apart with isLive().
size_t Table::findIndex(const Value& key, size_t hash) const {
    size_t mask = entries_.size() - 1;
    size_t index = hash & mask;
    uint8_t tag = tagFromHash(hash);
    size_t start = index;
    size_t first_deleted = SIZE_MAX;

    do {
        uint8_t t = tags_[index];
        if (t == TAG_EMPTY)
            return first_deleted != SIZE_MAX ? first_deleted : index;
        if (t == TAG_DELETED) {
            if (first_deleted == SIZE_MAX) first_deleted = index;
        } else if (t == tag && entries_[index].key.exactlyEqual(key)) {
            return index;
        }
        index = (index + 1) & mask;
    } while (index != start);

    return first_deleted != SIZE_MAX ? first_deleted : start;
}

void Table::resize(size_t new_capacity) {
    if (new_capacity <= entries_.size()) return;
    rehash(next_power_of_2(new_capacity));
}

// Make room for one more entry: grow, or when deleted slots rather than
// live entries fill the table, rebuild it at the same size to drop them.
void Table::growForInsert() {
    if ((size_ + deleted_) * 4 < entries_.size() * 3) return;
    if (size_ * 2 < entries_.size()) rehash(entries_.size());
    else rehash(entries_.size() * 2);
}

void Table::rehash(size_t new_capacity) {

    // Relocating rehash: every occupied entry is memcpy'd into its new slot
    // (Value is trivially relocatable — the same contract SmallVec growth
    // relies on), so no refcounts tick and no destructors run. The rehash
    // probe only hunts for empty slots: keys are unique by construction, so
    // the equality checks the insert path does are dead weight here.
    EntryStorage new_entries;
    TagStorage new_tags;
    new_entries.resizeNoInit(new_capacity);   // slots init'd by relocation
    new_tags.assign(new_capacity, TAG_EMPTY);

    size_t mask = new_capacity - 1;
    for (size_t i = 0; i < entries_.size(); i++) {
        if (!isLive(tags_[i])) continue;
        size_t h = hash_value_raw(entries_[i].key);
        size_t index = h & mask;
        while (new_tags[index] != TAG_EMPTY) index = (index + 1) & mask;
        memcpy((void*)&new_entries[index], (const void*)&entries_[i],
               sizeof(TableEntry));
        new_tags[index] = tags_[i];
    }

    entries_.clearNoDestroy();          // contents live on in new_entries
    entries_ = std::move(new_entries);
    tags_ = std::move(new_tags);
    // size_ unchanged: relocation neither adds nor drops entries. Deleted
    // slots were not carried over.
    deleted_ = 0;
}

const Value& Table::get(const Value& key) const {
    return getUnlocked(key);
}

// Longest table __index chain a lookup follows. A cycle (t's __index is t,
// or a loop through several tables) used to recurse forever; past this
// many links the lookup gives nil and the VM reports the loop
// (vm_index_function_fallback).
static const int MAX_INDEX_CHAIN = 1000;

// Next table in the __index chain of `t`, or nullptr.
static const Table* index_parent(const Table* t, MobiusState* state) {
    Table* mt = t->getMetatable();
    if (!mt) return nullptr;
    const Value& index_method = t->getMetamethod(state->metamethods()->index());
    return index_method.type == VAL_TABLE ? index_method.as.table : nullptr;
}

const Value* Table::findRaw(const Value& key) const {
    if (size_ == 0) return nullptr;
    size_t h = hash_value_raw(key);
    size_t index = findIndex(key, h);
    if (isLive(tags_[index]) && entries_[index].key.exactlyEqual(key)) {
        return &entries_[index].value;
    }
    return nullptr;
}

const Value* Table::findRawString(MobiusString* key) const {
    if (size_ == 0) return nullptr;
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
                return &entries_[index].value;
        }
        index = (index + 1) & mask;
    } while (index != start);
    return nullptr;
}

const Value& Table::getUnlocked(const Value& key) const {
    const Table* t = this;
    for (int hops = 0; t && hops <= MAX_INDEX_CHAIN; hops++) {
        if (const Value* v = t->findRaw(key)) return *v;
        t = index_parent(t, state_);
    }
    return kNilValue;
}

const Value& Table::getByString(MobiusString* key) const {
    return getByStringUnlocked(key);
}

const Value& Table::getByStringUnlocked(MobiusString* key) const {
    if (MOBIUS_UNLIKELY(!key)) return kNilValue;
    const Table* t = this;
    for (int hops = 0; t && hops <= MAX_INDEX_CHAIN; hops++) {
        if (const Value* v = t->findRawString(key)) return *v;
        t = index_parent(t, state_);
    }
    return kNilValue;
}

bool Table::set(const Value& key, const Value& value) {
    return setUnlocked(key, value);
}

bool Table::setUnlocked(const Value& key, const Value& value) {
    if (MOBIUS_UNLIKELY(owner_cell_ != nullptr)) {
        Value shared = share_for_cell(value);   // shared all the way down
        if (shared.type != value.type) return setUnlocked(key, shared);
    }
    mm_cache_name_ = nullptr;   // this table may be someone's metatable
    growForInsert();

    size_t h = hash_value_raw(key);
    size_t index = findIndex(key, h);
    bool is_new = !isLive(tags_[index]);

    if (is_new) {
        if (metatable_) {
            const Value& newindex_method = getMetamethod(state_->metamethods()->newindex());
            if (newindex_method.type == VAL_TABLE) {
                return newindex_method.as.table->set(key, value);
            }
        }

        new (&entries_[index].key) Value(key);     // slot was uninitialized
        new (&entries_[index].value) Value(value);
        if (tags_[index] == TAG_DELETED) deleted_--;
        tags_[index] = tagFromHash(h);
        size_++;
        return true;
    }

    entries_[index].value = value;
    return true;
}

bool Table::setByString(MobiusString* key, const Value& value) {
    return setByStringUnlocked(key, value);
}

bool Table::setByStringUnlocked(MobiusString* key, const Value& value) {
    if (MOBIUS_UNLIKELY(owner_cell_ != nullptr)) {
        Value shared = share_for_cell(value);   // shared all the way down
        if (shared.type != value.type) return setByStringUnlocked(key, shared);
    }
    mm_cache_name_ = nullptr;   // this table may be someone's metatable
    if (!key) return false;

    growForInsert();

    size_t h = (size_t)key->hash;
    size_t mask = entries_.size() - 1;
    size_t index = h & mask;
    uint8_t tag = tagFromHash(h);
    size_t start = index;
    size_t first_deleted = SIZE_MAX;

    do {
        uint8_t t = tags_[index];
        if (t == TAG_DELETED) {
            if (first_deleted == SIZE_MAX) first_deleted = index;
        } else if (t == TAG_EMPTY) {
            if (metatable_) {
                const Value& newindex_method = getMetamethod(state_->metamethods()->newindex());
                if (newindex_method.type == VAL_TABLE) {
                    Value key_val = make_string_value(key);
                    return newindex_method.as.table->set(key_val, value);
                }
            }

            if (first_deleted != SIZE_MAX) {   // reuse the first deleted slot
                index = first_deleted;
                deleted_--;
            }
            TableEntry& e = entries_[index];
            new (&e.key) Value(make_string_value(key));   // retains; slot was uninitialized
            new (&e.value) Value(value);
            tags_[index] = tag;
            size_++;
            return true;
        } else if (t == tag) {
            if (string_key_equals(entries_[index].key, key)) {
                entries_[index].value = value;
                return true;
            }
        }
        index = (index + 1) & mask;
    } while (index != start);

    // No empty slot on the whole probe path: growForInsert keeps the load
    // under 3/4, so a deleted slot was seen.
    index = first_deleted;
    deleted_--;
    TableEntry& e = entries_[index];
    new (&e.key) Value(make_string_value(key));
    new (&e.value) Value(value);
    tags_[index] = tag;
    size_++;
    return true;
}

bool Table::hasKey(const Value& key) const {
    if (size_ == 0) return false;
    size_t h = hash_value_raw(key);
    size_t index = findIndex(key, h);
    return isLive(tags_[index]) && entries_[index].key.exactlyEqual(key);
}

bool Table::remove(const Value& key) {
    return removeUnlocked(key);
}

bool Table::removeUnlocked(const Value& key) {
    mm_cache_name_ = nullptr;   // this table may be someone's metatable
    if (size_ == 0) return false;

    size_t h = hash_value_raw(key);
    size_t index = findIndex(key, h);
    if (!isLive(tags_[index]) || !entries_[index].key.exactlyEqual(key))
        return false;

    entries_[index].key.~Value();     // the slot becomes uninitialized memory
    entries_[index].value.~Value();
    size_--;
    // Mark the slot deleted so the probe chains through it still reach the
    // entries after it; no entry moves. If the next slot is empty, no chain
    // continues past this one, so it can be empty too.
    size_t mask = entries_.size() - 1;
    if (tags_[(index + 1) & mask] == TAG_EMPTY) {
        tags_[index] = TAG_EMPTY;
    } else {
        tags_[index] = TAG_DELETED;
        deleted_++;
    }
    return true;
}

Table* Table::copy() const {
    Table* c = new (std::nothrow) Table(state_, entries_.size());
    if (!c) return nullptr;
    for (size_t i = 0; i < entries_.size(); i++) {
        if (isLive(tags_[i])) {
            c->set(entries_[i].key, entries_[i].value);
        }
    }
    c->setMetatable(metatable_);
    return c;
}

void Table::forEach(const std::function<void(const Value& key, const Value& value)>& fn) const {
    for (size_t i = 0; i < entries_.size(); i++) {
        if (isLive(tags_[i])) {
            fn(entries_[i].key, entries_[i].value);
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

static void print_table_contents(const Table* table) {
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
            print_value(key);
            printf("]");
        }

        printf(": ");
        print_value(val);
    });

    printf("}");
}

// Nested tables and arrays go through print_value, which shares one cycle
// guard (print_container_enter) between both container kinds. The old
// guard tracked only tables, so a cycle through an array never ended.
void Table::print() const {
    if (!print_container_enter(this, true)) return;
    print_table_contents(this);
    print_container_leave();
}

void Table::printDebug() const {
    printf("Table (size: %zu, capacity: %zu, refCount: %d)\n",
           size_, entries_.size(), refCount());

    for (size_t i = 0; i < entries_.size(); i++) {
        printf("[%zu] ", i);
        if (isLive(tags_[i])) {
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
