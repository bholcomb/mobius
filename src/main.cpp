#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "mobius/mobius.h"

int execute_file(MobiusState* state, const char* filename) {
    if (!state || !filename) {
        fprintf(stderr, "Invalid arguments to execute_file\n");
        return 1;
    }
    
    int result = mobius_exec_file(state, filename);
    return (result != MOBIUS_OK) ? 1 : 0;
}

int main(int argc, char *argv[]) {
    const char* script_file = NULL;
    bool debug_mode = false;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--debug") == 0 || strcmp(argv[i], "-d") == 0) {
            debug_mode = true;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Mobius Scripting Language Interpreter v0.1.0\n\n");
            printf("Usage: %s [options] [script_file]\n", argv[0]);
            printf("\nOptions:\n");
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
    
    // Configure plugin directories (before creating state)
    mobius_add_plugin_directory("./bin/modules");
    mobius_add_plugin_directory("./modules");
    
    MobiusConfig config = mobius_default_config();
    if (debug_mode) {
        config.debug_mode = true;
    }
    
    MobiusState* state = mobius_new_state(&config);
    if (!state) {
        fprintf(stderr, "Failed to create Mobius state\n");
        return 1;
    }
    
    if (mobius_init_stdlib(state) != MOBIUS_OK) {
        fprintf(stderr, "Failed to initialize standard library\n");
        mobius_free_state(state);
        return 1;
    }
    
    int result = 0;
    
    if (script_file) {
        result = execute_file(state, script_file);
    } else {
        printf("Mobius Scripting Language Interpreter v0.1.0\n");
        mobius_start_repl(state);
    }
    
    mobius_free_state(state);
    
    return result;
}
