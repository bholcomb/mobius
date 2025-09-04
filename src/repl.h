#ifndef MOBIUS_REPL_H
#define MOBIUS_REPL_H

#include "environment.h"
#include <stdbool.h>

// REPL state
typedef struct {
    Environment* env;       // REPL environment (persistent across commands)
    bool running;          // Whether REPL is active
    int command_count;     // Number of commands executed
} ReplState;

// REPL functions
void start_repl(void);
void repl_loop(ReplState* state);
bool process_repl_line(ReplState* state, const char* line);
void print_repl_prompt(int command_count);
void print_repl_welcome(void);
void print_repl_help(void);

// Line reading utilities
char* read_line(void);
char* trim_whitespace(char* str);
bool is_empty_line(const char* line);

// REPL commands
bool handle_repl_command(ReplState* state, const char* line);
void repl_command_help(void);
void repl_command_env(ReplState* state);
void repl_command_clear(void);
void repl_command_quit(void);

#endif // MOBIUS_REPL_H
