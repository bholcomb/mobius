#ifndef MOBIUS_STATE_H
#define MOBIUS_STATE_H

#include "data/value.h"
#include <mobius/mobius.h>

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
class Table;

struct GlobalEnvironment {
    std::vector<Value> slots;
    std::atomic<int> count{0};
    mutable std::mutex mutex;
    std::unordered_map<std::string, int> slot_map;
    Table* backing_table = nullptr;

    GlobalEnvironment() = default;
    GlobalEnvironment(const GlobalEnvironment&) = delete;
    GlobalEnvironment& operator=(const GlobalEnvironment&) = delete;
};

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
    int execStringInEnvironment(const char* code, GlobalEnvironment* env);
    int execFile(const char* filename);
    int execFileInEnvironment(const char* filename, GlobalEnvironment* env);

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

    int assignGlobalSlot(const char* name, GlobalEnvironment* env = nullptr);
    Value& globalSlot(int idx, GlobalEnvironment* env = nullptr);
    const Value& globalSlot(int idx, GlobalEnvironment* env = nullptr) const;
    int globalSlotCount(GlobalEnvironment* env = nullptr) const;
    int findGlobalSlot(const char* name, GlobalEnvironment* env = nullptr) const;
    const char* globalSlotName(int idx, GlobalEnvironment* env = nullptr) const;
    void removeGlobalSlots(int from_slot, GlobalEnvironment* env = nullptr);
    void setGlobalReadonly(const char* name, bool readonly);
    bool removeGlobal(const char* name);
    void setGlobalValue(int slot, const Value& value, GlobalEnvironment* env = nullptr, bool mark_defined = true);
    void syncGlobalSlotToBackingTable(int slot, GlobalEnvironment* env = nullptr);
    void seedGlobalEnvironmentFromTable(GlobalEnvironment* env, Table* table);
    GlobalEnvironment* rootGlobalEnvironment() { return &root_globals_; }
    const GlobalEnvironment* rootGlobalEnvironment() const { return &root_globals_; }

    void addOwnedProto(struct Prototype* proto);

    void addPluginDirectory(const char* directory);
    void clearPluginDirectories();
    const std::vector<std::string>& pluginDirectories() const { return plugin_directories_; }

    // Active VM — returns the currently executing VM for this state, or nullptr
    // if this state is not currently executing on the thread.
    class MobiusVM* activeVM() const;

    // Native call context — resolved from the active VM for this state, falling
    // back to the state's persistent main VM for host-side stack operations.
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
    Table* typeMetatable(ValueType t) const {
        std::lock_guard<std::mutex> lock(type_metatables_mutex_);
        return type_metatables_[t];
    }
    void setTypeMetatable(ValueType t, Table* mt);
    Table* userdataTypeMetatable(MobiusString* type_tag) const;
    void setUserdataTypeMetatable(MobiusString* type_tag, Table* mt);

private:
    class MobiusVM* boundVM() const;

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

    GlobalEnvironment root_globals_;

    // Prototypes compiled by the VM are owned here so they outlive any
    // MobiusFunction objects that reference them (e.g. functions defined
    // in scripts loaded via load()).
    std::vector<struct Prototype*> owned_protos_;
    std::mutex owned_protos_mutex_;

    std::mutex import_mutex_;

    std::vector<std::string> plugin_directories_;
    std::mutex plugin_dirs_mutex_;

    mutable std::mutex type_metatables_mutex_;
    Table* type_metatables_[VALUE_TYPE_COUNT] = {};
    mutable std::mutex userdata_type_metatables_mutex_;
    std::unordered_map<MobiusString*, Table*> userdata_type_metatables_;

    class MobiusVM* main_vm_;
    GlobalEnvironment* current_compile_env_ = nullptr;

    void clearErrorInternal();
};

// ============================================================================
// UTILITY
// ============================================================================

void free_stack_trace(StackTrace* trace);
void free_internal_error(InternalError* error);

#endif // MOBIUS_STATE_H
