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
    EXPR_TABLE_LITERAL,
    EXPR_TABLE_INDEX,
    EXPR_TABLE_DOT
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
    STMT_CLASS,
    STMT_RETURN
} StmtType;

// Forward declarations for AST structures (core types in value.h)

// Function representation for runtime  
struct MobiusFunction {
    Token name;
    Token* params;
    size_t param_count;
    Stmt** body;
    size_t body_count;
    struct Environment* closure;  // Lexical scope
};

// Table entry for hash table
struct TableEntry {
    Value key;
    Value value;
    struct TableEntry* next;  // For collision chaining
    bool is_occupied;
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
        TableLiteralExpr table_literal;
        TableIndexExpr table_index;
        TableDotExpr table_dot;
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
    TypeInfo type_hint; // Optional type annotation
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
    Token name;
    VariableExpr* superclass;  // Can be NULL
    FunctionStmt** methods;    // Array of method declarations
    size_t method_count;
} ClassStmt;

typedef struct {
    Token keyword;      // The 'return' token for error reporting
    Expr* value;        // Can be NULL for bare return
} ReturnStmt;

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
        ClassStmt class_stmt;
        ReturnStmt return_stmt;
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
Expr* make_table_literal_expr(TablePair* pairs, size_t pair_count);
Expr* make_table_index_expr(Expr* table, Expr* index);
Expr* make_table_dot_expr(Expr* table, Token key);

Stmt* make_expression_stmt(Expr* expression);
Stmt* make_print_stmt(Expr* expression);
Stmt* make_var_stmt(Token name, Expr* initializer, TypeInfo type_hint);
Stmt* make_block_stmt(Stmt** statements, size_t count);
Stmt* make_if_stmt(Expr* condition, Stmt* then_branch, Stmt* else_branch);
Stmt* make_while_stmt(Expr* condition, Stmt* body);
Stmt* make_for_stmt(Stmt* initializer, Expr* condition, Expr* increment, Stmt* body);
Stmt* make_function_stmt(Token name, Token* params, size_t param_count, 
                        Stmt** body, size_t body_count);
Stmt* make_class_stmt(Token name, VariableExpr* superclass, 
                     FunctionStmt** methods, size_t method_count);
Stmt* make_return_stmt(Token keyword, Expr* value);

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
