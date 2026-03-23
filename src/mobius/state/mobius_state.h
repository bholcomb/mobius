#ifndef MOBIUS_STATE_H
#define MOBIUS_STATE_H

#include "data/value.h"
#include "state/environment.h"
#include "mobius/mobius.h"

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <vector>

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

class MobiusState;
class Environment;
class ModuleRegistry;
class Metamethods;

// Internal error structure with owned (heap-allocated) strings
typedef struct InternalError {
    int code;
    char* message;
    char* suggestion;
    int line;
    int column;
    char* function_name;
} InternalError;

#define INITIAL_STACK_CAPACITY 256
#define MAX_STACK_CAPACITY 65536
#define MAX_CALL_DEPTH 1000

// ============================================================================
// CALL FRAME (for stack tracing with profiling)
// ============================================================================

enum FunctionType {
    FUNCTION_TYPE_NATIVE,
    FUNCTION_TYPE_SCRIPT,
    FUNCTION_TYPE_PLUGIN,
    FUNCTION_TYPE_CLOSURE
};

struct CallFrame {
    const char* function_name;
    const char* filename;
    int line;
    int column;
    FunctionType type;
    void* function_ptr;

    Environment* env;

    size_t stack_base;
    size_t stack_top;

    uint64_t start_time;
};

// ============================================================================
// EXECUTION CONTEXT
// ============================================================================

class MOBIUS_API ExecutionContext {
public:
    ExecutionContext(MobiusState* owner, size_t initial_stack, size_t max_depth);
    ~ExecutionContext();

    // Value stack operations
    void push(const Value& value);
    void push(Value&& value);
    Value pop();
    const Value& peek(size_t offset = 0) const;
    size_t stackSize() const;
    void stackClear();

    // Call stack / stack trace operations
    void pushFrame(const char* function_name, const char* filename,
                   int line, int column, FunctionType type,
                   void* function_ptr, Environment* env);
    void popFrame();
    void clearFrames();
    size_t frameDepth() const;
    bool isStackOverflow() const;
    void printStackTrace() const;
    char* formatStackTrace() const;
    StackTrace* captureStackTrace() const;

    MobiusState* state;
    std::vector<Value> stack;
    Environment* current_env;

private:
    std::vector<CallFrame> call_frames_;
    size_t max_depth_;
};

// ============================================================================
// MOBIUS STATE
// ============================================================================

class MOBIUS_API MobiusState {
public:
    explicit MobiusState(MobiusConfig* config = nullptr);
    ~MobiusState();

    MobiusState(const MobiusState&) = delete;
    MobiusState& operator=(const MobiusState&) = delete;

    // Lifecycle
    int initStdlib();

    // Execution
    int execString(const char* code);
    int execFile(const char* filename);

    // Error handling
    InternalError* getLastError() const;
    void clearError();
    int setError(int code, const char* message, const char* suggestion,
                 int line, int column, const char* function_name);
    int error(const char* message);

    MobiusErrorHandler setErrorHandler(MobiusErrorHandler handler, void* userdata);

    // Source code context
    void setSourceContext(const char* source);
    const char* getSourceContext() const;

    // Module helpers
    size_t getModuleCount() const;
    void printModules() const;

    // REPL
    void startRepl();

    // Accessors
    Environment* globalEnv() const { return global_env_; }
    ExecutionContext* mainContext() const { return main_context_; }
    ModuleRegistry* registry() const { return registry_; }
    StringInternPool* stringPool() const { return string_pool_; }
    const MobiusConfig& config() const { return config_; }
    MobiusConfig& config() { return config_; }
    bool isInitialized() const { return initialized_; }
    InternalError* lastError() const { return last_error_; }
    Metamethods* metamethods() const { return metamethods_; }

private:
    Environment* global_env_;
    ModuleRegistry* registry_;
    StringInternPool* string_pool_;
    Metamethods* metamethods_;

    ExecutionContext* main_context_;

    InternalError* last_error_;
    MobiusErrorHandler error_handler_;
    void* error_handler_userdata_;

    MobiusConfig config_;
    bool initialized_;
    const char* source_code_;

    // Prototypes compiled by the VM are owned here so they outlive any
    // MobiusFunction objects that reference them (e.g. functions defined
    // in scripts loaded via load()).
    std::vector<struct Prototype*> owned_protos_;

    void clearErrorInternal();
};

// ============================================================================
// UTILITY
// ============================================================================

void free_stack_trace(StackTrace* trace);

#endif // MOBIUS_STATE_H
