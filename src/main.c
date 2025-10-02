#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "mobius/mobius.h"
#include "mobius/stack_trace.h"
#include "mobius/file_io.h"

int execute_file(const char* filename) {
    // Read the file
    FileResult file_result = read_file(filename);
    if (!file_result.success) {
        fprintf(stderr, "Error reading file '%s': %s\n", filename, file_result.error ? file_result.error : "Unknown error");
        return 1;
    }
    
    // Scan the source code
    TokenArray token_array = scan_source(file_result.content);
    
    // Parse the source code
    ParseResult parse_result = parse(token_array);
    if (parse_result.had_error) {
        fprintf(stderr, "Parse error occurred\n");
        free_parse_result(&parse_result);
        free_token_array(&token_array);
        free_file_result(&file_result);
        return 1;
    }
    
    Stmt** statements = parse_result.statements;
    
    // Initialize global execution stack if not already done
    if (!global_context) {
        global_context = create_execution_context(256);
        if (!global_context) {
            fprintf(stderr, "Failed to create execution stack\n");
            free_parse_result(&parse_result);
            free_token_array(&token_array);
            free_file_result(&file_result);
            return 1;
        }
    }
    
    // Create environment
    Environment* env = create_environment(NULL);
    if (!env) {
        fprintf(stderr, "Failed to create environment\n");
        free_parse_result(&parse_result);
        free_token_array(&token_array);
        free_file_result(&file_result);
        return 1;
    }
    
    // Execute statements
    int result = 0;
    for (size_t i = 0; i < parse_result.count; i++) {
        EvalResult eval_result = evaluate_stmt(statements[i], env);
        if (is_error(eval_result)) {
            fprintf(stderr, "Runtime error: %s\n", eval_result.error.message ? eval_result.error.message : "Unknown error");
            result = 1;
            break;
        }
        // For script files, we don't print the result value unless it's from a print statement
    }
    
    // Cleanup
    free_environment(env);
    free_parse_result(&parse_result);
    free_token_array(&token_array);
    free_file_result(&file_result);
    
    return result;
}

int main(int argc, char *argv[]) {
    // Parse command line arguments
    const char* script_file = NULL;
    bool list_modules = false;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--list-modules") == 0) {
            list_modules = true;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Mobius Scripting Language Interpreter v0.1.0\n\n");
            printf("Usage: %s [options] [script_file]\n", argv[0]);
            printf("\nOptions:\n");
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
    }
    
    // Auto-discover and load extension modules
    if (!script_file) {
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
    } else {
        // Silently load modules for script execution
        auto_load_core_modules(registry);
    }
    
    int result = 0;
    
    if (list_modules) {
        print_loaded_modules(registry);
        print_available_functions(registry);
    } else if (script_file) {
        // Execute script file
        result = execute_file(script_file);
    } else {
        // Start interactive REPL
        start_repl();
    }
    
    free_module_registry(registry);
    
    // Cleanup stack trace system
    stack_trace_cleanup();
    
    return result;
}
