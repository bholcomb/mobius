#ifndef MOBIUS_EXECUTION_H
#define MOBIUS_EXECUTION_H

#include "value.h"
#include "environment.h"
#include "bytecode.h"
#include <time.h>

// Execution backend types
typedef enum {
    EXEC_BACKEND_AST,      // Tree-walking AST interpreter
    EXEC_BACKEND_BYTECODE  // Bytecode virtual machine
} ExecutionBackend;

// Unified execution result
typedef struct {
    bool success;
    Value result_value;     // Final value (for expressions)
    char* error_message;    // Error description if failed
    int line;              // Error line number
    int column;            // Error column number
    
    // Performance metrics
    double execution_time_ms;
    size_t memory_used_bytes;
    size_t instructions_executed;
} ExecutionResult;

// Execution context - holds state for either backend
typedef struct {
    ExecutionBackend backend;
    
    union {
        struct {
            Environment* env;
        } ast;
        
        struct {
            MobiusVM* vm;
            BytecodeChunk* chunk;
        } bytecode;
    } state;
} ExecutionContext;

// ============================================================================= 
// EXECUTION INTERFACE
// =============================================================================

// Create execution context for specified backend
ExecutionContext* execution_context_create(ExecutionBackend backend);

// Free execution context and associated resources
void execution_context_free(ExecutionContext* ctx);

// Execute source code using the specified backend
ExecutionResult execute_source(ExecutionContext* ctx, const char* source);

// Execute a single statement using the specified backend
ExecutionResult execute_statement(ExecutionContext* ctx, Stmt* stmt);

// Execute a program (multiple statements) using the specified backend  
ExecutionResult execute_program(ExecutionContext* ctx, Stmt** statements, size_t count);

// Free execution result resources
void free_execution_result(ExecutionResult* result);

// Helper function to get backend name as string
const char* execution_backend_name(ExecutionBackend backend);

// Compare execution results for validation (returns true if equivalent)
bool execution_results_equivalent(const ExecutionResult* a, const ExecutionResult* b);

#endif // MOBIUS_EXECUTION_H
