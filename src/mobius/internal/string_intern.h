#ifndef MOBIUS_STRING_INTERN_H
#define MOBIUS_STRING_INTERN_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <vector>

// Forward declarations
class MobiusState;

struct MobiusString {
    const char* data;
    size_t length;
    uint32_t hash;
    int ref_count;
    MobiusString* next;

    MobiusString* retain() {
        ref_count++;
        return this;
    }

    void release() {
        ref_count--;
    }

    bool operator==(const MobiusString& other) const;
};

class StringInternPool {
public:
    explicit StringInternPool(size_t initial_bucket_count = 256);
    ~StringInternPool();

    StringInternPool(const StringInternPool&) = delete;
    StringInternPool& operator=(const StringInternPool&) = delete;

    MobiusString* intern(const char* data);
    MobiusString* intern(const char* data, size_t length);
    void stats(size_t* out_bucket_count, size_t* out_string_count,
               float* out_load_factor) const;

    size_t stringCount() const { return string_count_; }
    size_t bucketCount() const { return buckets_.size(); }

private:
    std::vector<MobiusString*> buckets_;
    size_t string_count_;
    float load_factor_;

    MobiusString* find(const char* data, size_t len, uint32_t hash) const;
    MobiusString* insert(const char* data, size_t len, uint32_t hash);
    void resize();
};

#endif // MOBIUS_STRING_INTERN_H
