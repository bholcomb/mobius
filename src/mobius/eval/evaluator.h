#ifndef MOBIUS_EVALUATOR_H
#define MOBIUS_EVALUATOR_H

#include "data/value.h"
#include "data/function.h"
#include "frontend/ast.h"
#include "state/environment.h"

#include <stdbool.h>

#include "eval/evalResult.h"

// Forward declaration for plugin system
class ModuleRegistry;
class MobiusState;
struct MobiusFunction;
class ExecutionContext;

// Main evaluation functions (all use stack-based calling convention)
EvalResult evaluate_expr(Expr* expr, Environment* env);
EvalResult evaluate_stmt(Stmt* stmt, Environment* env);
EvalResult evaluate_program(Stmt** statements, size_t count, Environment* env);

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

// Statement evaluation
EvalResult eval_expression_stmt(ExpressionStmt* stmt, Environment* env);
EvalResult eval_var_stmt(VarStmt* stmt, Environment* env);
EvalResult eval_block_stmt(BlockStmt* stmt, Environment* env);
EvalResult eval_if_stmt(IfStmt* stmt, Environment* env);
EvalResult eval_while_stmt(WhileStmt* stmt, Environment* env);
EvalResult eval_for_stmt(ForStmt* stmt, Environment* env);

// Utility functions (make_success and is_error are inline in evalResult.h)
EvalResult make_error(Environment* env, const char* message, int line, int column);
EvalResult make_error_detailed(Environment* env, const char* message, const char* suggestion, 
                              ErrorCategory category, int line, int column,
                              const char* function_name, const char* source_line);

// Type conversion and checking
Value convert_to_string(Value value);
Value convert_to_number(Value value);
bool are_types_compatible(ValueType a, ValueType b);

// Arithmetic operations
EvalResult add_values(Environment* env, const Value& left, const Value& right);
EvalResult subtract_values(Environment* env, const Value& left, const Value& right);
EvalResult multiply_values(Environment* env, const Value& left, const Value& right);
EvalResult divide_values(Environment* env, const Value& left, const Value& right, int line, int column);
EvalResult modulo_values(Environment* env, const Value& left, const Value& right, int line, int column);
EvalResult compare_values(Environment* env, const Value& left, const Value& right, TokenType op);
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

// Switch statement support
EvalResult eval_switch_stmt(SwitchStmt* stmt, Environment* env);
EvalResult eval_break_stmt(BreakStmt* stmt, Environment* env);
EvalResult eval_continue_stmt(ContinueStmt* stmt, Environment* env);

// Import statement support
EvalResult eval_import_stmt(ImportStmt* stmt, Environment* env);
EvalResult eval_pragma_stmt(PragmaStmt* stmt, Environment* env);

// Enum statement support
EvalResult eval_enum_stmt(EnumStmt* stmt, Environment* env);


#endif // MOBIUS_EVALUATOR_H
