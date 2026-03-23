#ifndef MOBIUS_SCANNER_H
#define MOBIUS_SCANNER_H

#include "token.h"
#include <stddef.h>

// Scanner state structure
typedef struct {
    const char* start;      // Start of current token
    const char* current;    // Current character being examined
    const char* source;     // Source code string
    int line;              // Current line number
    int column;            // Current column number
    StringInternPool* pool; // String intern pool for identifier interning
} Scanner;

// Token array structure for returning results
typedef struct {
    Token* tokens;         // Array of tokens
    size_t count;          // Number of tokens
    size_t capacity;       // Allocated capacity
} TokenArray;

// Main scanning function
TokenArray scan_source(const char* source, StringInternPool* pool = nullptr);

// Scanner utility functions
void init_scanner(Scanner* scanner, const char* source, StringInternPool* pool = nullptr);
Token scan_token(Scanner* scanner);
void free_token_array(TokenArray* array);

// Character classification helpers
bool is_alpha(char c);
bool is_digit(char c);
bool is_alphanumeric(char c);
bool is_whitespace(char c);

// Scanner state helpers
bool is_at_end(Scanner* scanner);
char advance(Scanner* scanner);
bool match(Scanner* scanner, char expected);
char peek(Scanner* scanner);
char peek_next(Scanner* scanner);
void skip_whitespace(Scanner* scanner);

// Token creation helpers for scanner
Token make_simple_token(Scanner* scanner, TokenType type);
Token scan_string(Scanner* scanner);
Token scan_number(Scanner* scanner);
Token scan_identifier(Scanner* scanner);
Token scan_char(Scanner* scanner);

// Keyword recognition
TokenType identifier_type(const char* start, int length);

#endif // MOBIUS_SCANNER_H
