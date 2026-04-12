#include "internal/string_intern.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <new>

// ============================================================================
// HASH FUNCTION (Lua-style)
// ============================================================================

static uint32_t compute_string_hash(const char* str, size_t len) {
    uint32_t hash = (uint32_t)len;
    size_t step;

    if (len < 40) {
        step = 1;
    } else {
        step = (len / 32) + 1;
    }

    for (size_t i = len; i >= step; i -= step) {
        hash = hash ^ ((hash << 5) + (hash >> 2) + (unsigned char)str[i - 1]);
    }

    return hash;
}

// ============================================================================
// MOBIUS STRING
// ============================================================================

bool MobiusString::operator==(const MobiusString& other) const {
    if (this == &other) return true;
    if (hash != other.hash) return false;
    if (length != other.length) return false;

    if (length >= 40) {
        size_t mid = length / 2;
        if (data[0] != other.data[0]) return false;
        if (data[length - 1] != other.data[length - 1]) return false;
        if (data[mid] != other.data[mid]) return false;
    }

    return memcmp(data, other.data, length) == 0;
}

// ============================================================================
// SHARD METHODS
// ============================================================================

MobiusString* StringInternPool::Shard::find(const char* data, size_t len, uint32_t hash) const {
    if (buckets.empty()) return nullptr;
    size_t bucket = hash & (buckets.size() - 1);
    MobiusString* str = buckets[bucket];

    while (str) {
        if (str->hash == hash && str->length == len) {
            if (len >= 40) {
                size_t mid = len / 2;
                if (str->data[0] == data[0] &&
                    str->data[len - 1] == data[len - 1] &&
                    str->data[mid] == data[mid] &&
                    memcmp(str->data, data, len) == 0) {
                    return str;
                }
            } else {
                if (memcmp(str->data, data, len) == 0) {
                    return str;
                }
            }
        }
        str = str->next;
    }

    return nullptr;
}

MobiusString* StringInternPool::Shard::insert(const char* data, size_t len, uint32_t hash) {
    float current_load = (float)(string_count + 1) / (float)buckets.size();
    if (current_load > load_factor) {
        resize();
    }

    MobiusString* str = new (std::nothrow) MobiusString();
    if (!str) return nullptr;

    char* data_copy = new (std::nothrow) char[len + 1];
    if (!data_copy) {
        delete str;
        return nullptr;
    }

    memcpy(data_copy, data, len);
    data_copy[len] = '\0';

    str->data = data_copy;
    str->length = len;
    str->hash = hash;
    str->next = nullptr;

    size_t bucket = hash & (buckets.size() - 1);
    str->next = buckets[bucket];
    buckets[bucket] = str;

    string_count++;

    return str;
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
                delete[] str->data;
                delete str;
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

    uint32_t hash = compute_string_hash(data, length);
    int shard_idx = shardIndex(hash);
    Shard& shard = shards_[shard_idx];

    std::lock_guard<std::mutex> lock(shard.mutex);

    MobiusString* existing = shard.find(data, length, hash);
    if (existing) {
        return existing;
    }

    return shard.insert(data, length, hash);
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
