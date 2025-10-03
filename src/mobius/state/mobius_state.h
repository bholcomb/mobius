#pragma once

#include "value.h"

typedef struct MobiusState MobiusState;
typedef struct ExecutionContext ExecutionContext;

// Create new VM instance
MobiusState* mobius_new_state(MobiusConfig* config);

// Initialize standard library
int mobius_init_stdlib(MobiusState* state);

// Clean up VM
void mobius_free_state(MobiusState* state);

// ================================================
// Thread Management
// ================================================

// Attach current thread to VM (call once per thread)
ThreadState* mobius_thread_attach(MobiusState* state);

// Detach current thread from VM
void mobius_thread_detach(MobiusState* state);

// Get current thread's state (fast, thread-local)
ThreadState* mobius_get_thread_state(MobiusState* state);

// ================================================
// Execution Context Management
// ================================================

// Create main execution context for current thread
ExecutionContext* mobius_create_main_context(ThreadState* thread);

// Create fiber (lightweight, no suspend/resume)
ExecutionContext* mobius_create_fiber(ThreadState* thread);

// Create coroutine (full suspend/resume support)
ExecutionContext* mobius_create_coroutine(ThreadState* thread);

// Switch to different context (within same thread)
int mobius_switch_context(ThreadState* thread, ExecutionContext* context);

// Suspend current context (coroutines only)
int mobius_suspend(ExecutionContext* context, Value* suspend_value);

// Resume suspended context
int mobius_resume(ExecutionContext* context, Value* resume_value);

// Clean up context
void mobius_free_context(ExecutionContext* context);

// ================================================
// Execution API
// ================================================

// Execute code in current context
int mobius_exec(MobiusState* state, const char* code);

// Execute code in specific context
int mobius_exec_in_context(ExecutionContext* context, const char* code);

// Call function in current context
int mobius_call(MobiusState* state, const char* func_name, 
                Value* args, int arg_count, Value* result);