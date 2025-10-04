#ifndef MOBIUS_EVAL_RESULT_H
#define MOBIUS_EVAL_RESULT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Forward declarations
struct ExecutionContext;

// Error categories for better reporting
typedef enum {
    ERROR_RUNTIME,      // General runtime errors
    ERROR_TYPE,         // Type mismatches
    ERROR_UNDEFINED,    // Undefined variables/functions
    ERROR_ARGUMENT,     // Wrong number of arguments
    ERROR_DIVISION,     // Division by zero
    ERROR_MEMORY,       // Memory allocation failures
    ERROR_RETURN        // Return outside function
} ErrorCategory;

// Function type for stack trace
typedef enum {
    TRACE_FUNCTION_NATIVE,         // C function
    TRACE_FUNCTION_SCRIPT,         // Mobius script function
    TRACE_FUNCTION_PLUGIN,         // Plugin function
    TRACE_FUNCTION_CLOSURE         // Closure with captured environment
} TraceFunctionType;

// Stack trace frame - snapshot of a call frame
typedef struct {
    const char* function_name;    // Name of the function (not owned, don't free)
    const char* filename;         // Source file name (not owned, don't free)
    int line;                     // Line number
    int column;                   // Column number
    TraceFunctionType type;       // Function type
} TraceFrame;

// Stack trace - snapshot of the call stack at time of error
typedef struct {
    TraceFrame* frames;           // Array of frames (owned, must free)
    size_t frame_count;           // Number of frames
} StackTrace;

// Enhanced runtime error structure
typedef struct {
    const char* message;
    const char* suggestion;       // Optional suggestion for fixing
    ErrorCategory category;
    int line;
    int column;
    const char* function_name;    // Function where error occurred
    const char* source_line;      // The actual source code line
    StackTrace* stack_trace;      // Call stack snapshot (owned, must free)
} RuntimeError;

// Evaluation result (stack-based only - no .value field)
typedef struct {
    int return_count;   // Number of values pushed onto stack
    bool has_error;
    bool has_returned;  // Flag to indicate if a return statement was executed
    bool has_break;     // Flag to indicate if a break statement was executed
    bool has_continue;  // Flag to indicate if a continue statement was executed
    RuntimeError error;
} EvalResult;

#endif