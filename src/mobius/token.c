#define _POSIX_C_SOURCE 200809L  // For strdup
#include "token.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Array of token type names for debugging and error messages
static const char* token_names[] = {
    // Single-character tokens
    "LEFT_PAREN", "RIGHT_PAREN", "LEFT_BRACKET", "RIGHT_BRACKET",
    "LEFT_BRACE", "RIGHT_BRACE", "COMMA", "DOT", "MINUS", "PLUS",
    "SEMICOLON", "COLON", "SLASH", "STAR", "PERCENT", "QUESTION",
    "AMPERSAND", "PIPE", "CARET", "TILDE",
    
    // One or two character tokens
    "BANG", "BANG_EQUAL", "EQUAL", "EQUAL_EQUAL", "GREATER", "GREATER_EQUAL",
    "LESS", "LESS_EQUAL", "PLUS_PLUS", "MINUS_MINUS", "PLUS_EQUAL",
    "MINUS_EQUAL", "STAR_EQUAL", "SLASH_EQUAL", "AND_AND", "OR_OR",
    "LEFT_SHIFT", "RIGHT_SHIFT", "ARROW",
    
    // Literals
    "IDENTIFIER", "STRING", "INTEGER", "FLOAT", "CHAR",
    
    // Keywords
    "AND", "BREAK", "CASE", "CLASS", "CONST", "CONTINUE", "DEFAULT", "DO",
    "ELSE", "ELIF", "FALSE", "FOR", "FUNC", "IF", "IMPORT", "IN", "IS",
    "LET", "MATCH", "NIL", "NOT", "OR", "RETURN", "STATIC", "SUPER",
    "SWITCH", "THIS", "TRUE", "TRY", "CATCH", "FINALLY", "THROW",
    "VAR", "WHEN", "WHILE", "WITH", "YIELD",
    
    // Special tokens
    "NEWLINE", "EOF", "ERROR"
};

const char* token_type_name(TokenType type) {
    if (type >= 0 && type < sizeof(token_names) / sizeof(token_names[0])) {
        return token_names[type];
    }
    return "UNKNOWN";
}

const char* numeric_type_name(NumericType type) {
    static const char* numeric_names[] = {
        "int8", "uint8", "int16", "uint16", 
        "int32", "uint32", "int64", "uint64", "float64"
    };
    
    if (type >= 0 && type < sizeof(numeric_names) / sizeof(numeric_names[0])) {
        return numeric_names[type];
    }
    return "UNKNOWN_NUMERIC";
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
        // Print the value with appropriate format based on type
        int64_t value = token->literal.integer.value;
        switch (token->literal.integer.num_type) {
            case NUM_INT8:   printf("%d", (int8_t)value); break;
            case NUM_UINT8:  printf("%u", (uint8_t)value); break;
            case NUM_INT16:  printf("%d", (int16_t)value); break;
            case NUM_UINT16: printf("%u", (uint16_t)value); break;
            case NUM_INT32:  printf("%d", (int32_t)value); break;
            case NUM_UINT32: printf("%u", (uint32_t)value); break;
            case NUM_INT64:  printf("%ld", value); break;
            case NUM_UINT64: printf("%lu", (uint64_t)value); break;
            default: printf("unknown"); break;
        }
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

Token make_token(TokenType type, const char* start, int length, int line, int column) {
    Token token = {
        .type = type,
        .identifier = NULL,  // Will be set only for IDENTIFIER tokens
        .length = length,
        .line = line,
        .column = column,
        .literal = {{0}} // Initialize union to zero
    };
    
    // Copy identifier string only for IDENTIFIER tokens
    if (type == TOKEN_IDENTIFIER && start && length > 0) {
        char* id_copy = malloc(length + 1);
        if (id_copy) {
            memcpy(id_copy, start, length);
            id_copy[length] = '\0';
            token.identifier = id_copy;
        }
    }
    
    return token;
}

Token make_error_token(const char* message, int line, int column) {
    Token token = {
        .type = TOKEN_ERROR,
        .identifier = NULL,  // Error tokens don't need identifier copying
        .length = (int)strlen(message),
        .line = line,
        .column = column,
        .literal = {{0}}
    };
    // Note: Error message is typically a string literal, so we don't copy it
    return token;
}

Token make_integer_token(const char* start, int length, int line, int column, 
                        NumericType num_type, int64_t value) {
    Token token = {
        .type = TOKEN_INTEGER,
        .identifier = NULL,  // Integer tokens don't need identifier copying
        .length = length,
        .line = line,
        .column = column,
        .literal = {{0}}
    };
    
    // Store type and value directly - no complex union needed
    token.literal.integer.num_type = num_type;
    token.literal.integer.value = value;
    
    return token;
}

Token make_float_token(const char* start, int length, int line, int column, double value) {
    Token token = {
        .type = TOKEN_FLOAT,
        .identifier = NULL,  // Float tokens don't need identifier copying
        .length = length,
        .line = line,
        .column = column,
        .literal = {{0}}
    };
    
    token.literal.float_val = value;
    return token;
}

Token make_string_token(const char* start, int length, int line, int column, const char* string) {
    Token token = {
        .type = TOKEN_STRING,
        .identifier = NULL,  // String tokens don't need identifier copying (use literal.string instead)
        .length = length,
        .line = line,
        .column = column,
        .literal = {{0}}
    };
    
    token.literal.string = string;
    return token;
}

Token make_char_token(const char* start, int length, int line, int column, char character) {
    Token token = {
        .type = TOKEN_CHAR,
        .identifier = NULL,  // Char tokens don't need identifier copying
        .length = length,
        .line = line,
        .column = column,
        .literal = {{0}}
    };
    
    token.literal.character = character;
    return token;
}

// Extract identifier name from token (for IDENTIFIER tokens only)
char* extract_identifier_name(const Token* token) {
    if (!token || token->type != TOKEN_IDENTIFIER || !token->identifier) {
        return NULL;
    }
    
    // Return a copy of the already-extracted identifier string
    return strdup(token->identifier);
}

// Token copying functions for memory management (simplified - only copy what's needed)
Token copy_token(const Token* token) {
    if (!token) {
        Token empty = {0};
        return empty;
    }
    
    Token copy = *token;  // Shallow copy first
    
    // Copy identifier string if it exists
    if (token->identifier) {
        copy.identifier = strdup(token->identifier);
    }
    
    // Copy string literal if it exists
    if (token->type == TOKEN_STRING && token->literal.string) {
        copy.literal.string = strdup(token->literal.string);
    }
    
    return copy;
}

void free_token(Token* token) {
    if (!token) return;
    
    // Free the copied identifier string
    if (token->identifier) {
        free((void*)token->identifier);
        token->identifier = NULL;
    }
    
    // Free the copied string literal
    if (token->type == TOKEN_STRING && token->literal.string) {
        free((void*)token->literal.string);
        token->literal.string = NULL;
    }
}
