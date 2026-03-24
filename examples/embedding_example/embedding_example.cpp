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
#include <mobius/mobius.h>
#include <mobius/mobius_plugin.h>

// ============================================================================
// CUSTOM C FUNCTIONS FOR MOBIUS
// ============================================================================

/**
 * Custom function: add two numbers
 * Demonstrates basic value exchange and error handling
 */
int custom_add(MobiusState* state, int arg_count) {
    if (arg_count != 2) {
        return mobius_error(state, "custom_add requires exactly 2 arguments");
    }

    if (!mobius_stack_isNumber(state, -1) || !mobius_stack_isNumber(state, -2)) {
        return mobius_error(state, "custom_add arguments must be numbers");
    }

    double num_a = mobius_stack_asFloat64(state, -2);
    double num_b = mobius_stack_asFloat64(state, -1);
    mobius_stack_pop(state, 2);
    mobius_stack_pushFloat64(state, num_a + num_b);
    return 1;
}

/**
 * Custom function: get system information
 * Demonstrates string return values
 */
int custom_system_info(MobiusState* state, int arg_count) {
    if (arg_count != 0) {
        return mobius_error(state, "system_info takes no arguments");
    }

    const char* info = "System: Linux x86_64, Mobius v0.1.0";
    mobius_stack_pushString(state, info);
    return 1;
}

/**
 * Custom function: double a number
 * Demonstrates simple numeric operations
 */
int custom_double(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return mobius_error(state, "double requires exactly 1 argument");
    }

    if (!mobius_stack_isNumber(state, -1)) {
        return mobius_error(state, "Argument must be a number");
    }

    double num = mobius_stack_asFloat64(state, -1);
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, num * 2.0);
    return 1;
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
        printf("Script execution failed (details printed by error handler)\n");
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
    
    // Set variables from C using the public stack API
    mobius_stack_pushString(state, "Set from C");
    mobius_stack_setGlobal(state, "c_string");
    mobius_stack_pushInt64(state, 123);
    mobius_stack_setGlobal(state, "c_number");
    mobius_stack_pushBool(state, true);
    mobius_stack_setGlobal(state, "c_bool");
    
    // Execute script that uses these variables
    const char* script = 
        "print(\"From C:\", c_string, c_number, c_bool);\n"
        "var result = c_number * 2;\n"
        "print(\"Calculated:\", result);\n";
    
    mobius_exec_string(state, script);
    
    // Get variables back from Mobius using the public stack API
    mobius_stack_getGlobal(state, "result");
    int64_t result_int = mobius_stack_getInt64(state, -1);
    printf("Result from Mobius: %ld\n", result_int);
    mobius_stack_pop(state, 1);
    
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
    
    // Register custom functions using the public API
    mobius_register_function(state, "c_add", custom_add);
    mobius_register_function(state, "system_info", custom_system_info);
    mobius_register_function(state, "double", custom_double);
    
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
        printf("Script execution failed (details printed by error handler)\n");
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
        printf("Script execution failed (details printed by error handler)\n");
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
    mobius_register_function(state, "c_add", custom_add);
    
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
        if (result == MOBIUS_OK) {
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
            printf("Script execution failed (details printed by error handler)\n");
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
    printf("- Bidirectional value exchange (mobius_stack_push*/mobius_stack_get*)\n");
    printf("- Custom C function registration (mobius_register_function)\n");
    printf("- Comprehensive error handling (MobiusError)\n");
    printf("- File and string execution (mobius_exec_file/mobius_exec_string)\n");
    printf("- Standard library integration (mobius_init_stdlib)\n");
    
    return 0;
}
