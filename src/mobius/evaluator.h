#ifndef MOBIUS_EVALUATOR_H
#define MOBIUS_EVALUATOR_H

#include "ast.h"
#include "environment.h"
#include <stdbool.h>

// Forward declaration for plugin system
typedef struct ModuleRegistry ModuleRegistry;

// Error categories for better reporting
typedef enum {
    ERROR_RUNTIME,      // General runtime errors
    ERROR_TYPE,         // Type mismatches
    ERROR_UNDEFINED,    // Undefined variables/functions
    ERROR_ARGUMENT,     // Wrong number of arguments
    ERROR_DIVISION,     // Division by zero
    ERROR_MEMORY,       // Memory allocation failures
    ERROR_RETURN        // Return outside function
} ErrorCategory;

// Enhanced runtime error structure
typedef struct {
    const char* message;
    const char* suggestion;    // Optional suggestion for fixing
    ErrorCategory category;
    int line;
    int column;
    const char* function_name; // Function where error occurred
    const char* source_line;   // The actual source code line
} RuntimeError;

// Evaluation result
typedef struct {
    Value value;
    bool has_error;
    RuntimeError error;
} EvalResult;

// Built-in function type
typedef EvalResult (*BuiltinFunction)(Value* args, size_t arg_count);

// Built-in function entry
typedef struct {
    const char* name;
    BuiltinFunction function;
    size_t arity;  // Expected number of arguments (-1 for variadic)
} BuiltinEntry;

// Main evaluation functions
EvalResult evaluate_expr(Expr* expr, Environment* env);
EvalResult evaluate_stmt(Stmt* stmt, Environment* env);
EvalResult evaluate_program(Stmt** statements, size_t count, Environment* env);

// Plugin-aware evaluation
EvalResult evaluate_expr_with_registry(Expr* expr, Environment* env, ModuleRegistry* registry);
EvalResult evaluate_stmt_with_registry(Stmt* stmt, Environment* env, ModuleRegistry* registry);
EvalResult evaluate_program_with_registry(Stmt** statements, size_t count, Environment* env, ModuleRegistry* registry);

// Expression evaluation
EvalResult eval_binary_expr(BinaryExpr* expr, Environment* env);
EvalResult eval_unary_expr(UnaryExpr* expr, Environment* env);
EvalResult eval_literal_expr(LiteralExpr* expr, Environment* env);
EvalResult eval_variable_expr(VariableExpr* expr, Environment* env);
EvalResult eval_assignment_expr(AssignmentExpr* expr, Environment* env);
EvalResult eval_call_expr(CallExpr* expr, Environment* env);
EvalResult eval_call_expr_with_registry(CallExpr* expr, Environment* env, ModuleRegistry* registry);
EvalResult eval_grouping_expr(GroupingExpr* expr, Environment* env);
EvalResult eval_table_literal_expr(TableLiteralExpr* expr, Environment* env);
EvalResult eval_table_index_expr(TableIndexExpr* expr, Environment* env);
EvalResult eval_table_dot_expr(TableDotExpr* expr, Environment* env);

// Statement evaluation
EvalResult eval_expression_stmt(ExpressionStmt* stmt, Environment* env);
EvalResult eval_var_stmt(VarStmt* stmt, Environment* env);
EvalResult eval_block_stmt(BlockStmt* stmt, Environment* env);
EvalResult eval_if_stmt(IfStmt* stmt, Environment* env);
EvalResult eval_while_stmt(WhileStmt* stmt, Environment* env);
EvalResult eval_for_stmt(ForStmt* stmt, Environment* env);

// Note: Built-in functions moved to stdlib.h/stdlib.c

// Plugin-aware function management
BuiltinFunction lookup_builtin(const char* name);
BuiltinFunction lookup_plugin_function(ModuleRegistry* registry, const char* name);
BuiltinFunction lookup_qualified_plugin_function(ModuleRegistry* registry, 
                                                const char* module_name, 
                                                const char* function_name);

// Global module registry management
void set_global_module_registry(ModuleRegistry* registry);
ModuleRegistry* get_global_module_registry(void);

// Utility functions
EvalResult make_success(Value value);
EvalResult make_error(const char* message, int line, int column);
EvalResult make_error_detailed(const char* message, const char* suggestion, 
                              ErrorCategory category, int line, int column,
                              const char* function_name, const char* source_line);
bool is_error(EvalResult result);

// Type conversion and checking
Value convert_to_string(Value value);
Value convert_to_number(Value value);
bool are_types_compatible(ValueType a, ValueType b);

// Arithmetic operations
EvalResult add_values(Value left, Value right);
EvalResult subtract_values(Value left, Value right);
EvalResult multiply_values(Value left, Value right);
EvalResult divide_values(Value left, Value right);
EvalResult modulo_values(Value left, Value right);

// Comparison operations
EvalResult compare_values(Value left, Value right, TokenType operator);
EvalResult logical_and(Value left, Value right);
EvalResult logical_or(Value left, Value right);
EvalResult logical_not(Value value);

// Enhanced error reporting
void print_runtime_error(RuntimeError error);
void print_runtime_error_with_context(RuntimeError error, const char* filename);
const char* error_category_name(ErrorCategory category);
const char* get_error_suggestion(ErrorCategory category);

// User-defined function support
EvalResult eval_function_stmt(FunctionStmt* stmt, Environment* env);
EvalResult eval_return_stmt(ReturnStmt* stmt, Environment* env);
EvalResult call_user_function(MobiusFunction* function, Expr** arguments, size_t arg_count, Environment* env);

#endif // MOBIUS_EVALUATOR_H
