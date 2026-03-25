#include "library/string.h"
#include "data/value.h"
#include "data/array.h"
#include "state/mobius_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// =============================================================================
// UNIFIED STRING FUNCTION IMPLEMENTATIONS
// =============================================================================

int lib_len(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("len expects exactly 1 argument");
    }
    
    Value arg = state->npeek(0);
    state->npop(); // Remove argument
    
    if (arg.type == VAL_STRING && arg.as.string) {
        state->npush(make_int64_value((int64_t)arg.as.string->length));
    } else if (arg.type == VAL_ARRAY && arg.as.array) {
        state->npush(make_int64_value((int64_t)arg.as.array->length()));
    } else {
        return state->error("len expects a string or array argument");
    }
    
    return 1;
}
    
int lib_upper(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("upper expects exactly 1 argument");
    }
    
    Value arg = state->npeek(0);
    state->npop(); // Remove argument
    
    if (arg.type != VAL_STRING || !arg.as.string) {
        return state->error("upper expects a string argument");
    }
    
    const char* input = arg.as.string->data;
    size_t len = arg.as.string->length;
    char* upper_str = malloc(len + 1);
    if (!upper_str) {
        return state->error("Memory allocation failed");
    }
    
    for (size_t i = 0; i < len; i++) {
        upper_str[i] = toupper(input[i]);
    }
    upper_str[len] = '\0';
    
    MobiusString* result_string = state->stringPool()->intern(upper_str);
    free(upper_str);
    
    if (!result_string) {
        return state->error("String creation failed");
    }
    
    state->npush(make_string_value(result_string));
    return 1;
}

int lib_lower(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("lower expects exactly 1 argument");
    }
    
    Value arg = state->npeek(0);
    state->npop(); // Remove argument
    
    if (arg.type != VAL_STRING || !arg.as.string) {
        return state->error("lower expects a string argument");
    }
    
    const char* input = arg.as.string->data;
    size_t len = arg.as.string->length;
    char* lower_str = malloc(len + 1);
    if (!lower_str) {
        return state->error("Memory allocation failed");
    }
    
    for (size_t i = 0; i < len; i++) {
        lower_str[i] = tolower(input[i]);
    }
    lower_str[len] = '\0';
    
    MobiusString* result_string = state->stringPool()->intern(lower_str);
    free(lower_str);
    
    if (!result_string) {
        return state->error("String creation failed");
    }
    
    state->npush(make_string_value(result_string));
    return 1;
}

int lib_substr(MobiusState* state, int arg_count) {
    if (arg_count != 3) {
        return state->error("substr expects exactly 3 arguments (string, start, length)");
    }
    
    Value length_val = state->npeek(0);
    Value start_val = state->npeek(1);
    Value string_val = state->npeek(2);
    
    // Remove arguments
    for (int i = 0; i < 3; i++) {
        state->npop();
    }
    
    if (string_val.type != VAL_STRING || !string_val.as.string) {
        return state->error("substr expects first argument to be a string");
    }
    
    if (start_val.type != VAL_INT64 || length_val.type != VAL_INT64) {
        return state->error("substr expects start and length to be integers");
    }
    
    const char* input = string_val.as.string->data;
    size_t input_len = string_val.as.string->length;
    int64_t start = start_val.as.i64;
    int64_t length = length_val.as.i64;
    
    // Handle negative start (from end)
    if (start < 0) {
        start = (int64_t)input_len + start;
    }
    
    if (start < 0 || start >= (int64_t)input_len || length < 0) {
        state->npush(make_string_value_from_cstr(state, ""));
        return 1;
    }
    
    size_t actual_length = (size_t)length;
    if (start + length > (int64_t)input_len) {
        actual_length = input_len - (size_t)start;
    }
    
    char* substr_data = malloc(actual_length + 1);
    if (!substr_data) {
        return state->error("Memory allocation failed");
    }
    
    strncpy(substr_data, input + start, actual_length);
    substr_data[actual_length] = '\0';
    
    MobiusString* result_string = state->stringPool()->intern(substr_data);
    free(substr_data);
    
    if (!result_string) {
        return state->error("String creation failed");
    }
    
    state->npush(make_string_value(result_string));
    return 1;
}

int lib_concat(MobiusState* state, int arg_count) {
    if (arg_count < 2) {
        return state->error("concat expects at least 2 arguments");
    }
    
    // Calculate total length
    size_t total_length = 0;
    for (int i = 0; i < arg_count; i++) {
        Value arg = state->npeek(i);
        if (arg.type == VAL_STRING && arg.as.string) {
            total_length += arg.as.string->length;
        } else {
            return state->error("concat expects all arguments to be strings");
        }
    }
    
    char* result_data = malloc(total_length + 1);
    if (!result_data) {
        return state->error("Memory allocation failed");
    }
    
    size_t offset = 0;
    for (int i = arg_count - 1; i >= 0; i--) {
        Value arg = state->npeek(i);
        const char* str_data = arg.as.string->data;
        size_t str_len = arg.as.string->length;
        memcpy(result_data + offset, str_data, str_len);
        offset += str_len;
    }
    result_data[total_length] = '\0';
    
    // Remove arguments
    for (int i = 0; i < arg_count; i++) {
        state->npop();
    }
    
    MobiusString* result_string = state->stringPool()->intern(result_data);
    free(result_data);
    
    if (!result_string) {
        return state->error("String creation failed");
    }
    
    state->npush(make_string_value(result_string));
    return 1;
}

int lib_contains(MobiusState* state, int arg_count) {
    if (arg_count != 2) {
        return state->error("contains expects exactly 2 arguments (haystack, needle)");
    }
    
    Value needle_val = state->npeek(0);
    Value haystack_val = state->npeek(1);
    
    // Remove arguments
    state->npop();
    state->npop();
    
    if (haystack_val.type != VAL_STRING || !haystack_val.as.string ||
        needle_val.type != VAL_STRING || !needle_val.as.string) {
        return state->error("contains expects both arguments to be strings");
    }
    
    const char* haystack = haystack_val.as.string->data;
    const char* needle = needle_val.as.string->data;
    
    bool found = strstr(haystack, needle) != NULL;
    state->npush(make_bool_value(found));
    
    return 1;
}

int lib_split(MobiusState* state, int arg_count) {
    if (arg_count != 2) return state->error("split expects 2 arguments (string, delimiter)");

    Value delim_val = state->npeek(0);
    Value str_val = state->npeek(1);
    state->npop(); state->npop();

    if (str_val.type != VAL_STRING || !str_val.as.string ||
        delim_val.type != VAL_STRING || !delim_val.as.string)
        return state->error("split expects string arguments");

    const char* str = str_val.as.string->data;
    const char* delim = delim_val.as.string->data;
    size_t delim_len = delim_val.as.string->length;

    ArrayValue* arr = new ArrayValue();

    if (delim_len == 0) {
        size_t slen = str_val.as.string->length;
        for (size_t i = 0; i < slen; i++) {
            char buf[2] = { str[i], '\0' };
            arr->push(make_string_value_from_cstr(state, buf));
        }
    } else {
        const char* p = str;
        const char* found;
        while ((found = strstr(p, delim)) != NULL) {
            size_t seg_len = found - p;
            char* seg = (char*)malloc(seg_len + 1);
            memcpy(seg, p, seg_len); seg[seg_len] = '\0';
            arr->push(make_string_value_from_cstr(state, seg));
            free(seg);
            p = found + delim_len;
        }
        arr->push(make_string_value_from_cstr(state, p));
    }

    state->npush(make_array_value(arr));
    return 1;
}

int lib_join(MobiusState* state, int arg_count) {
    if (arg_count != 2) return state->error("join expects 2 arguments (array, separator)");

    Value sep_val = state->npeek(0);
    Value arr_val = state->npeek(1);
    state->npop(); state->npop();

    if (arr_val.type != VAL_ARRAY || !arr_val.as.array)
        return state->error("join expects first argument to be an array");
    if (sep_val.type != VAL_STRING || !sep_val.as.string)
        return state->error("join expects second argument to be a string");

    ArrayValue* arr = arr_val.as.array;
    const char* sep = sep_val.as.string->data;
    size_t sep_len = sep_val.as.string->length;
    size_t count = arr->length();

    size_t total = 0;
    for (size_t i = 0; i < count; i++) {
        Value v = arr->get(i);
        if (v.type == VAL_STRING && v.as.string) total += v.as.string->length;
        if (i > 0) total += sep_len;
    }

    char* buf = (char*)malloc(total + 1);
    size_t offset = 0;
    for (size_t i = 0; i < count; i++) {
        if (i > 0) { memcpy(buf + offset, sep, sep_len); offset += sep_len; }
        Value v = arr->get(i);
        if (v.type == VAL_STRING && v.as.string) {
            memcpy(buf + offset, v.as.string->data, v.as.string->length);
            offset += v.as.string->length;
        }
    }
    buf[offset] = '\0';

    MobiusString* result = state->stringPool()->intern(buf);
    free(buf);
    state->npush(make_string_value(result));
    return 1;
}

int lib_trim(MobiusState* state, int arg_count) {
    if (arg_count != 1) return state->error("trim expects 1 argument");

    Value arg = state->npeek(0);
    state->npop();

    if (arg.type != VAL_STRING || !arg.as.string)
        return state->error("trim expects a string argument");

    const char* s = arg.as.string->data;
    size_t len = arg.as.string->length;

    size_t start = 0;
    while (start < len && isspace((unsigned char)s[start])) start++;
    size_t end = len;
    while (end > start && isspace((unsigned char)s[end - 1])) end--;

    size_t new_len = end - start;
    char* buf = (char*)malloc(new_len + 1);
    memcpy(buf, s + start, new_len);
    buf[new_len] = '\0';

    MobiusString* result = state->stringPool()->intern(buf);
    free(buf);
    state->npush(make_string_value(result));
    return 1;
}

int lib_startswith(MobiusState* state, int arg_count) {
    if (arg_count != 2) return state->error("startswith expects 2 arguments");

    Value prefix_val = state->npeek(0);
    Value str_val = state->npeek(1);
    state->npop(); state->npop();

    if (str_val.type != VAL_STRING || !str_val.as.string ||
        prefix_val.type != VAL_STRING || !prefix_val.as.string)
        return state->error("startswith expects string arguments");

    const char* s = str_val.as.string->data;
    const char* prefix = prefix_val.as.string->data;
    size_t plen = prefix_val.as.string->length;

    bool result = str_val.as.string->length >= plen && memcmp(s, prefix, plen) == 0;
    state->npush(make_bool_value(result));
    return 1;
}

int lib_endswith(MobiusState* state, int arg_count) {
    if (arg_count != 2) return state->error("endswith expects 2 arguments");

    Value suffix_val = state->npeek(0);
    Value str_val = state->npeek(1);
    state->npop(); state->npop();

    if (str_val.type != VAL_STRING || !str_val.as.string ||
        suffix_val.type != VAL_STRING || !suffix_val.as.string)
        return state->error("endswith expects string arguments");

    size_t slen = str_val.as.string->length;
    size_t suflen = suffix_val.as.string->length;

    bool result = slen >= suflen &&
        memcmp(str_val.as.string->data + slen - suflen, suffix_val.as.string->data, suflen) == 0;
    state->npush(make_bool_value(result));
    return 1;
}

int lib_replace(MobiusState* state, int arg_count) {
    if (arg_count != 3) return state->error("replace expects 3 arguments (string, old, new)");

    Value new_val = state->npeek(0);
    Value old_val = state->npeek(1);
    Value str_val = state->npeek(2);
    state->npop(); state->npop(); state->npop();

    if (str_val.type != VAL_STRING || !str_val.as.string ||
        old_val.type != VAL_STRING || !old_val.as.string ||
        new_val.type != VAL_STRING || !new_val.as.string)
        return state->error("replace expects string arguments");

    const char* s = str_val.as.string->data;
    const char* old_s = old_val.as.string->data;
    const char* new_s = new_val.as.string->data;
    size_t old_len = old_val.as.string->length;
    size_t new_len = new_val.as.string->length;

    if (old_len == 0) {
        state->npush(str_val);
        return 1;
    }

    size_t count = 0;
    const char* p = s;
    while ((p = strstr(p, old_s)) != NULL) { count++; p += old_len; }

    size_t result_len = str_val.as.string->length + count * (new_len - old_len);
    char* buf = (char*)malloc(result_len + 1);
    char* dst = buf;
    p = s;
    const char* found;
    while ((found = strstr(p, old_s)) != NULL) {
        size_t seg = found - p;
        memcpy(dst, p, seg); dst += seg;
        memcpy(dst, new_s, new_len); dst += new_len;
        p = found + old_len;
    }
    strcpy(dst, p);

    MobiusString* result = state->stringPool()->intern(buf);
    free(buf);
    state->npush(make_string_value(result));
    return 1;
}

int lib_find(MobiusState* state, int arg_count) {
    if (arg_count != 2) return state->error("find expects 2 arguments (string, substring)");

    Value needle_val = state->npeek(0);
    Value hay_val = state->npeek(1);
    state->npop(); state->npop();

    if (hay_val.type != VAL_STRING || !hay_val.as.string ||
        needle_val.type != VAL_STRING || !needle_val.as.string)
        return state->error("find expects string arguments");

    const char* found = strstr(hay_val.as.string->data, needle_val.as.string->data);
    if (found) {
        state->npush(make_int64_value((int64_t)(found - hay_val.as.string->data)));
    } else {
        state->npush(make_int64_value(-1));
    }
    return 1;
}

int lib_repeat(MobiusState* state, int arg_count) {
    if (arg_count != 2) return state->error("repeat expects 2 arguments (string, count)");

    Value count_val = state->npeek(0);
    Value str_val = state->npeek(1);
    state->npop(); state->npop();

    if (str_val.type != VAL_STRING || !str_val.as.string)
        return state->error("repeat expects first argument to be a string");
    if (count_val.type != VAL_INT64)
        return state->error("repeat expects second argument to be an integer");

    int64_t count = count_val.as.i64;
    if (count <= 0) {
        state->npush(make_string_value_from_cstr(state, ""));
        return 1;
    }

    size_t slen = str_val.as.string->length;
    size_t total = slen * (size_t)count;
    char* buf = (char*)malloc(total + 1);
    for (int64_t i = 0; i < count; i++) {
        memcpy(buf + i * slen, str_val.as.string->data, slen);
    }
    buf[total] = '\0';

    MobiusString* result = state->stringPool()->intern(buf);
    free(buf);
    state->npush(make_string_value(result));
    return 1;
}