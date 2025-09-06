#include "stack_trace.h"
#include "utility.h"
#include "token.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Global stack trace instance
static StackTrace* global_stack_trace = NULL;
static const size_t DEFAULT_MAX_DEPTH = 1000;  // Default max recursion depth

// Initialize the global stack trace system
void stack_trace_init(void) {
    if (global_stack_trace) {
        return; // Already initialized
    }
    
    global_stack_trace = malloc(sizeof(StackTrace));
    if (!global_stack_trace) {
        fprintf(stderr, "Failed to allocate memory for stack trace\n");
        return;
    }
    
    global_stack_trace->frames = malloc(16 * sizeof(CallFrame));
    if (!global_stack_trace->frames) {
        free(global_stack_trace);
        global_stack_trace = NULL;
        fprintf(stderr, "Failed to allocate memory for stack frames\n");
        return;
    }
    
    global_stack_trace->frame_count = 0;
    global_stack_trace->frame_capacity = 16;
    global_stack_trace->max_depth = DEFAULT_MAX_DEPTH;
}

// Cleanup the global stack trace system
void stack_trace_cleanup(void) {
    if (!global_stack_trace) {
        return;
    }
    
    // Free any dynamically allocated strings in frames
    // Note: We assume function_name, filename, and module_name are string literals
    // or managed elsewhere, so we don't free them here
    
    free(global_stack_trace->frames);
    free(global_stack_trace);
    global_stack_trace = NULL;
}

// Get the current stack trace
StackTrace* get_current_stack_trace(void) {
    return global_stack_trace;
}

// Push a new call frame onto the stack
void stack_trace_push(const char* function_name, const char* filename, 
                     int line, int column, bool is_builtin, bool is_plugin, 
                     const char* module_name) {
    if (!global_stack_trace) {
        stack_trace_init();
        if (!global_stack_trace) {
            return; // Failed to initialize
        }
    }
    
    // Check for stack overflow
    if (global_stack_trace->frame_count >= global_stack_trace->max_depth) {
        fprintf(stderr, "Stack overflow: maximum call depth of %zu exceeded\n", 
                global_stack_trace->max_depth);
        return;
    }
    
    // Resize if needed
    if (global_stack_trace->frame_count >= global_stack_trace->frame_capacity) {
        size_t new_capacity = global_stack_trace->frame_capacity * 2;
        CallFrame* new_frames = realloc(global_stack_trace->frames, 
                                       new_capacity * sizeof(CallFrame));
        if (!new_frames) {
            fprintf(stderr, "Failed to expand stack trace capacity\n");
            return;
        }
        global_stack_trace->frames = new_frames;
        global_stack_trace->frame_capacity = new_capacity;
    }
    
    // Add the new frame
    CallFrame* frame = &global_stack_trace->frames[global_stack_trace->frame_count];
    frame->function_name = function_name;
    frame->filename = filename;
    frame->line = line;
    frame->column = column;
    frame->is_builtin = is_builtin;
    frame->is_plugin = is_plugin;
    frame->module_name = module_name;
    
    global_stack_trace->frame_count++;
}

// Pop the top call frame from the stack
void stack_trace_pop(void) {
    if (!global_stack_trace || global_stack_trace->frame_count == 0) {
        return;
    }
    
    global_stack_trace->frame_count--;
}

// Clear all call frames from the stack
void stack_trace_clear(void) {
    if (!global_stack_trace) {
        return;
    }
    
    global_stack_trace->frame_count = 0;
}

// Print the full stack trace
void print_stack_trace(StackTrace* trace) {
    if (!trace || trace->frame_count == 0) {
        printf("  (no stack trace available)\n");
        return;
    }
    
    printf("\n━━━ Call Stack ━━━\n");
    
    // Print frames from most recent to oldest
    for (size_t i = trace->frame_count; i > 0; i--) {
        CallFrame* frame = &trace->frames[i - 1];
        
        printf("  %zu. ", trace->frame_count - i + 1);
        
        if (frame->is_builtin) {
            printf("builtin function '%s'", frame->function_name ? frame->function_name : "unknown");
        } else if (frame->is_plugin) {
            if (frame->module_name) {
                printf("plugin function '%s.%s'", frame->module_name, 
                       frame->function_name ? frame->function_name : "unknown");
            } else {
                printf("plugin function '%s'", frame->function_name ? frame->function_name : "unknown");
            }
        } else {
            printf("function '%s'", frame->function_name ? frame->function_name : "unknown");
        }
        
        if (frame->filename && frame->line > 0) {
            printf(" at %s:%d", frame->filename, frame->line);
            if (frame->column > 0) {
                printf(":%d", frame->column);
            }
        } else if (frame->line > 0) {
            printf(" at line %d", frame->line);
            if (frame->column > 0) {
                printf(":%d", frame->column);
            }
        }
        
        printf("\n");
    }
    printf("\n");
}

// Print a compact stack trace (for inline error messages)
void print_stack_trace_compact(StackTrace* trace) {
    if (!trace || trace->frame_count == 0) {
        return;
    }
    
    printf("Call stack: ");
    
    // Show up to 3 most recent frames
    size_t frames_to_show = trace->frame_count > 3 ? 3 : trace->frame_count;
    
    for (size_t i = trace->frame_count; i > trace->frame_count - frames_to_show; i--) {
        CallFrame* frame = &trace->frames[i - 1];
        
        if (i < trace->frame_count) {
            printf(" <- ");
        }
        
        if (frame->is_builtin) {
            printf("%s()", frame->function_name ? frame->function_name : "unknown");
        } else if (frame->is_plugin && frame->module_name) {
            printf("%s.%s()", frame->module_name, 
                   frame->function_name ? frame->function_name : "unknown");
        } else {
            printf("%s()", frame->function_name ? frame->function_name : "unknown");
        }
    }
    
    if (trace->frame_count > 3) {
        printf(" <- ... (%zu more)", trace->frame_count - 3);
    }
    
    printf("\n");
}

// Format stack trace as a string (caller must free)
char* format_stack_trace(StackTrace* trace) {
    if (!trace || trace->frame_count == 0) {
        return mobius_strdup("(no stack trace available)");
    }
    
    // Estimate buffer size (generous allocation)
    size_t buffer_size = trace->frame_count * 200 + 100;
    char* buffer = malloc(buffer_size);
    if (!buffer) {
        return NULL;
    }
    
    strcpy(buffer, "Call Stack:\n");
    
    for (size_t i = trace->frame_count; i > 0; i--) {
        CallFrame* frame = &trace->frames[i - 1];
        char frame_str[200];
        
        snprintf(frame_str, sizeof(frame_str), "  %zu. ", trace->frame_count - i + 1);
        
        if (frame->is_builtin) {
            snprintf(frame_str + strlen(frame_str), sizeof(frame_str) - strlen(frame_str),
                    "builtin function '%s'", frame->function_name ? frame->function_name : "unknown");
        } else if (frame->is_plugin) {
            if (frame->module_name) {
                snprintf(frame_str + strlen(frame_str), sizeof(frame_str) - strlen(frame_str),
                        "plugin function '%s.%s'", frame->module_name, 
                        frame->function_name ? frame->function_name : "unknown");
            } else {
                snprintf(frame_str + strlen(frame_str), sizeof(frame_str) - strlen(frame_str),
                        "plugin function '%s'", frame->function_name ? frame->function_name : "unknown");
            }
        } else {
            snprintf(frame_str + strlen(frame_str), sizeof(frame_str) - strlen(frame_str),
                    "function '%s'", frame->function_name ? frame->function_name : "unknown");
        }
        
        if (frame->filename && frame->line > 0) {
            snprintf(frame_str + strlen(frame_str), sizeof(frame_str) - strlen(frame_str),
                    " at %s:%d", frame->filename, frame->line);
            if (frame->column > 0) {
                snprintf(frame_str + strlen(frame_str), sizeof(frame_str) - strlen(frame_str),
                        ":%d", frame->column);
            }
        } else if (frame->line > 0) {
            snprintf(frame_str + strlen(frame_str), sizeof(frame_str) - strlen(frame_str),
                    " at line %d", frame->line);
            if (frame->column > 0) {
                snprintf(frame_str + strlen(frame_str), sizeof(frame_str) - strlen(frame_str),
                        ":%d", frame->column);
            }
        }
        
        strcat(frame_str, "\n");
        strcat(buffer, frame_str);
    }
    
    return buffer;
}

// Get current stack depth
size_t get_stack_depth(void) {
    if (!global_stack_trace) {
        return 0;
    }
    return global_stack_trace->frame_count;
}

// Check if stack overflow would occur
bool is_stack_overflow(void) {
    if (!global_stack_trace) {
        return false;
    }
    return global_stack_trace->frame_count >= global_stack_trace->max_depth;
}

// Set maximum stack depth
void set_max_stack_depth(size_t max_depth) {
    if (!global_stack_trace) {
        stack_trace_init();
    }
    if (global_stack_trace) {
        global_stack_trace->max_depth = max_depth;
    }
}

// Get maximum stack depth
size_t get_max_stack_depth(void) {
    if (!global_stack_trace) {
        return DEFAULT_MAX_DEPTH;
    }
    return global_stack_trace->max_depth;
}

// Print error with stack trace
void print_error_with_stack_trace(const char* message, StackTrace* trace) {
    printf("\n━━━ Runtime Error ━━━\n");
    printf("\n  %s\n", message);
    
    if (trace && trace->frame_count > 0) {
        print_stack_trace(trace);
    } else {
        printf("\n  (no stack trace available)\n\n");
    }
}