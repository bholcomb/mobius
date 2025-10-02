#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

// Forward declarations
Stmt* parse_continue_statement(Parser* parser);

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
        Token eof = {TOKEN_EOF, "", 0, 0, 0, {{0}}};
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
        fprintf(stderr, " at '%s'", token.identifier ? token.identifier : "unknown");
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
    Token error_token = {TOKEN_ERROR, message, (int)strlen(message), 0, 0, {{0}}};
    return error_token;
}

// Helper function to consume statement terminator (semicolon or newline)
// Returns true if terminator was found, false if error
bool consume_statement_terminator(Parser* parser, const char* message) {
    // If we're at end of file, that's a valid terminator
    if (parser_at_end(parser)) {
        return true;
    }
    
    // If we have a semicolon, consume it
    if (parser_check(parser, TOKEN_SEMICOLON)) {
        parser_advance(parser);
        return true;
    }
    
    // If we have a newline, consume it
    if (parser_check(parser, TOKEN_NEWLINE)) {
        parser_advance(parser);
        return true;
    }
    
    // If we're at a closing brace (end of block), that's also valid
    if (parser_check(parser, TOKEN_RIGHT_BRACE)) {
        return true;
    }
    
    // Check if there's more content on the same line that would require a semicolon
    Token current = parser_peek(parser);
    Token previous = parser_previous(parser);
    
    // If both tokens are on the same line, we need a semicolon
    if (current.line == previous.line && !parser_at_end(parser)) {
        parser_error_at_current(parser, message);
        return false;
    }
    
    // Otherwise, newline is implicit and we're good
    return true;
}

void synchronize(Parser* parser) {
    parser->panic_mode = false;
    
    while (!parser_at_end(parser)) {
        if (parser_previous(parser).type == TOKEN_SEMICOLON) return;
        
        // Skip newlines during synchronization
        if (parser_match(parser, TOKEN_NEWLINE)) continue;
        
        switch (parser_peek(parser).type) {
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
        // With simplified integer representation, value is already stored as int64_t
        NumericType num_type = token.literal.integer.num_type;
        int64_t int_value = token.literal.integer.value;
        
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
        // Create a RefCountedString from the token's string literal
        if (token.literal.string) {
            Value value = make_string_value_from_cstr(token.literal.string);
            return make_literal_expr(value);
        } else {
            Value value = make_string_value_from_cstr("");
            return make_literal_expr(value);
        }
    }
    
    if (parser_match(parser, TOKEN_CHAR)) {
        Token token = parser_previous(parser);
        Value value = make_char_value(token.literal.character);
        return make_literal_expr(value);
    }
    
    if (parser_match(parser, TOKEN_IDENTIFIER)) {
        return make_variable_expr(parser_previous(parser));
    }
    
    if (parser_match(parser, TOKEN_LEFT_BRACE)) {
        return parse_table_literal(parser);
    }
    
    if (parser_match(parser, TOKEN_LEFT_BRACKET)) {
        return parse_array_literal(parser);
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

Expr* parse_table_literal(Parser* parser) {
    TablePair* pairs = NULL;
    size_t pair_count = 0;
    size_t pair_capacity = 0;
    
    // Handle empty table
    if (parser_check(parser, TOKEN_RIGHT_BRACE)) {
        parser_advance(parser);
        return make_table_literal_expr(pairs, pair_count);
    }
    
    do {
        // Skip any newlines before processing the next element
        while (parser_match(parser, TOKEN_NEWLINE)) {
            // Continue skipping newlines
        }
        
        // Check if we've reached the end of the table after skipping newlines
        if (parser_check(parser, TOKEN_RIGHT_BRACE)) {
            break;
        }
        
        // Ensure capacity
        if (pair_count >= pair_capacity) {
            pair_capacity = pair_capacity == 0 ? 4 : pair_capacity * 2;
            TablePair* new_pairs = realloc(pairs, pair_capacity * sizeof(TablePair));
            if (!new_pairs) {
                free(pairs);
                parser_error_at_current(parser, "Memory allocation failed");
                return NULL;
            }
            pairs = new_pairs;
        }
        
        TablePair* current_pair = &pairs[pair_count];
        current_pair->key = NULL;
        current_pair->value = NULL;
        current_pair->is_computed_key = false;
        
        // Check for computed key [expression] = value
        if (parser_match(parser, TOKEN_LEFT_BRACKET)) {
            current_pair->key = parse_expression(parser);
            current_pair->is_computed_key = true;
            consume(parser, TOKEN_RIGHT_BRACKET, "Expect ']' after computed key");
            consume(parser, TOKEN_EQUAL, "Expect '=' after computed key");
            current_pair->value = parse_expression(parser);
        }
        // Check for identifier key: value
        else if (parser_check(parser, TOKEN_IDENTIFIER)) {
            Token key_token = parser_advance(parser);
            if (parser_match(parser, TOKEN_COLON)) {
                // Create a string literal from the identifier
                char* key_str = malloc(key_token.length + 1);
                if (key_str) {
                    const char* key_id = key_token.identifier ? key_token.identifier : "unknown";
                    strncpy(key_str, key_id, strlen(key_id));
                    key_str[key_token.length] = '\0';
                    current_pair->key = make_literal_expr(make_string_value_from_cstr(key_str));
                    free(key_str);
                    current_pair->is_computed_key = false;
                    current_pair->value = parse_expression(parser);
                } else {
                    parser_error_at_current(parser, "Memory allocation failed");
                    free(pairs);
                    return NULL;
                }
            } else {
                // No colon means this is just a value
                parser->current--; // Back up
                current_pair->value = parse_expression(parser);
            }
        }
        // Otherwise it's just a value
        else {
            current_pair->value = parse_expression(parser);
        }
        
        pair_count++;
        
        // Skip any newlines after processing an element
        while (parser_match(parser, TOKEN_NEWLINE)) {
            // Continue skipping newlines
        }
        
    } while (parser_match(parser, TOKEN_COMMA));
    
    // Skip any trailing newlines before the closing brace
    while (parser_match(parser, TOKEN_NEWLINE)) {
        // Continue skipping newlines
    }
    
    consume(parser, TOKEN_RIGHT_BRACE, "Expect '}' after table literal");
    
    return make_table_literal_expr(pairs, pair_count);
}

Expr* parse_array_literal(Parser* parser) {
    Expr** elements = NULL;
    size_t element_count = 0;
    size_t element_capacity = 0;
    
    // Handle empty array []
    if (parser_check(parser, TOKEN_RIGHT_BRACKET)) {
        parser_advance(parser);
        return make_array_literal_expr(elements, element_count);
    }
    
    do {
        // Skip any newlines before processing the next element
        while (parser_match(parser, TOKEN_NEWLINE)) {
            // Continue to skip newlines
        }
        
        // Check for end of array after skipping newlines
        if (parser_check(parser, TOKEN_RIGHT_BRACKET)) {
            break;
        }
        
        // Resize elements array if needed
        if (element_count >= element_capacity) {
            element_capacity = element_capacity == 0 ? 8 : element_capacity * 2;
            elements = realloc(elements, element_capacity * sizeof(Expr*));
            if (!elements) {
                parser_error_at_current(parser, "Out of memory parsing array literal");
                return NULL;
            }
        }
        
        // Parse the element expression
        elements[element_count++] = parse_expression(parser);
        
        // Skip any newlines after the element
        while (parser_match(parser, TOKEN_NEWLINE)) {
            // Continue to skip newlines
        }
        
    } while (parser_match(parser, TOKEN_COMMA));
    
    consume(parser, TOKEN_RIGHT_BRACKET, "Expect ']' after array literal");
    
    return make_array_literal_expr(elements, element_count);
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
        } else if (parser_match(parser, TOKEN_LEFT_BRACKET)) {
            Expr* index = parse_expression(parser);
            consume(parser, TOKEN_RIGHT_BRACKET, "Expect ']' after index");
            expr = make_array_index_expr(expr, index);
        } else if (parser_match(parser, TOKEN_DOT)) {
            Token key = consume(parser, TOKEN_IDENTIFIER, "Expect property name after '.'");
            
            // Always treat as table dot access initially
            // The evaluator will handle enum access if the base turns out to be an enum
            expr = make_table_dot_expr(expr, key);
        } 
        // Handle postfix increment/decrement
        else if (parser_match_any(parser, 2, TOKEN_PLUS_PLUS, TOKEN_MINUS_MINUS)) {
            Token op = parser_previous(parser);
            
            // Can only apply to variable expressions
            if (expr->type != EXPR_VARIABLE) {
                parser_error(parser, op, "Postfix operator can only be applied to variables");
                free_expr(expr);
                return NULL;
            }
            
            Token name = expr->as.variable.name;
            bool is_increment = (op.type == TOKEN_PLUS_PLUS);
            free_expr(expr);
            expr = make_increment_expr(name, false, is_increment, op);
        }
        else {
            break;
        }
    }
    
    return expr;
}

Expr* parse_unary(Parser* parser) {
    // Handle prefix increment/decrement
    if (parser_match_any(parser, 2, TOKEN_PLUS_PLUS, TOKEN_MINUS_MINUS)) {
        Token op = parser_previous(parser);
        
        // Expect an identifier
        if (!parser_check(parser, TOKEN_IDENTIFIER)) {
            parser_error_at_current(parser, "Expect variable name after prefix operator");
            return NULL;
        }
        
        Token name = parser_advance(parser);
        bool is_increment = (op.type == TOKEN_PLUS_PLUS);
        return make_increment_expr(name, true, is_increment, op);
    }
    
    // Handle other unary operators
    if (parser_match_any(parser, 4, TOKEN_BANG, TOKEN_MINUS, TOKEN_NOT, TOKEN_PLUS)) {
        Token op = parser_previous(parser);
        Expr* right = parse_unary(parser);
        return make_unary_expr(op, right);
    }
    
    return parse_call(parser);
}

Expr* parse_factor(Parser* parser) {
    Expr* expr = parse_unary(parser);
    
    while (parser_match_any(parser, 3, TOKEN_SLASH, TOKEN_STAR, TOKEN_PERCENT)) {
        Token op = parser_previous(parser);
        Expr* right = parse_unary(parser);
        expr = make_binary_expr(expr, op, right);
    }
    
    return expr;
}

Expr* parse_term(Parser* parser) {
    Expr* expr = parse_factor(parser);
    
    while (parser_match_any(parser, 2, TOKEN_MINUS, TOKEN_PLUS)) {
        Token op = parser_previous(parser);
        Expr* right = parse_factor(parser);
        expr = make_binary_expr(expr, op, right);
    }
    
    return expr;
}

Expr* parse_comparison(Parser* parser) {
    Expr* expr = parse_term(parser);
    
    while (parser_match_any(parser, 4, TOKEN_GREATER, TOKEN_GREATER_EQUAL, 
                           TOKEN_LESS, TOKEN_LESS_EQUAL)) {
        Token op = parser_previous(parser);
        Expr* right = parse_term(parser);
        expr = make_binary_expr(expr, op, right);
    }
    
    return expr;
}

Expr* parse_equality(Parser* parser) {
    Expr* expr = parse_comparison(parser);
    
    while (parser_match_any(parser, 2, TOKEN_BANG_EQUAL, TOKEN_EQUAL_EQUAL)) {
        Token op = parser_previous(parser);
        Expr* right = parse_comparison(parser);
        expr = make_binary_expr(expr, op, right);
    }
    
    return expr;
}

Expr* parse_and(Parser* parser) {
    Expr* expr = parse_equality(parser);
    
    while (parser_match_any(parser, 2, TOKEN_AND, TOKEN_AND_AND)) {
        Token op = parser_previous(parser);
        Expr* right = parse_equality(parser);
        expr = make_binary_expr(expr, op, right);
    }
    
    return expr;
}

Expr* parse_or(Parser* parser) {
    Expr* expr = parse_and(parser);
    
    while (parser_match_any(parser, 2, TOKEN_OR, TOKEN_OR_OR)) {
        Token op = parser_previous(parser);
        Expr* right = parse_and(parser);
        expr = make_binary_expr(expr, op, right);
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
    if (!consume_statement_terminator(parser, "Expect ';' or newline after expression")) {
        // Error already reported by consume_statement_terminator
        free_expr(expr);
        return NULL;
    }
    return make_expression_stmt(expr);
}

// Parse optional type annotation: ": type"
NumberInfo parse_number_type_annotation(Parser* parser) {
    NumberInfo type_info = {NUMBER_TYPE_UNKNOWN, false};
    
    if (parser_match(parser, TOKEN_COLON)) {
        type_info.is_annotated = true;
        
        if (parser_match(parser, TOKEN_TYPE_INT8)) {
            type_info.type = NUMBER_TYPE_INT8;
        } else if (parser_match(parser, TOKEN_TYPE_INT16)) {
            type_info.type = NUMBER_TYPE_INT16;
        } else if (parser_match(parser, TOKEN_TYPE_INT32)) {
            type_info.type = NUMBER_TYPE_INT32;
        } else if (parser_match(parser, TOKEN_TYPE_INT64)) {
            type_info.type = NUMBER_TYPE_INT64;
        } else if (parser_match(parser, TOKEN_TYPE_UINT8)) {
            type_info.type = NUMBER_TYPE_UINT8;
        } else if (parser_match(parser, TOKEN_TYPE_UINT16)) {
            type_info.type = NUMBER_TYPE_UINT16;
        } else if (parser_match(parser, TOKEN_TYPE_UINT32)) {
            type_info.type = NUMBER_TYPE_UINT32;
        } else if (parser_match(parser, TOKEN_TYPE_UINT64)) {
            type_info.type = NUMBER_TYPE_UINT64;
        } else if (parser_match(parser, TOKEN_TYPE_FLOAT32)) {
            type_info.type = NUMBER_TYPE_FLOAT32;
        } else if (parser_match(parser, TOKEN_TYPE_FLOAT64)) {
            type_info.type = NUMBER_TYPE_FLOAT64;
        } else {
            parser_error(parser, parser_peek(parser), "Expected type name after ':'");
            type_info.is_annotated = false;
        }
    }
    
    return type_info;
}

Stmt* parse_var_declaration(Parser* parser) {
    Token name = consume(parser, TOKEN_IDENTIFIER, "Expect variable name.");
    
    // Parse optional type annotation
    NumberInfo type_hint = parse_number_type_annotation(parser);
    
    Expr* initializer = NULL;
    if (parser_match(parser, TOKEN_EQUAL)) {
        initializer = parse_expression(parser);
    }
    
    if (!consume_statement_terminator(parser, "Expect ';' or newline after variable declaration")) {
        // Error already reported, clean up
        if (initializer) free_expr(initializer);
        return NULL;
    }
    return make_var_stmt(name, initializer, type_hint);
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

    while (parser_check(parser, TOKEN_NEWLINE)) {
        parser_advance(parser);
    }
    
    Stmt* then_branch = parse_statement(parser);
    Stmt* else_branch = NULL;
    
    // Handle elif as chained if-else statements
    if (parser_match(parser, TOKEN_ELIF)) {
        // Recursively parse the elif as another if statement
        else_branch = parse_if_statement(parser);
    } else if (parser_match(parser, TOKEN_ELSE)) {
        else_branch = parse_statement(parser);
    }
    
    return make_if_stmt(condition, then_branch, else_branch);
}

Stmt* parse_while_statement(Parser* parser) {
    consume(parser, TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
    Expr* condition = parse_expression(parser);
    consume(parser, TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    while (parser_check(parser, TOKEN_NEWLINE)) {
        parser_advance(parser);
    }

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

    while (parser_check(parser, TOKEN_NEWLINE)) {
        parser_advance(parser);
    }
    
    Stmt* body = parse_statement(parser);
    
    return make_for_stmt(initializer, condition, increment, body);
}

Stmt* parse_statement(Parser* parser) {
    // Special handling for { to distinguish table literals from block statements
    if (parser_check(parser, TOKEN_LEFT_BRACE)) {
        // Look ahead to distinguish table literal from block statement
        // Table literals typically have identifier:value or [key]:value patterns
        // Block statements have statements (declarations, expressions, etc.)
        
        // Save current position
        size_t saved_current = parser->current;
        
        // Advance past the {
        parser_advance(parser);
        
        bool is_table_literal = false;
        
        // Skip newlines
        while (parser_check(parser, TOKEN_NEWLINE)) {
            parser_advance(parser);
        }
        
        // Check for table literal patterns:
        // 1. identifier : (key-value pair)
        // 2. [ (computed key)
        // 3. } (empty table)
        if (parser_check(parser, TOKEN_RIGHT_BRACE) ||
            parser_check(parser, TOKEN_LEFT_BRACKET) ||
            (parser_check(parser, TOKEN_IDENTIFIER) && 
             (parser->current + 1 < parser->token_count && 
              parser->tokens[parser->current + 1].type == TOKEN_COLON))) {
            is_table_literal = true;
        }
        
        // Restore position
        parser->current = saved_current;
        
        if (is_table_literal) {
            // Parse as expression statement with table literal
            return parse_expression_statement(parser);
        } else {
            // Parse as block statement
            parser_advance(parser);  // consume the {
            return parse_block_statement(parser);
        }
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
    
    if (parser_match(parser, TOKEN_RETURN)) {
        return parse_return_statement(parser);
    }
    
    if (parser_match(parser, TOKEN_SWITCH)) {
        return parse_switch_statement(parser);
    }
    
    if (parser_match(parser, TOKEN_BREAK)) {
        return parse_break_statement(parser);
    }
    
    if (parser_match(parser, TOKEN_CONTINUE)) {
        return parse_continue_statement(parser);
    }
    
    if (parser_match(parser, TOKEN_IMPORT)) {
        return parse_import_statement(parser);
    }
    
    return parse_expression_statement(parser);
}

Stmt* parse_declaration(Parser* parser) {
    if (parser_match(parser, TOKEN_FUNC)) {
        return parse_function_declaration(parser);
    }
    
    if (parser_match(parser, TOKEN_VAR)) {
        return parse_var_declaration(parser);
    }
    
    if (parser_match(parser, TOKEN_ENUM)) {
        return parse_enum_declaration(parser);
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

// Function declaration parsing: func name(param1, param2) { ... }
Stmt* parse_function_declaration(Parser* parser) {
    // Parse function name
    if (!parser_check(parser, TOKEN_IDENTIFIER)) {
        parser_error_at_current(parser, "Expected function name");
        return NULL;
    }
    
    Token name = parser_advance(parser);
    
    // Parse parameter list
    consume(parser, TOKEN_LEFT_PAREN, "Expected '(' after function name");
    
    Token* params = NULL;
    size_t param_count = 0;
    size_t param_capacity = 0;
    
    // Parse parameters
    if (!parser_check(parser, TOKEN_RIGHT_PAREN)) {
        do {
            if (param_count >= param_capacity) {
                size_t new_capacity = param_capacity == 0 ? 4 : param_capacity * 2;
                Token* new_params = realloc(params, new_capacity * sizeof(Token));
                if (!new_params) {
                    free(params);
                    parser_error_at_current(parser, "Memory allocation failed");
                    return NULL;
                }
                params = new_params;
                param_capacity = new_capacity;
            }
            
            if (!parser_check(parser, TOKEN_IDENTIFIER)) {
                parser_error_at_current(parser, "Expected parameter name");
                free(params);
                return NULL;
            }
            
            params[param_count++] = parser_advance(parser);
            
            if (param_count >= 255) {
                parser_error_at_current(parser, "Cannot have more than 255 parameters");
                free(params);
                return NULL;
            }
        } while (parser_match(parser, TOKEN_COMMA));
    }
    
    consume(parser, TOKEN_RIGHT_PAREN, "Expected ')' after parameters");
    
    // Parse function body
    consume(parser, TOKEN_LEFT_BRACE, "Expected '{' before function body");
    
    Stmt** body = NULL;
    size_t body_count = 0;
    size_t body_capacity = 0;
    
    while (!parser_check(parser, TOKEN_RIGHT_BRACE) && !parser_at_end(parser)) {
        // Skip newlines in function body
        if (parser_match(parser, TOKEN_NEWLINE)) {
            continue;
        }
        
        Stmt* stmt = parse_declaration(parser);
        if (!stmt) {
            // Error occurred, clean up and return
            for (size_t i = 0; i < body_count; i++) {
                ast_release_stmt(body[i]);
            }
            free(body);
            free(params);
            return NULL;
        }
        
        // Add statement to body
        if (body_count >= body_capacity) {
            size_t new_capacity = body_capacity == 0 ? 8 : body_capacity * 2;
            Stmt** new_body = realloc(body, new_capacity * sizeof(Stmt*));
            if (!new_body) {
                ast_release_stmt(stmt);
                for (size_t i = 0; i < body_count; i++) {
                    ast_release_stmt(body[i]);
                }
                free(body);
                free(params);
                parser_error_at_current(parser, "Memory allocation failed");
                return NULL;
            }
            body = new_body;
            body_capacity = new_capacity;
        }
        
        body[body_count++] = stmt;
    }
    
    consume(parser, TOKEN_RIGHT_BRACE, "Expected '}' after function body");
    
    return make_function_stmt(name, params, param_count, body, body_count);
}

// Return statement parsing: return [expression];
Stmt* parse_return_statement(Parser* parser) {
    Token keyword = parser_previous(parser);
    
    Expr* value = NULL;
    if (!parser_check(parser, TOKEN_SEMICOLON) && !parser_check(parser, TOKEN_NEWLINE) && 
        !parser_check(parser, TOKEN_RIGHT_BRACE) && !parser_at_end(parser)) {
        value = parse_expression(parser);
    }
    
    if (!consume_statement_terminator(parser, "Expect ';' or newline after return statement")) {
        if (value) free_expr(value);
        return NULL;
    }
    
    return make_return_stmt(keyword, value);
}

Stmt* parse_break_statement(Parser* parser) {
    Token keyword = parser_previous(parser);
    if (!consume_statement_terminator(parser, "Expect ';' or newline after 'break'")) {
        return NULL;
    }
    return make_break_stmt(keyword);
}

Stmt* parse_continue_statement(Parser* parser) {
    Token keyword = parser_previous(parser);
    if (!consume_statement_terminator(parser, "Expect ';' or newline after 'continue'")) {
        return NULL;
    }
    return make_continue_stmt(keyword);
}

Stmt* parse_import_statement(Parser* parser) {
    Token keyword = parser_previous(parser);
    
    if (!parser_check(parser, TOKEN_STRING)) {
        parser_error_at_current(parser, "Expect string literal after 'import'");
        return NULL;
    }
    
    Token module_name = parser_advance(parser);
    if (!consume_statement_terminator(parser, "Expect ';' or newline after import statement")) {
        return NULL;
    }
    
    return make_import_stmt(keyword, module_name);
}

// Forward declarations for switch parsing functions
CasePattern* parse_case_pattern(Parser* parser);
SwitchCase* parse_switch_case(Parser* parser);

Stmt* parse_switch_statement(Parser* parser) {
    consume(parser, TOKEN_LEFT_PAREN, "Expect '(' after 'switch'");
    Expr* discriminant = parse_expression(parser);
    consume(parser, TOKEN_RIGHT_PAREN, "Expect ')' after switch expression");
    consume(parser, TOKEN_LEFT_BRACE, "Expect '{' before switch body");
    
    SwitchCase** cases = NULL;
    size_t case_count = 0;
    size_t case_capacity = 0;
    
    Stmt** default_body = NULL;
    size_t default_body_count = 0;
    
    while (!parser_check(parser, TOKEN_RIGHT_BRACE) && !parser_at_end(parser)) {
        // Skip newlines
        if (parser_match(parser, TOKEN_NEWLINE)) {
            continue;
        }
        
        if (parser_match(parser, TOKEN_CASE)) {
            // Parse case clause
            SwitchCase* case_clause = parse_switch_case(parser);
            if (case_count >= case_capacity) {
                case_capacity = case_capacity == 0 ? 4 : case_capacity * 2;
                cases = realloc(cases, sizeof(SwitchCase*) * case_capacity);
            }
            cases[case_count++] = case_clause;
        } else if (parser_match(parser, TOKEN_DEFAULT)) {
            // Parse default clause
            consume(parser, TOKEN_COLON, "Expect ':' after 'default'");
            
            // Parse statements until next case/default or end of switch
            Stmt** body = NULL;
            size_t body_count = 0;
            size_t body_capacity = 0;
            
            while (!parser_check(parser, TOKEN_CASE) && 
                   !parser_check(parser, TOKEN_DEFAULT) && 
                   !parser_check(parser, TOKEN_RIGHT_BRACE) && 
                   !parser_at_end(parser)) {
                // Skip newlines
                if (parser_match(parser, TOKEN_NEWLINE)) {
                    continue;
                }
                Stmt* stmt = parse_statement(parser);
                if (body_count >= body_capacity) {
                    body_capacity = body_capacity == 0 ? 4 : body_capacity * 2;
                    body = realloc(body, sizeof(Stmt*) * body_capacity);
                }
                body[body_count++] = stmt;
            }
            
            default_body = body;
            default_body_count = body_count;
        } else {
            parser_error_at_current(parser, "Expect 'case' or 'default' in switch body");
            break;
        }
    }
    
    consume(parser, TOKEN_RIGHT_BRACE, "Expect '}' after switch body");
    
    return make_switch_stmt(discriminant, cases, case_count, default_body, default_body_count);
}

SwitchCase* parse_switch_case(Parser* parser) {
    // For now, only implement simple value patterns
    CasePattern** patterns = malloc(sizeof(CasePattern*));
    size_t pattern_count = 1;
    
    patterns[0] = parse_case_pattern(parser);
    
    // TODO: Handle multiple patterns per case (comma-separated)
    
    consume(parser, TOKEN_COLON, "Expect ':' after case pattern");
    
    // Parse case body statements
    Stmt** body = NULL;
    size_t body_count = 0;
    size_t body_capacity = 0;
    bool has_break = false;
    
    while (!parser_check(parser, TOKEN_CASE) && 
           !parser_check(parser, TOKEN_DEFAULT) && 
           !parser_check(parser, TOKEN_RIGHT_BRACE) && 
           !parser_at_end(parser)) {
        // Skip newlines
        if (parser_match(parser, TOKEN_NEWLINE)) {
            continue;
        }
        Stmt* stmt = parse_statement(parser);
        
        // Check if this statement is a break
        if (stmt->type == STMT_BREAK) {
            has_break = true;
        }
        
        if (body_count >= body_capacity) {
            body_capacity = body_capacity == 0 ? 4 : body_capacity * 2;
            body = realloc(body, sizeof(Stmt*) * body_capacity);
        }
        body[body_count++] = stmt;
        
        // Continue parsing statements even after break (they become unreachable code)
        // The break flag will be used by the evaluator to stop execution
    }
    
    return make_switch_case(patterns, pattern_count, NULL, body, body_count, has_break);
}

CasePattern* parse_case_pattern(Parser* parser) {
    // Check for expression patterns first (>=, <=, ==, !=, >, <)
    if (parser_check(parser, TOKEN_GREATER_EQUAL) || 
        parser_check(parser, TOKEN_LESS_EQUAL) ||
        parser_check(parser, TOKEN_EQUAL_EQUAL) ||
        parser_check(parser, TOKEN_BANG_EQUAL) ||
        parser_check(parser, TOKEN_GREATER) ||
        parser_check(parser, TOKEN_LESS)) {
        
        TokenType op = parser_peek(parser).type;
        parser_advance(parser); // consume the operator
        
        Expr* expr = parse_expression(parser);
        return make_expression_pattern(op, expr);
    }
    
    // Range patterns (e.g., 1..10, 'a'..'z')
    // We need to look ahead to see if this is a range
    if ((parser_check(parser, TOKEN_INTEGER) || parser_check(parser, TOKEN_FLOAT) || 
         parser_check(parser, TOKEN_CHAR) || parser_check(parser, TOKEN_STRING)) &&
        parser->current + 1 < parser->token_count && 
        parser->tokens[parser->current + 1].type == TOKEN_DOT_DOT) {
        
        Expr* start = parse_primary(parser);  // Parse the start value
        consume(parser, TOKEN_DOT_DOT, "Expect '..' in range pattern");
        
        bool inclusive = true;
        if (parser_check(parser, TOKEN_DOT)) {
            parser_advance(parser); // consume the third dot for exclusive range
            inclusive = false;
        }
        
        Expr* end = parse_primary(parser);    // Parse the end value
        return make_range_pattern(start, end, inclusive);
    }
    
    // Simple value patterns
    if (parser_check(parser, TOKEN_INTEGER)) {
        Token token = parser_advance(parser);
        Value value = make_integer_value(token.literal.integer.num_type, token.literal.integer.value);
        return make_value_pattern(value);
    } else if (parser_check(parser, TOKEN_FLOAT)) {
        Token token = parser_advance(parser);
        Value value = make_float_value(token.literal.float_val);
        return make_value_pattern(value);
    } else if (parser_check(parser, TOKEN_STRING)) {
        Token token = parser_advance(parser);
        RefCountedString* rc_string = string_create(token.literal.string);
        Value value = make_string_value(rc_string);
        return make_value_pattern(value);
    } else if (parser_check(parser, TOKEN_TRUE)) {
        parser_advance(parser);
        Value value = make_bool_value(true);
        return make_value_pattern(value);
    } else if (parser_check(parser, TOKEN_FALSE)) {
        parser_advance(parser);
        Value value = make_bool_value(false);
        return make_value_pattern(value);
    } else if (parser_check(parser, TOKEN_NIL)) {
        parser_advance(parser);
        Value value = make_nil_value();
        return make_value_pattern(value);
    } else if (parser_check(parser, TOKEN_IDENTIFIER)) {
        // Check if this is an enum access pattern (EnumName.MEMBER)
        if (parser->current + 1 < parser->token_count && 
            parser->tokens[parser->current + 1].type == TOKEN_DOT) {
            
            Token enum_name = parser_advance(parser);  // consume enum name
            parser_advance(parser);  // consume '.'
            Token member_name = consume(parser, TOKEN_IDENTIFIER, "Expect member name after '.'");
            
            // Create an enum access expression and wrap it in an expression pattern
            Expr* enum_expr = make_enum_access_expr(enum_name, member_name);
            return make_expression_pattern(TOKEN_EQUAL_EQUAL, enum_expr);
        } else {
            parser_error_at_current(parser, "Expect literal value in case pattern");
            return make_wildcard_pattern();
        }
    } else if (parser_check(parser, TOKEN_IS)) {
        // Type matching pattern: is string, is array, etc.
        parser_advance(parser); // consume 'is'
        
        ValueType value_type;
        const char* type_name = NULL;
        
        if (parser_check(parser, TOKEN_IDENTIFIER)) {
            Token type_token = parser_advance(parser);
            type_name = type_token.identifier;
        } else if (parser_check(parser, TOKEN_NIL)) {
            parser_advance(parser);
            type_name = "nil";
        } else {
            parser_error_at_current(parser, "Expect type name after 'is'");
            return make_wildcard_pattern();
        }
        
        // Map type names to ValueType enum
        if (strcmp(type_name, "string") == 0) {
            value_type = VAL_STRING;
        } else if (strcmp(type_name, "int") == 0 || strcmp(type_name, "integer") == 0) {
            value_type = VAL_INTEGER;
        } else if (strcmp(type_name, "float") == 0) {
            value_type = VAL_FLOAT64;
        } else if (strcmp(type_name, "bool") == 0 || strcmp(type_name, "boolean") == 0) {
            value_type = VAL_BOOL;
        } else if (strcmp(type_name, "array") == 0) {
            value_type = VAL_ARRAY;
        } else if (strcmp(type_name, "table") == 0) {
            value_type = VAL_TABLE;
        } else if (strcmp(type_name, "function") == 0) {
            value_type = VAL_FUNCTION;
        } else if (strcmp(type_name, "nil") == 0) {
            value_type = VAL_NIL;
        } else {
            parser_error_at_current(parser, "Unknown type name in 'is' pattern");
            return make_wildcard_pattern();
        }
        
        return make_type_pattern(value_type);
    } else {
        parser_error_at_current(parser, "Expect literal value in case pattern");
        return make_wildcard_pattern();
    }
}

// Parse enum member: NAME or NAME = value
EnumMemberDef* parse_enum_member(Parser* parser) {
    if (!parser_check(parser, TOKEN_IDENTIFIER)) {
        parser_error_at_current(parser, "Expect enum member name");
        return NULL;
    }
    
    Token name = parser_advance(parser);
    Expr* value = NULL;
    
    if (parser_match(parser, TOKEN_EQUAL)) {
        value = parse_expression(parser);
    }
    
    return make_enum_member(name, value);
}

// Parse enum declaration: enum Name : type { MEMBER1, MEMBER2 = value, ... }
Stmt* parse_enum_declaration(Parser* parser) {
    Token keyword = parser_previous(parser);  // The 'enum' token
    
    // Parse enum name
    if (!parser_check(parser, TOKEN_IDENTIFIER)) {
        parser_error_at_current(parser, "Expect enum name");
        return NULL;
    }
    Token name = parser_advance(parser);
    
    // Parse optional underlying type
    NumericType underlying_type = NUM_INT32;  // Default to int32
    bool has_explicit_type = false;
    
    if (parser_match(parser, TOKEN_COLON)) {
        has_explicit_type = true;
        
        if (parser_match(parser, TOKEN_TYPE_INT8)) {
            underlying_type = NUM_INT8;
        } else if (parser_match(parser, TOKEN_TYPE_UINT8)) {
            underlying_type = NUM_UINT8;
        } else if (parser_match(parser, TOKEN_TYPE_INT16)) {
            underlying_type = NUM_INT16;
        } else if (parser_match(parser, TOKEN_TYPE_UINT16)) {
            underlying_type = NUM_UINT16;
        } else if (parser_match(parser, TOKEN_TYPE_INT32)) {
            underlying_type = NUM_INT32;
        } else if (parser_match(parser, TOKEN_TYPE_UINT32)) {
            underlying_type = NUM_UINT32;
        } else if (parser_match(parser, TOKEN_TYPE_INT64)) {
            underlying_type = NUM_INT64;
        } else if (parser_match(parser, TOKEN_TYPE_UINT64)) {
            underlying_type = NUM_UINT64;
        } else {
            parser_error_at_current(parser, "Expect integer type after ':'");
            return NULL;
        }
    }
    
    // Parse enum body
    consume(parser, TOKEN_LEFT_BRACE, "Expect '{' before enum body");
    
    EnumMemberDef* members = NULL;
    EnumMemberDef* last_member = NULL;
    
    // Skip newlines
    while (parser_check(parser, TOKEN_NEWLINE)) {
        parser_advance(parser);
    }
    
    while (!parser_check(parser, TOKEN_RIGHT_BRACE) && !parser_at_end(parser)) {
        EnumMemberDef* member = parse_enum_member(parser);
        if (!member) {
            // Error in parsing member, cleanup and return
            while (members) {
                EnumMemberDef* next = members->next;
                if (members->value) free_expr(members->value);
                free(members);
                members = next;
            }
            return NULL;
        }
        
        // Add to linked list
        if (!members) {
            members = member;
            last_member = member;
        } else {
            last_member->next = member;
            last_member = member;
        }
        
        // Skip newlines
        while (parser_check(parser, TOKEN_NEWLINE)) {
            parser_advance(parser);
        }
        
        // Handle comma separator
        if (parser_check(parser, TOKEN_COMMA)) {
            parser_advance(parser);
            // Skip newlines after comma
            while (parser_check(parser, TOKEN_NEWLINE)) {
                parser_advance(parser);
            }
        } else if (!parser_check(parser, TOKEN_RIGHT_BRACE)) {
            parser_error_at_current(parser, "Expect ',' between enum members");
            break;
        }
    }
    
    consume(parser, TOKEN_RIGHT_BRACE, "Expect '}' after enum body");
    
    // Consume optional semicolon or newline (same as other declarations)
    if (!consume_statement_terminator(parser, "Expect ';' or newline after enum declaration")) {
        // Error already reported, clean up
        while (members) {
            EnumMemberDef* next = members->next;
            if (members->value) free_expr(members->value);
            free(members);
            members = next;
        }
        return NULL;
    }
    
    return make_enum_stmt(keyword, name, underlying_type, has_explicit_type, members);
}

void free_parse_result(ParseResult* result) {
    if (result->statements) {
        for (size_t i = 0; i < result->count; i++) {
            ast_release_stmt(result->statements[i]);  // Use reference counting
        }
        free(result->statements);
        result->statements = NULL;
    }
    result->count = 0;
}
