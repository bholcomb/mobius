#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "mobius/mobius.h"
#include "mobius/stack_trace.h"
#include "mobius/execution.h"
#include "mobius/file_io.h"


int execute_file_with_backend(const char* filename, ExecutionBackend backend) {
    // Read the file
    FileResult file_result = read_file(filename);
    if (!file_result.success) {
        fprintf(stderr, "Error reading file '%s': %s\n", filename, file_result.error ? file_result.error : "Unknown error");
        return 1;
    }
    
    // Create execution context
    ExecutionContext* ctx = execution_context_create(backend);
    if (!ctx) {
        fprintf(stderr, "Failed to create execution context\n");
        free_file_result(&file_result);
        return 1;
    }
    
    // Execute the source
    ExecutionResult result = execute_source(ctx, file_result.content);
    
    if (!result.success) {
        fprintf(stderr, "Execution failed: %s\n", result.error_message ? result.error_message : "Unknown error");
        free_execution_result(&result);
        execution_context_free(ctx);
        free_file_result(&file_result);
        return 1;
    }
    
    // For script files, don't print the final result value
    // (unlike REPL mode where we show expression results)
    // The final result is typically from the last expression statement,
    // but scripts usually want to control their own output via print statements
    
    // Cleanup
    free_execution_result(&result);
    execution_context_free(ctx);
    free_file_result(&file_result);
    
    return 0;
}

int main(int argc, char *argv[]) {
    // Parse command line arguments
    bool use_bytecode = false;
    const char* script_file = NULL;
    bool list_modules = false;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--bytecode") == 0) {
            use_bytecode = true;
        } else if (strcmp(argv[i], "--list-modules") == 0) {
            list_modules = true;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Mobius Scripting Language Interpreter v0.1.0\n\n");
            printf("Usage: %s [options] [script_file]\n", argv[0]);
            printf("\nOptions:\n");
            printf("  --bytecode         Use bytecode VM instead of AST tree-walker\n");
            printf("  --list-modules     List loaded modules and functions\n");
            printf("  --help, -h         Show this help message\n");
            printf("\nIf no script file is provided, starts interactive REPL.\n");
            return 0;
        } else if (argv[i][0] != '-') {
            script_file = argv[i];
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return 1;
        }
    }
    
    // Initialize stack trace system
    stack_trace_init();
    
    // Initialize plugin system
    ModuleRegistry* registry = create_module_registry();
    if (!registry) {
        fprintf(stderr, "Failed to create module registry\n");
        return 1;
    }
    
    // Set global registry for evaluator
    set_global_module_registry(registry);
    
    // Only show startup messages when not running a script file
    if (!script_file) {
        printf("Mobius Scripting Language Interpreter v0.1.0\n");
        if (use_bytecode) {
            printf("Using bytecode VM execution backend\n");
        } else {
            printf("Using AST tree-walker execution backend\n");
        }
    }
    
    // Auto-discover and load extension modules (only for AST backend and when not running scripts)
    if (!use_bytecode && !script_file) {
        printf("🔍 Auto-discovering extension modules...\n");
        
        int loaded_count = auto_load_core_modules(registry);
        if (loaded_count > 0) {
            printf("✅ Successfully loaded %d extension module%s\n", 
                   loaded_count, loaded_count == 1 ? "" : "s");
        } else if (loaded_count == 0) {
            printf("ℹ️  No extension modules found (built-in functions still available)\n");
        } else {
            printf("⚠️  Failed to scan for modules: %s\n", 
                   module_registry_get_last_error() ? module_registry_get_last_error() : "unknown error");
        }
    } else if (!use_bytecode && script_file) {
        // Silently load modules for AST execution
        auto_load_core_modules(registry);
    }
    
    int result = 0;
    
    if (list_modules) {
        print_loaded_modules(registry);
        print_available_functions(registry);
    } else if (script_file) {
        // Execute script file with selected backend
        ExecutionBackend backend = use_bytecode ? EXEC_BACKEND_BYTECODE : EXEC_BACKEND_AST;
        result = execute_file_with_backend(script_file, backend);
    } else {
        // Start interactive REPL (always uses AST for now)
        start_repl();
    }
    
    free_module_registry(registry);
    
    // Cleanup stack trace system
    stack_trace_cleanup();
    
    return result;
}
