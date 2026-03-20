#include "utility.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// =============================================================================
// STRING UTILITIES
// =============================================================================

char* mobius_strdup(const char* s) {
    if (!s) return NULL;
    
    size_t len = strlen(s) + 1;
    char* copy = (char*)malloc(len);
    if (copy) {
        memcpy(copy, s, len);
    }
    return copy;
}

char* mobius_strndup(const char* s, size_t n) {
    if (!s) return NULL;
    
    size_t len = strlen(s);
    if (n > len) n = len;
    
    char* copy = (char*)malloc(n + 1);
    if (copy) {
        memcpy(copy, s, n);
        copy[n] = '\0';
    }
    return copy;
}

bool mobius_str_starts_with(const char* str, const char* prefix) {
    if (!str || !prefix) return false;
    
    size_t str_len = strlen(str);
    size_t prefix_len = strlen(prefix);
    
    if (prefix_len > str_len) return false;
    
    return strncmp(str, prefix, prefix_len) == 0;
}

bool mobius_str_ends_with(const char* str, const char* suffix) {
    if (!str || !suffix) return false;
    
    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);
    
    if (suffix_len > str_len) return false;
    
    return strcmp(str + str_len - suffix_len, suffix) == 0;
}

void mobius_str_trim(char* str) {
    if (!str) return;
    
    char* start = str;
    while (isspace(*start)) start++;
    
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
    
    char* end = str + strlen(str) - 1;
    while (end > str && isspace(*end)) {
        *end = '\0';
        end--;
    }
}
