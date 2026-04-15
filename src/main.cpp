#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <mobius/mobius.h>
#include <mobius/mobius_plugin.h>

int execute_file(MobiusState* state, const char* filename) {
    if (!state || !filename) {
        fprintf(stderr, "Invalid arguments to execute_file\n");
        return 1;
    }
    
    int result = mobius_exec_file(state, filename);
    return (result != MOBIUS_OK) ? 1 : 0;
}

static void register_cli_argv(MobiusState* state, int argc, char* argv[], int arg_start) {
    if (!state) return;
    if (arg_start < 0) arg_start = 0;
    if (arg_start > argc) arg_start = argc;

    mobius_stack_pushNewArray(state, (size_t)(argc - arg_start));
    int arr_idx = mobius_stack_size(state) - 1;
    for (int i = arg_start; i < argc; i++) {
        mobius_stack_pushString(state, argv[i]);
        mobius_stack_arrayPush(state, arr_idx);
    }
    mobius_stack_setGlobal(state, "argv");
    mobius_set_global_readonly(state, "argv", true);
}

int main(int argc, char *argv[]) {
    const char* script_file = NULL;
    bool debug_mode = false;
    int script_arg_start = argc;
    
    for (int i = 1; i < argc; i++) {
        if (!script_file && (strcmp(argv[i], "--debug") == 0 || strcmp(argv[i], "-d") == 0)) {
            debug_mode = true;
        } else if (!script_file && (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)) {
            printf("Mobius Scripting Language Interpreter v0.1.0\n\n");
            printf("Usage: %s [options] [script_file] [script_args...]\n", argv[0]);
            printf("\nOptions:\n");
            printf("  --debug, -d        Enable debug mode\n");
            printf("  --help, -h         Show this help message\n");
            printf("\nIf a script file is provided, remaining positional arguments are exposed\n");
            printf("to the script via the global argv array.\n");
            printf("\nIf no script file is provided, starts interactive REPL.\n");
            return 0;
        } else if (!script_file) {
            script_file = argv[i];
            script_arg_start = i + 1;
        } else {
            break;
        }
    }
    
    MobiusConfig config = mobius_default_config();
    if (debug_mode) {
        config.debug_mode = true;
    }
    
    MobiusState* state = mobius_new_state(&config);
    if (!state) {
        fprintf(stderr, "Failed to create Mobius state\n");
        return 1;
    }
    
    mobius_add_plugin_directory(state, "./bin/modules");
    mobius_add_plugin_directory(state, "./modules");

    if (mobius_init_stdlib(state) != MOBIUS_OK) {
        fprintf(stderr, "Failed to initialize standard library\n");
        mobius_free_state(state);
        return 1;
    }

    register_cli_argv(state, argc, argv, script_file ? script_arg_start : argc);
    
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
