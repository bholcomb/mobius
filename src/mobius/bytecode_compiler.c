#define _GNU_SOURCE  // For strdup
#include "bytecode.h"
#include "ast.h"
#include "value.h"
#include "token.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Forward declarations for static functions
static bool compile_literal_expr(CompilerContext* compiler, LiteralExpr* expr);
static bool compile_binary_expr(CompilerContext* compiler, BinaryExpr* expr);
static bool compile_unary_expr(CompilerContext* compiler, UnaryExpr* expr);
static bool compile_variable_expr(CompilerContext* compiler, VariableExpr* expr);
static bool compile_assignment_expr(CompilerContext* compiler, AssignmentExpr* expr);
static bool compile_call_expr(CompilerContext* compiler, CallExpr* expr);
static bool compile_table_literal_expr(CompilerContext* compiler, TableLiteralExpr* expr);
static bool compile_array_literal_expr(CompilerContext* compiler, ArrayLiteralExpr* expr);
static bool compile_array_index_expr(CompilerContext* compiler, ArrayIndexExpr* expr);
static bool compile_table_index_expr(CompilerContext* compiler, TableIndexExpr* expr);
static bool compile_table_dot_expr(CompilerContext* compiler, TableDotExpr* expr);
static bool compile_expression_stmt(CompilerContext* compiler, ExpressionStmt* stmt);
static bool compile_print_stmt(CompilerContext* compiler, PrintStmt* stmt);
static bool compile_var_stmt(CompilerContext* compiler, VarStmt* stmt);
static bool compile_block_stmt(CompilerContext* compiler, BlockStmt* stmt);
static bool compile_if_stmt(CompilerContext* compiler, IfStmt* stmt);
static bool compile_while_stmt(CompilerContext* compiler, WhileStmt* stmt);
static bool compile_for_stmt(CompilerContext* compiler, ForStmt* stmt);
static bool compile_function_stmt(CompilerContext* compiler, FunctionStmt* stmt);
static bool compile_return_stmt(CompilerContext* compiler, ReturnStmt* stmt);
static bool compile_switch_stmt(CompilerContext* compiler, SwitchStmt* stmt);
static bool compile_break_stmt(CompilerContext* compiler, BreakStmt* stmt);
static bool compile_continue_stmt(CompilerContext* compiler, ContinueStmt* stmt);
static bool compile_import_stmt(CompilerContext* compiler, ImportStmt* stmt);
static bool compile_enum_stmt(CompilerContext* compiler, EnumStmt* stmt);
static bool compile_enum_access_expr(CompilerContext* compiler, EnumAccessExpr* expr);

// =============================================================================
// COMPILER CONTEXT MANAGEMENT
// =============================================================================

CompilerContext* compiler_create(BytecodeChunk* chunk, MobiusFunction* function) {
    CompilerContext* compiler = malloc(sizeof(CompilerContext));
    if (!compiler) return NULL;
    
    compiler->chunk = chunk;
    compiler->local_count = 0;
    compiler->upvalue_count = 0;
    compiler->scope_depth = 0;
    compiler->enclosing = NULL;
    compiler->function = function;
    compiler->had_error = false;
    compiler->panic_mode = false;
    
    // Initialize locals array
    for (int i = 0; i < 256; i++) {
        compiler->locals[i].name = NULL;
        compiler->locals[i].slot = -1;
        compiler->locals[i].is_hot = false;
        compiler->locals[i].risc_v_reg_hint = -1;
        compiler->locals[i].scope_depth = -1;
        compiler->locals[i].is_captured = false;
    }
    
    // Initialize scopes array
    for (int i = 0; i < 64; i++) {
        compiler->scopes[i].start = -1;
        compiler->scopes[i].end = -1;
        compiler->scopes[i].is_loop = false;
        compiler->scopes[i].break_count = 0;
        compiler->scopes[i].continue_count = 0;
    }
    
    return compiler;
}

void compiler_free(CompilerContext* compiler) {
    if (!compiler) return;
    
    // Free local variable names
    for (int i = 0; i < compiler->local_count; i++) {
        free(compiler->locals[i].name);
    }
    
    free(compiler);
}

static void emit_byte(CompilerContext* compiler, uint8_t byte) {
    Instruction instruction = {.opcode = byte, .reg_hint = REG_HINT_NONE, .operand = 0};
    bytecode_chunk_write(compiler->chunk, instruction);
}

static void emit_instruction(CompilerContext* compiler, Opcode opcode, uint16_t operand) {
    Instruction instruction = {.opcode = opcode, .reg_hint = REG_HINT_NONE, .operand = operand};
    bytecode_chunk_write(compiler->chunk, instruction);
}

// =============================================================================
// LOCAL VARIABLE MANAGEMENT
// =============================================================================

static int add_local(CompilerContext* compiler, const char* name) {
    if (compiler->local_count >= 256) {
        return -1; // Too many locals
    }
    
    int slot = compiler->local_count++;
    compiler->locals[slot].name = strdup(name);
    compiler->locals[slot].slot = slot;
    compiler->locals[slot].scope_depth = compiler->scope_depth;
    compiler->locals[slot].is_hot = false;
    compiler->locals[slot].risc_v_reg_hint = -1;
    compiler->locals[slot].is_captured = false;
    
    return slot;
}

static int resolve_local(CompilerContext* compiler, const char* name) {
    // Search locals from most recent to oldest
    for (int i = compiler->local_count - 1; i >= 0; i--) {
        if (compiler->locals[i].name && strcmp(compiler->locals[i].name, name) == 0) {
            return compiler->locals[i].slot;
        }
    }
    return -1; // Not found
}

// Simple global to track current function being compiled (Lua-inspired approach)
static const char* current_function_name = NULL;

// Compiler-time function registry for recursive references
typedef struct {
    const char* name;
    BytecodeFunction* function;
} CompilerFunction;

static CompilerFunction compiler_functions[256];
static int compiler_function_count = 0;

static void register_compiler_function(const char* name, BytecodeFunction* function) {
    if (compiler_function_count < 256) {
        compiler_functions[compiler_function_count].name = strdup(name);
        compiler_functions[compiler_function_count].function = function;
        compiler_function_count++;
    }
}

static BytecodeFunction* find_compiler_function(const char* name) {
    for (int i = 0; i < compiler_function_count; i++) {
        if (strcmp(compiler_functions[i].name, name) == 0) {
            return compiler_functions[i].function;
        }
    }
    return NULL;
}

static void clear_compiler_functions(void) {
    for (int i = 0; i < compiler_function_count; i++) {
        free((void*)compiler_functions[i].name);
    }
    compiler_function_count = 0;
}

static void emit_constant(CompilerContext* compiler, Value value) {
    bytecode_chunk_write_constant(compiler->chunk, value);
    int constant_index = (int)compiler->chunk->constant_count - 1;
    emit_instruction(compiler, OP_PUSH_CONSTANT, (uint16_t)constant_index);
}

static uint8_t add_constant(CompilerContext* compiler, Value value) {
    bytecode_chunk_write_constant(compiler->chunk, value);
    return (uint8_t)(compiler->chunk->constant_count - 1);
}

static uint8_t add_string_constant(CompilerContext* compiler, const char* string) {
    // Find or add the string to the string pool
    for (size_t i = 0; i < compiler->chunk->string_count; i++) {
        if (strcmp(compiler->chunk->string_pool[i], string) == 0) {
            return (uint8_t)i;
        }
    }
    
    // Add new string to pool
    if (!compiler->chunk->string_pool) {
        compiler->chunk->string_pool = malloc(sizeof(char*) * 16);
    }
    
    compiler->chunk->string_pool[compiler->chunk->string_count] = strdup(string);
    return (uint8_t)(compiler->chunk->string_count++);
}

static void compiler_error_at(CompilerContext* compiler, const char* message) {
    if (compiler->panic_mode) return;  // Suppress additional errors
    compiler->panic_mode = true;
    compiler->had_error = true;
    
    printf("Compilation error: %s\n", message);
}

// =============================================================================
// EXPRESSION COMPILATION
// =============================================================================

bool compile_expression(CompilerContext* compiler, Expr* expr) {
    if (!compiler || !expr) return false;
    
    switch (expr->type) {
        case EXPR_LITERAL:
            return compile_literal_expr(compiler, &expr->as.literal);
            
        case EXPR_BINARY:
            return compile_binary_expr(compiler, &expr->as.binary);
            
        case EXPR_UNARY:
            return compile_unary_expr(compiler, &expr->as.unary);
            
        case EXPR_VARIABLE:
            return compile_variable_expr(compiler, &expr->as.variable);
            
        case EXPR_ASSIGNMENT:
            return compile_assignment_expr(compiler, &expr->as.assignment);
            
        case EXPR_CALL:
            return compile_call_expr(compiler, &expr->as.call);
            
        case EXPR_GROUPING:
            return compile_expression(compiler, expr->as.grouping.expression);
            
        case EXPR_TABLE_LITERAL:
            return compile_table_literal_expr(compiler, &expr->as.table_literal);
            
        case EXPR_ARRAY_LITERAL:
            return compile_array_literal_expr(compiler, &expr->as.array_literal);
            
        case EXPR_ARRAY_INDEX:
            return compile_array_index_expr(compiler, &expr->as.array_index);
            
        case EXPR_TABLE_INDEX:
            return compile_table_index_expr(compiler, &expr->as.table_index);
            
        case EXPR_TABLE_DOT:
            return compile_table_dot_expr(compiler, &expr->as.table_dot);
            
        case EXPR_ENUM_ACCESS:
            return compile_enum_access_expr(compiler, &expr->as.enum_access);
            
        default:
            compiler_error_at(compiler, "Unknown expression type");
            return false;
    }
}

static bool compile_literal_expr(CompilerContext* compiler, LiteralExpr* expr) {
    switch (expr->value.type) {
        case VAL_NIL:
            emit_byte(compiler, OP_PUSH_NIL);
            break;
            
        case VAL_BOOL:
            emit_byte(compiler, expr->value.as.boolean ? OP_PUSH_TRUE : OP_PUSH_FALSE);
            break;
            
        case VAL_INTEGER: {
            // All integer literals are now NUM_INT64 by default
            int64_t value = expr->value.as.integer.value.i64;
            
            // Optimize storage for small values
            if (value >= -128 && value <= 127) {
                emit_instruction(compiler, OP_PUSH_INT8, (uint16_t)((int8_t)value));
            } else if (value >= -32768 && value <= 32767) {
                emit_instruction(compiler, OP_PUSH_INT16, (uint16_t)((int16_t)value));
            } else {
                emit_constant(compiler, expr->value);
            }
            break;
        }
        
        case VAL_FLOAT:
        case VAL_FLOAT32:
        case VAL_STRING:
        case VAL_CHAR:
            emit_constant(compiler, expr->value);
            break;
            
        default:
            compiler_error_at(compiler, "Invalid literal type");
            return false;
    }
    
    return true;
}

static bool compile_binary_expr(CompilerContext* compiler, BinaryExpr* expr) {
    // Compile operands (left first, then right for stack order)
    if (!compile_expression(compiler, expr->left)) return false;
    if (!compile_expression(compiler, expr->right)) return false;
    
    // Emit the operation
    switch (expr->op.type) {
        case TOKEN_PLUS:          emit_byte(compiler, OP_ADD); break;
        case TOKEN_MINUS:         emit_byte(compiler, OP_SUB); break;
        case TOKEN_STAR:          emit_byte(compiler, OP_MUL); break;
        case TOKEN_SLASH:         emit_byte(compiler, OP_DIV); break;
        case TOKEN_PERCENT:       emit_byte(compiler, OP_MOD); break;
        
        case TOKEN_EQUAL_EQUAL:   emit_byte(compiler, OP_EQ); break;
        case TOKEN_BANG_EQUAL:    emit_byte(compiler, OP_NE); break;
        case TOKEN_GREATER:       emit_byte(compiler, OP_GT); break;
        case TOKEN_GREATER_EQUAL: emit_byte(compiler, OP_GE); break;
        case TOKEN_LESS:          emit_byte(compiler, OP_LT); break;
        case TOKEN_LESS_EQUAL:    emit_byte(compiler, OP_LE); break;
        
        case TOKEN_AND_AND:       emit_byte(compiler, OP_LOGICAL_AND); break;
        case TOKEN_OR_OR:         emit_byte(compiler, OP_LOGICAL_OR); break;
        
        default:
            compiler_error_at(compiler, "Unknown binary operator");
            return false;
    }
    
    return true;
}

static bool compile_unary_expr(CompilerContext* compiler, UnaryExpr* expr) {
    // Compile the operand first
    if (!compile_expression(compiler, expr->right)) return false;
    
    // Emit the operation
    switch (expr->op.type) {
        case TOKEN_MINUS:
            emit_byte(compiler, OP_NEG);
            break;
            
        case TOKEN_PLUS:
            emit_byte(compiler, OP_POS);
            break;
            
        case TOKEN_BANG:
        case TOKEN_NOT:
            emit_byte(compiler, OP_LOGICAL_NOT);
            break;
            
        default:
            compiler_error_at(compiler, "Unknown unary operator");
            return false;
    }
    
    return true;
}

static bool compile_variable_expr(CompilerContext* compiler, VariableExpr* expr) {
    const char* name = expr->name.identifier;
    
    // Debug output removed for cleaner testing
    
    // First, try to resolve as local variable
    int local_slot = resolve_local(compiler, name);
    if (local_slot != -1) {
        // Variable is a local - emit OP_LOAD_LOCAL
        emit_instruction(compiler, OP_LOAD_LOCAL, (uint16_t)local_slot);
        return true;
    }
    
    // Check if this is a reference to a function being compiled (for recursion)
    BytecodeFunction* compiler_func = find_compiler_function(name);
    if (compiler_func) {
        // This is a reference to a function being compiled - treat as global
        int name_index = bytecode_chunk_add_string(compiler->chunk, name);
        if (name_index >= 0) {
            emit_instruction(compiler, OP_LOAD_GLOBAL, (uint16_t)name_index);
            return true;
        } else {
            compiler_error_at(compiler, "Failed to add function name to string pool");
            return false;
        }
    }
    
    // Not a local variable, treat as global
    int name_index = bytecode_chunk_add_string(compiler->chunk, name);
    if (name_index >= 0) {
        emit_instruction(compiler, OP_LOAD_GLOBAL, (uint16_t)name_index);
        return true;
    } else {
        compiler_error_at(compiler, "Failed to add variable name to string pool");
        return false;
    }
}

static bool compile_assignment_expr(CompilerContext* compiler, AssignmentExpr* expr) {
    // Compile the value to assign
    if (!compile_expression(compiler, expr->value)) return false;
    
    const char* name = expr->name.identifier;
    
    // First, try to resolve as local variable
    int local_slot = resolve_local(compiler, name);
    if (local_slot != -1) {
        // Variable is a local - emit OP_STORE_LOCAL
        emit_instruction(compiler, OP_STORE_LOCAL, (uint16_t)local_slot);
        return true;
    }
    
    // Not a local variable, treat as global
    int name_index = bytecode_chunk_add_string(compiler->chunk, name);
    if (name_index >= 0) {
        emit_instruction(compiler, OP_STORE_GLOBAL, (uint16_t)name_index);
        return true;
    } else {
        compiler_error_at(compiler, "Failed to add variable name to string pool");
        return false;
    }
}

static bool compile_call_expr(CompilerContext* compiler, CallExpr* expr) {
    // Compile the callee expression
    if (!compile_expression(compiler, expr->callee)) return false;
    
    // Compile arguments
    int arg_count = 0;
    for (size_t i = 0; i < expr->arg_count; i++) {
        if (!compile_expression(compiler, expr->arguments[i])) return false;
        arg_count++;
    }
    
    // Emit call instruction
    emit_instruction(compiler, OP_CALL, (uint16_t)arg_count);
    return true;
}

static bool compile_table_literal_expr(CompilerContext* compiler, TableLiteralExpr* expr) {
    // Create empty table
    emit_byte(compiler, OP_TABLE_NEW);
    
    // Add key-value pairs
    for (size_t i = 0; i < expr->pair_count; i++) {
        // Table is on stack, duplicate it for the set operation
        emit_byte(compiler, OP_DUP);
        
        // Compile key (handle NULL keys for array-style syntax)
        if (expr->pairs[i].key == NULL) {
            // Generate implicit numeric index for array-style syntax
            Value index_value = make_integer_value(NUM_INT64, (int64_t)i);
            emit_constant(compiler, index_value);
        } else {
            // Compile explicit key expression
            if (!compile_expression(compiler, expr->pairs[i].key)) return false;
        }
        
        // Compile value  
        if (!compile_expression(compiler, expr->pairs[i].value)) return false;
        
        // Set table[key] = value (pops key and value, modifies table)
        emit_byte(compiler, OP_TABLE_SET);
    }
    
    return true;
}

static bool compile_array_literal_expr(CompilerContext* compiler, ArrayLiteralExpr* expr) {
    // Create empty array with initial capacity
    emit_instruction(compiler, OP_ARRAY_NEW, (uint16_t)expr->element_count);
    
    // Add elements
    for (size_t i = 0; i < expr->element_count; i++) {
        // Array is on stack, duplicate it for the push operation
        emit_byte(compiler, OP_DUP);
        
        // Compile element value
        if (!compile_expression(compiler, expr->elements[i])) return false;
        
        // Push element to array
        emit_byte(compiler, OP_ARRAY_PUSH);
    }
    
    return true;
}

static bool compile_array_index_expr(CompilerContext* compiler, ArrayIndexExpr* expr) {
    // Compile the container expression (array or table)
    if (!compile_expression(compiler, expr->array)) return false;
    
    // Compile the index expression  
    if (!compile_expression(compiler, expr->index)) return false;
    
    // Emit indexing instruction (works for both arrays and tables)
    emit_byte(compiler, OP_ARRAY_GET);
    
    return true;
}

static bool compile_table_index_expr(CompilerContext* compiler, TableIndexExpr* expr) {
    // Compile the table expression
    if (!compile_expression(compiler, expr->table)) return false;
    
    // Compile the index expression
    if (!compile_expression(compiler, expr->index)) return false;
    
    // Emit table get instruction (pops table and key, pushes result)
    emit_byte(compiler, OP_TABLE_GET);
    
    return true;
}

static bool compile_table_dot_expr(CompilerContext* compiler, TableDotExpr* expr) {
    // Compile the table expression
    if (!compile_expression(compiler, expr->table)) return false;
    
    // For dot access, the key is a compile-time constant string
    // Convert the token identifier to a string value
    Value key_value = make_string_value_from_cstr(expr->key.identifier);
    emit_constant(compiler, key_value);
    
    // Emit table get instruction (pops table and key, pushes result)
    emit_byte(compiler, OP_TABLE_GET);
    
    return true;
}

// =============================================================================
// STATEMENT COMPILATION  
// =============================================================================

bool compile_statement(CompilerContext* compiler, Stmt* stmt) {
    if (!compiler || !stmt) return false;
    
    switch (stmt->type) {
        case STMT_EXPRESSION:
            return compile_expression_stmt(compiler, &stmt->as.expression);
            
        case STMT_PRINT:
            return compile_print_stmt(compiler, &stmt->as.print);
            
        case STMT_VAR:
            return compile_var_stmt(compiler, &stmt->as.var);
            
        case STMT_BLOCK:
            return compile_block_stmt(compiler, &stmt->as.block);
            
        case STMT_IF:
            return compile_if_stmt(compiler, &stmt->as.if_stmt);
            
        case STMT_WHILE:
            return compile_while_stmt(compiler, &stmt->as.while_stmt);
            
        case STMT_FOR:
            return compile_for_stmt(compiler, &stmt->as.for_stmt);
            
        case STMT_FUNCTION:
            return compile_function_stmt(compiler, &stmt->as.function);
            
        case STMT_RETURN:
            return compile_return_stmt(compiler, &stmt->as.return_stmt);
            
        case STMT_SWITCH:
            return compile_switch_stmt(compiler, &stmt->as.switch_stmt);
            
        case STMT_BREAK:
            return compile_break_stmt(compiler, &stmt->as.break_stmt);
            
        case STMT_CONTINUE:
            return compile_continue_stmt(compiler, &stmt->as.continue_stmt);
            
        case STMT_IMPORT:
            return compile_import_stmt(compiler, &stmt->as.import_stmt);
            
        case STMT_ENUM:
            return compile_enum_stmt(compiler, &stmt->as.enum_stmt);
            
        default:
            compiler_error_at(compiler, "Unknown statement type");
            return false;
    }
}

static bool compile_expression_stmt(CompilerContext* compiler, ExpressionStmt* stmt) {
    if (!compile_expression(compiler, stmt->expression)) return false;
    
    // For now, always pop the result. The main compilation function will handle
    // keeping the final result on the stack for script execution.
    emit_byte(compiler, OP_POP);
    return true;
}

static bool compile_print_stmt(CompilerContext* compiler, PrintStmt* stmt) {
    // Compile the expression to print
    if (!compile_expression(compiler, stmt->expression)) return false;
    
    // Emit print instruction
    emit_byte(compiler, OP_PRINT);
    
    return true;
}

static bool compile_var_stmt(CompilerContext* compiler, VarStmt* stmt) {
    // Compile initializer if present
    if (stmt->initializer) {
        if (!compile_expression(compiler, stmt->initializer)) return false;
    } else {
        // Initialize with nil
        emit_byte(compiler, OP_PUSH_NIL);
    }
    
    // Store in global variable (for now - TODO: implement locals)
    const char* name = stmt->name.identifier;
    int name_index = bytecode_chunk_add_string(compiler->chunk, name);
    if (name_index >= 0) {
        emit_instruction(compiler, OP_STORE_GLOBAL, (uint16_t)name_index);
        return true;
    } else {
        compiler_error_at(compiler, "Failed to add variable name to string pool");
        return false;
    }
}

static bool compile_block_stmt(CompilerContext* compiler, BlockStmt* stmt) {
    // TODO: Implement proper scope management
    
    // Compile all statements in the block
    for (size_t i = 0; i < stmt->count; i++) {
        if (!compile_statement(compiler, stmt->statements[i])) return false;
    }
    
    return true;
}

static bool compile_if_stmt(CompilerContext* compiler, IfStmt* stmt) {
    // Compile condition
    if (!compile_expression(compiler, stmt->condition)) return false;
    
    // Jump to else branch if condition is false
    emit_instruction(compiler, OP_JUMP_IF_FALSE, 0);  // Placeholder address
    size_t else_jump = compiler->chunk->count - 1;
    
    // Pop the condition value since we're in the then branch
    emit_byte(compiler, OP_POP);
    
    // Compile then branch
    if (!compile_statement(compiler, stmt->then_branch)) return false;
    
    // Jump over else branch if there is one
    size_t end_jump = 0;
    if (stmt->else_branch) {
        emit_instruction(compiler, OP_JUMP, 0);  // Placeholder address
        end_jump = compiler->chunk->count - 1;
    }
    
    // Patch else jump to point here (start of else or end of if)
    compiler->chunk->instructions[else_jump].operand = (uint16_t)compiler->chunk->count;
    
    // Pop the condition value for the else path (or end of if if no else)
    emit_byte(compiler, OP_POP);
    
    // Compile else branch if present
    if (stmt->else_branch) {
        if (!compile_statement(compiler, stmt->else_branch)) return false;
        
        // Patch end jump to point here
        compiler->chunk->instructions[end_jump].operand = (uint16_t)compiler->chunk->count;
    }
    
    return true;
}

static bool compile_while_stmt(CompilerContext* compiler, WhileStmt* stmt) {
    size_t loop_start = compiler->chunk->count;
    
    // Compile condition
    if (!compile_expression(compiler, stmt->condition)) return false;
    
    // Jump out of loop if condition is false
    emit_instruction(compiler, OP_JUMP_IF_FALSE, 0);  // Placeholder
    size_t exit_jump = compiler->chunk->count - 1;
    
    // Pop the condition value since JUMP_IF_FALSE consumed it
    emit_byte(compiler, OP_POP);
    
    // Compile loop body
    if (!compile_statement(compiler, stmt->body)) return false;
    
    // Jump back to start of loop
    size_t loop_offset = compiler->chunk->count - loop_start + 1;
    emit_instruction(compiler, OP_LOOP, (uint16_t)loop_offset);
    
    // Patch exit jump to point here
    compiler->chunk->instructions[exit_jump].operand = (uint16_t)compiler->chunk->count;
    
    // Pop the condition value for the exit path
    emit_byte(compiler, OP_POP);
    
    return true;
}

static bool compile_for_stmt(CompilerContext* compiler, ForStmt* stmt) {
    // Compile the initializer if present
    if (stmt->initializer) {
        if (!compile_statement(compiler, stmt->initializer)) return false;
    }
    
    // Mark the start of the loop
    size_t loop_start = compiler->chunk->count;
    
    // Compile the condition if present (default to true if none)
    if (stmt->condition) {
        if (!compile_expression(compiler, stmt->condition)) return false;
        
        // Jump out of loop if condition is false
        emit_instruction(compiler, OP_JUMP_IF_FALSE, 0);  // Placeholder
        size_t exit_jump = compiler->chunk->count - 1;
        
        // Pop the condition value
        emit_byte(compiler, OP_POP);
        
        // Compile loop body
        if (!compile_statement(compiler, stmt->body)) return false;
        
        // Compile increment expression if present
        if (stmt->increment) {
            if (!compile_expression(compiler, stmt->increment)) return false;
            emit_byte(compiler, OP_POP);  // Discard increment result
        }
        
        // Jump back to condition check
        size_t loop_offset = compiler->chunk->count - loop_start + 1;
        emit_instruction(compiler, OP_LOOP, (uint16_t)loop_offset);
        
        // Patch exit jump to point here
        compiler->chunk->instructions[exit_jump].operand = (uint16_t)compiler->chunk->count;
        
        // Pop the condition value for the exit path
        emit_byte(compiler, OP_POP);
    } else {
        // Infinite loop - compile body and jump back
        if (!compile_statement(compiler, stmt->body)) return false;
        
        // Compile increment expression if present
        if (stmt->increment) {
            if (!compile_expression(compiler, stmt->increment)) return false;
            emit_byte(compiler, OP_POP);  // Discard increment result
        }
        
        // Jump back to start
        size_t loop_offset = compiler->chunk->count - loop_start + 1;
        emit_instruction(compiler, OP_LOOP, (uint16_t)loop_offset);
    }
    
    return true;
}

static bool compile_function_stmt(CompilerContext* compiler, FunctionStmt* stmt) {
    // Create parameter names array
    char** param_names = NULL;
    if (stmt->param_count > 0) {
        param_names = malloc(stmt->param_count * sizeof(char*));
        if (!param_names) {
            compiler_error_at(compiler, "Memory allocation failed for parameter names");
            return false;
        }
        
        for (size_t i = 0; i < stmt->param_count; i++) {
            param_names[i] = (char*)stmt->params[i].identifier;
        }
    }
    
    // Create bytecode function object
    BytecodeFunction* function = bytecode_function_create(stmt->name.identifier, param_names, stmt->param_count);
    free(param_names);  // bytecode_function_create copies the names
    
    if (!function) {
        compiler_error_at(compiler, "Failed to create bytecode function");
        return false;
    }
    
    // Compile function body to bytecode
    function->bytecode = bytecode_chunk_create();
    if (!function->bytecode) {
        bytecode_function_free(function);
        compiler_error_at(compiler, "Failed to create bytecode chunk for function");
        return false;
    }
    
    // Register function in compiler-time registry for recursive references
    register_compiler_function(function->name, function);
    
    // Create compiler context for function body
    CompilerContext* func_compiler = compiler_create(function->bytecode, NULL);
    if (!func_compiler) {
        bytecode_function_free(function);
        compiler_error_at(compiler, "Failed to create function compiler context");
        return false;
    }
    
    // Lua-inspired: Set current function name for recursive reference resolution
    const char* previous_function_name = current_function_name;
    current_function_name = function->name;
    
    // Register function parameters as local variables
    // Parameters are the first local variables (slots 0, 1, 2, ...)
    for (size_t i = 0; i < stmt->param_count; i++) {
        int slot = add_local(func_compiler, stmt->params[i].identifier);
        if (slot == -1) {
            current_function_name = previous_function_name; // Restore
            compiler_free(func_compiler);
            bytecode_function_free(function);
            compiler_error_at(compiler, "Too many function parameters");
            return false;
        }
    }
    
    // Compile each statement in the function body
    bool success = true;
    for (size_t i = 0; i < stmt->body_count; i++) {
        if (!compile_statement(func_compiler, stmt->body[i])) {
            success = false;
            break;
        }
    }
    
    // Add return nil if no explicit return
    if (success) {
        emit_byte(func_compiler, OP_PUSH_NIL);
        emit_byte(func_compiler, OP_RETURN);
    }
    
    compiler_free(func_compiler);
    
    // Restore previous function name
    current_function_name = previous_function_name;
    
    if (!success) {
        bytecode_function_free(function);
        compiler_error_at(compiler, "Failed to compile function body");
        return false;
    }
    
    // Store the function in constants and globals (runtime storage)
    int name_index = bytecode_chunk_add_string(compiler->chunk, function->name);
    if (name_index < 0) {
        bytecode_function_free(function);
        compiler_error_at(compiler, "Failed to add function name to string pool");
        return false;
    }
    
    // Create bytecode function value and add to constants
    Value func_value;
    func_value.type = VAL_BYTECODE_FUNCTION;
    func_value.as.bytecode_func = function;
    
    // Increment reference count for runtime storage
    function->ref_count++;
    
    bytecode_chunk_write_constant(compiler->chunk, func_value);
    uint16_t constant_index = (uint16_t)(compiler->chunk->constant_count - 1);
    
    // Emit instructions to load and store the function at runtime
    emit_instruction(compiler, OP_PUSH_CONSTANT, constant_index);
    emit_instruction(compiler, OP_STORE_GLOBAL, (uint16_t)name_index);
    
    return true;
}

static bool compile_return_stmt(CompilerContext* compiler, ReturnStmt* stmt) {
    if (stmt->value) {
        if (!compile_expression(compiler, stmt->value)) return false;
    } else {
        emit_byte(compiler, OP_PUSH_NIL);
    }
    
    emit_byte(compiler, OP_RETURN);
    return true;
}

static bool compile_switch_stmt(CompilerContext* compiler, SwitchStmt* stmt) {
    // For simplicity, implement switch as a series of if-else statements
    // Compile the discriminant once and store it
    if (!compile_expression(compiler, stmt->discriminant)) return false;
    
    // Array to track jump addresses for case ends
    size_t* case_end_jumps = malloc(stmt->case_count * sizeof(size_t));
    if (!case_end_jumps && stmt->case_count > 0) {
        compiler_error_at(compiler, "Memory allocation failed");
        return false;
    }
    
    // Compile each case as: if (discriminant == case_value) { body }
    for (size_t i = 0; i < stmt->case_count; i++) {
        SwitchCase* case_clause = stmt->cases[i];
        
        // For each pattern in the case (typically just one for simple cases)
        for (size_t j = 0; j < case_clause->pattern_count; j++) {
            // Duplicate discriminant for comparison
            emit_byte(compiler, OP_DUP);
            
            // Compile the pattern value (assuming it's a literal for now)
            CasePattern* pattern = case_clause->patterns[j];
            if (pattern->type == PATTERN_VALUE) {
                // Push the literal value for comparison
                emit_constant(compiler, pattern->as.literal);
                
                // Compare discriminant with case value
                emit_byte(compiler, OP_EQ);
                
                // Jump to next case if not equal
                emit_instruction(compiler, OP_JUMP_IF_FALSE, 0);
                size_t next_case_jump = compiler->chunk->count - 1;
                
                // Pop the comparison result since we're entering the case
                emit_byte(compiler, OP_POP);
                
                // Compile case body
                for (size_t k = 0; k < case_clause->body_count; k++) {
                    if (!compile_statement(compiler, case_clause->body[k])) {
                        free(case_end_jumps);
                        return false;
                    }
                }
                
                // Jump to end of switch after case execution
                emit_instruction(compiler, OP_JUMP, 0);
                case_end_jumps[i] = compiler->chunk->count - 1;
                
                // Patch the next case jump to point here
                compiler->chunk->instructions[next_case_jump].operand = (uint16_t)compiler->chunk->count;
                
                // Pop the comparison result for the non-matching path
                emit_byte(compiler, OP_POP);
            }
        }
    }
    
    // If we have a default case, compile it here
    if (stmt->default_body_count > 0) {
        for (size_t i = 0; i < stmt->default_body_count; i++) {
            if (!compile_statement(compiler, stmt->default_body[i])) {
                free(case_end_jumps);
                return false;
            }
        }
    }
    
    // Pop the discriminant value (it's still on the stack)
    emit_byte(compiler, OP_POP);
    
    // Patch all case end jumps to point here
    for (size_t i = 0; i < stmt->case_count; i++) {
        if (case_end_jumps[i] > 0) {
            compiler->chunk->instructions[case_end_jumps[i]].operand = (uint16_t)compiler->chunk->count;
        }
    }
    
    free(case_end_jumps);
    return true;
}

static bool compile_break_stmt(CompilerContext* compiler, BreakStmt* stmt) {
    // For simplicity, implement break as a no-op for now
    // In a more complete implementation, this would jump to the end of the loop/switch
    (void)compiler;  // Suppress unused parameter warning
    (void)stmt;      // Suppress unused parameter warning
    return true;
}

static bool compile_continue_stmt(CompilerContext* compiler, ContinueStmt* stmt) {
    // For simplicity, implement continue as a no-op for now
    // In a more complete implementation, this would jump to the loop condition
    (void)compiler;  // Suppress unused parameter warning
    (void)stmt;      // Suppress unused parameter warning
    return true;
}

static bool compile_import_stmt(CompilerContext* compiler, ImportStmt* stmt) {
    // For the bytecode backend, we need to handle imports at runtime
    // Create a string constant for the module name and call a builtin import function
    
    // Push the module name as a string constant
    // The module name is already parsed and stored in the token's literal.string field
    const char* module_name = stmt->module_name.literal.string;
    if (!module_name) {
        compiler_error_at(compiler, "Invalid module name - null string");
        return false;
    }
    
    // Load the import builtin function from globals
    int import_index = bytecode_chunk_add_string(compiler->chunk, "__import");
    if (import_index < 0) {
        compiler_error_at(compiler, "Failed to add import function name to string pool");
        return false;
    }
    emit_instruction(compiler, OP_LOAD_GLOBAL, (uint16_t)import_index);
    
    // Then push the module name argument
    Value module_name_value = make_string_value_from_cstr(module_name);
    emit_constant(compiler, module_name_value);
    
    // Call the import function with 1 argument (module name)
    emit_instruction(compiler, OP_CALL, 1);
    
    // Pop the result (import returns nil)
    emit_byte(compiler, OP_POP);
    
    return true;
}

// =============================================================================
// TOP-LEVEL COMPILATION
// =============================================================================

bool compile_ast_to_bytecode(Stmt** statements, size_t count, BytecodeChunk* chunk) {
    if (!statements || !chunk) return false;
    
    CompilerContext* compiler = compiler_create(chunk, NULL);
    if (!compiler) return false;
    
    bool success = true;
    
    // Compile all statements
    for (size_t i = 0; i < count; i++) {
        // For the last statement, if it's an expression statement, don't pop its result
        if (i == count - 1 && statements[i]->type == STMT_EXPRESSION) {
            // Compile the expression directly without popping
            if (!compile_expression(compiler, statements[i]->as.expression.expression)) {
                success = false;
                break;
            }
        } else {
            if (!compile_statement(compiler, statements[i])) {
                success = false;
                break;
            }
        }
    }
    
    // Add final return
    if (success) {
        emit_byte(compiler, OP_RETURN);
    }
    
    bool had_error = compiler->had_error;
    compiler_free(compiler);
    
    // Clean up compiler function registry
    clear_compiler_functions();
    
    return success && !had_error;
}

// =============================================================================
// ENUM COMPILATION IMPLEMENTATION
// =============================================================================

static bool compile_enum_stmt(CompilerContext* compiler, EnumStmt* stmt) {
    if (!compiler || !stmt) return false;
    
    // Create the enum definition
    const char* enum_name = stmt->name.identifier;
    if (!enum_name) {
        compiler_error_at(compiler, "Invalid enum name");
        return false;
    }
    
    EnumDefinition* enum_def = enum_definition_create(enum_name, stmt->underlying_type);
    if (!enum_def) {
        compiler_error_at(compiler, "Failed to create enum definition");
        return false;
    }
    
    // Process enum members
    EnumMemberDef* member = stmt->members;
    while (member) {
        const char* member_name = member->name.identifier;
        if (!member_name) {
            enum_definition_release(enum_def);
            compiler_error_at(compiler, "Invalid enum member name");
            return false;
        }
        
        // Evaluate member value if provided
        if (member->value) {
            // For bytecode compilation, we need to evaluate the expression at compile time
            // This is a simplified version - in a full implementation, you'd want
            // constant folding for compile-time evaluation
            compiler_error_at(compiler, "Computed enum values not yet supported in bytecode mode");
            enum_definition_release(enum_def);
            return false;
        } else {
            // Auto-assign value
            enum_definition_add_auto_member(enum_def, member_name);
        }
        
        member = member->next;
    }
    
    // Add enum definition to bytecode chunk
    if (!compiler->chunk->enum_definitions) {
        compiler->chunk->enum_definitions = malloc(sizeof(EnumDefinition*) * 16);
        if (!compiler->chunk->enum_definitions) {
            enum_definition_release(enum_def);
            compiler_error_at(compiler, "Failed to allocate enum definitions array");
            return false;
        }
    }
    
    size_t enum_index = compiler->chunk->enum_count;
    compiler->chunk->enum_definitions[enum_index] = enum_def;
    compiler->chunk->enum_count++;
    
    // Emit enum definition instruction
    emit_byte(compiler, OP_ENUM_DEF);
    emit_byte(compiler, (uint8_t)enum_index);
    
    // Store enum definition in global scope with special naming
    char enum_var_name[256];
    snprintf(enum_var_name, sizeof(enum_var_name), "__enum_%s", enum_name);
    
    // Add to constants pool
    Value enum_value = make_userdata_value(enum_def, NULL, "enum_definition", sizeof(EnumDefinition));
    uint8_t constant_index = add_constant(compiler, enum_value);
    
    // Store as global variable
    uint8_t name_index = add_string_constant(compiler, enum_var_name);
    emit_byte(compiler, OP_PUSH_CONSTANT);
    emit_byte(compiler, constant_index);
    emit_byte(compiler, OP_ENUM_STORE);
    emit_byte(compiler, name_index);
    
    return true;
}

static bool compile_enum_access_expr(CompilerContext* compiler, EnumAccessExpr* expr) {
    if (!compiler || !expr) return false;
    
    const char* enum_name = expr->enum_name.identifier;
    const char* member_name = expr->member_name.identifier;
    
    if (!enum_name || !member_name) {
        compiler_error_at(compiler, "Invalid enum access");
        return false;
    }
    
    // Find the enum definition in the chunk
    EnumDefinition* enum_def = NULL;
    
    for (size_t i = 0; i < compiler->chunk->enum_count; i++) {
        if (strcmp(compiler->chunk->enum_definitions[i]->name, enum_name) == 0) {
            enum_def = compiler->chunk->enum_definitions[i];
            break;
        }
    }
    
    if (!enum_def) {
        compiler_error_at(compiler, "Undefined enum");
        return false;
    }
    
    // Find the member in the enum
    EnumMember* member = enum_definition_find_member(enum_def, member_name);
    if (!member) {
        compiler_error_at(compiler, "Undefined enum member");
        return false;
    }
    
    // Create enum value and emit as constant
    Value enum_value = make_enum_value(enum_def, member->value);
    uint8_t constant_index = add_constant(compiler, enum_value);
    
    // Emit constant load instruction
    emit_byte(compiler, OP_PUSH_CONSTANT);
    emit_byte(compiler, constant_index);
    
    return true;
}
