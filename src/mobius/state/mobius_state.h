#ifndef MOBIUS_STATE_H
#define MOBIUS_STATE_H

#include "data/value.h"
#include "mobius/mobius.h"

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <atomic>
#include <vector>
#include <unordered_map>
#include <string>
#include <mutex>

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

class MobiusState;
class ModuleRegistry;
class Metamethods;
class JobSystem;

// Internal error structure with owned (heap-allocated) strings
typedef struct InternalError {
    int code;
    char* message;
    char* suggestion;
    char* filename;
    int line;
    int column;
    char* function_name;
} InternalError;

#define INITIAL_STACK_CAPACITY 256
#define MAX_STACK_CAPACITY 65536
#define MAX_CALL_DEPTH 1000

// ============================================================================
// NATIVE CALL CONTEXT
//
// Set by MobiusVM::callNative() before invoking a MobiusCFunction.
// The C-API stack functions (mobius_stack_push*, mobius_stack_pop, etc.)
// operate on this window into the VM's flat register array instead of a
// separate vector, eliminating all argument/result copying.
// ============================================================================

struct NativeCallContext {
    Value*  registers;   // pointer into MobiusVM::registers_.data()
    int     base;        // absolute index of first argument slot
    int     top;         // exclusive end — incremented by push, decremented by pop
    int     capacity;    // total registers_.size(), for bounds checks
};

// ============================================================================
// STACK TRACE TYPES
// ============================================================================

typedef enum {
    TRACE_FUNCTION_NATIVE,
    TRACE_FUNCTION_SCRIPT,
    TRACE_FUNCTION_PLUGIN,
    TRACE_FUNCTION_CLOSURE
} TraceFunctionType;

struct TraceFrame {
    const char*     function_name;
    const char*     filename;
    int             line;
    int             column;
    TraceFunctionType type;
};

struct StackTrace {
    TraceFrame* frames;
    size_t      frame_count;
};

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

    size_t stack_base;
    size_t stack_top;

    uint64_t start_time;
};

// ============================================================================
// EXECUTION CONTEXT
// ============================================================================

class MOBIUS_API ExecutionContext {
public:
    ExecutionContext(MobiusState* owner, size_t max_depth);
    ~ExecutionContext();

    // Call stack / stack trace operations
    void pushFrame(const char* function_name, const char* filename,
                   int line, int column, FunctionType type,
                   void* function_ptr);
    void popFrame();
    void clearFrames();
    size_t frameDepth() const;
    bool isStackOverflow() const;
    void printStackTrace() const;
    char* formatStackTrace() const;
    StackTrace* captureStackTrace() const;

    MobiusState* state;

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
                 int line, int column, const char* function_name,
                 const char* filename = nullptr);
    int error(const char* message);

    MobiusErrorHandler setErrorHandler(MobiusErrorHandler handler, void* userdata);

    // Source code context
    void setSourceContext(const char* source);
    const char* getSourceContext() const;



    // REPL
    void startRepl();

    // Accessors
    ExecutionContext* mainContext() const;
    ModuleRegistry* registry() const { return registry_; }
    StringInternPool* stringPool() const { return string_pool_; }
    const MobiusConfig& config() const { return config_; }
    bool isInitialized() const { return initialized_; }
    InternalError* lastError() const;
    Metamethods* metamethods() const { return metamethods_; }
    MobiusMetrics& metrics() { return metrics_; }
    std::mutex& importMutex() { return import_mutex_; }
    JobSystem* jobSystem() const { return job_system_; }
    const MobiusMetrics& metrics() const { return metrics_; }
    void resetMetrics() { memset(&metrics_, 0, sizeof(metrics_)); }

    // Flat global variable array — indexed by compile-time slot numbers.
    // globals_ is pre-sized; globalSlot() is lock-free for reads.
    // assignGlobalSlot() and map lookups are mutex-protected.
    int assignGlobalSlot(const char* name);
    Value& globalSlot(int idx) { return globals_[idx]; }
    const Value& globalSlot(int idx) const { return globals_[idx]; }
    int globalSlotCount() const { return global_count_.load(std::memory_order_relaxed); }
    int findGlobalSlot(const char* name) const;
    const char* globalSlotName(int idx) const;
    void removeGlobalSlots(int from_slot);
    void setGlobalReadonly(const char* name, bool readonly);
    bool removeGlobal(const char* name);

    void addOwnedProto(struct Prototype* proto);

    // Main VM — persistent VM owned by the state, used for the creating thread.
    class MobiusVM* mainVM() const { return main_vm_; }

    // Active VM — returns the thread-local current VM.
    class MobiusVM* activeVM() const;

    // Native call context — accessed through the active VM.
    NativeCallContext* nativeContext() const;

    // Convenience wrappers for native functions operating on the NativeCallContext.
    inline const Value& npeek(int offset = 0) const {
        NativeCallContext* ctx = nativeContext();
        return ctx->registers[ctx->top - 1 - offset];
    }
    inline Value& npeek(int offset = 0) {
        NativeCallContext* ctx = nativeContext();
        return ctx->registers[ctx->top - 1 - offset];
    }
    inline Value npop() {
        NativeCallContext* ctx = nativeContext();
        return ctx->registers[--ctx->top];
    }
    inline void npush(const Value& v) {
        NativeCallContext* ctx = nativeContext();
        ctx->registers[ctx->top++] = v;
    }
    inline void npush(Value&& v) {
        NativeCallContext* ctx = nativeContext();
        ctx->registers[ctx->top++] = std::move(v);
    }
    inline int nsize() const {
        NativeCallContext* ctx = nativeContext();
        return ctx->top - ctx->base;
    }

    inline const Value& npeek_self() const {
        NativeCallContext* ctx = nativeContext();
        return ctx->registers[ctx->base];
    }

    // Type-level metatables — one per ValueType, for method dispatch on non-table values
    Table* typeMetatable(ValueType t) const { return type_metatables_[t]; }
    void setTypeMetatable(ValueType t, Table* mt);

private:
    ModuleRegistry* registry_;
    StringInternPool* string_pool_;
    Metamethods* metamethods_;
    JobSystem* job_system_;

    MobiusErrorHandler error_handler_;
    void* error_handler_userdata_;

    MobiusConfig config_;
    MobiusMetrics metrics_;
    bool initialized_;

    InternalError* fallback_last_error_;
    const char* fallback_source_code_;

    // Flat global variable array — pre-sized, lock-free reads by slot index.
    std::vector<Value> globals_;
    std::atomic<int> global_count_{0};
    mutable std::mutex global_slot_mutex_;
    std::unordered_map<std::string, int> global_slot_map_;

    // Prototypes compiled by the VM are owned here so they outlive any
    // MobiusFunction objects that reference them (e.g. functions defined
    // in scripts loaded via load()).
    std::vector<struct Prototype*> owned_protos_;
    std::mutex owned_protos_mutex_;

    std::mutex import_mutex_;

    Table* type_metatables_[16] = {};

    class MobiusVM* main_vm_;

    void clearErrorInternal();
};

// ============================================================================
// UTILITY
// ============================================================================

void free_stack_trace(StackTrace* trace);
void free_internal_error(InternalError* error);

#endif // MOBIUS_STATE_H
