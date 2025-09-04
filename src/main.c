#include <stdio.h>
#include <stdlib.h>
#include "token.h"
#include "scanner.h"

int main(int argc, char *argv[]) {
    printf("Mobius Scripting Language Interpreter v0.1.0\n");
    printf("Usage: %s [script_file]\n", argv[0]);
    
    if (argc > 1) {
        printf("Would execute script: %s\n", argv[1]);
        // TODO: Implement script execution using scanner
        // TokenArray tokens = scan_source(source_code);
        // ... parse and execute tokens ...
    } else {
        printf("Interactive mode not yet implemented\n");
        // TODO: Implement REPL using scanner
    }
    
    return 0;
}
