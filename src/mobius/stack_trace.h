#ifndef MOBIUS_STACK_TRACE_H
#define MOBIUS_STACK_TRACE_H

#include <stddef.h>
#include <stdbool.h>

// Call frame structure to track function calls
typedef struct CallFrame {
    const char* function_name;    // Name of the function being called
    const char* filename;         // Source file name (if available)
    int line;                     // Line number where function was called
    int column;                   // Column number where function was called
    bool is_builtin;             // True if this is a built-in function
    bool is_plugin;              // True if this is a plugin function
    const char* module_name;      // Module name for plugin functions (NULL for others)
} CallFrame;

// Stack trace structure
typedef struct StackTrace {
    CallFrame* frames;           // Array of call frames
    size_t frame_count;          // Number of frames in the stack
    size_t frame_capacity;       // Allocated capacity for frames
    size_t max_depth;            // Maximum stack depth (for overflow protection)
} StackTrace;

// Global stack trace management
void stack_trace_init(void);
void stack_trace_cleanup(void);
StackTrace* get_current_stack_trace(void);

// Stack frame management
void stack_trace_push(const char* function_name, const char* filename, 
                     int line, int column, bool is_builtin, bool is_plugin, 
                     const char* module_name);
void stack_trace_pop(void);
void stack_trace_clear(void);

// Stack trace utilities
void print_stack_trace(StackTrace* trace);
void print_stack_trace_compact(StackTrace* trace);
char* format_stack_trace(StackTrace* trace);
size_t get_stack_depth(void);

// Stack overflow protection
bool is_stack_overflow(void);
void set_max_stack_depth(size_t max_depth);
size_t get_max_stack_depth(void);

// Integration with error reporting
void print_error_with_stack_trace(const char* message, StackTrace* trace);

#endif // MOBIUS_STACK_TRACE_H
