#define _POSIX_C_SOURCE 199309L 

#include <mobius/mobius_plugin.h>
#include "state/mobius_state.h"
#include "frontend/ast.h"
#include "frontend/scanner.h"
#include "frontend/parser.h"
#include "frontend/token.h"
#include "library/library.h"
#include "library/array.h"
#include "library/fiber_lib.h"
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

    config.fiber_stack_size        = 128 * 1024;  // 128 KiB
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
    trace->frames = (TraceFrame*)malloc(sizeof(TraceFrame) * call_frames_.size());
    if (!trace->frames) {
        free(trace);
        return NULL;
    }

    for (size_t i = 0; i < call_frames_.size(); i++) {
        const CallFrame& src = call_frames_[i];
        TraceFrame* dst = &trace->frames[i];

        dst->function_name = src.function_name;
        dst->filename = src.filename;
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

MobiusState::MobiusState(MobiusConfig* config)
    : registry_(nullptr), string_pool_(nullptr),
      metamethods_(nullptr), job_system_(nullptr),
      error_handler_(default_error_handler), error_handler_userdata_(nullptr),
      metrics_{}, initialized_(false),
      fallback_last_error_(nullptr), fallback_source_code_(nullptr) {
    
    config_ = config ? *config : mobius_default_config();

    globals_.resize(4096);

    string_pool_ = new (std::nothrow) StringInternPool(256);
    if (!string_pool_) return;

    metamethods_ = new (std::nothrow) Metamethods(string_pool_);
    if (!metamethods_) return;

    registry_ = getGlobalRegistry();
    if (!registry_) return;

    job_system_ = new (std::nothrow) JobSystem(this);
    if (!job_system_) return;

    auto defineGlobal = [&](const char* name, Value val, bool readonly = false) {
        int slot = assignGlobalSlot(name);
        val.flags |= VAL_FLAG_DEFINED;
        if (readonly) val.flags |= VAL_FLAG_READONLY;
        globals_[slot] = val;
    };
    defineGlobal("nil", make_nil_value(), true);
    defineGlobal("true", make_bool_value(true), true);
    defineGlobal("false", make_bool_value(false), true);
    defineGlobal("inf", make_float_value(1.0 / 0.0), true);
    defineGlobal("nan", make_float_value(0.0 / 0.0), true);
}

MobiusState::~MobiusState() {
    delete job_system_;

    for (int i = 0; i < 16; i++) {
        if (type_metatables_[i]) {
            type_metatables_[i]->release();
            type_metatables_[i] = nullptr;
        }
    }

    for (Prototype* p : owned_protos_) {
        delete p;
    }
    owned_protos_.clear();

    delete metamethods_;

    // Don't free module registry — it's a global singleton freed via atexit()
    registry_ = nullptr;

    delete string_pool_;

    if (fallback_last_error_) {
        free_internal_error(fallback_last_error_);
    }
}

void MobiusState::setTypeMetatable(ValueType t, Table* mt) {
    if (type_metatables_[t]) {
        type_metatables_[t]->release();
    }
    type_metatables_[t] = mt;
    if (mt) mt->retain();
}

void MobiusState::clearErrorInternal() {
    MobiusVM* vm = MobiusVM::t_current_vm;
    InternalError*& err = vm ? vm->last_error_ : fallback_last_error_;
    if (err) {
        free_internal_error(err);
        err = nullptr;
    }
}

int MobiusState::assignGlobalSlot(const char* name) {
    std::lock_guard<std::mutex> lock(global_slot_mutex_);
    auto it = global_slot_map_.find(name);
    if (it != global_slot_map_.end()) return it->second;
    int slot = global_count_.load(std::memory_order_relaxed);
    if (slot >= (int)globals_.size()) {
        size_t new_cap = globals_.size() * 2;
        globals_.resize(new_cap);
        fprintf(stderr, "Warning: globals table resized from %zu to %zu slots\n",
                new_cap / 2, new_cap);
    }
    global_slot_map_[name] = slot;
    global_count_.store(slot + 1, std::memory_order_release);
    if ((size_t)(slot + 1) > metrics_.peak_globals)
        metrics_.peak_globals = (size_t)(slot + 1);
    return slot;
}

int MobiusState::findGlobalSlot(const char* name) const {
    std::lock_guard<std::mutex> lock(global_slot_mutex_);
    auto it = global_slot_map_.find(name);
    if (it != global_slot_map_.end()) return it->second;
    return -1;
}

const char* MobiusState::globalSlotName(int idx) const {
    std::lock_guard<std::mutex> lock(global_slot_mutex_);
    for (auto& kv : global_slot_map_) {
        if (kv.second == idx) return kv.first.c_str();
    }
    return "<unknown>";
}

void MobiusState::setGlobalReadonly(const char* name, bool readonly) {
    std::lock_guard<std::mutex> lock(global_slot_mutex_);
    auto it = global_slot_map_.find(name);
    if (it == global_slot_map_.end()) return;
    int slot = it->second;
    if (readonly)
        globals_[slot].flags |= VAL_FLAG_READONLY;
    else
        globals_[slot].flags &= ~VAL_FLAG_READONLY;
}

bool MobiusState::removeGlobal(const char* name) {
    std::lock_guard<std::mutex> lock(global_slot_mutex_);
    auto it = global_slot_map_.find(name);
    if (it == global_slot_map_.end()) return false;
    int slot = it->second;
    globals_[slot] = Value();
    globals_[slot].flags = 0;
    global_slot_map_.erase(it);
    return true;
}

void MobiusState::removeGlobalSlots(int from_slot) {
    std::lock_guard<std::mutex> lock(global_slot_mutex_);
    int count = global_count_.load(std::memory_order_relaxed);
    if (from_slot < 0 || from_slot >= count) return;

    std::vector<std::string> to_remove;
    for (auto& kv : global_slot_map_) {
        if (kv.second >= from_slot) {
            to_remove.push_back(kv.first);
        }
    }
    for (auto& name : to_remove) {
        global_slot_map_.erase(name);
    }

    for (int i = from_slot; i < count; i++) {
        globals_[i] = Value();
    }
    global_count_.store(from_slot, std::memory_order_release);
}

int MobiusState::initStdlib() {
    register_stdlib_functions(this);

    Table* fiber_mod = register_fiber_module(this);
    registry_->registerBuiltinModule("fiber", fiber_mod);

    Table* channel_mt = create_channel_type_metatable(this);
    setTypeMetatable(VAL_CHANNEL, channel_mt);
    channel_mt->release();

    Table* array_mt = create_array_type_metatable(this);
    setTypeMetatable(VAL_ARRAY, array_mt);
    array_mt->release();

    Table* table_mt = create_table_type_metatable(this);
    setTypeMetatable(VAL_TABLE, table_mt);
    table_mt->release();

    initialized_ = true;
    return MOBIUS_OK;
}

void MobiusState::addOwnedProto(Prototype* proto) {
    std::lock_guard<std::mutex> lock(owned_protos_mutex_);
    owned_protos_.push_back(proto);
}

int MobiusState::execString(const char* code) {
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
    Compiler compiler(string_pool_, this);
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

    MobiusVM vm(this);
    int rc = vm.execute(proto);

    if (rc != 0) {
        return MOBIUS_ERROR_RUNTIME;
    }
    return MOBIUS_OK;
}

int MobiusState::execFile(const char* filename) {
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
    int result = execString(file_result.content);
    setSourceContext(saved_source);
    free_file_result(&file_result);
    return result;
}

// ============================================================================
// ERROR HANDLING
// ============================================================================

InternalError* MobiusState::getLastError() const {
    MobiusVM* vm = MobiusVM::t_current_vm;
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

    MobiusVM* vm = MobiusVM::t_current_vm;
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
    MobiusVM* vm = MobiusVM::t_current_vm;
    if (vm) vm->source_code_ = source;
    else fallback_source_code_ = source;
}

const char* MobiusState::getSourceContext() const {
    MobiusVM* vm = MobiusVM::t_current_vm;
    return vm ? vm->source_code_ : fallback_source_code_;
}

// ============================================================================
// THREAD-LOCAL VM FORWARDING
// ============================================================================

MobiusVM* MobiusState::activeVM() const {
    return MobiusVM::t_current_vm;
}

NativeCallContext* MobiusState::nativeContext() const {
    MobiusVM* vm = MobiusVM::t_current_vm;
    return vm ? vm->native_ctx_ : nullptr;
}

ExecutionContext* MobiusState::mainContext() const {
    MobiusVM* vm = MobiusVM::t_current_vm;
    return vm ? vm->exec_context_ : nullptr;
}

InternalError* MobiusState::lastError() const {
    MobiusVM* vm = MobiusVM::t_current_vm;
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
    free(trace->frames);
    free(trace);
}
