#define _POSIX_C_SOURCE 199309L 

#include <mobius/mobius_plugin.h>
#include "state/mobius_state.h"
#include "state/environment.h"
#include "eval/evaluator.h"
#include "frontend/ast.h"
#include "frontend/scanner.h"
#include "frontend/parser.h"
#include "frontend/token.h"
#include "library/library.h"
#include "internal/string_intern.h"
#include "data/metamethods.h"
#include "plugin/module_registry.h"
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
    config.use_vm = false;
    config.override_behavior = MOBIUS_OVERRIDE_ERROR;
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

ExecutionContext::ExecutionContext(MobiusState* owner, size_t initial_stack, size_t max_depth)
    : state(owner), current_env(nullptr), max_depth_(max_depth) {
    stack.reserve(initial_stack);
    call_frames_.reserve(64);
}

ExecutionContext::~ExecutionContext() {
}

void ExecutionContext::push(const Value& value) {
    if (stack.size() >= state->config().max_stack_size) {
        fprintf(stderr, "Stack overflow: size %zu exceeds max %zu\n",
                stack.size(), state->config().max_stack_size);
        return;
    }
    stack.push_back(value);
}

void ExecutionContext::push(Value&& value) {
    if (stack.size() >= state->config().max_stack_size) {
        fprintf(stderr, "Stack overflow: size %zu exceeds max %zu\n",
                stack.size(), state->config().max_stack_size);
        return;
    }
    stack.push_back(std::move(value));
}

Value ExecutionContext::pop() {
    if (stack.empty()) {
        fprintf(stderr, "Stack underflow\n");
        return make_nil_value();
    }
    Value result = std::move(stack.back());
    stack.pop_back();
    return result;
}

const Value& ExecutionContext::peek(size_t offset) const {
    static Value nil_sentinel;
    if (stack.empty()) {
        fprintf(stderr, "Stack empty\n");
        nil_sentinel = make_nil_value();
        return nil_sentinel;
    }

    if (offset >= stack.size()) {
        fprintf(stderr, "Stack peek offset %zu out of bounds (size: %zu)\n",
                offset, stack.size());
        nil_sentinel = make_nil_value();
        return nil_sentinel;
    }

    return stack[stack.size() - 1 - offset];
}

size_t ExecutionContext::stackSize() const {
    return stack.size();
}

void ExecutionContext::stackClear() {
    stack.clear();
}

// ============================================================================
// CALL STACK / STACK TRACE OPERATIONS
// ============================================================================

void ExecutionContext::pushFrame(const char* function_name, const char* filename,
                                 int line, int column, FunctionType type,
                                 void* function_ptr, Environment* env) {
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
    frame.env = env;
    frame.stack_base = stack.size();
    frame.stack_top = stack.size();
    frame.start_time = get_time_ns();

    call_frames_.push_back(frame);

    if (env) {
        current_env = env;
    }
}

void ExecutionContext::popFrame() {
    if (call_frames_.empty()) {
        return;
    }

    call_frames_.pop_back();

    if (!call_frames_.empty()) {
        current_env = call_frames_.back().env;
    } else {
        if (state && state->globalEnv()) {
            current_env = state->globalEnv();
        }
    }
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

static void free_internal_error(InternalError* error) {
    if (!error) return;
    free(error->message);
    free(error->suggestion);
    free(error->function_name);
    free(error);
}

// ============================================================================
// MOBIUS STATE IMPLEMENTATION
// ============================================================================

static void default_error_handler(MobiusState* state, const MobiusError* error, void* userdata);

MobiusState::MobiusState(MobiusConfig* config)
    : global_env_(nullptr), registry_(nullptr), string_pool_(nullptr),
      metamethods_(nullptr),
      main_context_(nullptr), last_error_(nullptr),
      error_handler_(default_error_handler), error_handler_userdata_(nullptr),
      initialized_(false), source_code_(nullptr) {
    
    config_ = config ? *config : mobius_default_config();

    string_pool_ = new (std::nothrow) StringInternPool(256);
    if (!string_pool_) return;

    metamethods_ = new (std::nothrow) Metamethods(string_pool_);
    if (!metamethods_) return;

    main_context_ = new (std::nothrow) ExecutionContext(
        this, config_.initial_stack_size, config_.max_call_depth);
    if (!main_context_) return;

    global_env_ = new (std::nothrow) Environment();
    if (!global_env_) return;

    global_env_->current_context = main_context_;
    main_context_->current_env = global_env_;

    registry_ = getGlobalRegistry();
    if (!registry_) return;

    registry_->scanPlugins(config_.enable_hot_reload);

    global_env_->define(string_pool_->intern("nil"), make_nil_value());
    global_env_->define(string_pool_->intern("true"), make_bool_value(true));
    global_env_->define(string_pool_->intern("false"), make_bool_value(false));
}

MobiusState::~MobiusState() {
    for (Prototype* p : owned_protos_) {
        delete p;
    }
    owned_protos_.clear();

    delete main_context_;
    if (global_env_) global_env_->release();
    delete metamethods_;

    // Don't free module registry — it's a global singleton freed via atexit()
    registry_ = nullptr;

    delete string_pool_;

    if (last_error_) {
        free_internal_error(last_error_);
    }
}

void MobiusState::clearErrorInternal() {
    if (last_error_) {
        free_internal_error(last_error_);
        last_error_ = nullptr;
    }
}

int MobiusState::initStdlib() {
    register_stdlib_functions(this);
    initialized_ = true;
    return MOBIUS_OK;
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
        setError(MOBIUS_ERROR_SYNTAX, "Parse error", 
                 "Check syntax and structure", 0, 0, NULL);
        free_parse_result(&parse_result);
        return MOBIUS_ERROR_SYNTAX;
    }

    if (config_.use_vm) {
        Compiler compiler(string_pool_);
        Prototype* proto = compiler.compile(parse_result.statements,
                                            parse_result.count,
                                            source_code_ ? source_code_ : "<string>");
        free_parse_result(&parse_result);

        if (!proto) {
            setError(MOBIUS_ERROR_RUNTIME, "Bytecode compilation failed",
                     nullptr, 0, 0, nullptr);
            return MOBIUS_ERROR_RUNTIME;
        }

        if (config_.debug_mode) {
            disassemble_prototype(proto);
        }

        // Prototypes are owned by the state so they outlive any
        // MobiusFunction closures that reference child prototypes.
        owned_protos_.push_back(proto);

        MobiusVM vm(this);
        int rc = vm.execute(proto);

        if (rc != 0) {
            return MOBIUS_ERROR_RUNTIME;
        }
        return MOBIUS_OK;
    }

    EvalResult eval_result = evaluate_program(parse_result.statements, 
                                             parse_result.count, 
                                             global_env_);
    free_parse_result(&parse_result);

    if (is_error(eval_result)) {
        setError(MOBIUS_ERROR_RUNTIME, 
                 eval_result.error.message,
                 eval_result.error.suggestion,
                 eval_result.error.line,
                 eval_result.error.column,
                 eval_result.error.function_name);
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

    char* content = file_result.content;
    int result = execString(content);
    free_file_result(&file_result);
    return result;
}

// ============================================================================
// ERROR HANDLING
// ============================================================================

InternalError* MobiusState::getLastError() const {
    if (!last_error_) return NULL;

    InternalError* copy = (InternalError*)malloc(sizeof(InternalError));
    if (!copy) return NULL;

    copy->code = last_error_->code;
    copy->message = last_error_->message ? mobius_strdup(last_error_->message) : NULL;
    copy->suggestion = last_error_->suggestion ? mobius_strdup(last_error_->suggestion) : NULL;
    copy->line = last_error_->line;
    copy->column = last_error_->column;
    copy->function_name = last_error_->function_name ? mobius_strdup(last_error_->function_name) : NULL;

    return copy;
}

void MobiusState::clearError() {
    clearErrorInternal();
}

int MobiusState::setError(int code, const char* message, const char* suggestion,
                          int line, int column, const char* function_name) {
    clearErrorInternal();

    last_error_ = (InternalError*)malloc(sizeof(InternalError));
    if (!last_error_) return code;

    last_error_->code = code;
    last_error_->message = message ? mobius_strdup(message) : NULL;
    last_error_->suggestion = suggestion ? mobius_strdup(suggestion) : NULL;
    last_error_->line = line;
    last_error_->column = column;
    last_error_->function_name = function_name ? mobius_strdup(function_name) : NULL;

    if (error_handler_) {
        MobiusError pub_err;
        pub_err.code = code;
        pub_err.message = message;
        pub_err.suggestion = suggestion;
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
    if (error->line > 0) {
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
    source_code_ = source;
}

const char* MobiusState::getSourceContext() const {
    return source_code_;
}

// ============================================================================
// MODULE HELPERS
// ============================================================================

size_t MobiusState::getModuleCount() const {
    if (!registry_) return 0;
    return registry_->moduleCount();
}

void MobiusState::printModules() const {
    if (!registry_) {
        printf("No modules loaded.\n");
        return;
    }
    registry_->printLoadedModules();
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
    if (!state->globalEnv() || !state->mainContext() || !state->stringPool()) {
        delete state;
        return NULL;
    }
    return state;
}

void mobius_free_state(MobiusState* state) {
    delete state;
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

size_t mobius_get_module_count(MobiusState* state) {
    if (!state) return 0;
    return state->getModuleCount();
}

void mobius_print_modules(MobiusState* state) {
    if (!state) return;
    state->printModules();
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
