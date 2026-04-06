#include "library/util.h"
#include "data/value.h"
#include "state/mobius_state.h"
#include "util/file_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <chrono>

// =============================================================================
// UNIFIED UTILITY FUNCTION IMPLEMENTATIONS
// =============================================================================

int lib_random(MobiusState* state, int arg_count) {
    if (arg_count > 2) {
        return state->error("random expects 0, 1, or 2 arguments");
    }
    
    if (arg_count == 0) {
        // Return random float between 0 and 1
        state->npush(make_float_value((double)rand() / RAND_MAX));
        return 1;
    } else if (arg_count == 1) {
        // Return random integer between 0 and n-1
        Value arg = state->npeek(0);
        state->npop(); // Remove argument
        
        if (arg.type != VAL_INT64) {
            return state->error("random expects an integer argument");
        }
        int64_t max_val = arg.as.i64;
        if (max_val <= 0) {
            return state->error("random expects a positive integer");
        }
        state->npush(make_int64_value(rand() % max_val));
        return 1;
    } else if (arg_count == 2) {
        // Return random integer between min and max (inclusive)
        Value max_arg = state->npeek(0);
        Value min_arg = state->npeek(1);
        
        // Remove arguments
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
        
        int64_t range = max_val - min_val + 1;
        int64_t result = min_val + (rand() % range);
        state->npush(make_int64_value(result));
        return 1;
    }
    
    return state->error("random: unexpected argument count");
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
    
    // Check if file exists
    if (!file_exists(filename)) {
        return state->error("File does not exist");
    }
    
    // Read file content
    FileResult file_result = read_file(filename);
    if (!file_result.success) {
        return state->error("Failed to read file");
    }
    
    const char* saved_source = state->getSourceContext();
    state->setSourceContext(filename);
    int exec_result = state->execString(file_result.content);
    state->setSourceContext(saved_source);
    free_file_result(&file_result);

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
        srand((unsigned int)arg.as.i64);
    } else if (arg.type == VAL_FLOAT64) {
        srand((unsigned int)arg.as.double_val);
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