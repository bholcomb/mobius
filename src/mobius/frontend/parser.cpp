#include "parser.h"
#include "util/utility.h"
#include "frontend/scanner.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

// Forward declarations
Stmt* parse_continue_statement(Parser* parser);
Stmt* parse_pragma_statement(Parser* parser);
static ValueType parse_type_name(Parser* parser, const char* context = "after ':'");

// Parser initialization
void init_parser(Parser* parser, MobiusState* state, Token* tokens, size_t token_count) {
    parser->tokens = tokens;
    parser->token_count = token_count;
    parser->current = 0;
    parser->had_error = false;
    parser->panic_mode = false;
    parser->source_name = state ? state->getSourceContext() : nullptr;
    parser->state = state;
    parser->suppress_method_colon = false;
}

// Parser state functions
bool parser_at_end(Parser* parser) {
    return parser->current >= parser->token_count || 
           parser->tokens[parser->current].type == TOKEN_EOF ||
           parser->tokens[parser->current].type == TOKEN_ERROR;
}

Token parser_peek(Parser* parser) {
    if (parser->current >= parser->token_count) {
        // Return EOF token if we're past the end
        Token eof = {};
        eof.type = TOKEN_EOF;
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
        TokenType type = (TokenType)va_arg(args, int);
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

    const char* final_message = message;
    if (token.type == TOKEN_ERROR && token.identifier && token.identifier[0] != '\0') {
        final_message = token.identifier;
    }

    parser->panic_mode = true;
    parser->had_error = true;
    
    if (parser->source_name) {
        fprintf(stderr, "[%s:%d] Error", parser->source_name, token.line);
    } else {
        fprintf(stderr, "[line %d] Error", token.line);
    }
    
    if (token.type == TOKEN_EOF) {
        fprintf(stderr, " at end");
    } else if (token.type == TOKEN_ERROR) {
        // Nothing
    } else {
        fprintf(stderr, " at '%s'", token.identifier ? token.identifier : "unknown");
    }
    
    fprintf(stderr, ": %s\n", final_message ? final_message : "Unknown parse error");
}

void parser_error_at_current(Parser* parser, const char* message) {
    parser_error(parser, parser_peek(parser), message);
}

Token consume(Parser* parser, TokenType type, const char* message) {
    if (parser_check(parser, type)) {
        return parser_advance(parser);
    }
    
    parser_error_at_current(parser, message);
    Token error_token = {};
    error_token.type = TOKEN_ERROR;
    error_token.identifier = message;
    error_token.length = (int)strlen(message);
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
            case TOKEN_STRUCT:
            case TOKEN_SHARED:
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
        NumberType num_type = token.literal.integer.num_type;
        int64_t int_value = token.literal.integer.value;

        Value value = (num_type == NUM_UINT64)
            ? make_uint64_value((uint64_t)int_value)
            : make_int64_value(int_value);
        return make_literal_expr(value);
    }
    
    if (parser_match(parser, TOKEN_FLOAT)) {
        Token token = parser_previous(parser);
        Value value = make_float_value(token.literal.float_val);
        return make_literal_expr(value);
    }
    
    if (parser_match(parser, TOKEN_STRING)) {
        Token token = parser_previous(parser);
        // Create a MobiusString from the token's string literal
        if (token.literal.string) {
            Value value = make_string_value_from_cstr(parser->state, token.literal.string);
            return make_literal_expr(value);
        } else {
            Value value = make_string_value_from_cstr(parser->state, "");
            return make_literal_expr(value);
        }
    }
    
    if (parser_match(parser, TOKEN_INTERP_STRING)) {
        Token token = parser_previous(parser);
        const char* raw = token.literal.string ? token.literal.string : "";
        size_t raw_len = raw ? strlen(raw) : 0;

        Expr* result = NULL;
        Token plus_op = token;
        plus_op.type = TOKEN_PLUS;

        const char* p = raw;
        const char* end = raw + raw_len;
        while (p < end) {
            const char* ds = strstr(p, "${");
            if (!ds) {
                if (p < end) {
                    Value sv = make_string_value_from_cstr(parser->state, p);
                    Expr* seg = make_literal_expr(sv);
                    result = result ? make_binary_expr(result, plus_op, seg) : seg;
                }
                break;
            }
            if (ds > p) {
                size_t seg_len = ds - p;
                char* buf = (char*)malloc(seg_len + 1);
                if (!buf) {
                    parser_error(parser, token, "Out of memory in interpolated string");
                    if (result) ast_release_expr(result);
                    return NULL;
                }
                memcpy(buf, p, seg_len); buf[seg_len] = '\0';
                Value sv = make_string_value_from_cstr(parser->state, buf);
                free(buf);
                Expr* seg = make_literal_expr(sv);
                result = result ? make_binary_expr(result, plus_op, seg) : seg;
            }
            ds += 2;
            int depth = 1;
            const char* expr_start = ds;
            while (ds < end && depth > 0) {
                if (*ds == '{') depth++;
                else if (*ds == '}') depth--;
                if (depth > 0) ds++;
            }
            if (depth != 0) {
                parser_error(parser, token, "Unterminated ${...} in interpolated string");
                if (result) ast_release_expr(result);
                return NULL;
            }
            size_t expr_len = ds - expr_start;
            char* expr_src = (char*)malloc(expr_len + 1);
            if (!expr_src) {
                parser_error(parser, token, "Out of memory in interpolated string");
                if (result) ast_release_expr(result);
                return NULL;
            }
            memcpy(expr_src, expr_start, expr_len); expr_src[expr_len] = '\0';

            TokenArray sub_tokens = scan_source(expr_src, parser->state->stringPool());
            free(expr_src);

            if (sub_tokens.count > 0) {
                Parser sub_parser;
                init_parser(&sub_parser, parser->state, sub_tokens.tokens, sub_tokens.count);
                Expr* expr_node = parse_expression(&sub_parser);
                if (expr_node) {
                    result = result ? make_binary_expr(result, plus_op, expr_node) : expr_node;
                }
            }

            if (sub_tokens.tokens) {
                for (size_t i = 0; i < sub_tokens.count; i++) free_token(&sub_tokens.tokens[i]);
                free(sub_tokens.tokens);
            }

            p = ds + 1;
        }

        if (!result) {
            return make_literal_expr(make_string_value_from_cstr(parser->state, ""));
        }
        return result;
    }

    if (parser_match(parser, TOKEN_CHAR)) {
        Token token = parser_previous(parser);
        Value value = make_char_value(token.literal.character);
        return make_literal_expr(value);
    }
    
    if (parser_match(parser, TOKEN_IDENTIFIER)) {
        return make_variable_expr(parser_previous(parser));
    }
    
    if (parser_match(parser, TOKEN_FUNC)) {
        Token name = {};
        name.type = TOKEN_IDENTIFIER;
        name.identifier = NULL;
        name.interned = NULL;
        name.length = 0;
        name.line = parser_previous(parser).line;
        name.column = parser_previous(parser).column;

        if (parser_check(parser, TOKEN_IDENTIFIER)) {
            name = parser_advance(parser);
        }

        consume(parser, TOKEN_LEFT_PAREN, "Expect '(' after 'func' in expression.");

        Token* params = NULL;
        ValueType* param_types = NULL;
        size_t param_count = 0;
        size_t param_capacity = 0;
        bool has_any_type = false;

        if (!parser_check(parser, TOKEN_RIGHT_PAREN)) {
            do {
                if (param_count >= param_capacity) {
                    size_t new_cap = param_capacity == 0 ? 4 : param_capacity * 2;
                    Token* new_params = (Token*)realloc(params, new_cap * sizeof(Token));
                    ValueType* new_types = (ValueType*)realloc(param_types, new_cap * sizeof(ValueType));
                    if (!new_params || !new_types) { free(params); free(param_types); parser_error_at_current(parser, "Out of memory"); return NULL; }
                    params = new_params;
                    param_types = new_types;
                    param_capacity = new_cap;
                }
                if (!parser_check(parser, TOKEN_IDENTIFIER)) {
                    parser_error_at_current(parser, "Expect parameter name");
                    free(params); free(param_types);
                    return NULL;
                }
                params[param_count] = parser_advance(parser);
                if (parser_match(parser, TOKEN_COLON)) {
                    ValueType vt = parse_type_name(parser, "in parameter type annotation");
                    if ((int)vt < 0) { free(params); free(param_types); return NULL; }
                    param_types[param_count] = vt;
                    has_any_type = true;
                } else {
                    param_types[param_count] = VAL_UNKNOWN;
                }
                param_count++;
            } while (parser_match(parser, TOKEN_COMMA));
        }
        consume(parser, TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");

        ValueType return_type = VAL_UNKNOWN;
        if (parser_match(parser, TOKEN_COLON)) {
            return_type = parse_type_name(parser, "in return type annotation");
            if ((int)return_type < 0) { free(params); free(param_types); return NULL; }
        }

        if (!has_any_type) { free(param_types); param_types = NULL; }

        consume(parser, TOKEN_LEFT_BRACE, "Expect '{' before function body.");

        Stmt** body = NULL;
        size_t body_count = 0;
        size_t body_capacity = 0;

        while (!parser_check(parser, TOKEN_RIGHT_BRACE) && !parser_at_end(parser)) {
            if (parser_match(parser, TOKEN_NEWLINE)) continue;
            Stmt* s = parse_declaration(parser);
            if (!s) {
                for (size_t i = 0; i < body_count; i++) ast_release_stmt(body[i]);
                free(body); free(params); free(param_types);
                return NULL;
            }
            if (body_count >= body_capacity) {
                size_t new_cap = body_capacity == 0 ? 8 : body_capacity * 2;
                Stmt** nb = (Stmt**)realloc(body, new_cap * sizeof(Stmt*));
                if (!nb) { ast_release_stmt(s); for (size_t i = 0; i < body_count; i++) ast_release_stmt(body[i]); free(body); free(params); free(param_types); return NULL; }
                body = nb;
                body_capacity = new_cap;
            }
            body[body_count++] = s;
        }
        consume(parser, TOKEN_RIGHT_BRACE, "Expect '}' after function body.");

        return make_function_expr(name, params, param_types, param_count, return_type, body, body_count);
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
            TablePair* new_pairs = (TablePair*)realloc(pairs, pair_capacity * sizeof(TablePair));
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
                char* key_str = (char*)malloc(key_token.length + 1);
                if (key_str) {
                    const char* key_id = key_token.identifier ? key_token.identifier : "unknown";
                    strncpy(key_str, key_id, strlen(key_id));
                    key_str[key_token.length] = '\0';
                    current_pair->key = make_literal_expr(make_string_value_from_cstr(parser->state, key_str));
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
            elements = (Expr**)realloc(elements, element_capacity * sizeof(Expr*));
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
                arguments = (Expr**)realloc(arguments, arg_capacity * sizeof(Expr*));
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
            expr = make_table_dot_expr(expr, key);
        } else if (!parser->suppress_method_colon &&
                   parser_check(parser, TOKEN_COLON) &&
                   parser->current + 2 < parser->token_count &&
                   parser->tokens[parser->current + 1].type == TOKEN_IDENTIFIER &&
                   parser->tokens[parser->current + 2].type == TOKEN_LEFT_PAREN) {
            parser_advance(parser);
            Token key = consume(parser, TOKEN_IDENTIFIER, "Expect method name after ':'");
            expr = make_method_dot_expr(expr, key);
        }
        // Handle postfix increment/decrement
        else if (parser_match_any(parser, 2, TOKEN_PLUS_PLUS, TOKEN_MINUS_MINUS)) {
            Token op = parser_previous(parser);
            
            // Can only apply to variable expressions
            if (expr->type != EXPR_VARIABLE) {
                parser_error(parser, op, "Postfix operator can only be applied to variables");
                ast_release_expr(expr);
                return NULL;
            }
            
            Token name = expr->as.variable.name;
            bool is_increment = (op.type == TOKEN_PLUS_PLUS);
            ast_release_expr(expr);
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
    if (parser_match_any(parser, 5, TOKEN_BANG, TOKEN_MINUS, TOKEN_NOT, TOKEN_PLUS, TOKEN_TILDE)) {
        Token op = parser_previous(parser);
        Expr* right = parse_unary(parser);
        return make_unary_expr(op, right);
    }

    if (parser_match(parser, TOKEN_SPAWN)) {
        Expr* callee = parse_call(parser);
        if (!callee) return NULL;

        if (callee->type != EXPR_CALL) {
            parser_error_at_current(parser, "spawn requires a function call, e.g. 'spawn func(args)'");
            ast_release_expr(callee);
            return NULL;
        }

        Expr* spawn = make_spawn_expr(callee->as.call.callee,
                                       callee->as.call.arguments,
                                       callee->as.call.arg_count);
        callee->as.call.callee = NULL;
        callee->as.call.arguments = NULL;
        callee->as.call.arg_count = 0;
        ast_release_expr(callee);
        return spawn;
    }

    if (parser_match(parser, TOKEN_AWAIT)) {
        Expr* operand = parse_unary(parser);
        if (!operand) return NULL;
        return make_await_expr(operand);
    }

    if (parser_match(parser, TOKEN_ATOMIC)) {
        consume(parser, TOKEN_LEFT_PAREN, "Expect '(' after 'atomic'.");
        Expr* body = parse_expression(parser);
        if (!body) return NULL;
        consume(parser, TOKEN_RIGHT_PAREN, "Expect ')' after atomic expression.");
        return make_atomic_expr(body);
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

Expr* parse_shift(Parser* parser) {
    Expr* expr = parse_term(parser);

    while (parser_match_any(parser, 2, TOKEN_LEFT_SHIFT, TOKEN_RIGHT_SHIFT)) {
        Token op = parser_previous(parser);
        Expr* right = parse_term(parser);
        expr = make_binary_expr(expr, op, right);
    }

    return expr;
}

// Shared helper: consume a type name token and return the corresponding ValueType.
// Returns (ValueType)-1 on failure and reports an error.
static ValueType parse_type_name(Parser* parser, const char* context) {
    const char* type_name = NULL;

    if (parser_check(parser, TOKEN_IDENTIFIER)) {
        Token t = parser_advance(parser);
        type_name = t.identifier;
    } else if (parser_check(parser, TOKEN_TYPE_INT64))   { parser_advance(parser); type_name = "int64";   }
      else if (parser_check(parser, TOKEN_TYPE_UINT64))  { parser_advance(parser); type_name = "uint64";  }
      else if (parser_check(parser, TOKEN_TYPE_FLOAT64)) { parser_advance(parser); type_name = "float64"; }
      else if (parser_check(parser, TOKEN_NIL))          { parser_advance(parser); type_name = "nil";     }
      else if (parser_check(parser, TOKEN_TRUE) || parser_check(parser, TOKEN_FALSE)) {
          parser_advance(parser); type_name = "bool";
      } else {
        char msg[128];
        snprintf(msg, sizeof(msg), "Expected type name %s", context);
        parser_error_at_current(parser, msg);
        return (ValueType)-1;
    }

    if (strcmp(type_name, "string") == 0)                                          return VAL_STRING;
    if (strcmp(type_name, "int64") == 0)                                           return VAL_INT64;
    if (strcmp(type_name, "uint64") == 0)                                          return VAL_UINT64;
    if (strcmp(type_name, "float64") == 0)                                         return VAL_FLOAT64;
    if (strcmp(type_name, "bool") == 0 || strcmp(type_name, "boolean") == 0)       return VAL_BOOL;
    if (strcmp(type_name, "array") == 0)                                           return VAL_ARRAY;
    if (strcmp(type_name, "buffer") == 0)                                          return VAL_BUFFER;
    if (strcmp(type_name, "table") == 0)                                           return VAL_TABLE;
    if (strcmp(type_name, "function") == 0)                                        return VAL_FUNCTION;
    if (strcmp(type_name, "nil") == 0)                                             return VAL_NIL;
    if (strcmp(type_name, "userdata") == 0)                                        return VAL_USERDATA;
    if (strcmp(type_name, "enum") == 0)                                            return VAL_ENUM;

    char msg[128];
    snprintf(msg, sizeof(msg), "Unknown type name %s", context);
    parser_error_at_current(parser, msg);
    return (ValueType)-1;
}

Expr* parse_comparison(Parser* parser) {
    Expr* expr = parse_shift(parser);
    
    while (parser_match_any(parser, 4, TOKEN_GREATER, TOKEN_GREATER_EQUAL, 
                           TOKEN_LESS, TOKEN_LESS_EQUAL)) {
        Token op = parser_previous(parser);
        Expr* right = parse_shift(parser);
        expr = make_binary_expr(expr, op, right);
    }

    // Handle 'expr is type' — same precedence level as comparisons
    if (parser_match(parser, TOKEN_IS)) {
        Token op = parser_previous(parser);
        ValueType vt = parse_type_name(parser, "after 'is'");
        if ((int)vt >= 0) {
            Expr* right = make_literal_expr(make_int64_value((int64_t)vt));
            expr = make_binary_expr(expr, op, right);
        }
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

Expr* parse_bitwise_and(Parser* parser) {
    Expr* expr = parse_equality(parser);

    while (parser_match(parser, TOKEN_AMPERSAND)) {
        Token op = parser_previous(parser);
        Expr* right = parse_equality(parser);
        expr = make_binary_expr(expr, op, right);
    }

    return expr;
}

Expr* parse_bitwise_xor(Parser* parser) {
    Expr* expr = parse_bitwise_and(parser);

    while (parser_match(parser, TOKEN_CARET)) {
        Token op = parser_previous(parser);
        Expr* right = parse_bitwise_and(parser);
        expr = make_binary_expr(expr, op, right);
    }

    return expr;
}

Expr* parse_bitwise_or(Parser* parser) {
    Expr* expr = parse_bitwise_xor(parser);

    while (parser_match(parser, TOKEN_PIPE)) {
        Token op = parser_previous(parser);
        Expr* right = parse_bitwise_xor(parser);
        expr = make_binary_expr(expr, op, right);
    }

    return expr;
}

Expr* parse_and(Parser* parser) {
    Expr* expr = parse_bitwise_or(parser);
    
    while (parser_match_any(parser, 2, TOKEN_AND, TOKEN_AND_AND)) {
        Token op = parser_previous(parser);
        Expr* right = parse_bitwise_or(parser);
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

Expr* parse_ternary(Parser* parser) {
    Expr* expr = parse_or(parser);

    if (parser_match(parser, TOKEN_QUESTION)) {
        Expr* then_expr = parse_expression(parser);
        consume(parser, TOKEN_COLON, "Expect ':' in ternary expression.");
        Expr* else_expr = parse_ternary(parser);
        expr = make_ternary_expr(expr, then_expr, else_expr);
    }

    return expr;
}

Expr* parse_assignment(Parser* parser) {
    Expr* expr = parse_ternary(parser);
    
    if (parser_match(parser, TOKEN_EQUAL)) {
        Token equals = parser_previous(parser);
        Expr* value = parse_assignment(parser);
        
        if (expr->type == EXPR_VARIABLE ||
            expr->type == EXPR_ARRAY_INDEX ||
            expr->type == EXPR_TABLE_INDEX ||
            expr->type == EXPR_TABLE_DOT) {
            return make_assignment_expr(expr, value);
        }
        
        parser_error(parser, equals, "Invalid assignment target.");
        ast_release_expr(value);
    }

    if (parser_match_any(parser, 5, TOKEN_PLUS_EQUAL, TOKEN_MINUS_EQUAL,
                         TOKEN_STAR_EQUAL, TOKEN_SLASH_EQUAL, TOKEN_PERCENT_EQUAL)) {
        Token compound_op = parser_previous(parser);

        if (expr->type != EXPR_VARIABLE &&
            expr->type != EXPR_ARRAY_INDEX &&
            expr->type != EXPR_TABLE_INDEX &&
            expr->type != EXPR_TABLE_DOT) {
            parser_error(parser, compound_op, "Invalid compound assignment target.");
            return expr;
        }

        Token bin_op = compound_op;
        switch (compound_op.type) {
            case TOKEN_PLUS_EQUAL:  bin_op.type = TOKEN_PLUS;  break;
            case TOKEN_MINUS_EQUAL: bin_op.type = TOKEN_MINUS; break;
            case TOKEN_STAR_EQUAL:  bin_op.type = TOKEN_STAR;  break;
            case TOKEN_SLASH_EQUAL:   bin_op.type = TOKEN_SLASH;   break;
            case TOKEN_PERCENT_EQUAL: bin_op.type = TOKEN_PERCENT; break;
            default: break;
        }

        Expr* rhs = parse_assignment(parser);
        Expr* target_copy = ast_retain_expr(expr);
        Expr* binary = make_binary_expr(target_copy, bin_op, rhs);
        return make_assignment_expr(expr, binary);
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
        ast_release_expr(expr);
        return NULL;
    }
    if (!expr) {
        return NULL;
    }
    return make_expression_stmt(expr);
}

// Parse optional type annotation: ": type"
void parse_NUM_annotation(Parser* parser, NumberType* type_hint, bool* is_annotated) {
    *type_hint = NUM_UNKNOWN;
    *is_annotated = false;
    
    if (parser_match(parser, TOKEN_COLON)) {
        *is_annotated = true;
        
        if (parser_match(parser, TOKEN_TYPE_INT64)) {
            *type_hint = NUM_INT64;
        } else if (parser_match(parser, TOKEN_TYPE_UINT64)) {
            *type_hint = NUM_UINT64;
        } else if (parser_match(parser, TOKEN_TYPE_FLOAT64)) {
            *type_hint = NUM_FLOAT64;
        } else {
            parser_error(parser, parser_peek(parser), "Expected type name after ':'");
            *is_annotated = false;
        }
    }
}

static bool token_text_equals(const Token& token, const char* text) {
    return token.identifier && strcmp(token.identifier, text) == 0;
}

static bool is_builtin_struct_type_token(const Token& token) {
    switch (token.type) {
        case TOKEN_TYPE_INT64:
        case TOKEN_TYPE_UINT64:
        case TOKEN_TYPE_FLOAT64:
            return true;
        case TOKEN_IDENTIFIER:
            return token_text_equals(token, "int8") ||
                   token_text_equals(token, "uint8") ||
                   token_text_equals(token, "byte") ||
                   token_text_equals(token, "int16") ||
                   token_text_equals(token, "uint16") ||
                   token_text_equals(token, "int32") ||
                   token_text_equals(token, "uint32") ||
                   token_text_equals(token, "float32") ||
                   token_text_equals(token, "bool") ||
                   token_text_equals(token, "bool8");
        default:
            return false;
    }
}

static void free_struct_members_array(StructMemberDef* members, size_t count) {
    if (!members) return;
    for (size_t i = 0; i < count; i++) {
        if (members[i].kind == STRUCT_MEMBER_FIELD) {
            free_token(&members[i].as.field.name);
            free_token(&members[i].as.field.type.type_name);
        } else if (members[i].kind == STRUCT_MEMBER_UNION ||
                   members[i].kind == STRUCT_MEMBER_STRUCT) {
            free_struct_members_array(members[i].as.group_def.members,
                                      members[i].as.group_def.member_count);
        }
    }
    free(members);
}

static bool parse_struct_type_ref(Parser* parser, StructTypeRef* out) {
    memset(out, 0, sizeof(*out));

    if (parser_match(parser, TOKEN_IDENTIFIER) ||
        parser_match(parser, TOKEN_TYPE_INT64) ||
        parser_match(parser, TOKEN_TYPE_UINT64) ||
        parser_match(parser, TOKEN_TYPE_FLOAT64)) {
        out->type_name = parser_previous(parser);
        out->is_builtin_type = is_builtin_struct_type_token(out->type_name);
    } else {
        parser_error_at_current(parser, "Expect field type");
        return false;
    }

    if (parser_match(parser, TOKEN_LEFT_BRACKET)) {
        Token count = consume(parser, TOKEN_INTEGER, "Expect array length inside '[' and ']'.");
        if (count.literal.integer.value <= 0) {
            parser_error(parser, count, "Array length must be positive");
            return false;
        }
        out->array_count = (size_t)count.literal.integer.value;
        consume(parser, TOKEN_RIGHT_BRACKET, "Expect ']' after array length.");
    }

    return true;
}

static bool parse_struct_members(Parser* parser, StructMemberDef** out_members, size_t* out_count) {
    StructMemberDef* members = NULL;
    size_t count = 0;
    size_t capacity = 0;

    while (!parser_check(parser, TOKEN_RIGHT_BRACE) && !parser_at_end(parser)) {
        while (parser_match(parser, TOKEN_NEWLINE)) {}
        if (parser_check(parser, TOKEN_RIGHT_BRACE)) break;

        StructMemberDef member = {};
        if (parser_match(parser, TOKEN_UNION) || parser_match(parser, TOKEN_STRUCT)) {
            Token keyword = parser_previous(parser);
            bool is_union = keyword.type == TOKEN_UNION;
            consume(parser, TOKEN_LEFT_BRACE, is_union ? "Expect '{' after 'union'."
                                                       : "Expect '{' after 'struct'.");

            StructMemberDef* group_members = NULL;
            size_t group_count = 0;
            if (!parse_struct_members(parser, &group_members, &group_count)) {
                free_struct_members_array(members, count);
                return false;
            }

            consume(parser, TOKEN_RIGHT_BRACE, is_union ? "Expect '}' after union body."
                                                        : "Expect '}' after struct body.");
            if (!consume_statement_terminator(parser, is_union
                    ? "Expect ';' or newline after union declaration"
                    : "Expect ';' or newline after struct declaration")) {
                free_struct_members_array(group_members, group_count);
                free_struct_members_array(members, count);
                return false;
            }

            member = make_struct_group_member(keyword, group_members, group_count, is_union);
        } else {
            Token name = consume(parser, TOKEN_IDENTIFIER, "Expect field name.");
            bool has_explicit_offset = false;
            int64_t offset = 0;
            if (parser_match(parser, TOKEN_AT)) {
                Token offset_token = consume(parser, TOKEN_INTEGER, "Expect integer offset after 'at'.");
                if (offset_token.literal.integer.value < 0) {
                    parser_error(parser, offset_token, "Field offset must be non-negative");
                    free_struct_members_array(members, count);
                    return false;
                }
                has_explicit_offset = true;
                offset = offset_token.literal.integer.value;
            }

            consume(parser, TOKEN_COLON, "Expect ':' after field name.");
            StructTypeRef type = {};
            if (!parse_struct_type_ref(parser, &type)) {
                free_struct_members_array(members, count);
                return false;
            }
            if (!consume_statement_terminator(parser, "Expect ';' or newline after struct field")) {
                free_token(&type.type_name);
                free_struct_members_array(members, count);
                return false;
            }

            member = make_struct_field_member(name, type, has_explicit_offset, offset);
            free_token(&type.type_name);
        }

        if (count >= capacity) {
            capacity = capacity == 0 ? 8 : capacity * 2;
            members = (StructMemberDef*)realloc(members, capacity * sizeof(StructMemberDef));
        }
        members[count++] = member;
    }

    *out_members = members;
    *out_count = count;
    return true;
}

Stmt* parse_var_declaration(Parser* parser) {
    Token name = consume(parser, TOKEN_IDENTIFIER, "Expect variable name.");
    
    // Parse optional type annotation
    NumberType type_hint = NUM_UNKNOWN;
    bool is_annotated = false;
    parse_NUM_annotation(parser, &type_hint, &is_annotated);
    
    Expr* initializer = NULL;
    if (parser_match(parser, TOKEN_EQUAL)) {
        initializer = parse_expression(parser);
    }
    
    if (!consume_statement_terminator(parser, "Expect ';' or newline after variable declaration")) {
        // Error already reported, clean up
        if (initializer) ast_release_expr(initializer);
        return NULL;
    }
    return make_var_stmt(name, initializer, type_hint, is_annotated);
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
                statements = (Stmt**)realloc(statements, capacity * sizeof(Stmt*));
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

Stmt* parse_for_in_statement(Parser* parser, Token var_name, bool has_two, Token var_name2) {
    Expr* iterable = parse_expression(parser);
    consume(parser, TOKEN_RIGHT_PAREN, "Expect ')' after for-in expression.");

    while (parser_check(parser, TOKEN_NEWLINE)) parser_advance(parser);

    Stmt* body = parse_statement(parser);
    if (has_two) {
        return make_for_in_stmt_kv(var_name, var_name2, iterable, body);
    }
    return make_for_in_stmt(var_name, iterable, body);
}

Stmt* parse_for_statement(Parser* parser) {
    consume(parser, TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");

    // Detect for-in: for (var x in expr), for (var k, v in expr), or for (x in expr)
    size_t saved = parser->current;
    bool is_for_in = false;
    bool has_two_vars = false;
    Token for_in_var = {};
    Token for_in_var2 = {};

    if (parser_check(parser, TOKEN_VAR)) {
        parser_advance(parser);
        if (parser_check(parser, TOKEN_IDENTIFIER)) {
            for_in_var = parser_advance(parser);
            if (parser_check(parser, TOKEN_COMMA)) {
                parser_advance(parser);
                if (parser_check(parser, TOKEN_IDENTIFIER)) {
                    for_in_var2 = parser_advance(parser);
                    if (parser_check(parser, TOKEN_IN)) {
                        parser_advance(parser);
                        is_for_in = true;
                        has_two_vars = true;
                    }
                }
            } else if (parser_check(parser, TOKEN_IN)) {
                parser_advance(parser);
                is_for_in = true;
            }
        }
    } else if (parser_check(parser, TOKEN_IDENTIFIER)) {
        for_in_var = parser_advance(parser);
        if (parser_check(parser, TOKEN_COMMA)) {
            parser_advance(parser);
            if (parser_check(parser, TOKEN_IDENTIFIER)) {
                for_in_var2 = parser_advance(parser);
                if (parser_check(parser, TOKEN_IN)) {
                    parser_advance(parser);
                    is_for_in = true;
                    has_two_vars = true;
                }
            }
        } else if (parser_check(parser, TOKEN_IN)) {
            parser_advance(parser);
            is_for_in = true;
        }
    }

    if (is_for_in) {
        return parse_for_in_statement(parser, for_in_var, has_two_vars, for_in_var2);
    }

    parser->current = saved;
    
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
        //
        // Disambiguation: `{ id : id ( }` could be a table literal
        // `{ key: func() }` or a block with a method call `{ obj:method() }`.
        // We treat `identifier : identifier (` as a block (method call)
        // since that is far more common. Use `["key"]: func()` for the
        // table literal form if needed.
        if (parser_check(parser, TOKEN_RIGHT_BRACE) ||
            parser_check(parser, TOKEN_LEFT_BRACKET) ||
            (parser_check(parser, TOKEN_IDENTIFIER) && 
             parser->current + 1 < parser->token_count && 
             parser->tokens[parser->current + 1].type == TOKEN_COLON &&
             !(parser->current + 3 < parser->token_count &&
               parser->tokens[parser->current + 2].type == TOKEN_IDENTIFIER &&
               parser->tokens[parser->current + 3].type == TOKEN_LEFT_PAREN))) {
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
    
    if (parser_match(parser, TOKEN_HASH)) {
        return parse_pragma_statement(parser);
    }

    if (parser_match(parser, TOKEN_TRY)) {
        consume(parser, TOKEN_LEFT_BRACE, "Expect '{' after 'try'.");

        Stmt** try_body = NULL;
        size_t try_count = 0;
        size_t try_cap = 0;
        while (!parser_check(parser, TOKEN_RIGHT_BRACE) && !parser_at_end(parser)) {
            if (parser_match(parser, TOKEN_NEWLINE)) continue;
            Stmt* s = parse_declaration(parser);
            if (!s) { for (size_t i = 0; i < try_count; i++) ast_release_stmt(try_body[i]); free(try_body); return NULL; }
            if (try_count >= try_cap) { try_cap = try_cap == 0 ? 8 : try_cap * 2; try_body = (Stmt**)realloc(try_body, try_cap * sizeof(Stmt*)); }
            try_body[try_count++] = s;
        }
        consume(parser, TOKEN_RIGHT_BRACE, "Expect '}' after try body.");
        while (parser_match(parser, TOKEN_NEWLINE)) {}

        consume(parser, TOKEN_CATCH, "Expect 'catch' after try block.");
        Token catch_var = consume(parser, TOKEN_IDENTIFIER, "Expect variable name after 'catch'.");
        consume(parser, TOKEN_LEFT_BRACE, "Expect '{' after catch variable.");

        Stmt** catch_body = NULL;
        size_t catch_count = 0;
        size_t catch_cap = 0;
        while (!parser_check(parser, TOKEN_RIGHT_BRACE) && !parser_at_end(parser)) {
            if (parser_match(parser, TOKEN_NEWLINE)) continue;
            Stmt* s = parse_declaration(parser);
            if (!s) { for (size_t i = 0; i < catch_count; i++) ast_release_stmt(catch_body[i]); free(catch_body); for (size_t i = 0; i < try_count; i++) ast_release_stmt(try_body[i]); free(try_body); return NULL; }
            if (catch_count >= catch_cap) { catch_cap = catch_cap == 0 ? 8 : catch_cap * 2; catch_body = (Stmt**)realloc(catch_body, catch_cap * sizeof(Stmt*)); }
            catch_body[catch_count++] = s;
        }
        consume(parser, TOKEN_RIGHT_BRACE, "Expect '}' after catch body.");
        while (parser_match(parser, TOKEN_NEWLINE)) {}

        Stmt** finally_body = NULL;
        size_t finally_count = 0;
        if (parser_match(parser, TOKEN_FINALLY)) {
            consume(parser, TOKEN_LEFT_BRACE, "Expect '{' after 'finally'.");
            size_t finally_cap = 0;
            while (!parser_check(parser, TOKEN_RIGHT_BRACE) && !parser_at_end(parser)) {
                if (parser_match(parser, TOKEN_NEWLINE)) continue;
                Stmt* s = parse_declaration(parser);
                if (!s) break;
                if (finally_count >= finally_cap) { finally_cap = finally_cap == 0 ? 8 : finally_cap * 2; finally_body = (Stmt**)realloc(finally_body, finally_cap * sizeof(Stmt*)); }
                finally_body[finally_count++] = s;
            }
            consume(parser, TOKEN_RIGHT_BRACE, "Expect '}' after finally body.");
        }

        return make_try_catch_stmt(try_body, try_count, catch_var, catch_body, catch_count,
                                   finally_body, finally_count);
    }

    if (parser_match(parser, TOKEN_THROW)) {
        Token keyword = parser_previous(parser);
        Expr* value = NULL;
        if (!parser_check(parser, TOKEN_SEMICOLON) && !parser_check(parser, TOKEN_NEWLINE) &&
            !parser_check(parser, TOKEN_RIGHT_BRACE) && !parser_at_end(parser)) {
            value = parse_expression(parser);
        }
        if (!consume_statement_terminator(parser, "Expect ';' or newline after throw")) {
            if (value) ast_release_expr(value);
            return NULL;
        }
        return make_throw_stmt(keyword, value);
    }

    if (parser_match(parser, TOKEN_YIELD)) {
        Token keyword = parser_previous(parser);
        if (!consume_statement_terminator(parser, "Expect ';' or newline after yield")) {
            return NULL;
        }
        return make_yield_stmt(keyword);
    }
    
    return parse_expression_statement(parser);
}

Stmt* parse_declaration(Parser* parser) {
    if (parser_match(parser, TOKEN_FUNC)) {
        return parse_function_declaration(parser);
    }

    if (parser_match(parser, TOKEN_SHARED)) {
        if (!parser_match(parser, TOKEN_VAR)) {
            parser_error_at_current(parser, "'shared' must be followed by 'var'");
            return NULL;
        }
        Stmt* stmt = parse_var_declaration(parser);
        if (stmt && stmt->as.var.initializer) {
            stmt->as.var.initializer = make_shared_expr(stmt->as.var.initializer);
        }
        return stmt;
    }

    if (parser_match(parser, TOKEN_VAR)) {
        return parse_var_declaration(parser);
    }
    
    if (parser_match(parser, TOKEN_ENUM)) {
        return parse_enum_declaration(parser);
    }

    if (parser_match(parser, TOKEN_STRUCT)) {
        return parse_struct_declaration(parser);
    }
    
    return parse_statement(parser);
}

// Main parsing function
ParseResult parse(MobiusState* state, TokenArray tokens) {
    ParseResult result = {};
    
    Parser parser;
    init_parser(&parser, state, tokens.tokens, tokens.count);
    
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
            Stmt** new_statements = (Stmt**)realloc(statements, capacity * sizeof(Stmt*));
            if (!new_statements) {
                parser_error_at_current(&parser, "Out of memory growing statement list");
                break;
            }
            statements = new_statements;
        }
        
        Stmt* stmt = parse_declaration(&parser);
        if (stmt) {
            statements[count++] = stmt;
        }
        
        if (parser.panic_mode) {
            synchronize(&parser);
        }
    }

    if (parser.current < parser.token_count &&
        parser.tokens[parser.current].type == TOKEN_ERROR &&
        parser.tokens[parser.current].identifier) {
        parser.panic_mode = false;
        parser_error(&parser, parser.tokens[parser.current], parser.tokens[parser.current].identifier);
    }
    
    result.statements = statements;
    result.count = count;
    result.had_error = parser.had_error;
    
    return result;
}

// Function declaration parsing: func name(param1, param2) { ... }
Stmt* parse_function_declaration(Parser* parser) {
    if (!parser_check(parser, TOKEN_IDENTIFIER)) {
        parser_error_at_current(parser, "Expected function name");
        return NULL;
    }
    
    Token name = parser_advance(parser);
    
    consume(parser, TOKEN_LEFT_PAREN, "Expected '(' after function name");
    
    Token* params = NULL;
    ValueType* param_types = NULL;
    size_t param_count = 0;
    size_t param_capacity = 0;
    bool has_any_type = false;
    
    if (!parser_check(parser, TOKEN_RIGHT_PAREN)) {
        do {
            if (param_count >= param_capacity) {
                size_t new_capacity = param_capacity == 0 ? 4 : param_capacity * 2;
                Token* new_params = (Token*)realloc(params, new_capacity * sizeof(Token));
                ValueType* new_types = (ValueType*)realloc(param_types, new_capacity * sizeof(ValueType));
                if (!new_params || !new_types) {
                    free(params); free(param_types);
                    parser_error_at_current(parser, "Memory allocation failed");
                    return NULL;
                }
                params = new_params;
                param_types = new_types;
                param_capacity = new_capacity;
            }
            
            if (!parser_check(parser, TOKEN_IDENTIFIER)) {
                parser_error_at_current(parser, "Expected parameter name");
                free(params); free(param_types);
                return NULL;
            }
            
            params[param_count] = parser_advance(parser);
            
            if (parser_match(parser, TOKEN_COLON)) {
                ValueType vt = parse_type_name(parser, "in parameter type annotation");
                if ((int)vt < 0) { free(params); free(param_types); return NULL; }
                param_types[param_count] = vt;
                has_any_type = true;
            } else {
                param_types[param_count] = VAL_UNKNOWN;
            }
            param_count++;
            
            if (param_count >= 255) {
                parser_error_at_current(parser, "Cannot have more than 255 parameters");
                free(params); free(param_types);
                return NULL;
            }
        } while (parser_match(parser, TOKEN_COMMA));
    }
    
    consume(parser, TOKEN_RIGHT_PAREN, "Expected ')' after parameters");
    
    ValueType return_type = VAL_UNKNOWN;
    if (parser_match(parser, TOKEN_COLON)) {
        return_type = parse_type_name(parser, "in return type annotation");
        if ((int)return_type < 0) { free(params); free(param_types); return NULL; }
    }
    
    if (!has_any_type) { free(param_types); param_types = NULL; }
    
    consume(parser, TOKEN_LEFT_BRACE, "Expected '{' before function body");
    
    Stmt** body = NULL;
    size_t body_count = 0;
    size_t body_capacity = 0;
    
    while (!parser_check(parser, TOKEN_RIGHT_BRACE) && !parser_at_end(parser)) {
        if (parser_match(parser, TOKEN_NEWLINE)) {
            continue;
        }
        
        Stmt* stmt = parse_declaration(parser);
        if (!stmt) {
            for (size_t i = 0; i < body_count; i++) {
                ast_release_stmt(body[i]);
            }
            free(body); free(params); free(param_types);
            return NULL;
        }
        
        if (body_count >= body_capacity) {
            size_t new_capacity = body_capacity == 0 ? 8 : body_capacity * 2;
            Stmt** new_body = (Stmt**)realloc(body, new_capacity * sizeof(Stmt*));
            if (!new_body) {
                ast_release_stmt(stmt);
                for (size_t i = 0; i < body_count; i++) {
                    ast_release_stmt(body[i]);
                }
                free(body); free(params); free(param_types);
                parser_error_at_current(parser, "Memory allocation failed");
                return NULL;
            }
            body = new_body;
            body_capacity = new_capacity;
        }
        
        body[body_count++] = stmt;
    }
    
    consume(parser, TOKEN_RIGHT_BRACE, "Expected '}' after function body");
    
    return make_function_stmt(name, params, param_types, param_count, return_type, body, body_count);
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
        if (value) ast_release_expr(value);
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
    
    // Check for optional 'as' clause
    Token alias = {};
    bool has_alias = false;
    
    if (parser_match(parser, TOKEN_IDENTIFIER) && 
        parser_previous(parser).identifier && 
        strcmp(parser_previous(parser).identifier, "as") == 0) {
        // We found 'as' keyword, now expect identifier, dotted path, or string for alias
        if (parser_check(parser, TOKEN_IDENTIFIER)) {
            alias = parser_advance(parser);
            has_alias = true;
            
            // Check for dotted path (e.g., math.complex or a.b.c)
            // We'll build up the full path as a string in alias.literal.string
            if (parser_check(parser, TOKEN_DOT)) {
                // Build a dotted path string
                char path_buffer[256];
                snprintf(path_buffer, sizeof(path_buffer), "%s", alias.identifier);
                
                while (parser_match(parser, TOKEN_DOT)) {
                    if (!parser_check(parser, TOKEN_IDENTIFIER)) {
                        parser_error_at_current(parser, "Expect identifier after '.' in alias path");
                        return NULL;
                    }
                    Token part = parser_advance(parser);
                    size_t current_len = strlen(path_buffer);
                    snprintf(path_buffer + current_len, sizeof(path_buffer) - current_len, 
                             ".%s", part.identifier);
                }
                
                // Store the dotted path as a string literal in alias
                // We need to allocate and store this string
                alias.literal.string = mobius_strdup(path_buffer);
                alias.type = TOKEN_STRING;  // Mark it as a string type for later processing
            }
        } else if (parser_check(parser, TOKEN_STRING)) {
            alias = parser_advance(parser);
            has_alias = true;
        } else {
            parser_error_at_current(parser, "Expect identifier or string after 'as'");
            return NULL;
        }
    }
    
    if (!consume_statement_terminator(parser, "Expect ';' or newline after import statement")) {
        return NULL;
    }
    
    return make_import_stmt(keyword, module_name, alias, has_alias);
}

Stmt* parse_pragma_statement(Parser* parser) {
    Token hash_token = parser_previous(parser);  // The # token
    
    // Expect 'pragma' keyword (as identifier)
    if (!parser_check(parser, TOKEN_IDENTIFIER)) {
        parser_error_at_current(parser, "Expect 'pragma' after '#'");
        return NULL;
    }
    
    Token keyword_token = parser_advance(parser);
    if (!keyword_token.identifier || strcmp(keyword_token.identifier, "pragma") != 0) {
        parser_error(parser, keyword_token, "Expect 'pragma' after '#'");
        return NULL;
    }
    
    // Expect pragma name (identifier)
    if (!parser_check(parser, TOKEN_IDENTIFIER)) {
        parser_error_at_current(parser, "Expect pragma name after '#pragma'");
        return NULL;
    }
    Token name = parser_advance(parser);
    
    // Expect pragma value (identifier, string, or boolean keyword)
    Token value = {};
    if (parser_check(parser, TOKEN_IDENTIFIER)) {
        value = parser_advance(parser);
    } else if (parser_check(parser, TOKEN_STRING)) {
        value = parser_advance(parser);
    } else if (parser_check(parser, TOKEN_TRUE) || parser_check(parser, TOKEN_FALSE)) {
        value = parser_advance(parser);
    } else {
        parser_error_at_current(parser, "Expect value after pragma name");
        return NULL;
    }
    
    if (!consume_statement_terminator(parser, "Expect ';' or newline after pragma")) {
        return NULL;
    }
    
    return make_pragma_stmt(hash_token, name, value);
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
                cases = (SwitchCase**)realloc(cases, sizeof(SwitchCase*) * case_capacity);
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
                    body = (Stmt**)realloc(body, sizeof(Stmt*) * body_capacity);
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
    size_t pattern_capacity = 4;
    CasePattern** patterns = (CasePattern**)malloc(sizeof(CasePattern*) * pattern_capacity);
    size_t pattern_count = 0;

    patterns[pattern_count++] = parse_case_pattern(parser);

    while (parser_match(parser, TOKEN_COMMA)) {
        if (pattern_count >= pattern_capacity) {
            pattern_capacity *= 2;
            patterns = (CasePattern**)realloc(patterns, sizeof(CasePattern*) * pattern_capacity);
        }
        patterns[pattern_count++] = parse_case_pattern(parser);
    }

    Expr* guard = NULL;
    if (parser_match(parser, TOKEN_WHEN)) {
        guard = parse_expression(parser);
    }

    consume(parser, TOKEN_COLON, "Expect ':' after case pattern");
    
    Stmt** body = NULL;
    size_t body_count = 0;
    size_t body_capacity = 0;
    bool has_break = false;
    
    while (!parser_check(parser, TOKEN_CASE) && 
           !parser_check(parser, TOKEN_DEFAULT) && 
           !parser_check(parser, TOKEN_RIGHT_BRACE) && 
           !parser_at_end(parser)) {
        if (parser_match(parser, TOKEN_NEWLINE)) {
            continue;
        }
        Stmt* stmt = parse_statement(parser);
        
        if (stmt->type == STMT_BREAK) {
            has_break = true;
        }
        
        if (body_count >= body_capacity) {
            body_capacity = body_capacity == 0 ? 4 : body_capacity * 2;
            body = (Stmt**)realloc(body, sizeof(Stmt*) * body_capacity);
        }
        body[body_count++] = stmt;
    }
    
    return make_switch_case(patterns, pattern_count, guard, body, body_count, has_break);
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

        // Don't let the operand's parse consume the case's terminating ':' as a
        // method call (e.g. `case >= 100: body` must not read `100:body`).
        bool saved_suppress = parser->suppress_method_colon;
        parser->suppress_method_colon = true;
        Expr* expr = parse_expression(parser);
        parser->suppress_method_colon = saved_suppress;
        return make_expression_pattern(op, expr);
    }
    
    // Range patterns: '..' is inclusive (1..10), '...' is exclusive of the
    // end (1...10). The scanner emits '...' as a single TOKEN_DOT_DOT_DOT, so
    // accept either separator here.
    if ((parser_check(parser, TOKEN_INTEGER) || parser_check(parser, TOKEN_FLOAT) ||
         parser_check(parser, TOKEN_CHAR) || parser_check(parser, TOKEN_STRING) ||
         parser_check(parser, TOKEN_IDENTIFIER)) &&
        parser->current + 1 < parser->token_count &&
        (parser->tokens[parser->current + 1].type == TOKEN_DOT_DOT ||
         parser->tokens[parser->current + 1].type == TOKEN_DOT_DOT_DOT)) {

        Expr* start = parse_primary(parser);  // Parse the start value
        bool inclusive = parser_check(parser, TOKEN_DOT_DOT);
        if (inclusive) {
            consume(parser, TOKEN_DOT_DOT, "Expect '..' in range pattern");
        } else {
            consume(parser, TOKEN_DOT_DOT_DOT, "Expect '...' in range pattern");
        }

        Expr* end = parse_primary(parser);    // Parse the end value
        return make_range_pattern(start, end, inclusive);
    }
    
    // Simple value patterns
    if (parser_check(parser, TOKEN_INTEGER)) {
        Token token = parser_advance(parser);
        Value value = (token.literal.integer.num_type == NUM_UINT64)
            ? make_uint64_value((uint64_t)token.literal.integer.value)
            : make_int64_value(token.literal.integer.value);
        return make_value_pattern(value);
    } else if (parser_check(parser, TOKEN_FLOAT)) {
        Token token = parser_advance(parser);
        Value value = make_float_value(token.literal.float_val);
        return make_value_pattern(value);
    } else if (parser_check(parser, TOKEN_STRING)) {
        Token token = parser_advance(parser);
        Value value = make_string_value_from_cstr(parser->state, token.literal.string);
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
        if (parser->current + 1 < parser->token_count && 
            parser->tokens[parser->current + 1].type == TOKEN_DOT) {
            Token enum_name = parser_advance(parser);
            parser_advance(parser);
            Token member_name = consume(parser, TOKEN_IDENTIFIER, "Expect member name after '.'");
            Expr* enum_expr = make_enum_access_expr(enum_name, member_name);
            return make_expression_pattern(TOKEN_EQUAL_EQUAL, enum_expr);
        } else {
            Expr* expr = parse_primary(parser);
            return make_expression_pattern(TOKEN_EQUAL_EQUAL, expr);
        }
    } else if (parser_check(parser, TOKEN_LEFT_BRACKET)) {
        parser_advance(parser);
        size_t capacity = 4;
        ArrayPattern* elements = (ArrayPattern*)malloc(sizeof(ArrayPattern) * capacity);
        size_t count = 0;
        bool has_rest = false;
        char* rest_name = NULL;

        while (!parser_check(parser, TOKEN_RIGHT_BRACKET) && !parser_at_end(parser)) {
            if (count > 0) {
                consume(parser, TOKEN_COMMA, "Expect ',' between array pattern elements");
            }
            if (parser_check(parser, TOKEN_DOT_DOT_DOT)) {
                parser_advance(parser);
                has_rest = true;
                if (parser_check(parser, TOKEN_IDENTIFIER)) {
                    Token name_tok = parser_advance(parser);
                    rest_name = mobius_strdup(name_tok.identifier);
                }
                break;
            }
            if (count >= capacity) {
                capacity *= 2;
                elements = (ArrayPattern*)realloc(elements, sizeof(ArrayPattern) * capacity);
            }
            if (parser_check(parser, TOKEN_IDENTIFIER)) {
                Token name_tok = parser_advance(parser);
                elements[count].name = mobius_strdup(name_tok.identifier);
                elements[count].pattern = NULL;
                elements[count].is_rest = false;
            } else {
                parser_error_at_current(parser, "Expect identifier in array destructuring pattern");
                elements[count].name = mobius_strdup("_");
                elements[count].pattern = NULL;
                elements[count].is_rest = false;
            }
            count++;
        }
        consume(parser, TOKEN_RIGHT_BRACKET, "Expect ']' after array pattern");
        return make_array_pattern(elements, count, has_rest, rest_name);
    } else if (parser_check(parser, TOKEN_LEFT_BRACE)) {
        parser_advance(parser);
        size_t capacity = 4;
        TablePattern* fields = (TablePattern*)malloc(sizeof(TablePattern) * capacity);
        size_t count = 0;

        while (!parser_check(parser, TOKEN_RIGHT_BRACE) && !parser_at_end(parser)) {
            if (count > 0) {
                consume(parser, TOKEN_COMMA, "Expect ',' between table pattern fields");
            }
            if (count >= capacity) {
                capacity *= 2;
                fields = (TablePattern*)realloc(fields, sizeof(TablePattern) * capacity);
            }
            Token key_tok = consume(parser, TOKEN_IDENTIFIER, "Expect field name in table pattern");
            fields[count].key = mobius_strdup(key_tok.identifier);
            fields[count].pattern = NULL;
            fields[count].is_optional = false;

            if (parser_match(parser, TOKEN_COLON)) {
                Token bind_tok = consume(parser, TOKEN_IDENTIFIER, "Expect binding name after ':'");
                fields[count].bind_name = mobius_strdup(bind_tok.identifier);
            } else {
                fields[count].bind_name = NULL;
            }
            count++;
        }
        consume(parser, TOKEN_RIGHT_BRACE, "Expect '}' after table pattern");
        return make_table_pattern(fields, count, false);
    } else if (parser_check(parser, TOKEN_IS)) {
        parser_advance(parser); // consume 'is'
        ValueType value_type = parse_type_name(parser, "after 'is'");
        if ((int)value_type < 0) return make_wildcard_pattern();
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
    
    NumberType underlying_type = NUM_INT64;
    bool has_explicit_type = false;
    
    if (parser_match(parser, TOKEN_COLON)) {
        has_explicit_type = true;
        
        if (parser_match(parser, TOKEN_TYPE_INT64)) {
            underlying_type = NUM_INT64;
        } else if (parser_match(parser, TOKEN_TYPE_UINT64)) {
            underlying_type = NUM_UINT64;
        } else {
            parser_error_at_current(parser, "Expect integer type (int64 or uint64) after ':'");
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
                if (members->value) ast_release_expr(members->value);
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
            if (members->value) ast_release_expr(members->value);
            free(members);
            members = next;
        }
        return NULL;
    }
    
    return make_enum_stmt(keyword, name, underlying_type, has_explicit_type, members);
}

Stmt* parse_struct_declaration(Parser* parser) {
    Token keyword = parser_previous(parser);
    Token name = consume(parser, TOKEN_IDENTIFIER, "Expect struct name.");

    StructLayoutKind layout_kind = STRUCT_LAYOUT_NATIVE;
    if (parser_check(parser, TOKEN_IDENTIFIER)) {
        Token layout = parser_peek(parser);
        if (token_text_equals(layout, "packed")) {
            parser_advance(parser);
            layout_kind = STRUCT_LAYOUT_PACKED;
        } else if (token_text_equals(layout, "native")) {
            parser_advance(parser);
            layout_kind = STRUCT_LAYOUT_NATIVE;
        }
    }

    consume(parser, TOKEN_LEFT_BRACE, "Expect '{' before struct body.");

    StructMemberDef* members = NULL;
    size_t member_count = 0;
    if (!parse_struct_members(parser, &members, &member_count)) {
        return NULL;
    }

    consume(parser, TOKEN_RIGHT_BRACE, "Expect '}' after struct body.");
    if (!consume_statement_terminator(parser, "Expect ';' or newline after struct declaration")) {
        free_struct_members_array(members, member_count);
        return NULL;
    }

    return make_struct_stmt(keyword, name, layout_kind, members, member_count);
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
