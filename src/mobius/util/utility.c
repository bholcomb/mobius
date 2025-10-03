#include "utility.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <ctype.h>

// =============================================================================
// STRING UTILITIES
// =============================================================================

// Duplicate a string (portable strdup implementation)
char* mobius_strdup(const char* s) {
    if (!s) return NULL;
    
    size_t len = strlen(s) + 1;
    char* copy = malloc(len);
    if (copy) {
        memcpy(copy, s, len);
    }
    return copy;
}

// Duplicate first n characters of a string
char* mobius_strndup(const char* s, size_t n) {
    if (!s) return NULL;
    
    size_t len = strlen(s);
    if (n > len) n = len;
    
    char* copy = malloc(n + 1);
    if (copy) {
        memcpy(copy, s, n);
        copy[n] = '\0';
    }
    return copy;
}

// Check if string starts with prefix
bool mobius_str_starts_with(const char* str, const char* prefix) {
    if (!str || !prefix) return false;
    
    size_t str_len = strlen(str);
    size_t prefix_len = strlen(prefix);
    
    if (prefix_len > str_len) return false;
    
    return strncmp(str, prefix, prefix_len) == 0;
}

// Check if string ends with suffix
bool mobius_str_ends_with(const char* str, const char* suffix) {
    if (!str || !suffix) return false;
    
    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);
    
    if (suffix_len > str_len) return false;
    
    return strcmp(str + str_len - suffix_len, suffix) == 0;
}

// Trim whitespace from string (modifies in place)
void mobius_str_trim(char* str) {
    if (!str) return;
    
    // Trim leading whitespace
    char* start = str;
    while (isspace(*start)) start++;
    
    // Move string to beginning if needed
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
    
    // Trim trailing whitespace
    char* end = str + strlen(str) - 1;
    while (end > str && isspace(*end)) {
        *end = '\0';
        end--;
    }
}

// =============================================================================
// MEMORY UTILITIES
// =============================================================================

// Safe malloc with error checking
void* mobius_malloc(size_t size) {
    if (size == 0) return NULL;
    
    void* ptr = malloc(size);
    if (!ptr) {
        fprintf(stderr, "Mobius: Memory allocation failed for %zu bytes\n", size);
    }
    return ptr;
}

// Safe calloc with error checking
void* mobius_calloc(size_t count, size_t size) {
    if (count == 0 || size == 0) return NULL;
    
    void* ptr = calloc(count, size);
    if (!ptr) {
        fprintf(stderr, "Mobius: Memory allocation failed for %zu x %zu bytes\n", count, size);
    }
    return ptr;
}

// Safe realloc with error checking
void* mobius_realloc(void* ptr, size_t size) {
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    
    void* new_ptr = realloc(ptr, size);
    if (!new_ptr && size > 0) {
        fprintf(stderr, "Mobius: Memory reallocation failed for %zu bytes\n", size);
    }
    return new_ptr;
}

// Safe free (handles NULL pointers)
void mobius_free(void* ptr) {
    if (ptr) {
        free(ptr);
    }
}

// =============================================================================
// ARRAY UTILITIES
// =============================================================================

// Resize an array (handles growth and shrinkage)
void* mobius_array_resize(void* array, size_t old_count, size_t new_count, size_t element_size) {
    if (new_count == 0) {
        mobius_free(array);
        return NULL;
    }
    
    void* new_array = mobius_realloc(array, new_count * element_size);
    if (!new_array) {
        return array; // Keep old array if realloc fails
    }
    
    // Zero out new elements if growing
    if (new_count > old_count) {
        char* byte_array = (char*)new_array;
        memset(byte_array + (old_count * element_size), 0, (new_count - old_count) * element_size);
    }
    
    return new_array;
}

// Check if pointer array contains a specific pointer
bool mobius_array_contains_ptr(void** array, size_t count, void* ptr) {
    if (!array || count == 0) return false;
    
    for (size_t i = 0; i < count; i++) {
        if (array[i] == ptr) return true;
    }
    return false;
}

// =============================================================================
// MATH UTILITIES
// =============================================================================

// Get next power of 2 greater than or equal to n
size_t mobius_next_power_of_2(size_t n) {
    if (n == 0) return 1;
    
    // Handle overflow
    if (n > SIZE_MAX / 2) return SIZE_MAX;
    
    size_t power = 1;
    while (power < n) {
        power *= 2;
    }
    return power;
}

// Get minimum of two size_t values
size_t mobius_min_size_t(size_t a, size_t b) {
    return (a < b) ? a : b;
}

// Get maximum of two size_t values
size_t mobius_max_size_t(size_t a, size_t b) {
    return (a > b) ? a : b;
}

// =============================================================================
// FILE PATH UTILITIES
// =============================================================================

// Extract filename from path
char* mobius_get_filename(const char* path) {
    if (!path) return NULL;
    
    const char* last_slash = strrchr(path, '/');
    const char* last_backslash = strrchr(path, '\\');
    
    const char* filename = path;
    if (last_slash && last_slash > filename) filename = last_slash + 1;
    if (last_backslash && last_backslash > filename) filename = last_backslash + 1;
    
    return mobius_strdup(filename);
}

// Extract directory from path
char* mobius_get_directory(const char* path) {
    if (!path) return NULL;
    
    const char* last_slash = strrchr(path, '/');
    const char* last_backslash = strrchr(path, '\\');
    
    const char* separator = NULL;
    if (last_slash && last_backslash) {
        separator = (last_slash > last_backslash) ? last_slash : last_backslash;
    } else if (last_slash) {
        separator = last_slash;
    } else if (last_backslash) {
        separator = last_backslash;
    }
    
    if (!separator) {
        return mobius_strdup("."); // Current directory
    }
    
    size_t dir_len = separator - path;
    if (dir_len == 0) {
        return mobius_strdup("/"); // Root directory
    }
    
    return mobius_strndup(path, dir_len);
}

// Join directory and filename
char* mobius_join_path(const char* dir, const char* filename) {
    if (!dir || !filename) return NULL;
    
    size_t dir_len = strlen(dir);
    size_t filename_len = strlen(filename);
    
    // Check if we need a separator
    bool needs_separator = (dir_len > 0 && dir[dir_len - 1] != '/' && dir[dir_len - 1] != '\\');
    
    size_t total_len = dir_len + filename_len + (needs_separator ? 1 : 0) + 1;
    char* result = mobius_malloc(total_len);
    if (!result) return NULL;
    
    strcpy(result, dir);
    if (needs_separator) {
        strcat(result, "/");
    }
    strcat(result, filename);
    
    return result;
}

// =============================================================================
// DEBUG UTILITIES
// =============================================================================

// Debug print with prefix
void mobius_debug_print(const char* format, ...) {
    #ifdef MOBIUS_DEBUG
    printf("[MOBIUS DEBUG] ");
    
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    
    printf("\n");
    #else
    (void)format; // Suppress unused parameter warning
    #endif
}

// Hex dump for debugging binary data
void mobius_debug_hexdump(const void* data, size_t size) {
    #ifdef MOBIUS_DEBUG
    const unsigned char* bytes = (const unsigned char*)data;
    
    printf("[MOBIUS HEXDUMP] %zu bytes:\n", size);
    
    for (size_t i = 0; i < size; i += 16) {
        printf("%08zx: ", i);
        
        // Print hex bytes
        for (size_t j = 0; j < 16; j++) {
            if (i + j < size) {
                printf("%02x ", bytes[i + j]);
            } else {
                printf("   ");
            }
        }
        
        printf(" |");
        
        // Print ASCII representation
        for (size_t j = 0; j < 16 && i + j < size; j++) {
            unsigned char c = bytes[i + j];
            printf("%c", (c >= 32 && c <= 126) ? c : '.');
        }
        
        printf("|\n");
    }
    #else
    (void)data;
    (void)size;
    #endif
}
