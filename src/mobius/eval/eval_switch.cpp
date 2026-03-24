#include "eval/evaluator.h"
#include "state/mobius_state.h"
#include "data/array.h"
#include "data/table.h"

#include <new>
#include <string.h>

// Helper structure for pattern matching results
typedef struct {
    bool matches;
    Environment* bindings;  // Variable bindings from pattern (if any)
} PatternMatchResult;

#include <climits>

// Sentinel: returned when the two values are incomparable (different type
// families).  Callers must check for this before interpreting the result.
static const int COMPARE_INCOMPATIBLE = INT_MIN;

// Ordered comparison that mirrors strcmp conventions.
// Returns -1 / 0 / +1 for numeric and string operands of the same family,
// or COMPARE_INCOMPATIBLE when the types cannot be ordered.
static int simple_compare_values(Value left, Value right) {
    bool l_num = (left.type == VAL_INTEGER || left.type == VAL_FLOAT32 || left.type == VAL_FLOAT64);
    bool r_num = (right.type == VAL_INTEGER || right.type == VAL_FLOAT32 || right.type == VAL_FLOAT64);

    if (l_num && r_num) {
        double lv = 0.0, rv = 0.0;
        switch (left.type) {
            case VAL_INTEGER:  lv = (double)left.as.integer.value.i64;  break;
            case VAL_FLOAT32:  lv = (double)left.as.float32_val;        break;
            case VAL_FLOAT64:  lv = left.as.float64_val;                break;
            default: break;
        }
        switch (right.type) {
            case VAL_INTEGER:  rv = (double)right.as.integer.value.i64; break;
            case VAL_FLOAT32:  rv = (double)right.as.float32_val;       break;
            case VAL_FLOAT64:  rv = right.as.float64_val;               break;
            default: break;
        }
        if (lv < rv) return -1;
        if (lv > rv) return  1;
        return 0;
    }

    if (left.type == VAL_STRING && right.type == VAL_STRING) {
        return strcmp(left.as.string->data, right.as.string->data);
    }

    return COMPARE_INCOMPATIBLE;
}


// Range matching (e.g., 1..10)
static bool value_in_range(Value value, CasePattern* pattern, Environment* env) {
    Environment* temp_env = new (std::nothrow) Environment(env, env->current_context);
    if (!temp_env) {
        return false;
    }
    
    // Evaluate start and end expressions
    EvalResult start_result = evaluate_expr(pattern->as.range_pattern.start, temp_env);
    if (start_result.has_error) {
        temp_env->release();
        return false;
    }
    Value start_val = env->current_context->pop();
    
    EvalResult end_result = evaluate_expr(pattern->as.range_pattern.end, temp_env);
    if (end_result.has_error) {
        temp_env->release();
        return false;
    }
    Value end_val = env->current_context->pop();
    
    temp_env->release();
    bool inclusive = pattern->as.range_pattern.inclusive;
    bool in_range = false;
    
    int cmp_start = simple_compare_values(value, start_val);
    int cmp_end   = simple_compare_values(value, end_val);

    if (cmp_start == COMPARE_INCOMPATIBLE || cmp_end == COMPARE_INCOMPATIBLE)
        return false;

    if (inclusive) {
        in_range = (cmp_start >= 0) && (cmp_end <= 0);
    } else {
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
    
    bool strict = env->current_context->state->config().strict_mode;

    // Equality/inequality can always be tested across types
    if (op == TOKEN_EQUAL_EQUAL) {
        return strict ? value.exactlyEqual(rhs_val) : (value == rhs_val);
    }
    if (op == TOKEN_BANG_EQUAL) {
        return strict ? !value.exactlyEqual(rhs_val) : (value != rhs_val);
    }

    // Relational ops require comparable types; incompatible → no match
    int cmp = simple_compare_values(value, rhs_val);
    if (cmp == COMPARE_INCOMPATIBLE) return false;

    switch (op) {
        case TOKEN_GREATER:       return cmp > 0;
        case TOKEN_GREATER_EQUAL: return cmp >= 0;
        case TOKEN_LESS:          return cmp < 0;
        case TOKEN_LESS_EQUAL:    return cmp <= 0;
        default:                  return false;
    }
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
            
        case PATTERN_ARRAY: {
            if (value.type != VAL_ARRAY || !value.as.array) {
                result.matches = false;
                break;
            }
            ArrayValue* arr = value.as.array;
            size_t elem_count = pattern->as.array_pattern.element_count;
            bool has_rest = pattern->as.array_pattern.has_rest;

            if (!has_rest && arr->length() != elem_count) {
                result.matches = false;
                break;
            }
            if (has_rest && arr->length() < elem_count) {
                result.matches = false;
                break;
            }

            result.matches = true;
            result.bindings = new (std::nothrow) Environment(env, env->current_context);
            StringInternPool* pool = env->current_context->state->stringPool();
            for (size_t k = 0; k < elem_count; k++) {
                const char* name = pattern->as.array_pattern.elements[k].name;
                if (name) {
                    result.bindings->define(pool->intern(name), arr->get(k));
                }
            }
            if (has_rest && pattern->as.array_pattern.rest_name) {
                ArrayValue* rest = new (std::nothrow) ArrayValue(arr->length() - elem_count);
                for (size_t k = elem_count; k < arr->length(); k++) {
                    rest->push(arr->get(k));
                }
                result.bindings->define(pool->intern(pattern->as.array_pattern.rest_name),
                                        make_array_value(rest));
            }
            break;
        }

        case PATTERN_TABLE: {
            if (value.type != VAL_TABLE || !value.as.table) {
                result.matches = false;
                break;
            }
            Table* tbl = value.as.table;
            size_t field_count = pattern->as.table_pattern.field_count;

            result.matches = true;
            result.bindings = new (std::nothrow) Environment(env, env->current_context);
            StringInternPool* tpool = env->current_context->state->stringPool();
            for (size_t k = 0; k < field_count; k++) {
                const char* key = pattern->as.table_pattern.fields[k].key;
                const char* bind = pattern->as.table_pattern.fields[k].bind_name;
                bool optional = pattern->as.table_pattern.fields[k].is_optional;

                Value key_val = make_string_value_from_cstr(
                    env->current_context->state, key);
                Value field_val = tbl->get(key_val);

                if (field_val.type == VAL_NIL && !optional) {
                    result.matches = false;
                    result.bindings->release();
                    result.bindings = NULL;
                    break;
                }
                const char* var_name = bind ? bind : key;
                result.bindings->define(tpool->intern(var_name), field_val);
            }
            break;
        }
            
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
        
        bool case_matches = false;
        Environment* bindings_env = nullptr;
        Environment* case_env = new (std::nothrow) Environment(env, env->current_context);
        if (!case_env) {
            return make_error(env, "Memory allocation failed", 0, 0);
        }

        for (size_t j = 0; j < case_clause->pattern_count; j++) {
            PatternMatchResult match_result = match_pattern(
                case_clause->patterns[j], switch_value, env);

            if (match_result.matches) {
                if (case_clause->guard) {
                    EvalResult guard_result = evaluate_expr(case_clause->guard, case_env);
                    if (guard_result.has_error) {
                        if (match_result.bindings) match_result.bindings->release();
                        case_env->release();
                        return guard_result;
                    }
                    Value guard_val = env->current_context->pop();
                    if (!is_truthy(guard_val)) {
                        if (match_result.bindings) match_result.bindings->release();
                        continue;
                    }
                }

                case_matches = true;
                if (match_result.bindings) {
                    bindings_env = match_result.bindings;
                    case_env->release();
                    case_env = new (std::nothrow) Environment(bindings_env,
                                                              env->current_context);
                }
                break;
            }
        }
        
        if (case_matches) {
            EvalResult case_result = make_success(0);

            for (size_t j = 0; j < case_clause->body_count; j++) {
                case_result = evaluate_stmt(case_clause->body[j], case_env);

                if (case_result.has_error || case_result.has_returned) {
                    case_env->release();
                    if (bindings_env) bindings_env->release();
                    return case_result;
                }

                if (case_result.has_break) {
                    case_env->release();
                    if (bindings_env) bindings_env->release();
                    case_result.has_break = false;
                    return case_result;
                }
            }

            case_env->release();
            if (bindings_env) bindings_env->release();

            if (!case_clause->has_break) {
                continue;
            } else {
                return case_result;
            }
        }

        case_env->release();
        if (bindings_env) bindings_env->release();
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
