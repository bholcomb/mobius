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

// Every function goes through the state's file access: the host's file
// system (mobius_set_file_system), the real one, or - in a sandbox that
// doesn't allow it - a "file access is not available" error.

int lib_readfile(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("readfile expects 1 argument (path)");
    }
    Value path_val = state->npop();
    if (path_val.type != VAL_STRING || !path_val.as.string) {
        return state->error("readfile argument must be a string");
    }

    std::string data, err;
    if (!state->readFile(path_val.as.string->data, data, err)) {
        return state->error(("readfile: " + err).c_str());
    }

    // A string of the data's length: going through a C string cut the
    // contents off at the first NUL byte.
    MobiusString* str = StringInternPool::allocHeap(data.size());
    if (!str) return state->error("readfile: memory allocation failed");
    if (!data.empty()) memcpy(str->mutableData(), data.data(), data.size());
    StringInternPool::finishHeap(str, data.size());

    state->npush(make_string_value_adopt(str));
    return 1;
}

// writefile/appendfile: true when written, false on a write error; an error
// when the file can't be opened (or file access is not available).
static int write_or_append(MobiusState* state, int arg_count, bool append) {
    const char* name = append ? "appendfile" : "writefile";
    if (arg_count != 2) {
        return state->error((std::string(name) + " expects 2 arguments (path, content)").c_str());
    }
    Value content_val = state->npop();
    Value path_val = state->npop();

    if (path_val.type != VAL_STRING || !path_val.as.string) {
        return state->error((std::string(name) + ": path must be a string").c_str());
    }
    if (content_val.type != VAL_STRING || !content_val.as.string) {
        return state->error((std::string(name) + ": content must be a string").c_str());
    }

    std::string err;
    bool ok = state->writeFile(path_val.as.string->data, content_val.as.string->data,
                               content_val.as.string->length, append, err);
    if (!ok && err != "write error") {
        return state->error((std::string(name) + ": " + err).c_str());
    }
    state->npush(make_bool_value(ok));
    return 1;
}

int lib_writefile(MobiusState* state, int arg_count) {
    return write_or_append(state, arg_count, false);
}

int lib_appendfile(MobiusState* state, int arg_count) {
    return write_or_append(state, arg_count, true);
}

int lib_file_exists(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("file_exists expects 1 argument (path)");
    }
    Value path_val = state->npop();
    if (path_val.type != VAL_STRING || !path_val.as.string) {
        return state->error("file_exists: argument must be a string");
    }

    std::string err;
    int exists = state->fileExists(path_val.as.string->data, err);
    if (exists < 0) return state->error(("file_exists: " + err).c_str());
    state->npush(make_bool_value(exists == 1));
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

    std::string data, err;
    if (!state->readFile(path_val.as.string->data, data, err)) {
        return state->error(("readlines: " + err).c_str());
    }

    ArrayValue* arr = new (std::nothrow) ArrayValue(state->gcHeap());
    if (!arr) return state->error("readlines: failed to allocate result array");
    size_t start = 0;
    while (start < data.size()) {
        size_t nl = data.find('\n', start);
        size_t end = nl == std::string::npos ? data.size() : nl;
        size_t len = end - start;
        if (len > 0 && data[start + len - 1] == '\r') len--;
        arr->push(make_heap_string_value(data.data() + start, len));
        if (nl == std::string::npos) break;
        start = nl + 1;
    }

    state->npush(make_array_value(arr));
    return 1;
}
