#include "bytecode_types.h"
#include "bytecode.h"
#include "value.h"
#include "types.h"
#include "table.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

// =============================================================================
// TYPE SYSTEM BUILTIN FUNCTIONS FOR BYTECODE VM
// =============================================================================


// Type system functions
void builtin_set_strict_types_vm(MobiusVM* vm, int arg_count, void* result_ptr) {
    Value* result = (Value*)result_ptr;
    extern TypeCheckConfig global_type_config;
    
    if (arg_count > 1) {
        vm_runtime_error(vm, "set_strict_types expects 0 or 1 arguments");
        *result = make_nil_value();
        return;
    }
    
    bool strict = true; // Default to strict if no argument
    if (arg_count == 1) {
        Value arg = vm_peek(vm, 0);
        if (arg.type != VAL_BOOL) {
            vm_runtime_error(vm, "set_strict_types argument must be a boolean");
            *result = make_nil_value();
            return;
        }
        strict = arg.as.boolean;
    }
    
    global_type_config.strict_mode = strict;
    *result = make_nil_value();
}

void builtin_set_type_warnings_vm(MobiusVM* vm, int arg_count, void* result_ptr) {
    Value* result = (Value*)result_ptr;
    extern TypeCheckConfig global_type_config;
    
    if (arg_count != 1) {
        vm_runtime_error(vm, "set_type_warnings expects 1 argument");
        *result = make_nil_value();
        return;
    }
    
    Value arg = vm_peek(vm, 0);
    if (arg.type != VAL_BOOL) {
        vm_runtime_error(vm, "set_type_warnings argument must be a boolean");
        *result = make_nil_value();
        return;
    }
    
    global_type_config.warn_on_conversion = arg.as.boolean;
    *result = make_nil_value();
}

void builtin_get_type_config_vm(MobiusVM* vm, int arg_count, void* result_ptr) {
    Value* result = (Value*)result_ptr;
    extern TypeCheckConfig global_type_config;
    
    if (arg_count != 0) {
        vm_runtime_error(vm, "get_type_config expects no arguments");
        *result = make_nil_value();
        return;
    }
    
    // Return a table with configuration
    Table* config_table = create_table(8);
    if (!config_table) {
        vm_runtime_error(vm, "Failed to create config table");
        *result = make_nil_value();
        return;
    }
    
    // Add strict_mode key-value pair
    Value strict_key = make_string_value_from_cstr("strict_mode");
    Value strict_value = make_bool_value(global_type_config.strict_mode);
    table_set(config_table, strict_key, strict_value);
    
    // Add warn_on_conversion key-value pair
    Value warn_key = make_string_value_from_cstr("warn_on_conversion");
    Value warn_value = make_bool_value(global_type_config.warn_on_conversion);
    table_set(config_table, warn_key, warn_value);
    
    *result = make_table_value(config_table);
}