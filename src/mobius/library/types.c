#include "library/types.h"
#include "data/value.h"
#include "state/environment.h"
#include "data/table.h"
#include "eval/evaluator.h"

#include <stdio.h>
#include <stdlib.h>

// =============================================================================
// UNIFIED TYPE SYSTEM FUNCTION IMPLEMENTATIONS
// =============================================================================
// Note: set_strict_types() and set_type_warnings() have been removed.
// Use #pragma strict_types true/false instead.

EvalResult lib_get_type_config(MobiusState* state, int arg_count) {
    extern TypeCheckConfig global_type_config;
    
    if (arg_count != 0) {
        return make_error(state->main_context->current_env, "get_type_config expects no arguments", 0, 0);
    }
    
    // Return a table with configuration
    Table* config_table = create_table(8);
    if (!config_table) {
        return make_error(state->main_context->current_env, "Failed to create config table", 0, 0);
    }
    
    // Add strict_mode key-value pair
    Value strict_key = make_string_value_from_cstr("strict_mode");
    Value strict_value = make_bool_value(global_type_config.strict_mode);
    table_set(config_table, strict_key, strict_value);
    
    // Add warn_on_conversion key-value pair
    Value warn_key = make_string_value_from_cstr("warn_on_conversion");
    Value warn_value = make_bool_value(global_type_config.warn_on_conversion);
    table_set(config_table, warn_key, warn_value);
    
    ctx_push(state->main_context, make_table_value(config_table));
    return make_success(1);
}