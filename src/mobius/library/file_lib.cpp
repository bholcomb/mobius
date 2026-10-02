#include "library/file_lib.h"
#include "data/value.h"
#include "internal/string_intern.h"
#include "data/array.h"
#include "state/mobius_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <string>

int lib_readfile(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("readfile expects 1 argument (path)");
    }
    Value path_val = state->npop();
    if (path_val.type != VAL_STRING || !path_val.as.string) {
        return state->error("readfile argument must be a string");
    }

    FILE* f = fopen(path_val.as.string->data, "rb");
    if (!f) {
        return state->error("readfile: could not open file");
    }

    // Read until end of file rather than trusting the reported size:
    // files under /proc and pipes report 0 and read back empty otherwise.
    // The size is only a first guess at the buffer.
    std::string data;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz > 0) data.reserve((size_t)sz);
    char chunk[65536];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) data.append(chunk, n);
    bool failed = ferror(f) != 0;
    fclose(f);
    if (failed) return state->error("readfile: read error");

    // A string of the data's length: going through a C string cut the
    // contents off at the first NUL byte.
    MobiusString* str = StringInternPool::allocHeap(data.size());
    if (!str) return state->error("readfile: memory allocation failed");
    if (!data.empty()) memcpy(str->mutableData(), data.data(), data.size());
    StringInternPool::finishHeap(str, data.size());

    state->npush(make_string_value_adopt(str));
    return 1;
}

int lib_writefile(MobiusState* state, int arg_count) {
    if (arg_count != 2) {
        return state->error("writefile expects 2 arguments (path, content)");
    }
    Value content_val = state->npop();
    Value path_val = state->npop();

    if (path_val.type != VAL_STRING || !path_val.as.string) {
        return state->error("writefile: path must be a string");
    }
    if (content_val.type != VAL_STRING || !content_val.as.string) {
        return state->error("writefile: content must be a string");
    }

    FILE* f = fopen(path_val.as.string->data, "w");
    if (!f) {
        return state->error("writefile: could not open file for writing");
    }

    size_t len = content_val.as.string->length;
    size_t written = fwrite(content_val.as.string->data, 1, len, f);
    fclose(f);

    state->npush(make_bool_value(written == len));
    return 1;
}

int lib_appendfile(MobiusState* state, int arg_count) {
    if (arg_count != 2) {
        return state->error("appendfile expects 2 arguments (path, content)");
    }
    Value content_val = state->npop();
    Value path_val = state->npop();

    if (path_val.type != VAL_STRING || !path_val.as.string) {
        return state->error("appendfile: path must be a string");
    }
    if (content_val.type != VAL_STRING || !content_val.as.string) {
        return state->error("appendfile: content must be a string");
    }

    FILE* f = fopen(path_val.as.string->data, "a");
    if (!f) {
        return state->error("appendfile: could not open file for appending");
    }

    size_t len = content_val.as.string->length;
    size_t written = fwrite(content_val.as.string->data, 1, len, f);
    fclose(f);

    state->npush(make_bool_value(written == len));
    return 1;
}

int lib_file_exists(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("file_exists expects 1 argument (path)");
    }
    Value path_val = state->npop();
    if (path_val.type != VAL_STRING || !path_val.as.string) {
        return state->error("file_exists: argument must be a string");
    }

    struct stat st;
    bool exists = (stat(path_val.as.string->data, &st) == 0);
    state->npush(make_bool_value(exists));
    return 1;
}

int lib_readlines(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("readlines expects 1 argument (path)");
    }
    Value path_val = state->npop();
    if (path_val.type != VAL_STRING || !path_val.as.string) {
        return state->error("readlines: argument must be a string");
    }

    FILE* f = fopen(path_val.as.string->data, "r");
    if (!f) {
        return state->error("readlines: could not open file");
    }

    ArrayValue* arr = new (std::nothrow) ArrayValue();
    if (!arr) {
        fclose(f);
        return state->error("readlines: failed to allocate result array");
    }
    char line_buf[4096];
    std::string current_line;
    while (fgets(line_buf, sizeof(line_buf), f)) {
        size_t len = strlen(line_buf);
        bool has_newline = len > 0 && line_buf[len - 1] == '\n';
        if (has_newline) {
            line_buf[--len] = '\0';
        }
        if (len > 0 && line_buf[len - 1] == '\r') {
            line_buf[--len] = '\0';
        }
        current_line.append(line_buf, len);
        if (has_newline) {
            arr->push(make_string_value_from_cstr(state, current_line.c_str()));
            current_line.clear();
        }
    }
    if (ferror(f)) {
        fclose(f);
        arr->release();
        return state->error("readlines: error while reading file");
    }
    if (!current_line.empty()) {
        arr->push(make_string_value_from_cstr(state, current_line.c_str()));
    }
    fclose(f);

    state->npush(make_array_value(arr));
    return 1;
}
