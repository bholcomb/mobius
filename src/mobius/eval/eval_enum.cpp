#include "eval/evaluator.h"
#include "data/enum.h"
#include "state/mobius_state.h"

#include <stdio.h>
#include <string.h>

// Destructor adapter for storing EnumDefinition as userdata
static void enum_definition_destructor(void* ptr) {
    if (ptr) static_cast<EnumDefinition*>(ptr)->release();
}

// ============================================================================
// ENUM EVALUATION IMPLEMENTATION
// ============================================================================

EvalResult eval_enum_stmt(EnumStmt* stmt, Environment* env) {
    if (!stmt) {
        return make_error(env, "Null enum statement", 0, 0);
    }
    
    const char* enum_name = stmt->name.identifier;
    if (!enum_name) {
        return make_error(env, "Invalid enum name", stmt->name.line, stmt->name.column);
    }
    
    StringInternPool* pool = env->current_context->state->stringPool();
    
    bool variable_exists = false;
    env->get(enum_name, &variable_exists);
    if (variable_exists) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Name collision: variable '%s' already exists, cannot declare enum with the same name", enum_name);
        return make_error(env, error_msg, stmt->name.line, stmt->name.column);
    }
    
    char check_buf[256];
    snprintf(check_buf, sizeof(check_buf), "__enum_%s", enum_name);
    const char* check_key = pool->intern(check_buf)->data;
    bool enum_exists = false;
    env->get(check_key, &enum_exists);
    if (enum_exists) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Enum '%s' is already declared", enum_name);
        return make_error(env, error_msg, stmt->name.line, stmt->name.column);
    }
    
    EnumDefinition* enum_def = new (std::nothrow) EnumDefinition(enum_name, stmt->underlying_type);
    if (!enum_def) {
        return make_error(env, "Failed to create enum definition", stmt->name.line, stmt->name.column);
    }
    
    EnumMemberDef* member = stmt->members;
    while (member) {
        const char* member_name = member->name.identifier;
        if (!member_name) {
            enum_def->release();
            return make_error(env, "Invalid enum member name", member->name.line, member->name.column);
        }
        
        if (member->value) {
            EvalResult value_result = evaluate_expr(member->value, env);
            if (value_result.has_error) {
                enum_def->release();
                return value_result;
            }
            
            Value enum_value = env->current_context->pop();
            
            if (enum_value.type != VAL_INTEGER) {
                enum_def->release();
                return make_error(env, "Enum member value must be an integer", member->name.line, member->name.column);
            }
            
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
                    enum_def->release();
                    return make_error(env, "Unsupported integer type for enum", member->name.line, member->name.column);
            }
            
            enum_def->addMember(member_name, int_value);
        } else {
            enum_def->addAutoMember(member_name);
        }
        
        member = member->next;
    }
    
    char enum_buf[256];
    snprintf(enum_buf, sizeof(enum_buf), "__enum_%s", enum_name);
    const char* enum_key = pool->intern(enum_buf)->data;
    
    Value enum_value = make_userdata_value(enum_def, enum_definition_destructor, "enum_definition", sizeof(EnumDefinition));
    env->define(enum_key, enum_value);
    
    env->current_context->push( make_nil_value());
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
    
    StringInternPool* pool = env->current_context->state->stringPool();
    char enum_buf[256];
    snprintf(enum_buf, sizeof(enum_buf), "__enum_%s", enum_name);
    const char* enum_key = pool->intern(enum_buf)->data;
    
    bool found = false;
    Value enum_value = env->get(enum_key, &found);
    if (!found || enum_value.type == VAL_NIL) {
        Value member_var = env->get(member_name, &found);
        if (found && member_var.type != VAL_NIL) {
            env->current_context->push( member_var);
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
    
    EnumDefinition* enum_def = static_cast<EnumDefinition*>(enum_value.as.userdata.ptr);
    if (!enum_def) {
        return make_error(env, "Null enum definition", expr->enum_name.line, expr->enum_name.column);
    }
    
    const EnumMember* member = enum_def->findMember(member_name);
    if (!member) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Undefined enum member '%s.%s'", enum_name, member_name);
        return make_error(env, error_msg, expr->member_name.line, expr->member_name.column);
    }
    
    Value result = Value::makeEnum(enum_def, member->value);
    env->current_context->push( result);
    return make_success(1);
}
