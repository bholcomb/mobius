#include "library/util.h"
#include "data/value.h"
#include "state/mobius_state.h"
#include "util/file_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <chrono>
#include <string>

// =============================================================================
// UNIFIED UTILITY FUNCTION IMPLEMENTATIONS
// =============================================================================

// A uniform integer in [0, range), without modulo bias. range 0 means the
// full 2^64.
static uint64_t random_below(MobiusState* state, uint64_t range) {
    if (range == 0) return state->nextRandom();
    uint64_t limit = (0 - range) % range;   // reject the short first chunk
    while (true) {
        uint64_t x = state->nextRandom();
        if (x >= limit) return x % range;
    }
}

int lib_random(MobiusState* state, int arg_count) {
    if (arg_count > 2) {
        return state->error("random expects 0, 1, or 2 arguments");
    }

    if (arg_count == 0) {
        // A float in [0, 1): the top 53 bits.
        state->npush(make_float_value((double)(state->nextRandom() >> 11) * 0x1.0p-53));
        return 1;
    } else if (arg_count == 1) {
        // An integer in [0, n-1]
        Value arg = state->npeek(0);
        state->npop();
        if (arg.type != VAL_INT64) {
            return state->error("random expects an integer argument");
        }
        int64_t max_val = arg.as.i64;
        if (max_val <= 0) {
            return state->error("random expects a positive integer");
        }
        state->npush(make_int64_value((int64_t)random_below(state, (uint64_t)max_val)));
        return 1;
    } else {
        // An integer in [min, max] (inclusive)
        Value max_arg = state->npeek(0);
        Value min_arg = state->npeek(1);
        state->npop();
        state->npop();
        if (min_arg.type != VAL_INT64 || max_arg.type != VAL_INT64) {
            return state->error("random expects integer arguments");
        }
        int64_t min_val = min_arg.as.i64;
        int64_t max_val = max_arg.as.i64;
        if (min_val > max_val) {
            return state->error("random min value must be <= max value");
        }
        // Unsigned arithmetic: the span of the whole int64 range wraps to 0.
        uint64_t range = (uint64_t)max_val - (uint64_t)min_val + 1;
        uint64_t offset = random_below(state, range);
        state->npush(make_int64_value((int64_t)((uint64_t)min_val + offset)));
        return 1;
    }
}

int lib_clock(MobiusState* state, int arg_count) {
    if (arg_count != 0) {
        return state->error("clock expects no arguments");
    }
    auto now = std::chrono::steady_clock::now();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
    state->npush(make_int64_value((int64_t)ns));
    return 1;
}

int lib_load(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("load expects exactly 1 argument (filename)");
    }
    
    Value filename_val = state->npeek(0);
    state->npop(); // Remove argument
    
    if (filename_val.type != VAL_STRING) {
        return state->error("load argument must be a string");
    }
    
    const char* filename = filename_val.as.string->data;
    if (!filename || filename_val.as.string->length == 0) {
        return state->error("load filename cannot be empty");
    }
    
    // Through the host's file system, the real one, or not available.
    std::string err;
    int exists = state->fileExists(filename, err);
    if (exists < 0) return state->error(("load: " + err).c_str());
    if (exists == 0) {
        return state->error("File does not exist");
    }
    std::string content;
    if (!state->readFile(filename, content, err)) {
        return state->error("Failed to read file");
    }

    const char* saved_source = state->getSourceContext();
    state->setSourceContext(filename);
    int exec_result = state->execString(content.c_str());
    state->setSourceContext(saved_source);

    if (exec_result != MOBIUS_OK) {
        return state->error("error in loaded script");
    }

    state->npush(make_bool_value(true));
    return 1;
}

int lib_randomseed(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("randomseed expects exactly 1 argument");
    }
    Value arg = state->npeek(0);
    state->npop();
    if (arg.type == VAL_INT64) {
        state->seedRandom((uint64_t)arg.as.i64);
    } else if (arg.type == VAL_FLOAT64) {
        state->seedRandom((uint64_t)(int64_t)arg.as.double_val);
    } else {
        return state->error("randomseed expects a numeric argument");
    }
    return 0;
}

int lib_isnan(MobiusState* state, int arg_count) {
    if (arg_count != 1) return state->error("isnan expects 1 argument");
    Value arg = state->npeek(0);
    state->npop();
    bool result = false;
    if (arg.type == VAL_FLOAT64) result = isnan(arg.as.double_val);
    state->npush(make_bool_value(result));
    return 1;
}

int lib_isinf(MobiusState* state, int arg_count) {
    if (arg_count != 1) return state->error("isinf expects 1 argument");
    Value arg = state->npeek(0);
    state->npop();
    bool result = false;
    if (arg.type == VAL_FLOAT64) result = isinf(arg.as.double_val);
    state->npush(make_bool_value(result));
    return 1;
}

int lib_isfinite(MobiusState* state, int arg_count) {
    if (arg_count != 1) return state->error("isfinite expects 1 argument");
    Value arg = state->npeek(0);
    state->npop();
    bool result = true;
    if (arg.type == VAL_FLOAT64) result = isfinite(arg.as.double_val);
    state->npush(make_bool_value(result));
    return 1;
}

int lib_id(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("id() expects exactly 1 argument");
    }
    
    Value arg = state->npeek(0);
    state->npop();
    
    uintptr_t addr = 0;
    switch (arg.type) {
        case VAL_TABLE:
            addr = (uintptr_t)arg.as.table;
            break;
        case VAL_ARRAY:
            addr = (uintptr_t)arg.as.array;
            break;
        case VAL_FUNCTION:
            addr = (uintptr_t)arg.as.function;
            break;
        case VAL_USERDATA:
            addr = arg.as.userdata ? (uintptr_t)arg.as.userdata->ptr : 0;
            break;
        default:
            // For value types, just return 0
            addr = 0;
            break;
    }
    
    state->npush(make_int64_value((int64_t)addr));
    return 1;
}

int lib_time(MobiusState* state, int arg_count) {
    if (arg_count != 0) {
        return state->error("time expects no arguments");
    }
    auto now = std::chrono::system_clock::now();
    auto secs = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    state->npush(make_int64_value((int64_t)secs));
    return 1;
}