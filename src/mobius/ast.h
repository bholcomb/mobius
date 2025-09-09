#ifndef MOBIUS_AST_H
#define MOBIUS_AST_H

#include "token.h"
#include "value.h"
#include <stddef.h>

// Forward declarations
typedef struct Expr Expr;
typedef struct Stmt Stmt;

// Expression types
typedef enum {
    EXPR_BINARY,
    EXPR_UNARY,
    EXPR_LITERAL,
    EXPR_VARIABLE,
    EXPR_ASSIGNMENT,
    EXPR_CALL,
    EXPR_GROUPING,
    EXPR_ARRAY_LITERAL,
    EXPR_ARRAY_INDEX,
    EXPR_TABLE_LITERAL,
    EXPR_TABLE_INDEX,
    EXPR_TABLE_DOT,
    EXPR_ENUM_ACCESS
} ExprType;

// Statement types
typedef enum {
    STMT_EXPRESSION,
    STMT_PRINT,
    STMT_VAR,
    STMT_BLOCK,
    STMT_IF,
    STMT_WHILE,
    STMT_FOR,
    STMT_FUNCTION,
    STMT_RETURN,
    STMT_SWITCH,
    STMT_BREAK,
    STMT_CONTINUE,
    STMT_IMPORT,
    STMT_ENUM
} StmtType;

// Forward declarations for AST structures (core types in value.h)

// Function representation (AST functions only - builtins are loaded as regular functions)
struct MobiusFunction {
    char* name;               // Function name (owned string)
    char** param_names;       // Parameter names (owned strings)
    size_t param_count;       // Parameter count
    Stmt** body;              // AST statements
    size_t body_count;        // Number of statements in body
    struct Environment* closure;  // Lexical scope
    int ref_count;            // Reference counter for memory management
};

// Table entry for hash table
struct TableEntry {
    Value key;
    Value value;
    struct TableEntry* next;  // For collision chaining (unused in Robin Hood)
    bool is_occupied;
    uint32_t distance;       // Distance from ideal position (for Robin Hood hashing)
};

// Table structure - pure hash table
struct Table {
    TableEntry* entries;      // Hash table entries
    size_t size;             // Number of key-value pairs
    size_t capacity;         // Size of entries array
    struct Table* metatable; // For operator overloading
    int ref_count;           // Reference counting for memory management
};

// Expression structures
typedef struct {
    Expr* left;
    Token op;
    Expr* right;
} BinaryExpr;

typedef struct {
    Token op;
    Expr* right;
} UnaryExpr;

typedef struct {
    Value value;
} LiteralExpr;

typedef struct {
    Token name;
} VariableExpr;

typedef struct {
    Token name;
    Expr* value;
} AssignmentExpr;

typedef struct {
    Expr* callee;
    Token paren;        // Closing paren for error reporting
    Expr** arguments;   // Array of argument expressions
    size_t arg_count;
} CallExpr;

typedef struct {
    Expr* expression;
} GroupingExpr;

// Array literal expression [1, 2, 3]
typedef struct {
    Expr** elements;
    size_t element_count;
} ArrayLiteralExpr;

// Array indexing expression arr[index]
typedef struct {
    Expr* array;
    Expr* index;
} ArrayIndexExpr;

// Table key-value pair for table literals
typedef struct {
    Expr* key;    // Can be NULL for computed indices
    Expr* value;
    bool is_computed_key;  // true if key is [expression]
} TablePair;

typedef struct {
    TablePair* pairs;
    size_t pair_count;
} TableLiteralExpr;

typedef struct {
    Expr* table;
    Expr* index;
} TableIndexExpr;

typedef struct {
    Expr* table;
    Token key;  // Identifier after the dot
} TableDotExpr;

// Enum access expression (EnumName.MEMBER)
typedef struct {
    Token enum_name;    // The enum name
    Token member_name;  // The member name after the dot
} EnumAccessExpr;

// Main expression structure
struct Expr {
    ExprType type;
    int ref_count;          // Reference counter for memory management
    union {
        BinaryExpr binary;
        UnaryExpr unary;
        LiteralExpr literal;
        VariableExpr variable;
        AssignmentExpr assignment;
        CallExpr call;
        GroupingExpr grouping;
        ArrayLiteralExpr array_literal;
        ArrayIndexExpr array_index;
        TableLiteralExpr table_literal;
        TableIndexExpr table_index;
        TableDotExpr table_dot;
        EnumAccessExpr enum_access;
    } as;
};

// Statement structures
typedef struct {
    Expr* expression;
} ExpressionStmt;

typedef struct {
    Expr* expression;
} PrintStmt;

typedef struct {
    Token name;
    Expr* initializer;  // Can be NULL for uninitialized variables
    NumberInfo type_hint; // Optional type annotation
} VarStmt;

typedef struct {
    Stmt** statements;
    size_t count;
} BlockStmt;

typedef struct {
    Expr* condition;
    Stmt* then_branch;
    Stmt* else_branch;  // Can be NULL
} IfStmt;

typedef struct {
    Expr* condition;
    Stmt* body;
} WhileStmt;

typedef struct {
    Stmt* initializer;  // Can be NULL
    Expr* condition;    // Can be NULL
    Expr* increment;    // Can be NULL
    Stmt* body;
} ForStmt;

typedef struct {
    Token name;
    Token* params;      // Array of parameter tokens
    size_t param_count;
    Stmt** body;        // Array of statements in function body
    size_t body_count;
} FunctionStmt;


typedef struct {
    Token keyword;      // The 'return' token for error reporting
    Expr* value;        // Can be NULL for bare return
} ReturnStmt;

// Pattern types for case matching
typedef enum {
    PATTERN_VALUE,             // Literal value: case 42:
    PATTERN_EXPRESSION,        // Expression: case >= 10:
    PATTERN_RANGE,             // Range: case 1..10:
    PATTERN_TYPE,              // Type: case type(string):
    PATTERN_ARRAY,             // Array destructuring: case [x, y]:
    PATTERN_TABLE,             // Table destructuring: case {name, age}:
    PATTERN_WILDCARD,          // Wildcard: case _:
} PatternType;

// Forward declarations for pattern structures
typedef struct CasePattern CasePattern;

// Array pattern element
typedef struct ArrayPattern {
    char* name;                // Variable name to bind (NULL for literals)
    CasePattern* pattern;      // Nested pattern (NULL for simple binding)
    bool is_rest;             // Whether this is a ...rest element
} ArrayPattern;

// Table pattern field
typedef struct TablePattern {
    char* key;                // Field key
    char* bind_name;          // Variable name to bind (NULL to use key)
    CasePattern* pattern;     // Nested pattern (NULL for simple binding)
    bool is_optional;         // Whether field is optional
} TablePattern;

// Pattern matching structure
struct CasePattern {
    PatternType type;
    union {
        Value literal;          // For PATTERN_VALUE
        struct {
            TokenType op;       // >=, <=, ==, !=, etc.
            Expr* expression;   // Right-hand side
        } expr_pattern;
        struct {
            Expr* start;        // Start of range
            Expr* end;          // End of range
            bool inclusive;     // Whether end is inclusive
        } range_pattern;
        struct {
            ValueType value_type; // Type to match
        } type_pattern;
        struct {
            ArrayPattern* elements;
            size_t element_count;
            bool has_rest;      // Has ...rest pattern
            char* rest_name;    // Name for rest elements
        } array_pattern;
        struct {
            TablePattern* fields;
            size_t field_count;
            bool is_exhaustive; // Whether all fields must match
        } table_pattern;
    } as;
};

// Individual case clause
typedef struct {
    CasePattern** patterns;     // Array of patterns to match
    size_t pattern_count;
    Expr* guard;               // Optional guard expression (when clause)
    Stmt** body;               // Case body statements
    size_t body_count;
    bool has_break;            // Whether case has explicit break
} SwitchCase;

// Switch statement AST node
typedef struct {
    Expr* discriminant;         // The value being switched on
    SwitchCase** cases;         // Array of case clauses
    size_t case_count;
    Stmt** default_body;        // Default case body (optional)
    size_t default_body_count;
} SwitchStmt;

// Break statement
typedef struct {
    Token keyword;              // The 'break' token for error reporting
} BreakStmt;

// Continue statement
typedef struct {
    Token keyword;              // The 'continue' token for error reporting
} ContinueStmt;

// Import statement
typedef struct {
    Token keyword;              // The 'import' token for error reporting
    Token module_name;          // Module name string literal
} ImportStmt;

// Enum member definition
typedef struct EnumMemberDef {
    Token name;                    // Member name token
    Expr* value;                   // Optional explicit value (can be NULL)
    struct EnumMemberDef* next;    // Linked list
} EnumMemberDef;

// Enum statement
typedef struct {
    Token keyword;                 // The 'enum' token for error reporting
    Token name;                    // Enum name
    NumericType underlying_type;   // Underlying integer type (NUM_INT32 default)
    bool has_explicit_type;        // Whether type was explicitly specified
    EnumMemberDef* members;        // Linked list of enum members
} EnumStmt;

// Main statement structure
struct Stmt {
    StmtType type;
    int ref_count;          // Reference counter for memory management
    union {
        ExpressionStmt expression;
        PrintStmt print;
        VarStmt var;
        BlockStmt block;
        IfStmt if_stmt;
        WhileStmt while_stmt;
        ForStmt for_stmt;
        FunctionStmt function;
        ReturnStmt return_stmt;
        SwitchStmt switch_stmt;
        BreakStmt break_stmt;
        ContinueStmt continue_stmt;
        ImportStmt import_stmt;
        EnumStmt enum_stmt;
    } as;
};

// AST creation functions
Expr* make_binary_expr(Expr* left, Token op, Expr* right);
Expr* make_unary_expr(Token op, Expr* right);
Expr* make_literal_expr(Value value);
Expr* make_variable_expr(Token name);
Expr* make_assignment_expr(Token name, Expr* value);
Expr* make_call_expr(Expr* callee, Token paren, Expr** arguments, size_t arg_count);
Expr* make_grouping_expr(Expr* expression);
Expr* make_array_literal_expr(Expr** elements, size_t element_count);
Expr* make_array_index_expr(Expr* array, Expr* index);
Expr* make_table_literal_expr(TablePair* pairs, size_t pair_count);
Expr* make_table_index_expr(Expr* table, Expr* index);
Expr* make_table_dot_expr(Expr* table, Token key);
Expr* make_enum_access_expr(Token enum_name, Token member_name);

Stmt* make_expression_stmt(Expr* expression);
Stmt* make_print_stmt(Expr* expression);
Stmt* make_var_stmt(Token name, Expr* initializer, NumberInfo type_hint);
Stmt* make_block_stmt(Stmt** statements, size_t count);
Stmt* make_if_stmt(Expr* condition, Stmt* then_branch, Stmt* else_branch);
Stmt* make_while_stmt(Expr* condition, Stmt* body);
Stmt* make_for_stmt(Stmt* initializer, Expr* condition, Expr* increment, Stmt* body);
Stmt* make_function_stmt(Token name, Token* params, size_t param_count, 
                        Stmt** body, size_t body_count);
Stmt* make_return_stmt(Token keyword, Expr* value);
Stmt* make_switch_stmt(Expr* discriminant, SwitchCase** cases, size_t case_count,
                      Stmt** default_body, size_t default_body_count);
Stmt* make_break_stmt(Token keyword);
Stmt* make_continue_stmt(Token keyword);
Stmt* make_import_stmt(Token keyword, Token module_name);
Stmt* make_enum_stmt(Token keyword, Token name, NumericType underlying_type, 
                     bool has_explicit_type, EnumMemberDef* members);

// Enum helper functions
EnumMemberDef* make_enum_member(Token name, Expr* value);

// Pattern and case creation functions
CasePattern* make_value_pattern(Value literal);
CasePattern* make_expression_pattern(TokenType op, Expr* expression);
CasePattern* make_range_pattern(Expr* start, Expr* end, bool inclusive);
CasePattern* make_type_pattern(ValueType value_type);
CasePattern* make_array_pattern(ArrayPattern* elements, size_t element_count, 
                               bool has_rest, char* rest_name);
CasePattern* make_table_pattern(TablePattern* fields, size_t field_count, bool is_exhaustive);
CasePattern* make_wildcard_pattern(void);

SwitchCase* make_switch_case(CasePattern** patterns, size_t pattern_count,
                            Expr* guard, Stmt** body, size_t body_count, bool has_break);

// Pattern cleanup function
void free_case_pattern(CasePattern* pattern);

// AST Reference Counting
Expr* ast_retain_expr(Expr* expr);
void ast_release_expr(Expr* expr);
Stmt* ast_retain_stmt(Stmt* stmt);
void ast_release_stmt(Stmt* stmt);

// Helper functions for arrays and complex structures
void ast_retain_stmt_array(Stmt** stmts, size_t count);
void ast_release_stmt_array(Stmt** stmts, size_t count);
void ast_retain_expr_array(Expr** exprs, size_t count);
void ast_release_expr_array(Expr** exprs, size_t count);

// Memory management (legacy - will be replaced by reference counting)
void free_expr(Expr* expr);
void free_stmt(Stmt* stmt);

// AST printing for debugging
void print_expr(Expr* expr);
void print_stmt(Stmt* stmt);

#endif // MOBIUS_AST_H
