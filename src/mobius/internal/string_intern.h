#ifndef MOBIUS_STRING_INTERN_H
#define MOBIUS_STRING_INTERN_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <atomic>
#include <vector>
#include <mutex>

class MobiusState;

// Choose this process's string-hash seed (once). Called by the StringInternPool
// constructor. Honours MOBIUS_HASH_SEED to pin the seed for reproducible runs.
void mobius_init_string_hash_seed();

// The characters live in the same allocation as the header, immediately after
// it, so creating a string costs one allocation rather than two and the header
// and its bytes share a cache line. `data` is kept (rather than dropped in
// favour of an accessor) so the many `str->data` call sites keep working.
//
// There are two kinds of string:
//
//   * INTERNED strings — source literals, identifiers, metamethod names, keys
//     that came from the program text. Their count is bounded by the size of
//     the program, so they are IMMORTAL: refcount is a sentinel, retain/release
//     are no-ops, and copying one is a bare 16-byte Value move. This matters:
//     they are the most-shared objects in the runtime, and a refcount on them
//     would be a contended atomic on every worker thread.
//
//   * HEAP strings — anything computed at runtime (concat, str(), upper, JSON
//     values). Unbounded in number, never entered into the intern pool, and
//     reference counted so they can be reclaimed.
//
// `length` is 32-bit so the refcount fits in what would otherwise be padding:
// sizeof(MobiusString) stays 32 bytes. Strings are therefore capped at 4 GiB.
struct MobiusString {
    static constexpr uint32_t IMMORTAL_RC = 0xFFFFFFFFu;
    static constexpr size_t   MAX_LENGTH  = 0xFFFFFFFEu;

    const char* data;
    uint32_t length;
    std::atomic<uint32_t> refcount;
    // 64-bit: Table::tagFromHash() reads bits 57..63, which a 32-bit hash never
    // sets, so a 32-bit hash makes every string key share one tag byte.
    uint64_t hash;
    MobiusString* next;   // intern-pool chain; unused by heap strings

    bool operator==(const MobiusString& other) const;

    bool isImmortal() const {
        return refcount.load(std::memory_order_relaxed) == IMMORTAL_RC;
    }

    void retain() {
        if (isImmortal()) return;
        refcount.fetch_add(1, std::memory_order_relaxed);
    }

    void release() {
        if (isImmortal()) return;
        if (refcount.fetch_sub(1, std::memory_order_acq_rel) == 1) destroy();
    }

    void destroy();

    // Writable view of the inline character storage. Only valid before the
    // string is published — interned and shared strings are immutable.
    char* mutableData() { return reinterpret_cast<char*>(this + 1); }
};

static_assert(sizeof(MobiusString) == 32, "MobiusString must stay 32 bytes");

class StringInternPool {
public:
    static constexpr int SHARD_BITS  = 4;
    static constexpr int SHARD_COUNT = 1 << SHARD_BITS;

    explicit StringInternPool(size_t initial_bucket_count = 65536);
    ~StringInternPool();

    StringInternPool(const StringInternPool&) = delete;
    StringInternPool& operator=(const StringInternPool&) = delete;

    // Interned strings are immortal. Use these only for strings whose count is
    // bounded by the program text: literals, identifiers, metamethod names,
    // userdata type tags. Anything computed at runtime must use the heap
    // constructors below, or it will never be reclaimed.
    MobiusString* intern(const char* data);
    MobiusString* intern(const char* data, size_t length);

    // Heap strings: refcounted, never entered into the pool, returned with a
    // refcount of 1 which the caller owns. `hash` and `length` are filled in,
    // which Table's key comparison requires.
    static MobiusString* createHeap(const char* data, size_t length);
    static MobiusString* allocHeap(size_t capacity);              // fill via mutableData()
    static MobiusString* finishHeap(MobiusString* s, size_t len); // set length + hash

    // Build-in-place API, for callers that would otherwise allocate a scratch
    // buffer and hand it over. Allocate a string with room for `capacity`
    // characters, write into mutableData(), then finalize with the actual
    // length. finalize() hashes, deduplicates, and takes ownership: if an equal
    // string is already interned it frees the candidate and returns the
    // existing one. On any early-out, release the candidate with
    // freeUninterned(). This keeps string creation at one allocation.
    //
    // allocUninterned() leaves `hash` zero; internFinalize() fills it in. A
    // string that is never interned must still have `hash` and `length` set
    // before it is used as a table key — Table compares keys by (hash, length,
    // bytes) when the pointers differ, so a zero hash silently fails to match.
    MobiusString* allocUninterned(size_t capacity);
    MobiusString* internFinalize(MobiusString* candidate, size_t actual_length);
    static void   freeUninterned(MobiusString* candidate);

    void stats(size_t* out_bucket_count, size_t* out_string_count,
               float* out_load_factor) const;

    size_t stringCount() const;
    size_t bucketCount() const;

private:
    struct Shard {
        mutable std::mutex mutex;
        std::vector<MobiusString*> buckets;
        size_t string_count = 0;
        float load_factor = 0.75f;

        MobiusString* find(const char* data, size_t len, uint64_t hash) const;
        MobiusString* insert(const char* data, size_t len, uint64_t hash);
        void insertPrepared(MobiusString* str);
        void resize();
    };

    Shard shards_[SHARD_COUNT];

    // Select the shard from the HIGH bits. The bucket index within a shard is
    // `hash & (buckets.size() - 1)`, i.e. the LOW bits. Deriving the shard from
    // the low bits too would mean every string in shard `s` lands in a bucket
    // congruent to `s` mod SHARD_COUNT, leaving 15/16 of each shard's table
    // unreachable and chains ~6x longer than the load factor suggests (resize()
    // measures nominal load, so it never notices).
    static int shardIndex(uint64_t hash) {
        return (int)(hash >> (64 - SHARD_BITS));
    }
};

#endif // MOBIUS_STRING_INTERN_H
