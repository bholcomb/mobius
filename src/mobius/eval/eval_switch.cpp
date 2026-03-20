#include "eval/evaluator.h"
#include "state/mobius_state.h"

#include <new>
#include <string.h>

// Helper structure for pattern matching results
typedef struct {
    bool matches;
    Environment* bindings;  // Variable bindings from pattern (if any)
} PatternMatchResult;

// Simple comparison function that returns integer like strcmp
// Returns: -1 if left < right, 0 if left == right, 1 if left > right
static int simple_compare_values(Value left, Value right) {
    // Handle numeric comparisons with type coercion
    if ((left.type == VAL_INTEGER || left.type == VAL_FLOAT32 || left.type == VAL_FLOAT64) &&
        (right.type == VAL_INTEGER || right.type == VAL_FLOAT32 || right.type == VAL_FLOAT64)) {
        
        double left_val = 0.0, right_val = 0.0;
        
        // Convert left to double
        switch (left.type) {
            case VAL_INTEGER:
                left_val = (double)left.as.integer.value.i64;
                break;
            case VAL_FLOAT32:
                left_val = (double)left.as.float32_val;
                break;
            case VAL_FLOAT64:
                left_val = left.as.float64_val;
                break;
            default:
                return 0; // Should not happen
        }
        
        // Convert right to double
        switch (right.type) {
            case VAL_INTEGER:
                right_val = (double)right.as.integer.value.i64;
                break;
            case VAL_FLOAT32:
                right_val = (double)right.as.float32_val;
                break;
            case VAL_FLOAT64:
                right_val = right.as.float64_val;
                break;
            default:
                return 0; // Should not happen
        }
        
        if (left_val < right_val) return -1;
        if (left_val > right_val) return 1;
        return 0;
    }
    
    // String comparison
    if (left.type == VAL_STRING && right.type == VAL_STRING) {
        const char* left_str = left.as.string->data;
        const char* right_str = right.as.string->data;
        return strcmp(left_str, right_str);
    }
    
    // Different types - not comparable
    return 0;
}


// Range matching (e.g., 1..10)
static bool value_in_range(Value value, CasePattern* pattern, Environment* env) {
    // Note: These evaluations need a temporary environment
    // For now, we'll assume range patterns use literals
    // TODO: Support complex expressions in range patterns with proper environment
    Environment* temp_env = new (std::nothrow) Environment(nullptr, env->current_context);
    if (!temp_env) {
        return false;
    }
    
    // Evaluate start and end expressions
    EvalResult start_result = evaluate_expr(pattern->as.range_pattern.start, temp_env);
    if (start_result.has_error) {
        delete temp_env;
        return false;
    }
    Value start_val = env->current_context->pop();
    
    EvalResult end_result = evaluate_expr(pattern->as.range_pattern.end, temp_env);
    if (end_result.has_error) {
        delete temp_env;
        return false;
    }
    Value end_val = env->current_context->pop();
    
    delete temp_env;
    bool inclusive = pattern->as.range_pattern.inclusive;
    bool in_range = false;
    
    // Use simple_compare_values for range checking
    int cmp_start = simple_compare_values(value, start_val);
    int cmp_end = simple_compare_values(value, end_val);
    
    if (inclusive) {
        // value >= start && value <= end
        in_range = (cmp_start >= 0) && (cmp_end <= 0);
    } else {
        // value >= start && value < end  
        in_range = (cmp_start >= 0) && (cmp_end < 0);
    }
    
    return in_range;
}


// Expression pattern matching (e.g., >= 10, <= 5)
static bool evaluate_expression_pattern(CasePattern* pattern, Value value, Environment* env) {
    EvalResult rhs_result = evaluate_expr(pattern->as.expr_pattern.expression, env);
    if (rhs_result.has_error) {
        return false;
    }
    
    Value rhs_val = env->current_context->pop();
    TokenType op = pattern->as.expr_pattern.op;
    bool result = false;
    
    bool strict = env->current_context->state->config().strict_mode;

    switch (op) {
        case TOKEN_EQUAL_EQUAL:
            result = strict ? value.exactlyEqual(rhs_val) : (value == rhs_val);
            break;
        case TOKEN_BANG_EQUAL:
            result = strict ? !value.exactlyEqual(rhs_val) : (value != rhs_val);
            break;
        case TOKEN_GREATER:
            result = simple_compare_values(value, rhs_val) > 0;
            break;
        case TOKEN_GREATER_EQUAL:
            result = simple_compare_values(value, rhs_val) >= 0;
            break;
        case TOKEN_LESS:
            result = simple_compare_values(value, rhs_val) < 0;
            break;
        case TOKEN_LESS_EQUAL:
            result = simple_compare_values(value, rhs_val) <= 0;
            break;
        default:
            result = false;
            break;
    }
    
    return result;
}



// Pattern matching implementation
static PatternMatchResult match_pattern(CasePattern* pattern, Value value, Environment* env) {
    PatternMatchResult result = {false, NULL};
    
    bool strict_eq = env->current_context->state->config().strict_mode;

    switch (pattern->type) {
        case PATTERN_VALUE:
            result.matches = strict_eq
                ? pattern->as.literal.exactlyEqual(value)
                : (pattern->as.literal == value);
            break;
            
        case PATTERN_EXPRESSION:
            result.matches = evaluate_expression_pattern(pattern, value, env);
            break;
            
        case PATTERN_RANGE:
            result.matches = value_in_range(value, pattern, env);
            break;
            
        case PATTERN_TYPE:
            result.matches = (value.type == pattern->as.type_pattern.value_type);
            break;
            
        case PATTERN_ARRAY:
            // TODO: Implement array destructuring
            result.matches = false;
            break;
            
        case PATTERN_TABLE:
            // TODO: Implement table destructuring
            result.matches = false;
            break;
            
        case PATTERN_WILDCARD:
            result.matches = true;  // Always matches
            break;
    }
    
    return result;
}

// Switch statement evaluation
EvalResult eval_switch_stmt(SwitchStmt* stmt, Environment* env) {
    // Evaluate the discriminant
    EvalResult discriminant_result = evaluate_expr(stmt->discriminant, env);
    if (discriminant_result.has_error) {
        return discriminant_result;
    }
    
    Value switch_value = env->current_context->pop();
    
    // Try each case in order
    for (size_t i = 0; i < stmt->case_count; i++) {
        SwitchCase* case_clause = stmt->cases[i];
        
        // Check if any pattern in this case matches
        bool case_matches = false;
        Environment* case_env = new (std::nothrow) Environment(env, env->current_context);
        if (!case_env) {
            return make_error(env, "Memory allocation failed", 0, 0);
        }
        
        for (size_t j = 0; j < case_clause->pattern_count; j++) {
            PatternMatchResult match_result = match_pattern(
                case_clause->patterns[j], switch_value, env);
            
            if (match_result.matches) {
                // Check guard clause if present
                if (case_clause->guard) {
                    EvalResult guard_result = evaluate_expr(case_clause->guard, case_env);
                        if (guard_result.has_error) {
                            delete case_env;
                            return guard_result;
                        }
                    Value guard_val = env->current_context->pop();
                    bool guard_passed = is_truthy(guard_val);
                    if (!guard_passed) {
                        continue;  // Guard failed, try next pattern
                    }
                }
                
                // Pattern and guard matched
                case_matches = true;
                if (match_result.bindings) {
                    // TODO: Merge bindings into case_env
                    delete match_result.bindings;
                }
                break;
            }
        }
        
        if (case_matches) {
            // Execute case body
            EvalResult case_result = make_success(0);
            
            for (size_t j = 0; j < case_clause->body_count; j++) {
                case_result = evaluate_stmt(case_clause->body[j], case_env);
                
                if (case_result.has_error || case_result.has_returned) {
                    delete case_env;
                    return case_result;
                }
                
                // Check for break
                if (case_result.has_break) {
                    delete case_env;
                    // Convert break to normal success
                    case_result.has_break = false;
                    return case_result;
                }
            }
            
            delete case_env;
            
            // If no explicit break, fall through to next case
            if (!case_clause->has_break) {
                continue;  // Fall through
            } else {
                return case_result;
            }
        }
        
        delete case_env;
    }
    
    // No case matched, try default
    if (stmt->default_body) {
        EvalResult default_result = make_success(0);
        
        for (size_t i = 0; i < stmt->default_body_count; i++) {
            default_result = evaluate_stmt(stmt->default_body[i], env);
            
            if (default_result.has_error || default_result.has_returned || default_result.has_break) {
                if (default_result.has_break) {
                    default_result.has_break = false;  // Convert break to normal
                }
                return default_result;
            }
        }
        
        return default_result;
    }
    
    // No match and no default
    return make_success(0);
}
