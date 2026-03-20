#include "library/util.h"
#include "data/value.h"
#include "state/environment.h"
#include "util/file_io.h"
#include "frontend/scanner.h"
#include "frontend/parser.h"
#include "eval/evaluator.h"
#include "frontend/parser.h"
#include "eval/evaluator.h"

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

// =============================================================================
// UNIFIED UTILITY FUNCTION IMPLEMENTATIONS
// =============================================================================

int lib_random(MobiusState* state, int arg_count) {
    if (arg_count > 2) {
        return state->error("random expects 0, 1, or 2 arguments");
    }
    
    if (arg_count == 0) {
        // Return random float between 0 and 1
        state->mainContext()->push( make_float_value((double)rand() / RAND_MAX));
        return 1;
    } else if (arg_count == 1) {
        // Return random integer between 0 and n-1
        Value arg = state->mainContext()->peek( 0);
        state->mainContext()->pop(); // Remove argument
        
        if (arg.type != VAL_INTEGER) {
            return state->error("random expects an integer argument");
        }
        int64_t max_val = arg.as.integer.value.i64;
        if (max_val <= 0) {
            return state->error("random expects a positive integer");
        }
        state->mainContext()->push( make_integer_value(NUM_INT64, rand() % max_val));
        return 1;
    } else if (arg_count == 2) {
        // Return random integer between min and max (inclusive)
        Value max_arg = state->mainContext()->peek( 0);
        Value min_arg = state->mainContext()->peek( 1);
        
        // Remove arguments
        state->mainContext()->pop();
        state->mainContext()->pop();
        
        if (min_arg.type != VAL_INTEGER || max_arg.type != VAL_INTEGER) {
            return state->error("random expects integer arguments");
        }
        
        int64_t min_val = min_arg.as.integer.value.i64;
        int64_t max_val = max_arg.as.integer.value.i64;
        
        if (min_val > max_val) {
            return state->error("random min value must be <= max value");
        }
        
        int64_t range = max_val - min_val + 1;
        int64_t result = min_val + (rand() % range);
        state->mainContext()->push( make_integer_value(NUM_INT64, result));
        return 1;
    }
    
    return state->error("random: unexpected argument count");
}

int lib_time(MobiusState* state, int arg_count) {
    if (arg_count != 0) {
        return state->error("time expects no arguments");
    }
    
    state->mainContext()->push( make_integer_value(NUM_INT64, (int64_t)time(NULL)));
    return 1;
}

int lib_clock(MobiusState* state, int arg_count) {
    if (arg_count != 0) {
        return state->error("clock expects no arguments");
    }
    
    state->mainContext()->push( make_float_value((double)clock() / CLOCKS_PER_SEC));
    return 1;
}

int lib_load(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("load expects exactly 1 argument (filename)");
    }
    
    Value filename_val = state->mainContext()->peek( 0);
    state->mainContext()->pop(); // Remove argument
    
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
    
    // Parse and execute the loaded script in the current environment
    TokenArray tokens = scan_source(file_result.content);
    if (tokens.count == 0) {
        free_file_result(&file_result);
        return state->error("No code found in file");
    }
    
    // Parse AST
    ParseResult parse_result = parse(state, tokens);
    free_token_array(&tokens);
    
    if (parse_result.had_error) {
        free_file_result(&file_result);
        free_parse_result(&parse_result);
        return state->error("Parse error in loaded file");
    }
    
    // Set source context for better error reporting
    const char* old_context = state->getSourceContext();
    state->setSourceContext(file_result.content);
    
    // Execute the loaded script in the current environment
    // This allows loaded functions and variables to be available in the calling script
    EvalResult eval_result = evaluate_program(parse_result.statements, parse_result.count, state->globalEnv());
    
    // Restore previous source context
    state->setSourceContext(old_context);
    
    // Note: We intentionally don't free the memory immediately to prevent issues
    // with function definitions that reference the AST nodes
    // This creates a small memory leak but ensures stability
    
    if (is_error(eval_result)) {
        const char* msg = eval_result.error.message ? eval_result.error.message : "error in loaded script";
        return state->error(msg);
    }
    
    state->mainContext()->push( make_bool_value(true));
    return 1;
}

// Get identity (memory address) of a value - useful for comparing table/array references
int lib_id(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("id() expects exactly 1 argument");
    }
    
    Value arg = state->mainContext()->peek( 0);
    state->mainContext()->pop();
    
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
            addr = (uintptr_t)arg.as.userdata.ptr;
            break;
        default:
            // For value types, just return 0
            addr = 0;
            break;
    }
    
    state->mainContext()->push( make_integer_value(NUM_INT64, (int64_t)addr));
    return 1;
}