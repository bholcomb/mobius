#ifndef MOBIUS_EVAL_RESULT_H
#define MOBIUS_EVAL_RESULT_H

//forward declarations
struct StackTrace;

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

// Enhanced runtime error structure
typedef struct {
    const char* message;
    const char* suggestion;    // Optional suggestion for fixing
    ErrorCategory category;
    int line;
    int column;
    const char* function_name; // Function where error occurred
    const char* source_line;   // The actual source code line
    struct StackTrace* stack_trace;   // Call stack at time of error
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