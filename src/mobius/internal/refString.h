#ifndef MOBIUS_REF_STRING_H
#define MOBIUS_REF_STRING_H

#include <stdbool.h>
#include <stddef.h>

// Reference counted string structure
typedef struct RefCountedString {
    char* data;           // Actual string data (null-terminated)
    size_t length;        // Cached string length for O(1) operations
    int ref_count;        // Reference count
    bool is_literal;      // True for string literals (never freed)
} RefCountedString;

// String reference counting functions
RefCountedString* string_create(const char* data);
RefCountedString* string_create_literal(const char* data);
RefCountedString* string_retain(RefCountedString* str);
void string_release(RefCountedString* str);
size_t string_length(RefCountedString* str);
const char* string_data(RefCountedString* str);
bool string_equals(RefCountedString* a, RefCountedString* b);


#endif // MOBIUS_REF_STRING_H