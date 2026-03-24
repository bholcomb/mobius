/*
 * Simple Mobius Embedding Example
 * 
 * A minimal example showing how to embed Mobius in a C application.
 * This demonstrates the core embedding functionality without complex features.
 */

#include <stdio.h>
#include <stdlib.h>
#include <mobius/mobius.h>

int main(void) {
    printf("🚀 Simple Mobius Embedding Example\n");
    printf("===================================\n\n");
    
    // Create interpreter state with default config
    MobiusState* state = mobius_new_state(NULL);
    if (!state) {
        printf("❌ Failed to create Mobius state\n");
        return 1;
    }
    
    // Initialize standard library
    if (mobius_init_stdlib(state) != MOBIUS_OK) {
        printf("❌ Failed to initialize Mobius stdlib\n");
        mobius_free_state(state);
        return 1;
    }
    
    printf("✅ Mobius interpreter initialized\n");
    printf("Version: 0.1.0\n");
    printf("Interpreter ready.\n\n");
    
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
        printf("\nScript execution failed!\n");
    } else {
        printf("\nScript executed successfully!\n");
    }
    
    // Cleanup
    printf("\n🧹 Cleaning up...\n");
    mobius_free_state(state);
    printf("✅ Mobius embedding example completed!\n");
    
    return 0;
}
