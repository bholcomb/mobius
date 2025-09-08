#define _POSIX_C_SOURCE 199309L
#define _GNU_SOURCE

#include "execution.h"
#include "parser.h"
#include "scanner.h"
#include "evaluator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

// Helper function to get current time in nanoseconds
static double get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

// Helper function to create an error result
static ExecutionResult make_error_result(const char* message, int line, int column) {
    ExecutionResult result = {0};
    result.success = false;
    result.result_value = make_nil_value();
    result.error_message = strdup(message ? message : "Unknown error");
    result.line = line;
    result.column = column;
    return result;
}

// Helper function to create a success result
static ExecutionResult make_success_result(Value value, double execution_time, size_t instructions) {
    ExecutionResult result = {0};
    result.success = true;
    result.result_value = copy_value(value);
    result.error_message = NULL;
    result.execution_time_ms = execution_time;
    result.instructions_executed = instructions;
    return result;
}

ExecutionContext* execution_context_create(ExecutionBackend backend) {
    ExecutionContext* ctx = calloc(1, sizeof(ExecutionContext));
    if (!ctx) return NULL;
    
    ctx->backend = backend;
    
    switch (backend) {
        case EXEC_BACKEND_AST:
            ctx->state.ast.env = create_environment(NULL);
            if (!ctx->state.ast.env) {
                free(ctx);
                return NULL;
            }
            break;
            
        case EXEC_BACKEND_BYTECODE:
            ctx->state.bytecode.vm = vm_create();
            if (!ctx->state.bytecode.vm) {
                free(ctx);
                return NULL;
            }
            ctx->state.bytecode.chunk = NULL;
            break;
            
        default:
            free(ctx);
            return NULL;
    }
    
    return ctx;
}

void execution_context_free(ExecutionContext* ctx) {
    if (!ctx) return;
    
    switch (ctx->backend) {
        case EXEC_BACKEND_AST:
            if (ctx->state.ast.env) {
                free_environment(ctx->state.ast.env);
            }
            break;
            
        case EXEC_BACKEND_BYTECODE:
            if (ctx->state.bytecode.vm) {
                vm_free(ctx->state.bytecode.vm);
            }
            if (ctx->state.bytecode.chunk) {
                bytecode_chunk_free(ctx->state.bytecode.chunk);
            }
            break;
    }
    
    free(ctx);
}

ExecutionResult execute_source(ExecutionContext* ctx, const char* source) {
    if (!ctx || !source) {
        return make_error_result("Invalid context or source", 0, 0);
    }
    
    double start_time = get_time_ms();
    
    // Parse the source code
    TokenArray tokens = scan_source(source);
    if (tokens.count == 0) {
        free_token_array(&tokens);
        return make_error_result("No tokens found", 0, 0);
    }
    
    ParseResult parse_result = parse(tokens);
    free_token_array(&tokens);
    
    if (parse_result.had_error) {
        free_parse_result(&parse_result);
        return make_error_result("Parse error", 0, 0);
    }
    
    // Execute using the appropriate backend
    ExecutionResult result = execute_program(ctx, parse_result.statements, parse_result.count);
    
    // Add parsing time to execution time
    result.execution_time_ms += get_time_ms() - start_time;
    
    free_parse_result(&parse_result);
    return result;
}

ExecutionResult execute_statement(ExecutionContext* ctx, Stmt* stmt) {
    if (!ctx || !stmt) {
        return make_error_result("Invalid context or statement", 0, 0);
    }
    
    return execute_program(ctx, &stmt, 1);
}

ExecutionResult execute_program(ExecutionContext* ctx, Stmt** statements, size_t count) {
    if (!ctx || !statements) {
        return make_error_result("Invalid context or statements", 0, 0);
    }
    
    double start_time = get_time_ms();
    
    switch (ctx->backend) {
        case EXEC_BACKEND_AST: {
            // Execute using AST interpreter
            EvalResult eval_result = evaluate_program(statements, count, ctx->state.ast.env);
            
            double execution_time = get_time_ms() - start_time;
            
            if (is_error(eval_result)) {
                return make_error_result(
                    eval_result.error.message,
                    eval_result.error.line,
                    eval_result.error.column
                );
            } else {
                return make_success_result(eval_result.value, execution_time, count);
            }
        }
        
        case EXEC_BACKEND_BYTECODE: {
            // Compile to bytecode
            BytecodeChunk* chunk = bytecode_chunk_create();
            if (!chunk) {
                return make_error_result("Failed to create bytecode chunk", 0, 0);
            }
            
            bool compile_success = compile_ast_to_bytecode(statements, count, chunk);
            if (!compile_success) {
                bytecode_chunk_free(chunk);
                return make_error_result("Bytecode compilation failed", 0, 0);
            }
            
            // Execute bytecode
            int vm_result = vm_execute(ctx->state.bytecode.vm, chunk);
            double execution_time = get_time_ms() - start_time;
            
            if (vm_result != 0) {
                bytecode_chunk_free(chunk);
                return make_error_result("Bytecode execution failed", 0, 0);
            }
            
            // Get result value from VM stack
            Value result_value = make_nil_value();
            if (ctx->state.bytecode.vm->stack_top > ctx->state.bytecode.vm->stack) {
                result_value = vm_peek(ctx->state.bytecode.vm, 0);
            }
            
            ExecutionResult result = make_success_result(result_value, execution_time, chunk->count);
            
            // Store chunk for potential reuse (will be freed in context cleanup)
            if (ctx->state.bytecode.chunk) {
                bytecode_chunk_free(ctx->state.bytecode.chunk);
            }
            ctx->state.bytecode.chunk = chunk;
            
            return result;
        }
        
        default:
            return make_error_result("Unknown execution backend", 0, 0);
    }
}

void free_execution_result(ExecutionResult* result) {
    if (!result) return;
    
    free_value(result->result_value);
    if (result->error_message) {
        free(result->error_message);
        result->error_message = NULL;
    }
}

const char* execution_backend_name(ExecutionBackend backend) {
    switch (backend) {
        case EXEC_BACKEND_AST: return "AST Tree-Walker";
        case EXEC_BACKEND_BYTECODE: return "Bytecode VM";
        default: return "Unknown";
    }
}

// Helper function to compare values for equivalence
static bool values_equivalent(const Value* a, const Value* b) {
    if (a->type != b->type) return false;
    
    switch (a->type) {
        case VAL_NIL:
            return true;
            
        case VAL_BOOL:
            return a->as.boolean == b->as.boolean;
            
        case VAL_INTEGER: {
            // Compare integers using the correct field based on their types
            int64_t val_a, val_b;
            switch (a->as.integer.num_type) {
                case NUM_INT8:  val_a = a->as.integer.value.i8; break;
                case NUM_INT16: val_a = a->as.integer.value.i16; break;
                case NUM_INT32: val_a = a->as.integer.value.i32; break;
                case NUM_INT64: val_a = a->as.integer.value.i64; break;
                default: val_a = a->as.integer.value.i32; break;
            }
            switch (b->as.integer.num_type) {
                case NUM_INT8:  val_b = b->as.integer.value.i8; break;
                case NUM_INT16: val_b = b->as.integer.value.i16; break;
                case NUM_INT32: val_b = b->as.integer.value.i32; break;
                case NUM_INT64: val_b = b->as.integer.value.i64; break;
                default: val_b = b->as.integer.value.i32; break;
            }
            return val_a == val_b;
        }
            
        case VAL_FLOAT:
            // Use small epsilon for floating point comparison
            return fabs(a->as.float_val - b->as.float_val) < 1e-9;
            
        case VAL_STRING:
            if (!a->as.string || !b->as.string) return a->as.string == b->as.string;
            return strcmp(string_data(a->as.string), string_data(b->as.string)) == 0;
            
        case VAL_ARRAY:
            if (!a->as.array || !b->as.array) return a->as.array == b->as.array;
            if (a->as.array->length != b->as.array->length) return false;
            for (size_t i = 0; i < a->as.array->length; i++) {
                Value elem_a = array_get(a->as.array, i);
                Value elem_b = array_get(b->as.array, i);
                if (!values_equivalent(&elem_a, &elem_b)) return false;
            }
            return true;
            
        case VAL_TABLE:
            // For tables, we'll do a simplified comparison - just check if both are tables
            // A full comparison would require iterating through all key-value pairs
            return a->as.table != NULL && b->as.table != NULL;
            
        default:
            return false;
    }
}

bool execution_results_equivalent(const ExecutionResult* a, const ExecutionResult* b) {
    if (!a || !b) return false;
    
    // Both must succeed or both must fail
    if (a->success != b->success) return false;
    
    if (a->success) {
        // Compare result values
        return values_equivalent(&a->result_value, &b->result_value);
    } else {
        // For errors, we just check that both failed (error messages may differ)
        return true;
    }
}
