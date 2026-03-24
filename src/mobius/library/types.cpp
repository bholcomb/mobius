#include "library/types.h"
#include "data/value.h"
#include "state/environment.h"
#include "data/table.h"

#include <cstdio>
#include <cstdlib>
#include <new>

// =============================================================================
// UNIFIED TYPE SYSTEM FUNCTION IMPLEMENTATIONS
// =============================================================================
// Note: set_strict_types() and set_type_warnings() have been removed.
// Use #pragma strict_types true/false instead.

int lib_get_type_config(MobiusState* state, int arg_count) {
    if (arg_count != 0) {
        return state->error("get_type_config expects no arguments");
    }
    
    // Return a table with configuration
    Table* config_table = new (std::nothrow) Table(state, 8);
    if (!config_table) {
        return state->error("Failed to create config table");
    }
    
    // Add strict_mode key-value pair
    Value strict_key = make_string_value_from_cstr(state, "strict_mode");
    Value strict_value = make_bool_value(state->config().strict_mode);
    config_table->set(strict_key, strict_value);
    
    // Add warn_on_conversion key-value pair
    Value warn_key = make_string_value_from_cstr(state, "warn_on_conversion");
    Value warn_value = make_bool_value(state->config().warn_on_conversion);
    config_table->set(warn_key, warn_value);
    
    state->npush(make_table_value(config_table));
    return 1;
}