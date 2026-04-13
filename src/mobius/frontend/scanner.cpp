#include "frontend/scanner.h"
#include "data/value.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

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
    {"throw",    TOKEN_THROW},
    {"try",      TOKEN_TRY},
    {"catch",    TOKEN_CATCH},
    {"in",       TOKEN_IN},
    {"var",      TOKEN_VAR},
    {"when",     TOKEN_WHEN},
    {"while",    TOKEN_WHILE},
    {"finally",  TOKEN_FINALLY},
    {"spawn",    TOKEN_SPAWN},
    {"await",    TOKEN_AWAIT},
    {"yield",    TOKEN_YIELD},
    {"shared",   TOKEN_SHARED},
    {"atomic",   TOKEN_ATOMIC},
    {"struct",   TOKEN_STRUCT},
    {"union",    TOKEN_UNION},
    {"at",       TOKEN_AT},
    
    // Type keywords
    {"int64",    TOKEN_TYPE_INT64},
    {"uint64",   TOKEN_TYPE_UINT64},
    {"float64",  TOKEN_TYPE_FLOAT64},
};

static const size_t keyword_count = sizeof(keywords) / sizeof(keywords[0]);

// Initialize scanner state
void init_scanner(Scanner* scanner, const char* source, StringInternPool* pool) {
    scanner->start = source;
    scanner->current = source;
    scanner->source = source;
    scanner->line = 1;
    scanner->column = 1;
    scanner->pool = pool;
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
                     scanner->column - (int)(scanner->current - scanner->start),
                     scanner->pool);
}

// Process escape sequences in a raw source range, writing the decoded bytes
// into `out`. Returns the number of bytes written.
static size_t process_escapes(const char* src, size_t src_len, char* out) {
    size_t w = 0;
    for (size_t i = 0; i < src_len; i++) {
        if (src[i] == '\\' && i + 1 < src_len) {
            i++;
            switch (src[i]) {
                case 'n':  out[w++] = '\n'; break;
                case 't':  out[w++] = '\t'; break;
                case 'r':  out[w++] = '\r'; break;
                case '\\': out[w++] = '\\'; break;
                case '"':  out[w++] = '"';  break;
                case '\'': out[w++] = '\''; break;
                case '0':  out[w++] = '\0'; break;
                default:
                    out[w++] = '\\';
                    out[w++] = src[i];
                    break;
            }
        } else {
            out[w++] = src[i];
        }
    }
    return w;
}

static char peek_offset(Scanner* scanner, size_t offset) {
    for (size_t i = 0; i <= offset; i++) {
        char c = scanner->current[i];
        if (c == '\0') return '\0';
        if (i == offset) return c;
    }
    return '\0';
}

static Token make_decoded_string_token(Scanner* scanner, const char* raw, size_t raw_len) {
    Token token = make_simple_token(scanner, TOKEN_STRING);

    char* string_content = (char*)malloc(raw_len + 1);
    if (string_content) {
        size_t decoded_len = process_escapes(raw, raw_len, string_content);
        string_content[decoded_len] = '\0';
        token.literal.string = string_content;
    } else {
        token.literal.string = NULL;
    }

    return token;
}

// Scan string literal
Token scan_string(Scanner* scanner) {
    bool multiline = peek(scanner) == '"' && peek_next(scanner) == '"';
    if (multiline) {
        advance(scanner);
        advance(scanner);
    }

    while (!is_at_end(scanner)) {
        if (peek(scanner) == '\\' && !is_at_end(scanner)) {
            advance(scanner); // skip the backslash
            if (!is_at_end(scanner)) advance(scanner); // skip the escaped char
            continue;
        }
        if (multiline) {
            if (peek(scanner) == '"' &&
                peek_offset(scanner, 1) == '"' &&
                peek_offset(scanner, 2) == '"') {
                break;
            }
        } else if (peek(scanner) == '"') {
            break;
        }
        advance(scanner);
    }

    if (is_at_end(scanner)) {
        return make_error_token(multiline ? "Unterminated multiline string" : "Unterminated string",
                                scanner->line, scanner->column);
    }

    const char* raw = nullptr;
    size_t raw_len = 0;
    if (multiline) {
        advance(scanner);
        advance(scanner);
        advance(scanner);
        raw = scanner->start + 3;
        raw_len = (size_t)(scanner->current - scanner->start - 6);
    } else {
        advance(scanner);
        raw = scanner->start + 1;
        raw_len = (size_t)(scanner->current - scanner->start - 2);
    }

    return make_decoded_string_token(scanner, raw, raw_len);
}

// Scan character literal
Token scan_char(Scanner* scanner) {
    if (is_at_end(scanner)) {
        return make_error_token("Unterminated character", scanner->line, scanner->column);
    }
    
    char character = advance(scanner);
    if (character == '\\' && !is_at_end(scanner)) {
        char esc = advance(scanner);
        switch (esc) {
            case 'n':  character = '\n'; break;
            case 't':  character = '\t'; break;
            case 'r':  character = '\r'; break;
            case '\\': character = '\\'; break;
            case '\'': character = '\''; break;
            case '"':  character = '"';  break;
            case '0':  character = '\0'; break;
            default:   character = esc;  break;
        }
    }
    
    if (peek(scanner) != '\'') {
        return make_error_token("Unterminated character", scanner->line, scanner->column);
    }
    
    advance(scanner); // consume closing quote
    
    Token token = make_simple_token(scanner, TOKEN_CHAR);
    token.literal.character = character;
    return token;
}

static bool is_hex_digit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static bool is_binary_digit(char c) {
    return c == '0' || c == '1';
}

// Scan number literal
Token scan_number(Scanner* scanner) {
    auto make_numeric_error = [&](const char* message) {
        return make_error_token(message,
                                scanner->line,
                                scanner->column - (int)(scanner->current - scanner->start));
    };
    auto copy_numeric_lexeme = [&]() -> char* {
        size_t len = (size_t)(scanner->current - scanner->start);
        char* buf = (char*)malloc(len + 1);
        if (!buf) return nullptr;
        memcpy(buf, scanner->start, len);
        buf[len] = '\0';
        return buf;
    };
    auto make_integer_literal_token = [&](char* literal, int base, size_t prefix_offset,
                                          const char* error_message) {
        errno = 0;
        char* endptr = nullptr;
        unsigned long long raw = strtoull(literal + prefix_offset, &endptr, base);
        bool ok = endptr && *endptr == '\0' && endptr != literal + prefix_offset && errno != ERANGE;
        if (!ok) {
            free(literal);
            return make_numeric_error(error_message);
        }
        NumberType num_type = (raw > (unsigned long long)INT64_MAX) ? NUM_UINT64 : NUM_INT64;
        int64_t stored = (int64_t)(uint64_t)raw;
        free(literal);
        return make_integer_token(scanner->start,
                                  (int)(scanner->current - scanner->start),
                                  scanner->line,
                                  scanner->column - (int)(scanner->current - scanner->start),
                                  num_type, stored);
    };

    // Check for hex (0x/0X) or binary (0b/0B) prefix
    if (peek(scanner) == '0') {
        char next = peek_next(scanner);
        if (next == 'x' || next == 'X') {
            advance(scanner); // consume '0'
            advance(scanner); // consume 'x'
            while (is_hex_digit(peek(scanner))) {
                advance(scanner);
            }
            char* literal = copy_numeric_lexeme();
            if (!literal) return make_numeric_error("Out of memory parsing number");
            return make_integer_literal_token(literal, 16, 0, "Invalid hexadecimal integer literal");
        }
        if (next == 'b' || next == 'B') {
            advance(scanner); // consume '0'
            advance(scanner); // consume 'b'
            while (is_binary_digit(peek(scanner))) {
                advance(scanner);
            }
            char* literal = copy_numeric_lexeme();
            if (!literal) return make_numeric_error("Out of memory parsing number");
            return make_integer_literal_token(literal, 2, 2, "Invalid binary integer literal");
        }
    }

    // Scan decimal integer part
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
        char* literal = copy_numeric_lexeme();
        if (!literal) return make_numeric_error("Out of memory parsing number");
        errno = 0;
        char* endptr = nullptr;
        double value = strtod(literal, &endptr);
        bool ok = endptr && *endptr == '\0' && errno != ERANGE;
        free(literal);
        if (!ok) return make_numeric_error("Invalid floating-point literal");
        return make_float_token(scanner->start, 
                               (int)(scanner->current - scanner->start),
                               scanner->line,
                               scanner->column - (int)(scanner->current - scanner->start),
                               value);
    } else {
        char* literal = copy_numeric_lexeme();
        if (!literal) return make_numeric_error("Out of memory parsing number");
        return make_integer_literal_token(literal, 10, 0, "Invalid integer literal");
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
        case '%':
            if (match(scanner, '=')) {
                return make_simple_token(scanner, TOKEN_PERCENT_EQUAL);
            } else {
                return make_simple_token(scanner, TOKEN_PERCENT);
            }
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

        // Interpolated string literals
        case '`': {
            while (peek(scanner) != '`' && !is_at_end(scanner)) {
                if (peek(scanner) == '\\') {
                    advance(scanner);
                    if (!is_at_end(scanner)) advance(scanner);
                    continue;
                }
                advance(scanner);
            }
            if (is_at_end(scanner)) {
                return make_error_token("Unterminated interpolated string", scanner->line, scanner->column);
            }
            advance(scanner);
            Token token = make_simple_token(scanner, TOKEN_INTERP_STRING);
            const char* raw = scanner->start + 1;
            size_t raw_len = scanner->current - scanner->start - 2;
            char* content = (char*)malloc(raw_len + 1);
            if (content) {
                size_t decoded_len = process_escapes(raw, raw_len, content);
                content[decoded_len] = '\0';
                token.literal.string = content;
            } else {
                token.literal.string = NULL;
            }
            return token;
        }

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
TokenArray scan_source(const char* source, StringInternPool* pool) {
    TokenArray array = {0};
    array.capacity = INITIAL_TOKEN_CAPACITY;
    array.tokens = (Token*)malloc(sizeof(Token) * array.capacity);
    
    if (!array.tokens) {
        array.capacity = 0;
        return array;
    }
    
    Scanner scanner;
    init_scanner(&scanner, source, pool);
    
    while (true) {
        Token token = scan_token(&scanner);
        
        // Resize array if needed
        if (array.count >= array.capacity) {
            array.capacity *= 2;
            Token* new_tokens = (Token*)realloc(array.tokens, sizeof(Token) * array.capacity);
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
        // Free each token (identifiers and string literals)
        for (size_t i = 0; i < array->count; i++) {
            free_token(&array->tokens[i]);
        }
        free(array->tokens);
        array->tokens = NULL;
    }
    array->count = 0;
    array->capacity = 0;
}
