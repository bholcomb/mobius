#include "eval/evaluator.h"
#include "util/utility.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Global source code context for error reporting
static const char* global_source_code = NULL;

void set_source_context(const char* source) {
    global_source_code = source;
}

const char* get_source_context(void) {
    return global_source_code;
}


EvalResult make_success(int return_count) {
    EvalResult result = {0};
    result.return_count = return_count;
    result.has_error = false;
    result.has_returned = false;
    result.has_break = false;
    result.has_continue = false;
    return result;
}

// Helper to push value onto global stack and return success
EvalResult make_success_with_value(Value value) {
    ctx_push(global_context, value);
    return make_success(1);
}

EvalResult make_error(const char* message, int line, int column) {
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