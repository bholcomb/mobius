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

#endif // MOBIUS_UTILITY_H
