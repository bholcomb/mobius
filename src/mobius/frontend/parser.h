#ifndef MOBIUS_PARSER_H
#define MOBIUS_PARSER_H

#include "frontend/token.h"
#include "frontend/ast.h"
#include "frontend/scanner.h"
#include "state/mobius_state.h"

// Parser state structure
typedef struct {
    Token* tokens;          // Array of tokens from scanner
    size_t token_count;     // Total number of tokens
    size_t current;         // Current token index
    bool had_error;         // Whether an error occurred
    bool panic_mode;        // Whether we're in panic mode recovery
    MobiusState* state;     // Mobius state we're parsing in
    const char* source_name; // Source filename for error messages (may be NULL)
    bool suppress_method_colon; // When set, ':' is not parsed as a method call
                                // (used for switch comparison-case operands so
                                // `case >= 100: body` doesn't read `100:body`
                                // as a method call).
} Parser;

// Parser result structure
typedef struct {
    Stmt** statements;      // Array of parsed statements
    size_t count;           // Number of statements
    bool had_error;         // Whether parsing had errors
} ParseResult;

// Main parsing functions
ParseResult parse(MobiusState* state, TokenArray tokens);
void free_parse_result(ParseResult* result);

// Parser initialization and state management
void init_parser(Parser* parser, MobiusState* state, Token* tokens, size_t token_count);
bool parser_at_end(Parser* parser);
Token parser_peek(Parser* parser);
Token parser_previous(Parser* parser);
Token parser_advance(Parser* parser);
bool parser_check(Parser* parser, TokenType type);
bool parser_match(Parser* parser, TokenType type);
bool parser_match_any(Parser* parser, size_t count, ...);

// Error handling
void parser_error(Parser* parser, Token token, const char* message);
void parser_error_at_current(Parser* parser, const char* message);
void synchronize(Parser* parser);

// Statement parsing
Stmt* parse_statement(Parser* parser);
Stmt* parse_declaration(Parser* parser);
Stmt* parse_var_declaration(Parser* parser);
Stmt* parse_function_declaration(Parser* parser);
Stmt* parse_expression_statement(Parser* parser);
// Print statement removed - print is now a built-in function
Stmt* parse_block_statement(Parser* parser);
Stmt* parse_if_statement(Parser* parser);
Stmt* parse_while_statement(Parser* parser);
Stmt* parse_for_statement(Parser* parser);
Stmt* parse_return_statement(Parser* parser);
Stmt* parse_switch_statement(Parser* parser);
Stmt* parse_break_statement(Parser* parser);
Stmt* parse_import_statement(Parser* parser);
Stmt* parse_enum_declaration(Parser* parser);
Stmt* parse_struct_declaration(Parser* parser);

// Switch statement parsing helpers
SwitchCase* parse_switch_case(Parser* parser);
CasePattern* parse_case_pattern(Parser* parser);

// Expression parsing (recursive descent)
Expr* parse_expression(Parser* parser);
Expr* parse_assignment(Parser* parser);
Expr* parse_or(Parser* parser);
Expr* parse_and(Parser* parser);
Expr* parse_equality(Parser* parser);
Expr* parse_comparison(Parser* parser);
Expr* parse_term(Parser* parser);
Expr* parse_factor(Parser* parser);
Expr* parse_unary(Parser* parser);
Expr* parse_call(Parser* parser);
Expr* parse_primary(Parser* parser);
Expr* parse_table_literal(Parser* parser);
Expr* parse_array_literal(Parser* parser);

// Helper functions
Expr* finish_call(Parser* parser, Expr* callee);
Token consume(Parser* parser, TokenType type, const char* message);

// Operator precedence levels (from lowest to highest):
// assignment       =
// or               or
// and              and  
// equality         == !=
// comparison       > >= < <=
// term             - +
// factor           / *
// unary            ! - +
// call             . ()
// primary          literals, identifiers, grouping

#endif // MOBIUS_PARSER_H
