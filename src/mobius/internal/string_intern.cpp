#include "internal/string_intern.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <chrono>
#include <mutex>
#include <new>
#include <random>

// ============================================================================
// HASH SEED
// ============================================================================

// The string hash is seeded with a value chosen once per process.
//
// Without a secret seed, an attacker who controls table keys — query
// parameters, JSON object keys, header names — can precompute keys that all
// hash to one bucket and turn table insertion into O(n^2). Randomizing the seed
// makes those keys unpredictable. This is the same mitigation Lua 5.4 and
// CPython apply.
//
// The cost is that table iteration order (`pairs()`) now varies between runs.
// Set MOBIUS_HASH_SEED to a decimal or 0x-prefixed value to pin the seed and
// recover a reproducible order when debugging or diffing output.
static uint64_t g_string_hash_seed = 0x9E3779B97F4A7C15ull;
static std::once_flag g_string_hash_seed_once;

static uint64_t generate_string_hash_seed() {
    if (const char* env = getenv("MOBIUS_HASH_SEED")) {
        char* end = nullptr;
        unsigned long long pinned = strtoull(env, &end, 0);
        if (end != env) return (uint64_t)pinned;
    }

    uint64_t seed = 0;
    std::random_device rd;
    seed  = ((uint64_t)rd() << 32) ^ (uint64_t)rd();
    seed ^= (uint64_t)std::chrono::steady_clock::now().time_since_epoch().count();
    seed ^= (uint64_t)(uintptr_t)&g_string_hash_seed;   // ASLR

    // A zero seed would degrade the finalizer for the empty string.
    return seed ? seed : 0x9E3779B97F4A7C15ull;
}

// Called from the StringInternPool constructor, which necessarily runs before
// any string can be interned. Keeping it out of compute_string_hash() avoids a
// guard check on every hash.
void mobius_init_string_hash_seed() {
    std::call_once(g_string_hash_seed_once,
                   [] { g_string_hash_seed = generate_string_hash_seed(); });
}

static constexpr uint64_t MIX_A = 0xff51afd7ed558ccdull;
static constexpr uint64_t MIX_B = 0xc4ceb9fe1a85ec53ull;
static constexpr uint64_t FNV_PRIME_64 = 0x100000001b3ull;

// Word-at-a-time hash over every byte, with a murmur3-style finalizer.
//
// This reads all of the input. The previous hash sampled only ~32 bytes of any
// string of length >= 40 (`step = len/32 + 1`, walking backwards) and seeded
// from `len`, so same-length strings that differed only at unsampled positions
// all produced the SAME value: 20k distinct 128-byte strings collapsed to one
// hash, turning every intern and every table probe on them into a linear scan
// with a full memcmp. Correctness held (find() compares bytes) but throughput
// collapsed, and the collisions are trivially constructible — a hash-flooding
// vector for any table keyed by attacker-supplied strings.
//
// Hashing all bytes byte-at-a-time (FNV-1a) fixes the collisions but runs at
// only ~1.2 GB/s, ~50x the cost of the memcpy that interning already performs.
// Consuming 8 bytes per multiply reaches ~9.9 GB/s, so the hash costs a small
// multiple of the copy instead of dominating it.
static uint64_t compute_string_hash(const char* str, size_t len) {
    uint64_t hash = g_string_hash_seed ^ (uint64_t)len;

    size_t i = 0;
    for (; i + 8 <= len; i += 8) {
        uint64_t chunk;
        memcpy(&chunk, str + i, 8);   // compiles to one unaligned load
        chunk *= MIX_A;
        chunk ^= chunk >> 33;
        hash ^= chunk;
        hash *= MIX_B;
    }
    for (; i < len; i++) {
        hash ^= (unsigned char)str[i];
        hash *= FNV_PRIME_64;
    }

    hash ^= hash >> 33;
    hash *= MIX_A;
    hash ^= hash >> 33;

    return hash;
}

// ============================================================================
// MOBIUS STRING
// ============================================================================

bool MobiusString::operator==(const MobiusString& other) const {
    if (this == &other) return true;
    if (hash != other.hash) return false;
    if (length != other.length) return false;

    // hash + length already matched above; reaching here requires a 64-bit
    // hash collision, so go straight to the byte compare. (The char-sampling
    // heuristic that used to live here paid off only under the old partial
    // hash.)
    return memcmp(data, other.data, length) == 0;
}

// ============================================================================
// SHARD METHODS
// ============================================================================

MobiusString* StringInternPool::Shard::find(const char* data, size_t len, uint64_t hash) const {
    if (buckets.empty()) return nullptr;
    size_t bucket = hash & (buckets.size() - 1);
    MobiusString* str = buckets[bucket];

    while (str) {
        if (str->hash == hash && str->length == len &&
            memcmp(str->data, data, len) == 0) {
            return str;
        }
        str = str->next;
    }

    return nullptr;
}

// ---------------------------------------------------------------------------
// Small-string block pools. Computed (heap) strings churn hard — every
// concat/str() in a loop is a malloc+free — so blocks up to
// STRING_POOL_MAX_CAPACITY chars come from size-class free lists carved out
// of 64KB slabs. Same-thread free/alloc touches only a thread-local list (no
// locks, no atomics — the common case: strings usually die where they were
// made). A string freed on a foreign thread is pushed onto the class's
// global lock-free overflow stack, which allocating threads drain when their
// local list is empty. Slabs are never returned to the OS.
//
// The size class is stashed in the header's `next` field (unused by heap
// strings; interned strings never reach these pools — IMMORTAL_RC means
// destroy() is never called on them).
// ---------------------------------------------------------------------------

namespace {

// Block sizes 64/128/256 cover header (32B) + capacity+1 chars.
constexpr size_t STRING_CLASS_SIZES[3] = {64, 128, 256};
constexpr int    STRING_CLASS_COUNT = 3;
constexpr size_t STRING_POOL_MAX_BLOCK = 256;
constexpr size_t STRING_SLAB_SIZE = 64 * 1024;

struct StringPoolClass {
    void* free_head = nullptr;
    char* cursor = nullptr;
    char* end = nullptr;
};
thread_local StringPoolClass tl_string_pools[STRING_CLASS_COUNT];

// Foreign frees land here; allocators drain opportunistically.
std::atomic<void*> g_string_overflow[STRING_CLASS_COUNT] = {};
thread_local bool tl_string_pool_owner[STRING_CLASS_COUNT] = {};

inline int string_class_for_block(size_t block) {
    if (block <= 64) return 0;
    if (block <= 128) return 1;
    if (block <= 256) return 2;
    return -1;
}

void* string_pool_alloc(int cls) {
    StringPoolClass& p = tl_string_pools[cls];
    if (void* c = p.free_head) {
        p.free_head = *(void**)c;
        return c;
    }
    // Local list empty: adopt the whole global overflow stack, if any.
    if (g_string_overflow[cls].load(std::memory_order_relaxed)) {
        void* chain = g_string_overflow[cls].exchange(nullptr, std::memory_order_acquire);
        if (chain) {
            p.free_head = *(void**)chain;
            return chain;
        }
    }
    size_t bs = STRING_CLASS_SIZES[cls];
    if ((size_t)(p.end - p.cursor) < bs) {
        char* slab = (char*)malloc(STRING_SLAB_SIZE);
        if (!slab) return nullptr;
        p.cursor = slab;
        p.end = slab + STRING_SLAB_SIZE;
        tl_string_pool_owner[cls] = true;
    }
    void* c = p.cursor;
    p.cursor += bs;
    return c;
}

void string_pool_free(int cls, void* block) {
    // Same-thread if this thread has ever allocated this class (the usual
    // case); otherwise push onto the class's global overflow stack.
    if (tl_string_pool_owner[cls]) {
        StringPoolClass& p = tl_string_pools[cls];
        *(void**)block = p.free_head;
        p.free_head = block;
        return;
    }
    void* head = g_string_overflow[cls].load(std::memory_order_relaxed);
    do {
        *(void**)block = head;
    } while (!g_string_overflow[cls].compare_exchange_weak(
        head, block, std::memory_order_release, std::memory_order_relaxed));
}

} // namespace

// Allocate one block holding the header followed by `capacity + 1` characters.
// Returns a string whose `data` points at its own inline storage. `rc` selects
// immortal (interned) or a live refcount (heap). Heap strings small enough
// for a size class remember it in `next` (as class+1) for destroy().
static MobiusString* alloc_string_block(size_t capacity, uint32_t rc) {
    if (capacity > MobiusString::MAX_LENGTH) return nullptr;
    size_t block = sizeof(MobiusString) + capacity + 1;
    void* mem = nullptr;
    int cls = -1;
    if (rc != MobiusString::IMMORTAL_RC && block <= STRING_POOL_MAX_BLOCK) {
        cls = string_class_for_block(block);
        mem = string_pool_alloc(cls);
    }
    if (!mem) {
        cls = -1;
        mem = ::operator new(block, std::nothrow);
        if (!mem) return nullptr;
    }
    MobiusString* str = static_cast<MobiusString*>(mem);
    str->data = reinterpret_cast<const char*>(str) + sizeof(MobiusString);
    str->length = 0;
    new (&str->refcount) std::atomic<uint32_t>(rc);
    str->hash = 0;
    str->next = reinterpret_cast<MobiusString*>((uintptr_t)(cls + 1));
    return str;
}

void MobiusString::destroy() {
    // Header and characters are one block. Heap strings are never linked into
    // a pool bucket, so there is nothing to unlink. `next` carries the pool
    // size class + 1 (0 = plain heap block).
    uintptr_t cls_plus_1 = (uintptr_t)next;
    if (cls_plus_1) {
        string_pool_free((int)cls_plus_1 - 1, this);
        return;
    }
    ::operator delete(this);
}

MobiusString* StringInternPool::allocHeap(size_t capacity) {
    return alloc_string_block(capacity, 1);
}

MobiusString* StringInternPool::finishHeap(MobiusString* s, size_t len) {
    if (!s) return nullptr;
    char* buf = s->mutableData();
    buf[len] = '\0';
    s->length = (uint32_t)len;
    s->hash = compute_string_hash(buf, len);
    return s;
}

MobiusString* StringInternPool::createHeap(const char* data, size_t length) {
    MobiusString* s = alloc_string_block(length, 1);
    if (!s) return nullptr;
    memcpy(s->mutableData(), data, length);
    return finishHeap(s, length);
}

MobiusString* StringInternPool::Shard::insert(const char* data, size_t len, uint64_t hash) {
    float current_load = (float)(string_count + 1) / (float)buckets.size();
    if (current_load > load_factor) {
        resize();
    }

    MobiusString* str = alloc_string_block(len, MobiusString::IMMORTAL_RC);
    if (!str) return nullptr;

    char* data_copy = str->mutableData();
    memcpy(data_copy, data, len);
    data_copy[len] = '\0';

    str->length = (uint32_t)len;
    str->hash = hash;
    str->next = nullptr;

    size_t bucket = hash & (buckets.size() - 1);
    str->next = buckets[bucket];
    buckets[bucket] = str;

    string_count++;

    return str;
}

// Link an already-populated block (from allocUninterned) into the shard.
// `str->length` and `str->hash` must already be set.
void StringInternPool::Shard::insertPrepared(MobiusString* str) {
    float current_load = (float)(string_count + 1) / (float)buckets.size();
    if (current_load > load_factor) {
        resize();
    }

    size_t bucket = str->hash & (buckets.size() - 1);
    str->next = buckets[bucket];
    buckets[bucket] = str;

    string_count++;
}

void StringInternPool::Shard::resize() {
    size_t new_count = buckets.size() * 2;
    std::vector<MobiusString*> new_buckets(new_count, nullptr);

    for (MobiusString* head : buckets) {
        MobiusString* str = head;
        while (str) {
            MobiusString* next = str->next;
            size_t new_bucket = str->hash & (new_count - 1);
            str->next = new_buckets[new_bucket];
            new_buckets[new_bucket] = str;
            str = next;
        }
    }

    buckets = std::move(new_buckets);
}

// ============================================================================
// STRING INTERN POOL
// ============================================================================

StringInternPool::StringInternPool(size_t initial_bucket_count) {
    mobius_init_string_hash_seed();

    if (initial_bucket_count == 0) {
        initial_bucket_count = 256;
    }

    size_t per_shard = initial_bucket_count / SHARD_COUNT;
    if (per_shard == 0) per_shard = 16;

    size_t count = 1;
    while (count < per_shard) {
        count <<= 1;
    }

    for (int i = 0; i < SHARD_COUNT; i++) {
        shards_[i].buckets.assign(count, nullptr);
    }
}

StringInternPool::~StringInternPool() {
    for (int s = 0; s < SHARD_COUNT; s++) {
        for (MobiusString* head : shards_[s].buckets) {
            MobiusString* str = head;
            while (str) {
                MobiusString* next = str->next;
                // Header and characters are one block.
                ::operator delete(str);
                str = next;
            }
        }
    }
}

MobiusString* StringInternPool::intern(const char* data) {
    if (!data) return nullptr;
    return intern(data, strlen(data));
}

MobiusString* StringInternPool::intern(const char* data, size_t length) {
    if (!data) return nullptr;
    if (length > MobiusString::MAX_LENGTH) return nullptr;

    uint64_t hash = compute_string_hash(data, length);
    int shard_idx = shardIndex(hash);
    Shard& shard = shards_[shard_idx];

    std::lock_guard<std::mutex> lock(shard.mutex);

    MobiusString* existing = shard.find(data, length, hash);
    if (existing) {
        return existing;
    }

    return shard.insert(data, length, hash);
}

MobiusString* StringInternPool::allocUninterned(size_t capacity) {
    return alloc_string_block(capacity, MobiusString::IMMORTAL_RC);
}

void StringInternPool::freeUninterned(MobiusString* candidate) {
    if (candidate) ::operator delete(candidate);
}

MobiusString* StringInternPool::internFinalize(MobiusString* candidate, size_t actual_length) {
    if (!candidate) return nullptr;

    char* buf = candidate->mutableData();
    buf[actual_length] = '\0';
    candidate->length = (uint32_t)actual_length;

    uint64_t hash = compute_string_hash(buf, actual_length);
    candidate->hash = hash;

    Shard& shard = shards_[shardIndex(hash)];
    std::lock_guard<std::mutex> lock(shard.mutex);

    MobiusString* existing = shard.find(buf, actual_length, hash);
    if (existing) {
        ::operator delete(candidate);
        return existing;
    }

    shard.insertPrepared(candidate);
    return candidate;
}

void StringInternPool::stats(size_t* out_bucket_count, size_t* out_string_count,
                             float* out_load_factor) const {
    size_t total_buckets = 0;
    size_t total_strings = 0;
    for (int i = 0; i < SHARD_COUNT; i++) {
        std::lock_guard<std::mutex> lock(shards_[i].mutex);
        total_buckets += shards_[i].buckets.size();
        total_strings += shards_[i].string_count;
    }
    if (out_bucket_count) *out_bucket_count = total_buckets;
    if (out_string_count) *out_string_count = total_strings;
    if (out_load_factor) {
        *out_load_factor = total_buckets > 0
            ? (float)total_strings / (float)total_buckets
            : 0.0f;
    }
}

size_t StringInternPool::stringCount() const {
    size_t total = 0;
    for (int i = 0; i < SHARD_COUNT; i++) {
        std::lock_guard<std::mutex> lock(shards_[i].mutex);
        total += shards_[i].string_count;
    }
    return total;
}

size_t StringInternPool::bucketCount() const {
    size_t total = 0;
    for (int i = 0; i < SHARD_COUNT; i++) {
        std::lock_guard<std::mutex> lock(shards_[i].mutex);
        total += shards_[i].buckets.size();
    }
    return total;
}
