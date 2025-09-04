#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mobius/mobius.h"

void test_interpreter() {
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

int main(int argc, char *argv[]) {
    printf("Mobius Scripting Language Interpreter v0.1.0\n");
    
    // Initialize plugin system
    ModuleRegistry* registry = create_module_registry();
    if (!registry) {
        fprintf(stderr, "Failed to create module registry\n");
        return 1;
    }
    
    // Set global registry for evaluator
    set_global_module_registry(registry);
    
    // Load available extension modules
    printf("🔍 Loading extension modules...\n");
    
    // Try to load math module
    PluginLoadResult math_result = load_module(registry, "./bin/modules/math.so");
    if (math_result.status == PLUGIN_STATUS_LOADED) {
        printf("✅ Loaded math extension v%s\n", math_result.plugin->metadata.version);
    } else {
        printf("ℹ️  Math extension not available (%s)\n", 
               math_result.error_message ? math_result.error_message : "module not found");
    }
    
    int result = 0;
    
    if (argc > 1) {
        if (strcmp(argv[1], "--test-interpreter") == 0) {
            test_interpreter();
        } else if (strcmp(argv[1], "--list-modules") == 0) {
            print_loaded_modules(registry);
            print_available_functions(registry);
        } else {
            // Execute script file
            result = execute_script_file(argv[1]);
        }
    } else {
        // Start interactive REPL
        start_repl();
    }
    
    free_module_registry(registry);
    return result;
}
