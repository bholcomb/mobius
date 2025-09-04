#include "token.h"
#include <stdio.h>
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
    "TYPEOF", "VAR", "WHEN", "WHILE", "WITH", "YIELD",
    
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
    printf("Token[%s] '%.*s' at line %d, column %d",
           token_type_name(token->type),
           token->length,
           token->start,
           token->line,
           token->column);
    
    // Print literal value if applicable
    if (token->type == TOKEN_INTEGER) {
        printf(" (%s: ", numeric_type_name(token->literal.integer.num_type));
        switch (token->literal.integer.num_type) {
            case NUM_INT8:   printf("%d", token->literal.integer.value.i8); break;
            case NUM_UINT8:  printf("%u", token->literal.integer.value.u8); break;
            case NUM_INT16:  printf("%d", token->literal.integer.value.i16); break;
            case NUM_UINT16: printf("%u", token->literal.integer.value.u16); break;
            case NUM_INT32:  printf("%d", token->literal.integer.value.i32); break;
            case NUM_UINT32: printf("%u", token->literal.integer.value.u32); break;
            case NUM_INT64:  printf("%ld", token->literal.integer.value.i64); break;
            case NUM_UINT64: printf("%lu", token->literal.integer.value.u64); break;
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
        .start = start,
        .length = length,
        .line = line,
        .column = column,
        .literal = {0} // Initialize union to zero
    };
    return token;
}

Token make_error_token(const char* message, int line, int column) {
    Token token = {
        .type = TOKEN_ERROR,
        .start = message,
        .length = (int)strlen(message),
        .line = line,
        .column = column,
        .literal = {0}
    };
    return token;
}

Token make_integer_token(const char* start, int length, int line, int column, 
                        NumericType num_type, int64_t value) {
    Token token = {
        .type = TOKEN_INTEGER,
        .start = start,
        .length = length,
        .line = line,
        .column = column,
        .literal = {0}
    };
    
    token.literal.integer.num_type = num_type;
    switch (num_type) {
        case NUM_INT8:   token.literal.integer.value.i8  = (int8_t)value; break;
        case NUM_UINT8:  token.literal.integer.value.u8  = (uint8_t)value; break;
        case NUM_INT16:  token.literal.integer.value.i16 = (int16_t)value; break;
        case NUM_UINT16: token.literal.integer.value.u16 = (uint16_t)value; break;
        case NUM_INT32:  token.literal.integer.value.i32 = (int32_t)value; break;
        case NUM_UINT32: token.literal.integer.value.u32 = (uint32_t)value; break;
        case NUM_INT64:  token.literal.integer.value.i64 = value; break;
        case NUM_UINT64: token.literal.integer.value.u64 = (uint64_t)value; break;
        default: break;
    }
    
    return token;
}

Token make_float_token(const char* start, int length, int line, int column, double value) {
    Token token = {
        .type = TOKEN_FLOAT,
        .start = start,
        .length = length,
        .line = line,
        .column = column,
        .literal = {0}
    };
    
    token.literal.float_val = value;
    return token;
}

Token make_string_token(const char* start, int length, int line, int column, const char* string) {
    Token token = {
        .type = TOKEN_STRING,
        .start = start,
        .length = length,
        .line = line,
        .column = column,
        .literal = {0}
    };
    
    token.literal.string = string;
    return token;
}

Token make_char_token(const char* start, int length, int line, int column, char character) {
    Token token = {
        .type = TOKEN_CHAR,
        .start = start,
        .length = length,
        .line = line,
        .column = column,
        .literal = {0}
    };
    
    token.literal.character = character;
    return token;
}
