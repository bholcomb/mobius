#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

// Parser initialization
void init_parser(Parser* parser, Token* tokens, size_t token_count) {
    parser->tokens = tokens;
    parser->token_count = token_count;
    parser->current = 0;
    parser->had_error = false;
    parser->panic_mode = false;
}

// Parser state functions
bool parser_at_end(Parser* parser) {
    return parser->current >= parser->token_count || 
           parser->tokens[parser->current].type == TOKEN_EOF;
}

Token parser_peek(Parser* parser) {
    if (parser->current >= parser->token_count) {
        // Return EOF token if we're past the end
        Token eof = {TOKEN_EOF, "", 0, 0, 0, {0}};
        return eof;
    }
    return parser->tokens[parser->current];
}

Token parser_previous(Parser* parser) {
    if (parser->current == 0) {
        return parser->tokens[0];
    }
    return parser->tokens[parser->current - 1];
}

Token parser_advance(Parser* parser) {
    if (!parser_at_end(parser)) {
        parser->current++;
    }
    return parser_previous(parser);
}

bool parser_check(Parser* parser, TokenType type) {
    if (parser_at_end(parser)) return false;
    return parser_peek(parser).type == type;
}

bool parser_match(Parser* parser, TokenType type) {
    if (parser_check(parser, type)) {
        parser_advance(parser);
        return true;
    }
    return false;
}

bool parser_match_any(Parser* parser, size_t count, ...) {
    va_list args;
    va_start(args, count);
    
    for (size_t i = 0; i < count; i++) {
        TokenType type = va_arg(args, TokenType);
        if (parser_check(parser, type)) {
            parser_advance(parser);
            va_end(args);
            return true;
        }
    }
    
    va_end(args);
    return false;
}

// Error handling
void parser_error(Parser* parser, Token token, const char* message) {
    if (parser->panic_mode) return;
    
    parser->panic_mode = true;
    parser->had_error = true;
    
    fprintf(stderr, "[line %d] Error", token.line);
    
    if (token.type == TOKEN_EOF) {
        fprintf(stderr, " at end");
    } else if (token.type == TOKEN_ERROR) {
        // Nothing
    } else {
        fprintf(stderr, " at '%.*s'", token.length, token.start);
    }
    
    fprintf(stderr, ": %s\n", message);
}

void parser_error_at_current(Parser* parser, const char* message) {
    parser_error(parser, parser_peek(parser), message);
}

Token consume(Parser* parser, TokenType type, const char* message) {
    if (parser_check(parser, type)) {
        return parser_advance(parser);
    }
    
    parser_error_at_current(parser, message);
    Token error_token = {TOKEN_ERROR, message, (int)strlen(message), 0, 0, {0}};
    return error_token;
}

void synchronize(Parser* parser) {
    parser->panic_mode = false;
    
    while (!parser_at_end(parser)) {
        if (parser_previous(parser).type == TOKEN_SEMICOLON) return;
        
        // Skip newlines during synchronization
        if (parser_match(parser, TOKEN_NEWLINE)) continue;
        
        switch (parser_peek(parser).type) {
            case TOKEN_CLASS:
            case TOKEN_FUNC:
            case TOKEN_VAR:
            case TOKEN_FOR:
            case TOKEN_IF:
            case TOKEN_WHILE:
            case TOKEN_RETURN:
                return;
            default:
                break;
        }
        
        parser_advance(parser);
    }
}

// Expression parsing (recursive descent)
Expr* parse_primary(Parser* parser) {
    if (parser_match(parser, TOKEN_TRUE)) {
        return make_literal_expr(make_bool_value(true));
    }
    
    if (parser_match(parser, TOKEN_FALSE)) {
        return make_literal_expr(make_bool_value(false));
    }
    
    if (parser_match(parser, TOKEN_NIL)) {
        return make_literal_expr(make_nil_value());
    }
    
    if (parser_match(parser, TOKEN_INTEGER)) {
        Token token = parser_previous(parser);
        // Extract the value from the token's literal field
        NumericType num_type = token.literal.integer.num_type;
        int64_t int_value = 0;
        
        switch (num_type) {
            case NUM_INT8:   int_value = token.literal.integer.value.i8; break;
            case NUM_UINT8:  int_value = token.literal.integer.value.u8; break;
            case NUM_INT16:  int_value = token.literal.integer.value.i16; break;
            case NUM_UINT16: int_value = token.literal.integer.value.u16; break;
            case NUM_INT32:  int_value = token.literal.integer.value.i32; break;
            case NUM_UINT32: int_value = token.literal.integer.value.u32; break;
            case NUM_INT64:  int_value = token.literal.integer.value.i64; break;
            case NUM_UINT64: int_value = (int64_t)token.literal.integer.value.u64; break;
            default: int_value = 0; break;
        }
        
        Value value = make_integer_value(num_type, int_value);
        return make_literal_expr(value);
    }
    
    if (parser_match(parser, TOKEN_FLOAT)) {
        Token token = parser_previous(parser);
        Value value = make_float_value(token.literal.float_val);
        return make_literal_expr(value);
    }
    
    if (parser_match(parser, TOKEN_STRING)) {
        Token token = parser_previous(parser);
        // Create a copy of the string
        char* str_copy = malloc(strlen(token.literal.string) + 1);
        if (str_copy) {
            strcpy(str_copy, token.literal.string);
        }
        Value value = make_string_value(str_copy);
        return make_literal_expr(value);
    }
    
    if (parser_match(parser, TOKEN_CHAR)) {
        Token token = parser_previous(parser);
        Value value = make_char_value(token.literal.character);
        return make_literal_expr(value);
    }
    
    if (parser_match(parser, TOKEN_IDENTIFIER)) {
        return make_variable_expr(parser_previous(parser));
    }
    
    if (parser_match(parser, TOKEN_LEFT_PAREN)) {
        Expr* expr = parse_expression(parser);
        consume(parser, TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
        return make_grouping_expr(expr);
    }
    
    // Skip unexpected newlines and try again
    if (parser_match(parser, TOKEN_NEWLINE)) {
        return parse_primary(parser);
    }
    
    parser_error_at_current(parser, "Expect expression.");
    return NULL;
}

Expr* finish_call(Parser* parser, Expr* callee) {
    Expr** arguments = NULL;
    size_t arg_count = 0;
    size_t arg_capacity = 0;
    
    if (!parser_check(parser, TOKEN_RIGHT_PAREN)) {
        do {
            if (arg_count >= 255) {
                parser_error_at_current(parser, "Can't have more than 255 arguments.");
            }
            
            // Resize arguments array if needed
            if (arg_count >= arg_capacity) {
                arg_capacity = arg_capacity == 0 ? 8 : arg_capacity * 2;
                arguments = realloc(arguments, arg_capacity * sizeof(Expr*));
            }
            
            arguments[arg_count++] = parse_expression(parser);
        } while (parser_match(parser, TOKEN_COMMA));
    }
    
    Token paren = consume(parser, TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
    return make_call_expr(callee, paren, arguments, arg_count);
}

Expr* parse_call(Parser* parser) {
    Expr* expr = parse_primary(parser);
    
    while (true) {
        if (parser_match(parser, TOKEN_LEFT_PAREN)) {
            expr = finish_call(parser, expr);
        } else {
            break;
        }
    }
    
    return expr;
}

Expr* parse_unary(Parser* parser) {
    if (parser_match_any(parser, 3, TOKEN_BANG, TOKEN_MINUS, TOKEN_NOT)) {
        Token operator = parser_previous(parser);
        Expr* right = parse_unary(parser);
        return make_unary_expr(operator, right);
    }
    
    return parse_call(parser);
}

Expr* parse_factor(Parser* parser) {
    Expr* expr = parse_unary(parser);
    
    while (parser_match_any(parser, 2, TOKEN_SLASH, TOKEN_STAR)) {
        Token operator = parser_previous(parser);
        Expr* right = parse_unary(parser);
        expr = make_binary_expr(expr, operator, right);
    }
    
    return expr;
}

Expr* parse_term(Parser* parser) {
    Expr* expr = parse_factor(parser);
    
    while (parser_match_any(parser, 2, TOKEN_MINUS, TOKEN_PLUS)) {
        Token operator = parser_previous(parser);
        Expr* right = parse_factor(parser);
        expr = make_binary_expr(expr, operator, right);
    }
    
    return expr;
}

Expr* parse_comparison(Parser* parser) {
    Expr* expr = parse_term(parser);
    
    while (parser_match_any(parser, 4, TOKEN_GREATER, TOKEN_GREATER_EQUAL, 
                           TOKEN_LESS, TOKEN_LESS_EQUAL)) {
        Token operator = parser_previous(parser);
        Expr* right = parse_term(parser);
        expr = make_binary_expr(expr, operator, right);
    }
    
    return expr;
}

Expr* parse_equality(Parser* parser) {
    Expr* expr = parse_comparison(parser);
    
    while (parser_match_any(parser, 2, TOKEN_BANG_EQUAL, TOKEN_EQUAL_EQUAL)) {
        Token operator = parser_previous(parser);
        Expr* right = parse_comparison(parser);
        expr = make_binary_expr(expr, operator, right);
    }
    
    return expr;
}

Expr* parse_and(Parser* parser) {
    Expr* expr = parse_equality(parser);
    
    while (parser_match_any(parser, 2, TOKEN_AND, TOKEN_AND_AND)) {
        Token operator = parser_previous(parser);
        Expr* right = parse_equality(parser);
        expr = make_binary_expr(expr, operator, right);
    }
    
    return expr;
}

Expr* parse_or(Parser* parser) {
    Expr* expr = parse_and(parser);
    
    while (parser_match_any(parser, 2, TOKEN_OR, TOKEN_OR_OR)) {
        Token operator = parser_previous(parser);
        Expr* right = parse_and(parser);
        expr = make_binary_expr(expr, operator, right);
    }
    
    return expr;
}

Expr* parse_assignment(Parser* parser) {
    Expr* expr = parse_or(parser);
    
    if (parser_match(parser, TOKEN_EQUAL)) {
        Token equals = parser_previous(parser);
        Expr* value = parse_assignment(parser);
        
        if (expr->type == EXPR_VARIABLE) {
            Token name = expr->as.variable.name;
            free_expr(expr);  // Free the variable expression
            return make_assignment_expr(name, value);
        }
        
        parser_error(parser, equals, "Invalid assignment target.");
    }
    
    return expr;
}

Expr* parse_expression(Parser* parser) {
    return parse_assignment(parser);
}

// Statement parsing
Stmt* parse_expression_statement(Parser* parser) {
    Expr* expr = parse_expression(parser);
    consume(parser, TOKEN_SEMICOLON, "Expect ';' after expression.");
    return make_expression_stmt(expr);
}

// Print statement removed - print is now a built-in function

Stmt* parse_var_declaration(Parser* parser) {
    Token name = consume(parser, TOKEN_IDENTIFIER, "Expect variable name.");
    
    Expr* initializer = NULL;
    if (parser_match(parser, TOKEN_EQUAL)) {
        initializer = parse_expression(parser);
    }
    
    consume(parser, TOKEN_SEMICOLON, "Expect ';' after variable declaration.");
    return make_var_stmt(name, initializer);
}

Stmt* parse_block_statement(Parser* parser) {
    Stmt** statements = NULL;
    size_t count = 0;
    size_t capacity = 0;
    
    while (!parser_check(parser, TOKEN_RIGHT_BRACE) && !parser_at_end(parser)) {
        // Skip newlines in blocks
        if (parser_match(parser, TOKEN_NEWLINE)) {
            continue;
        }
        
        Stmt* stmt = parse_declaration(parser);
        if (stmt) {
            // Resize statements array if needed
            if (count >= capacity) {
                capacity = capacity == 0 ? 8 : capacity * 2;
                statements = realloc(statements, capacity * sizeof(Stmt*));
            }
            statements[count++] = stmt;
        }
        
        if (parser->panic_mode) {
            synchronize(parser);
        }
    }
    
    consume(parser, TOKEN_RIGHT_BRACE, "Expect '}' after block.");
    return make_block_stmt(statements, count);
}

Stmt* parse_if_statement(Parser* parser) {
    consume(parser, TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
    Expr* condition = parse_expression(parser);
    consume(parser, TOKEN_RIGHT_PAREN, "Expect ')' after if condition.");
    
    Stmt* then_branch = parse_statement(parser);
    Stmt* else_branch = NULL;
    if (parser_match(parser, TOKEN_ELSE)) {
        else_branch = parse_statement(parser);
    }
    
    return make_if_stmt(condition, then_branch, else_branch);
}

Stmt* parse_while_statement(Parser* parser) {
    consume(parser, TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
    Expr* condition = parse_expression(parser);
    consume(parser, TOKEN_RIGHT_PAREN, "Expect ')' after condition.");
    Stmt* body = parse_statement(parser);
    
    return make_while_stmt(condition, body);
}

Stmt* parse_for_statement(Parser* parser) {
    consume(parser, TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");
    
    Stmt* initializer = NULL;
    if (parser_match(parser, TOKEN_SEMICOLON)) {
        initializer = NULL;
    } else if (parser_match(parser, TOKEN_VAR)) {
        initializer = parse_var_declaration(parser);
    } else {
        initializer = parse_expression_statement(parser);
    }
    
    Expr* condition = NULL;
    if (!parser_check(parser, TOKEN_SEMICOLON)) {
        condition = parse_expression(parser);
    }
    consume(parser, TOKEN_SEMICOLON, "Expect ';' after loop condition.");
    
    Expr* increment = NULL;
    if (!parser_check(parser, TOKEN_RIGHT_PAREN)) {
        increment = parse_expression(parser);
    }
    consume(parser, TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");
    
    Stmt* body = parse_statement(parser);
    
    return make_for_stmt(initializer, condition, increment, body);
}

Stmt* parse_statement(Parser* parser) {
    if (parser_match(parser, TOKEN_LEFT_BRACE)) {
        return parse_block_statement(parser);
    }
    
    if (parser_match(parser, TOKEN_IF)) {
        return parse_if_statement(parser);
    }
    
    if (parser_match(parser, TOKEN_WHILE)) {
        return parse_while_statement(parser);
    }
    
    if (parser_match(parser, TOKEN_FOR)) {
        return parse_for_statement(parser);
    }
    
    return parse_expression_statement(parser);
}

Stmt* parse_declaration(Parser* parser) {
    if (parser_match(parser, TOKEN_VAR)) {
        return parse_var_declaration(parser);
    }
    
    return parse_statement(parser);
}

// Main parsing function
ParseResult parse(TokenArray tokens) {
    ParseResult result = {0};
    
    Parser parser;
    init_parser(&parser, tokens.tokens, tokens.count);
    
    Stmt** statements = NULL;
    size_t count = 0;
    size_t capacity = 0;
    
    while (!parser_at_end(&parser)) {
        // Skip newlines at the top level
        if (parser_match(&parser, TOKEN_NEWLINE)) {
            continue;
        }
        
        // Resize statements array if needed
        if (count >= capacity) {
            capacity = capacity == 0 ? 8 : capacity * 2;
            statements = realloc(statements, capacity * sizeof(Stmt*));
        }
        
        Stmt* stmt = parse_declaration(&parser);
        if (stmt) {
            statements[count++] = stmt;
        }
        
        if (parser.panic_mode) {
            synchronize(&parser);
        }
    }
    
    result.statements = statements;
    result.count = count;
    result.had_error = parser.had_error;
    
    return result;
}

void free_parse_result(ParseResult* result) {
    if (result->statements) {
        for (size_t i = 0; i < result->count; i++) {
            free_stmt(result->statements[i]);
        }
        free(result->statements);
        result->statements = NULL;
    }
    result->count = 0;
}
