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

EvalResult lib_random(ExecutionContext* ctx, int arg_count) {
    if (arg_count > 2) {
        return make_error("random expects 0, 1, or 2 arguments", 0, 0);
    }
    
    if (arg_count == 0) {
        // Return random float between 0 and 1
        ctx_push(ctx, make_float_value((double)rand() / RAND_MAX));
        return make_success(1);
    } else if (arg_count == 1) {
        // Return random integer between 0 and n-1
        Value arg = ctx_peek(ctx, 0);
        ctx_pop(ctx); // Remove argument
        
        if (arg.type != VAL_INTEGER) {
            return make_error("random expects an integer argument", 0, 0);
        }
        int64_t max_val = arg.as.integer.value.i64;
        if (max_val <= 0) {
            return make_error("random expects a positive integer", 0, 0);
        }
        ctx_push(ctx, make_integer_value(NUM_INT64, rand() % max_val));
        return make_success(1);
    } else if (arg_count == 2) {
        // Return random integer between min and max (inclusive)
        Value max_arg = ctx_peek(ctx, 0);
        Value min_arg = ctx_peek(ctx, 1);
        
        // Remove arguments
        ctx_pop(ctx);
        ctx_pop(ctx);
        
        if (min_arg.type != VAL_INTEGER || max_arg.type != VAL_INTEGER) {
            return make_error("random expects integer arguments", 0, 0);
        }
        
        int64_t min_val = min_arg.as.integer.value.i64;
        int64_t max_val = max_arg.as.integer.value.i64;
        
        if (min_val > max_val) {
            return make_error("random min value must be <= max value", 0, 0);
        }
        
        int64_t range = max_val - min_val + 1;
        int64_t result = min_val + (rand() % range);
        ctx_push(ctx, make_integer_value(NUM_INT64, result));
        return make_success(1);
    }
    
    return make_error("random: unexpected argument count", 0, 0);
}

EvalResult lib_time(ExecutionContext* ctx, int arg_count) {
    if (arg_count != 0) {
        return make_error("time expects no arguments", 0, 0);
    }
    
    ctx_push(ctx, make_integer_value(NUM_INT64, (int64_t)time(NULL)));
    return make_success(1);
}

EvalResult lib_clock(ExecutionContext* ctx, int arg_count) {
    if (arg_count != 0) {
        return make_error("clock expects no arguments", 0, 0);
    }
    
    ctx_push(ctx, make_float_value((double)clock() / CLOCKS_PER_SEC));
    return make_success(1);
}

EvalResult lib_load(ExecutionContext* ctx, int arg_count) {
    if (arg_count != 1) {
        return make_error("load expects exactly 1 argument (filename)", 0, 0);
    }
    
    Value filename_val = ctx_peek(ctx, 0);
    ctx_pop(ctx); // Remove argument
    
    if (filename_val.type != VAL_STRING) {
        return make_error("load argument must be a string", 0, 0);
    }
    
    const char* filename = string_data(filename_val.as.string);
    if (!filename || string_length(filename_val.as.string) == 0) {
        return make_error("load filename cannot be empty", 0, 0);
    }
    
    // Check if file exists
    if (!file_exists(filename)) {
        return make_error("File does not exist", 0, 0);
    }
    
    // Read file content
    FileResult file_result = read_file(filename);
    if (!file_result.success) {
        return make_error("Failed to read file", 0, 0);
    }
    
    // Parse and execute the loaded script in the current environment
    TokenArray tokens = scan_source(file_result.content);
    if (tokens.count == 0) {
        free_file_result(&file_result);
        return make_error("No code found in file", 0, 0);
    }
    
    // Parse AST
    ParseResult parse_result = parse(tokens);
    free_token_array(&tokens);
    
    if (parse_result.had_error) {
        free_file_result(&file_result);
        free_parse_result(&parse_result);
        return make_error("Parse error in loaded file", 0, 0);
    }
    
    // Set source context for better error reporting
    const char* old_context = get_source_context();
    set_source_context(file_result.content);
    
    // Execute the loaded script in the current environment
    // This allows loaded functions and variables to be available in the calling script
    EvalResult eval_result = evaluate_program(parse_result.statements, parse_result.count, ctx->env);
    
    // Restore previous source context
    set_source_context(old_context);
    
    // Note: We intentionally don't free the memory immediately to prevent issues
    // with function definitions that reference the AST nodes
    // This creates a small memory leak but ensures stability
    
    if (is_error(eval_result)) {
        return eval_result; // Propagate error from loaded script
    }
    
    // Return true to indicate successful loading
    ctx_push(ctx, make_bool_value(true));
    return make_success(1);
}

// Get identity (memory address) of a value - useful for comparing table/array references
EvalResult lib_id(ExecutionContext* ctx, int arg_count) {
    if (arg_count != 1) {
        return make_error("id() expects exactly 1 argument", 0, 0);
    }
    
    Value arg = ctx_peek(ctx, 0);
    ctx_pop(ctx);
    
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
    
    free_value(arg);
    ctx_push(ctx, make_integer_value(NUM_INT64, (int64_t)addr));
    return make_success(1);
}