#ifndef MOBIUS_UTILITY_H
#define MOBIUS_UTILITY_H

#include <stddef.h>
#include <stdbool.h>

// String utilities
char* mobius_strdup(const char* s);
char* mobius_strndup(const char* s, size_t n);
bool mobius_str_starts_with(const char* str, const char* prefix);
bool mobius_str_ends_with(const char* str, const char* suffix);
void mobius_str_trim(char* str);

// Memory utilities
void* mobius_malloc(size_t size);
void* mobius_calloc(size_t count, size_t size);
void* mobius_realloc(void* ptr, size_t size);
void mobius_free(void* ptr);

// Array utilities
void* mobius_array_resize(void* array, size_t old_count, size_t new_count, size_t element_size);
bool mobius_array_contains_ptr(void** array, size_t count, void* ptr);

// Math utilities
size_t mobius_next_power_of_2(size_t n);
size_t mobius_min_size_t(size_t a, size_t b);
size_t mobius_max_size_t(size_t a, size_t b);

// File path utilities
char* mobius_get_filename(const char* path);
char* mobius_get_directory(const char* path);
char* mobius_join_path(const char* dir, const char* filename);

// Debug utilities
void mobius_debug_print(const char* format, ...);
void mobius_debug_hexdump(const void* data, size_t size);

#endif // MOBIUS_UTILITY_H
