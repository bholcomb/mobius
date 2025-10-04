#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "mobius/mobius.h"
#include "mobius/state/mobius_state.h"
#include "mobius/plugin/module_registry.h"

// Forward declaration for REPL
void start_repl(MobiusState* state);

int execute_file(MobiusState* state, const char* filename) {
    if (!state || !filename) {
        fprintf(stderr, "Invalid arguments to execute_file\n");
        return 1;
    }
    
    // Execute the file using the MobiusState API
    int result = mobius_exec_file(state, filename);
    
    if (result != MOBIUS_OK) {
        // Get and print error details
        MobiusError* error = mobius_get_last_error(state);
        if (error) {
            fprintf(stderr, "Error executing '%s': %s\n", filename, 
                    error->message ? error->message : "Unknown error");
            if (error->line > 0) {
                fprintf(stderr, "  at line %d:%d", error->line, error->column);
                if (error->function_name) {
                    fprintf(stderr, " in function '%s'", error->function_name);
                }
                fprintf(stderr, "\n");
            }
            if (error->suggestion) {
                fprintf(stderr, "  suggestion: %s\n", error->suggestion);
            }
            mobius_free_error(error);
        }
        return 1;
    }
    
    return 0;
}

int main(int argc, char *argv[]) {
    // Parse command line arguments
    const char* script_file = NULL;
    bool list_modules = false;
    bool debug_mode = false;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--list-modules") == 0) {
            list_modules = true;
        } else if (strcmp(argv[i], "--debug") == 0 || strcmp(argv[i], "-d") == 0) {
            debug_mode = true;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Mobius Scripting Language Interpreter v0.1.0\n\n");
            printf("Usage: %s [options] [script_file]\n", argv[0]);
            printf("\nOptions:\n");
            printf("  --list-modules     List loaded modules and functions\n");
            printf("  --debug, -d        Enable debug mode\n");
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
    
    // Create MobiusState with optional custom config
    MobiusConfig config = mobius_default_config();
    if (debug_mode) {
        config.debug_mode = true;
    }
    
    MobiusState* state = mobius_new_state(&config);
    if (!state) {
        fprintf(stderr, "Failed to create Mobius state\n");
        return 1;
    }
    
    // Initialize standard library
    if (mobius_init_stdlib(state) != MOBIUS_OK) {
        fprintf(stderr, "Failed to initialize standard library\n");
        mobius_free_state(state);
        return 1;
    }
    
    // Only show startup messages when not running a script file
    if (!script_file) {
        printf("Mobius Scripting Language Interpreter v0.1.0\n");
    }
    
    // Auto-discover and load extension modules
    if (!script_file) {
        printf("🔍 Auto-discovering extension modules...\n");
        
        int loaded_count = auto_load_core_modules(state->registry);
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
        auto_load_core_modules(state->registry);
    }
    
    int result = 0;
    
    if (list_modules) {
        print_loaded_modules(state->registry);
        print_available_functions(state->registry);
    } else if (script_file) {
        // Execute script file
        result = execute_file(state, script_file);
    } else {
        // Start interactive REPL
        start_repl(state);
    }
    
    // Cleanup
    mobius_free_state(state);
    
    return result;
}
