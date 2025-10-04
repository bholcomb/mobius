#include "util/file_io.h"
#include "frontend/scanner.h"
#include "frontend/parser.h"
#include "eval/evaluator.h"
#include "state/environment.h"
#include "state/mobius_state.h"
#include "library/library.h"
#include "plugin/module_registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// Read entire file into memory
FileResult read_file(const char* path) {
    FileResult result = {0};
    
    if (!path) {
        result.error = "Invalid file path";
        return result;
    }
    
    FILE* file = fopen(path, "r");
    if (!file) {
        result.error = "Could not open file";
        return result;
    }
    
    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (file_size < 0) {
        fclose(file);
        result.error = "Could not determine file size";
        return result;
    }
    
    // Allocate buffer
    result.content = malloc(file_size + 1);
    if (!result.content) {
        fclose(file);
        result.error = "Memory allocation failed";
        return result;
    }
    
    // Read file content
    size_t bytes_read = fread(result.content, 1, file_size, file);
    fclose(file);
    
    if (bytes_read != (size_t)file_size) {
        free(result.content);
        result.content = NULL;
        result.error = "Failed to read entire file";
        return result;
    }
    
    // Null-terminate the content
    result.content[file_size] = '\0';
    result.size = file_size;
    result.success = true;
    
    return result;
}

// Free file result
void free_file_result(FileResult* result) {
    if (result && result->content) {
        free(result->content);
        result->content = NULL;
        result->size = 0;
        result->success = false;
    }
}

// Check if file exists
bool file_exists(const char* path) {
    if (!path) return false;
    
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

// Get file extension
const char* get_file_extension(const char* path) {
    if (!path) return NULL;
    
    const char* last_dot = strrchr(path, '.');
    if (!last_dot || last_dot == path) return NULL;
    
    return last_dot + 1;
}

// Execute a script from a string with optional filename for error reporting
int execute_script_string(const char* source, const char* filename) {
    if (!source) {
        fprintf(stderr, "Error: No source code provided\n");
        return 1;
    }
    
    printf("Executing %s...\n", filename ? filename : "script");
    
    // Scan tokens
    TokenArray tokens = scan_source(source);
    if (tokens.count == 0) {
        fprintf(stderr, "Error: No tokens found\n");
        return 1;
    }
    
    MobiusState* state = mobius_new_state(NULL);
    register_stdlib_functions(state);

    // Set source context for better error reporting
    set_source_context(state, source);
    
    // Parse AST
    ParseResult parse_result = parse(state, tokens);
    if (parse_result.had_error) {
        fprintf(stderr, "Parse errors occurred\n");
        free_parse_result(&parse_result);  // Then free parse result
        free_token_array(&tokens);
        return 1;
    }

    // Execute the program
    EvalResult eval_result = evaluate_program(parse_result.statements, 
                                            parse_result.count, state->global_env);
    
    int exit_code = 0;
    if (is_error(eval_result)) {
        print_runtime_error(eval_result.error);
        exit_code = 1;
    }
    
    // Cleanup
    set_source_context(state, NULL);  // Clear source context
    free_parse_result(&parse_result);  // Then free parse result
    free_token_array(&tokens);
    mobius_free_state(state);
    return exit_code;
}

// Execute a script file
int execute_script_file(const char* path) {
    if (!path) {
        fprintf(stderr, "Error: No file path provided\n");
        return 1;
    }
    
    // Check if file exists
    if (!file_exists(path)) {
        fprintf(stderr, "Error: File '%s' does not exist\n", path);
        return 1;
    }
    
    // Check file extension (optional warning for non-.mob files)
    const char* ext = get_file_extension(path);
    if (ext && strcmp(ext, "mob") != 0) {
        printf("Warning: File '%s' does not have .mob extension\n", path);
    }
    
    // Read file content
    FileResult file_result = read_file(path);
    if (!file_result.success) {
        fprintf(stderr, "Error reading file '%s': %s\n", path, file_result.error);
        return 1;
    }
    
    // Execute the script
    int exit_code = execute_script_string(file_result.content, path);
    
    // Cleanup
    free_file_result(&file_result);
    
    return exit_code;
}
