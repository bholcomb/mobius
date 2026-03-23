#include "repl.h"
#include "state/mobius_state.h"
#include "state/environment.h"
#include "frontend/scanner.h"
#include "frontend/parser.h"
#include "eval/evaluator.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>

#define MAX_LINE_LENGTH 1024

Repl::Repl(MobiusState* state)
    : state_(state), running_(true), command_count_(1) {}

void Repl::run() {
    printWelcome();
    loop();
}

void Repl::loop() {
    while (running_) {
        printPrompt();

        char* line = readLine();
        if (!line) {
            printf("\n");
            break;
        }

        char* trimmed = trimWhitespace(line);

        if (!isEmptyLine(trimmed)) {
            processLine(trimmed);
            command_count_++;
        }

        free(line);
    }
}

bool Repl::processLine(const char* line) {
    if (handleCommand(line)) {
        return true;
    }

    char* modified_line = nullptr;
    size_t len = strlen(line);
    bool needs_semicolon = false;

    if (len > 0 && line[len - 1] != ';' && line[len - 1] != '}') {
        needs_semicolon = true;
        modified_line = (char*)malloc(len + 2);
        if (modified_line) {
            strcpy(modified_line, line);
            strcat(modified_line, ";");
        }
    }

    const char* source = needs_semicolon && modified_line ? modified_line : line;

    TokenArray tokens = scan_source(source, state_->stringPool());
    if (tokens.count == 0) {
        printf("No tokens found\n");
        free(modified_line);
        return true;
    }

    ParseResult parse_result = parse(state_, tokens);
    if (parse_result.had_error) {
        printf("Parse error in input\n");
        free_parse_result(&parse_result);
        free_token_array(&tokens);
        free(modified_line);
        return true;
    }

    for (size_t i = 0; i < parse_result.count; i++) {
        Stmt* stmt = parse_result.statements[i];

        if (stmt->type == STMT_EXPRESSION) {
            ExpressionStmt* expr_stmt = &stmt->as.expression;
            EvalResult result = evaluate_expr(expr_stmt->expression, state_->globalEnv());

            if (is_error(result)) {
                print_runtime_error(result.error);
            } else if (result.return_count > 0) {
                Value val = state_->mainContext()->pop();
                if (val.type != VAL_NIL) {
                    print_value(val);
                    printf("\n");
                }
            }
        } else {
            EvalResult result = evaluate_stmt(stmt, state_->globalEnv());

            if (is_error(result)) {
                print_runtime_error(result.error);
            }
        }
    }

    free_parse_result(&parse_result);
    free_token_array(&tokens);
    free(modified_line);

    return true;
}

bool Repl::handleCommand(const char* line) {
    if (line[0] != ':') return false;

    const char* cmd = line + 1;

    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "h") == 0) {
        commandHelp();
        return true;
    }

    if (strcmp(cmd, "env") == 0 || strcmp(cmd, "e") == 0) {
        commandEnv();
        return true;
    }

    if (strcmp(cmd, "clear") == 0 || strcmp(cmd, "c") == 0) {
        commandClear();
        return true;
    }

    if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "q") == 0 ||
        strcmp(cmd, "exit") == 0) {
        commandQuit();
        running_ = false;
        return true;
    }

    printf("Unknown command: %s\n", cmd);
    printf("Type :help for available commands\n");
    return true;
}

void Repl::printWelcome() {
    printf("Mobius REPL v0.1.0\n");
    printf("Type :help for commands or enter Mobius code to execute.\n");
    printf("Press Ctrl+C or type :quit to exit.\n\n");
}

void Repl::printPrompt() const {
    printf("mobius[%d]> ", command_count_);
    fflush(stdout);
}

void Repl::commandHelp() {
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

void Repl::commandEnv() const {
    printf("Current environment:\n");
    state_->globalEnv()->print();
}

void Repl::commandClear() {
    printf("\033[2J\033[H");
    fflush(stdout);
}

void Repl::commandQuit() {
    printf("Goodbye!\n");
}

char* Repl::readLine() {
    char* line = (char*)malloc(MAX_LINE_LENGTH);
    if (!line) return nullptr;

    if (fgets(line, MAX_LINE_LENGTH, stdin)) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }
        return line;
    }

    free(line);
    return nullptr;
}

char* Repl::trimWhitespace(char* str) {
    if (!str) return nullptr;

    while (isspace((unsigned char)*str)) str++;

    if (*str == 0) return str;

    char* end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;

    end[1] = '\0';
    return str;
}

bool Repl::isEmptyLine(const char* line) {
    if (!line) return true;
    while (*line) {
        if (!isspace((unsigned char)*line)) return false;
        line++;
    }
    return true;
}
