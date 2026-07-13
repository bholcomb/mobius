#define _POSIX_C_SOURCE 199309L 

#include <mobius/mobius_plugin.h>
#include "state/mobius_state.h"
#include "frontend/ast.h"
#include "frontend/scanner.h"
#include "frontend/parser.h"
#include "frontend/token.h"
#include "library/library.h"
#include "library/array.h"
#include "library/buffer_lib.h"
#include "library/fiber_lib.h"
#include "library/struct_view_lib.h"
#include "library/table_lib.h"
#include "internal/string_intern.h"
#include "data/table.h"
#include "data/metamethods.h"
#include "plugin/module_registry.h"
#include "fiber/job_system.h"
#include "repl.h"
#include "util/utility.h"
#include "util/file_io.h"
#include "vm/compiler.h"
#include "vm/vm.h"

#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <thread>
#include <algorithm>

static GlobalEnvironment* env_or_root(MobiusState* state, GlobalEnvironment* env) {
    return env ? env : state->rootGlobalEnvironment();
}

static const GlobalEnvironment* env_or_root(const MobiusState* state, const GlobalEnvironment* env) {
    return env ? env : state->rootGlobalEnvironment();
}

static constexpr size_t kGlobalSlotCapacity = 16384;
static constexpr size_t kInitialStringBucketCount = 65536;

static inline bool global_slot_in_bounds(const GlobalEnvironment* globals, int idx, int count) {
    return idx >= 0 && idx < count && (size_t)idx < globals->slots.size();
}

// ============================================================================
// CONFIGURATION
// ============================================================================

MobiusConfig mobius_default_config(void) {
    MobiusConfig config;
    config.initial_stack_size = INITIAL_STACK_CAPACITY;
    config.max_stack_size = MAX_STACK_CAPACITY;
    config.max_call_depth = MAX_CALL_DEPTH;
    config.strict_mode = false;
    config.warn_on_conversion = false;
    config.debug_mode = false;
    config.enable_hot_reload = false;
    config.override_behavior = MOBIUS_OVERRIDE_ERROR;

    config.fiber_stack_size        = 512 * 1024;  // 512 KiB — deep native calls
                                                   // (e.g. Vulkan drivers) overflow
                                                   // a smaller per-fiber stack.
    config.main_fiber_stack_size   = 8 * 1024 * 1024;  // 8 MiB — the top-level
                                                   // script fiber behaves like a
                                                   // normal thread stack.
    config.initial_fiber_pool_size = 16;
    config.max_fiber_pool_size     = 256;
    unsigned int hw = std::thread::hardware_concurrency();
    config.max_worker_threads      = static_cast<int>(std::max(1u, hw / 2));

    return config;
}

// ============================================================================
// Helper: time in nanoseconds for profiling
// ============================================================================

static uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

// ============================================================================
// EXECUTION CONTEXT IMPLEMENTATION
// ============================================================================

ExecutionContext::ExecutionContext(MobiusState* owner, size_t max_depth)
    : state(owner), max_depth_(max_depth) {
    call_frames_.reserve(64);
}

ExecutionContext::~ExecutionContext() {
}

// ============================================================================
// CALL STACK / STACK TRACE OPERATIONS
// ============================================================================

void ExecutionContext::pushFrame(const char* function_name, const char* filename,
                                 int line, int column, FunctionType type,
                                 void* function_ptr) {
    if (call_frames_.size() >= max_depth_) {
        fprintf(stderr, "Stack overflow: call depth exceeds maximum %zu\n", max_depth_);
        if (state) {
            state->setError(MOBIUS_ERROR_RUNTIME,
                            "Stack overflow: call depth exceeded maximum",
                            "Reduce recursion depth or increase the configured call-depth limit",
                            line, column, function_name, filename);
        }
        return;
    }

    CallFrame frame;
    frame.function_name = function_name;
    frame.filename = filename;
    frame.line = line;
    frame.column = column;
    frame.type = type;
    frame.function_ptr = function_ptr;
    frame.stack_base = 0;
    frame.stack_top = 0;
    frame.start_time = get_time_ns();

    call_frames_.push_back(frame);
}

void ExecutionContext::popFrame() {
    if (call_frames_.empty()) {
        return;
    }

    call_frames_.pop_back();
}

void ExecutionContext::clearFrames() {
    call_frames_.clear();
}

size_t ExecutionContext::frameDepth() const {
    return call_frames_.size();
}

bool ExecutionContext::isStackOverflow() const {
    return call_frames_.size() >= max_depth_;
}

void ExecutionContext::printStackTrace() const {
    if (call_frames_.empty()) {
        printf("Stack trace: (empty)\n");
        return;
    }

    printf("Stack trace:\n");
    for (size_t i = 0; i < call_frames_.size(); i++) {
        const CallFrame& frame = call_frames_[i];

        const char* type_str;
        switch (frame.type) {
            case FUNCTION_TYPE_NATIVE:  type_str = "native"; break;
            case FUNCTION_TYPE_SCRIPT:  type_str = "script"; break;
            case FUNCTION_TYPE_PLUGIN:  type_str = "plugin"; break;
            case FUNCTION_TYPE_CLOSURE: type_str = "closure"; break;
            default: type_str = "unknown"; break;
        }

        printf("  [%zu] %s (%s)", i, 
               frame.function_name ? frame.function_name : "<anonymous>",
               type_str);

        if (frame.filename) {
            printf(" at %s:%d:%d", frame.filename, frame.line, frame.column);
        } else if (frame.line > 0) {
            printf(" at line %d:%d", frame.line, frame.column);
        }

        printf("\n");
    }
}

char* ExecutionContext::formatStackTrace() const {
    if (call_frames_.empty()) {
        return mobius_strdup("Stack trace: (empty)\n");
    }

    size_t buffer_size = 256 * call_frames_.size() + 100;
    char* buffer = (char*)malloc(buffer_size);
    if (!buffer) return NULL;

    size_t offset = 0;
    offset += snprintf(buffer + offset, buffer_size - offset, "Stack trace:\n");

    for (size_t i = 0; i < call_frames_.size() && offset < buffer_size - 1; i++) {
        const CallFrame& frame = call_frames_[i];

        const char* type_str;
        switch (frame.type) {
            case FUNCTION_TYPE_NATIVE:  type_str = "native"; break;
            case FUNCTION_TYPE_SCRIPT:  type_str = "script"; break;
            case FUNCTION_TYPE_PLUGIN:  type_str = "plugin"; break;
            case FUNCTION_TYPE_CLOSURE: type_str = "closure"; break;
            default: type_str = "unknown"; break;
        }

        offset += snprintf(buffer + offset, buffer_size - offset,
                          "  [%zu] %s (%s)", i,
                          frame.function_name ? frame.function_name : "<anonymous>",
                          type_str);

        if (frame.filename) {
            offset += snprintf(buffer + offset, buffer_size - offset,
                             " at %s:%d:%d", frame.filename, frame.line, frame.column);
        } else if (frame.line > 0) {
            offset += snprintf(buffer + offset, buffer_size - offset,
                             " at line %d:%d", frame.line, frame.column);
        }

        offset += snprintf(buffer + offset, buffer_size - offset, "\n");
    }

    return buffer;
}

StackTrace* ExecutionContext::captureStackTrace() const {
    if (call_frames_.empty()) {
        return NULL;
    }

    StackTrace* trace = (StackTrace*)malloc(sizeof(StackTrace));
    if (!trace) return NULL;

    trace->frame_count = call_frames_.size();
    trace->frames = (TraceFrame*)calloc(call_frames_.size(), sizeof(TraceFrame));
    if (!trace->frames) {
        free(trace);
        return NULL;
    }

    for (size_t i = 0; i < call_frames_.size(); i++) {
        const CallFrame& src = call_frames_[i];
        TraceFrame* dst = &trace->frames[i];

        dst->function_name = src.function_name ? mobius_strdup(src.function_name) : NULL;
        dst->filename = src.filename ? mobius_strdup(src.filename) : NULL;
        if ((src.function_name && !dst->function_name) ||
            (src.filename && !dst->filename)) {
            free_stack_trace(trace);
            return NULL;
        }
        dst->line = src.line;
        dst->column = src.column;

        switch (src.type) {
            case FUNCTION_TYPE_NATIVE:  dst->type = TRACE_FUNCTION_NATIVE; break;
            case FUNCTION_TYPE_SCRIPT:  dst->type = TRACE_FUNCTION_SCRIPT; break;
            case FUNCTION_TYPE_PLUGIN:  dst->type = TRACE_FUNCTION_PLUGIN; break;
            case FUNCTION_TYPE_CLOSURE: dst->type = TRACE_FUNCTION_CLOSURE; break;
            default: dst->type = TRACE_FUNCTION_SCRIPT; break;
        }
    }

    return trace;
}

// ============================================================================
// INTERNAL HELPERS
// ============================================================================

void free_internal_error(InternalError* error) {
    if (!error) return;
    free(error->message);
    free(error->suggestion);
    free(error->filename);
    free(error->function_name);
    free(error);
}

// ============================================================================
// MOBIUS STATE IMPLEMENTATION
// ============================================================================

static void default_error_handler(MobiusState* state, const MobiusError* error, void* userdata);

static MOBIUS_FORCEINLINE MobiusVM* executing_vm_for_state(const MobiusState* state) {
    MobiusVM* vm = MobiusVM::t_current_vm;
    return (vm && vm->state_ == state) ? vm : nullptr;
}

namespace {
struct ScopedCurrentVMOverride {
    MobiusVM* previous;

    explicit ScopedCurrentVMOverride(MobiusVM* vm) : previous(MobiusVM::t_current_vm) {
        MobiusVM::t_current_vm = vm;
    }

    ~ScopedCurrentVMOverride() {
        MobiusVM::t_current_vm = previous;
    }
};
}

MobiusState::MobiusState(MobiusConfig* config)
    : registry_(nullptr), string_pool_(nullptr),
      metamethods_(nullptr), job_system_(nullptr),
      error_handler_(default_error_handler), error_handler_userdata_(nullptr),
      metrics_{}, initialized_(false),
      fallback_last_error_(nullptr), fallback_source_code_(nullptr) {

    config_ = config ? *config : mobius_default_config();

    root_globals_.slots.resize(kGlobalSlotCapacity);
    root_globals_.slot_names.resize(kGlobalSlotCapacity);

    string_pool_ = new (std::nothrow) StringInternPool(kInitialStringBucketCount);
    if (!string_pool_) return;

    common_strings_.empty = string_pool_->intern("", 0);
    common_strings_.nil = string_pool_->intern("nil", 3);
    common_strings_.true_value = string_pool_->intern("true", 4);
    common_strings_.false_value = string_pool_->intern("false", 5);
    common_strings_.null_string = string_pool_->intern("(null)", 6);
    common_strings_.function = string_pool_->intern("<function>", 10);
    common_strings_.native_function = string_pool_->intern("<native function>", 17);
    common_strings_.table = string_pool_->intern("<table>", 7);
    common_strings_.array = string_pool_->intern("<array>", 7);
    common_strings_.userdata_null = string_pool_->intern("<userdata (null)>", 17);
    common_strings_.shared_null = string_pool_->intern("<shared null>", 13);
    common_strings_.buffer_null = string_pool_->intern("<buffer (null)>", 15);
    common_strings_.unknown = string_pool_->intern("unknown", 7);

    metamethods_ = new (std::nothrow) Metamethods(string_pool_);
    if (!metamethods_) return;

    registry_ = getGlobalRegistry();
    if (!registry_) return;

    job_system_ = new (std::nothrow) JobSystem(this);
    if (!job_system_) return;

    auto defineGlobal = [&](const char* name, Value val, bool readonly = false) {
        int slot = assignGlobalSlot(name);
        if (slot < 0) return;
        val.flags |= VAL_FLAG_DEFINED;
        if (readonly) val.flags |= VAL_FLAG_READONLY;
        setGlobalValue(slot, val, nullptr, false);
    };
    defineGlobal("nil", make_nil_value(), true);
    defineGlobal("true", make_bool_value(true), true);
    defineGlobal("false", make_bool_value(false), true);
    defineGlobal("inf", make_float_value(1.0 / 0.0), true);
    defineGlobal("nan", make_float_value(0.0 / 0.0), true);

    main_vm_ = new (std::nothrow) MobiusVM(this);
}

MobiusState::~MobiusState() {
    delete job_system_;

    {
        std::lock_guard<std::mutex> lock(type_metatables_mutex_);
        for (int i = 0; i < VALUE_TYPE_COUNT; i++) {
            if (type_metatables_[i]) {
                type_metatables_[i]->release();
                type_metatables_[i] = nullptr;
            }
        }
    }
    {
        std::lock_guard<std::mutex> lock(userdata_type_metatables_mutex_);
        for (auto& entry : userdata_type_metatables_) {
            if (entry.second) entry.second->release();
        }
        userdata_type_metatables_.clear();
    }

    for (Prototype* p : owned_protos_) {
        delete p;
    }
    owned_protos_.clear();

    delete metamethods_;

    // The module registry is a global singleton freed via atexit(), which runs
    // AFTER this state frees its string pool below. Its cached module
    // environments hold copies of this state's global slots — i.e. this state's
    // interned strings — so release those Values now, while the pool is alive.
    // Otherwise the atexit teardown dereferences freed strings.
    if (registry_) registry_->releaseModuleValues();
    registry_ = nullptr;

    // Destroy main VM before the string pool — VM registers hold
    // MobiusString* pointers that must not dangle during teardown.
    delete main_vm_;
    main_vm_ = nullptr;

    // Clear all Value containers BEFORE destroying the string pool.
    for (int i = 0; i < root_globals_.count.load(std::memory_order_relaxed); i++)
        root_globals_.slots[i] = make_nil_value();

    // GC-managed objects (tables, arrays, closures, upvalues) are freed only
    // by the collector; every root above has been dropped, so sweep the lot.
    // Must precede string-pool deletion — destructors release string refs.
    // (The registry is process-global; with multiple concurrent states this
    // would need per-state partitioning — see the stage-5 plan.)
    gc_collect_all_for_teardown();

    delete string_pool_;

    if (fallback_last_error_) {
        free_internal_error(fallback_last_error_);
    }
}

void MobiusState::setTypeMetatable(ValueType t, Table* mt) {
    std::lock_guard<std::mutex> lock(type_metatables_mutex_);
    if (type_metatables_[t]) {
        type_metatables_[t]->release();
    }
    type_metatables_[t] = mt;
    if (mt) mt->retain();
}

Table* MobiusState::userdataTypeMetatable(MobiusString* type_tag) const {
    if (!type_tag) return nullptr;
    std::lock_guard<std::mutex> lock(userdata_type_metatables_mutex_);
    auto it = userdata_type_metatables_.find(type_tag);
    return it != userdata_type_metatables_.end() ? it->second : nullptr;
}

void MobiusState::setUserdataTypeMetatable(MobiusString* type_tag, Table* mt) {
    if (!type_tag) return;
    std::lock_guard<std::mutex> lock(userdata_type_metatables_mutex_);
    auto it = userdata_type_metatables_.find(type_tag);
    if (it != userdata_type_metatables_.end()) {
        if (it->second) it->second->release();
        if (mt) {
            mt->retain();
            it->second = mt;
        } else {
            userdata_type_metatables_.erase(it);
        }
        return;
    }
    if (mt) {
        mt->retain();
        userdata_type_metatables_[type_tag] = mt;
    }
}

void MobiusState::forEachValueRef(void (*cb)(const Value&, void*), void* ud) {
    std::lock_guard<std::mutex> lock(value_refs_mutex_);
    for (auto& kv : value_refs_) cb(kv.second, ud);
}

void MobiusState::gcVisitRoots(void (*value_cb)(const Value&, void*),
                               void (*table_cb)(Table*, void*), void* ud) {
    {
        int n = root_globals_.count.load(std::memory_order_relaxed);
        for (int i = 0; i < n && i < (int)root_globals_.slots.size(); i++)
            value_cb(root_globals_.slots[i], ud);
    }
    forEachValueRef(value_cb, ud);
    {
        std::lock_guard<std::mutex> lock(type_metatables_mutex_);
        for (int i = 0; i < VALUE_TYPE_COUNT; i++)
            if (type_metatables_[i]) table_cb(type_metatables_[i], ud);
    }
    {
        std::lock_guard<std::mutex> lock(userdata_type_metatables_mutex_);
        for (auto& kv : userdata_type_metatables_)
            if (kv.second) table_cb(kv.second, ud);
    }
}

MobiusValueRef MobiusState::createValueRef(const Value& value) {
    std::lock_guard<std::mutex> lock(value_refs_mutex_);
    MobiusValueRef ref = next_value_ref_++;
    if (next_value_ref_ == 0) next_value_ref_ = 1;
    value_refs_[ref] = value;
    return ref;
}

bool MobiusState::releaseValueRef(MobiusValueRef ref) {
    if (ref == 0) return false;
    std::lock_guard<std::mutex> lock(value_refs_mutex_);
    auto it = value_refs_.find(ref);
    if (it == value_refs_.end()) return false;
    value_refs_.erase(it);
    return true;
}

bool MobiusState::copyValueRef(MobiusValueRef ref, Value* out) const {
    if (!out || ref == 0) return false;
    std::lock_guard<std::mutex> lock(value_refs_mutex_);
    auto it = value_refs_.find(ref);
    if (it == value_refs_.end()) return false;
    *out = it->second;
    return true;
}

int MobiusState::callValue(const Value& function, const Value* args, int nargs,
                           int nresults, std::vector<Value>* out_results) {
    if (!out_results) {
        return setError(MOBIUS_ERROR_ARGUMENT,
                        "callValue() requires an output results vector",
                        nullptr, 0, 0, nullptr);
    }
    out_results->clear();

    if (function.type != VAL_FUNCTION && function.type != VAL_NATIVE_FUNCTION) {
        return setError(MOBIUS_ERROR_TYPE,
                        "callValue() target is not callable",
                        nullptr, 0, 0, nullptr);
    }

    if (nargs < 0 || nresults < 0) {
        return setError(MOBIUS_ERROR_ARGUMENT,
                        "callValue() argument counts must be non-negative",
                        nullptr, 0, 0, nullptr);
    }

    MobiusVM* vm = activeVM();
    if (!vm) vm = main_vm_;
    if (!vm) {
        return setError(MOBIUS_ERROR_RUNTIME,
                        "callValue() requires a bound VM",
                        nullptr, 0, 0, nullptr);
    }

    ScopedCurrentVMOverride bind_vm(vm);

    NativeCallContext* nctx = &vm->native_ctx_;
    Value* saved_registers = nctx->registers;
    int saved_capacity = nctx->capacity;
    int saved_base = nctx->base;
    int saved_top = nctx->top;

    if (function.type == VAL_NATIVE_FUNCTION) {
        int scratch = activeVM() && vm->callStackTop().proto
            ? vm->callStackTop().base + vm->callStackTop().proto->num_registers
            : 0;
        vm->ensureRegisters(scratch + nargs + 16);
        for (int i = 0; i < nargs; i++) {
            vm->registers_[scratch + i] = args ? args[i] : Value();
        }

        nctx->registers = vm->registers_.data();
        nctx->capacity = (int)vm->registers_.size();
        nctx->base = scratch;
        nctx->top = scratch + nargs;

        int rc = function.as.native_function(this, nargs);
        int result_top = nctx->top;

        nctx->registers = saved_registers;
        nctx->capacity = saved_capacity;
        nctx->base = saved_base;
        nctx->top = saved_top;

        if (rc < 0) return rc;
        int result_count = (nresults == 0) ? rc : std::min(rc, nresults);
        for (int i = 0; i < result_count; i++) {
            if (scratch + i < result_top) out_results->push_back(vm->registers_[scratch + i]);
            else out_results->push_back(Value());
        }
        return result_count;
    }

    MobiusFunction* mf = function.as.function;
    if (!mf || !mf->proto) {
        nctx->registers = saved_registers;
        nctx->capacity = saved_capacity;
        nctx->base = saved_base;
        nctx->top = saved_top;
        return setError(MOBIUS_ERROR_RUNTIME,
                        "callValue() function has no bytecode prototype",
                        nullptr, 0, 0, nullptr);
    }

    if ((int)mf->param_count != nargs) {
        nctx->registers = saved_registers;
        nctx->capacity = saved_capacity;
        nctx->base = saved_base;
        nctx->top = saved_top;
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "Function '%s' expects %zu arguments but got %d",
                 mf->name ? mf->name->data : "anonymous",
                 mf->param_count, nargs);
        return setError(MOBIUS_ERROR_ARGUMENT, buf, nullptr, 0, 0, nullptr);
    }

    Prototype* child_proto = mf->proto;
    int pcall_base = activeVM() && vm->callStackTop().proto
        ? vm->callStackTop().base + vm->callStackTop().proto->num_registers
        : 0;
    int child_base = pcall_base + 1;
    int needed = child_base + child_proto->num_registers + 16;
    vm->ensureRegisters(needed);

    vm->registers_[pcall_base] = function;
    for (int i = 0; i < nargs; i++) {
        vm->registers_[child_base + i] = args ? args[i] : Value();
    }

    nctx->registers = vm->registers_.data();
    nctx->capacity = (int)vm->registers_.size();

    CallInfo& child_ci = vm->callStackPush(child_proto, child_proto->code.data(),
                                           child_base, nresults + 1);
    if (mf->upvalues && mf->upvalue_count > 0) {
        if (!child_ci.setUpvaluesFrom(mf->upvalues, mf->upvalue_count)) {
            vm->callStackPop();
            nctx->registers = saved_registers;
            nctx->capacity = saved_capacity;
            nctx->base = saved_base;
            nctx->top = saved_top;
            return setError(MOBIUS_ERROR_MEMORY,
                            "Failed to allocate closure upvalues",
                            nullptr, 0, 0, nullptr);
        }
    }

    size_t depth = vm->callStackSize() - 1;
    int rc = vm->run(depth);

    nctx->registers = saved_registers;
    nctx->capacity = saved_capacity;
    nctx->base = saved_base;
    nctx->top = saved_top;

    if (rc < 0) return rc;

    int result_count = (nresults > 0) ? nresults : 1;
    for (int i = 0; i < result_count; i++) {
        out_results->push_back(vm->registers_[pcall_base + i]);
    }
    return result_count;
}

void MobiusState::clearErrorInternal() {
    MobiusVM* vm = boundVM();
    InternalError*& err = vm ? vm->last_error_ : fallback_last_error_;
    if (err) {
        free_internal_error(err);
        err = nullptr;
    }
}

int MobiusState::assignGlobalSlot(const char* name, GlobalEnvironment* env) {
    GlobalEnvironment* globals = env_or_root(this, env);
    std::lock_guard<std::mutex> lock(globals->mutex);
    auto it = globals->slot_map.find(name);
    if (it != globals->slot_map.end()) return it->second;
    int slot = globals->count.load(std::memory_order_relaxed);
    if (slot >= (int)globals->slots.size()) {
        setError(MOBIUS_ERROR_MEMORY,
                 "Global slot capacity exceeded",
                 "Increase the preallocated global slot capacity",
                 0, 0, nullptr);
        return -1;
    }
    globals->slot_names[slot] = name;
    globals->slot_map[name] = slot;
    globals->count.store(slot + 1, std::memory_order_release);
    if ((size_t)(slot + 1) > metrics_.peak_globals)
        metrics_.peak_globals = (size_t)(slot + 1);
    return slot;
}

Value& MobiusState::globalSlot(int idx, GlobalEnvironment* env) {
    GlobalEnvironment* globals = env_or_root(this, env);
    int count = globals->count.load(std::memory_order_acquire);
    if (idx < 0 || idx >= count || (size_t)idx >= globals->slots.size()) {
        return invalidNativeValue();
    }
    return globals->slots[idx];
}

const Value& MobiusState::globalSlot(int idx, GlobalEnvironment* env) const {
    const GlobalEnvironment* globals = env_or_root(this, env);
    int count = globals->count.load(std::memory_order_acquire);
    if (idx < 0 || idx >= count || (size_t)idx >= globals->slots.size()) {
        return invalidNativeValue();
    }
    return globals->slots[idx];
}

bool MobiusState::copyGlobalValueShared(int idx, Value* out, const GlobalEnvironment* globals) const {
    std::lock_guard<std::mutex> lock(globals->mutex);
    int count = globals->count.load(std::memory_order_acquire);
    if (!global_slot_in_bounds(globals, idx, count)) return false;
    if (out) *out = globals->slots[idx];
    return true;
}

Value MobiusState::getGlobalValue(int idx, GlobalEnvironment* env) const {
    const GlobalEnvironment* globals = env_or_root(this, env);
    int count = globals->count.load(std::memory_order_acquire);
    if (!global_slot_in_bounds(globals, idx, count)) {
        return make_nil_value();
    }
    if (!globals->shared.load(std::memory_order_acquire)) {
        return globals->slots[idx];
    }
    std::lock_guard<std::mutex> lock(globals->mutex);
    count = globals->count.load(std::memory_order_acquire);
    if (!global_slot_in_bounds(globals, idx, count)) return make_nil_value();
    return globals->slots[idx];
}

int MobiusState::globalSlotCount(GlobalEnvironment* env) const {
    return env_or_root(this, env)->count.load(std::memory_order_relaxed);
}

int MobiusState::findGlobalSlot(const char* name, GlobalEnvironment* env) const {
    const GlobalEnvironment* globals = env_or_root(this, env);
    if (!globals->shared.load(std::memory_order_acquire)) {
        auto it = globals->slot_map.find(name);
        if (it != globals->slot_map.end()) return it->second;
        return -1;
    }
    std::lock_guard<std::mutex> lock(globals->mutex);
    auto it = globals->slot_map.find(name);
    if (it != globals->slot_map.end()) return it->second;
    return -1;
}

const char* MobiusState::globalSlotName(int idx, GlobalEnvironment* env) const {
    const GlobalEnvironment* globals = env_or_root(this, env);
    int count = globals->count.load(std::memory_order_acquire);
    if (!global_slot_in_bounds(globals, idx, count)) return "<unknown>";
    if (!globals->shared.load(std::memory_order_acquire)) {
        const std::string& name = globals->slot_names[idx];
        return name.empty() ? "<unknown>" : name.c_str();
    }
    std::lock_guard<std::mutex> lock(globals->mutex);
    count = globals->count.load(std::memory_order_acquire);
    if (!global_slot_in_bounds(globals, idx, count)) return "<unknown>";
    const std::string& name = globals->slot_names[idx];
    return name.empty() ? "<unknown>" : name.c_str();
}

void MobiusState::setGlobalReadonly(const char* name, bool readonly) {
    GlobalEnvironment* globals = rootGlobalEnvironment();
    std::lock_guard<std::mutex> lock(globals->mutex);
    auto it = globals->slot_map.find(name);
    if (it == globals->slot_map.end()) return;
    int slot = it->second;
    if (readonly)
        globals->slots[slot].flags |= VAL_FLAG_READONLY;
    else
        globals->slots[slot].flags &= ~VAL_FLAG_READONLY;
}

void MobiusState::setGlobalReadonly(int slot, bool readonly, GlobalEnvironment* env) {
    GlobalEnvironment* globals = env_or_root(this, env);
    std::lock_guard<std::mutex> lock(globals->mutex);
    int count = globals->count.load(std::memory_order_acquire);
    if (!global_slot_in_bounds(globals, slot, count)) return;
    if (readonly)
        globals->slots[slot].flags |= VAL_FLAG_READONLY;
    else
        globals->slots[slot].flags &= ~VAL_FLAG_READONLY;
}

bool MobiusState::removeGlobal(const char* name) {
    GlobalEnvironment* globals = rootGlobalEnvironment();
    std::lock_guard<std::mutex> lock(globals->mutex);
    auto it = globals->slot_map.find(name);
    if (it == globals->slot_map.end()) return false;
    int slot = it->second;
    globals->slots[slot] = Value();
    globals->slots[slot].flags = 0;
    globals->slot_map.erase(it);
    return true;
}

void MobiusState::setGlobalValue(int slot, const Value& value, GlobalEnvironment* env, bool mark_defined) {
    GlobalEnvironment* globals = env_or_root(this, env);
    int count = globals->count.load(std::memory_order_acquire);
    if (!global_slot_in_bounds(globals, slot, count)) {
        setError(MOBIUS_ERROR_ARGUMENT, "Global slot index out of bounds", nullptr, 0, 0, nullptr);
        return;
    }
    if (!globals->shared.load(std::memory_order_acquire)) {
        Value updated = value;
        if (mark_defined) updated.flags |= VAL_FLAG_DEFINED;
        globals->slots[slot] = updated;
        if (globals->backing_table) {
            const std::string& name = globals->slot_names[slot];
            if (!name.empty()) {
                MobiusString* key = string_pool_->intern(name.c_str());
                globals->backing_table->setByString(key, globals->slots[slot]);
            }
        }
        return;
    }
    std::lock_guard<std::mutex> lock(globals->mutex);
    count = globals->count.load(std::memory_order_acquire);
    if (!global_slot_in_bounds(globals, slot, count)) {
        setError(MOBIUS_ERROR_ARGUMENT, "Global slot index out of bounds", nullptr, 0, 0, nullptr);
        return;
    }
    Value updated = value;
    if (mark_defined) updated.flags |= VAL_FLAG_DEFINED;
    globals->slots[slot] = updated;
    if (globals->backing_table) {
        const std::string& name = globals->slot_names[slot];
        if (!name.empty()) {
            MobiusString* key = string_pool_->intern(name.c_str());
            globals->backing_table->setByString(key, globals->slots[slot]);
        }
    }
}

void MobiusState::syncGlobalSlotToBackingTable(int slot, GlobalEnvironment* env) {
    GlobalEnvironment* globals = env_or_root(this, env);
    int count = globals->count.load(std::memory_order_acquire);
    if (!global_slot_in_bounds(globals, slot, count) || !globals->backing_table) return;
    if (!globals->shared.load(std::memory_order_acquire)) {
        const std::string& name = globals->slot_names[slot];
        if (name.empty()) return;
        MobiusString* key = string_pool_->intern(name.c_str());
        globals->backing_table->setByString(key, globals->slots[slot]);
        return;
    }
    std::lock_guard<std::mutex> lock(globals->mutex);
    count = globals->count.load(std::memory_order_acquire);
    if (!global_slot_in_bounds(globals, slot, count) || !globals->backing_table) return;
    const std::string& name = globals->slot_names[slot];
    if (name.empty()) return;
    MobiusString* key = string_pool_->intern(name.c_str());
    globals->backing_table->setByString(key, globals->slots[slot]);
}

void MobiusState::seedGlobalEnvironmentFromTable(GlobalEnvironment* env, Table* table) {
    if (!env) return;
    env->backing_table = table;
    if (!table) return;
    table->forEach([&](const Value& key, const Value& value) {
        if (key.type != VAL_STRING || !key.as.string) return;
        int slot = assignGlobalSlot(key.as.string->data, env);
        if (slot < 0) return;
        Value defined_value = value;
        if (!(defined_value.flags & VAL_FLAG_DEFINED)) defined_value.flags |= VAL_FLAG_DEFINED;
        setGlobalValue(slot, defined_value, env, false);
    });
}

void MobiusState::removeGlobalSlots(int from_slot, GlobalEnvironment* env) {
    GlobalEnvironment* globals = env_or_root(this, env);
    std::lock_guard<std::mutex> lock(globals->mutex);
    int count = globals->count.load(std::memory_order_relaxed);
    if (from_slot < 0 || from_slot >= count) return;

    std::vector<std::string> to_remove;
    for (auto& kv : globals->slot_map) {
        if (kv.second >= from_slot) {
            to_remove.push_back(kv.first);
        }
    }
    for (auto& name : to_remove) {
        globals->slot_map.erase(name);
    }

    for (int i = from_slot; i < count; i++) {
        globals->slots[i] = Value();
    }
    globals->count.store(from_slot, std::memory_order_release);
}

int MobiusState::initStdlib() {
    register_stdlib_functions(this);

    Table* fiber_mod = register_fiber_module(this);
    registry_->registerBuiltinModule("fiber", fiber_mod);

    // Expose `fiber` as a builtin global table so scripts can call
    // fiber.channel(), fiber.sleep(), fiber.all(), etc. without an
    // `import "fiber"`. The concurrency keywords (spawn/await/yield/
    // shared/atomic) and channel methods are already always available;
    // this makes the constructors and helpers first-class too.
    int fiber_slot = assignGlobalSlot("fiber");
    if (fiber_slot >= 0) {
        fiber_mod->retain();
        setGlobalValue(fiber_slot, make_table_value(fiber_mod));
    }

    Table* channel_mt = create_channel_type_metatable(this);
    setTypeMetatable(VAL_CHANNEL, channel_mt);
    channel_mt->release();

    Table* array_mt = create_array_type_metatable(this);
    setTypeMetatable(VAL_ARRAY, array_mt);
    array_mt->release();

    Table* table_mt = create_table_type_metatable(this);
    setTypeMetatable(VAL_TABLE, table_mt);
    table_mt->release();

    Table* buffer_mt = create_buffer_type_metatable(this);
    setTypeMetatable(VAL_BUFFER, buffer_mt);
    buffer_mt->release();

    Table* struct_layout_mt = create_struct_layout_metatable(this);
    setUserdataTypeMetatable(string_pool_->intern("StructLayout"), struct_layout_mt);
    struct_layout_mt->release();

    Table* struct_view_mt = create_struct_view_metatable(this);
    setUserdataTypeMetatable(string_pool_->intern("StructView"), struct_view_mt);
    struct_view_mt->release();

    Table* struct_array_view_mt = create_struct_array_view_metatable(this);
    setUserdataTypeMetatable(string_pool_->intern("StructArrayView"), struct_array_view_mt);
    struct_array_view_mt->release();

    initialized_ = true;
    return MOBIUS_OK;
}

void MobiusState::addOwnedProto(Prototype* proto) {
    std::lock_guard<std::mutex> lock(owned_protos_mutex_);
    owned_protos_.push_back(proto);
}

void MobiusState::addPluginDirectory(const char* directory) {
    if (!directory) return;
    std::lock_guard<std::mutex> lock(plugin_dirs_mutex_);
    plugin_directories_.emplace_back(directory);
}

void MobiusState::clearPluginDirectories() {
    std::lock_guard<std::mutex> lock(plugin_dirs_mutex_);
    plugin_directories_.clear();
}

int MobiusState::execString(const char* code) {
    return execStringInEnvironment(code, nullptr);
}

int MobiusState::execStringInEnvironment(const char* code, GlobalEnvironment* env) {
    if (!code) return MOBIUS_ERROR_ARGUMENT;

    clearErrorInternal();

    TokenArray tokens = scan_source(code, string_pool_);
    if (tokens.count == 0) {
        free_token_array(&tokens);
        setError(MOBIUS_ERROR_SYNTAX, "No tokens found", NULL, 0, 0, NULL);
        return MOBIUS_ERROR_SYNTAX;
    }

    ParseResult parse_result = parse(this, tokens);
    free_token_array(&tokens);

    if (parse_result.had_error) {
        const char* src = getSourceContext();
        setError(MOBIUS_ERROR_SYNTAX, "Parse error", 
                 "Check syntax and structure", 0, 0, NULL, src);
        free_parse_result(&parse_result);
        return MOBIUS_ERROR_SYNTAX;
    }

    const char* src = getSourceContext();
    Compiler compiler(string_pool_, this, env_or_root(this, env));
    Prototype* proto = compiler.compile(parse_result.statements,
                                        parse_result.count,
                                        src ? src : "<string>");
    free_parse_result(&parse_result);

    if (!proto) {
        setError(MOBIUS_ERROR_RUNTIME, "Bytecode compilation failed",
                 nullptr, 0, 0, nullptr);
        return MOBIUS_ERROR_RUNTIME;
    }

    if (config_.debug_mode) {
        disassemble_prototype(proto);
    }

    addOwnedProto(proto);

    int rc = main_vm_->execute(proto);

    if (rc != 0) {
        return MOBIUS_ERROR_RUNTIME;
    }
    return MOBIUS_OK;
}

int MobiusState::execFile(const char* filename) {
    return execFileInEnvironment(filename, nullptr);
}

int MobiusState::execFileInEnvironment(const char* filename, GlobalEnvironment* env) {
    if (!filename) return MOBIUS_ERROR_ARGUMENT;

    FileResult file_result = read_file(filename);
    if (!file_result.success) {
        setError(MOBIUS_ERROR_RUNTIME, 
                 file_result.error ? file_result.error : "Failed to read file", 
                 "Check that file exists and is readable", 
                 0, 0, NULL);
        return MOBIUS_ERROR_RUNTIME;
    }

    const char* saved_source = getSourceContext();
    setSourceContext(filename);
    int result = execStringInEnvironment(file_result.content, env);
    setSourceContext(saved_source);
    free_file_result(&file_result);
    return result;
}

// ============================================================================
// ERROR HANDLING
// ============================================================================

InternalError* MobiusState::getLastError() const {
    MobiusVM* vm = boundVM();
    InternalError* err = vm ? vm->last_error_ : fallback_last_error_;
    if (!err) return NULL;

    InternalError* copy = (InternalError*)malloc(sizeof(InternalError));
    if (!copy) return NULL;

    copy->code = err->code;
    copy->message = err->message ? mobius_strdup(err->message) : NULL;
    copy->suggestion = err->suggestion ? mobius_strdup(err->suggestion) : NULL;
    copy->filename = err->filename ? mobius_strdup(err->filename) : NULL;
    copy->line = err->line;
    copy->column = err->column;
    copy->function_name = err->function_name ? mobius_strdup(err->function_name) : NULL;

    return copy;
}

void MobiusState::clearError() {
    clearErrorInternal();
}

int MobiusState::setError(int code, const char* message, const char* suggestion,
                          int line, int column, const char* function_name,
                          const char* filename) {
    clearErrorInternal();

    MobiusVM* vm = boundVM();
    InternalError*& err_slot = vm ? vm->last_error_ : fallback_last_error_;

    err_slot = (InternalError*)malloc(sizeof(InternalError));
    if (!err_slot) return code;

    err_slot->code = code;
    err_slot->message = message ? mobius_strdup(message) : NULL;
    err_slot->suggestion = suggestion ? mobius_strdup(suggestion) : NULL;
    err_slot->filename = filename ? mobius_strdup(filename) : NULL;
    err_slot->line = line;
    err_slot->column = column;
    err_slot->function_name = function_name ? mobius_strdup(function_name) : NULL;

    if (error_handler_) {
        MobiusError pub_err;
        pub_err.code = code;
        pub_err.message = message;
        pub_err.suggestion = suggestion;
        pub_err.filename = filename;
        pub_err.line = line;
        pub_err.column = column;
        pub_err.function_name = function_name;
        error_handler_(this, &pub_err, error_handler_userdata_);
    }

    return code;
}

int MobiusState::error(const char* message) {
    setError(MOBIUS_ERROR_RUNTIME, message, NULL, 0, 0, NULL);
    return -1;
}

MobiusErrorHandler MobiusState::setErrorHandler(MobiusErrorHandler handler, void* userdata) {
    MobiusErrorHandler old = error_handler_;
    if (handler) {
        error_handler_ = handler;
        error_handler_userdata_ = userdata;
    } else {
        error_handler_ = default_error_handler;
        error_handler_userdata_ = NULL;
    }
    return (old == default_error_handler) ? NULL : old;
}

static void default_error_handler(MobiusState* state, const MobiusError* error, void* userdata) {
    (void)state;
    (void)userdata;
    fprintf(stderr, "Error");
    if (error->filename && error->line > 0) {
        fprintf(stderr, " [%s:%d:%d]", error->filename, error->line, error->column);
    } else if (error->filename) {
        fprintf(stderr, " [%s]", error->filename);
    } else if (error->line > 0) {
        fprintf(stderr, " [line %d:%d]", error->line, error->column);
    }
    if (error->function_name) {
        fprintf(stderr, " in %s", error->function_name);
    }
    fprintf(stderr, ": %s\n", error->message ? error->message : "Unknown error");
    if (error->suggestion) {
        fprintf(stderr, "  suggestion: %s\n", error->suggestion);
    }
}

// ============================================================================
// SOURCE CODE CONTEXT
// ============================================================================

void MobiusState::setSourceContext(const char* source) {
    MobiusVM* vm = boundVM();
    if (vm) vm->source_code_ = source;
    else fallback_source_code_ = source;
}

const char* MobiusState::getSourceContext() const {
    MobiusVM* vm = boundVM();
    return vm ? vm->source_code_ : fallback_source_code_;
}

// ============================================================================
// THREAD-LOCAL VM FORWARDING
// ============================================================================

MobiusVM* MobiusState::activeVM() const {
    return executing_vm_for_state(this);
}

MobiusVM* MobiusState::boundVM() const {
    MobiusVM* vm = activeVM();
    return vm ? vm : main_vm_;
}

Value& MobiusState::invalidNativeValue() {
    static Value nil_value = make_nil_value();
    return nil_value;
}

NativeCallContext* MobiusState::checkedNativeContext(int required_count, bool require_self,
                                                     bool for_push) const {
    NativeCallContext* ctx = nativeContext();
    if (!ctx) {
        const_cast<MobiusState*>(this)->setError(
            MOBIUS_ERROR_RUNTIME,
            "Native call attempted without an available VM context",
            "Ensure the state has an active or persistent VM before using native stack helpers",
            0, 0, nullptr);
        return nullptr;
    }

    if (require_self && ctx->base >= ctx->top) {
        const_cast<MobiusState*>(this)->setError(
            MOBIUS_ERROR_ARGUMENT,
            "Native stack has no self value",
            "Call the method with ':' syntax or provide a receiver value",
            0, 0, nullptr);
        return nullptr;
    }

    int available = ctx->top - ctx->base;
    if (required_count > 0 && available < required_count) {
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "Native stack underflow (needed %d values, found %d)",
                 required_count, available);
        const_cast<MobiusState*>(this)->setError(MOBIUS_ERROR_ARGUMENT, buf, nullptr, 0, 0, nullptr);
        return nullptr;
    }

    if (for_push && ctx->top >= ctx->capacity) {
        const_cast<MobiusState*>(this)->setError(
            MOBIUS_ERROR_MEMORY,
            "Native stack overflow",
            "Reduce the number of values pushed to the stack",
            0, 0, nullptr);
        return nullptr;
    }

    return ctx;
}

NativeCallContext* MobiusState::nativeContext() const {
    MobiusVM* vm = boundVM();
    return vm ? &vm->native_ctx_ : nullptr;
}

ExecutionContext* MobiusState::mainContext() const {
    MobiusVM* vm = boundVM();
    return vm ? vm->exec_context_ : nullptr;
}

InternalError* MobiusState::lastError() const {
    MobiusVM* vm = boundVM();
    return vm ? vm->last_error_ : fallback_last_error_;
}

// ============================================================================
// REPL
// ============================================================================

void MobiusState::startRepl() {
    Repl repl(this);
    repl.run();
}

// ============================================================================
// PUBLIC C API WRAPPERS
// ============================================================================

extern "C" {

MobiusState* mobius_new_state(MobiusConfig* config) {
    MobiusState* state = new (std::nothrow) MobiusState(config);
    if (!state) return NULL;
    if (!state->stringPool()) {
        delete state;
        return NULL;
    }
    return state;
}

void mobius_free_state(MobiusState* state) {
    delete state;
}

void mobius_get_metrics(MobiusState* state, MobiusMetrics* out) {
    if (!state || !out) return;
    MobiusMetrics& m = state->metrics();
    size_t sc = state->stringPool()->stringCount();
    if (sc > m.peak_interned_strings)
        m.peak_interned_strings = sc;
    *out = m;
}

void mobius_reset_metrics(MobiusState* state) {
    if (!state) return;
    state->resetMetrics();
}

int mobius_init_stdlib(MobiusState* state) {
    if (!state) return MOBIUS_ERROR_ARGUMENT;
    return state->initStdlib();
}

int mobius_exec_string(MobiusState* state, const char* code) {
    if (!state) return MOBIUS_ERROR_ARGUMENT;
    return state->execString(code);
}

int mobius_exec_file(MobiusState* state, const char* filename) {
    if (!state) return MOBIUS_ERROR_ARGUMENT;
    return state->execFile(filename);
}

MobiusErrorHandler mobius_set_error_handler(MobiusState* state,
                                           MobiusErrorHandler handler,
                                           void* userdata) {
    if (!state) return NULL;
    return state->setErrorHandler(handler, userdata);
}

void mobius_start_repl(MobiusState* state) {
    if (!state) return;
    state->startRepl();
}

InternalError* mobius_get_last_error(MobiusState* state) {
    if (!state) return NULL;
    return state->getLastError();
}

void mobius_clear_error(MobiusState* state) {
    if (!state) return;
    state->clearError();
}

int mobius_set_error(MobiusState* state, int code, const char* message,
                     const char* suggestion, int line, int column,
                     const char* function_name) {
    if (!state) return code;
    return state->setError(code, message, suggestion, line, column, function_name);
}

int mobius_error(MobiusState* state, const char* message) {
    if (!state) return -1;
    return state->error(message);
}

} // extern "C"

// ============================================================================
// UTILITY FREE FUNCTIONS
// ============================================================================

void free_stack_trace(StackTrace* trace) {
    if (!trace) return;
    if (trace->frames) {
        for (size_t i = 0; i < trace->frame_count; i++) {
            free((void*)trace->frames[i].function_name);
            free((void*)trace->frames[i].filename);
        }
    }
    free(trace->frames);
    free(trace);
}
