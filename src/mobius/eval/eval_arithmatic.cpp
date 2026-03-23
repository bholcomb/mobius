#include "eval/evaluator.h"
#include "state/mobius_state.h"
#include "data/table.h"
#include "data/metamethods.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

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
    
    // Numeric addition
    if (left.type == VAL_FLOAT32 || left.type == VAL_FLOAT64 || right.type == VAL_FLOAT32 || right.type == VAL_FLOAT64) {
        // Determine result type: VAL_FLOAT64 (double) takes precedence over VAL_FLOAT32
        bool use_double = (left.type == VAL_FLOAT64 || right.type == VAL_FLOAT64);
        
        double left_val = 0.0;
        double right_val = 0.0;
        
        // Convert left operand
        if (left.type == VAL_FLOAT64) {
            left_val = left.as.float64_val;
        } else if (left.type == VAL_FLOAT32) {
            left_val = (double)left.as.float32_val;
        }
        
        // Convert integers to double if needed
        if (left.type == VAL_INTEGER) {
            switch (left.as.integer.num_type) {
                case NUM_INT8:   left_val = left.as.integer.value.i8; break;
                case NUM_UINT8:  left_val = left.as.integer.value.u8; break;
                case NUM_INT16:  left_val = left.as.integer.value.i16; break;
                case NUM_UINT16: left_val = left.as.integer.value.u16; break;
                case NUM_INT32:  left_val = left.as.integer.value.i32; break;
                case NUM_UINT32: left_val = left.as.integer.value.u32; break;
                case NUM_INT64:  left_val = left.as.integer.value.i64; break;
                case NUM_UINT64: left_val = left.as.integer.value.u64; break;
                default: left_val = 0.0; break;
            }
        }
        
        // Convert right operand
        if (right.type == VAL_FLOAT64) {
            right_val = right.as.float64_val;
        } else if (right.type == VAL_FLOAT32) {
            right_val = (double)right.as.float32_val;
        } else if (right.type == VAL_INTEGER) {
            switch (right.as.integer.num_type) {
                case NUM_INT8:   right_val = right.as.integer.value.i8; break;
                case NUM_UINT8:  right_val = right.as.integer.value.u8; break;
                case NUM_INT16:  right_val = right.as.integer.value.i16; break;
                case NUM_UINT16: right_val = right.as.integer.value.u16; break;
                case NUM_INT32:  right_val = right.as.integer.value.i32; break;
                case NUM_UINT32: right_val = right.as.integer.value.u32; break;
                case NUM_INT64:  right_val = right.as.integer.value.i64; break;
                case NUM_UINT64: right_val = right.as.integer.value.u64; break;
                default: right_val = 0.0; break;
            }
        }
        
        // Return appropriate result type
        if (use_double) {
            env->current_context->push( make_float_value(left_val + right_val));
        } else {
            env->current_context->push( make_float32_value((float)(left_val + right_val)));
        }
        return make_success(1);
    }
    
    // Integer addition
    if (left.type == VAL_INTEGER && right.type == VAL_INTEGER) {
        // For simplicity, promote to int64 for arithmetic
        int64_t left_val = 0, right_val = 0;
        
        switch (left.as.integer.num_type) {
            case NUM_INT8:   left_val = left.as.integer.value.i8; break;
            case NUM_UINT8:  left_val = left.as.integer.value.u8; break;
            case NUM_INT16:  left_val = left.as.integer.value.i16; break;
            case NUM_UINT16: left_val = left.as.integer.value.u16; break;
            case NUM_INT32:  left_val = left.as.integer.value.i32; break;
            case NUM_UINT32: left_val = left.as.integer.value.u32; break;
            case NUM_INT64:  left_val = left.as.integer.value.i64; break;
            case NUM_UINT64: left_val = (int64_t)left.as.integer.value.u64; break;
            default: left_val = 0; break;
        }
        
        switch (right.as.integer.num_type) {
            case NUM_INT8:   right_val = right.as.integer.value.i8; break;
            case NUM_UINT8:  right_val = right.as.integer.value.u8; break;
            case NUM_INT16:  right_val = right.as.integer.value.i16; break;
            case NUM_UINT16: right_val = right.as.integer.value.u16; break;
            case NUM_INT32:  right_val = right.as.integer.value.i32; break;
            case NUM_UINT32: right_val = right.as.integer.value.u32; break;
            case NUM_INT64:  right_val = right.as.integer.value.i64; break;
            case NUM_UINT64: right_val = (int64_t)right.as.integer.value.u64; break;
            default: right_val = 0; break;
        }
        
        env->current_context->push( make_integer_value(NUM_INT64, left_val + right_val));
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
    
    // Numeric subtraction only
    if (left.type == VAL_FLOAT64 || right.type == VAL_FLOAT64) {
        double left_val = (left.type == VAL_FLOAT64) ? left.as.float64_val : 0.0;
        double right_val = (right.type == VAL_FLOAT64) ? right.as.float64_val : 0.0;
        
        // Convert integers to double if needed (similar to add_values)
        if (left.type == VAL_INTEGER) {
            switch (left.as.integer.num_type) {
                case NUM_INT32:  left_val = left.as.integer.value.i32; break;
                // ... other cases similar to add_values
                default: left_val = 0.0; break;
            }
        }
        
        if (right.type == VAL_INTEGER) {
            switch (right.as.integer.num_type) {
                case NUM_INT32:  right_val = right.as.integer.value.i32; break;
                // ... other cases similar to add_values
                default: right_val = 0.0; break;
            }
        }
        
        env->current_context->push( make_float_value(left_val - right_val));
        return make_success(1);
    }
    
    if (left.type == VAL_INTEGER && right.type == VAL_INTEGER) {
        // Simplified integer subtraction (assuming int32 for now)
        int64_t left_val = left.as.integer.value.i32;
        int64_t right_val = right.as.integer.value.i32;
        env->current_context->push( make_integer_value(NUM_INT32, left_val - right_val));
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
    
    // Similar pattern to add_values for numeric multiplication
    if (left.type == VAL_FLOAT64 || right.type == VAL_FLOAT64) {
        double left_val = (left.type == VAL_FLOAT64) ? left.as.float64_val : left.as.integer.value.i32;
        double right_val = (right.type == VAL_FLOAT64) ? right.as.float64_val : right.as.integer.value.i32;
        env->current_context->push( make_float_value(left_val * right_val));
        return make_success(1);
    }
    
    if (left.type == VAL_INTEGER && right.type == VAL_INTEGER) {
        int64_t left_val = left.as.integer.value.i32;
        int64_t right_val = right.as.integer.value.i32;
        env->current_context->push( make_integer_value(NUM_INT32, left_val * right_val));
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
    double left_val = (left.type == VAL_FLOAT64) ? left.as.float64_val : 
                      (left.type == VAL_INTEGER) ? left.as.integer.value.i32 : 0.0;
    double right_val = (right.type == VAL_FLOAT64) ? right.as.float64_val : 
                       (right.type == VAL_INTEGER) ? right.as.integer.value.i32 : 0.0;
    
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
    
    // Handle integer modulo (preferred for exact results)
    if (left.type == VAL_INTEGER && right.type == VAL_INTEGER) {
        int32_t left_val = left.as.integer.value.i32;
        int32_t right_val = right.as.integer.value.i32;
        
        if (right_val == 0) {
            return make_error_detailed(
                env,
                "Modulo by zero",
                "Check that the divisor is not zero before performing modulo",
                ERROR_DIVISION,
                line, column,
                NULL,
                NULL
            );
        }
        
        env->current_context->push( make_integer_value(NUM_INT32, left_val % right_val));
        return make_success(1);
    }
    
    // Handle float modulo using fmod
    if ((left.type == VAL_FLOAT64 || left.type == VAL_INTEGER) &&
        (right.type == VAL_FLOAT64 || right.type == VAL_INTEGER)) {
        double left_val = (left.type == VAL_FLOAT64) ? left.as.float64_val : 
                          (double)left.as.integer.value.i32;
        double right_val = (right.type == VAL_FLOAT64) ? right.as.float64_val : 
                           (double)right.as.integer.value.i32;
        
        if (right_val == 0.0) {
            return make_error_detailed(
                env,
                "Modulo by zero",
                "Check that the divisor is not zero before performing modulo",
                ERROR_DIVISION,
                line, column,
                NULL,
                NULL
            );
        }
        
        env->current_context->push( make_float_value(fmod(left_val, right_val)));
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
        // Numeric comparison
        double left_val = 0.0, right_val = 0.0;
        
        if (left.type == VAL_FLOAT64) {
            left_val = left.as.float64_val;
        } else if (left.type == VAL_INTEGER) {
            left_val = left.as.integer.value.i32; // Simplified
        } else {
            return make_error(env, "Cannot compare non-numeric types", 0, 0);
        }
        
        if (right.type == VAL_FLOAT64) {
            right_val = right.as.float64_val;
        } else if (right.type == VAL_INTEGER) {
            right_val = right.as.integer.value.i32; // Simplified
        } else {
            return make_error(env, "Cannot compare non-numeric types", 0, 0);
        }
        
        switch (op) {
            case TOKEN_GREATER:       result = left_val > right_val; break;
            case TOKEN_GREATER_EQUAL: result = left_val >= right_val; break;
            case TOKEN_LESS:          result = left_val < right_val; break;
            case TOKEN_LESS_EQUAL:    result = left_val <= right_val; break;
            default:
                return make_error(env, "Unknown comparison operator", 0, 0);
        }
    }
    
    env->current_context->push( make_bool_value(result));
    return make_success(1);
}