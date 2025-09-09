#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "mobius/mobius.h"
#include "mobius/stack_trace.h"
#include "mobius/execution.h"
#include "mobius/file_io.h"

void test_interpreter_func() {
    printf("Testing Mobius Interpreter\n");
    printf("===========================\n\n");
    
    const char* test_source = 
        "var x = 42;\n"
        "var y = 3.14;\n"
        "print(\"Starting values:\");\n"
        "print(\"x =\", x, \"type:\", typeof(x));\n"
        "print(\"y =\", y, \"type:\", typeof(y));\n"
        "var z = x + y;\n"
        "print(\"z = x + y =\", z, \"type:\", typeof(z));\n"
        "x = x - 10;\n"
        "print(\"After x = x - 10:\", x);\n"
        "if (x > 30) {\n"
        "    z = z * 2;\n"
        "    print(\"z doubled:\", z);\n"
        "}\n"
        "print(\"Type conversions:\");\n"
        "print(\"int(y) =\", int(y), \"type:\", typeof(int(y)));\n"
        "print(\"float(x) =\", float(x), \"type:\", typeof(float(x)));\n"
        "print(\"str(z) =\", str(z), \"type:\", typeof(str(z)));\n";
    
    printf("Source code:\n%s\n", test_source);
    
    // Initialize environment
    init_global_environment();
    
    // Scan tokens
    TokenArray tokens = scan_source(test_source);
    printf("Scanned %zu tokens\n\n", tokens.count);
    
    // Parse AST
    ParseResult parse_result = parse(tokens);
    
    if (parse_result.had_error) {
        printf("Parser encountered errors!\n");
        free_parse_result(&parse_result);
        free_token_array(&tokens);
        cleanup_global_environment();
        return;
    }
    
    printf("Parsed %zu statements successfully\n\n", parse_result.count);
    
    // Execute the program
    printf("Executing program:\n");
    printf("==================\n");
    
    EvalResult eval_result = evaluate_program(parse_result.statements, parse_result.count, global_env);
    
    if (is_error(eval_result)) {
        printf("Runtime error during execution!\n");
        print_runtime_error(eval_result.error);
    } else {
        printf("Program executed successfully!\n\n");
        
        printf("Final environment state:\n");
        print_environment(global_env);
    }
    
    // Cleanup
    free_parse_result(&parse_result);
    free_token_array(&tokens);
    cleanup_global_environment();
}

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
    
    // Print result if it's not nil (only the value, no extra output)
    if (result.result_value.type != VAL_NIL) {
        switch (result.result_value.type) {
            case VAL_INTEGER:
                printf("%d", result.result_value.as.integer.value.i32);
                break;
            case VAL_FLOAT:
                printf("%g", result.result_value.as.float_val);
                break;
            case VAL_STRING:
                printf("%s", result.result_value.as.string && result.result_value.as.string->data ? 
                       result.result_value.as.string->data : "");
                break;
            case VAL_BOOL:
                printf("%s", result.result_value.as.boolean ? "true" : "false");
                break;
            case VAL_ARRAY:
                print_value(result.result_value);
                break;
            case VAL_TABLE:
                print_value(result.result_value);
                break;
            default:
                print_value(result.result_value);
                break;
        }
        printf("\n");
        fflush(stdout);
    }
    
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
    bool test_interpreter = false;
    bool list_modules = false;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--bytecode") == 0) {
            use_bytecode = true;
        } else if (strcmp(argv[i], "--test-interpreter") == 0) {
            test_interpreter = true;
        } else if (strcmp(argv[i], "--list-modules") == 0) {
            list_modules = true;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Mobius Scripting Language Interpreter v0.1.0\n\n");
            printf("Usage: %s [options] [script_file]\n", argv[0]);
            printf("\nOptions:\n");
            printf("  --bytecode         Use bytecode VM instead of AST tree-walker\n");
            printf("  --test-interpreter Run built-in interpreter test\n");
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
    
    if (test_interpreter) {
        printf("Mobius Scripting Language Interpreter v0.1.0\n");
        test_interpreter_func();
    } else if (list_modules) {
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
