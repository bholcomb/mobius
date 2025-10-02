#define _GNU_SOURCE  // For strdup
#include "evaluator.h"
#include "library/library.h"
#include "module_registry.h"
#include "token.h"
#include "table.h"
#include "types.h"
#include "stack_trace.h"
#include "enumValue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Global type checking configuration
TypeCheckConfig global_type_config = {false, false};

// Utility functions
EvalResult make_success(int return_count) {
    EvalResult result = {0};
    result.value = make_nil_value();
    result.return_count = return_count;
    result.has_error = false;
    result.has_returned = false;
    result.has_break = false;
    result.has_continue = false;
    return result;
}

EvalResult make_success_with_value(Value value) {
    EvalResult result = {0};
    result.value = value;
    result.return_count = 0;  // Traditional evaluation doesn't use stack returns
    result.has_error = false;
    result.has_returned = false;
    result.has_break = false;
    result.has_continue = false;
    return result;
}

EvalResult make_error(const char* message, int line, int column) {
    EvalResult result = {0};
    result.value = make_nil_value();
    result.return_count = 0;
    result.has_error = true;
    result.has_returned = false;
    result.has_break = false;
    result.has_continue = false;
    
    // Duplicate the message to prevent corruption from stack-allocated strings
    if (message) {
        char* message_copy = strdup(message);
        result.error.message = message_copy;
    } else {
        result.error.message = "Unknown error";
    }
    
    result.error.suggestion = NULL;
    result.error.category = ERROR_RUNTIME;
    result.error.line = line;
    result.error.column = column;
    result.error.function_name = NULL;
    result.error.source_line = NULL;
    
    // Capture current stack trace immediately (make a copy)
    StackTrace* current = get_current_stack_trace();
    if (current && current->frame_count > 0) {
        result.error.stack_trace = malloc(sizeof(StackTrace));
        if (result.error.stack_trace) {
            result.error.stack_trace->frame_count = current->frame_count;
            result.error.stack_trace->frame_capacity = current->frame_capacity;
            result.error.stack_trace->max_depth = current->max_depth;
            
            // Copy the frames
            result.error.stack_trace->frames = malloc(sizeof(CallFrame) * current->frame_count);
            if (result.error.stack_trace->frames) {
                memcpy(result.error.stack_trace->frames, current->frames, 
                       sizeof(CallFrame) * current->frame_count);
            } else {
                free(result.error.stack_trace);
                result.error.stack_trace = NULL;
            }
        }
    } else {
        result.error.stack_trace = NULL;
    }
    
    return result;
}

EvalResult make_error_detailed(const char* message, const char* suggestion, 
                              ErrorCategory category, int line, int column,
                              const char* function_name, const char* source_line) {
    EvalResult result = {0};
    result.value = make_nil_value();
    result.return_count = 0;
    result.has_error = true;
    result.has_returned = false;
    result.has_break = false;
    result.has_continue = false;
    
    // Duplicate the message and suggestion to prevent corruption
    if (message) {
        char* message_copy = strdup(message);
        result.error.message = message_copy;
    } else {
        result.error.message = "Unknown error";
    }
    
    if (suggestion) {
        char* suggestion_copy = strdup(suggestion);
        result.error.suggestion = suggestion_copy;
    } else {
        result.error.suggestion = NULL;
    }
    result.error.category = category;
    result.error.line = line;
    result.error.column = column;
    result.error.function_name = function_name;
    result.error.source_line = source_line;
    
    // Capture current stack trace immediately (make a copy)
    StackTrace* current = get_current_stack_trace();
    if (current && current->frame_count > 0) {
        result.error.stack_trace = malloc(sizeof(StackTrace));
        if (result.error.stack_trace) {
            result.error.stack_trace->frame_count = current->frame_count;
            result.error.stack_trace->frame_capacity = current->frame_capacity;
            result.error.stack_trace->max_depth = current->max_depth;
            
            // Copy the frames
            result.error.stack_trace->frames = malloc(sizeof(CallFrame) * current->frame_count);
            if (result.error.stack_trace->frames) {
                memcpy(result.error.stack_trace->frames, current->frames, 
                       sizeof(CallFrame) * current->frame_count);
            } else {
                free(result.error.stack_trace);
                result.error.stack_trace = NULL;
            }
        }
    } else {
        result.error.stack_trace = NULL;
    }
    
    return result;
}

bool is_error(EvalResult result) {
    return result.has_error;
}

const char* error_category_name(ErrorCategory category) {
    switch (category) {
        case ERROR_RUNTIME: return "Runtime Error";
        case ERROR_TYPE: return "Type Error";
        case ERROR_UNDEFINED: return "Undefined Error";
        case ERROR_ARGUMENT: return "Argument Error";
        case ERROR_DIVISION: return "Division Error";
        case ERROR_MEMORY: return "Memory Error";
        case ERROR_RETURN: return "Return Error";
        default: return "Unknown Error";
    }
}

// Extract a specific line from source code
const char* extract_source_line(const char* source, int line_number) {
    if (!source || line_number <= 0) {
        return NULL;
    }
    
    static char line_buffer[512];  // Static buffer for the extracted line
    const char* current = source;
    int current_line = 1;
    
    // Find the start of the target line
    while (current_line < line_number && *current) {
        if (*current == '\n') {
            current_line++;
        }
        current++;
    }
    
    if (current_line != line_number || !*current) {
        return NULL;  // Line not found
    }
    
    // Copy the line content, excluding the newline
    const char* line_start = current;
    const char* line_end = current;
    while (*line_end && *line_end != '\n' && *line_end != '\r') {
        line_end++;
    }
    
    size_t line_length = line_end - line_start;
    if (line_length >= sizeof(line_buffer)) {
        line_length = sizeof(line_buffer) - 1;  // Truncate if too long
    }
    
    strncpy(line_buffer, line_start, line_length);
    line_buffer[line_length] = '\0';
    
    return line_buffer;
}

const char* get_error_suggestion(ErrorCategory category) {
    switch (category) {
        case ERROR_TYPE:
            return "Check that all operands are of compatible types";
        case ERROR_UNDEFINED:
            return "Make sure the variable or function is declared before use";
        case ERROR_ARGUMENT:
            return "Check the function definition for the correct number of parameters";
        case ERROR_DIVISION:
            return "Ensure the divisor is not zero";
        case ERROR_MEMORY:
            return "The system may be low on memory";
        case ERROR_RETURN:
            return "Return statements can only be used inside functions";
        default:
            return NULL;
    }
}

void print_runtime_error(RuntimeError error) {
    fprintf(stderr, "\n");
    fprintf(stderr, "━━━ %s ━━━\n", error_category_name(error.category));
    
    if (error.line > 0) {
        fprintf(stderr, "  at line %d", error.line);
        if (error.column > 0) {
            fprintf(stderr, ", column %d", error.column);
        }
        if (error.function_name) {
            fprintf(stderr, " in function '%s'", error.function_name);
        }
        fprintf(stderr, "\n");
    }
    
    fprintf(stderr, "\n  %s\n", error.message);
    
    // Show source line if available
    if (error.source_line) {
        fprintf(stderr, "\n  Source: %s\n", error.source_line);
        if (error.column > 0) {
            fprintf(stderr, "          ");
            for (int i = 1; i < error.column; i++) {
                fprintf(stderr, " ");
            }
            fprintf(stderr, "^\n");
        }
    }
    
    // Show stack trace if available
    if (error.stack_trace && error.stack_trace->frame_count > 0) {
        fprintf(stderr, "\n━━━ Call Stack ━━━\n");
        for (size_t i = error.stack_trace->frame_count; i > 0; i--) {
            CallFrame* frame = &error.stack_trace->frames[i - 1];
            
            fprintf(stderr, "  %zu. ", error.stack_trace->frame_count - i + 1);
            
            if (frame->is_builtin) {
                fprintf(stderr, "builtin function '%s'", frame->function_name ? frame->function_name : "unknown");
            } else if (frame->is_plugin) {
                if (frame->module_name) {
                    fprintf(stderr, "plugin function '%s.%s'", frame->module_name, 
                           frame->function_name ? frame->function_name : "unknown");
                } else {
                    fprintf(stderr, "plugin function '%s'", frame->function_name ? frame->function_name : "unknown");
                }
            } else {
                fprintf(stderr, "function '%s'", frame->function_name ? frame->function_name : "unknown");
            }
            
            if (frame->filename && frame->line > 0) {
                fprintf(stderr, " at %s:%d", frame->filename, frame->line);
                if (frame->column > 0) {
                    fprintf(stderr, ":%d", frame->column);
                }
            } else if (frame->line > 0) {
                fprintf(stderr, " at line %d", frame->line);
                if (frame->column > 0) {
                    fprintf(stderr, ":%d", frame->column);
                }
            }
            
            fprintf(stderr, "\n");
        }
    }
    
    // Show suggestion if available
    if (error.suggestion) {
        fprintf(stderr, "\n  💡 Suggestion: %s\n", error.suggestion);
    } else {
        const char* auto_suggestion = get_error_suggestion(error.category);
        if (auto_suggestion) {
            fprintf(stderr, "\n  💡 Suggestion: %s\n", auto_suggestion);
        }
    }
    
    fprintf(stderr, "\n");
}

void print_runtime_error_with_context(RuntimeError error, const char* filename) {
    fprintf(stderr, "\n");
    fprintf(stderr, "━━━ %s ━━━\n", error_category_name(error.category));
    
    if (filename) {
        fprintf(stderr, "  in file: %s\n", filename);
    }
    
    if (error.line > 0) {
        fprintf(stderr, "  at line %d", error.line);
        if (error.column > 0) {
            fprintf(stderr, ", column %d", error.column);
        }
        if (error.function_name) {
            fprintf(stderr, " in function '%s'", error.function_name);
        }
        fprintf(stderr, "\n");
    }
    
    fprintf(stderr, "\n  %s\n", error.message);
    
    // Show source line if available
    if (error.source_line) {
        fprintf(stderr, "\n  %3d | %s\n", error.line, error.source_line);
        if (error.column > 0) {
            fprintf(stderr, "      | ");
            for (int i = 1; i < error.column; i++) {
                fprintf(stderr, " ");
            }
            fprintf(stderr, "^\n");
        }
    }
    
    // Show suggestion
    if (error.suggestion) {
        fprintf(stderr, "\n  💡 Suggestion: %s\n", error.suggestion);
    } else {
        const char* auto_suggestion = get_error_suggestion(error.category);
        if (auto_suggestion) {
            fprintf(stderr, "\n  💡 Suggestion: %s\n", auto_suggestion);
        }
    }
    
    fprintf(stderr, "\n");
}

// Expression evaluation
EvalResult eval_literal_expr(LiteralExpr* expr, Environment* env) {
    (void)env;  // Unused parameter
    return make_success_with_value(copy_value(expr->value));
}

// =============================================================================
// STACK-BASED EXPRESSION EVALUATION (NEW)
// =============================================================================

// Stack-based literal evaluation - pushes value onto stack
EvalResult eval_literal_expr_stack(LiteralExpr* expr, Environment* env) {
    // Push the literal value onto the environment's stack
    env_push(env, expr->value);
    return make_success(1);  // Success indicator, 1 value pushed onto stack
}

// Stack-based variable evaluation - pushes variable value onto stack
EvalResult eval_variable_expr_stack(VariableExpr* expr, Environment* env) {
    const char* name = expr->name.identifier ? expr->name.identifier : "unknown";
    
    bool found;
    Value value = get_variable(env, name, &found);
    
    if (!found) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Undefined variable '%s'", name);
        
        return make_error_detailed(
            error_msg,
            "Make sure the variable is declared before use",
            ERROR_UNDEFINED,
            0, 0,
            name,
            NULL
        );
    }
    
    // Push the variable value onto the stack
    env_push(env, value);
    return make_success(1);  // Success indicator, 1 value pushed onto stack
}

// Stack-based binary evaluation - pops operands, computes, pushes result
EvalResult eval_binary_expr_stack(BinaryExpr* expr, Environment* env) {
    // Evaluate left operand (pushes onto stack)
    EvalResult left_result = evaluate_expr_stack(expr->left, env);
    if (is_error(left_result)) {
        return left_result;
    }
    
    // Evaluate right operand (pushes onto stack)
    EvalResult right_result = evaluate_expr_stack(expr->right, env);
    if (is_error(right_result)) {
        return right_result;
    }
    
    // Pop operands from stack (right operand is on top)
    Value right = env_pop(env);
    Value left = env_pop(env);
    
    EvalResult result;
    switch (expr->op.type) {
        case TOKEN_PLUS:
            result = add_values(left, right);
            break;
        case TOKEN_MINUS:
            result = subtract_values(left, right);
            break;
        case TOKEN_STAR:
            result = multiply_values(left, right);
            break;
        case TOKEN_SLASH:
            result = divide_values(left, right, expr->op.line, expr->op.column);
            break;
        case TOKEN_PERCENT:
            result = modulo_values(left, right, expr->op.line, expr->op.column);
            break;
        case TOKEN_EQUAL_EQUAL:
        case TOKEN_BANG_EQUAL:
        case TOKEN_GREATER:
        case TOKEN_GREATER_EQUAL:
        case TOKEN_LESS:
        case TOKEN_LESS_EQUAL:
            result = compare_values(left, right, expr->op.type);
            break;
        case TOKEN_AND:
        case TOKEN_AND_AND:
            if (!is_truthy(left)) {
                env_push(env, left);
            } else {
                env_push(env, right);
            }
            result = make_success(1);
            break;
        case TOKEN_OR:
        case TOKEN_OR_OR:
            if (is_truthy(left)) {
                env_push(env, left);
            } else {
                env_push(env, right);
            }
            result = make_success(1);
            break;
        default:
            result = make_error("Unsupported binary operator", expr->op.line, expr->op.column);
            break;
    }
    
    // Clean up operands
    free_value(left);
    free_value(right);
    
    if (is_error(result)) {
        return result;
    }
    
    // Push result onto stack
    env_push(env, result.value);
    free_value(result.value);  // env_push copies the value
    
    return make_success(1);  // Success indicator, 1 value pushed onto stack
}

// Stack-based unary evaluation - pops operand, computes, pushes result
EvalResult eval_unary_expr_stack(UnaryExpr* expr, Environment* env) {
    // Evaluate operand (pushes onto stack)
    EvalResult operand_result = evaluate_expr_stack(expr->right, env);
    if (is_error(operand_result)) {
        return operand_result;
    }
    
    // Pop operand from stack
    Value operand = env_pop(env);
    
    EvalResult result;
    switch (expr->op.type) {
        case TOKEN_MINUS:
            if (operand.type == VAL_FLOAT64) {
                result = make_success_with_value(make_float_value(-operand.as.float64_val));
            } else if (operand.type == VAL_INTEGER) {
                // Extract value properly based on the actual type, but result should be int64_t
                int64_t value = 0;
                switch (operand.as.integer.num_type) {
                    case NUM_INT8:   value = operand.as.integer.value.i8; break;
                    case NUM_UINT8:  value = operand.as.integer.value.u8; break;
                    case NUM_INT16:  value = operand.as.integer.value.i16; break;
                    case NUM_UINT16: value = operand.as.integer.value.u16; break;
                    case NUM_INT32:  value = operand.as.integer.value.i32; break;
                    case NUM_UINT32: value = operand.as.integer.value.u32; break;
                    case NUM_INT64:  value = operand.as.integer.value.i64; break;
                    case NUM_UINT64: value = operand.as.integer.value.u64; break;
                    default: value = operand.as.integer.value.i32; break;
                }
                result = make_success_with_value(make_integer_value(NUM_INT64, -value));
            } else {
                result = make_error("Cannot negate non-numeric value", expr->op.line, expr->op.column);
            }
            break;
        case TOKEN_PLUS:
            // Unary plus is identity for numbers
            if (operand.type == VAL_FLOAT64 || operand.type == VAL_INTEGER) {
                result = make_success_with_value(operand);
            } else {
                result = make_error("Cannot apply unary plus to non-numeric value", expr->op.line, expr->op.column);
            }
            break;
        case TOKEN_BANG:
        case TOKEN_NOT:
            result = make_success_with_value(make_bool_value(!is_truthy(operand)));
            break;
        default:
            result = make_error("Unknown unary operator", expr->op.line, expr->op.column);
            break;
    }
    
    // Clean up operand
    free_value(operand);
    
    if (is_error(result)) {
        return result;
    }
    
    // Push result onto stack
    env_push(env, result.value);
    free_value(result.value);  // env_push copies the value
    
    return make_success(1);  // Success indicator, 1 value pushed onto stack
}

// Stack-based grouping evaluation - just evaluates the inner expression
EvalResult eval_grouping_expr_stack(GroupingExpr* expr, Environment* env) {
    // Grouping just evaluates the inner expression
    return evaluate_expr_stack(expr->expression, env);
}

// =============================================================================
// STACK-BASED FUNCTION INTERFACE (UNIFIED FOR AST AND BYTECODE)
// =============================================================================

// Stack-based builtin function signature - unified interface that works for both AST and bytecode
typedef EvalResult (*StackLibraryFunction)(Environment* env, int arg_count);

// =============================================================================
// NATIVE STACK-BASED BUILTIN FUNCTIONS
// =============================================================================

// Native stack-based print function
EvalResult builtin_print_stack(Environment* env, int arg_count) {
    for (int i = 0; i < arg_count; i++) {
        if (i > 0) printf(" ");
        Value arg = env_peek(env, arg_count - 1 - i);  // Get args in correct order
        print_value(arg);
    }
    printf("\n");
    
    // Pop arguments from stack
    for (int i = 0; i < arg_count; i++) {
        env_pop(env);
    }
    
    return make_success(0);
}

// Native stack-based str function
EvalResult builtin_str_stack(Environment* env, int arg_count) {
    if (arg_count != 1) {
        return make_error("str() expects exactly 1 argument", 0, 0);
    }
    
    Value arg = env_pop(env);
    char* temp_str = value_to_string(arg);
    Value result = make_string_value_from_cstr(temp_str);
    free(temp_str);
    free_value(arg);
    
    env_push(env, result);
    free_value(result);  // env_push copies the value
    return make_success(1);
}

// Native stack-based int function
EvalResult builtin_int_stack(Environment* env, int arg_count) {
    if (arg_count != 1) {
        return make_error("int() expects exactly 1 argument", 0, 0);
    }
    
    Value arg = env_pop(env);
    
    Value result_val;
    switch (arg.type) {
        case VAL_INTEGER:
            result_val = arg;  // Already an integer
            break;
        case VAL_FLOAT64:
            result_val = make_integer_value(NUM_INT32, (int32_t)arg.as.float64_val);
            free_value(arg);
            break;
        case VAL_STRING: {
            if (arg.as.string) {
                const char* str_data = string_data(arg.as.string);
                char* endptr;
                long val = strtol(str_data, &endptr, 10);
                if (*endptr == '\0') {
                    result_val = make_integer_value(NUM_INT32, (int32_t)val);
                } else {
                    free_value(arg);
                    return make_error("Cannot convert string to integer", 0, 0);
                }
            } else {
                free_value(arg);
                return make_error("Cannot convert null string to integer", 0, 0);
            }
            free_value(arg);
            break;
        }
        case VAL_BOOL:
            result_val = make_integer_value(NUM_INT32, arg.as.boolean ? 1 : 0);
            free_value(arg);
            break;
        default:
            free_value(arg);
            return make_error("Cannot convert value to integer", 0, 0);
    }
    
    env_push(env, result_val);
    free_value(result_val);  // env_push copies the value
    return make_success(1);
}

// Native stack-based float function  
EvalResult builtin_float_stack(Environment* env, int arg_count) {
    if (arg_count != 1) {
        return make_error("float() expects exactly 1 argument", 0, 0);
    }
    
    Value arg = env_pop(env);
    
    Value result_val;
    switch (arg.type) {
        case VAL_FLOAT64:
            result_val = arg;  // Already a float
            break;
        case VAL_INTEGER: {
            // Convert integer to float based on its numeric type
            double val = 0.0;
            switch (arg.as.integer.num_type) {
                case NUM_INT8:   val = arg.as.integer.value.i8; break;
                case NUM_UINT8:  val = arg.as.integer.value.u8; break;
                case NUM_INT16:  val = arg.as.integer.value.i16; break;
                case NUM_UINT16: val = arg.as.integer.value.u16; break;
                case NUM_INT32:  val = arg.as.integer.value.i32; break;
                case NUM_UINT32: val = arg.as.integer.value.u32; break;
                case NUM_INT64:  val = arg.as.integer.value.i64; break;
                case NUM_UINT64: val = arg.as.integer.value.u64; break;
                default: val = arg.as.integer.value.i32; break;
            }
            result_val = make_float_value(val);
            free_value(arg);
            break;
        }
        case VAL_STRING: {
            if (arg.as.string) {
                const char* str = string_data(arg.as.string);
                char* endptr;
                double val = strtod(str, &endptr);
                if (*endptr == '\0') {
                    result_val = make_float_value(val);
                } else {
                    free_value(arg);
                    return make_error("Cannot convert string to float", 0, 0);
                }
            } else {
                free_value(arg);
                return make_error("Cannot convert null string to float", 0, 0);
            }
            free_value(arg);
            break;
        }
        default:
            free_value(arg);
            return make_error("Cannot convert value to float", 0, 0);
    }
    
    env_push(env, result_val);
    free_value(result_val);  // env_push copies the value
    return make_success(1);
}

// Native stack-based typeof function
EvalResult builtin_typeof_stack(Environment* env, int arg_count) {
    if (arg_count != 1) {
        return make_error("typeof() expects exactly 1 argument", 0, 0);
    }
    
    Value arg = env_pop(env);
    const char* type_name = value_type_name(arg.type);
    Value result = make_string_value_from_cstr(type_name);
    free_value(arg);
    
    env_push(env, result);
    free_value(result);  // env_push copies the value
    return make_success(1);
}

// Include the new library interface
#include "library/library.h"

// Stack-based function call evaluation
EvalResult eval_call_expr_stack(CallExpr* expr, Environment* env) {
    char full_name[256];
    int call_line = 0;
    int call_column = 0;
    ModuleRegistry* registry = get_global_module_registry();
    
    // Handle module.function() syntax (TABLE_DOT expression)
    if (expr->callee->type == EXPR_TABLE_DOT) {
        TableDotExpr* dot_expr = &expr->callee->as.table_dot;
        
        // Check if the table part is a module name (VARIABLE expression)
        if (dot_expr->table->type == EXPR_VARIABLE) {
            const char* module_name = dot_expr->table->as.variable.name.identifier;
            const char* func_name = dot_expr->key.identifier;
            
            // Check if this is a loaded module
            if (registry && is_module_loaded(registry, module_name)) {
                // This is module.function() - construct qualified name
                snprintf(full_name, sizeof(full_name), "%s.%s", module_name, func_name);
                call_line = dot_expr->key.line;
                call_column = dot_expr->key.column;
            } else {
                // Not a module, might be a table method call - not supported yet
                return make_error("Table method calls not yet supported", 0, 0);
            }
        } else {
            return make_error("Complex table method calls not supported", 0, 0);
        }
    }
    // Handle simple function() or qualified_name() syntax  
    else if (expr->callee->type == EXPR_VARIABLE) {
        VariableExpr* var_expr = &expr->callee->as.variable;
        const char* identifier = var_expr->name.identifier ? var_expr->name.identifier : "unknown";
        snprintf(full_name, sizeof(full_name), "%s", identifier);
        call_line = var_expr->name.line;
        call_column = var_expr->name.column;
    }
    else {
        return make_error("Only variable and module.function calls supported", 0, 0);
    }
    
    // Parse qualified name (module.function)
    char module_name[128] = {0};
    char function_name[128] = {0};
    bool is_qualified = parse_qualified_name(full_name, module_name, function_name);
    
    LibraryFunction builtin = NULL;
    
    if (is_qualified) {
        // Qualified function call: module.function()
        if (registry) {
            builtin = lookup_qualified_plugin_function(registry, module_name, function_name);
        }
        
        if (!builtin) {
            char error_msg[512];
            snprintf(error_msg, sizeof(error_msg), "Unknown function '%s' in module '%s'", 
                    function_name, module_name);
            
            return make_error_detailed(
                error_msg,
                "Check if the module is loaded and the function name is correct",
                ERROR_UNDEFINED,
                call_line,
                call_column,
                NULL,
                NULL
            );
        }
    } else {
        // Regular function call: function()
        
        // First check if it's a user-defined function
        bool found = false;
        Value func_value = get_variable(env, full_name, &found);
        if (found && func_value.type == VAL_FUNCTION && func_value.as.function) {
            // TODO: Implement stack-based user function calls
            // For now, fall back to old evaluation
            Expr* call_expr = malloc(sizeof(Expr));
            call_expr->type = EXPR_CALL;
            call_expr->as.call = *expr;
            EvalResult result = evaluate_expr(call_expr, env);
            free(call_expr);
            if (!is_error(result)) {
                env_push(env, result.value);
                free_value(result.value);
                return make_success(1);
            }
            return result;
        }
        
        // First try the new unified library
        LibraryFunction lib_func = lookup_library_function(full_name);
        if (lib_func) {
            // Evaluate arguments onto stack
            for (size_t i = 0; i < expr->arg_count; i++) {
                EvalResult arg_result = evaluate_expr_stack(expr->arguments[i], env);
                if (is_error(arg_result)) {
                    // Clean up any arguments already on stack
                    for (size_t j = 0; j < i; j++) {
                        env_pop(env);
                    }
                    return arg_result;
                }
                // Argument is now on stack
            }
            
            // Push builtin function call onto stack trace
            stack_trace_push(full_name, NULL, 0, 0, true, false, NULL);
            
            // Call the unified library function
            EvalResult result = lib_func(env, expr->arg_count);
            
            // Pop builtin function call from stack trace
            stack_trace_pop();
            
            return result;
        }
        
        // Fall back to stdlib (plugins require namespace)
        builtin = lookup_builtin(full_name);
        
        if (!builtin) {
            // Check if this function exists in any loaded module (to give helpful error)
            char suggested_name[256] = {0};
            ModuleRegistry* reg = get_global_module_registry();
            if (reg) {
                for (size_t i = 0; i < reg->function_count; i++) {
                    const char* qualified = reg->function_table[i].qualified_name;
                    // Check if the qualified name ends with our function name
                    size_t qual_len = strlen(qualified);
                    size_t func_len = strlen(full_name);
                    if (qual_len > func_len && 
                        qualified[qual_len - func_len - 1] == '.' &&
                        strcmp(qualified + qual_len - func_len, full_name) == 0) {
                        // Found it! Save the qualified name
                        snprintf(suggested_name, sizeof(suggested_name), "%s", qualified);
                        break;
                    }
                }
            }
            
            char error_msg[512];
            char suggestion[512];
            
            if (suggested_name[0]) {
                // Function exists in a module - suggest qualified name
                snprintf(error_msg, sizeof(error_msg), 
                        "Function '%s' requires module namespace", full_name);
                snprintf(suggestion, sizeof(suggestion),
                        "Use qualified name: %s()", suggested_name);
            } else {
                // Function truly doesn't exist
                snprintf(error_msg, sizeof(error_msg), "Unknown function '%s'", full_name);
                snprintf(suggestion, sizeof(suggestion),
                        "Check the function name spelling or make sure it's declared before use");
            }
            
            return make_error_detailed(
                error_msg,
                suggestion,
                ERROR_UNDEFINED,
                call_line,
                call_column,
                NULL,
                NULL
            );
        }
    }
    
    // OLD PATH: Evaluate arguments onto stack for adapter
    for (size_t i = 0; i < expr->arg_count; i++) {
        EvalResult arg_result = evaluate_expr_stack(expr->arguments[i], env);
        if (is_error(arg_result)) {
            // Clean up any arguments already on stack
            for (size_t j = 0; j < i; j++) {
                env_pop(env);
            }
            return arg_result;
        }
        // Argument is now on stack
    }
    
    // Push builtin function call onto stack trace
    const char* func_name = is_qualified ? function_name : full_name;
    stack_trace_push(func_name, NULL, 0, 0, true, is_qualified, 
                     is_qualified ? module_name : NULL);
    
    // Call the function using stack-based interface
    EvalResult result = builtin(env, expr->arg_count);
    
    // Pop builtin function call from stack trace
    stack_trace_pop();
    
    return result;
}

// Main stack-based expression evaluator
EvalResult evaluate_expr_stack(Expr* expr, Environment* env) {
    if (!expr) {
        return make_error("Null expression", 0, 0);
    }
    
    switch (expr->type) {
        case EXPR_LITERAL:
            return eval_literal_expr_stack(&expr->as.literal, env);
        case EXPR_VARIABLE:
            return eval_variable_expr_stack(&expr->as.variable, env);
        case EXPR_BINARY:
            return eval_binary_expr_stack(&expr->as.binary, env);
        case EXPR_UNARY:
            return eval_unary_expr_stack(&expr->as.unary, env);
        case EXPR_GROUPING:
            return eval_grouping_expr_stack(&expr->as.grouping, env);
        case EXPR_CALL:
            return eval_call_expr_stack(&expr->as.call, env);
        case EXPR_INCREMENT:
        case EXPR_DECREMENT:
            return eval_increment_expr_stack(&expr->as.increment, env);
        // For now, fall back to old evaluation for unsupported expressions
        // We'll convert these one by one
        default: {
            EvalResult result = evaluate_expr(expr, env);
            if (!is_error(result)) {
                env_push(env, result.value);
                free_value(result.value);
                return make_success(1);
            }
            return result;
        }
    }
}

EvalResult eval_variable_expr(VariableExpr* expr, Environment* env) {
    // Use the extracted identifier string from the token's identifier field
    const char* name = expr->name.identifier ? expr->name.identifier : "unknown";
    
    bool found;
    Value value = get_variable(env, name, &found);
    
    if (!found) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Undefined variable '%s'", name);
        
        return make_error_detailed(
            error_msg,
            "Make sure the variable is declared before use",
            ERROR_UNDEFINED,
            0, 0,  // No line/column info available
            name,
            NULL
        );
    }
    
    return make_success_with_value(value);
}

EvalResult eval_assignment_expr(AssignmentExpr* expr, Environment* env) {
    EvalResult value_result = evaluate_expr(expr->value, env);
    if (is_error(value_result)) {
        return value_result;
    }
    
    char name[256];
    const char* identifier = expr->name.identifier ? expr->name.identifier : "unknown";
    snprintf(name, sizeof(name), "%s", identifier);
    
    if (!assign_variable(env, name, value_result.value)) {
        return make_error_detailed_with_source("Undefined variable in assignment", 
                                               "Make sure the variable is declared before use",
                                               ERROR_UNDEFINED, expr->name.line, expr->name.column, NULL);
    }
    
    return make_success_with_value(value_result.value);
}

EvalResult eval_grouping_expr(GroupingExpr* expr, Environment* env) {
    return evaluate_expr(expr->expression, env);
}

// Helper function to increment/decrement an integer value
Value increment_integer(Value val, bool is_increment, int line, int column, bool* success) {
    *success = false;
    (void)line;
    (void)column;
    
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
    Value current = get_variable(env, var_name, &found);
    
    if (!found) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Undefined variable '%s'", var_name);
        return make_error(error_msg, expr->op.line, expr->op.column);
    }
    
    // Check if it's an integer
    if (current.type != VAL_INTEGER) {
        return make_error("Increment/decrement can only be applied to integers", 
                        expr->op.line, expr->op.column);
    }
    
    // Compute new value
    bool success;
    Value new_value = increment_integer(current, expr->is_increment, 
                                       expr->op.line, expr->op.column, &success);
    
    if (!success) {
        return make_error("Failed to increment/decrement value", 
                        expr->op.line, expr->op.column);
    }
    
    // Update the variable
    if (!assign_variable(env, var_name, new_value)) {
        return make_error("Failed to update variable", expr->op.line, expr->op.column);
    }
    
    // Return appropriate value based on prefix/postfix
    if (expr->is_prefix) {
        return make_success_with_value(new_value);  // Return new value (++i)
    } else {
        return make_success_with_value(current);     // Return old value (i++)
    }
}

EvalResult eval_increment_expr_stack(IncrementExpr* expr, Environment* env) {
    EvalResult result = eval_increment_expr(expr, env);
    if (!is_error(result)) {
        env_push(env, result.value);
        free_value(result.value);
        return make_success(1);
    }
    return result;
}

// Arithmetic operations
EvalResult add_values(Value left, Value right) {
    // Check for table metamethods first
    if (left.type == VAL_TABLE || right.type == VAL_TABLE) {
        Table* table = (left.type == VAL_TABLE) ? left.as.table : right.as.table;
        Value add_method = get_table_metamethod(table, "__add");
        
        if (add_method.type == VAL_FUNCTION) {
            // TODO: Call function metamethod
            // For now, fall through to default behavior
        } else if (add_method.type != VAL_NIL) {
            // Non-function metamethod - treat as error for arithmetic
            return make_error("__add metamethod must be a function", 0, 0);
        }
        // If no metamethod found, continue to default error
    }
    // String concatenation
    if (left.type == VAL_STRING || right.type == VAL_STRING) {
        char* left_str = value_to_string(left);
        char* right_str = value_to_string(right);
        
        if (!left_str || !right_str) {
            free(left_str);
            free(right_str);
            return make_error("Memory allocation failed in string concatenation", 0, 0);
        }
        
        size_t len = strlen(left_str) + strlen(right_str) + 1;
        char* result = malloc(len);
        if (!result) {
            free(left_str);
            free(right_str);
            return make_error("Memory allocation failed", 0, 0);
        }
        
        strcpy(result, left_str);
        strcat(result, right_str);
        
        free(left_str);
        free(right_str);
        
        Value final_result = make_string_value_from_cstr(result);
        free(result);
        return make_success_with_value(final_result);
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
            return make_success_with_value(make_float_value(left_val + right_val));
        } else {
            return make_success_with_value(make_float32_value((float)(left_val + right_val)));
        }
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
        
        return make_success_with_value(make_integer_value(NUM_INT64, left_val + right_val));
    }
    
    if (left.type == VAL_TABLE || right.type == VAL_TABLE) {
        return make_error("Cannot perform arithmetic on tables without __add metamethod", 0, 0);
    }
    return make_error("Cannot add these types", 0, 0);
}

EvalResult subtract_values(Value left, Value right) {
    // Check for table metamethods first
    if (left.type == VAL_TABLE || right.type == VAL_TABLE) {
        Table* table = (left.type == VAL_TABLE) ? left.as.table : right.as.table;
        Value sub_method = get_table_metamethod(table, "__sub");
        
        if (sub_method.type == VAL_FUNCTION) {
            // TODO: Call function metamethod
            // For now, fall through to default behavior
        } else if (sub_method.type != VAL_NIL) {
            return make_error("__sub metamethod must be a function", 0, 0);
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
        
        return make_success_with_value(make_float_value(left_val - right_val));
    }
    
    if (left.type == VAL_INTEGER && right.type == VAL_INTEGER) {
        // Simplified integer subtraction (assuming int32 for now)
        int64_t left_val = left.as.integer.value.i32;
        int64_t right_val = right.as.integer.value.i32;
        return make_success_with_value(make_integer_value(NUM_INT32, left_val - right_val));
    }
    
    if (left.type == VAL_TABLE || right.type == VAL_TABLE) {
        return make_error("Cannot perform arithmetic on tables without __sub metamethod", 0, 0);
    }
    return make_error("Cannot subtract these types", 0, 0);
}

EvalResult multiply_values(Value left, Value right) {
    // Check for table metamethods first
    if (left.type == VAL_TABLE || right.type == VAL_TABLE) {
        Table* table = (left.type == VAL_TABLE) ? left.as.table : right.as.table;
        Value mul_method = get_table_metamethod(table, "__mul");
        
        if (mul_method.type == VAL_FUNCTION) {
            // TODO: Call function metamethod
            // For now, fall through to default behavior
        } else if (mul_method.type != VAL_NIL) {
            return make_error("__mul metamethod must be a function", 0, 0);
        }
    }
    
    // Similar pattern to add_values for numeric multiplication
    if (left.type == VAL_FLOAT64 || right.type == VAL_FLOAT64) {
        double left_val = (left.type == VAL_FLOAT64) ? left.as.float64_val : left.as.integer.value.i32;
        double right_val = (right.type == VAL_FLOAT64) ? right.as.float64_val : right.as.integer.value.i32;
        return make_success_with_value(make_float_value(left_val * right_val));
    }
    
    if (left.type == VAL_INTEGER && right.type == VAL_INTEGER) {
        int64_t left_val = left.as.integer.value.i32;
        int64_t right_val = right.as.integer.value.i32;
        return make_success_with_value(make_integer_value(NUM_INT32, left_val * right_val));
    }
    
    if (left.type == VAL_TABLE || right.type == VAL_TABLE) {
        return make_error("Cannot perform arithmetic on tables without __mul metamethod", 0, 0);
    }
    return make_error("Cannot multiply these types", 0, 0);
}

EvalResult divide_values(Value left, Value right, int line, int column) {
    // Check for table metamethods first
    if (left.type == VAL_TABLE || right.type == VAL_TABLE) {
        Table* table = (left.type == VAL_TABLE) ? left.as.table : right.as.table;
        Value div_method = get_table_metamethod(table, "__div");
        
        if (div_method.type == VAL_FUNCTION) {
            // TODO: Call function metamethod
            // For now, fall through to default behavior
        } else if (div_method.type != VAL_NIL) {
            return make_error("__div metamethod must be a function", 0, 0);
        }
        
        // If tables without metamethods, return error
        return make_error("Cannot perform arithmetic on tables without __div metamethod", 0, 0);
    }
    
    // Division always returns float to handle fractions
    double left_val = (left.type == VAL_FLOAT64) ? left.as.float64_val : 
                      (left.type == VAL_INTEGER) ? left.as.integer.value.i32 : 0.0;
    double right_val = (right.type == VAL_FLOAT64) ? right.as.float64_val : 
                       (right.type == VAL_INTEGER) ? right.as.integer.value.i32 : 0.0;
    
    if (right_val == 0.0) {
        return make_error_detailed_with_source(
            "Division by zero",
            "Check that the divisor is not zero before performing division",
            ERROR_DIVISION,
            line, column,
            NULL
        );
    }
    
    return make_success_with_value(make_float_value(left_val / right_val));
}

EvalResult modulo_values(Value left, Value right, int line, int column) {
    // Check for table metamethods first
    if (left.type == VAL_TABLE || right.type == VAL_TABLE) {
        Table* table = (left.type == VAL_TABLE) ? left.as.table : right.as.table;
        Value mod_method = get_table_metamethod(table, "__mod");
        
        if (mod_method.type == VAL_FUNCTION) {
            // TODO: Call function metamethod
            // For now, fall through to default behavior
        } else if (mod_method.type != VAL_NIL) {
            return make_error("__mod metamethod must be a function", 0, 0);
        }
        
        // If tables without metamethods, return error
        return make_error("Cannot perform modulo on tables without __mod metamethod", 0, 0);
    }
    
    // Handle integer modulo (preferred for exact results)
    if (left.type == VAL_INTEGER && right.type == VAL_INTEGER) {
        int32_t left_val = left.as.integer.value.i32;
        int32_t right_val = right.as.integer.value.i32;
        
        if (right_val == 0) {
            return make_error_detailed_with_source(
                "Modulo by zero",
                "Check that the divisor is not zero before performing modulo",
                ERROR_DIVISION,
                line, column,
                NULL
            );
        }
        
        return make_success_with_value(make_integer_value(NUM_INT32, left_val % right_val));
    }
    
    // Handle float modulo using fmod
    if ((left.type == VAL_FLOAT64 || left.type == VAL_INTEGER) &&
        (right.type == VAL_FLOAT64 || right.type == VAL_INTEGER)) {
        double left_val = (left.type == VAL_FLOAT64) ? left.as.float64_val : 
                          (double)left.as.integer.value.i32;
        double right_val = (right.type == VAL_FLOAT64) ? right.as.float64_val : 
                           (double)right.as.integer.value.i32;
        
        if (right_val == 0.0) {
            return make_error_detailed_with_source(
                "Modulo by zero",
                "Check that the divisor is not zero before performing modulo",
                ERROR_DIVISION,
                line, column,
                NULL
            );
        }
        
        return make_success_with_value(make_float_value(fmod(left_val, right_val)));
    }
    
    return make_error("Cannot modulo these types", 0, 0);
}

EvalResult compare_values(Value left, Value right, TokenType op) {
    bool result = false;
    
    // Check for table metamethods first
    if (left.type == VAL_TABLE || right.type == VAL_TABLE) {
        Table* table = (left.type == VAL_TABLE) ? left.as.table : right.as.table;
        const char* metamethod_name = NULL;
        
        switch (op) {
            case TOKEN_EQUAL_EQUAL:
            case TOKEN_BANG_EQUAL:
                metamethod_name = "__eq";
                break;
            case TOKEN_LESS:
                metamethod_name = "__lt";
                break;
            case TOKEN_LESS_EQUAL:
                metamethod_name = "__le";
                break;
            case TOKEN_GREATER:
            case TOKEN_GREATER_EQUAL:
                // For > and >=, we check if the right operand has the metamethod
                if (right.type == VAL_TABLE) {
                    table = right.as.table;
                    metamethod_name = (op == TOKEN_GREATER) ? "__lt" : "__le";
                    // Note: a > b becomes b < a, a >= b becomes b <= a
                }
                break;
            default:
                break;
        }
        
        if (metamethod_name) {
            Value compare_method = get_table_metamethod(table, metamethod_name);
            
            if (compare_method.type == VAL_FUNCTION) {
                // TODO: Call function metamethod
                // For now, fall through to default behavior
            } else if (compare_method.type != VAL_NIL) {
                return make_error("Comparison metamethod must be a function", 0, 0);
            }
        }
        
        // If no metamethod found and tables are involved, handle equality specially
        if (op == TOKEN_EQUAL_EQUAL || op == TOKEN_BANG_EQUAL) {
            // Use default equality for tables
        } else {
            // Other comparisons require metamethods for tables
            return make_error("Cannot compare tables without appropriate metamethod", 0, 0);
        }
    }
    
    // Equality comparison
    if (op == TOKEN_EQUAL_EQUAL) {
        result = values_equal(left, right);
    } else if (op == TOKEN_BANG_EQUAL) {
        result = !values_equal(left, right);
    } else {
        // Numeric comparison
        double left_val = 0.0, right_val = 0.0;
        
        if (left.type == VAL_FLOAT64) {
            left_val = left.as.float64_val;
        } else if (left.type == VAL_INTEGER) {
            left_val = left.as.integer.value.i32; // Simplified
        } else {
            return make_error("Cannot compare non-numeric types", 0, 0);
        }
        
        if (right.type == VAL_FLOAT64) {
            right_val = right.as.float64_val;
        } else if (right.type == VAL_INTEGER) {
            right_val = right.as.integer.value.i32; // Simplified
        } else {
            return make_error("Cannot compare non-numeric types", 0, 0);
        }
        
        switch (op) {
            case TOKEN_GREATER:       result = left_val > right_val; break;
            case TOKEN_GREATER_EQUAL: result = left_val >= right_val; break;
            case TOKEN_LESS:          result = left_val < right_val; break;
            case TOKEN_LESS_EQUAL:    result = left_val <= right_val; break;
            default:
                return make_error("Unknown comparison operator", 0, 0);
        }
    }
    
    return make_success_with_value(make_bool_value(result));
}

EvalResult eval_binary_expr(BinaryExpr* expr, Environment* env) {
    EvalResult left_result = evaluate_expr(expr->left, env);
    if (is_error(left_result)) {
        return left_result;
    }
    
    EvalResult right_result = evaluate_expr(expr->right, env);
    if (is_error(right_result)) {
        return right_result;
    }
    
    switch (expr->op.type) {
        case TOKEN_PLUS:
            return add_values(left_result.value, right_result.value);
        case TOKEN_MINUS:
            return subtract_values(left_result.value, right_result.value);
        case TOKEN_STAR:
            return multiply_values(left_result.value, right_result.value);
        case TOKEN_SLASH:
                return divide_values(left_result.value, right_result.value, expr->op.line, expr->op.column);
        case TOKEN_PERCENT:
                return modulo_values(left_result.value, right_result.value, expr->op.line, expr->op.column);
        case TOKEN_EQUAL_EQUAL:
        case TOKEN_BANG_EQUAL:
        case TOKEN_GREATER:
        case TOKEN_GREATER_EQUAL:
        case TOKEN_LESS:
        case TOKEN_LESS_EQUAL:
            return compare_values(left_result.value, right_result.value, expr->op.type);
        case TOKEN_AND:
        case TOKEN_AND_AND:
            if (!is_truthy(left_result.value)) {
                return make_success_with_value(left_result.value);
            }
            return make_success_with_value(right_result.value);
        case TOKEN_OR:
        case TOKEN_OR_OR:
            if (is_truthy(left_result.value)) {
                return make_success_with_value(left_result.value);
            }
            return make_success_with_value(right_result.value);
        default:
            return make_error_with_source("Unknown binary operator", expr->op.line, expr->op.column);
    }
}

EvalResult eval_unary_expr(UnaryExpr* expr, Environment* env) {
    EvalResult operand_result = evaluate_expr(expr->right, env);
    if (is_error(operand_result)) {
        return operand_result;
    }
    
    switch (expr->op.type) {
        case TOKEN_MINUS:
            if (operand_result.value.type == VAL_FLOAT64) {
                return make_success_with_value(make_float_value(-operand_result.value.as.float64_val));
            } else if (operand_result.value.type == VAL_INTEGER) {
                // Extract value properly based on the actual type, but result should be int64_t
                int64_t value = 0;
                switch (operand_result.value.as.integer.num_type) {
                    case NUM_INT8:   value = operand_result.value.as.integer.value.i8; break;
                    case NUM_UINT8:  value = operand_result.value.as.integer.value.u8; break;
                    case NUM_INT16:  value = operand_result.value.as.integer.value.i16; break;
                    case NUM_UINT16: value = operand_result.value.as.integer.value.u16; break;
                    case NUM_INT32:  value = operand_result.value.as.integer.value.i32; break;
                    case NUM_UINT32: value = operand_result.value.as.integer.value.u32; break;
                    case NUM_INT64:  value = operand_result.value.as.integer.value.i64; break;
                    case NUM_UINT64: value = operand_result.value.as.integer.value.u64; break;
                    default: value = operand_result.value.as.integer.value.i32; break;
                }
                return make_success_with_value(make_integer_value(NUM_INT64, -value));
            } else {
                return make_error("Cannot negate non-numeric value", expr->op.line, expr->op.column);
            }
        case TOKEN_PLUS:
            // Unary plus is identity for numbers
            if (operand_result.value.type == VAL_FLOAT64 || operand_result.value.type == VAL_INTEGER) {
                return make_success_with_value(operand_result.value);
            } else {
                return make_error("Cannot apply unary plus to non-numeric value", expr->op.line, expr->op.column);
            }
        case TOKEN_BANG:
        case TOKEN_NOT:
            return make_success_with_value(make_bool_value(!is_truthy(operand_result.value)));
        default:
            return make_error("Unknown unary operator", expr->op.line, expr->op.column);
    }
}

// Main expression evaluator
EvalResult evaluate_expr(Expr* expr, Environment* env) {
    if (!expr) {
        return make_error("Null expression", 0, 0);
    }
    
    switch (expr->type) {
        case EXPR_LITERAL:
            return eval_literal_expr(&expr->as.literal, env);
        case EXPR_VARIABLE:
            return eval_variable_expr(&expr->as.variable, env);
        case EXPR_ASSIGNMENT:
            return eval_assignment_expr(&expr->as.assignment, env);
        case EXPR_BINARY:
            return eval_binary_expr(&expr->as.binary, env);
        case EXPR_UNARY:
            return eval_unary_expr(&expr->as.unary, env);
        case EXPR_GROUPING:
            return eval_grouping_expr(&expr->as.grouping, env);
        case EXPR_CALL:
            return eval_call_expr(&expr->as.call, env);
        case EXPR_TABLE_LITERAL:
            return eval_table_literal_expr(&expr->as.table_literal, env);
        case EXPR_TABLE_INDEX:
            return eval_table_index_expr(&expr->as.table_index, env);
        case EXPR_TABLE_DOT:
            return eval_table_dot_expr(&expr->as.table_dot, env);
        case EXPR_ARRAY_LITERAL:
            return eval_array_literal_expr(&expr->as.array_literal, env);
        case EXPR_ARRAY_INDEX:
            return eval_array_index_expr(&expr->as.array_index, env);
        case EXPR_ENUM_ACCESS:
            return eval_enum_access_expr(&expr->as.enum_access, env);
        case EXPR_INCREMENT:
        case EXPR_DECREMENT:
            return eval_increment_expr(&expr->as.increment, env);
        default:
            return make_error("Unknown expression type", 0, 0);
    }
}

// Statement evaluation
EvalResult eval_expression_stmt(ExpressionStmt* stmt, Environment* env) {
    return evaluate_expr(stmt->expression, env);
}

EvalResult eval_var_stmt(VarStmt* stmt, Environment* env) {
    Value value = make_nil_value();
    
    if (stmt->initializer) {
        EvalResult init_result = evaluate_expr(stmt->initializer, env);
        if (is_error(init_result)) {
            return init_result;
        }
        value = init_result.value;
    }
    
    // Validate and convert type if annotation is provided
    if (stmt->type_hint.is_annotated) {
        TypeConversionResult conversion = validate_and_convert_value(value, stmt->type_hint, global_type_config);
        if (!conversion.success) {
            return make_error_detailed_with_source(
                conversion.error_message ? conversion.error_message : "Type validation failed",
                "Check that the value matches the declared type",
                ERROR_TYPE,
                stmt->name.line,
                stmt->name.column,
                "eval_var_stmt"
            );
        }
        
        // Use the converted value
        value = conversion.converted_value;
        
        // Warn about conversions if enabled
        if (conversion.was_converted && global_type_config.warn_on_conversion) {
            printf("Warning: Implicit type conversion in variable declaration at line %d\n", stmt->name.line);
        }
        
        // Free error message if any
        free(conversion.error_message);
    }
    
    char name[256];
    const char* identifier = stmt->name.identifier ? stmt->name.identifier : "unknown";
    snprintf(name, sizeof(name), "%s", identifier);
    
    // Check for namespace collision: an enum with the same name shouldn't exist
    char enum_var_name[256];
    snprintf(enum_var_name, sizeof(enum_var_name), "__enum_%s", identifier);
    bool enum_exists = false;
    get_variable(env, enum_var_name, &enum_exists);
    
    if (enum_exists) {
        
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Name collision: enum '%s' already exists, cannot declare variable with the same name", identifier);
        return make_error(error_msg, stmt->name.line, stmt->name.column);
    }
    
    define_variable(env, name, value);
    
    return make_success_with_value(make_nil_value());
}

EvalResult eval_block_stmt(BlockStmt* stmt, Environment* env) {
    Environment* block_env = create_environment(env);
    if (!block_env) {
        return make_error("Memory allocation failed", 0, 0);
    }
    
    EvalResult result = make_success_with_value(make_nil_value());
    
    for (size_t i = 0; i < stmt->count; i++) {
        result = evaluate_stmt(stmt->statements[i], block_env);
        if (is_error(result)) {
            break;
        }
        
        // If a return statement was executed, break out of the block
        if (result.has_returned) {
            break;
        }
        
        // If a break or continue statement was executed, break out of the block
        // These will be handled by the containing loop
        if (result.has_break || result.has_continue) {
            break;
        }
    }
    
    free_environment(block_env);
    return result;
}

EvalResult eval_if_stmt(IfStmt* stmt, Environment* env) {
    EvalResult condition_result = evaluate_expr(stmt->condition, env);
    if (is_error(condition_result)) {
        return condition_result;
    }
    
    if (is_truthy(condition_result.value)) {
        return evaluate_stmt(stmt->then_branch, env);  // This will propagate has_returned
    } else if (stmt->else_branch) {
        return evaluate_stmt(stmt->else_branch, env);  // This will propagate has_returned
    }
    
    return make_success_with_value(make_nil_value());
}

EvalResult eval_while_stmt(WhileStmt* stmt, Environment* env) {
    EvalResult result = make_success_with_value(make_nil_value());
    
    while (true) {
        EvalResult condition_result = evaluate_expr(stmt->condition, env);
        if (is_error(condition_result)) {
            return condition_result;
        }
        
        if (!is_truthy(condition_result.value)) {
            break;
        }
        
        result = evaluate_stmt(stmt->body, env);
        if (is_error(result)) {
            return result;
        }
        
        // Handle break statement
        if (result.has_break) {
            result.has_break = false;  // Reset break flag
            break;
        }
        
        // Handle continue statement
        if (result.has_continue) {
            result.has_continue = false;  // Reset continue flag
            continue;  // Go to next iteration
        }
        
        // Handle return statement
        if (result.has_returned) {
            return result;  // Propagate return
        }
    }
    
    return result;
}

// Built-in functions now implemented in stdlib.c

// Global module registry (will be initialized by main.c)
static ModuleRegistry* global_registry = NULL;

void set_global_module_registry(ModuleRegistry* registry) {
    global_registry = registry;
}

ModuleRegistry* get_global_module_registry(void) {
    return global_registry;
}

// Built-in function lookup - first checks plugins, then falls back to library
LibraryFunction lookup_builtin(const char* name) {
    // NAMESPACE ENFORCEMENT: Do NOT search plugins by unqualified name
    // Plugin functions MUST be accessed via qualified name (module.function)
    // This ensures no naming conflicts between modules
    
    // Only check the standard library (global functions like print, typeof, etc.)
    return lookup_library_function(name);
}

// Plugin-aware function lookup
LibraryFunction lookup_plugin_function(ModuleRegistry* registry, const char* name) {
    if (!registry) return NULL;
    
    PluginFunction* plugin_func = lookup_function(registry, name);
    return plugin_func ? plugin_func->function : NULL;
}

// Qualified function lookup (module.function)
LibraryFunction lookup_qualified_plugin_function(ModuleRegistry* registry, 
                                                const char* module_name, 
                                                const char* function_name) {
    if (!registry) return NULL;
    
    PluginFunction* plugin_func = lookup_qualified_function(registry, module_name, function_name);
    return plugin_func ? plugin_func->function : NULL;
}

void register_builtins(Environment* env) {
    // Built-ins are looked up dynamically, so we don't need to register them
    // in the environment. They're handled specially in eval_call_expr.
    (void)env;
}

EvalResult eval_call_expr(CallExpr* expr, Environment* env) {
    return eval_call_expr_with_registry(expr, env, global_registry);
}

EvalResult eval_call_expr_with_registry(CallExpr* expr, Environment* env, ModuleRegistry* registry) {
    char full_name[256];
    int call_line = 0;
    int call_column = 0;
    
    // Handle module.function() syntax (TABLE_DOT expression)
    if (expr->callee->type == EXPR_TABLE_DOT) {
        TableDotExpr* dot_expr = &expr->callee->as.table_dot;
        
        // Check if the table part is a module name (VARIABLE expression)
        if (dot_expr->table->type == EXPR_VARIABLE) {
            const char* module_name = dot_expr->table->as.variable.name.identifier;
            const char* func_name = dot_expr->key.identifier;
            
            // Check if this is a loaded module
            if (registry && is_module_loaded(registry, module_name)) {
                // This is module.function() - construct qualified name
                snprintf(full_name, sizeof(full_name), "%s.%s", module_name, func_name);
                call_line = dot_expr->key.line;
                call_column = dot_expr->key.column;
            } else {
                // Not a module, might be a table method call - not supported yet
                return make_error("Table method calls not yet supported", 0, 0);
            }
        } else {
            return make_error("Complex table method calls not supported", 0, 0);
        }
    }
    // Handle simple function() or qualified_name() syntax  
    else if (expr->callee->type == EXPR_VARIABLE) {
        VariableExpr* var_expr = &expr->callee->as.variable;
        const char* identifier = var_expr->name.identifier ? var_expr->name.identifier : "unknown";
        snprintf(full_name, sizeof(full_name), "%s", identifier);
        call_line = var_expr->name.line;
        call_column = var_expr->name.column;
    }
    else {
        return make_error("Only variable and module.function calls supported", 0, 0);
    }
    
    // Parse qualified name (module.function)
    char module_name[128] = {0};
    char function_name[128] = {0};
    bool is_qualified = parse_qualified_name(full_name, module_name, function_name);
    
    LibraryFunction builtin = NULL;
    
    if (is_qualified) {
        // Qualified function call: module.function()
        if (registry) {
            builtin = lookup_qualified_plugin_function(registry, module_name, function_name);
        }
        
        if (!builtin) {
            char error_msg[512];
            snprintf(error_msg, sizeof(error_msg), "Unknown function '%s' in module '%s'", 
                    function_name, module_name);
            
            return make_error_detailed(
                error_msg,
                "Check if the module is loaded and the function name is correct",
                ERROR_UNDEFINED,
                call_line,
                call_column,
                NULL,
                NULL
            );
        }
    } else {
        // Regular function call: function()
        
        // First check if it's a user-defined function
        bool found = false;
        Value func_value = get_variable(env, full_name, &found);
        if (found && func_value.type == VAL_FUNCTION && func_value.as.function) {
            return call_user_function(func_value.as.function, expr->arguments, expr->arg_count, env);
        }
        
        // Then check stdlib (plugins require namespace)
        builtin = lookup_builtin(full_name);
        
        if (!builtin) {
            // Check if this function exists in any loaded module (to give helpful error)
            char suggested_name[256] = {0};
            if (registry) {
                for (size_t i = 0; i < registry->function_count; i++) {
                    const char* qualified = registry->function_table[i].qualified_name;
                    // Check if the qualified name ends with our function name
                    size_t qual_len = strlen(qualified);
                    size_t func_len = strlen(full_name);
                    if (qual_len > func_len && 
                        qualified[qual_len - func_len - 1] == '.' &&
                        strcmp(qualified + qual_len - func_len, full_name) == 0) {
                        // Found it! Save the qualified name
                        snprintf(suggested_name, sizeof(suggested_name), "%s", qualified);
                        break;
                    }
                }
            }
            
            char error_msg[512];
            char suggestion[512];
            
            if (suggested_name[0]) {
                // Function exists in a module - suggest qualified name
                snprintf(error_msg, sizeof(error_msg), 
                        "Function '%s' requires module namespace", full_name);
                snprintf(suggestion, sizeof(suggestion),
                        "Use qualified name: %s()", suggested_name);
            } else {
                // Function truly doesn't exist
                snprintf(error_msg, sizeof(error_msg), "Unknown function '%s'", full_name);
                snprintf(suggestion, sizeof(suggestion),
                        "Check the function name spelling or make sure it's declared before use");
            }
            
            return make_error_detailed(
                error_msg,
                suggestion,
                ERROR_UNDEFINED,
                call_line,
                call_column,
                NULL,
                NULL
            );
        }
    }
    
    // Evaluate and push arguments onto stack
    for (size_t i = 0; i < expr->arg_count; i++) {
        EvalResult arg_result = evaluate_expr(expr->arguments[i], env);
        if (is_error(arg_result)) {
            // Pop any arguments already pushed
            for (size_t j = 0; j < i; j++) {
                env_pop(env);
            }
            return arg_result;
        }
        env_push(env, arg_result.value);
    }
    
    // Push builtin function call onto stack trace
    const char* func_name = is_qualified ? function_name : full_name;
    stack_trace_push(func_name, NULL, 0, 0, true, is_qualified, 
                     is_qualified ? module_name : NULL);
    
    // Call the function using stack-based interface
    EvalResult result = builtin(env, expr->arg_count);
    
    // Pop builtin function call from stack trace
    stack_trace_pop();
    
    // Handle library functions that use stack-based returns
    if (!is_error(result) && result.return_count == 0) {
        // Function pushed no values to stack, return nil for compatibility with old evaluation
        result.value = make_nil_value();
        return result;
    } else if (!is_error(result) && result.return_count == 1) {
        // Function pushed one value to stack, pop it and return as traditional value
        result.value = env_pop(env);
        return result;
    } else if (!is_error(result) && result.return_count > 1) {
        // Function returned multiple values - for now, just return the top one
        // TODO: Handle multiple return values properly
        result.value = env_pop(env);
        // Pop the rest for now
        for (int i = 1; i < result.return_count; i++) {
            Value discarded = env_pop(env);
            free_value(discarded);
        }
        return result;
    }
    
    return result;
}

// Main statement evaluator
EvalResult evaluate_stmt(Stmt* stmt, Environment* env) {
    if (!stmt) {
        return make_error("Null statement", 0, 0);
    }
    
    switch (stmt->type) {
        case STMT_EXPRESSION:
            return eval_expression_stmt(&stmt->as.expression, env);
        case STMT_VAR:
            return eval_var_stmt(&stmt->as.var, env);
        case STMT_BLOCK:
            return eval_block_stmt(&stmt->as.block, env);
        case STMT_IF:
            return eval_if_stmt(&stmt->as.if_stmt, env);
        case STMT_WHILE:
            return eval_while_stmt(&stmt->as.while_stmt, env);
        case STMT_FOR:
            return eval_for_stmt(&stmt->as.for_stmt, env);
        case STMT_FUNCTION:
            return eval_function_stmt(&stmt->as.function, env);
        case STMT_RETURN:
            return eval_return_stmt(&stmt->as.return_stmt, env);
        case STMT_SWITCH:
            return eval_switch_stmt(&stmt->as.switch_stmt, env);
        case STMT_BREAK:
            return eval_break_stmt(&stmt->as.break_stmt, env);
        case STMT_CONTINUE:
            return eval_continue_stmt(&stmt->as.continue_stmt, env);
        case STMT_IMPORT:
            return eval_import_stmt(&stmt->as.import_stmt, env);
        case STMT_ENUM:
            return eval_enum_stmt(&stmt->as.enum_stmt, env);
        default:
            return make_error("Unknown statement type", 0, 0);
    }
}

EvalResult eval_for_stmt(ForStmt* stmt, Environment* env) {
    // Create new environment for the for loop scope
    Environment* for_env = create_environment(env);
    if (!for_env) {
        return make_error("Memory allocation failed", 0, 0);
    }
    
    EvalResult result = make_success_with_value(make_nil_value());
    
    // Execute initializer
    if (stmt->initializer) {
        result = evaluate_stmt(stmt->initializer, for_env);
        if (is_error(result)) {
            free_environment(for_env);
            return result;
        }
    }
    
    // Loop
    while (true) {
        // Check condition
        if (stmt->condition) {
            EvalResult condition_result = evaluate_expr(stmt->condition, for_env);
            if (is_error(condition_result)) {
                free_environment(for_env);
                return condition_result;
            }
            
            if (!is_truthy(condition_result.value)) {
                break;
            }
        }
        
        // Execute body
        result = evaluate_stmt(stmt->body, for_env);
        if (is_error(result)) {
            free_environment(for_env);
            return result;
        }
        
        // Handle break statement
        if (result.has_break) {
            result.has_break = false;  // Reset break flag
            break;
        }
        
        // Handle return statement
        if (result.has_returned) {
            free_environment(for_env);
            return result;  // Propagate return
        }
        
        // Execute increment (always executed, even on continue)
        if (stmt->increment) {
            EvalResult increment_result = evaluate_expr(stmt->increment, for_env);
            if (is_error(increment_result)) {
                free_environment(for_env);
                return increment_result;
            }
        }
        
        // Handle continue statement (after increment)
        if (result.has_continue) {
            result.has_continue = false;  // Reset continue flag
            continue;  // Go to next iteration
        }
    }
    
    free_environment(for_env);
    return result;
}

// Function statement evaluation: func name(params) { body }
EvalResult eval_function_stmt(FunctionStmt* stmt, Environment* env) {
    // Create a function object
    MobiusFunction* function = malloc(sizeof(MobiusFunction));
    if (!function) {
        return make_error("Memory allocation failed", 0, 0);
    }
    
    // Extract function name from token
    function->name = extract_identifier_name(&stmt->name);
    if (!function->name) {
        free(function);
        return make_error("Failed to extract function name", 0, 0);
    }
    
    // Extract parameter names from tokens
    function->param_count = stmt->param_count;
    if (stmt->param_count > 0) {
        function->param_names = malloc(stmt->param_count * sizeof(char*));
        if (!function->param_names) {
            free(function->name);
            free(function);
            return make_error("Memory allocation failed", 0, 0);
        }
        
        for (size_t i = 0; i < stmt->param_count; i++) {
            function->param_names[i] = extract_identifier_name(&stmt->params[i]);
            if (!function->param_names[i]) {
                // Cleanup previously allocated names
                for (size_t j = 0; j < i; j++) {
                    free(function->param_names[j]);
                }
                free(function->param_names);
                free(function->name);
                free(function);
                return make_error("Failed to extract parameter name", 0, 0);
            }
        }
    } else {
        function->param_names = NULL;
    }
    
    // Create a copy of the body array and retain references to AST body statements
    // This ensures the function has its own copy and proper memory management
    function->body_count = stmt->body_count;
    if (stmt->body_count > 0) {
        function->body = malloc(stmt->body_count * sizeof(Stmt*));
        if (!function->body) {
            // Handle allocation failure - cleanup parameter names
            if (function->param_names) {
                for (size_t j = 0; j < function->param_count; j++) {
                    free(function->param_names[j]);
                }
                free(function->param_names);
            }
            free(function->name);
            free(function);
            return make_error_detailed("Memory allocation failed for function body", NULL, ERROR_RUNTIME, 0, 0, NULL, NULL);
        }
        
        // Copy the body array and retain each statement
        for (size_t i = 0; i < stmt->body_count; i++) {
            function->body[i] = stmt->body[i];
            ast_retain_stmt(stmt->body[i]);
        }
    } else {
        function->body = NULL;
    }
    function->closure = env;  // Capture current environment as closure
    function->ref_count = 1;  // Initialize reference count
    
    // Create function value
    Value func_value = make_function_value(function);
    
    // Define the function in the current environment using the stored name
    define_variable(env, function->name, func_value);
    
    return make_success_with_value(make_nil_value());
}

// Return statement evaluation: return [expression]
EvalResult eval_return_stmt(ReturnStmt* stmt, Environment* env) {
    Value return_value = make_nil_value();
    
    if (stmt->value) {
        EvalResult result = evaluate_expr(stmt->value, env);
        if (is_error(result)) {
            return result;
        }
        return_value = result.value;
    }
    
    // Create a result with the return flag set
    EvalResult result = make_success_with_value(return_value);
    result.has_returned = true;
    return result;
}

// Call a user-defined function
EvalResult call_user_function(MobiusFunction* function, Expr** arguments, size_t arg_count, Environment* env) {
    // Check argument count
    if (arg_count != function->param_count) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), 
                "Function '%s' expects %zu arguments but got %zu",
                function->name ? function->name : "anonymous",
                function->param_count, arg_count);
                
        return make_error_detailed(
            error_msg,
            "Check the function definition for the correct number of parameters",
            ERROR_ARGUMENT,
            0, 0,
            function->name,
            NULL
        );
    }
    
    // Push function call onto stack trace
    stack_trace_push(function->name ? function->name : "anonymous", 
                     NULL, // filename - TODO: add filename tracking
                     0, 0, // line, column - TODO: add call site tracking
                     false, false, NULL); // not builtin, not plugin
    
    // Create new environment for function execution (with closure as parent)
    Environment* func_env = create_environment(function->closure);
    
    // Evaluate and bind arguments to parameters
    for (size_t i = 0; i < arg_count; i++) {
        EvalResult arg_result = evaluate_expr(arguments[i], env);
        if (is_error(arg_result)) {
            stack_trace_pop();
            free_environment(func_env);
            return arg_result;
        }
        
        // Extract parameter name
        // Bind parameter using the stored parameter name
        define_variable(func_env, function->param_names[i], arg_result.value);
    }
    
    // Execute function body
    EvalResult result = make_success_with_value(make_nil_value());
    
    for (size_t i = 0; i < function->body_count; i++) {
        result = evaluate_stmt(function->body[i], func_env);
        if (is_error(result)) {
            stack_trace_pop();
            free_environment(func_env);
            return result;
        }
        
        // Check if a return statement was executed (including nested in control structures)
        if (result.has_returned) {
            break;  // Return statement found, exit function body execution
        }
    }
    
    // Deep copy the result value before freeing the function environment
    // This prevents use-after-free bugs when the result contains references
    // to values that will be freed with the function environment
    EvalResult final_result;
    if (is_error(result)) {
        final_result = result; // Errors don't need deep copying
    } else {
        final_result = make_success_with_value(copy_value(result.value));
    }
    
    // Pop function call from stack trace
    stack_trace_pop();
    
    free_environment(func_env);
    return final_result;
}

// Global source code context for error reporting
static const char* global_source_code = NULL;

void set_source_context(const char* source) {
    global_source_code = source;
}

const char* get_source_context(void) {
    return global_source_code;
}


// Enhanced error creation with source line extraction
EvalResult make_error_with_source(const char* message, int line, int column) {
    const char* source_line = NULL;
    if (global_source_code && line > 0) {
        source_line = extract_source_line(global_source_code, line);
    }
    
    return make_error_detailed(message, NULL, ERROR_RUNTIME, line, column, NULL, source_line);
}

EvalResult make_error_detailed_with_source(const char* message, const char* suggestion, 
                                          ErrorCategory category, int line, int column,
                                          const char* function_name) {
    const char* source_line = NULL;
    if (global_source_code && line > 0) {
        source_line = extract_source_line(global_source_code, line);
    }
    
    return make_error_detailed(message, suggestion, category, line, column, function_name, source_line);
}

// Evaluate a program (array of statements)
EvalResult evaluate_program(Stmt** statements, size_t count, Environment* env) {
    EvalResult result = make_success_with_value(make_nil_value());
    
    for (size_t i = 0; i < count; i++) {
        result = evaluate_stmt(statements[i], env);
        if (is_error(result)) {
            print_runtime_error(result.error);
            // Continue execution instead of breaking for better error recovery
            // In a real language, you might want to break on certain error types
            result = make_success_with_value(make_nil_value()); // Reset result for next statement
        }
    }
    
    return result;
}

// Table evaluation functions
EvalResult eval_table_literal_expr(TableLiteralExpr* expr, Environment* env) {
    Table* table = create_table(INITIAL_TABLE_CAPACITY);
    if (!table) {
        return make_error("Failed to create table", 0, 0);
    }
    
    // Evaluate each key-value pair
    for (size_t i = 0; i < expr->pair_count; i++) {
        TablePair* pair = &expr->pairs[i];
        
        Value key;
        Value value;
        
        // Evaluate the value
        EvalResult value_result = evaluate_expr(pair->value, env);
        if (is_error(value_result)) {
            free_table(table);
            return value_result;
        }
        value = value_result.value;
        
        // Evaluate the key (if provided, otherwise use index)
        if (pair->key) {
            EvalResult key_result = evaluate_expr(pair->key, env);
            if (is_error(key_result)) {
                free_table(table);
                free_value(value);
                return key_result;
            }
            key = key_result.value;
        } else {
            // Use index as key for array-style initialization
            key = make_integer_value(NUM_INT64, (int64_t)i);
        }
        
        // Set the key-value pair in the table
        if (!table_set(table, key, value)) {
            free_table(table);
            free_value(key);
            free_value(value);
            return make_error("Failed to set table entry", 0, 0);
        }
    }
    
    return make_success_with_value(make_table_value(table));
}

EvalResult eval_table_index_expr(TableIndexExpr* expr, Environment* env) {
    // Evaluate the table expression
    EvalResult table_result = evaluate_expr(expr->table, env);
    if (is_error(table_result)) {
        return table_result;
    }
    
    if (table_result.value.type != VAL_TABLE) {
        free_value(table_result.value);
        return make_error("Cannot index non-table value", 0, 0);
    }
    
    // Evaluate the index expression
    EvalResult index_result = evaluate_expr(expr->index, env);
    if (is_error(index_result)) {
        free_value(table_result.value);
        return index_result;
    }
    
    // Get the value from the table
    Value result = table_get(table_result.value.as.table, index_result.value);
    
    // Clean up
    free_value(index_result.value);
    // Don't free table_result.value here - the table is still referenced
    
    return make_success_with_value(result);
}

EvalResult eval_table_dot_expr(TableDotExpr* expr, Environment* env) {
    // Evaluate the table expression
    EvalResult table_result = evaluate_expr(expr->table, env);
    
    // Check if this might be an enum access attempt when variable lookup fails
    if (is_error(table_result) && expr->table->type == EXPR_VARIABLE) {
        // The variable doesn't exist, might be an enum name
        // Only try enum access if there's actually an enum with this name
        const char* enum_name = expr->table->as.variable.name.identifier;
        const char* member_name = expr->key.identifier;
        
        if (enum_name && member_name) {
            // First check if an enum with this name exists
            char enum_var_name[256];
            snprintf(enum_var_name, sizeof(enum_var_name), "__enum_%s", enum_name);
            
            bool enum_found = false;
            Value enum_value = get_variable(env, enum_var_name, &enum_found);
            
            // Only proceed with enum access if the enum actually exists
            if (enum_found && enum_value.type == VAL_USERDATA && 
                strcmp(enum_value.as.userdata.type_name, "enum_definition") == 0) {
                
                EnumDefinition* enum_def = (EnumDefinition*)enum_value.as.userdata.ptr;
                if (enum_def) {
                    // Find the enum member
                    EnumMember* member = enum_definition_find_member(enum_def, member_name);
                    if (member) {
                        // Create enum value
                        Value result = make_enum_value(enum_def, member->value);
                        return make_success_with_value(result);
                    } else {
                        char error_msg[256];
                        snprintf(error_msg, sizeof(error_msg), "Undefined enum member '%s.%s'", enum_name, member_name);
                        return make_error(error_msg, 0, 0);
                    }
                }
            }
        }
        
        // Not an enum access or enum doesn't exist, return the original variable error
        return table_result;
    }
    
    if (is_error(table_result)) {
        return table_result;
    }
    
    
    if (table_result.value.type != VAL_TABLE) {
        free_value(table_result.value);
        return make_error("Cannot access property of non-table value", 0, 0);
    }
    
    // Create a string key from the identifier token
    char* key_str = malloc(expr->key.length + 1);
    if (!key_str) {
        free_value(table_result.value);
        return make_error("Memory allocation failed", 0, 0);
    }
    const char* key_identifier = expr->key.identifier ? expr->key.identifier : "unknown";
    strncpy(key_str, key_identifier, strlen(key_identifier));
    key_str[expr->key.length] = '\0';
    
    Value key = make_string_value_from_cstr(key_str);
    free(key_str);
    
    // Get the value from the table
    Value result = table_get(table_result.value.as.table, key);
    
    // Clean up
    free_value(key);
    // Don't free table_result.value here - the table is still referenced
    
    return make_success_with_value(result);
}

// Array evaluation functions
EvalResult eval_array_literal_expr(ArrayLiteralExpr* expr, Environment* env) {
    // Create a new array
    ArrayValue* array = array_create(expr->element_count);
    if (!array) {
        return make_error("Failed to create array", 0, 0);
    }
    
    // Evaluate and add each element
    for (size_t i = 0; i < expr->element_count; i++) {
        EvalResult element_result = evaluate_expr(expr->elements[i], env);
        if (is_error(element_result)) {
            array_release(array);
            return element_result;
        }
        
        // Add element to array (array_push handles capacity expansion)
        array_push(array, element_result.value);
    }
    
    return make_success_with_value(make_array_value(array));
}

EvalResult eval_array_index_expr(ArrayIndexExpr* expr, Environment* env) {
    // Evaluate the target expression (could be array or table)
    EvalResult target_result = evaluate_expr(expr->array, env);
    if (is_error(target_result)) {
        return target_result;
    }
    
    // Evaluate the index expression
    EvalResult index_result = evaluate_expr(expr->index, env);
    if (is_error(index_result)) {
        free_value(target_result.value);
        return index_result;
    }
    
    // Handle both arrays and tables
    if (target_result.value.type == VAL_ARRAY) {
        // Array indexing logic
        if (index_result.value.type != VAL_INTEGER) {
            free_value(target_result.value);
            free_value(index_result.value);
            return make_error("Array index must be an integer", 0, 0);
        }
        
        // Get the index value
        int64_t index = index_result.value.as.integer.value.i64;
        ArrayValue* array = target_result.value.as.array;
        
        // Check bounds
        if (index < 0 || (size_t)index >= array->length) {
            free_value(target_result.value);
            free_value(index_result.value);
            return make_error("Array index out of bounds", 0, 0);
        }
        
        // Get the value (array_get returns a copy)
        Value result = array_get(array, (size_t)index);
        
        // Clean up
        free_value(index_result.value);
        // Don't free target_result.value here - the array is still referenced
        
        return make_success_with_value(result);
        
    } else if (target_result.value.type == VAL_TABLE) {
        // Table indexing logic (same as before)
        Value result = table_get(target_result.value.as.table, index_result.value);
        
        // Clean up
        free_value(index_result.value);
        // Don't free target_result.value here - the table is still referenced
        
        return make_success_with_value(result);
        
    } else {
        // Neither array nor table
        free_value(target_result.value);
        free_value(index_result.value);
        return make_error("Cannot index non-array/non-table value", 0, 0);
    }
}

// Helper structure for pattern matching results
typedef struct {
    bool matches;
    Environment* bindings;  // Variable bindings from pattern (if any)
} PatternMatchResult;

// Forward declarations for pattern matching
PatternMatchResult match_pattern(CasePattern* pattern, Value value, Environment* env);
bool evaluate_expression_pattern(CasePattern* pattern, Value value, Environment* env);
bool value_in_range(Value value, CasePattern* pattern);
int simple_compare_values(Value left, Value right);

// Break statement evaluation
EvalResult eval_break_stmt(BreakStmt* stmt, Environment* env) {
    (void)stmt;  // Unused parameter
    (void)env;   // Unused parameter
    
    // Break statements are handled by the control flow structures (loops, switch)
    EvalResult result;
    result.has_error = false;
    result.has_returned = false;
    result.has_break = true;
    result.has_continue = false;
    result.value = make_nil_value();
    return result;
}

// Continue statement evaluation
EvalResult eval_continue_stmt(ContinueStmt* stmt, Environment* env) {
    (void)stmt;  // Unused parameter
    (void)env;   // Unused parameter
    
    // Continue statements are handled by the control flow structures (loops)
    EvalResult result;
    result.has_error = false;
    result.has_returned = false;
    result.has_break = false;
    result.has_continue = true;
    result.value = make_nil_value();
    return result;
}

// Import statement evaluation
EvalResult eval_import_stmt(ImportStmt* stmt, Environment* env) {
    (void)env;  // Unused parameter for now
    
    ModuleRegistry* registry = get_global_module_registry();
    if (!registry) {
        return make_error("Module registry not initialized", 0, 0);
    }
    
    // Extract module name from string literal token
    const char* module_name = stmt->module_name.literal.string;
    if (!module_name) {
        return make_error("Invalid module name - null string", stmt->keyword.line, stmt->keyword.column);
    }
    
    // Additional validation for module name
    size_t name_len = strlen(module_name);
    if (name_len == 0) {
        return make_error("Invalid module name - empty string", stmt->keyword.line, stmt->keyword.column);
    }
    if (name_len > 100) {  // Sanity check
        return make_error("Invalid module name - too long", stmt->keyword.line, stmt->keyword.column);
    }
    
    // Check if the module table already exists in THIS environment
    bool found = false;
    Value existing_module = get_variable(env, module_name, &found);
    if (found && existing_module.type == VAL_TABLE) {
        // Module table already exists in this environment, nothing to do
        return make_success_with_value(make_nil_value());
    }
    
    // Try to load the module plugin (if not already loaded globally)
    if (!is_module_loaded(registry, module_name)) {
        PluginLoadResult result = load_module_by_name(registry, module_name);
        if (result.status != PLUGIN_STATUS_LOADED) {
            // Create a detailed error message with module name
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg), "FATAL: Import failed - module '%s' not found", module_name);
            return make_error(error_msg, stmt->keyword.line, stmt->keyword.column);
        }
    }
    
    // Create a module table and populate it with the module's functions
    // (Only reached if module doesn't exist in current environment)
    // This allows users to access module functions via module.function syntax
    // and also enables: var f = module.function; f();
    Table* module_table = create_table(16);  // Start with capacity of 16
    if (!module_table) {
        return make_error("Failed to create module table", stmt->keyword.line, stmt->keyword.column);
    }
    
    // Populate the table with references to module functions
    // For now, we store function names as strings - later we'll support callable refs
    int functions_added = 0;
    for (size_t i = 0; i < registry->function_count; i++) {
        FunctionEntry* entry = &registry->function_table[i];
        // Check if this function belongs to this module
        if (entry->qualified_name) {
            char entry_module[128] = {0};
            char entry_func[128] = {0};
            if (parse_qualified_name(entry->qualified_name, entry_module, entry_func)) {
                if (strcmp(entry_module, module_name) == 0) {
                    // This function belongs to our module - add it to the table
                    Value func_key = make_string_value_from_cstr(entry_func);
                    // For now, store a nil value - later we'll support native function values
                    Value func_value = make_nil_value();
                    table_set(module_table, func_key, func_value);
                    free_value(func_key);
                    functions_added++;
                }
            }
        }
    }
    
    // Define the module table in the environment so it's accessible as a variable
    Value table_value = make_table_value(module_table);
    define_variable(env, module_name, table_value);
    
    (void)functions_added;  // Suppress unused variable warning
    
    // Import successful - return nil value
    return make_success_with_value(make_nil_value());
}

// Switch statement evaluation
EvalResult eval_switch_stmt(SwitchStmt* stmt, Environment* env) {
    // Evaluate the discriminant
    EvalResult discriminant_result = evaluate_expr(stmt->discriminant, env);
    if (discriminant_result.has_error) {
        return discriminant_result;
    }
    
    Value switch_value = discriminant_result.value;
    
    // Try each case in order
    for (size_t i = 0; i < stmt->case_count; i++) {
        SwitchCase* case_clause = stmt->cases[i];
        
        // Check if any pattern in this case matches
        bool case_matches = false;
        Environment* case_env = create_environment(env);
        if (!case_env) {
            free_value(switch_value);
            return make_error("Memory allocation failed", 0, 0);
        }
        
        for (size_t j = 0; j < case_clause->pattern_count; j++) {
            PatternMatchResult match_result = match_pattern(
                case_clause->patterns[j], switch_value, env);
            
            if (match_result.matches) {
                // Check guard clause if present
                if (case_clause->guard) {
                    EvalResult guard_result = evaluate_expr(case_clause->guard, case_env);
                    if (guard_result.has_error || !is_truthy(guard_result.value)) {
                        if (guard_result.has_error) {
                            free_value(switch_value);
                            free_environment(case_env);
                            return guard_result;
                        }
                        free_value(guard_result.value);
                        continue;  // Guard failed, try next pattern
                    }
                    free_value(guard_result.value);
                }
                
                // Pattern and guard matched
                case_matches = true;
                if (match_result.bindings) {
                    // TODO: Merge bindings into case_env
                    free_environment(match_result.bindings);
                }
                break;
            }
        }
        
        if (case_matches) {
            // Execute case body
            EvalResult case_result = make_success_with_value(make_nil_value());
            
            for (size_t j = 0; j < case_clause->body_count; j++) {
                free_value(case_result.value);
                case_result = evaluate_stmt(case_clause->body[j], case_env);
                
                if (case_result.has_error || case_result.has_returned) {
                    free_environment(case_env);
                    free_value(switch_value);
                    return case_result;
                }
                
                // Check for break
                if (case_result.has_break) {
                    free_environment(case_env);
                    free_value(switch_value);
                    // Convert break to normal success
                    case_result.has_break = false;
                    return case_result;
                }
            }
            
            free_environment(case_env);
            
            // If no explicit break, fall through to next case
            if (!case_clause->has_break) {
                continue;  // Fall through
            } else {
                free_value(switch_value);
                return case_result;
            }
        }
        
        free_environment(case_env);
    }
    
    // No case matched, try default
    if (stmt->default_body) {
        EvalResult default_result = make_success_with_value(make_nil_value());
        
        for (size_t i = 0; i < stmt->default_body_count; i++) {
            free_value(default_result.value);
            default_result = evaluate_stmt(stmt->default_body[i], env);
            
            if (default_result.has_error || default_result.has_returned || default_result.has_break) {
                free_value(switch_value);
                if (default_result.has_break) {
                    default_result.has_break = false;  // Convert break to normal
                }
                return default_result;
            }
        }
        
        free_value(switch_value);
        return default_result;
    }
    
    // No match and no default
    free_value(switch_value);
    return make_success_with_value(make_nil_value());
}

// Pattern matching implementation
PatternMatchResult match_pattern(CasePattern* pattern, Value value, Environment* env) {
    PatternMatchResult result = {false, NULL};
    
    switch (pattern->type) {
        case PATTERN_VALUE:
            result.matches = values_equal(pattern->as.literal, value);
            break;
            
        case PATTERN_EXPRESSION:
            result.matches = evaluate_expression_pattern(pattern, value, env);
            break;
            
        case PATTERN_RANGE:
            result.matches = value_in_range(value, pattern);
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

// Expression pattern matching (e.g., >= 10, <= 5)
bool evaluate_expression_pattern(CasePattern* pattern, Value value, Environment* env) {
    EvalResult rhs_result = evaluate_expr(pattern->as.expr_pattern.expression, env);
    if (rhs_result.has_error) {
        free_value(rhs_result.value);
        return false;
    }
    
    TokenType op = pattern->as.expr_pattern.op;
    bool result = false;
    
    switch (op) {
        case TOKEN_EQUAL_EQUAL:
            result = values_equal(value, rhs_result.value);
            break;
        case TOKEN_BANG_EQUAL:
            result = !values_equal(value, rhs_result.value);
            break;
        case TOKEN_GREATER:
            result = simple_compare_values(value, rhs_result.value) > 0;
            break;
        case TOKEN_GREATER_EQUAL:
            result = simple_compare_values(value, rhs_result.value) >= 0;
            break;
        case TOKEN_LESS:
            result = simple_compare_values(value, rhs_result.value) < 0;
            break;
        case TOKEN_LESS_EQUAL:
            result = simple_compare_values(value, rhs_result.value) <= 0;
            break;
        default:
            result = false;
            break;
    }
    
    free_value(rhs_result.value);
    return result;
}

// Range matching (e.g., 1..10)
bool value_in_range(Value value, CasePattern* pattern) {
    // Evaluate start and end expressions
    EvalResult start_result = evaluate_expr(pattern->as.range_pattern.start, NULL);
    if (start_result.has_error) {
        free_value(start_result.value);
        return false;
    }
    
    EvalResult end_result = evaluate_expr(pattern->as.range_pattern.end, NULL);
    if (end_result.has_error) {
        free_value(start_result.value);
        free_value(end_result.value);
        return false;
    }
    
    Value start_val = start_result.value;
    Value end_val = end_result.value;
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
    
    free_value(start_result.value);
    free_value(end_result.value);
    return in_range;
}

// Simple comparison function that returns integer like strcmp
// Returns: -1 if left < right, 0 if left == right, 1 if left > right
int simple_compare_values(Value left, Value right) {
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
        const char* left_str = string_data(left.as.string);
        const char* right_str = string_data(right.as.string);
        return strcmp(left_str, right_str);
    }
    
    // Different types - not comparable
    return 0;
}

// ============================================================================
// ENUM EVALUATION IMPLEMENTATION
// ============================================================================

EvalResult eval_enum_stmt(EnumStmt* stmt, Environment* env) {
    if (!stmt) {
        return make_error("Null enum statement", 0, 0);
    }
    
    
    // Create the enum definition
    const char* enum_name = stmt->name.identifier;
    if (!enum_name) {
        return make_error("Invalid enum name", stmt->name.line, stmt->name.column);
    }
    
    // Check for namespace collision: a variable with the same name shouldn't exist
    bool variable_exists = false;
    get_variable(env, enum_name, &variable_exists);
    if (variable_exists) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Name collision: variable '%s' already exists, cannot declare enum with the same name", enum_name);
        return make_error(error_msg, stmt->name.line, stmt->name.column);
    }
    
    // Also check if an enum with this name already exists
    char check_enum_var_name[256];
    snprintf(check_enum_var_name, sizeof(check_enum_var_name), "__enum_%s", enum_name);
    bool enum_exists = false;
    get_variable(env, check_enum_var_name, &enum_exists);
    if (enum_exists) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Enum '%s' is already declared", enum_name);
        return make_error(error_msg, stmt->name.line, stmt->name.column);
    }
    
    EnumDefinition* enum_def = enum_definition_create(enum_name, stmt->underlying_type);
    if (!enum_def) {
        return make_error("Failed to create enum definition", stmt->name.line, stmt->name.column);
    }
    
    // Process enum members
    EnumMemberDef* member = stmt->members;
    while (member) {
        const char* member_name = member->name.identifier;
        if (!member_name) {
            enum_definition_release(enum_def);
            return make_error("Invalid enum member name", member->name.line, member->name.column);
        }
        
        // Evaluate member value if provided
        if (member->value) {
            EvalResult value_result = evaluate_expr(member->value, env);
            if (value_result.has_error) {
                enum_definition_release(enum_def);
                return value_result;
            }
            
            // Ensure the value is an integer
            if (value_result.value.type != VAL_INTEGER) {
                free_value(value_result.value);
                enum_definition_release(enum_def);
                return make_error("Enum member value must be an integer", member->name.line, member->name.column);
            }
            
            // Extract integer value - use the unified approach since all are stored as int64_t
            int64_t int_value = 0;
            switch (value_result.value.as.integer.num_type) {
                case NUM_INT8:  int_value = (int64_t)value_result.value.as.integer.value.i8; break;
                case NUM_UINT8: int_value = (int64_t)value_result.value.as.integer.value.u8; break;
                case NUM_INT16: int_value = (int64_t)value_result.value.as.integer.value.i16; break;
                case NUM_UINT16: int_value = (int64_t)value_result.value.as.integer.value.u16; break;
                case NUM_INT32: int_value = (int64_t)value_result.value.as.integer.value.i32; break;
                case NUM_UINT32: int_value = (int64_t)value_result.value.as.integer.value.u32; break;
                case NUM_INT64: int_value = value_result.value.as.integer.value.i64; break;
                case NUM_UINT64: int_value = (int64_t)value_result.value.as.integer.value.u64; break;
                default: 
                    free_value(value_result.value);
                    enum_definition_release(enum_def);
                    return make_error("Unsupported integer type for enum", member->name.line, member->name.column);
            }
            
            enum_definition_add_member(enum_def, member_name, int_value);
            free_value(value_result.value);
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
    
    return make_success_with_value(make_nil_value());
}

EvalResult eval_enum_access_expr(EnumAccessExpr* expr, Environment* env) {
    if (!expr) {
        return make_error("Null enum access expression", 0, 0);
    }
    
    const char* enum_name = expr->enum_name.identifier;
    const char* member_name = expr->member_name.identifier;
    
    if (!enum_name || !member_name) {
        return make_error("Invalid enum access", expr->enum_name.line, expr->enum_name.column);
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
            return make_success_with_value(copy_value(member_var));
        }
        
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Undefined enum '%s'", enum_name);
        return make_error(error_msg, expr->enum_name.line, expr->enum_name.column);
    }
    
    if (enum_value.type != VAL_USERDATA || 
        strcmp(enum_value.as.userdata.type_name, "enum_definition") != 0) {
        return make_error("Invalid enum definition", expr->enum_name.line, expr->enum_name.column);
    }
    
    EnumDefinition* enum_def = (EnumDefinition*)enum_value.as.userdata.ptr;
    if (!enum_def) {
        return make_error("Null enum definition", expr->enum_name.line, expr->enum_name.column);
    }
    
    // Find the enum member
    EnumMember* member = enum_definition_find_member(enum_def, member_name);
    if (!member) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Undefined enum member '%s.%s'", enum_name, member_name);
        return make_error(error_msg, expr->member_name.line, expr->member_name.column);
    }
    
    // Create enum value
    Value result = make_enum_value(enum_def, member->value);
    return make_success_with_value(result);
}
