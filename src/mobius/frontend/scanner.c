#include "frontend/scanner.h"
#include "data/value.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Initial capacity for token array
#define INITIAL_TOKEN_CAPACITY 64

// Keyword lookup table
typedef struct {
    const char* keyword;
    TokenType token_type;
} Keyword;

static const Keyword keywords[] = {
    {"and",      TOKEN_AND},
    {"break",    TOKEN_BREAK},
    {"case",     TOKEN_CASE},
    {"continue", TOKEN_CONTINUE},
    {"default",  TOKEN_DEFAULT},
    {"else",     TOKEN_ELSE},
    {"elif",     TOKEN_ELIF},
    {"enum",     TOKEN_ENUM},
    {"false",    TOKEN_FALSE},
    {"for",      TOKEN_FOR},
    {"func",     TOKEN_FUNC},
    {"if",       TOKEN_IF},
    {"import",   TOKEN_IMPORT},
    {"is",       TOKEN_IS},
    {"nil",      TOKEN_NIL},
    {"not",      TOKEN_NOT},
    {"or",       TOKEN_OR},
    {"return",   TOKEN_RETURN},
    {"switch",   TOKEN_SWITCH},
    {"true",     TOKEN_TRUE},
    {"var",      TOKEN_VAR},
    {"while",    TOKEN_WHILE},
    
    // Type keywords
    {"int8",     TOKEN_TYPE_INT8},
    {"int16",    TOKEN_TYPE_INT16},
    {"int32",    TOKEN_TYPE_INT32},
    {"int64",    TOKEN_TYPE_INT64},
    {"uint8",    TOKEN_TYPE_UINT8},
    {"uint16",   TOKEN_TYPE_UINT16},
    {"uint32",   TOKEN_TYPE_UINT32},
    {"uint64",   TOKEN_TYPE_UINT64},
    {"float32",  TOKEN_TYPE_FLOAT32},
    {"float64",  TOKEN_TYPE_FLOAT64},
};

static const size_t keyword_count = sizeof(keywords) / sizeof(keywords[0]);

// Initialize scanner state
void init_scanner(Scanner* scanner, const char* source) {
    scanner->start = source;
    scanner->current = source;
    scanner->source = source;
    scanner->line = 1;
    scanner->column = 1;
}

// Check if at end of source
bool is_at_end(Scanner* scanner) {
    return *scanner->current == '\0';
}

// Advance to next character
char advance(Scanner* scanner) {
    if (is_at_end(scanner)) return '\0';
    
    if (*scanner->current == '\n') {
        scanner->line++;
        scanner->column = 1;
    } else {
        scanner->column++;
    }
    
    return *scanner->current++;
}

// Check if current character matches expected, advance if so
bool match(Scanner* scanner, char expected) {
    if (is_at_end(scanner)) return false;
    if (*scanner->current != expected) return false;
    
    advance(scanner);
    return true;
}

// Peek at current character without advancing
char peek(Scanner* scanner) {
    return *scanner->current;
}

// Peek at next character without advancing
char peek_next(Scanner* scanner) {
    if (is_at_end(scanner)) return '\0';
    return scanner->current[1];
}

// Character classification
bool is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

bool is_alphanumeric(char c) {
    return is_alpha(c) || is_digit(c);
}

bool is_whitespace(char c) {
    return c == ' ' || c == '\r' || c == '\t';
}

// Skip whitespace (but not newlines - they may be significant)
void skip_whitespace(Scanner* scanner) {
    while (true) {
        char c = peek(scanner);
        if (is_whitespace(c)) {
            advance(scanner);
        } else if (c == '/' && peek_next(scanner) == '/') {
            // Skip line comment
            while (peek(scanner) != '\n' && !is_at_end(scanner)) {
                advance(scanner);
            }
        } else if (c == '/' && peek_next(scanner) == '*') {
            // Skip block comment
            advance(scanner); // consume '/'
            advance(scanner); // consume '*'
            while (!is_at_end(scanner)) {
                if (peek(scanner) == '*' && peek_next(scanner) == '/') {
                    advance(scanner); // consume '*'
                    advance(scanner); // consume '/'
                    break;
                }
                advance(scanner);
            }
        } else {
            break;
        }
    }
}

// Create a simple token
Token make_simple_token(Scanner* scanner, TokenType type) {
    return make_token(type, scanner->start, 
                     (int)(scanner->current - scanner->start),
                     scanner->line, 
                     scanner->column - (int)(scanner->current - scanner->start));
}

// Scan string literal
Token scan_string(Scanner* scanner) {
    while (peek(scanner) != '"' && !is_at_end(scanner)) {
        if (peek(scanner) == '\n') {
            // Multi-line strings allowed
        }
        advance(scanner);
    }
    
    if (is_at_end(scanner)) {
        return make_error_token("Unterminated string", scanner->line, scanner->column);
    }
    
    // Consume closing quote
    advance(scanner);
    
    // Create string token (without quotes)
    Token token = make_simple_token(scanner, TOKEN_STRING);
    
    // Allocate memory for string content (without quotes)
    // This memory will be freed by free_token() when the token is cleaned up
    size_t content_length = scanner->current - scanner->start - 2; // -2 for quotes
    char* string_content = malloc(content_length + 1);
    if (string_content) {
        strncpy(string_content, scanner->start + 1, content_length);
        string_content[content_length] = '\0';
        token.literal.string = string_content;
    } else {
        token.literal.string = NULL;
    }
    
    return token;
}

// Scan character literal
Token scan_char(Scanner* scanner) {
    if (is_at_end(scanner)) {
        return make_error_token("Unterminated character", scanner->line, scanner->column);
    }
    
    char character = advance(scanner);
    
    if (peek(scanner) != '\'') {
        return make_error_token("Unterminated character", scanner->line, scanner->column);
    }
    
    advance(scanner); // consume closing quote
    
    Token token = make_simple_token(scanner, TOKEN_CHAR);
    token.literal.character = character;
    return token;
}

// Scan number literal
Token scan_number(Scanner* scanner) {
    // Scan integer part
    while (is_digit(peek(scanner))) {
        advance(scanner);
    }
    
    // Check for decimal point
    bool is_float = false;
    if (peek(scanner) == '.' && is_digit(peek_next(scanner))) {
        is_float = true;
        advance(scanner); // consume '.'
        
        while (is_digit(peek(scanner))) {
            advance(scanner);
        }
    }
    
    // Check for scientific notation
    if (peek(scanner) == 'e' || peek(scanner) == 'E') {
        is_float = true;
        advance(scanner); // consume 'e'/'E'
        
        if (peek(scanner) == '+' || peek(scanner) == '-') {
            advance(scanner); // consume sign
        }
        
        while (is_digit(peek(scanner))) {
            advance(scanner);
        }
    }
    
    // Convert to number
    if (is_float) {
        double value = strtod(scanner->start, NULL);
        return make_float_token(scanner->start, 
                               (int)(scanner->current - scanner->start),
                               scanner->line,
                               scanner->column - (int)(scanner->current - scanner->start),
                               value);
    } else {
        // Use 64-bit signed integers by default (unless user specifies type suffixes)
        // TODO: Implement type suffixes (i8, u8, i16, etc.)
        long long value = strtoll(scanner->start, NULL, 10);
        return make_integer_token(scanner->start,
                                 (int)(scanner->current - scanner->start),
                                 scanner->line,
                                 scanner->column - (int)(scanner->current - scanner->start),
                                 NUM_INT64, value);
    }
}

// Look up keyword
TokenType identifier_type(const char* start, int length) {
    for (size_t i = 0; i < keyword_count; i++) {
        if ((int)strlen(keywords[i].keyword) == length &&
            memcmp(start, keywords[i].keyword, length) == 0) {
            return keywords[i].token_type;
        }
    }
    return TOKEN_IDENTIFIER;
}

// Scan identifier or keyword
Token scan_identifier(Scanner* scanner) {
    while (is_alphanumeric(peek(scanner))) {
        advance(scanner);
    }
    
    TokenType type = identifier_type(scanner->start, 
                                   (int)(scanner->current - scanner->start));
    return make_simple_token(scanner, type);
}

// Scan a single token
Token scan_token(Scanner* scanner) {
    skip_whitespace(scanner);
    
    scanner->start = scanner->current;
    
    if (is_at_end(scanner)) {
        return make_simple_token(scanner, TOKEN_EOF);
    }
    
    char c = advance(scanner);
    
    // Single character tokens
    switch (c) {
        case '(': return make_simple_token(scanner, TOKEN_LEFT_PAREN);
        case ')': return make_simple_token(scanner, TOKEN_RIGHT_PAREN);
        case '[': return make_simple_token(scanner, TOKEN_LEFT_BRACKET);
        case ']': return make_simple_token(scanner, TOKEN_RIGHT_BRACKET);
        case '{': return make_simple_token(scanner, TOKEN_LEFT_BRACE);
        case '}': return make_simple_token(scanner, TOKEN_RIGHT_BRACE);
        case ',': return make_simple_token(scanner, TOKEN_COMMA);
        case '.':
            if (match(scanner, '.')) {
                if (match(scanner, '.')) {
                    return make_simple_token(scanner, TOKEN_DOT_DOT_DOT);
                } else {
                    return make_simple_token(scanner, TOKEN_DOT_DOT);
                }
            } else {
                return make_simple_token(scanner, TOKEN_DOT);
            }
        case ';': return make_simple_token(scanner, TOKEN_SEMICOLON);
        case ':': return make_simple_token(scanner, TOKEN_COLON);
        case '?': return make_simple_token(scanner, TOKEN_QUESTION);
        case '~': return make_simple_token(scanner, TOKEN_TILDE);
        case '%': return make_simple_token(scanner, TOKEN_PERCENT);
        case '^': return make_simple_token(scanner, TOKEN_CARET);
        case '#': return make_simple_token(scanner, TOKEN_HASH);
        case '\n': return make_simple_token(scanner, TOKEN_NEWLINE);
        
        // One or two character tokens
        case '!':
            return make_simple_token(scanner, 
                match(scanner, '=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
        case '=':
            return make_simple_token(scanner,
                match(scanner, '=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
        case '<':
            if (match(scanner, '=')) {
                return make_simple_token(scanner, TOKEN_LESS_EQUAL);
            } else if (match(scanner, '<')) {
                return make_simple_token(scanner, TOKEN_LEFT_SHIFT);
            } else {
                return make_simple_token(scanner, TOKEN_LESS);
            }
        case '>':
            if (match(scanner, '=')) {
                return make_simple_token(scanner, TOKEN_GREATER_EQUAL);
            } else if (match(scanner, '>')) {
                return make_simple_token(scanner, TOKEN_RIGHT_SHIFT);
            } else {
                return make_simple_token(scanner, TOKEN_GREATER);
            }
        case '+':
            if (match(scanner, '+')) {
                return make_simple_token(scanner, TOKEN_PLUS_PLUS);
            } else if (match(scanner, '=')) {
                return make_simple_token(scanner, TOKEN_PLUS_EQUAL);
            } else {
                return make_simple_token(scanner, TOKEN_PLUS);
            }
        case '-':
            if (match(scanner, '-')) {
                return make_simple_token(scanner, TOKEN_MINUS_MINUS);
            } else if (match(scanner, '=')) {
                return make_simple_token(scanner, TOKEN_MINUS_EQUAL);
            } else if (match(scanner, '>')) {
                return make_simple_token(scanner, TOKEN_ARROW);
            } else {
                return make_simple_token(scanner, TOKEN_MINUS);
            }
        case '*':
            if (match(scanner, '=')) {
                return make_simple_token(scanner, TOKEN_STAR_EQUAL);
            } else {
                return make_simple_token(scanner, TOKEN_STAR);
            }
        case '/':
            if (match(scanner, '=')) {
                return make_simple_token(scanner, TOKEN_SLASH_EQUAL);
            } else {
                return make_simple_token(scanner, TOKEN_SLASH);
            }
        case '&':
            if (match(scanner, '&')) {
                return make_simple_token(scanner, TOKEN_AND_AND);
            } else {
                return make_simple_token(scanner, TOKEN_AMPERSAND);
            }
        case '|':
            if (match(scanner, '|')) {
                return make_simple_token(scanner, TOKEN_OR_OR);
            } else {
                return make_simple_token(scanner, TOKEN_PIPE);
            }
        
        // String literals
        case '"':
            return scan_string(scanner);
            
        // Character literals
        case '\'':
            return scan_char(scanner);
    }
    
    // Numbers
    if (is_digit(c)) {
        // Back up to include the first digit
        scanner->current--;
        scanner->column--;
        return scan_number(scanner);
    }
    
    // Identifiers and keywords
    if (is_alpha(c)) {
        // Back up to include the first character
        scanner->current--;
        scanner->column--;
        return scan_identifier(scanner);
    }
    
    // Unknown character
    char error_msg[64];
    snprintf(error_msg, sizeof(error_msg), "Unexpected character '%c'", c);
    return make_error_token(error_msg, scanner->line, scanner->column);
}

// Main scanning function
TokenArray scan_source(const char* source) {
    TokenArray array = {0};
    array.capacity = INITIAL_TOKEN_CAPACITY;
    array.tokens = malloc(sizeof(Token) * array.capacity);
    
    if (!array.tokens) {
        array.capacity = 0;
        return array;
    }
    
    Scanner scanner;
    init_scanner(&scanner, source);
    
    while (true) {
        Token token = scan_token(&scanner);
        
        // Resize array if needed
        if (array.count >= array.capacity) {
            array.capacity *= 2;
            Token* new_tokens = realloc(array.tokens, sizeof(Token) * array.capacity);
            if (!new_tokens) {
                free_token_array(&array);
                array.capacity = 0;
                return array;
            }
            array.tokens = new_tokens;
        }
        
        array.tokens[array.count++] = token;
        
        if (token.type == TOKEN_EOF || token.type == TOKEN_ERROR) {
            break;
        }
    }
    
    return array;
}

// Free token array
void free_token_array(TokenArray* array) {
    if (array->tokens) {
        // Free any allocated string literals
        for (size_t i = 0; i < array->count; i++) {
            if (array->tokens[i].type == TOKEN_STRING && 
                array->tokens[i].literal.string) {
                free((void*)array->tokens[i].literal.string);
            }
        }
        free(array->tokens);
        array->tokens = NULL;
    }
    array->count = 0;
    array->capacity = 0;
}
