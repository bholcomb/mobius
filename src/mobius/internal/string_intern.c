#include "internal/string_intern.h"
#include "state/mobius_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// HASH FUNCTION (Lua-style)
// ============================================================================

/**
 * Compute hash for a string using Lua's hash algorithm
 * - Strings < 40 chars: hash every byte
 * - Strings >= 40 chars: sample with step = len/32 + 1
 * 
 * This balances speed vs distribution quality for long strings
 */
static uint32_t compute_string_hash(const char* str, size_t len) {
    uint32_t hash = (uint32_t)len;
    size_t step;
    
    if (len < 40) {
        // Short strings: hash every character
        step = 1;
    } else {
        // Long strings: sample characters
        step = (len / 32) + 1;
    }
    
    // Lua's hash algorithm
    for (size_t i = len; i >= step; i -= step) {
        hash = hash ^ ((hash << 5) + (hash >> 2) + (unsigned char)str[i - 1]);
    }
    
    return hash;
}

// ============================================================================
// STRING COMPARISON (Layered for Performance)
// ============================================================================

/**
 * Compare two strings with layered optimization:
 * 1. Pointer equality (instant for interned strings)
 * 2. Hash comparison (fast rejection)
 * 3. Length comparison (fast rejection)
 * 4. For long strings: sample first/last/middle characters
 * 5. Full content comparison (correctness guarantee)
 */
bool string_equals(const MobiusString* a, const MobiusString* b) {
    // Layer 1: Pointer equality (same interned string)
    if (a == b) return true;
    
    // Null checks
    if (!a || !b) return false;
    
    // Layer 2: Hash comparison
    if (a->hash != b->hash) return false;
    
    // Layer 3: Length comparison
    if (a->length != b->length) return false;
    
    // Layer 4: For long strings, sample key positions
    if (a->length >= 40) {
        size_t mid = a->length / 2;
        if (a->data[0] != b->data[0]) return false;           // First char
        if (a->data[a->length - 1] != b->data[b->length - 1]) return false;  // Last char
        if (a->data[mid] != b->data[mid]) return false;       // Middle char
    }
    
    // Layer 5: Full string comparison (final correctness check)
    return memcmp(a->data, b->data, a->length) == 0;
}

// ============================================================================
// INTERN POOL MANAGEMENT
// ============================================================================

StringInternPool* string_pool_create(size_t initial_bucket_count) {
    // Ensure bucket count is power of 2
    if (initial_bucket_count == 0) {
        initial_bucket_count = 256;  // Default
    }
    
    // Round up to next power of 2
    size_t count = 1;
    while (count < initial_bucket_count) {
        count <<= 1;
    }
    
    StringInternPool* pool = malloc(sizeof(StringInternPool));
    if (!pool) return NULL;
    
    pool->buckets = calloc(count, sizeof(MobiusString*));
    if (!pool->buckets) {
        free(pool);
        return NULL;
    }
    
    pool->bucket_count = count;
    pool->string_count = 0;
    pool->load_factor = 0.75f;  // Resize at 75% capacity
    
    return pool;
}

void string_pool_free(StringInternPool* pool) {
    if (!pool) return;
    
    // Free all strings in all buckets
    for (size_t i = 0; i < pool->bucket_count; i++) {
        MobiusString* str = pool->buckets[i];
        while (str) {
            MobiusString* next = str->next;
            free((void*)str->data);  // Free string data
            free(str);               // Free string struct
            str = next;
        }
    }
    
    free(pool->buckets);
    free(pool);
}

void string_pool_stats(const StringInternPool* pool, 
                       size_t* out_bucket_count,
                       size_t* out_string_count, 
                       float* out_load_factor) {
    if (!pool) return;
    
    if (out_bucket_count) *out_bucket_count = pool->bucket_count;
    if (out_string_count) *out_string_count = pool->string_count;
    if (out_load_factor) {
        *out_load_factor = pool->bucket_count > 0 
            ? (float)pool->string_count / (float)pool->bucket_count
            : 0.0f;
    }
}

/**
 * Resize the intern pool (double the bucket count)
 * Called when load factor exceeds threshold
 */
static bool string_pool_resize(StringInternPool* pool) {
    size_t new_bucket_count = pool->bucket_count * 2;
    MobiusString** new_buckets = calloc(new_bucket_count, sizeof(MobiusString*));
    if (!new_buckets) return false;
    
    // Rehash all strings into new buckets
    for (size_t i = 0; i < pool->bucket_count; i++) {
        MobiusString* str = pool->buckets[i];
        while (str) {
            MobiusString* next = str->next;
            
            // Rehash into new buckets
            size_t new_bucket = str->hash & (new_bucket_count - 1);
            str->next = new_buckets[new_bucket];
            new_buckets[new_bucket] = str;
            
            str = next;
        }
    }
    
    free(pool->buckets);
    pool->buckets = new_buckets;
    pool->bucket_count = new_bucket_count;
    
    return true;
}

/**
 * Find an existing string in the pool or return NULL
 */
static MobiusString* string_pool_find(StringInternPool* pool, const char* data, size_t len, uint32_t hash) {
    if (!pool || !data) return NULL;
    
    size_t bucket = hash & (pool->bucket_count - 1);
    MobiusString* str = pool->buckets[bucket];
    
    while (str) {
        // Use our layered comparison (skips pointer check since we know they're different)
        if (str->hash == hash && 
            str->length == len) {
            
            // For long strings, sample key positions first
            if (len >= 40) {
                size_t mid = len / 2;
                if (str->data[0] == data[0] &&
                    str->data[len - 1] == data[len - 1] &&
                    str->data[mid] == data[mid] &&
                    memcmp(str->data, data, len) == 0) {
                    return str;
                }
            } else {
                // Short strings: just compare content
                if (memcmp(str->data, data, len) == 0) {
                    return str;
                }
            }
        }
        
        str = str->next;
    }
    
    return NULL;
}

/**
 * Insert a new string into the pool
 * Returns the newly created MobiusString
 */
static MobiusString* string_pool_insert(StringInternPool* pool, const char* data, size_t len, uint32_t hash) {
    // Check if we need to resize
    float current_load = (float)(pool->string_count + 1) / (float)pool->bucket_count;
    if (current_load > pool->load_factor) {
        string_pool_resize(pool);  // Best effort - continue even if resize fails
    }
    
    // Create new string
    MobiusString* str = malloc(sizeof(MobiusString));
    if (!str) return NULL;
    
    char* data_copy = malloc(len + 1);
    if (!data_copy) {
        free(str);
        return NULL;
    }
    
    memcpy(data_copy, data, len);
    data_copy[len] = '\0';
    
    str->data = data_copy;
    str->length = len;
    str->hash = hash;
    str->ref_count = 1;
    str->next = NULL;
    
    // Insert into bucket
    size_t bucket = hash & (pool->bucket_count - 1);
    str->next = pool->buckets[bucket];
    pool->buckets[bucket] = str;
    
    pool->string_count++;
    
    return str;
}

// ============================================================================
// PUBLIC STRING API
// ============================================================================

MobiusString* string_create(MobiusState* state, const char* data) {
    if (!state || !state->string_pool || !data) {
        return NULL;
    }
    
    size_t len = strlen(data);
    uint32_t hash = compute_string_hash(data, len);
    
    // Try to find existing string
    MobiusString* existing = string_pool_find(state->string_pool, data, len, hash);
    if (existing) {
        // String already interned, increment refcount and return
        existing->ref_count++;
        return existing;
    }
    
    // Create new interned string
    return string_pool_insert(state->string_pool, data, len, hash);
}

MobiusString* string_retain(MobiusString* str) {
    if (str) {
        str->ref_count++;
    }
    return str;
}

void string_release(MobiusString* str) {
    if (!str) return;
    
    str->ref_count--;
    
    // Note: We don't remove strings from intern pool when refcount hits 0
    // Future GC will handle cleanup during state destruction or GC cycles
    // For now, strings live until MobiusState is destroyed
}

size_t string_length(const MobiusString* str) {
    return str ? str->length : 0;
}

const char* string_data(const MobiusString* str) {
    return str ? str->data : NULL;
}

uint32_t string_hash(const MobiusString* str) {
    return str ? str->hash : 0;
}

