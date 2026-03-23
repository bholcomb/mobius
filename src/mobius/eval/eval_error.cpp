#include "eval/evaluator.h"
#include "state/mobius_state.h"
#include "util/utility.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ============================================================================
// EVALUATION RESULT HELPERS
// ============================================================================

EvalResult make_error(Environment* env, const char* message, int line, int column) {
    EvalResult result = {0};
    result.return_count = 0;
    result.has_error = true;
    result.has_returned = false;
    result.has_break = false;
    result.has_continue = false;
    
    // Duplicate the message to prevent corruption from stack-allocated strings
    if (message) {
        char* message_copy = mobius_strdup(message);
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
    result.error.stack_trace =  env->current_context->captureStackTrace();  // No context available in basic make_error
    
    return result;
}

EvalResult make_error_detailed(Environment* env, const char* message, const char* suggestion, 
                              ErrorCategory category, int line, int column,
                              const char* function_name, const char* source_line) {
    EvalResult result = {0};
    result.return_count = 0;
    result.has_error = true;
    result.has_returned = false;
    result.has_break = false;
    result.has_continue = false;
    
    // Duplicate the message and suggestion to prevent corruption
    if (message) {
        char* message_copy = mobius_strdup(message);
        result.error.message = message_copy;
    } else {
        result.error.message = "Unknown error";
    }
    
    if (suggestion) {
        char* suggestion_copy = mobius_strdup(suggestion);
        result.error.suggestion = suggestion_copy;
    } else {
        result.error.suggestion = NULL;
    }
    result.error.category = category;
    result.error.line = line;
    result.error.column = column;
    result.error.function_name = function_name;
    result.error.source_line = source_line;
    result.error.stack_trace = env->current_context->captureStackTrace();;  // No context available in basic make_error_detailed
    
    return result;
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
        for (size_t i = 0; i < error.stack_trace->frame_count; i++) {
            TraceFrame* frame = &error.stack_trace->frames[i];
            
            fprintf(stderr, "  [%zu] ", i);
            
            const char* type_str;
            switch (frame->type) {
                case TRACE_FUNCTION_NATIVE:  type_str = "native"; break;
                case TRACE_FUNCTION_SCRIPT:  type_str = "script"; break;
                case TRACE_FUNCTION_PLUGIN:  type_str = "plugin"; break;
                case TRACE_FUNCTION_CLOSURE: type_str = "closure"; break;
                default: type_str = "unknown"; break;
            }
            
            fprintf(stderr, "%s (%s)", 
                   frame->function_name ? frame->function_name : "<anonymous>",
                   type_str);
            
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
    
    // Clean up stack trace after printing
    if (error.stack_trace) {
        free_stack_trace((StackTrace*)error.stack_trace);
    }
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
