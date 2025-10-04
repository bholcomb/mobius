/*
 * Mobius Embedding Example
 * 
 * This example demonstrates how to embed the Mobius scripting language
 * in a C application, similar to embedding Lua or Python.
 * 
 * Features demonstrated:
 * - Creating and managing interpreter states
 * - Executing Mobius scripts from C
 * - Exchanging values between C and Mobius
 * - Registering custom C functions
 * - Error handling
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Include the Mobius library headers
#include "../../src/mobius/state/mobius_state.h"
#include "../../src/mobius/state/environment.h"
#include "../../src/mobius/data/value.h"
#include "../../src/mobius/library/library.h"
#include "../../src/mobius/eval/evaluator.h"

// ============================================================================
// CUSTOM C FUNCTIONS FOR MOBIUS
// ============================================================================

/**
 * Custom function: add two numbers
 * Demonstrates basic value exchange and error handling
 */
EvalResult custom_add(MobiusState* state, int arg_count) {
    ExecutionContext* ctx = mobius_get_main_context(state);
    
    // Validate argument count
    if (arg_count != 2) {
        return make_error(state->global_env, "custom_add requires exactly 2 arguments", 0, 0);
    }
    
    // Pop arguments from stack (in reverse order)
    Value b = ctx_pop(ctx);
    Value a = ctx_pop(ctx);
    
    // Validate argument types
    if (a.type != VAL_FLOAT64 && a.type != VAL_INTEGER) {
        free_value(b);
        return make_error(state->global_env, "First argument must be a number", 0, 0);
    }
    if (b.type != VAL_FLOAT64 && b.type != VAL_INTEGER) {
        free_value(a);
        return make_error(state->global_env, "Second argument must be a number", 0, 0);
    }
    
    // Convert to double for calculation
    double num_a = (a.type == VAL_FLOAT64) ? a.as.float64_val : (double)a.as.integer.value.i64;
    double num_b = (b.type == VAL_FLOAT64) ? b.as.float64_val : (double)b.as.integer.value.i64;
    
    // Clean up
    free_value(a);
    free_value(b);
    
    // Push result
    ctx_push(ctx, make_float_value(num_a + num_b));
    return make_success(1);
}

/**
 * Custom function: get system information
 * Demonstrates string return values
 */
EvalResult custom_system_info(MobiusState* state, int arg_count) {
    ExecutionContext* ctx = mobius_get_main_context(state);
    
    if (arg_count != 0) {
        return make_error(state->global_env, "system_info takes no arguments", 0, 0);
    }
    
    const char* info = "System: Linux x86_64, Mobius v0.1.0";
    ctx_push(ctx, make_string_value_from_cstr(state, info));
    return make_success(1);
}

/**
 * Custom function: double a number
 * Demonstrates simple numeric operations
 */
EvalResult custom_double(MobiusState* state, int arg_count) {
    ExecutionContext* ctx = mobius_get_main_context(state);
    
    if (arg_count != 1) {
        return make_error(state->global_env, "double requires exactly 1 argument", 0, 0);
    }
    
    Value val = ctx_pop(ctx);
    
    if (val.type != VAL_FLOAT64 && val.type != VAL_INTEGER) {
        free_value(val);
        return make_error(state->global_env, "Argument must be a number", 0, 0);
    }
    
    double num = (val.type == VAL_FLOAT64) ? val.as.float64_val : (double)val.as.integer.value.i64;
    free_value(val);
    
    ctx_push(ctx, make_float_value(num * 2.0));
    return make_success(1);
}

// ============================================================================
// EXAMPLE FUNCTIONS
// ============================================================================

/**
 * Example 1: Basic embedding and script execution
 */
void example_basic_execution(void) {
    printf("=== Example 1: Basic Script Execution ===\n");
    
    // Create interpreter state with default config
    MobiusState* state = mobius_new_state(NULL);
    if (!state) {
        printf("Failed to create Mobius state\n");
        return;
    }
    
    // Initialize standard library
    if (mobius_init_stdlib(state) != MOBIUS_OK) {
        printf("Failed to initialize Mobius stdlib\n");
        mobius_free_state(state);
        return;
    }
    
    // Execute a simple script
    const char* script = 
        "var greeting = \"Hello from Mobius!\";\n"
        "var number = 42;\n"
        "print(greeting);\n"
        "print(\"The answer is:\", number);\n"
        "print(\"sqrt(16) =\", sqrt(16));\n";
    
    printf("Executing script:\n%s\n", script);
    
    int result = mobius_exec_string(state, script);
    if (result != MOBIUS_OK) {
        MobiusError* error = mobius_get_last_error(state);
        if (error) {
            printf("Error: %s\n", error->message);
            if (error->suggestion) {
                printf("Suggestion: %s\n", error->suggestion);
            }
            mobius_free_error(error);
        }
    }
    
    mobius_free_state(state);
    printf("\n");
}

/**
 * Example 2: Value exchange between C and Mobius
 */
void example_value_exchange(void) {
    printf("=== Example 2: Value Exchange ===\n");
    
    MobiusState* state = mobius_new_state(NULL);
    if (!state) return;
    mobius_init_stdlib(state);
    
    // Set variables from C
    define_variable(state->global_env, "c_string", make_string_value_from_cstr(state, "Set from C"));
    define_variable(state->global_env, "c_number", make_integer_value(NUM_INT64, 123));
    define_variable(state->global_env, "c_bool", make_bool_value(true));
    
    // Execute script that uses these variables
    const char* script = 
        "print(\"From C:\", c_string, c_number, c_bool);\n"
        "var result = c_number * 2;\n"
        "print(\"Calculated:\", result);\n";
    
    mobius_exec_string(state, script);
    
    // Get variables back from Mobius
    bool found;
    Value result_val = get_variable(state->global_env, "result", &found);
    if (found && result_val.type == VAL_INTEGER) {
        printf("Result from Mobius: %ld\n", result_val.as.integer.value.i64);
    }
    
    mobius_free_state(state);
    printf("\n");
}

/**
 * Example 3: Custom C functions
 */
void example_custom_functions(void) {
    printf("=== Example 3: Custom C Functions ===\n");
    
    MobiusState* state = mobius_new_state(NULL);
    if (!state) return;
    mobius_init_stdlib(state);
    
    // Register custom functions
    define_variable(state->global_env, "c_add", make_native_function_value(custom_add));
    define_variable(state->global_env, "system_info", make_native_function_value(custom_system_info));
    define_variable(state->global_env, "double", make_native_function_value(custom_double));
    
    // Execute script using custom functions
    const char* script = 
        "print(\"Custom function results:\");\n"
        "print(\"c_add(10, 20) =\", c_add(10, 20));\n"
        "print(\"c_add(3.14, 2.86) =\", c_add(3.14, 2.86));\n"
        "print(\"System info:\", system_info());\n"
        "\n"
        "var my_value = 5;\n"
        "print(\"Before double: my_value =\", my_value);\n"
        "var doubled = double(my_value);\n"
        "print(\"After double:\", doubled);\n";
    
    printf("Executing script with custom functions:\n");
    int result = mobius_exec_string(state, script);
    if (result != MOBIUS_OK) {
        MobiusError* error = mobius_get_last_error(state);
        if (error) {
            printf("Error: %s\n", error->message);
            mobius_free_error(error);
        }
    }
    
    mobius_free_state(state);
    printf("\n");
}

/**
 * Example 4: Advanced features
 */
void example_advanced_features(void) {
    printf("=== Example 4: Advanced Features ===\n");
    
    MobiusState* state = mobius_new_state(NULL);
    if (!state) return;
    mobius_init_stdlib(state);
    
    // Show interpreter information
    printf("Mobius version: 0.1.0\n");
    
    // Execute advanced script with functions and control flow
    const char* script = 
        "print(\"\\n--- Advanced Mobius Script ---\");\n"
        "print(\"Built-in math functions:\");\n"
        "print(\"abs(-42) =\", abs(-42));\n"
        "print(\"pow(2, 8) =\", pow(2, 8));\n"
        "print(\"sqrt(144) =\", sqrt(144));\n"
        "\n"
        "func fibonacci(n) {\n"
        "    if (n <= 1) return n;\n"
        "    return fibonacci(n - 1) + fibonacci(n - 2);\n"
        "}\n"
        "\n"
        "print(\"\\nFibonacci sequence:\");\n"
        "for (var i = 0; i < 10; i = i + 1) {\n"
        "    print(\"fib(\", i, \") =\", fibonacci(i));\n"
        "}\n";
    
    printf("\nExecuting advanced script:\n");
    int result = mobius_exec_string(state, script);
    if (result != MOBIUS_OK) {
        MobiusError* error = mobius_get_last_error(state);
        if (error) {
            printf("Error: %s\n", error->message);
            if (error->suggestion) {
                printf("Suggestion: %s\n", error->suggestion);
            }
            mobius_free_error(error);
        }
    }
    
    mobius_free_state(state);
    printf("\n");
}

/**
 * Example 5: Error handling
 */
void example_error_handling(void) {
    printf("=== Example 5: Error Handling ===\n");
    
    MobiusState* state = mobius_new_state(NULL);
    if (!state) return;
    mobius_init_stdlib(state);
    
    // Register a custom function that can fail
    define_variable(state->global_env, "c_add", make_native_function_value(custom_add));
    
    // Execute scripts with various errors
    const char* scripts[] = {
        "var x = 10;\nvar y = x + ;",  // Syntax error
        "print(undefined_variable);",   // Runtime error
        "c_add(1, 2, 3);",             // Argument count error
        "c_add(\"hello\", \"world\");"  // Type error
    };
    
    const char* descriptions[] = {
        "Syntax error",
        "Undefined variable",
        "Wrong argument count",
        "Type error"
    };
    
    for (size_t i = 0; i < 4; i++) {
        printf("Testing %s:\n", descriptions[i]);
        printf("Script: %s\n", scripts[i]);
        
        int result = mobius_exec_string(state, scripts[i]);
        if (result != MOBIUS_OK) {
            MobiusError* error = mobius_get_last_error(state);
            if (error) {
                printf("Error code: %d\n", error->code);
                printf("Message: %s\n", error->message);
                if (error->suggestion) {
                    printf("Suggestion: %s\n", error->suggestion);
                }
                if (error->line > 0) {
                    printf("Location: line %d, column %d\n", error->line, error->column);
                }
                mobius_free_error(error);
            }
        } else {
            printf("Unexpected success!\n");
        }
        printf("\n");
        
        mobius_clear_error(state);
    }
    
    mobius_free_state(state);
}

/**
 * Example 6: File execution
 */
void example_file_execution(void) {
    printf("=== Example 6: File Execution ===\n");
    
    MobiusState* state = mobius_new_state(NULL);
    if (!state) return;
    mobius_init_stdlib(state);
    
    // Create a test script file
    const char* test_script = 
        "// Test script from file\n"
        "print(\"Executing from file!\");\n"
        "var x = 10;\n"
        "var y = 20;\n"
        "print(\"x + y =\", x + y);\n";
    
    FILE* f = fopen("/tmp/test_mobius.mb", "w");
    if (f) {
        fprintf(f, "%s", test_script);
        fclose(f);
        
        printf("Executing script from file: /tmp/test_mobius.mb\n");
        int result = mobius_exec_file(state, "/tmp/test_mobius.mb");
        if (result != MOBIUS_OK) {
            MobiusError* error = mobius_get_last_error(state);
            if (error) {
                printf("Error: %s\n", error->message);
                mobius_free_error(error);
            }
        }
        
        // Clean up test file
        remove("/tmp/test_mobius.mb");
    } else {
        printf("Could not create test file\n");
    }
    
    mobius_free_state(state);
    printf("\n");
}

// ============================================================================
// MAIN PROGRAM
// ============================================================================

int main(void) {
    printf("🚀 Mobius Embedding API Example\n");
    printf("=================================\n\n");
    
    // Run all examples
    example_basic_execution();
    example_value_exchange();
    example_custom_functions();
    example_advanced_features();
    example_error_handling();
    example_file_execution();
    
    printf("✅ All examples completed!\n");
    printf("\nThis demonstrates how to embed Mobius in your C application.\n");
    printf("Key features:\n");
    printf("- Isolated interpreter states (MobiusState)\n");
    printf("- Bidirectional value exchange (define_variable/get_variable)\n");
    printf("- Custom C function registration (make_native_function_value)\n");
    printf("- Comprehensive error handling (MobiusError)\n");
    printf("- File and string execution (mobius_exec_file/mobius_exec_string)\n");
    printf("- Standard library integration (mobius_init_stdlib)\n");
    
    return 0;
}
