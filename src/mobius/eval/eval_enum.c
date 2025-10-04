#include "eval/evaluator.h"
#include "data/enum.h"
#include "state/mobius_state.h"


#include <stdio.h>
#include <string.h>

// ============================================================================
// ENUM EVALUATION IMPLEMENTATION
// ============================================================================

EvalResult eval_enum_stmt(EnumStmt* stmt, Environment* env) {
    if (!stmt) {
        return make_error(env, "Null enum statement", 0, 0);
    }
    
    
    // Create the enum definition
    const char* enum_name = stmt->name.identifier;
    if (!enum_name) {
        return make_error(env, "Invalid enum name", stmt->name.line, stmt->name.column);
    }
    
    // Check for namespace collision: a variable with the same name shouldn't exist
    bool variable_exists = false;
    get_variable(env, enum_name, &variable_exists);
    if (variable_exists) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Name collision: variable '%s' already exists, cannot declare enum with the same name", enum_name);
        return make_error(env, error_msg, stmt->name.line, stmt->name.column);
    }
    
    // Also check if an enum with this name already exists
    char check_enum_var_name[256];
    snprintf(check_enum_var_name, sizeof(check_enum_var_name), "__enum_%s", enum_name);
    bool enum_exists = false;
    get_variable(env, check_enum_var_name, &enum_exists);
    if (enum_exists) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Enum '%s' is already declared", enum_name);
        return make_error(env, error_msg, stmt->name.line, stmt->name.column);
    }
    
    EnumDefinition* enum_def = enum_definition_create(enum_name, stmt->underlying_type);
    if (!enum_def) {
        return make_error(env, "Failed to create enum definition", stmt->name.line, stmt->name.column);
    }
    
    // Process enum members
    EnumMemberDef* member = stmt->members;
    while (member) {
        const char* member_name = member->name.identifier;
        if (!member_name) {
            enum_definition_release(enum_def);
            return make_error(env, "Invalid enum member name", member->name.line, member->name.column);
        }
        
        // Evaluate member value if provided
        if (member->value) {
            EvalResult value_result = evaluate_expr(member->value, env);
            if (value_result.has_error) {
                enum_definition_release(enum_def);
                return value_result;
            }
            
            Value enum_value = ctx_pop(env->current_context);
            
            // Ensure the value is an integer
            if (enum_value.type != VAL_INTEGER) {
                free_value(enum_value);
                enum_definition_release(enum_def);
                return make_error(env, "Enum member value must be an integer", member->name.line, member->name.column);
            }
            
            // Extract integer value - use the unified approach since all are stored as int64_t
            int64_t int_value = 0;
            switch (enum_value.as.integer.num_type) {
                case NUM_INT8:  int_value = (int64_t)enum_value.as.integer.value.i8; break;
                case NUM_UINT8: int_value = (int64_t)enum_value.as.integer.value.u8; break;
                case NUM_INT16: int_value = (int64_t)enum_value.as.integer.value.i16; break;
                case NUM_UINT16: int_value = (int64_t)enum_value.as.integer.value.u16; break;
                case NUM_INT32: int_value = (int64_t)enum_value.as.integer.value.i32; break;
                case NUM_UINT32: int_value = (int64_t)enum_value.as.integer.value.u32; break;
                case NUM_INT64: int_value = enum_value.as.integer.value.i64; break;
                case NUM_UINT64: int_value = (int64_t)enum_value.as.integer.value.u64; break;
                default: 
                    free_value(enum_value);
                    enum_definition_release(enum_def);
                    return make_error(env, "Unsupported integer type for enum", member->name.line, member->name.column);
            }
            
            enum_definition_add_member(enum_def, member_name, int_value);
            free_value(enum_value);
        } else {
            // Auto-assign value
            enum_definition_add_auto_member(enum_def, member_name);
        }
        
        member = member->next;
    }
    
    // Store the enum definition in the environment as a special variable
    // We'll use a special naming convention: __enum_<name>
    char enum_var_name[256];
    snprintf(enum_var_name, sizeof(enum_var_name), "__enum_%s", enum_name);
    
    Value enum_value = make_userdata_value(enum_def, (UserdataDestructor)enum_definition_release, "enum_definition", sizeof(EnumDefinition));
    define_variable(env, enum_var_name, enum_value);
    

    ctx_push(env->current_context, make_nil_value());
    return make_success(1);
}

EvalResult eval_enum_access_expr(EnumAccessExpr* expr, Environment* env) {
    if (!expr) {
        return make_error(env, "Null enum access expression", 0, 0);
    }
    
    const char* enum_name = expr->enum_name.identifier;
    const char* member_name = expr->member_name.identifier;
    
    if (!enum_name || !member_name) {
        return make_error(env, "Invalid enum access", expr->enum_name.line, expr->enum_name.column);
    }
    
    // Look up the enum definition in the environment
    char enum_var_name[256];
    snprintf(enum_var_name, sizeof(enum_var_name), "__enum_%s", enum_name);
    
    bool found = false;
    Value enum_value = get_variable(env, enum_var_name, &found);
    if (!found || enum_value.type == VAL_NIL) {
        // Fallback: check if it's a regular variable (for enum values stored as variables)
        Value member_var = get_variable(env, member_name, &found);
        if (found && member_var.type != VAL_NIL) {
            ctx_push(env->current_context, copy_value(member_var));
            return make_success(1);
        }
        
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Undefined enum '%s'", enum_name);
        return make_error(env, error_msg, expr->enum_name.line, expr->enum_name.column);
    }
    
    if (enum_value.type != VAL_USERDATA || 
        strcmp(enum_value.as.userdata.type_name, "enum_definition") != 0) {
        return make_error(env, "Invalid enum definition", expr->enum_name.line, expr->enum_name.column);
    }
    
    EnumDefinition* enum_def = (EnumDefinition*)enum_value.as.userdata.ptr;
    if (!enum_def) {
        return make_error(env, "Null enum definition", expr->enum_name.line, expr->enum_name.column);
    }
    
    // Find the enum member
    EnumMember* member = enum_definition_find_member(enum_def, member_name);
    if (!member) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Undefined enum member '%s.%s'", enum_name, member_name);
        return make_error(env, error_msg, expr->member_name.line, expr->member_name.column);
    }
    
    // Create enum value
    Value result = make_enum_value(enum_def, member->value);
    ctx_push(env->current_context, result);
    return make_success(1);
}