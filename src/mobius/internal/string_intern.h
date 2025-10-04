#ifndef MOBIUS_STRING_INTERN_H
#define MOBIUS_STRING_INTERN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Forward declarations
struct MobiusState;  // Defined in state/mobius_state.h
typedef struct StringInternPool StringInternPool;

/**
 * Interned immutable string with precomputed hash
 * All strings are interned - identical content = same pointer
 * Strings are immutable - modifications create new strings
 */
typedef struct MobiusString {
    const char* data;          // Null-terminated string data (owned by this struct)
    size_t length;             // Cached string length (excluding null terminator)
    uint32_t hash;             // Precomputed hash for fast comparison and table lookup
    int ref_count;             // Reference count (for future GC)
    struct MobiusString* next; // Next string in hash table bucket (for collision chaining)
} MobiusString;

/**
 * String intern pool - hash table for string deduplication
 * Lives in MobiusState for per-state isolation
 */
struct StringInternPool {
    MobiusString** buckets;    // Array of bucket heads (linked lists)
    size_t bucket_count;       // Current number of buckets (power of 2)
    size_t string_count;       // Total number of interned strings
    float load_factor;         // Resize threshold (typically 0.75)
};

// ============================================================================
// STRING INTERNING API
// ============================================================================

/**
 * Create/intern a string in the state's intern pool
 * If the string already exists, returns existing MobiusString* and increments refcount
 * If new, creates a new MobiusString, adds to pool, returns it
 * 
 * @param state The MobiusState containing the intern pool
 * @param data The C string to intern (will be copied)
 * @return Interned MobiusString* or NULL on allocation failure
 */
MobiusString* string_create(struct MobiusState* state, const char* data);

/**
 * Increment reference count of a string
 * Safe to call with NULL (no-op)
 * 
 * @param str The string to retain
 * @return The same string pointer (for convenience)
 */
MobiusString* string_retain(MobiusString* str);

/**
 * Decrement reference count of a string
 * Note: Currently strings are never removed from intern pool (future GC will handle this)
 * Safe to call with NULL (no-op)
 * 
 * @param str The string to release
 */
void string_release(MobiusString* str);

/**
 * Get the length of a string
 * 
 * @param str The string
 * @return Length in bytes (excluding null terminator), or 0 if NULL
 */
size_t string_length(const MobiusString* str);

/**
 * Get the raw C string data
 * 
 * @param str The string
 * @return Pointer to null-terminated C string, or NULL if str is NULL
 */
const char* string_data(const MobiusString* str);

/**
 * Get the precomputed hash of a string
 * 
 * @param str The string
 * @return 32-bit hash value, or 0 if NULL
 */
uint32_t string_hash(const MobiusString* str);

/**
 * Compare two strings for equality
 * Optimized: pointer equality (same intern) -> hash -> length -> content
 * 
 * @param a First string
 * @param b Second string
 * @return true if strings are equal, false otherwise
 */
bool string_equals(const MobiusString* a, const MobiusString* b);

// ============================================================================
// INTERN POOL MANAGEMENT (called by MobiusState)
// ============================================================================

/**
 * Create a new string intern pool
 * 
 * @param initial_bucket_count Initial number of buckets (should be power of 2)
 * @return New StringInternPool* or NULL on failure
 */
StringInternPool* string_pool_create(size_t initial_bucket_count);

/**
 * Free entire string intern pool and all interned strings
 * Called when MobiusState is destroyed
 * 
 * @param pool The pool to free
 */
void string_pool_free(StringInternPool* pool);

/**
 * Get statistics about the intern pool (for debugging/profiling)
 * 
 * @param pool The pool
 * @param out_bucket_count Output: number of buckets
 * @param out_string_count Output: number of interned strings
 * @param out_load_factor Output: current load factor
 */
void string_pool_stats(const StringInternPool* pool, 
                       size_t* out_bucket_count,
                       size_t* out_string_count, 
                       float* out_load_factor);

#endif // MOBIUS_STRING_INTERN_H

