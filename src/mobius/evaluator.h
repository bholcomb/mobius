#ifndef MOBIUS_EVALUATOR_H
#define MOBIUS_EVALUATOR_H

#include "ast.h"
#include "environment.h"
#include "types.h"
#include "stack_trace.h"
#include <stdbool.h>

// Forward declaration for plugin system
typedef struct ModuleRegistry ModuleRegistry;


// Global type checking configuration (TypeCheckConfig defined in types.h)
extern TypeCheckConfig global_type_config;

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
    StackTrace* stack_trace;   // Call stack at time of error
} RuntimeError;

// Evaluation result
typedef struct {
    Value value;        // For traditional AST evaluation
    int return_count;   // Number of values pushed onto stack (for library functions)
    bool has_error;
    bool has_returned;  // Flag to indicate if a return statement was executed
    bool has_break;     // Flag to indicate if a break statement was executed
    bool has_continue;  // Flag to indicate if a continue statement was executed
    RuntimeError error;
} EvalResult;

// Library function type (for builtin function interface)
// Uses EvalResult.return_count to indicate number of values pushed onto stack
// Uses EvalResult.has_error and EvalResult.error for error handling
typedef EvalResult (*LibraryFunction)(Environment* env, int arg_count);

// Main evaluation functions (all use stack-based calling convention)
EvalResult evaluate_expr(Expr* expr, Environment* env);
EvalResult evaluate_stmt(Stmt* stmt, Environment* env);
EvalResult evaluate_program(Stmt** statements, size_t count, Environment* env);

// Plugin-aware evaluation
EvalResult evaluate_stmt_with_registry(Stmt* stmt, Environment* env, ModuleRegistry* registry);
EvalResult evaluate_program_with_registry(Stmt** statements, size_t count, Environment* env, ModuleRegistry* registry);

// Expression evaluation
EvalResult eval_binary_expr(BinaryExpr* expr, Environment* env);
EvalResult eval_unary_expr(UnaryExpr* expr, Environment* env);
EvalResult eval_literal_expr(LiteralExpr* expr, Environment* env);
EvalResult eval_variable_expr(VariableExpr* expr, Environment* env);
EvalResult eval_assignment_expr(AssignmentExpr* expr, Environment* env);
EvalResult eval_call_expr(CallExpr* expr, Environment* env);
EvalResult eval_grouping_expr(GroupingExpr* expr, Environment* env);
EvalResult eval_table_literal_expr(TableLiteralExpr* expr, Environment* env);
EvalResult eval_table_index_expr(TableIndexExpr* expr, Environment* env);
EvalResult eval_table_dot_expr(TableDotExpr* expr, Environment* env);
EvalResult eval_array_literal_expr(ArrayLiteralExpr* expr, Environment* env);
EvalResult eval_array_index_expr(ArrayIndexExpr* expr, Environment* env);
EvalResult eval_enum_access_expr(EnumAccessExpr* expr, Environment* env);
EvalResult eval_increment_expr(IncrementExpr* expr, Environment* env);
EvalResult eval_increment_expr_stack(IncrementExpr* expr, Environment* env);

// Statement evaluation
EvalResult eval_expression_stmt(ExpressionStmt* stmt, Environment* env);
EvalResult eval_var_stmt(VarStmt* stmt, Environment* env);
EvalResult eval_block_stmt(BlockStmt* stmt, Environment* env);
EvalResult eval_if_stmt(IfStmt* stmt, Environment* env);
EvalResult eval_while_stmt(WhileStmt* stmt, Environment* env);
EvalResult eval_for_stmt(ForStmt* stmt, Environment* env);

// Note: Built-in functions moved to stdlib.h/stdlib.c

// Plugin-aware function management
LibraryFunction lookup_builtin(const char* name);
LibraryFunction lookup_plugin_function(ModuleRegistry* registry, const char* name);
LibraryFunction lookup_qualified_plugin_function(ModuleRegistry* registry, 
                                                const char* module_name, 
                                                const char* function_name);

// Global module registry management
void set_global_module_registry(ModuleRegistry* registry);
ModuleRegistry* get_global_module_registry(void);

// Utility functions
EvalResult make_success(int return_count);        // For library functions using stack-based returns
EvalResult make_success_with_value(Value value);  // For AST evaluation that returns values traditionally
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
EvalResult divide_values(Value left, Value right, int line, int column);
EvalResult modulo_values(Value left, Value right, int line, int column);

// Comparison operations
EvalResult compare_values(Value left, Value right, TokenType op);
EvalResult logical_and(Value left, Value right);
EvalResult logical_or(Value left, Value right);
EvalResult logical_not(Value value);

// Enhanced error reporting
void print_runtime_error(RuntimeError error);
void print_runtime_error_with_context(RuntimeError error, const char* filename);
const char* error_category_name(ErrorCategory category);
const char* get_error_suggestion(ErrorCategory category);

// Source context management for better error reporting
void set_source_context(const char* source);
const char* get_source_context(void);
const char* extract_source_line(const char* source, int line_number);


// Enhanced error creation with source line extraction
EvalResult make_error_with_source(const char* message, int line, int column);
EvalResult make_error_detailed_with_source(const char* message, const char* suggestion, 
                                          ErrorCategory category, int line, int column,
                                          const char* function_name);

// User-defined function support
EvalResult eval_function_stmt(FunctionStmt* stmt, Environment* env);
EvalResult eval_return_stmt(ReturnStmt* stmt, Environment* env);
EvalResult call_user_function(MobiusFunction* function, Expr** arguments, size_t arg_count, Environment* env);

// Switch statement support
EvalResult eval_switch_stmt(SwitchStmt* stmt, Environment* env);
EvalResult eval_break_stmt(BreakStmt* stmt, Environment* env);
EvalResult eval_continue_stmt(ContinueStmt* stmt, Environment* env);
EvalResult eval_import_stmt(ImportStmt* stmt, Environment* env);
EvalResult eval_pragma_stmt(PragmaStmt* stmt, Environment* env);
EvalResult eval_enum_stmt(EnumStmt* stmt, Environment* env);

#endif // MOBIUS_EVALUATOR_H
