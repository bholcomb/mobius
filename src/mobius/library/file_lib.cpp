#include "library/file_lib.h"
#include "data/value.h"
#include "data/array.h"
#include "state/mobius_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

int lib_readfile(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("readfile expects 1 argument (path)");
    }
    Value path_val = state->npop();
    if (path_val.type != VAL_STRING || !path_val.as.string) {
        return state->error("readfile argument must be a string");
    }

    FILE* f = fopen(path_val.as.string->data, "r");
    if (!f) {
        return state->error("readfile: could not open file");
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz < 0) { fclose(f); return state->error("readfile: could not determine file size"); }

    char* buf = (char*)malloc(sz + 1);
    if (!buf) { fclose(f); return state->error("readfile: memory allocation failed"); }

    size_t nread = fread(buf, 1, sz, f);
    fclose(f);
    buf[nread] = '\0';

    Value result = make_string_value_from_cstr(state, buf);
    free(buf);
    state->npush(result);
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

    ArrayValue* arr = new ArrayValue();
    char line_buf[4096];
    while (fgets(line_buf, sizeof(line_buf), f)) {
        size_t len = strlen(line_buf);
        if (len > 0 && line_buf[len - 1] == '\n') {
            line_buf[len - 1] = '\0';
            len--;
        }
        if (len > 0 && line_buf[len - 1] == '\r') {
            line_buf[len - 1] = '\0';
        }
        arr->push(make_string_value_from_cstr(state, line_buf));
    }
    fclose(f);

    state->npush(make_array_value(arr));
    return 1;
}
