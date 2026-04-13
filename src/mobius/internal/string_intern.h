#ifndef MOBIUS_STRING_INTERN_H
#define MOBIUS_STRING_INTERN_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <vector>
#include <mutex>

class MobiusState;

struct MobiusString {
    const char* data;
    size_t length;
    uint32_t hash;
    MobiusString* next;

    bool operator==(const MobiusString& other) const;
};

class StringInternPool {
public:
    static constexpr int SHARD_COUNT = 16;

    explicit StringInternPool(size_t initial_bucket_count = 65536);
    ~StringInternPool();

    StringInternPool(const StringInternPool&) = delete;
    StringInternPool& operator=(const StringInternPool&) = delete;

    MobiusString* intern(const char* data);
    MobiusString* intern(const char* data, size_t length);
    MobiusString* internOwned(char* data, size_t length);
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

        MobiusString* find(const char* data, size_t len, uint32_t hash) const;
        MobiusString* insert(const char* data, size_t len, uint32_t hash);
        MobiusString* insertOwned(char* data, size_t len, uint32_t hash);
        void resize();
    };

    Shard shards_[SHARD_COUNT];

    static int shardIndex(uint32_t hash) {
        return (int)(hash & (SHARD_COUNT - 1));
    }
};

#endif // MOBIUS_STRING_INTERN_H
