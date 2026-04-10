#include "frontend/token.h"
#include "util/utility.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Array of token type names for debugging and error messages
static const char* token_names[] = {
    // Single-character tokens
    "LEFT_PAREN", "RIGHT_PAREN", "LEFT_BRACKET", "RIGHT_BRACKET",
    "LEFT_BRACE", "RIGHT_BRACE", "COMMA", "DOT", "MINUS", "PLUS",
    "SEMICOLON", "COLON", "SLASH", "STAR", "PERCENT", "QUESTION",
    "AMPERSAND", "PIPE", "CARET", "TILDE", "HASH",
    
    // One or two character tokens
    "BANG", "BANG_EQUAL", "EQUAL", "EQUAL_EQUAL", "GREATER", "GREATER_EQUAL",
    "LESS", "LESS_EQUAL", "PLUS_PLUS", "MINUS_MINUS", "PLUS_EQUAL",
    "MINUS_EQUAL", "STAR_EQUAL", "SLASH_EQUAL", "PERCENT_EQUAL", "AND_AND", "OR_OR",
    "LEFT_SHIFT", "RIGHT_SHIFT", "DOT_DOT", "DOT_DOT_DOT",
    
    // Literals
    "IDENTIFIER", "STRING", "INTERP_STRING", "INTEGER", "FLOAT", "CHAR",
    
    // Keywords
    "AND", "BREAK", "CASE", "CONTINUE", "DEFAULT", "ELSE", "ELIF", 
    "ENUM", "FALSE", "FOR", "FUNC", "IF", "IMPORT", "IS", "NIL", 
    "NOT", "OR", "RETURN", "SWITCH", "TRUE", "THROW", "TRY", "CATCH", "IN",
    "VAR", "WHEN", "WHILE", "FINALLY", "SPAWN", "AWAIT", "YIELD", "SHARED", "ATOMIC",
    "STRUCT", "UNION", "AT",
    
    // Type annotation tokens
    "TYPE_INT64", "TYPE_UINT64", "TYPE_FLOAT64",
    
    // Special tokens
    "NEWLINE", "EOF", "ERROR"
};

const char* token_type_name(TokenType type) {
    if (type >= 0 && type < sizeof(token_names) / sizeof(token_names[0])) {
        return token_names[type];
    }
    return "UNKNOWN";
}

const char* numeric_type_name(NumberType type) {
    return number_type_name(type);
}

void print_token(const Token* token) {
    printf("Token[%s] '%s' at line %d, column %d",
           token_type_name(token->type),
           token->identifier ? token->identifier : "N/A",
           token->line,
           token->column);
    
    // Print literal value if applicable
    if (token->type == TOKEN_INTEGER) {
        printf(" (%s: ", numeric_type_name(token->literal.integer.num_type));
        if (token->literal.integer.num_type == NUM_UINT64)
            printf("%lu", (uint64_t)token->literal.integer.value);
        else
            printf("%ld", token->literal.integer.value);
        printf(")");
    } else if (token->type == TOKEN_FLOAT) {
        printf(" (float64: %g)", token->literal.float_val);
    } else if (token->type == TOKEN_STRING && token->literal.string) {
        printf(" (value: \"%s\")", token->literal.string);
    } else if (token->type == TOKEN_CHAR) {
        printf(" (value: '%c')", token->literal.character);
    }
    
    printf("\n");
}

Token make_token(TokenType type, const char* start, int length, int line, int column, StringInternPool* pool) {
    Token token;
    memset(&token, 0, sizeof(Token));
    token.type = type;
    token.length = length;
    token.line = line;
    token.column = column;
    
    if (type == TOKEN_IDENTIFIER && start && length > 0) {
        if (pool) {
            MobiusString* interned = pool->intern(start, length);
            token.interned = interned;
            token.identifier = interned->data;
        } else {
            token.interned = nullptr;
            char* id_copy = (char*)malloc(length + 1);
            if (id_copy) {
                memcpy(id_copy, start, length);
                id_copy[length] = '\0';
                token.identifier = id_copy;
            }
        }
    }
    
    return token;
}

Token make_error_token(const char* message, int line, int column) {
    Token token;
    memset(&token, 0, sizeof(Token));
    token.type = TOKEN_ERROR;
    token.length = (int)strlen(message);
    token.line = line;
    token.column = column;
    return token;
}

Token make_integer_token(const char* start, int length, int line, int column, 
                        NumberType num_type, int64_t value) {
    (void)start;
    Token token;
    memset(&token, 0, sizeof(Token));
    token.type = TOKEN_INTEGER;
    token.length = length;
    token.line = line;
    token.column = column;
    token.literal.integer.num_type = num_type;
    token.literal.integer.value = value;
    return token;
}

Token make_float_token(const char* start, int length, int line, int column, double value) {
    (void)start;
    Token token;
    memset(&token, 0, sizeof(Token));
    token.type = TOKEN_FLOAT;
    token.length = length;
    token.line = line;
    token.column = column;
    token.literal.float_val = value;
    return token;
}

Token make_string_token(const char* start, int length, int line, int column, const char* string) {
    (void)start;
    Token token;
    memset(&token, 0, sizeof(Token));
    token.type = TOKEN_STRING;
    token.length = length;
    token.line = line;
    token.column = column;
    token.literal.string = string;
    return token;
}

Token make_char_token(const char* start, int length, int line, int column, char character) {
    (void)start;
    Token token;
    memset(&token, 0, sizeof(Token));
    token.type = TOKEN_CHAR;
    token.length = length;
    token.line = line;
    token.column = column;
    token.literal.character = character;
    return token;
}

// Extract identifier name from token (for IDENTIFIER tokens only)
char* extract_identifier_name(const Token* token) {
    if (!token || token->type != TOKEN_IDENTIFIER || !token->identifier) {
        return NULL;
    }
    
    // Return a copy of the already-extracted identifier string
    return mobius_strdup(token->identifier);
}

Token copy_token(const Token* token) {
    if (!token) {
        Token empty;
        memset(&empty, 0, sizeof(Token));
        return empty;
    }
    
    Token copy = *token;
    
    // Interned identifiers are shared pointers — just copy the pointer.
    // Only duplicate non-interned identifier strings.
    if (token->identifier && !token->interned) {
        copy.identifier = mobius_strdup(token->identifier);
    }
    
    if (token->type == TOKEN_STRING && token->literal.string) {
        copy.literal.string = mobius_strdup(token->literal.string);
    }
    
    return copy;
}

void free_token(Token* token) {
    if (!token) return;
    
    // Only free non-interned identifier strings (interned strings are owned by the pool)
    if (token->identifier && !token->interned) {
        free((void*)token->identifier);
    }
    token->identifier = nullptr;
    token->interned = nullptr;
    
    // Free the copied string literal
    if (token->type == TOKEN_STRING && token->literal.string) {
        free((void*)token->literal.string);
        token->literal.string = nullptr;
    }
}
