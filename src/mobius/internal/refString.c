#include "internal/refString.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// STRING REFERENCE COUNTING IMPLEMENTATION
// ============================================================================

RefCountedString* string_create(const char* data) {
    if (!data) return NULL;
    
    RefCountedString* str = malloc(sizeof(RefCountedString));
    if (!str) return NULL;
    
    str->length = strlen(data);
    str->data = malloc(str->length + 1);
    if (!str->data) {
        free(str);
        return NULL;
    }
    
    strcpy(str->data, data);
    str->ref_count = 1;
    str->is_literal = false;
    
    return str;
}

RefCountedString* string_create_literal(const char* data) {
    if (!data) return NULL;
    
    RefCountedString* str = malloc(sizeof(RefCountedString));
    if (!str) return NULL;
    
    str->data = (char*)data;  // Point to literal, don't copy
    str->length = strlen(data);
    str->ref_count = 1;
    str->is_literal = true;   // Never free the data
    
    return str;
}

RefCountedString* string_retain(RefCountedString* str) {
    if (str) {
        str->ref_count++;
    }
    return str;
}

void string_release(RefCountedString* str) {
    if (!str) return;
    
    str->ref_count--;
    if (str->ref_count <= 0) {
        if (!str->is_literal) {
            free(str->data);  // Only free non-literals
        }
        free(str);
    }
}

size_t string_length(RefCountedString* str) {
    return str ? str->length : 0;
}

const char* string_data(RefCountedString* str) {
    return str ? str->data : NULL;
}

bool string_equals(RefCountedString* a, RefCountedString* b) {
    if (a == b) return true;  // Same pointer
    if (!a || !b) return false;  // One is NULL
    if (a->length != b->length) return false;  // Different lengths
    return strcmp(a->data, b->data) == 0;  // Compare content
}
