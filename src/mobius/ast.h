#ifndef MOBIUS_AST_H
#define MOBIUS_AST_H

#include "token.h"
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

// Value types for literals
typedef enum {
    VAL_NIL,
    VAL_BOOL,
    VAL_INTEGER,
    VAL_FLOAT,
    VAL_STRING,
    VAL_CHAR,
    VAL_FUNCTION,
    VAL_TABLE
} ValueType;

// Forward declarations
typedef struct MobiusFunction MobiusFunction;
typedef struct Table Table;
typedef struct TableEntry TableEntry;

// Runtime value representation
typedef struct {
    ValueType type;
    union {
        bool boolean;
        struct {
            NumericType num_type;
            union {
                int8_t   i8;    uint8_t  u8;
                int16_t  i16;   uint16_t u16;
                int32_t  i32;   uint32_t u32;
                int64_t  i64;   uint64_t u64;
            } value;
        } integer;
        double float_val;
        char* string;
        char character;
        MobiusFunction* function;
        Table* table;
    } as;
} Value;

// Function representation for runtime
typedef struct MobiusFunction {
    Token name;
    Token* params;
    size_t param_count;
    Stmt** body;
    size_t body_count;
    struct Environment* closure;  // Lexical scope
} MobiusFunction;

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
    Token operator;
    Expr* right;
} BinaryExpr;

typedef struct {
    Token operator;
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
    union {
        ExpressionStmt expression;
        PrintStmt print;
        VarStmt var;
        BlockStmt block;
        IfStmt if_stmt;
        WhileStmt while_stmt;
        ForStmt for_stmt;
        FunctionStmt function;
        ClassStmt class;
        ReturnStmt return_stmt;
    } as;
};

// AST creation functions
Expr* make_binary_expr(Expr* left, Token operator, Expr* right);
Expr* make_unary_expr(Token operator, Expr* right);
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
Stmt* make_var_stmt(Token name, Expr* initializer);
Stmt* make_block_stmt(Stmt** statements, size_t count);
Stmt* make_if_stmt(Expr* condition, Stmt* then_branch, Stmt* else_branch);
Stmt* make_while_stmt(Expr* condition, Stmt* body);
Stmt* make_for_stmt(Stmt* initializer, Expr* condition, Expr* increment, Stmt* body);
Stmt* make_function_stmt(Token name, Token* params, size_t param_count, 
                        Stmt** body, size_t body_count);
Stmt* make_class_stmt(Token name, VariableExpr* superclass, 
                     FunctionStmt** methods, size_t method_count);
Stmt* make_return_stmt(Token keyword, Expr* value);

// Value creation functions
Value make_nil_value();
Value make_bool_value(bool value);
Value make_integer_value(NumericType type, int64_t value);
Value make_float_value(double value);
Value make_string_value(char* string);
Value make_char_value(char value);
Value make_function_value(MobiusFunction* function);
Value make_table_value(Table* table);

// Value utility functions
bool is_truthy(Value value);
bool values_equal(Value a, Value b);
void print_value(Value value);
char* value_to_string(Value value);
const char* value_type_name(ValueType type);
Value copy_value(Value value);

// Memory management
void free_expr(Expr* expr);
void free_stmt(Stmt* stmt);
void free_value(Value value);

// AST printing for debugging
void print_expr(Expr* expr);
void print_stmt(Stmt* stmt);

#endif // MOBIUS_AST_H
