#ifndef MOBIUS_TOKEN_H
#define MOBIUS_TOKEN_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// Token types enumeration
typedef enum {
    // Single-character tokens
    TOKEN_LEFT_PAREN,       // (
    TOKEN_RIGHT_PAREN,      // )
    TOKEN_LEFT_BRACKET,     // [
    TOKEN_RIGHT_BRACKET,    // ]
    TOKEN_LEFT_BRACE,       // {
    TOKEN_RIGHT_BRACE,      // }
    TOKEN_COMMA,            // ,
    TOKEN_DOT,              // .
    TOKEN_MINUS,            // -
    TOKEN_PLUS,             // +
    TOKEN_SEMICOLON,        // ;
    TOKEN_COLON,            // :
    TOKEN_SLASH,            // /
    TOKEN_STAR,             // *
    TOKEN_PERCENT,          // %
    TOKEN_QUESTION,         // ?
    TOKEN_AMPERSAND,        // &
    TOKEN_PIPE,             // |
    TOKEN_CARET,            // ^
    TOKEN_TILDE,            // ~

    // One or two character tokens
    TOKEN_BANG,             // !
    TOKEN_BANG_EQUAL,       // !=
    TOKEN_EQUAL,            // =
    TOKEN_EQUAL_EQUAL,      // ==
    TOKEN_GREATER,          // >
    TOKEN_GREATER_EQUAL,    // >=
    TOKEN_LESS,             // <
    TOKEN_LESS_EQUAL,       // <=
    TOKEN_PLUS_PLUS,        // ++
    TOKEN_MINUS_MINUS,      // --
    TOKEN_PLUS_EQUAL,       // +=
    TOKEN_MINUS_EQUAL,      // -=
    TOKEN_STAR_EQUAL,       // *=
    TOKEN_SLASH_EQUAL,      // /=
    TOKEN_AND_AND,          // &&
    TOKEN_OR_OR,            // ||
    TOKEN_LEFT_SHIFT,       // <<
    TOKEN_RIGHT_SHIFT,      // >>
    TOKEN_ARROW,            // ->
    TOKEN_DOT_DOT,          // .. (range operator)
    TOKEN_DOT_DOT_DOT,      // ... (rest operator)

    // Literals
    TOKEN_IDENTIFIER,       // variable names, function names, etc.
    TOKEN_STRING,           // "string literals"
    TOKEN_INTEGER,          // integer literals (8, 16, 32, 64 bit signed/unsigned)
    TOKEN_FLOAT,            // floating point literals (64-bit double)
    TOKEN_CHAR,             // 'c'

    // Keywords
    TOKEN_AND,              // and (logical and)
    TOKEN_BREAK,            // break
    TOKEN_CASE,             // case
    TOKEN_CONTINUE,         // continue
    TOKEN_DEFAULT,          // default
    TOKEN_ELSE,             // else
    TOKEN_ELIF,             // elif (else if)
    TOKEN_ENUM,             // enum
    TOKEN_FALSE,            // false
    TOKEN_FOR,              // for
    TOKEN_FUNC,             // func (function definition)
    TOKEN_IF,               // if
    TOKEN_IMPORT,           // import
    TOKEN_IS,               // is (identity comparison and type matching)
    TOKEN_NIL,              // nil (null value)
    TOKEN_NOT,              // not (logical not)
    TOKEN_OR,               // or (logical or)
    TOKEN_RETURN,           // return
    TOKEN_SWITCH,           // switch
    TOKEN_TRUE,             // true
    TOKEN_VAR,              // var (mutable variable)
    TOKEN_WHILE,            // while

    // Type annotation tokens
    TOKEN_TYPE_INT8,        // int8
    TOKEN_TYPE_INT16,       // int16
    TOKEN_TYPE_INT32,       // int32
    TOKEN_TYPE_INT64,       // int64
    TOKEN_TYPE_UINT8,       // uint8
    TOKEN_TYPE_UINT16,      // uint16
    TOKEN_TYPE_UINT32,      // uint32
    TOKEN_TYPE_UINT64,      // uint64
    TOKEN_TYPE_FLOAT32,     // float32
    TOKEN_TYPE_FLOAT64,     // float64

    // Special tokens
    TOKEN_NEWLINE,          // \n (significant in some contexts)
    TOKEN_EOF,              // End of file
    TOKEN_ERROR             // Error token for invalid input
} TokenType;

// Numeric literal types
typedef enum {
    NUM_INT8,     // int8_t
    NUM_UINT8,    // uint8_t
    NUM_INT16,    // int16_t
    NUM_UINT16,   // uint16_t
    NUM_INT32,    // int32_t
    NUM_UINT32,   // uint32_t
    NUM_INT64,    // int64_t
    NUM_UINT64,   // uint64_t
    NUM_FLOAT32,  // float (32-bit float)
    NUM_FLOAT64   // double (64-bit float)
} NumericType;

// Token structure containing all token information
typedef struct {
    TokenType type;         // The type of the token
    const char* identifier; // Copied identifier string (only for IDENTIFIER tokens, NULL otherwise)
    int length;             // Length of the token string (for compatibility)
    int line;               // Line number where token appears
    int column;             // Column number where token starts
    
    // Literal value (for tokens that have computed values)
    union {
        // Integer values: single value + type metadata (preserves precision)
        struct {
            NumericType num_type;   // Which integer type this represents
            int64_t value;          // Always use largest signed type for storage
        } integer;
        
        double float_val;           // For floating point literals (always 64-bit)
        const char* string;         // For string literals (null-terminated copy)
        char character;             // For character literals
    } literal;
} Token;

// Token utility functions (to be implemented later)
const char* token_type_name(TokenType type);
const char* numeric_type_name(NumericType type);
void print_token(const Token* token);
Token make_token(TokenType type, const char* start, int length, int line, int column);
Token make_error_token(const char* message, int line, int column);

// Numeric token creation helpers
Token make_integer_token(const char* start, int length, int line, int column, 
                        NumericType num_type, int64_t value);
Token make_float_token(const char* start, int length, int line, int column, double value);
Token make_string_token(const char* start, int length, int line, int column, const char* string);
Token make_char_token(const char* start, int length, int line, int column, char character);

// Token utility functions for memory management
char* extract_identifier_name(const Token* token);
Token copy_token(const Token* token);
void free_token(Token* token);

#endif // MOBIUS_TOKEN_H
