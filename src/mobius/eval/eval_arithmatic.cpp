#include "eval/evaluator.h"
#include "state/mobius_state.h"
#include "data/table.h"
#include "data/metamethods.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

// C-style integer promotion helpers.
// If either operand is NUM_UINT64, arithmetic uses uint64_t and produces
// NUM_UINT64. Otherwise all integer types widen to int64_t / NUM_INT64.
static inline int64_t extract_int64(const Value& v) {
    switch (v.as.integer.num_type) {
        case NUM_INT8:   return v.as.integer.value.i8;
        case NUM_UINT8:  return v.as.integer.value.u8;
        case NUM_INT16:  return v.as.integer.value.i16;
        case NUM_UINT16: return v.as.integer.value.u16;
        case NUM_INT32:  return v.as.integer.value.i32;
        case NUM_UINT32: return v.as.integer.value.u32;
        case NUM_INT64:  return v.as.integer.value.i64;
        case NUM_UINT64: return (int64_t)v.as.integer.value.u64;
        default:         return 0;
    }
}

static inline uint64_t extract_uint64(const Value& v) {
    switch (v.as.integer.num_type) {
        case NUM_INT8:   return (uint64_t)v.as.integer.value.i8;
        case NUM_UINT8:  return v.as.integer.value.u8;
        case NUM_INT16:  return (uint64_t)v.as.integer.value.i16;
        case NUM_UINT16: return v.as.integer.value.u16;
        case NUM_INT32:  return (uint64_t)v.as.integer.value.i32;
        case NUM_UINT32: return v.as.integer.value.u32;
        case NUM_INT64:  return (uint64_t)v.as.integer.value.i64;
        case NUM_UINT64: return v.as.integer.value.u64;
        default:         return 0;
    }
}

static inline double extract_double(const Value& v) {
    if (v.type == VAL_FLOAT64) return v.as.float64_val;
    if (v.type == VAL_FLOAT32) return (double)v.as.float32_val;
    if (v.type == VAL_INTEGER) return (double)extract_int64(v);
    return 0.0;
}

static inline bool use_unsigned(const Value& l, const Value& r) {
    return (l.type == VAL_INTEGER && l.as.integer.num_type == NUM_UINT64) ||
           (r.type == VAL_INTEGER && r.as.integer.num_type == NUM_UINT64);
}

// Helper function to increment/decrement an integer value
Value increment_integer(Value val, bool is_increment, bool* success) {
    *success = false;
    
    if (val.type != VAL_INTEGER) {
        return make_nil_value();
    }
    
    int64_t delta = is_increment ? 1 : -1;
    *success = true;
    
    // Preserve the numeric type and update the value
    switch (val.as.integer.num_type) {
        case NUM_INT8: {
            int64_t new_val = val.as.integer.value.i8 + delta;
            return make_integer_value(NUM_INT8, new_val);
        }
        case NUM_UINT8: {
            int64_t new_val = val.as.integer.value.u8 + delta;
            return make_integer_value(NUM_UINT8, new_val);
        }
        case NUM_INT16: {
            int64_t new_val = val.as.integer.value.i16 + delta;
            return make_integer_value(NUM_INT16, new_val);
        }
        case NUM_UINT16: {
            int64_t new_val = val.as.integer.value.u16 + delta;
            return make_integer_value(NUM_UINT16, new_val);
        }
        case NUM_INT32: {
            int64_t new_val = val.as.integer.value.i32 + delta;
            return make_integer_value(NUM_INT32, new_val);
        }
        case NUM_UINT32: {
            int64_t new_val = val.as.integer.value.u32 + delta;
            return make_integer_value(NUM_UINT32, new_val);
        }
        case NUM_INT64: {
            int64_t new_val = val.as.integer.value.i64 + delta;
            return make_integer_value(NUM_INT64, new_val);
        }
        case NUM_UINT64: {
            uint64_t new_val = val.as.integer.value.u64 + (uint64_t)delta;
            return make_integer_value(NUM_UINT64, new_val);
        }
        default:
            *success = false;
            return make_nil_value();
    }
}

EvalResult eval_increment_expr(IncrementExpr* expr, Environment* env) {
    const char* var_name = expr->name.identifier ? expr->name.identifier : "unknown";
    
    // Get current value
    bool found;
    Value current = env->get(var_name, &found);
    
    if (!found) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Undefined variable '%s'", var_name);
        return make_error(env, error_msg, expr->op.line, expr->op.column);
    }
    
    // Check if it's an integer
    if (current.type != VAL_INTEGER) {
        return make_error(env, "Increment/decrement can only be applied to integers", 
                        expr->op.line, expr->op.column);
    }
    
    // Compute new value
    bool success;
    Value new_value = increment_integer(current, expr->is_increment, &success);
    
    if (!success) {
        return make_error(env, "Failed to increment/decrement value", 
                        expr->op.line, expr->op.column);
    }
    
    // Update the variable
    if (!env->assign(var_name, new_value)) {
        return make_error(env, "Failed to update variable", expr->op.line, expr->op.column);
    }
    
    // Return appropriate value based on prefix/postfix
    if (expr->is_prefix) {
        env->current_context->push( new_value);
    } else {
        env->current_context->push( current);

    }
    return make_success(1);  // Return new value (++i)
}

// Arithmetic operations
EvalResult add_values(Environment* env, const Value& left, const Value& right) {
    // Check for table metamethods first
    if (left.type == VAL_TABLE || right.type == VAL_TABLE) {
        Table* table = (left.type == VAL_TABLE) ? left.as.table : right.as.table;
        MobiusState* state = env->current_context->state;
        Value add_method = table->getMetamethod(state->metamethods()->add());
        
        if (add_method.type == VAL_FUNCTION) {
            // TODO: Call function metamethod
            // For now, fall through to default behavior
        } else if (add_method.type != VAL_NIL) {
            // Non-function metamethod - treat as error for arithmetic
            return make_error(env, "__add metamethod must be a function", 0, 0);
        }
        // If no metamethod found, continue to default error
    }
    // String concatenation
    if (left.type == VAL_STRING || right.type == VAL_STRING) {
        // Convert values to strings if needed, using value_to_string for non-strings
        char* left_str = NULL;
        char* right_str = NULL;
        bool free_left = false;
        bool free_right = false;
        
        // Convert left operand to string
        if (left.type == VAL_STRING) {
            left_str = (char*)left.as.string->data;
        } else {
            left_str = value_to_string(left);
            free_left = true;
        }
        
        // Convert right operand to string
        if (right.type == VAL_STRING) {
            right_str = (char*)right.as.string->data;
        } else {
            right_str = value_to_string(right);
            free_right = true;
        }
        
        // Allocate result buffer (combined length + null terminator)
        size_t left_len = strlen(left_str);
        size_t right_len = strlen(right_str);
        size_t result_len = left_len + right_len;
        char* result = malloc(result_len + 1);
        if (!result) {
            if (free_left) free(left_str);
            if (free_right) free(right_str);
            return make_error(env, "Memory allocation failed", 0, 0);
        }
        
        // Copy both parts
        memcpy(result, left_str, left_len);
        memcpy(result + left_len, right_str, right_len);
        result[result_len] = '\0';
        
        // Clean up temporary strings
        if (free_left) free(left_str);
        if (free_right) free(right_str);
        
        // Intern the result string and push
        Value final_result = make_string_value_from_cstr(env->current_context->state, result);
        free(result);
        
        env->current_context->push( final_result);
        return make_success(1);
    }
    
    // Numeric addition — float path
    if (left.type == VAL_FLOAT32 || left.type == VAL_FLOAT64 || right.type == VAL_FLOAT32 || right.type == VAL_FLOAT64) {
        bool result_is_double = (left.type == VAL_FLOAT64 || right.type == VAL_FLOAT64);
        double lv = extract_double(left);
        double rv = extract_double(right);
        if (result_is_double) {
            env->current_context->push(make_float_value(lv + rv));
        } else {
            env->current_context->push(make_float32_value((float)(lv + rv)));
        }
        return make_success(1);
    }
    
    // Integer addition — C-style promotion: uint64 wins over signed
    if (left.type == VAL_INTEGER && right.type == VAL_INTEGER) {
        if (use_unsigned(left, right)) {
            uint64_t lv = extract_uint64(left);
            uint64_t rv = extract_uint64(right);
            env->current_context->push(make_integer_value(NUM_UINT64, (int64_t)(lv + rv)));
        } else {
            int64_t lv = extract_int64(left);
            int64_t rv = extract_int64(right);
            env->current_context->push(make_integer_value(NUM_INT64, lv + rv));
        }
        return make_success(1);
    }
    
    if (left.type == VAL_TABLE || right.type == VAL_TABLE) {
        return make_error(env, "Cannot perform arithmetic on tables without __add metamethod", 0, 0);
    }
    return make_error(env, "Cannot add these types", 0, 0);
}

EvalResult subtract_values(Environment* env, const Value& left, const Value& right) {
    (void)env;
    // Check for table metamethods first
    if (left.type == VAL_TABLE || right.type == VAL_TABLE) {
        Table* table = (left.type == VAL_TABLE) ? left.as.table : right.as.table;
        MobiusState* state = env->current_context->state;
        Value sub_method = table->getMetamethod(state->metamethods()->sub());
        
        if (sub_method.type == VAL_FUNCTION) {
            // TODO: Call function metamethod
            // For now, fall through to default behavior
        } else if (sub_method.type != VAL_NIL) {
            return make_error(env, "__sub metamethod must be a function", 0, 0);
        }
    }
    
    // Float subtraction
    if (left.type == VAL_FLOAT32 || left.type == VAL_FLOAT64 || right.type == VAL_FLOAT32 || right.type == VAL_FLOAT64) {
        bool result_is_double = (left.type == VAL_FLOAT64 || right.type == VAL_FLOAT64);
        double lv = extract_double(left);
        double rv = extract_double(right);
        if (result_is_double) {
            env->current_context->push(make_float_value(lv - rv));
        } else {
            env->current_context->push(make_float32_value((float)(lv - rv)));
        }
        return make_success(1);
    }
    
    // Integer subtraction — C-style promotion
    if (left.type == VAL_INTEGER && right.type == VAL_INTEGER) {
        if (use_unsigned(left, right)) {
            uint64_t lv = extract_uint64(left);
            uint64_t rv = extract_uint64(right);
            env->current_context->push(make_integer_value(NUM_UINT64, (int64_t)(lv - rv)));
        } else {
            int64_t lv = extract_int64(left);
            int64_t rv = extract_int64(right);
            env->current_context->push(make_integer_value(NUM_INT64, lv - rv));
        }
        return make_success(1);
    }
    
    if (left.type == VAL_TABLE || right.type == VAL_TABLE) {
        return make_error(env, "Cannot perform arithmetic on tables without __sub metamethod", 0, 0);
    }
    return make_error(env, "Cannot subtract these types", 0, 0);
}

EvalResult multiply_values(Environment* env, const Value& left, const Value& right) {
    (void)env;
    // Check for table metamethods first
    if (left.type == VAL_TABLE || right.type == VAL_TABLE) {
        Table* table = (left.type == VAL_TABLE) ? left.as.table : right.as.table;
        MobiusState* state = env->current_context->state;
        Value mul_method = table->getMetamethod(state->metamethods()->mul());
        
        if (mul_method.type == VAL_FUNCTION) {
            // TODO: Call function metamethod
            // For now, fall through to default behavior
        } else if (mul_method.type != VAL_NIL) {
            return make_error(env, "__mul metamethod must be a function", 0, 0);
        }
    }
    
    // Float multiplication
    if (left.type == VAL_FLOAT32 || left.type == VAL_FLOAT64 || right.type == VAL_FLOAT32 || right.type == VAL_FLOAT64) {
        bool result_is_double = (left.type == VAL_FLOAT64 || right.type == VAL_FLOAT64);
        double lv = extract_double(left);
        double rv = extract_double(right);
        if (result_is_double) {
            env->current_context->push(make_float_value(lv * rv));
        } else {
            env->current_context->push(make_float32_value((float)(lv * rv)));
        }
        return make_success(1);
    }
    
    // Integer multiplication — C-style promotion
    if (left.type == VAL_INTEGER && right.type == VAL_INTEGER) {
        if (use_unsigned(left, right)) {
            uint64_t lv = extract_uint64(left);
            uint64_t rv = extract_uint64(right);
            env->current_context->push(make_integer_value(NUM_UINT64, (int64_t)(lv * rv)));
        } else {
            int64_t lv = extract_int64(left);
            int64_t rv = extract_int64(right);
            env->current_context->push(make_integer_value(NUM_INT64, lv * rv));
        }
        return make_success(1);
    }
    
    if (left.type == VAL_TABLE || right.type == VAL_TABLE) {
        return make_error(env, "Cannot perform arithmetic on tables without __mul metamethod", 0, 0);
    }
    return make_error(env, "Cannot multiply these types", 0, 0);
}

EvalResult divide_values(Environment* env, const Value& left, const Value& right, int line, int column) {
    (void)env;
    // Check for table metamethods first
    if (left.type == VAL_TABLE || right.type == VAL_TABLE) {
        Table* table = (left.type == VAL_TABLE) ? left.as.table : right.as.table;
        MobiusState* state = env->current_context->state;
        Value div_method = table->getMetamethod(state->metamethods()->div());
        
        if (div_method.type == VAL_FUNCTION) {
            // TODO: Call function metamethod
            // For now, fall through to default behavior
        } else if (div_method.type != VAL_NIL) {
            return make_error(env, "__div metamethod must be a function", 0, 0);
        }
        
        // If tables without metamethods, return error
        return make_error(env, "Cannot perform arithmetic on tables without __div metamethod", 0, 0);
    }
    
    // Division always returns float to handle fractions
    double left_val = extract_double(left);
    double right_val = extract_double(right);
    
    if (right_val == 0.0) {
        return make_error_detailed(
            env,
            "Division by zero",
            "Check that the divisor is not zero before performing division",
            ERROR_DIVISION,
            line, column,
            NULL,
            NULL
        );
    }
    
    env->current_context->push( make_float_value(left_val / right_val));
    return make_success(1);
}

EvalResult modulo_values(Environment* env, const Value& left, const Value& right, int line, int column) {
    (void)env;
    // Check for table metamethods first
    if (left.type == VAL_TABLE || right.type == VAL_TABLE) {
        Table* table = (left.type == VAL_TABLE) ? left.as.table : right.as.table;
        MobiusState* state = env->current_context->state;
        Value mod_method = table->getMetamethod(state->metamethods()->mod());
        
        if (mod_method.type == VAL_FUNCTION) {
            // TODO: Call function metamethod
            // For now, fall through to default behavior
        } else if (mod_method.type != VAL_NIL) {
            return make_error(env, "__mod metamethod must be a function", 0, 0);
        }
        
        // If tables without metamethods, return error
        return make_error(env, "Cannot perform modulo on tables without __mod metamethod", 0, 0);
    }
    
    // Integer modulo — C-style promotion
    if (left.type == VAL_INTEGER && right.type == VAL_INTEGER) {
        if (use_unsigned(left, right)) {
            uint64_t lv = extract_uint64(left);
            uint64_t rv = extract_uint64(right);
            if (rv == 0) {
                return make_error_detailed(env, "Modulo by zero",
                    "Check that the divisor is not zero before performing modulo",
                    ERROR_DIVISION, line, column, NULL, NULL);
            }
            env->current_context->push(make_integer_value(NUM_UINT64, (int64_t)(lv % rv)));
        } else {
            int64_t lv = extract_int64(left);
            int64_t rv = extract_int64(right);
            if (rv == 0) {
                return make_error_detailed(env, "Modulo by zero",
                    "Check that the divisor is not zero before performing modulo",
                    ERROR_DIVISION, line, column, NULL, NULL);
            }
            env->current_context->push(make_integer_value(NUM_INT64, lv % rv));
        }
        return make_success(1);
    }
    
    // Float modulo using fmod
    if ((left.type == VAL_FLOAT32 || left.type == VAL_FLOAT64 || left.type == VAL_INTEGER) &&
        (right.type == VAL_FLOAT32 || right.type == VAL_FLOAT64 || right.type == VAL_INTEGER)) {
        double lv = extract_double(left);
        double rv = extract_double(right);
        if (rv == 0.0) {
            return make_error_detailed(env, "Modulo by zero",
                "Check that the divisor is not zero before performing modulo",
                ERROR_DIVISION, line, column, NULL, NULL);
        }
        env->current_context->push(make_float_value(fmod(lv, rv)));
        return make_success(1);
    }
    
    return make_error(env, "Cannot modulo these types", 0, 0);
}

EvalResult compare_values(Environment* env, const Value& left, const Value& right, TokenType op) {
    (void)env;
    bool result = false;
    
    // Check for table metamethods first
    if (left.type == VAL_TABLE || right.type == VAL_TABLE) {
        Table* table = (left.type == VAL_TABLE) ? left.as.table : right.as.table;
        MobiusState* state = env->current_context->state;
        MobiusString* metamethod_name = NULL;
        
        switch (op) {
            case TOKEN_EQUAL_EQUAL:
            case TOKEN_BANG_EQUAL:
                metamethod_name = state->metamethods()->eq();
                break;
            case TOKEN_LESS:
                metamethod_name = state->metamethods()->lt();
                break;
            case TOKEN_LESS_EQUAL:
                metamethod_name = state->metamethods()->le();
                break;
            case TOKEN_GREATER:
            case TOKEN_GREATER_EQUAL:
                // For > and >=, we check if the right operand has the metamethod
                if (right.type == VAL_TABLE) {
                    table = right.as.table;
                    metamethod_name = (op == TOKEN_GREATER) ? state->metamethods()->lt() : state->metamethods()->le();
                    // Note: a > b becomes b < a, a >= b becomes b <= a
                }
                break;
            default:
                break;
        }
        
        if (metamethod_name) {
            Value compare_method = table->getMetamethod(metamethod_name);
            
            if (compare_method.type == VAL_FUNCTION) {
                // TODO: Call function metamethod
                // For now, fall through to default behavior
            } else if (compare_method.type != VAL_NIL) {
                return make_error(env, "Comparison metamethod must be a function", 0, 0);
            }
        }
        
        // If no metamethod found and tables are involved, handle equality specially
        if (op == TOKEN_EQUAL_EQUAL || op == TOKEN_BANG_EQUAL) {
            // Use default equality for tables
        } else {
            // Other comparisons require metamethods for tables
            return make_error(env, "Cannot compare tables without appropriate metamethod", 0, 0);
        }
    }
    
    // Equality comparison
    if (op == TOKEN_EQUAL_EQUAL) {
        bool strict = env->current_context->state->config().strict_mode;
        result = strict ? left.exactlyEqual(right) : (left == right);
    } else if (op == TOKEN_BANG_EQUAL) {
        bool strict = env->current_context->state->config().strict_mode;
        result = strict ? !left.exactlyEqual(right) : (left != right);
    } else {
        bool l_num = (left.type == VAL_INTEGER || left.type == VAL_FLOAT32 || left.type == VAL_FLOAT64);
        bool r_num = (right.type == VAL_INTEGER || right.type == VAL_FLOAT32 || right.type == VAL_FLOAT64);

        if (l_num && r_num) {
            bool is_float = (left.type == VAL_FLOAT32 || left.type == VAL_FLOAT64 ||
                             right.type == VAL_FLOAT32 || right.type == VAL_FLOAT64);
            if (is_float) {
                double lv = extract_double(left);
                double rv = extract_double(right);
                switch (op) {
                    case TOKEN_GREATER:       result = lv > rv; break;
                    case TOKEN_GREATER_EQUAL: result = lv >= rv; break;
                    case TOKEN_LESS:          result = lv < rv; break;
                    case TOKEN_LESS_EQUAL:    result = lv <= rv; break;
                    default: return make_error(env, "Unknown comparison operator", 0, 0);
                }
            } else if (use_unsigned(left, right)) {
                uint64_t lv = extract_uint64(left);
                uint64_t rv = extract_uint64(right);
                switch (op) {
                    case TOKEN_GREATER:       result = lv > rv; break;
                    case TOKEN_GREATER_EQUAL: result = lv >= rv; break;
                    case TOKEN_LESS:          result = lv < rv; break;
                    case TOKEN_LESS_EQUAL:    result = lv <= rv; break;
                    default: return make_error(env, "Unknown comparison operator", 0, 0);
                }
            } else {
                int64_t lv = extract_int64(left);
                int64_t rv = extract_int64(right);
                switch (op) {
                    case TOKEN_GREATER:       result = lv > rv; break;
                    case TOKEN_GREATER_EQUAL: result = lv >= rv; break;
                    case TOKEN_LESS:          result = lv < rv; break;
                    case TOKEN_LESS_EQUAL:    result = lv <= rv; break;
                    default: return make_error(env, "Unknown comparison operator", 0, 0);
                }
            }
        } else if (left.type == VAL_STRING && right.type == VAL_STRING) {
            int cmp = strcmp(left.as.string->data, right.as.string->data);
            switch (op) {
                case TOKEN_GREATER:       result = cmp > 0;  break;
                case TOKEN_GREATER_EQUAL: result = cmp >= 0; break;
                case TOKEN_LESS:          result = cmp < 0;  break;
                case TOKEN_LESS_EQUAL:    result = cmp <= 0; break;
                default: return make_error(env, "Unknown comparison operator", 0, 0);
            }
        } else {
            return make_error(env, "Cannot compare incompatible types", 0, 0);
        }
    }
    
    env->current_context->push( make_bool_value(result));
    return make_success(1);
}