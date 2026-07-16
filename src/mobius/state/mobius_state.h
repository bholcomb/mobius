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
typedef uint64_t MobiusValueRef;

struct GlobalEnvironment {
    std::vector<Value> slots;
    std::vector<std::string> slot_names;
    std::atomic<int> count{0};
    std::atomic<bool> shared{false};
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
    struct CommonInternedStrings {
        MobiusString* empty = nullptr;
        MobiusString* nil = nullptr;
        MobiusString* true_value = nullptr;
        MobiusString* false_value = nullptr;
        MobiusString* null_string = nullptr;
        MobiusString* function = nullptr;
        MobiusString* native_function = nullptr;
        MobiusString* table = nullptr;
        MobiusString* array = nullptr;
        MobiusString* userdata_null = nullptr;
        MobiusString* shared_null = nullptr;
        MobiusString* buffer_null = nullptr;
        MobiusString* unknown = nullptr;
    };

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
    const CommonInternedStrings& commonStrings() const { return common_strings_; }
    const MobiusConfig& config() const { return config_; }

    // The compile-time default for override_behavior (per-chunk pragma can
    // change it within a chunk). Seeded from config; the REPL sets QUIET so
    // redefining a function mid-session just works.
    MobiusOverrideBehavior compileOverrideBehavior() const { return compile_override_behavior_; }
    void setCompileOverrideBehavior(MobiusOverrideBehavior b) { compile_override_behavior_ = b; }
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
    // Hot path inline (the VM reads a global on every OP_GETGLOBAL): an
    // unshared environment needs no lock — one acquire load of count, a
    // bounds check, and the copy. Shared environments take the mutex in the
    // out-of-line slow path.
    MOBIUS_FORCEINLINE bool copyGlobalValue(int idx, Value* out, GlobalEnvironment* env = nullptr) const {
        const GlobalEnvironment* g = env ? env : &root_globals_;
        int count = g->count.load(std::memory_order_acquire);
        if (MOBIUS_UNLIKELY(idx < 0 || idx >= count ||
                            (size_t)idx >= g->slots.size())) return false;
        if (MOBIUS_LIKELY(!g->shared.load(std::memory_order_acquire))) {
            if (out) *out = g->slots[idx];
            return true;
        }
        return copyGlobalValueShared(idx, out, g);
    }
    bool copyGlobalValueShared(int idx, Value* out, const GlobalEnvironment* g) const;
    Value getGlobalValue(int idx, GlobalEnvironment* env = nullptr) const;
    int globalSlotCount(GlobalEnvironment* env = nullptr) const;
    int findGlobalSlot(const char* name, GlobalEnvironment* env = nullptr) const;
    const char* globalSlotName(int idx, GlobalEnvironment* env = nullptr) const;
    void removeGlobalSlots(int from_slot, GlobalEnvironment* env = nullptr);
    void setGlobalReadonly(const char* name, bool readonly);
    void setGlobalReadonly(int slot, bool readonly, GlobalEnvironment* env = nullptr);
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
        NativeCallContext* ctx = checkedNativeContext(offset + 1, false, false);
        return ctx ? ctx->registers[ctx->top - 1 - offset] : invalidNativeValue();
    }
    inline Value& npeek(int offset = 0) {
        NativeCallContext* ctx = checkedNativeContext(offset + 1, false, false);
        return ctx ? ctx->registers[ctx->top - 1 - offset] : invalidNativeValue();
    }
    inline Value npop() {
        NativeCallContext* ctx = checkedNativeContext(1, false, false);
        if (!ctx) return make_nil_value();
        return ctx->registers[--ctx->top];
    }
    inline void npush(const Value& v) {
        NativeCallContext* ctx = checkedNativeContext(0, false, true);
        if (!ctx) return;
        ctx->registers[ctx->top++] = v;
    }
    inline void npush(Value&& v) {
        NativeCallContext* ctx = checkedNativeContext(0, false, true);
        if (!ctx) return;
        ctx->registers[ctx->top++] = std::move(v);
    }
    inline int nsize() const {
        NativeCallContext* ctx = checkedNativeContext(0, false, false);
        return ctx ? (ctx->top - ctx->base) : 0;
    }

    inline const Value& npeek_self() const {
        NativeCallContext* ctx = checkedNativeContext(0, true, false);
        return ctx ? ctx->registers[ctx->base] : invalidNativeValue();
    }

    // Type-level metatables — one per ValueType, for method dispatch on non-table values
    Table* typeMetatable(ValueType t) const {
        std::lock_guard<std::mutex> lock(type_metatables_mutex_);
        return type_metatables_[t];
    }
    void setTypeMetatable(ValueType t, Table* mt);
    Table* userdataTypeMetatable(MobiusString* type_tag) const;
    void setUserdataTypeMetatable(MobiusString* type_tag, Table* mt);

    MobiusValueRef createValueRef(const Value& value);
    // Visit Values pinned by the C API's ref registry (GC roots).
    void forEachValueRef(void (*cb)(const Value&, void*), void* ud);
    // Visit every state-held GC root: root globals, C-API value refs, and the
    // builtin/userdata type metatables.
    void gcVisitRoots(void (*value_cb)(const Value&, void*),
                      void (*table_cb)(class Table*, void*), void* ud);
    bool releaseValueRef(MobiusValueRef ref);
    bool copyValueRef(MobiusValueRef ref, Value* out) const;
    int callValue(const Value& function, const Value* args, int nargs,
                  int nresults, std::vector<Value>* out_results);

private:
    class MobiusVM* boundVM() const;
    NativeCallContext* checkedNativeContext(int required_count, bool require_self, bool for_push) const;
    static Value& invalidNativeValue();

    ModuleRegistry* registry_;
    StringInternPool* string_pool_;
    CommonInternedStrings common_strings_;
    Metamethods* metamethods_;
    JobSystem* job_system_;

    MobiusErrorHandler error_handler_;
    void* error_handler_userdata_;

    MobiusConfig config_;
    MobiusOverrideBehavior compile_override_behavior_ = MOBIUS_OVERRIDE_ERROR;
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

    mutable std::mutex value_refs_mutex_;
    std::unordered_map<MobiusValueRef, Value> value_refs_;
    MobiusValueRef next_value_ref_ = 1;

    class MobiusVM* main_vm_ = nullptr;
    GlobalEnvironment* current_compile_env_ = nullptr;

    void clearErrorInternal();
};

// ============================================================================
// UTILITY
// ============================================================================

void free_stack_trace(StackTrace* trace);
void free_internal_error(InternalError* error);

#endif // MOBIUS_STATE_H
