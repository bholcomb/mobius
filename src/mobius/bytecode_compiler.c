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
static bool compile_expression_stmt(CompilerContext* compiler, ExpressionStmt* stmt);
static bool compile_var_stmt(CompilerContext* compiler, VarStmt* stmt);
static bool compile_block_stmt(CompilerContext* compiler, BlockStmt* stmt);
static bool compile_if_stmt(CompilerContext* compiler, IfStmt* stmt);
static bool compile_while_stmt(CompilerContext* compiler, WhileStmt* stmt);
static bool compile_for_stmt(CompilerContext* compiler, ForStmt* stmt);
static bool compile_function_stmt(CompilerContext* compiler, FunctionStmt* stmt);
static bool compile_return_stmt(CompilerContext* compiler, ReturnStmt* stmt);

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

static void emit_constant(CompilerContext* compiler, Value value) {
    bytecode_chunk_write_constant(compiler->chunk, value);
    int constant_index = (int)compiler->chunk->constant_count - 1;
    emit_instruction(compiler, OP_PUSH_CONSTANT, (uint16_t)constant_index);
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
            int64_t value = expr->value.as.integer.value.i64;
            if (value >= -128 && value <= 127) {
                emit_instruction(compiler, OP_PUSH_INT8, (uint16_t)(uint8_t)value);
            } else if (value >= -32768 && value <= 32767) {
                emit_instruction(compiler, OP_PUSH_INT16, (uint16_t)value);
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
        
        case TOKEN_AND:           emit_byte(compiler, OP_LOGICAL_AND); break;
        case TOKEN_OR:            emit_byte(compiler, OP_LOGICAL_OR); break;
        
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
    // For now, treat all variables as globals
    // TODO: Implement local variable resolution
    const char* name = expr->name.identifier;
    
    // Add variable name to string pool and emit load instruction
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
    
    // For now, treat all variables as globals
    const char* name = expr->name.identifier;
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
        
        // Compile key
        if (!compile_expression(compiler, expr->pairs[i].key)) return false;
        
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

// =============================================================================
// STATEMENT COMPILATION  
// =============================================================================

bool compile_statement(CompilerContext* compiler, Stmt* stmt) {
    if (!compiler || !stmt) return false;
    
    switch (stmt->type) {
        case STMT_EXPRESSION:
            return compile_expression_stmt(compiler, &stmt->as.expression);
            
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
            
        default:
            compiler_error_at(compiler, "Unknown statement type");
            return false;
    }
}

static bool compile_expression_stmt(CompilerContext* compiler, ExpressionStmt* stmt) {
    if (!compile_expression(compiler, stmt->expression)) return false;
    
    // Don't pop the result for the last statement - it becomes the return value
    // TODO: Only pop for non-final expression statements
    emit_byte(compiler, OP_POP);
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
    // TODO: Implement for loop compilation
    compiler_error_at(compiler, "For loops not yet implemented in bytecode compiler");
    return false;
}

static bool compile_function_stmt(CompilerContext* compiler, FunctionStmt* stmt) {
    // TODO: Implement function compilation
    compiler_error_at(compiler, "Functions not yet implemented in bytecode compiler");
    return false;
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
    
    return success && !had_error;
}
