#ifndef MOBIUS_STATE_H
#define MOBIUS_STATE_H

#include <memory>
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

struct GcHeap;

// A function the host registered with userdata and flags. Native function
// values refer to one through their aux field (0: a plain C function).
struct HostFunction {
    MobiusCFunction function = nullptr;
    void* userdata = nullptr;
    unsigned int flags = 0;
};

// Pause/abort hint for the VM's safe points (see MobiusState::runControl).
extern volatile bool g_vm_interrupt;
// The error message of an execution stopped by mobius_abort.
extern const char* const kAbortedMessage;
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
    // Slots of `const` globals, one flag per slot (allocated with slots,
    // which never resize). A constant slot is never written again, so
    // reads skip the mutex: under fibers, every global read used to lock
    // it, and with many fibers its cache line bounced between cores.
    std::unique_ptr<std::atomic<uint8_t>[]> constant;

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
    bool reported;   // passed to the error handler already
} InternalError;

#define INITIAL_STACK_CAPACITY 256
#define MAX_STACK_CAPACITY 65536
#define MAX_CALL_DEPTH 200000

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
    // `host_call`: the host asked (mobius_exec_file), so a sandbox does not
    // restrict reading the file; script-initiated loads (import) pass false.
    int execFileInEnvironment(const char* filename, GlobalEnvironment* env, bool host_call = false);

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
    GcHeap* gcHeap() const { return gc_heap_; }

    // This state's random number generator (random/randomseed):
    // xoshiro256**, the same sequence for a seed on every platform.
    uint64_t nextRandom();

    // Output (print, errors, warnings) through the host's handler, or to
    // stdout/stderr. `stream` is MOBIUS_STDOUT or MOBIUS_STDERR.
    void writeOutput(int stream, const char* data, size_t length);
    void setOutputHandler(MobiusOutputHandler handler, void* userdata) {
        output_handler_ = handler;
        output_handler_userdata_ = userdata;
    }
    void setExitHandler(MobiusExitHandler handler, void* userdata) {
        exit_handler_ = handler;
        exit_handler_userdata_ = userdata;
    }
    // exit(code) from a script: the host's handler, or a warning.
    void requestExit(int code);

    // Host functions: an id for (function, userdata, flags), stable for the
    // state's life (the same triple gets the same id). Lookups are
    // lock-free: the table only grows, in fixed chunks.
    int32_t hostFunctionId(MobiusCFunction function, void* userdata, unsigned int flags);
    const HostFunction* hostFunction(int32_t id) const {
        if (id <= 0) return nullptr;
        size_t chunk = (size_t)id >> kHostChunkBits, index = (size_t)id & (kHostChunkSize - 1);
        if (chunk >= kHostChunks) return nullptr;
        HostFunction* entries = host_function_chunks_[chunk].load(std::memory_order_acquire);
        return entries ? &entries[index] : nullptr;
    }

    // Pause / abort control (mobius_pause, mobius_abort, time limits).
    // Set flags make every VM safe point of this state take the slow path
    // (vm_interrupt_point) through the process-wide hint g_vm_interrupt.
    enum : int { RUN_PAUSE = 1, RUN_ABORT = 2 };
    int runControl() const { return run_control_.load(std::memory_order_acquire); }
    bool abortRequested() const { return (runControl() & RUN_ABORT) != 0; }
    void requestPause();
    void requestAbort();
    void setTimeLimit(unsigned int ms) { time_limit_ms_ = ms; }
    bool hasPausedExecution() const { return paused_execution_; }
    int resumeExecution();
    int abortExecution();
    // Called from the reactor thread when a time limit runs out.
    void timeLimitExpired();

    // Sandbox (mobius_sandbox) and the host's file system.
    void setSandbox(unsigned int allow) { sandboxed_ = true; sandbox_allow_ = allow; }
    bool sandboxed() const { return sandboxed_; }
    // The built-in behavior for `cap` may be used (no sandbox, or allowed).
    bool allowsDefault(unsigned int cap) const { return !sandboxed_ || (sandbox_allow_ & cap); }
    void setFileSystem(const MobiusFileSystem* fs, void* userdata);
    bool hasFileSystem() const { return has_file_system_; }

    // File access for scripts (and script loading): the host's file system,
    // the real one, or "not available" in a sandbox. On failure, `error`
    // says why (without an operation prefix). `host_call` marks an access
    // the host itself asked for (mobius_exec_file), which the sandbox
    // does not restrict.
    bool readFile(const char* path, std::string& out, std::string& error, bool host_call = false);
    bool writeFile(const char* path, const char* data, size_t length, bool append, std::string& error);
    // 1 exists, 0 not, -1 not available (`error` set).
    int fileExists(const char* path, std::string& error);
    void seedRandom(uint64_t seed);
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
        if (MOBIUS_LIKELY(!g->shared.load(std::memory_order_acquire)) ||
            (g->constant && g->constant[idx].load(std::memory_order_acquire))) {
            if (out) *out = g->slots[idx];
            return true;
        }
        return copyGlobalValueShared(idx, out, g);
    }
    bool copyGlobalValueShared(int idx, Value* out, const GlobalEnvironment* g) const;
    // Mark a global as a constant (`const`): read-only, and refused by every
    // later write (including override-pragma writes and the C API).
    void setGlobalConstant(int slot, GlobalEnvironment* env = nullptr);
    bool isGlobalConstant(int slot, const GlobalEnvironment* env = nullptr) const;
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
    class MobiusVM* mainVM() const { return main_vm_; }

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
    GcHeap* gc_heap_;   // this state's traced objects (tables, arrays, closures)
    uint64_t rng_[4] = {0, 0, 0, 0};
    std::mutex rng_mutex_;
    StringInternPool* string_pool_;
    CommonInternedStrings common_strings_;
    Metamethods* metamethods_;
    JobSystem* job_system_;

    MobiusErrorHandler error_handler_;
    void* error_handler_userdata_;
    static constexpr size_t kHostChunkBits = 10, kHostChunkSize = 1u << kHostChunkBits, kHostChunks = 1024;
    std::atomic<HostFunction*> host_function_chunks_[kHostChunks] = {};
    std::mutex host_functions_mutex_;
    int32_t next_host_function_ = 1;
    std::unordered_map<std::string, int32_t> host_function_ids_;
    std::atomic<int> run_control_{0};
    unsigned int time_limit_ms_ = 0;
    uint64_t time_limit_timer_ = 0;
    std::atomic<bool> time_limit_hit_{false};
    bool paused_execution_ = false;
    void setRunControl(int bits);
    void clearRunControl(int bits);
    void startTimeLimit();
    void stopTimeLimit();
    int finishExecution(int vm_result);
    void drainAbortedFibers();
    bool sandboxed_ = false;
    unsigned int sandbox_allow_ = 0;
    bool has_file_system_ = false;
    MobiusFileSystem file_system_{};
    void* file_system_userdata_ = nullptr;
    MobiusOutputHandler output_handler_ = nullptr;
    void* output_handler_userdata_ = nullptr;
    MobiusExitHandler exit_handler_ = nullptr;
    void* exit_handler_userdata_ = nullptr;

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
public:
    // Report an error that left a spawned fiber and that nobody observed
    // (see FutureValue). Does nothing if `state` has been destroyed.
    static void reportFiberError(MobiusState* state, InternalError* err);
private:
    void reportError(InternalError* err);
    // Report the current error if it reached the host without having been
    // reported (it was raised inside a try but escaped anyway).
    void reportEscapedError();
};

// ============================================================================
// UTILITY
// ============================================================================

void free_stack_trace(StackTrace* trace);
void free_internal_error(InternalError* error);

#endif // MOBIUS_STATE_H
