#define _POSIX_C_SOURCE 199309L 

#include "state/mobius_state.h"
#include "state/environment.h"
#include "eval/evaluator.h"
#include "frontend/ast.h"
#include "frontend/scanner.h"
#include "frontend/parser.h"
#include "frontend/token.h"
#include "library/library.h"
#include "plugin/module_registry.h"
#include "util/utility.h"
#include "util/file_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// ============================================================================
// Helper functions
// ============================================================================

static void clear_error(MobiusState* state) {
    if (state->last_error) {
        mobius_free_error(state->last_error);
        state->last_error = NULL;
    }
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
    return config;
}

// ============================================================================
// EXECUTION CONTEXT IMPLEMENTATION
// ============================================================================

ExecutionContext* mobius_create_context(MobiusState* state) {
    if (!state) return NULL;
    
    ExecutionContext* ctx = malloc(sizeof(ExecutionContext));
    if (!ctx) return NULL;
    
    // Initialize execution context
    ctx->state = state;
    
    // Initialize value stack
    ctx->stack_capacity = state->config.initial_stack_size;
    ctx->stack = malloc(ctx->stack_capacity * sizeof(Value));
    if (!ctx->stack) {
        free(ctx);
        return NULL;
    }
    ctx->stack_top = 0;
    ctx->current_env = state->global_env; // Start with global environment
    
    // Initialize call frame stack
    ctx->frame_capacity = 64; // Initial capacity for call frames
    ctx->call_frames = malloc(ctx->frame_capacity * sizeof(CallFrame));
    if (!ctx->call_frames) {
        free(ctx->stack);
        free(ctx);
        return NULL;
    }
    ctx->frame_count = 0;
    ctx->max_depth = state->config.max_call_depth;
    
    return ctx;
}

void mobius_free_context(ExecutionContext* ctx) {
    if (!ctx) return;
    
    // Free value stack
    free(ctx->stack);
    
    // Free call frame stack
    free(ctx->call_frames);
    
    free(ctx);
}

ExecutionContext* mobius_get_main_context(MobiusState* state) {
    return state ? state->main_context : NULL;
}

// ============================================================================
// STACK OPERATIONS
// ============================================================================

void ctx_ensure_stack_capacity(ExecutionContext* ctx, size_t needed) {
    if (!ctx) return;
    
    size_t required = ctx->stack_top + needed;
    if (required <= ctx->stack_capacity) return;
    
    // Check against maximum
    if (required > ctx->state->config.max_stack_size) {
        fprintf(stderr, "Stack overflow: required %zu exceeds max %zu\n", 
                required, ctx->state->config.max_stack_size);
        return;
    }
    
    // Grow stack (double or to required size, whichever is larger)
    size_t new_capacity = ctx->stack_capacity * 2;
    if (new_capacity < required) {
        new_capacity = required;
    }
    if (new_capacity > ctx->state->config.max_stack_size) {
        new_capacity = ctx->state->config.max_stack_size;
    }
    
    Value* new_stack = realloc(ctx->stack, new_capacity * sizeof(Value));
    if (!new_stack) {
        fprintf(stderr, "Failed to grow stack from %zu to %zu\n", 
                ctx->stack_capacity, new_capacity);
        return;
    }
    
    ctx->stack = new_stack;
    ctx->stack_capacity = new_capacity;
}

void ctx_push(ExecutionContext* ctx, Value value) {
    if (!ctx) return;
    
    ctx_ensure_stack_capacity(ctx, 1);
    ctx->stack[ctx->stack_top++] = value;
}

Value ctx_pop(ExecutionContext* ctx) {
    if (!ctx || ctx->stack_top == 0) {
        fprintf(stderr, "Stack underflow\n");
        return make_nil_value();
    }
    
    return ctx->stack[--ctx->stack_top];
}

Value ctx_peek(ExecutionContext* ctx, size_t offset) {
    if (!ctx || ctx->stack_top == 0) {
        fprintf(stderr, "Stack empty\n");
        return make_nil_value();
    }
    
    if (offset >= ctx->stack_top) {
        fprintf(stderr, "Stack peek offset %zu out of bounds (size: %zu)\n", 
                offset, ctx->stack_top);
        return make_nil_value();
    }
    
    return ctx->stack[ctx->stack_top - 1 - offset];
}

size_t ctx_stack_size(ExecutionContext* ctx) {
    return ctx ? ctx->stack_top : 0;
}

void ctx_stack_clear(ExecutionContext* ctx) {
    if (!ctx) return;
    ctx->stack_top = 0;
}

// ============================================================================
// CALL STACK / STACK TRACE OPERATIONS
// ============================================================================

// Get current time in nanoseconds for profiling
static uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void ensure_frame_capacity(ExecutionContext* ctx, size_t needed) {
    if (!ctx) return;
    
    size_t required = ctx->frame_count + needed;
    if (required <= ctx->frame_capacity) return;
    
    size_t new_capacity = ctx->frame_capacity * 2;
    if (new_capacity < required) {
        new_capacity = required;
    }
    
    CallFrame* new_frames = realloc(ctx->call_frames, new_capacity * sizeof(CallFrame));
    if (!new_frames) {
        fprintf(stderr, "Failed to grow call frame stack\n");
        return;
    }
    
    ctx->call_frames = new_frames;
    ctx->frame_capacity = new_capacity;
}

void stack_trace_push(ExecutionContext* ctx, const char* function_name,
                       const char* filename, int line, int column,
                       FunctionType type, void* function_ptr, Environment* env) {
    if (!ctx) return;
    
    // Check for stack overflow
    if (ctx->frame_count >= ctx->max_depth) {
        fprintf(stderr, "Stack overflow: call depth exceeds maximum %zu\n", ctx->max_depth);
        return;
    }
    
    ensure_frame_capacity(ctx, 1);
    
    CallFrame* frame = &ctx->call_frames[ctx->frame_count++];
    frame->function_name = function_name;
    frame->filename = filename;
    frame->line = line;
    frame->column = column;
    frame->type = type;
    frame->function_ptr = function_ptr;
    frame->env = env;
    frame->stack_base = ctx->stack_top;
    frame->stack_top = ctx->stack_top;
    frame->start_time = get_time_ns();
    
    // Update execution context's current environment to this frame's environment
    if (env) {
        ctx->current_env = env;
    }
}

void stack_trace_pop(ExecutionContext* ctx) {
    if (!ctx || ctx->frame_count == 0) {
        return;
    }
    
    ctx->frame_count--;
    
    // Restore previous environment
    if (ctx->frame_count > 0) {
        // Set current_env to the previous frame's environment
        ctx->current_env = ctx->call_frames[ctx->frame_count - 1].env;
    } else {
        // No frames left, revert to the state's global environment
        if (ctx->state && ctx->state->global_env) {
            ctx->current_env = ctx->state->global_env;
        }
    }
}

void stack_trace_clear(ExecutionContext* ctx) {
    if (!ctx) return;
    ctx->frame_count = 0;
}

size_t stack_trace_depth(ExecutionContext* ctx) {
    return ctx ? ctx->frame_count : 0;
}

bool is_stack_overflow(ExecutionContext* ctx) {
    return ctx ? (ctx->frame_count >= ctx->max_depth) : false;
}

void print_stack_trace(ExecutionContext* ctx) {
    if (!ctx || ctx->frame_count == 0) {
        printf("Stack trace: (empty)\n");
        return;
    }
    
    printf("Stack trace:\n");
    for (size_t i = 0; i < ctx->frame_count; i++) {
        CallFrame* frame = &ctx->call_frames[i];
        
        const char* type_str;
        switch (frame->type) {
            case FUNCTION_TYPE_NATIVE:  type_str = "native"; break;
            case FUNCTION_TYPE_SCRIPT:  type_str = "script"; break;
            case FUNCTION_TYPE_PLUGIN:  type_str = "plugin"; break;
            case FUNCTION_TYPE_CLOSURE: type_str = "closure"; break;
            default: type_str = "unknown"; break;
        }
        
        printf("  [%zu] %s (%s)", i, 
               frame->function_name ? frame->function_name : "<anonymous>",
               type_str);
        
        if (frame->filename) {
            printf(" at %s:%d:%d", frame->filename, frame->line, frame->column);
        } else if (frame->line > 0) {
            printf(" at line %d:%d", frame->line, frame->column);
        }
        
        printf("\n");
    }
}

char* format_stack_trace(ExecutionContext* ctx) {
    if (!ctx || ctx->frame_count == 0) {
        return mobius_strdup("Stack trace: (empty)\n");
    }
    
    // Estimate buffer size (generous estimate)
    size_t buffer_size = 256 * ctx->frame_count + 100;
    char* buffer = malloc(buffer_size);
    if (!buffer) return NULL;
    
    size_t offset = 0;
    offset += snprintf(buffer + offset, buffer_size - offset, "Stack trace:\n");
    
    for (size_t i = 0; i < ctx->frame_count && offset < buffer_size - 1; i++) {
        CallFrame* frame = &ctx->call_frames[i];
        
        const char* type_str;
        switch (frame->type) {
            case FUNCTION_TYPE_NATIVE:  type_str = "native"; break;
            case FUNCTION_TYPE_SCRIPT:  type_str = "script"; break;
            case FUNCTION_TYPE_PLUGIN:  type_str = "plugin"; break;
            case FUNCTION_TYPE_CLOSURE: type_str = "closure"; break;
            default: type_str = "unknown"; break;
        }
        
        offset += snprintf(buffer + offset, buffer_size - offset,
                          "  [%zu] %s (%s)", i,
                          frame->function_name ? frame->function_name : "<anonymous>",
                          type_str);
        
        if (frame->filename) {
            offset += snprintf(buffer + offset, buffer_size - offset,
                             " at %s:%d:%d", frame->filename, frame->line, frame->column);
        } else if (frame->line > 0) {
            offset += snprintf(buffer + offset, buffer_size - offset,
                             " at line %d:%d", frame->line, frame->column);
        }
        
        offset += snprintf(buffer + offset, buffer_size - offset, "\n");
    }
    
    return buffer;
}

// ============================================================================
// STATE MANAGEMENT
// ============================================================================

MobiusState* mobius_new_state(MobiusConfig* config) {
    MobiusState* state = malloc(sizeof(MobiusState));
    if (!state) return NULL;
    
    // Use provided config or defaults
    if (config) {
        state->config = *config;
    } else {
        state->config = mobius_default_config();
    }
    
    // Initialize fields
    state->global_env = NULL;
    state->registry = NULL;
    state->main_context = NULL;
    state->last_error = NULL;
    state->initialized = false;
    state->source_code = NULL;
    
    // Create main execution context first
    state->main_context = mobius_create_context(state);
    if (!state->main_context) {
        mobius_free_state(state);
        return NULL;
    }
    
    // Create global environment and link it to the context
    state->global_env = create_environment(NULL);
    if (!state->global_env) {
        mobius_free_state(state);
        return NULL;
    }
    
    // Link environment to context
    state->global_env->current_context = state->main_context;
    state->main_context->current_env = state->global_env;
    
    // Create module registry
    state->registry = create_module_registry();
    if (!state->registry) {
        mobius_free_state(state);
        return NULL;
    }

    // Add built-in constants to global environment
    define_variable(state->global_env, "nil", make_nil_value());
    define_variable(state->global_env, "true", make_bool_value(true));
    define_variable(state->global_env, "false", make_bool_value(false));
    
    return state;
}

void mobius_free_state(MobiusState* state) {
    if (!state) return;
    
    // Free main context
    if (state->main_context) {
        mobius_free_context(state->main_context);
    }
    
    // Free global environment
    if (state->global_env) {
        free_environment(state->global_env);
    }
    
    // Free module registry
    if (state->registry) {
        free_module_registry(state->registry);
    }
    
    // Free error
    if (state->last_error) {
        mobius_free_error(state->last_error);
    }
    
    free(state);
}

int mobius_init_stdlib(MobiusState* state) {
    if (!state) return MOBIUS_ERROR_ARGUMENT;
    
    register_stdlib_functions(state);
    
    state->initialized = true;
    return MOBIUS_OK;
}

// ============================================================================
// EXECUTION API (Stubs for now)
// ============================================================================

int mobius_exec_string(MobiusState* state, const char* code) {
    if (!state || !code) return MOBIUS_ERROR_ARGUMENT;
    
    clear_error(state);
    
    // Scan tokens
    TokenArray tokens = scan_source(code);
    if (tokens.count == 0) {
        free_token_array(&tokens);
        mobius_set_error(state, MOBIUS_ERROR_SYNTAX, "No tokens found", NULL, 0, 0, NULL);
        return MOBIUS_ERROR_SYNTAX;
    }
    
    // Parse AST
    ParseResult parse_result = parse(tokens);
    free_token_array(&tokens);
    
    if (parse_result.had_error) {
        mobius_set_error(state, MOBIUS_ERROR_SYNTAX, "Parse error", 
                          "Check syntax and structure", 0, 0, NULL);
        free_parse_result(&parse_result);
        return MOBIUS_ERROR_SYNTAX;
    }
    
    // Execute
    EvalResult eval_result = evaluate_program(parse_result.statements, 
                                             parse_result.count, 
                                             state->global_env);
    free_parse_result(&parse_result);
    
    if (is_error(eval_result)) {
        mobius_set_error(state, MOBIUS_ERROR_RUNTIME, 
                          eval_result.error.message,
                          eval_result.error.suggestion,
                          eval_result.error.line,
                          eval_result.error.column,
                          eval_result.error.function_name);
        return MOBIUS_ERROR_RUNTIME;
    }
    
    return MOBIUS_OK;
}

int mobius_exec_file(MobiusState* state, const char* filename) {
    if (!state || !filename) return MOBIUS_ERROR_ARGUMENT;
    
    // Read file content
    FileResult file_result = read_file(filename);
    if (!file_result.success) {
        mobius_set_error(state, MOBIUS_ERROR_RUNTIME, 
                          file_result.error ? file_result.error : "Failed to read file", 
                          "Check that file exists and is readable", 
                          0, 0, NULL);
        return MOBIUS_ERROR_RUNTIME;
    }
    
    char* content = file_result.content;
    
    int result = mobius_exec_string(state, content);
    free_file_result(&file_result);
    return result;
}

// ============================================================================
// ERROR HANDLING
// ============================================================================

MobiusError* mobius_get_last_error(MobiusState* state) {
    if (!state || !state->last_error) return NULL;
    
    // Return a copy of the error
    MobiusError* copy = malloc(sizeof(MobiusError));
    if (!copy) return NULL;
    
    copy->code = state->last_error->code;
    copy->message = state->last_error->message ? mobius_strdup(state->last_error->message) : NULL;
    copy->suggestion = state->last_error->suggestion ? mobius_strdup(state->last_error->suggestion) : NULL;
    copy->line = state->last_error->line;
    copy->column = state->last_error->column;
    copy->function_name = state->last_error->function_name ? mobius_strdup(state->last_error->function_name) : NULL;
    
    return copy;
}

void mobius_clear_error(MobiusState* state) {
    if (!state) return;
    clear_error(state);
}

void mobius_free_error(MobiusError* error) {
    if (!error) return;
    
    free(error->message);
    free(error->suggestion);
    free(error->function_name);
    free(error);
}

int mobius_set_error(MobiusState* state, int code, const char* message, const char* suggestion, int line, int column, const char* function_name) {
    if (!state) return code;
    
    clear_error(state);
    
    state->last_error = malloc(sizeof(MobiusError));
    if (!state->last_error) return code;
    
    state->last_error->code = code;
    state->last_error->message = message ? mobius_strdup(message) : NULL;
    state->last_error->suggestion = suggestion ? mobius_strdup(suggestion) : NULL;
    state->last_error->line = line;
    state->last_error->column = column;
    state->last_error->function_name = function_name ? mobius_strdup(function_name) : NULL;
    
    return code;
}

// ============================================================================
// STACK TRACE MANAGEMENT
// ============================================================================

// Capture a snapshot of the current call stack
StackTrace* capture_stack_trace(ExecutionContext* ctx) {
    if (!ctx || ctx->frame_count == 0) {
        return NULL;
    }
    
    StackTrace* trace = malloc(sizeof(StackTrace));
    if (!trace) return NULL;
    
    trace->frame_count = ctx->frame_count;
    trace->frames = malloc(sizeof(TraceFrame) * ctx->frame_count);
    if (!trace->frames) {
        free(trace);
        return NULL;
    }
    
    // Copy frame information (shallow copy of pointers is OK since they're static strings)
    for (size_t i = 0; i < ctx->frame_count; i++) {
        CallFrame* src = &ctx->call_frames[i];
        TraceFrame* dst = &trace->frames[i];
        
        dst->function_name = src->function_name;
        dst->filename = src->filename;
        dst->line = src->line;
        dst->column = src->column;
        
        // Convert FunctionType to TraceFunctionType
        switch (src->type) {
            case FUNCTION_TYPE_NATIVE:  dst->type = TRACE_FUNCTION_NATIVE; break;
            case FUNCTION_TYPE_SCRIPT:  dst->type = TRACE_FUNCTION_SCRIPT; break;
            case FUNCTION_TYPE_PLUGIN:  dst->type = TRACE_FUNCTION_PLUGIN; break;
            case FUNCTION_TYPE_CLOSURE: dst->type = TRACE_FUNCTION_CLOSURE; break;
            default: dst->type = TRACE_FUNCTION_SCRIPT; break;
        }
    }
    
    return trace;
}

// Free a stack trace
void free_stack_trace(StackTrace* trace) {
    if (!trace) return;
    free(trace->frames);
    free(trace);
}

// ============================================================================
// SOURCE CODE CONTEXT
// ============================================================================

void set_source_context(MobiusState* state, const char* source) {
    state->source_code = source;
}

const char* get_source_context(MobiusState* state) {
    return state->source_code;
}

const char* extract_source_line(const char* source, int line_number) {
    if (!source || line_number <= 0) {
        return NULL;
    }
    
    static char line_buffer[512];  // Static buffer for the extracted line
    const char* current = source;
    int current_line = 1;
    
    // Find the start of the target line
    while (current_line < line_number && *current) {
        if (*current == '\n') {
            current_line++;
        }
        current++;
    }
    
    if (current_line != line_number || !*current) {
        return NULL;  // Line not found
    }
    
    // Copy the line content, excluding the newline
    const char* line_start = current;
    const char* line_end = current;
    while (*line_end && *line_end != '\n' && *line_end != '\r') {
        line_end++;
    }
    
    size_t line_length = line_end - line_start;
    if (line_length >= sizeof(line_buffer)) {
        line_length = sizeof(line_buffer) - 1;  // Truncate if too long
    }
    
    strncpy(line_buffer, line_start, line_length);
    line_buffer[line_length] = '\0';
    
    return line_buffer;
}
