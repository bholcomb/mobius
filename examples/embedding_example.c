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
 * - Loading plugins
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/mobius/mobius.h"

// ============================================================================
// CUSTOM C FUNCTIONS FOR MOBIUS
// ============================================================================

/**
 * Custom function: add two numbers
 * Demonstrates basic value exchange and error handling
 */
int custom_add(MobiusState* state, MobiusValue** args, size_t arg_count, MobiusValue** result) {
    // Validate argument count
    MOBIUS_CHECK_ARG_COUNT(2);
    
    // Validate argument types
    if (!mobius_is_integer(args[0]) && !mobius_is_float(args[0])) {
        return mobius_set_error(state, MOBIUS_ERROR_TYPE, "First argument must be a number");
    }
    if (!mobius_is_integer(args[1]) && !mobius_is_float(args[1])) {
        return mobius_set_error(state, MOBIUS_ERROR_TYPE, "Second argument must be a number");
    }
    
    // Perform calculation
    double a = mobius_convert_to_float(args[0]);
    double b = mobius_convert_to_float(args[1]);
    double sum = a + b;
    
    // Return result
    *result = mobius_create_float(state, sum);
    return MOBIUS_OK;
}

/**
 * Custom function: get system information
 * Demonstrates string return values
 */
int custom_system_info(MobiusState* state, MobiusValue** args, size_t arg_count, MobiusValue** result) {
    MOBIUS_CHECK_ARG_COUNT(0);
    
    // Get system information (simplified example)
    const char* info = "System: Linux x86_64, Compiler: GCC";
    *result = mobius_create_string(state, info);
    
    return MOBIUS_OK;
}

/**
 * Custom function: multiply array elements
 * Demonstrates working with Mobius variables from C
 */
int custom_multiply_global(MobiusState* state, MobiusValue** args, size_t arg_count, MobiusValue** result) {
    MOBIUS_CHECK_ARG_COUNT(1);
    MOBIUS_CHECK_ARG_TYPE(0, mobius_is_string, "string");
    
    // Get variable name
    const char* var_name = mobius_to_string(args[0]);
    
    // Get the variable from Mobius
    MobiusValue* var_value = mobius_get_global(state, var_name);
    if (!var_value) {
        return mobius_set_error(state, MOBIUS_ERROR_RUNTIME, "Variable not found");
    }
    
    // For this example, assume it's a number and multiply by 2
    if (!mobius_is_integer(var_value) && !mobius_is_float(var_value)) {
        mobius_free_value(var_value);
        return mobius_set_error(state, MOBIUS_ERROR_TYPE, "Variable must be a number");
    }
    
    double value = mobius_convert_to_float(var_value);
    mobius_free_value(var_value);
    
    *result = mobius_create_float(state, value * 2.0);
    return MOBIUS_OK;
}

// ============================================================================
// EXAMPLE FUNCTIONS
// ============================================================================

/**
 * Example 1: Basic embedding and script execution
 */
void example_basic_execution(void) {
    printf("=== Example 1: Basic Script Execution ===\n");
    
    // Create interpreter state
    MobiusState* state = mobius_new_state();
    if (!state) {
        printf("Failed to create Mobius state\n");
        return;
    }
    
    // Initialize core functionality
    if (mobius_init_core(state) != MOBIUS_OK) {
        printf("Failed to initialize Mobius core\n");
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
    
    MobiusState* state = mobius_new_state();
    if (!state) return;
    mobius_init_core(state);
    
    // Set variables from C
    MobiusValue* str_val = mobius_create_string(state, "Set from C");
    MobiusValue* num_val = mobius_create_integer(state, 123);
    MobiusValue* bool_val = mobius_create_bool(state, true);
    
    mobius_set_global(state, "c_string", str_val);
    mobius_set_global(state, "c_number", num_val);
    mobius_set_global(state, "c_bool", bool_val);
    
    // Execute script that uses these variables
    const char* script = 
        "print(\"From C:\", c_string, c_number, c_bool);\n"
        "var result = c_number * 2;\n"
        "var message = \"Calculated: \" + str(result);\n";
    
    mobius_exec_string(state, script);
    
    // Get variables back from Mobius
    MobiusValue* result_val = mobius_get_global(state, "result");
    MobiusValue* message_val = mobius_get_global(state, "message");
    
    if (result_val && mobius_is_integer(result_val)) {
        printf("Result from Mobius: %ld\n", mobius_to_integer(result_val));
    }
    if (message_val && mobius_is_string(message_val)) {
        printf("Message from Mobius: %s\n", mobius_to_string(message_val));
    }
    
    // Cleanup
    mobius_free_value(str_val);
    mobius_free_value(num_val);
    mobius_free_value(bool_val);
    mobius_free_value(result_val);
    mobius_free_value(message_val);
    mobius_free_state(state);
    printf("\n");
}

/**
 * Example 3: Custom C functions
 */
void example_custom_functions(void) {
    printf("=== Example 3: Custom C Functions ===\n");
    
    MobiusState* state = mobius_new_state();
    if (!state) return;
    mobius_init_core(state);
    
    // Register custom functions
    mobius_register_function(state, "c_add", custom_add, 2, "Add two numbers");
    mobius_register_function(state, "system_info", custom_system_info, 0, "Get system information");
    mobius_register_function(state, "multiply_global", custom_multiply_global, 1, "Multiply global variable by 2");
    
    // Execute script using custom functions
    const char* script = 
        "print(\"Custom function results:\");\n"
        "print(\"c_add(10, 20) =\", c_add(10, 20));\n"
        "print(\"c_add(3.14, 2.86) =\", c_add(3.14, 2.86));\n"
        "print(\"System info:\", system_info());\n"
        "\n"
        "var my_value = 5;\n"
        "print(\"Before multiply_global: my_value =\", my_value);\n"
        "var doubled = multiply_global(\"my_value\");\n"
        "print(\"After multiply_global:\", doubled);\n";
    
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
 * Example 4: Plugin loading and advanced features
 */
void example_plugins_and_advanced(void) {
    printf("=== Example 4: Plugins and Advanced Features ===\n");
    
    MobiusState* state = mobius_new_state();
    if (!state) return;
    mobius_init_core(state);
    
    // Try to load math plugin
    int plugin_result = mobius_load_plugin(state, "./bin/modules/math.so");
    if (plugin_result == MOBIUS_OK) {
        printf("Math plugin loaded successfully!\n");
    } else {
        printf("Math plugin not available (this is okay for the example)\n");
    }
    
    // Show interpreter information
    printf("Mobius version: %s\n", mobius_version_string());
    printf("Loaded plugins: %zu\n", mobius_plugin_count(state));
    printf("Available functions: %zu\n", mobius_function_count(state));
    
    // Execute advanced script
    const char* script = 
        "print(\"\\n--- Advanced Mobius Script ---\");\n"
        "var numbers = [1, 2, 3, 4, 5];  // Note: arrays not yet implemented\n"
        "print(\"Built-in functions work:\");\n"
        "print(\"abs(-42) =\", abs(-42));\n"
        "print(\"pow(2, 8) =\", pow(2, 8));\n"
        "print(\"sqrt(144) =\", sqrt(144));\n"
        "\n"
        "// Try math plugin functions if available\n"
        "// print(\"sin(pi() / 2) =\", sin(pi() / 2));\n"
        "\n"
        "func fibonacci(n) {\n"
        "    if (n <= 1) return n;\n"
        "    return fibonacci(n - 1) + fibonacci(n - 2);\n"
        "}\n"
        "\n"
        "print(\"Fibonacci sequence:\");\n"
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
    
    MobiusState* state = mobius_new_state();
    if (!state) return;
    mobius_init_core(state);
    
    // Register a custom function that can fail
    mobius_register_function(state, "c_add", custom_add, 2, "Add two numbers");
    
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
    example_plugins_and_advanced();
    example_error_handling();
    
    printf("✅ All examples completed!\n");
    printf("\nThis demonstrates how to embed Mobius in your C application.\n");
    printf("Key features:\n");
    printf("- Isolated interpreter states\n");
    printf("- Bidirectional value exchange\n");
    printf("- Custom C function registration\n");
    printf("- Comprehensive error handling\n");
    printf("- Plugin system integration\n");
    printf("- Memory management\n");
    
    return 0;
}
