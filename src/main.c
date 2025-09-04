#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "token.h"
#include "scanner.h"
#include "parser.h"
#include "ast.h"
#include "evaluator.h"
#include "environment.h"
#include "file_io.h"
#include "repl.h"

void test_interpreter() {
    printf("Testing Mobius Interpreter\n");
    printf("===========================\n\n");
    
    const char* test_source = 
        "var x = 42;\n"
        "var y = 3.14;\n"
        "print(\"Starting values:\");\n"
        "print(\"x =\", x, \"type:\", typeof(x));\n"
        "print(\"y =\", y, \"type:\", typeof(y));\n"
        "var z = x + y;\n"
        "print(\"z = x + y =\", z, \"type:\", typeof(z));\n"
        "x = x - 10;\n"
        "print(\"After x = x - 10:\", x);\n"
        "if (x > 30) {\n"
        "    z = z * 2;\n"
        "    print(\"z doubled:\", z);\n"
        "}\n"
        "print(\"Type conversions:\");\n"
        "print(\"int(y) =\", int(y), \"type:\", typeof(int(y)));\n"
        "print(\"float(x) =\", float(x), \"type:\", typeof(float(x)));\n"
        "print(\"str(z) =\", str(z), \"type:\", typeof(str(z)));\n";
    
    printf("Source code:\n%s\n", test_source);
    
    // Initialize environment
    init_global_environment();
    
    // Scan tokens
    TokenArray tokens = scan_source(test_source);
    printf("Scanned %zu tokens\n\n", tokens.count);
    
    // Parse AST
    ParseResult parse_result = parse(tokens);
    
    if (parse_result.had_error) {
        printf("Parser encountered errors!\n");
        free_parse_result(&parse_result);
        free_token_array(&tokens);
        cleanup_global_environment();
        return;
    }
    
    printf("Parsed %zu statements successfully\n\n", parse_result.count);
    
    // Execute the program
    printf("Executing program:\n");
    printf("==================\n");
    
    EvalResult eval_result = evaluate_program(parse_result.statements, parse_result.count, global_env);
    
    if (is_error(eval_result)) {
        printf("Runtime error during execution!\n");
        print_runtime_error(eval_result.error);
    } else {
        printf("Program executed successfully!\n\n");
        
        printf("Final environment state:\n");
        print_environment(global_env);
    }
    
    // Cleanup
    free_parse_result(&parse_result);
    free_token_array(&tokens);
    cleanup_global_environment();
}

int main(int argc, char *argv[]) {
    printf("Mobius Scripting Language Interpreter v0.1.0\n");
    
    if (argc > 1) {
        if (strcmp(argv[1], "--test-interpreter") == 0) {
            test_interpreter();
            return 0;
        }
        // Execute script file
        return execute_script_file(argv[1]);
    } else {
        // Start interactive REPL
        start_repl();
    }
    
    return 0;
}
