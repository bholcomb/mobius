#ifndef MOBIUS_STATE_H
#define MOBIUS_STATE_H

#include "data/value.h"
#include "state/environment.h"
#include "mobius/mobius.h"

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

struct MobiusState;
struct Environment;
struct ModuleRegistry;

// Internal error structure with owned (heap-allocated) strings
typedef struct InternalError {
    int code;
    char* message;
    char* suggestion;
    int line;
    int column;
    char* function_name;
} InternalError;

// Default inital values
#define INITIAL_STACK_CAPACITY 256
#define MAX_STACK_CAPACITY 65536
#define MAX_CALL_DEPTH 1000

// ============================================================================
// CONFIGURATION (defined in include/mobius/mobius.h)
// ============================================================================
// MobiusConfig, MobiusOverrideBehavior, and mobius_default_config() are
// provided by the public header included above.

// ============================================================================
// CALL FRAME (for stack tracing with profiling)
// ============================================================================

typedef enum {
    FUNCTION_TYPE_NATIVE,         // C function
    FUNCTION_TYPE_SCRIPT,         // Mobius script function
    FUNCTION_TYPE_PLUGIN,         // Plugin function
    FUNCTION_TYPE_CLOSURE         // Closure with captured environment
} FunctionType;


typedef struct CallFrame {
    // Function info
    const char* function_name;    // Name of the function being called
    const char* filename;         // Source file name (if available)
    int line;                     // Line number where function was called
    int column;                   // Column number where function was called
    FunctionType type;            // NATIVE, SCRIPT, PLUGIN, CLOSURE
    void* function_ptr;           // Function pointer or AST node (optional)
    
    // Environment
    struct Environment* env;             // Local environment for this call
    
    // Stack positions (for unwinding)
    size_t stack_base;            // Where this frame's stack starts
    size_t stack_top;             // Top of stack for this frame
    
    // Profiling
    uint64_t start_time;          // Timestamp when function was entered (nanoseconds)
    
} CallFrame;

// ============================================================================
// EXECUTION CONTEXT
// ============================================================================

/**
 * Execution context - encapsulates per-context execution state
 * Each context has its own stack, call frames, and current environment.
 * In single-threaded mode, there's one main context per MobiusState.
 * In multi-threaded mode, each thread can have multiple contexts (for coroutines).
 */
typedef struct ExecutionContext {
    // Parent state
    struct MobiusState* state;           // Back-reference to owning state
    
    // ========== EXECUTION STATE ==========
    // Value stack (for expression evaluation)
    Value* stack;
    size_t stack_top;             // Current stack position (0 = empty)
    size_t stack_capacity;        // Allocated stack size
    
    // Current environment (for variable scoping)
    struct Environment* current_env;
    
    // ========== CALL STACK (Stack Trace with Profiling) ==========
    CallFrame* call_frames;       // Array of call frames
    size_t frame_count;           // Number of frames currently on stack
    size_t frame_capacity;        // Allocated capacity for frames
    size_t max_depth;             // Maximum stack depth (from config)
    
    // ========== FUTURE EXTENSIONS ==========
    // TODO(bytecode): uint8_t* ip; - Instruction pointer for bytecode VM
    // TODO(coroutines): void* suspend_point; - Where to resume from
    // TODO(coroutines): Value suspend_value; - Value to resume with
    // TODO(coroutines): ExecutionContext* parent; - Parent context for nesting
} ExecutionContext;

// ============================================================================
// MOBIUS STATE
// ============================================================================

/**
 * Main VM state - encapsulates all interpreter state
 * Currently single-threaded with one main execution context.
 * Future: Support multiple threads and multiple contexts per thread.
 */
struct MobiusState {
    // ========== GLOBAL STATE ==========
    struct Environment* global_env;      // Global environment (built-in functions, constants)
    struct ModuleRegistry* registry;     // Plugin/module registry
    struct StringInternPool* string_pool; // String interning pool for fast comparison and deduplication
    
    // ========== PRE-INTERNED STRINGS ==========
    // Common metamethod names (interned once for fast lookup)
    MobiusString* mm_add;        // "__add"
    MobiusString* mm_sub;        // "__sub"
    MobiusString* mm_mul;        // "__mul"
    MobiusString* mm_div;        // "__div"
    MobiusString* mm_mod;        // "__mod"
    MobiusString* mm_eq;         // "__eq"
    MobiusString* mm_lt;         // "__lt"
    MobiusString* mm_le;         // "__le"
    MobiusString* mm_index;      // "__index"
    MobiusString* mm_newindex;   // "__newindex"
    MobiusString* mm_call;       // "__call"
    MobiusString* mm_tostring;   // "__tostring"
    
    // ========== EXECUTION CONTEXTS ==========
    ExecutionContext* main_context;  // Primary execution context
    // TODO(coroutines): ExecutionContext** contexts; - Array of all contexts
    // TODO(coroutines): size_t context_count; - Number of contexts
    // TODO(coroutines): ExecutionContext* current_context; - Currently active context
    
    // ========== ERROR HANDLING ==========
    InternalError* last_error;
    MobiusErrorHandler error_handler;
    void* error_handler_userdata;
    
    // ========== CONFIGURATION ==========
    MobiusConfig config;          // VM configuration (immutable after init)
    
    // ========== LIFECYCLE ==========
    bool initialized;             // Whether core has been initialized
    
    // ========== FUTURE THREADING SUPPORT ==========
    // TODO(threading): pthread_mutex_t global_lock; - Protect global_env/registry
    // TODO(threading): ThreadState** threads; - Array of thread states
    // TODO(threading): size_t thread_count;
    // TODO(threading): pthread_key_t thread_key; - TLS for per-thread context

    // Global source code context for error reporting
    const char* source_code;
};

// ============================================================================
// STATE MANAGEMENT API
// ============================================================================

/**
 * Create new VM instance
 * @param config Configuration (NULL for defaults)
 * @return New MobiusState, or NULL on failure
 */
MobiusState* mobius_new_state(MobiusConfig* config);

/**
 * Initialize standard library
 * @param state The VM state
 * @return MOBIUS_OK on success, error code on failure
 */
int mobius_init_stdlib(MobiusState* state);

/**
 * Clean up VM and free all resources
 * @param state The VM state to free
 */
void mobius_free_state(MobiusState* state);

// ============================================================================
// EXECUTION CONTEXT API
// ============================================================================

/**
 * Create execution context (internal use)
 * @param state Parent VM state
 * @return New ExecutionContext, or NULL on failure
 */
ExecutionContext* mobius_create_context(MobiusState* state);

/**
 * Free execution context (internal use)
 * @param ctx Context to free
 */
void mobius_free_context(ExecutionContext* ctx);

/**
 * Get main execution context (convenience function)
 * @param state The VM state
 * @return Main execution context
 */
ExecutionContext* mobius_get_main_context(MobiusState* state);

// ============================================================================
// STACK OPERATIONS
// ============================================================================

/**
 * Push value onto execution stack
 * @param ctx Execution context
 * @param value Value to push
 */
void ctx_push(ExecutionContext* ctx, Value value);

/**
 * Pop value from execution stack
 * @param ctx Execution context
 * @return Popped value
 */
Value ctx_pop(ExecutionContext* ctx);

/**
 * Peek at value on stack without removing it
 * @param ctx Execution context
 * @param offset Offset from top (0 = top, 1 = second from top, etc.)
 * @return Value at offset
 */
Value ctx_peek(ExecutionContext* ctx, size_t offset);

/**
 * Get current stack size
 * @param ctx Execution context
 * @return Number of values on stack
 */
size_t ctx_stack_size(ExecutionContext* ctx);

/**
 * Clear execution stack
 * @param ctx Execution context
 */
void ctx_stack_clear(ExecutionContext* ctx);

/**
 * Ensure stack has capacity for n more values (internal use)
 * @param ctx Execution context
 * @param needed Number of additional slots needed
 */
void ctx_ensure_stack_capacity(ExecutionContext* ctx, size_t needed);

// ============================================================================
// CALL STACK / STACK TRACE OPERATIONS
// ============================================================================

/**
 * Push call frame onto stack trace
 * @param ctx Execution context
 * @param function_name Name of the function
 * @param filename Source file name (can be NULL)
 * @param line Line number
 * @param column Column number
 * @param type Function type
 * @param function_ptr Function pointer or AST node (can be NULL)
 * @param env Local environment for this call
 */
void stack_trace_push(ExecutionContext* ctx, const char* function_name, 
                       const char* filename, int line, int column,
                       FunctionType type, void* function_ptr, struct Environment* env);

/**
 * Pop call frame from stack trace
 * @param ctx Execution context
 */
void stack_trace_pop(ExecutionContext* ctx);

/**
 * Clear all call frames
 * @param ctx Execution context
 */
void stack_trace_clear(ExecutionContext* ctx);

/**
 * Get current call depth
 * @param ctx Execution context
 * @return Number of call frames on stack
 */
size_t stack_trace_depth(ExecutionContext* ctx);

/**
 * Check for stack overflow
 * @param ctx Execution context
 * @return true if stack overflow detected
 */
bool is_stack_overflow(ExecutionContext* ctx);

/**
 * Print stack trace to stdout
 * @param ctx Execution context
 */
void print_stack_trace(ExecutionContext* ctx);

/**
 * Format stack trace to string
 * @param ctx Execution context
 * @return Formatted string (caller must free), or NULL on error
 */
char* format_stack_trace(ExecutionContext* ctx);

/**
 * Capture stack trace
 * @param ctx Execution context
 * @return Stack trace
 */
StackTrace* capture_stack_trace(ExecutionContext* ctx);

/**
 * Free stack trace
 * @param trace Stack trace
 */
void free_stack_trace(StackTrace* trace);

// ============================================================================
// EXECUTION API
// ============================================================================

/**
 * Execute code in main context
 * @param state VM state
 * @param code Source code to execute
 * @return MOBIUS_OK on success, error code on failure
 */
int mobius_exec_string(MobiusState* state, const char* code);

/**
 * Execute code in main context
 * @param state VM state
 * @param filename Source code to execute
 * @return MOBIUS_OK on success, error code on failure
 */
int mobius_exec_file(MobiusState* state, const char* filename);

// ============================================================================
// ERROR HANDLING API
// ============================================================================

InternalError* mobius_get_last_error(MobiusState* state);
void mobius_clear_error(MobiusState* state);
void mobius_free_internal_error(InternalError* error);

int mobius_set_error(MobiusState* state, int code, const char* message, const char* suggestion, int line, int column, const char* function_name);

// Set error and return -1 (convenience for native functions)
int mobius_error(MobiusState* state, const char* message);

// ============================================================================
// SOURCE CODE CONTEXT
// ============================================================================

/**
 * Set the source code context
 * @param state The interpreter state
 * @param source The source code
 */
void set_source_context(MobiusState* state, const char* source);

/**
 * Get the source code context
 * @param state The interpreter state
 * @return The source code
 */
const char* get_source_context(MobiusState* state);

/**
 * Extract a source line from the source code
 * @param source The source code
 * @param line_number The line number
 * @return The source line
 */
const char* extract_source_line(const char* source, int line_number);

#endif // MOBIUS_STATE_H
