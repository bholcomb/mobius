#include "internal/string_intern.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>

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
// STRING INTERN POOL
// ============================================================================

StringInternPool::StringInternPool(size_t initial_bucket_count)
    : string_count_(0), load_factor_(0.75f) {
    if (initial_bucket_count == 0) {
        initial_bucket_count = 256;
    }

    size_t count = 1;
    while (count < initial_bucket_count) {
        count <<= 1;
    }

    buckets_.assign(count, nullptr);
}

StringInternPool::~StringInternPool() {
    for (MobiusString* head : buckets_) {
        MobiusString* str = head;
        while (str) {
            MobiusString* next = str->next;
            delete[] str->data;
            delete str;
            str = next;
        }
    }
}

MobiusString* StringInternPool::intern(const char* data) {
    if (!data || buckets_.empty()) return nullptr;
    return intern(data, strlen(data));
}

MobiusString* StringInternPool::intern(const char* data, size_t length) {
    if (!data || buckets_.empty()) return nullptr;

    uint32_t hash = compute_string_hash(data, length);

    MobiusString* existing = find(data, length, hash);
    if (existing) {
        existing->ref_count++;
        return existing;
    }

    return insert(data, length, hash);
}

void StringInternPool::stats(size_t* out_bucket_count, size_t* out_string_count,
                             float* out_load_factor) const {
    if (out_bucket_count) *out_bucket_count = buckets_.size();
    if (out_string_count) *out_string_count = string_count_;
    if (out_load_factor) {
        *out_load_factor = !buckets_.empty()
            ? (float)string_count_ / (float)buckets_.size()
            : 0.0f;
    }
}

MobiusString* StringInternPool::find(const char* data, size_t len, uint32_t hash) const {
    size_t bucket = hash & (buckets_.size() - 1);
    MobiusString* str = buckets_[bucket];

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

MobiusString* StringInternPool::insert(const char* data, size_t len, uint32_t hash) {
    float current_load = (float)(string_count_ + 1) / (float)buckets_.size();
    if (current_load > load_factor_) {
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
    str->ref_count = 1;
    str->next = nullptr;

    size_t bucket = hash & (buckets_.size() - 1);
    str->next = buckets_[bucket];
    buckets_[bucket] = str;

    string_count_++;

    return str;
}

void StringInternPool::resize() {
    size_t new_count = buckets_.size() * 2;
    std::vector<MobiusString*> new_buckets(new_count, nullptr);

    for (MobiusString* head : buckets_) {
        MobiusString* str = head;
        while (str) {
            MobiusString* next = str->next;
            size_t new_bucket = str->hash & (new_count - 1);
            str->next = new_buckets[new_bucket];
            new_buckets[new_bucket] = str;
            str = next;
        }
    }

    buckets_ = std::move(new_buckets);
}
