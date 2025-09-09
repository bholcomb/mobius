/*
 * Simple Mobius Embedding Example
 * 
 * A minimal example showing how to embed Mobius in a C application.
 * This demonstrates the core embedding functionality without complex features.
 */

#include <stdio.h>
#include <stdlib.h>
#include "../src/mobius/mobius.h"

int main(void) {
    printf("🚀 Simple Mobius Embedding Example\n");
    printf("===================================\n\n");
    
    // Create interpreter state
    MobiusState* state = mobius_new_state();
    if (!state) {
        printf("❌ Failed to create Mobius state\n");
        return 1;
    }
    
    // Initialize core functionality
    if (mobius_init_core(state) != MOBIUS_OK) {
        printf("❌ Failed to initialize Mobius core\n");
        mobius_free_state(state);
        return 1;
    }
    
    printf("✅ Mobius interpreter initialized\n");
    printf("Version: %s\n", mobius_version_string());
    printf("Available functions: %zu\n\n", mobius_function_count(state));
    
    // Execute a simple script
    const char* script = 
        "print(\"Hello from embedded Mobius!\");\n"
        "var x = 10;\n"
        "var y = 20;\n"
        "print(\"x =\", x, \", y =\", y);\n"
        "print(\"x + y =\", x + y);\n"
        "print(\"sqrt(x * y) =\", sqrt(x * y));\n";
    
    printf("📝 Executing Mobius script:\n");
    printf("---------------------------\n");
    
    int result = mobius_exec_string(state, script);
    if (result != MOBIUS_OK) {
        printf("\n❌ Script execution failed!\n");
        MobiusError* error = mobius_get_last_error(state);
        if (error) {
            printf("Error: %s\n", error->message);
            mobius_free_error(error);
        }
    } else {
        printf("\n✅ Script executed successfully!\n");
    }
    
    // Try loading the math plugin
    printf("\n🔌 Testing plugin loading...\n");
    if (mobius_load_plugin(state, "./bin/modules/math.so") == MOBIUS_OK) {
        printf("✅ Math plugin loaded!\n");
        printf("Updated function count: %zu\n", mobius_function_count(state));
        
        // Test math plugin function
        const char* math_script = "print(\"sin(pi() / 2) =\", sin(pi() / 2));";
        printf("\n📝 Testing math plugin:\n");
        mobius_exec_string(state, math_script);
    } else {
        printf("ℹ️  Math plugin not available (this is okay)\n");
    }
    
    // Cleanup
    printf("\n🧹 Cleaning up...\n");
    mobius_free_state(state);
    printf("✅ Mobius embedding example completed!\n");
    
    return 0;
}
