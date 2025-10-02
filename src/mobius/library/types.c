#include "types.h"
#include "../value.h"
#include "../environment.h"
#include "../types.h"
#include "../table.h"
#include <stdio.h>
#include <stdlib.h>

// =============================================================================
// UNIFIED TYPE SYSTEM FUNCTION IMPLEMENTATIONS
// =============================================================================

EvalResult lib_set_strict_types(ExecutionContext* ctx, int arg_count) {
    extern TypeCheckConfig global_type_config;
    
    if (arg_count > 1) {
        return make_error("set_strict_types expects 0 or 1 arguments", 0, 0);
    }
    
    bool strict = true; // Default to strict if no argument
    if (arg_count == 1) {
        Value arg = ctx_peek(ctx, 0);
        ctx_pop(ctx); // Remove argument
        
        if (arg.type != VAL_BOOL) {
            return make_error("set_strict_types argument must be a boolean", 0, 0);
        }
        strict = arg.as.boolean;
    }
    
    global_type_config.strict_mode = strict;
    ctx_push(ctx, make_nil_value());
    return make_success(1);
}

EvalResult lib_set_type_warnings(ExecutionContext* ctx, int arg_count) {
    extern TypeCheckConfig global_type_config;
    
    if (arg_count != 1) {
        return make_error("set_type_warnings expects exactly 1 argument", 0, 0);
    }
    
    Value arg = ctx_peek(ctx, 0);
    ctx_pop(ctx); // Remove argument
    
    if (arg.type != VAL_BOOL) {
        return make_error("set_type_warnings argument must be a boolean", 0, 0);
    }
    
    global_type_config.warn_on_conversion = arg.as.boolean;
    ctx_push(ctx, make_nil_value());
    return make_success(1);
}

EvalResult lib_get_type_config(ExecutionContext* ctx, int arg_count) {
    extern TypeCheckConfig global_type_config;
    
    if (arg_count != 0) {
        return make_error("get_type_config expects no arguments", 0, 0);
    }
    
    // Return a table with configuration
    Table* config_table = create_table(8);
    if (!config_table) {
        return make_error("Failed to create config table", 0, 0);
    }
    
    // Add strict_mode key-value pair
    Value strict_key = make_string_value_from_cstr("strict_mode");
    Value strict_value = make_bool_value(global_type_config.strict_mode);
    table_set(config_table, strict_key, strict_value);
    
    // Add warn_on_conversion key-value pair
    Value warn_key = make_string_value_from_cstr("warn_on_conversion");
    Value warn_value = make_bool_value(global_type_config.warn_on_conversion);
    table_set(config_table, warn_key, warn_value);
    
    ctx_push(ctx, make_table_value(config_table));
    return make_success(1);
}