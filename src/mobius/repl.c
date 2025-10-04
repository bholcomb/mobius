#include "repl.h"
#include "util/file_io.h"
#include "state/environment.h"
#include "state/mobius_state.h"
#include "frontend/scanner.h"
#include "frontend/parser.h"
#include "eval/evaluator.h"
#include "library/library.h"
#include "util/utility.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_LINE_LENGTH 1024

// Utility functions
char* trim_whitespace(char* str) {
    if (!str) return NULL;
    
    // Trim leading whitespace
    while (isspace((unsigned char)*str)) str++;
    
    if (*str == 0) return str;  // All spaces
    
    // Trim trailing whitespace
    char* end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    
    end[1] = '\0';
    return str;
}

bool is_empty_line(const char* line) {
    if (!line) return true;
    while (*line) {
        if (!isspace((unsigned char)*line)) return false;
        line++;
    }
    return true;
}

char* read_line(void) {
    char* line = malloc(MAX_LINE_LENGTH);
    if (!line) return NULL;
    
    if (fgets(line, MAX_LINE_LENGTH, stdin)) {
        // Remove newline if present
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }
        return line;
    }
    
    free(line);
    return NULL;
}

// REPL commands
bool handle_repl_command(ReplState* state, const char* line) {
    if (line[0] != ':') return false;  // Not a command
    
    const char* cmd = line + 1;
    
    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "h") == 0) {
        repl_command_help();
        return true;
    }
    
    if (strcmp(cmd, "env") == 0 || strcmp(cmd, "e") == 0) {
        repl_command_env(state);
        return true;
    }
    
    if (strcmp(cmd, "clear") == 0 || strcmp(cmd, "c") == 0) {
        repl_command_clear();
        return true;
    }
    
    if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "q") == 0 || 
        strcmp(cmd, "exit") == 0) {
        repl_command_quit();
        state->running = false;
        return true;
    }
    
    printf("Unknown command: %s\n", cmd);
    printf("Type :help for available commands\n");
    return true;
}

void repl_command_help(void) {
    printf("Mobius REPL Commands:\n");
    printf("  :help, :h        Show this help message\n");
    printf("  :env, :e         Show current environment variables\n");
    printf("  :clear, :c       Clear the screen\n");
    printf("  :quit, :q, :exit Exit the REPL\n");
    printf("\nEnter Mobius expressions or statements to evaluate them.\n");
    printf("Examples:\n");
    printf("  2 + 3\n");
    printf("  var x = 42\n");
    printf("  print(\"Hello, world!\")\n");
}

void repl_command_env(ReplState* state) {
    printf("Current environment:\n");
    print_environment(state->state->global_env);
}

void repl_command_clear(void) {
    // ANSI escape sequence to clear screen and move cursor to top-left
    printf("\033[2J\033[H");
    fflush(stdout);
}

void repl_command_quit(void) {
    printf("Goodbye!\n");
}

void print_repl_welcome(void) {
    printf("Mobius REPL v0.1.0\n");
    printf("Type :help for commands or enter Mobius code to execute.\n");
    printf("Press Ctrl+C or type :quit to exit.\n\n");
}

void print_repl_prompt(int command_count) {
    printf("mobius[%d]> ", command_count);
    fflush(stdout);
}

bool process_repl_line(ReplState* state, const char* line) {
    // Handle commands
    if (handle_repl_command(state, line)) {
        return true;
    }
    
    // Add semicolon to line if it doesn't end with one (for REPL convenience)
    char* modified_line = NULL;
    size_t len = strlen(line);
    bool needs_semicolon = false;
    
    if (len > 0 && line[len - 1] != ';' && line[len - 1] != '}') {
        needs_semicolon = true;
        modified_line = malloc(len + 2);
        if (modified_line) {
            strcpy(modified_line, line);
            strcat(modified_line, ";");
        }
    }
    
    const char* source = needs_semicolon && modified_line ? modified_line : line;
    
    // Try to execute as Mobius code
    TokenArray tokens = scan_source(source);
    if (tokens.count == 0) {
        printf("No tokens found\n");
        if (modified_line) free(modified_line);
        return true;
    }
    
    // Check for parse errors
    ParseResult parse_result = parse(state->state, tokens);
    if (parse_result.had_error) {
        printf("Parse error in input\n");
        free_parse_result(&parse_result);
        free_token_array(&tokens);
        if (modified_line) free(modified_line);
        return true;
    }
    
    // Execute the statements
    for (size_t i = 0; i < parse_result.count; i++) {
        Stmt* stmt = parse_result.statements[i];
        
        // For REPL, handle expression statements specially - evaluate and print
        if (stmt->type == STMT_EXPRESSION) {
            ExpressionStmt* expr_stmt = &stmt->as.expression;
            EvalResult result = evaluate_expr(expr_stmt->expression, state->state->global_env);
            
            if (is_error(result)) {
                print_runtime_error(result.error);
            } else if (result.return_count > 0) {
                // Pop and print the value (unless it's nil)
                Value val = ctx_pop(state->state->main_context);
                if (val.type != VAL_NIL) {
                    print_value(val);
                    printf("\n");
                }
                free_value(val);
            }
        } else {
            // For other statements, evaluate normally
            EvalResult result = evaluate_stmt(stmt, state->state->global_env);
            
            if (is_error(result)) {
                print_runtime_error(result.error);
            }
        }
    }
    
    // Cleanup
    free_parse_result(&parse_result);
    free_token_array(&tokens);
    if (modified_line) free(modified_line);
    
    return true;
}

void repl_loop(ReplState* state) {
    while (state->running) {
        print_repl_prompt(state->command_count);
        
        char* line = read_line();
        if (!line) {
            // EOF (Ctrl+D)
            printf("\n");
            break;
        }
        
        char* trimmed = trim_whitespace(line);
        
        if (!is_empty_line(trimmed)) {
            process_repl_line(state, trimmed);
            state->command_count++;
        }
        
        free(line);
    }
}

void start_repl(MobiusState* mobius_state) {
    print_repl_welcome();
    
    // Initialize REPL state
    ReplState state = {0};
    state.state = mobius_state;
    state.running = true;
    state.command_count = 1;
        
    // Run the REPL loop
    repl_loop(&state);
}
