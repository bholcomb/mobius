#include <vector>
#include "library/string.h"
#include "data/value.h"
#include "data/array.h"
#include "data/array_slice.h"
#include "data/buffer.h"
#include "data/table.h"
#include "data/shared_cell.h"
#include "state/mobius_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits>
#include <new>

static bool checked_add_size(size_t a, size_t b, size_t* out) {
    if (b > std::numeric_limits<size_t>::max() - a) return false;
    *out = a + b;
    return true;
}

static bool checked_mul_size(size_t a, size_t b, size_t* out) {
    if (a != 0 && b > std::numeric_limits<size_t>::max() / a) return false;
    *out = a * b;
    return true;
}

// Reserve a HEAP string with room for `capacity` characters, ready to be
// written through pending->mutableData(). Computed strings (concat, upper,
// substr, …) are unbounded in number, so they are NOT interned — interning them
// would leak, since the pool never reclaims. They are refcounted instead; the
// result is returned with refcount 1, which the caller transfers to the stack
// via make_string_value_adopt(). Pair with finalize_string().
static MobiusString* alloc_pending_string(MobiusState* state, size_t capacity) {
    (void)state;
    return StringInternPool::allocHeap(capacity);
}

static MobiusString* finalize_string(MobiusState* state, MobiusString* pending, size_t len) {
    (void)state;
    if (!pending) return nullptr;
    return StringInternPool::finishHeap(pending, len);   // sets length + hash, keeps rc = 1
}

// Length-aware substring search. Mobius strings carry their length and may
// contain NUL bytes, so the C string functions (strstr, strcpy) are wrong
// here: a needle of "\0" looked empty to strstr, which matched forever
// without advancing (split/replace hung reading past the string), and any
// text after an embedded NUL was ignored. Returns a pointer into hay, or
// nullptr. An empty needle matches at hay.
static const char* find_bytes(const char* hay, size_t hay_len,
                              const char* needle, size_t needle_len) {
    if (needle_len == 0) return hay;
    if (needle_len > hay_len) return nullptr;
    const char* last = hay + (hay_len - needle_len);
    for (const char* p = hay; p <= last; ) {
        p = (const char*)memchr(p, needle[0], (size_t)(last - p) + 1);
        if (!p) return nullptr;
        if (memcmp(p, needle, needle_len) == 0) return p;
        p++;
    }
    return nullptr;
}

// A new heap string holding exactly len bytes (which may include NULs).
static MobiusString* make_bytes_string(MobiusState* state, const char* data, size_t len) {
    MobiusString* pending = alloc_pending_string(state, len);
    if (!pending) return nullptr;
    char* out = pending->mutableData();
    memcpy(out, data, len);
    out[len] = '\0';
    return finalize_string(state, pending, len);
}

// =============================================================================
// UNIFIED STRING FUNCTION IMPLEMENTATIONS
// =============================================================================

int lib_len(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("size expects exactly 1 argument");
    }
    
    Value arg = state->npeek(0);
    state->npop(); // Remove argument

    if (arg.type == VAL_SHARED_CELL && arg.as.shared_cell) {
        arg = arg.as.shared_cell->load();
    }
    
    if (arg.type == VAL_STRING && arg.as.string) {
        state->npush(make_int64_value((int64_t)arg.as.string->length));
    } else if (arg.type == VAL_ARRAY && arg.as.array) {
        state->npush(make_int64_value((int64_t)arg.as.array->length()));
    } else if (arg.type == VAL_ARRAY_SLICE && arg.as.array_slice) {
        state->npush(make_int64_value((int64_t)arg.as.array_slice->length()));
    } else if (arg.type == VAL_BUFFER && arg.as.buffer) {
        state->npush(make_int64_value((int64_t)arg.as.buffer->size()));
    } else if (arg.type == VAL_TABLE && arg.as.table) {
        state->npush(make_int64_value((int64_t)arg.as.table->size()));
    } else {
        return state->error("size expects a string, array, table, or buffer argument");
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
    MobiusString* pending = alloc_pending_string(state, len);
    if (!pending) {
        return state->error("Memory allocation failed");
    }
    char* upper_str = pending->mutableData();

    for (size_t i = 0; i < len; i++) {
        upper_str[i] = toupper(input[i]);
    }
    upper_str[len] = '\0';

    MobiusString* result_string = finalize_string(state, pending, len);

    if (!result_string) {
        return state->error("String creation failed");
    }
    
    state->npush(make_string_value_adopt(result_string));
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
    MobiusString* pending = alloc_pending_string(state, len);
    if (!pending) {
        return state->error("Memory allocation failed");
    }
    char* lower_str = pending->mutableData();

    for (size_t i = 0; i < len; i++) {
        lower_str[i] = tolower(input[i]);
    }
    lower_str[len] = '\0';

    MobiusString* result_string = finalize_string(state, pending, len);

    if (!result_string) {
        return state->error("String creation failed");
    }
    
    state->npush(make_string_value_adopt(result_string));
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
    
    // Clamp to the end of the string. Compared against the remaining length:
    // `start + length` overflowed for a huge length, skipping the clamp and
    // failing the allocation.
    size_t actual_length = (size_t)length;
    if (length > (int64_t)input_len - start) {
        actual_length = input_len - (size_t)start;
    }
    
    MobiusString* pending = alloc_pending_string(state, actual_length);
    if (!pending) {
        return state->error("Memory allocation failed");
    }
    char* substr_data = pending->mutableData();

    memcpy(substr_data, input + start, actual_length);
    substr_data[actual_length] = '\0';

    MobiusString* result_string = finalize_string(state, pending, actual_length);

    if (!result_string) {
        return state->error("String creation failed");
    }
    
    state->npush(make_string_value_adopt(result_string));
    return 1;
}

int lib_concat(MobiusState* state, int arg_count) {
    if (arg_count < 2) {
        return state->error("concat expects at least 2 arguments");
    }

    const auto& common = state->commonStrings();
    size_t total_length = 0;
    int non_empty_count = 0;
    MobiusString* single_non_empty = nullptr;
    for (int i = 0; i < arg_count; i++) {
        const Value& arg = state->npeek(i);
        if (arg.type == VAL_STRING && arg.as.string) {
            size_t str_len = arg.as.string->length;
            if (!checked_add_size(total_length, str_len, &total_length)) {
                return state->error("concat result too large");
            }
            if (str_len != 0) {
                non_empty_count++;
                single_non_empty = arg.as.string;
            }
        } else {
            return state->error("concat expects all arguments to be strings");
        }
    }

    if (non_empty_count <= 1) {
        for (int i = 0; i < arg_count; i++) {
            state->npop();
        }
        state->npush(make_string_value(non_empty_count == 0 ? common.empty : single_non_empty));
        return 1;
    }

    size_t alloc_size = 0;
    if (!checked_add_size(total_length, 1, &alloc_size)) {
        return state->error("concat result too large");
    }
    MobiusString* pending = alloc_pending_string(state, total_length);
    if (!pending) {
        return state->error("Memory allocation failed");
    }
    char* result_data = pending->mutableData();

    size_t offset = 0;
    for (int i = arg_count - 1; i >= 0; i--) {
        const Value& arg = state->npeek(i);
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
    
    MobiusString* result_string = finalize_string(state, pending, total_length);

    if (!result_string) {
        return state->error("String creation failed");
    }

    state->npush(make_string_value_adopt(result_string));
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
    
    bool found = find_bytes(haystack_val.as.string->data, haystack_val.as.string->length,
                            needle_val.as.string->data, needle_val.as.string->length) != nullptr;
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

    ArrayValue* arr = new (std::nothrow) ArrayValue();
    if (!arr) {
        return state->error("Memory allocation failed");
    }

    size_t str_len = str_val.as.string->length;
    if (delim_len == 0) {
        for (size_t i = 0; i < str_len; i++) {
            MobiusString* ch = make_bytes_string(state, str + i, 1);
            if (!ch) {
                arr->release();
                return state->error("String creation failed");
            }
            arr->push(make_string_value_adopt(ch));
        }
    } else {
        const char* p = str;
        const char* end = str + str_len;
        while (true) {
            const char* found = find_bytes(p, (size_t)(end - p), delim, delim_len);
            const char* seg_end = found ? found : end;
            MobiusString* seg_str = make_bytes_string(state, p, (size_t)(seg_end - p));
            if (!seg_str) {
                arr->release();
                return state->error("String creation failed");
            }
            arr->push(make_string_value_adopt(seg_str));
            if (!found) break;
            p = found + delim_len;
        }
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

    // Non-string elements are converted the way string concatenation
    // converts them (`"" + 2.5`). They used to be dropped silently, so
    // join([1, "a"], ",") gave ",a".
    std::vector<Value> parts;
    parts.reserve(count);
    size_t total = 0;
    for (size_t i = 0; i < count; i++) {
        Value v = arr->get(i);
        if (v.type != VAL_STRING || !v.as.string) v = value_to_string_value(state, v);
        size_t len = (v.type == VAL_STRING && v.as.string) ? v.as.string->length : 0;
        parts.push_back(v);
        if (!checked_add_size(total, len, &total)) {
            return state->error("join result too large");
        }
        if (i > 0 && !checked_add_size(total, sep_len, &total)) {
            return state->error("join result too large");
        }
    }

    size_t alloc_size = 0;
    if (!checked_add_size(total, 1, &alloc_size)) {
        return state->error("join result too large");
    }
    MobiusString* pending = alloc_pending_string(state, total);
    if (!pending) {
        return state->error("Memory allocation failed");
    }
    char* buf = pending->mutableData();
    size_t offset = 0;
    for (size_t i = 0; i < count; i++) {
        if (i > 0) { memcpy(buf + offset, sep, sep_len); offset += sep_len; }
        const Value& v = parts[i];
        if (v.type == VAL_STRING && v.as.string) {
            memcpy(buf + offset, v.as.string->data, v.as.string->length);
            offset += v.as.string->length;
        }
    }
    buf[offset] = '\0';

    MobiusString* result = finalize_string(state, pending, total);
    if (!result) {
        return state->error("String creation failed");
    }
    state->npush(make_string_value_adopt(result));
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
    MobiusString* pending = alloc_pending_string(state, new_len);
    if (!pending) {
        return state->error("Memory allocation failed");
    }
    char* buf = pending->mutableData();
    memcpy(buf, s + start, new_len);
    buf[new_len] = '\0';

    MobiusString* result = finalize_string(state, pending, new_len);
    if (!result) {
        return state->error("String creation failed");
    }
    state->npush(make_string_value_adopt(result));
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

    size_t s_len = str_val.as.string->length;
    const char* s_end = s + s_len;
    size_t count = 0;
    const char* p = s;
    while ((p = find_bytes(p, (size_t)(s_end - p), old_s, old_len)) != nullptr) { count++; p += old_len; }

    size_t replacement_delta = 0;
    if (new_len >= old_len) {
        if (!checked_mul_size(count, new_len - old_len, &replacement_delta) ||
            !checked_add_size(str_val.as.string->length, replacement_delta, &replacement_delta)) {
            return state->error("replace result too large");
        }
    } else {
        size_t shrink_total = 0;
        if (!checked_mul_size(count, old_len - new_len, &shrink_total) ||
            shrink_total > str_val.as.string->length) {
            return state->error("replace result too large");
        }
        replacement_delta = str_val.as.string->length - shrink_total;
    }

    size_t result_len = replacement_delta;
    size_t alloc_size = 0;
    if (!checked_add_size(result_len, 1, &alloc_size)) {
        return state->error("replace result too large");
    }
    MobiusString* pending = alloc_pending_string(state, result_len);
    if (!pending) {
        return state->error("Memory allocation failed");
    }
    char* buf = pending->mutableData();
    char* dst = buf;
    p = s;
    const char* found;
    while ((found = find_bytes(p, (size_t)(s_end - p), old_s, old_len)) != nullptr) {
        size_t seg = found - p;
        memcpy(dst, p, seg); dst += seg;
        memcpy(dst, new_s, new_len); dst += new_len;
        p = found + old_len;
    }
    memcpy(dst, p, (size_t)(s_end - p));
    dst[s_end - p] = '\0';

    MobiusString* result = finalize_string(state, pending, result_len);
    if (!result) {
        return state->error("String creation failed");
    }
    state->npush(make_string_value_adopt(result));
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

    const char* found = find_bytes(hay_val.as.string->data, hay_val.as.string->length,
                                   needle_val.as.string->data, needle_val.as.string->length);
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
    size_t total = 0;
    if (!checked_mul_size(slen, (size_t)count, &total)) {
        return state->error("repeat result too large");
    }
    size_t alloc_size = 0;
    if (!checked_add_size(total, 1, &alloc_size)) {
        return state->error("repeat result too large");
    }
    MobiusString* pending = alloc_pending_string(state, total);
    if (!pending) {
        return state->error("Memory allocation failed");
    }
    char* buf = pending->mutableData();
    for (int64_t i = 0; i < count; i++) {
        memcpy(buf + i * slen, str_val.as.string->data, slen);
    }
    buf[total] = '\0';

    MobiusString* result = finalize_string(state, pending, total);
    if (!result) {
        return state->error("String creation failed");
    }
    state->npush(make_string_value_adopt(result));
    return 1;
}