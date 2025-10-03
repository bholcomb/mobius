#include "mobius_state.h"

#include <vector>

typedef enum {
    CONTEXT_TYPE_MAIN,          // Main execution context
    CONTEXT_TYPE_FIBER,         // Lightweight fiber
    CONTEXT_TYPE_COROUTINE,     // Full coroutine with suspend/resume
    CONTEXT_TYPE_ASYNC          // Async/await context
} ContextType;

typedef enum {
    CONTEXT_STATE_CREATED,      // Created but not started
    CONTEXT_STATE_RUNNING,      // Currently executing
    CONTEXT_STATE_SUSPENDED,    // Paused (fiber/coroutine)
    CONTEXT_STATE_WAITING,      // Waiting on another context
    CONTEXT_STATE_COMPLETED,    // Finished successfully
    CONTEXT_STATE_ERROR         // Terminated with error
} ContextState;

typedef enum {
    FUNCTION_TYPE_NATIVE,       // C function
    FUNCTION_TYPE_SCRIPT,       // Mobius script function
    FUNCTION_TYPE_PLUGIN,       // Plugin function
    FUNCTION_TYPE_CLOSURE       // Closure with captured environment
} FunctionType;

struct CallFrame {
    // Function info
    const char* function_name;     // Name of the function being called
    const char* filename;         // Source file name (if available)
    int line;                     // Line number where function was called
    int column;                   // Column number where function was called
    FunctionType type;              // NATIVE, SCRIPT, PLUGIN
    void* function_ptr;             // Function pointer or AST node
    
    // Environment
    Environment* env;               // Local environment for this call
    
    // Stack
    size_t stack_base;              // Where this frame's stack starts
    size_t stack_top;               // Top of stack for this frame

    // Timestamps (for profiling)
    uint64_t start_time;
};

struct ExecutionContext {
    // Identity
    uint32_t context_id;
    ThreadState* thread;            // Owning thread
    ContextType type;               // MAIN, FIBER, COROUTINE
    ContextState state;             // RUNNING, SUSPENDED, COMPLETED, ERROR
    
    // ========== EXECUTION STATE (swappable) ==========
    // Value stack
    std::vector<Value*> stack;
    
    // Call frame stack
    std::vector<CallFrame*> stackTrace; //call frames stack
    size_t max_depth;            // Maximum stack depth (for overflow protection)
    
    // Current environment (for variable scoping)
    Environment* current_env;
    
    // Instruction pointer (for bytecode in the future)
    uint8_t* ip;
    
    // ========== SUSPEND/RESUME SUPPORT ==========
    // For coroutines/fibers
    void* suspend_point;            // Where to resume from
    Value suspend_value;            // Value to resume with
    
    // Parent context (for fiber/coroutine hierarchy)
    ExecutionContext* parent;
    
    // For awaiting/yielding
    ExecutionContext* waiting_on;   // Context we're waiting for
};

struct ThreadState {
    // Identity
    pthread_t thread_id;
    uint32_t thread_index;          // Index in MobiusState->threads array
    MobiusState* vm;                // Back-reference to owning VM
    
    // Current execution
    ExecutionContext* current_context;  // Currently running fiber/coroutine
    
    // Fiber/Coroutine management
    std::vector<ExecutionContext*> contexts;    // All contexts (fibers/coroutines) in this thread
    
    // Thread-local error state
    MobiusError* last_error;
    
    // Thread-local temporary storage
    struct {
        Value* temp_values;         // Temporary value storage for interop
        size_t temp_count;
        size_t temp_capacity;
    } temps;
};

struct MobiusState {
    // ========== SHARED STATE (read-only or mutex-protected) ==========
    
    // Global environment - immutable after initialization
    Environment* global_env;        // Built-in functions, constants
    
    // Module system - mutex protected for thread-safe plugin loading
    ModuleRegistry* registry;    
    
    // Configuration - immutable after init
    struct {
        size_t initial_stack_size;
        size_t max_stack_size;
        size_t max_call_depth;
        bool strict_mode;       // If true, no automatic conversions
        bool warn_on_conversion; // If true, warn when converting types
        bool debug_mode;         // If true, print debug information
    } config;
    
    
    // ========== PER-THREAD STATE ==========
    // Thread tracking
    std::vector<ThreadState*> threads;          // Array of thread states
    pthread_mutex_t thread_lock;
    
    // Thread-local storage key (for getting current thread's state)
    pthread_key_t thread_state_key;
    
    // ========== VM-LEVEL STATE ==========
    bool initialized;
};