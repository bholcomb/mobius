#include "vm/compiler.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>

// ============================================================================
// Constructor
// ============================================================================

Compiler::Compiler(StringInternPool* pool)
    : current_(nullptr), pool_(pool) {}

// ============================================================================
// Public API
// ============================================================================

Prototype* Compiler::compile(Stmt** statements, size_t count,
                             const char* source_name) {
    FunctionState fs;
    initCompiler(nullptr, source_name);

    compileBlock(statements, count);
    emitReturn(0, 0);

    return endCompiler();
}

// ============================================================================
// Compiler state management
// ============================================================================

Compiler::FunctionState* Compiler::initCompiler(FunctionState* enclosing,
                                                const char* name) {
    auto* fs = new FunctionState();
    fs->proto = new Prototype();
    fs->enclosing = enclosing;
    if (name) fs->proto->name = name;
    fs->scope_depth = 0;
    fs->free_reg = 0;
    fs->max_reg = 0;

    current_ = fs;
    return fs;
}

Prototype* Compiler::endCompiler() {
    Prototype* proto = current_->proto;
    proto->num_registers = current_->max_reg;

    FunctionState* fs = current_;
    current_ = fs->enclosing;
    delete fs;
    return proto;
}

// ============================================================================
// Register management
// ============================================================================

int Compiler::allocReg() {
    int reg = current_->free_reg++;
    if (current_->free_reg > current_->max_reg) {
        current_->max_reg = current_->free_reg;
    }
    return reg;
}

void Compiler::allocRegs(int count) {
    current_->free_reg += count;
    if (current_->free_reg > current_->max_reg) {
        current_->max_reg = current_->free_reg;
    }
}

void Compiler::freeReg() {
    current_->free_reg--;
}

void Compiler::freeRegs(int count) {
    current_->free_reg -= count;
}

void Compiler::setFreeReg(int reg) {
    current_->free_reg = reg;
}

void Compiler::reserveRegs(int count) {
    allocRegs(count);
}

// ============================================================================
// Scope management
// ============================================================================

void Compiler::beginScope() {
    current_->scope_depth++;
}

void Compiler::endScope() {
    current_->scope_depth--;

    // Pop locals that belong to the scope we're leaving
    while (!current_->locals.empty() &&
           current_->locals.back().depth > current_->scope_depth) {

        if (current_->locals.back().is_captured) {
            int reg = (int)current_->locals.size() - 1;
            emitABC(OP_CLOSE, (uint8_t)reg, 0, 0);
        }
        current_->locals.pop_back();
        freeReg();
    }
}

// ============================================================================
// Local variable management
// ============================================================================

int Compiler::addLocal(const char* name) {
    int reg = allocReg();
    current_->locals.push_back({name, current_->scope_depth, false});
    return reg;
}

int Compiler::resolveLocal(const char* name) {
    for (int i = (int)current_->locals.size() - 1; i >= 0; i--) {
        if (current_->locals[i].name == name) {
            return i;
        }
    }
    return -1;
}

int Compiler::resolveUpvalue(FunctionState* fs, const char* name) {
    if (!fs->enclosing) return -1;

    // Check enclosing function's locals
    FunctionState* enclosing = fs->enclosing;
    for (int i = (int)enclosing->locals.size() - 1; i >= 0; i--) {
        if (enclosing->locals[i].name == name) {
            enclosing->locals[i].is_captured = true;
            return addUpvalue(fs, (uint8_t)i, true);
        }
    }

    // Check enclosing function's upvalues (recursive)
    int upvalue = resolveUpvalue(enclosing, name);
    if (upvalue != -1) {
        return addUpvalue(fs, (uint8_t)upvalue, false);
    }

    return -1;
}

int Compiler::addUpvalue(FunctionState* fs, uint8_t index, bool is_local) {
    auto& upvalues = fs->proto->upvalues;

    // Reuse existing upvalue if already captured
    for (int i = 0; i < (int)upvalues.size(); i++) {
        if (upvalues[i].index == index && upvalues[i].in_stack == is_local) {
            return i;
        }
    }

    upvalues.push_back({index, is_local});
    return (int)upvalues.size() - 1;
}

// ============================================================================
// Constant management
// ============================================================================

int Compiler::stringConstant(const char* name) {
    MobiusString* str = pool_->intern(name);
    return current_->proto->addStringConstant(str);
}

uint8_t Compiler::makeRK(int const_idx) {
    if (const_idx < 0) return 0;
    if (const_idx <= (int)RK_INDEX_MASK) {
        return MAKE_RK((uint8_t)const_idx);
    }
    int reg = allocReg();
    emitLoadK(reg, const_idx);
    return (uint8_t)reg;
}

// ============================================================================
// Code emission wrappers
// ============================================================================

int Compiler::emitABC(OpCode op, uint8_t a, uint8_t b, uint8_t c) {
    return current_->proto->emitABC(op, a, b, c, currentLine_);
}

int Compiler::emitABx(OpCode op, uint8_t a, uint16_t bx) {
    return current_->proto->emitABx(op, a, bx, currentLine_);
}

int Compiler::emitAsBx(OpCode op, uint8_t a, int sbx) {
    return current_->proto->emitAsBx(op, a, sbx, currentLine_);
}

int Compiler::emitJump() {
    return current_->proto->emitJump(0, currentLine_);
}

void Compiler::patchJump(int jump_idx) {
    current_->proto->patchJump(jump_idx, current_->proto->currentPC());
}

void Compiler::emitReturn(int first_reg, int count) {
    emitABC(OP_RETURN, (uint8_t)first_reg, (uint8_t)(count + 1), 0);
}

int Compiler::emitLoadK(int reg, int const_idx) {
    return emitABx(OP_LOADK, (uint8_t)reg, (uint16_t)const_idx);
}

// ============================================================================
// Expression compilation
// ============================================================================

int Compiler::compileExpr(Expr* expr, int dest) {
    if (!expr) return -1;

    switch (expr->type) {
        case EXPR_LITERAL:
            return compileLiteral(&expr->as.literal, dest);
        case EXPR_VARIABLE:
            return compileVariable(&expr->as.variable, dest);
        case EXPR_BINARY:
            return compileBinary(&expr->as.binary, dest);
        case EXPR_UNARY:
            return compileUnary(&expr->as.unary, dest);
        case EXPR_ASSIGNMENT:
            return compileAssignment(&expr->as.assignment, dest);
        case EXPR_CALL:
            return compileCall(&expr->as.call, dest);
        case EXPR_GROUPING:
            return compileGrouping(&expr->as.grouping, dest);
        case EXPR_ARRAY_LITERAL:
            return compileArrayLiteral(&expr->as.array_literal, dest);
        case EXPR_ARRAY_INDEX:
            return compileArrayIndex(&expr->as.array_index, dest);
        case EXPR_TABLE_LITERAL:
            return compileTableLiteral(&expr->as.table_literal, dest);
        case EXPR_TABLE_INDEX:
            return compileTableIndex(&expr->as.table_index, dest);
        case EXPR_TABLE_DOT:
            return compileTableDot(&expr->as.table_dot, dest);
        case EXPR_ENUM_ACCESS:
            return compileEnumAccess(&expr->as.enum_access, dest);
        case EXPR_INCREMENT:
        case EXPR_DECREMENT:
            return compileIncrement(&expr->as.increment, dest);
        default:
            fprintf(stderr, "Compiler: unknown expression type %d\n", expr->type);
            return -1;
    }
}

// --- Literal ---

int Compiler::compileLiteral(LiteralExpr* expr, int dest) {
    int reg = (dest >= 0) ? dest : allocReg();
    const Value& v = expr->value;

    switch (v.type) {
        case VAL_NIL:
            emitABC(OP_LOADNIL, (uint8_t)reg, 0, 0);
            break;
        case VAL_BOOL:
            emitABC(OP_LOADBOOL, (uint8_t)reg, v.as.boolean ? 1 : 0, 0);
            break;
        case VAL_INTEGER: {
            int64_t iv = v.as.integer.value.i64;
            if (iv >= -SBX16_BIAS && iv <= SBX16_BIAS) {
                emitAsBx(OP_LOADINT, (uint8_t)reg, (int)iv);
            } else {
                int ki = current_->proto->addIntConstant(iv);
                emitLoadK(reg, ki);
            }
            break;
        }
        case VAL_FLOAT32: {
            int ki = current_->proto->addFloatConstant((double)v.as.float32_val);
            emitLoadK(reg, ki);
            break;
        }
        case VAL_FLOAT64: {
            int ki = current_->proto->addFloatConstant(v.as.float64_val);
            emitLoadK(reg, ki);
            break;
        }
        case VAL_STRING: {
            int ki = current_->proto->addStringConstant(v.as.string);
            emitLoadK(reg, ki);
            break;
        }
        case VAL_CHAR: {
            int ki = current_->proto->addConstant(v);
            emitLoadK(reg, ki);
            break;
        }
        default: {
            int ki = current_->proto->addConstant(v);
            emitLoadK(reg, ki);
            break;
        }
    }
    return reg;
}

// --- Variable ---

int Compiler::compileVariable(VariableExpr* expr, int dest) {
    const char* name = expr->name.identifier;

    // Try local
    int local = resolveLocal(name);
    if (local >= 0) {
        if (dest >= 0 && dest != local) {
            emitABC(OP_MOVE, (uint8_t)dest, (uint8_t)local, 0);
            return dest;
        }
        return local;
    }

    // Try upvalue
    int upvalue = resolveUpvalue(current_, name);
    if (upvalue >= 0) {
        int reg = (dest >= 0) ? dest : allocReg();
        emitABC(OP_GETUPVAL, (uint8_t)reg, (uint8_t)upvalue, 0);
        return reg;
    }

    // Global
    int reg = (dest >= 0) ? dest : allocReg();
    int ki = stringConstant(name);
    emitABx(OP_GETGLOBAL, (uint8_t)reg, (uint16_t)ki);
    return reg;
}

// --- Binary ---

int Compiler::compileBinary(BinaryExpr* expr, int dest) {
    currentLine_ = expr->op.line;

    // Short-circuit logical operators
    if (expr->op.type == TOKEN_AND || expr->op.type == TOKEN_AND_AND) {
        return compileLogicalAnd(expr, dest);
    }
    if (expr->op.type == TOKEN_OR || expr->op.type == TOKEN_OR_OR) {
        return compileLogicalOr(expr, dest);
    }

    int reg = (dest >= 0) ? dest : allocReg();
    int save_reg = current_->free_reg;

    int left = compileExpr(expr->left);
    int right = compileExpr(expr->right);

    uint8_t rk_left = (uint8_t)left;
    uint8_t rk_right = (uint8_t)right;

    switch (expr->op.type) {
        case TOKEN_PLUS:
            emitABC(OP_ADD, (uint8_t)reg, rk_left, rk_right);
            break;
        case TOKEN_MINUS:
            emitABC(OP_SUB, (uint8_t)reg, rk_left, rk_right);
            break;
        case TOKEN_STAR:
            emitABC(OP_MUL, (uint8_t)reg, rk_left, rk_right);
            break;
        case TOKEN_SLASH:
            emitABC(OP_DIV, (uint8_t)reg, rk_left, rk_right);
            break;
        case TOKEN_PERCENT:
            emitABC(OP_MOD, (uint8_t)reg, rk_left, rk_right);
            break;
        case TOKEN_AMPERSAND:
            emitABC(OP_BAND, (uint8_t)reg, rk_left, rk_right);
            break;
        case TOKEN_PIPE:
            emitABC(OP_BOR, (uint8_t)reg, rk_left, rk_right);
            break;
        case TOKEN_CARET:
            emitABC(OP_BXOR, (uint8_t)reg, rk_left, rk_right);
            break;
        case TOKEN_LEFT_SHIFT:
            emitABC(OP_SHL, (uint8_t)reg, rk_left, rk_right);
            break;
        case TOKEN_RIGHT_SHIFT:
            emitABC(OP_SHR, (uint8_t)reg, rk_left, rk_right);
            break;

        // Comparison operators: emit comparison + conditional jump pattern
        case TOKEN_EQUAL_EQUAL: {
            emitABC(OP_EQ, 1, rk_left, rk_right);
            int jmp = emitJump();
            emitABC(OP_LOADBOOL, (uint8_t)reg, 0, 1);
            patchJump(jmp);
            emitABC(OP_LOADBOOL, (uint8_t)reg, 1, 0);
            break;
        }
        case TOKEN_BANG_EQUAL: {
            emitABC(OP_EQ, 0, rk_left, rk_right);
            int jmp = emitJump();
            emitABC(OP_LOADBOOL, (uint8_t)reg, 0, 1);
            patchJump(jmp);
            emitABC(OP_LOADBOOL, (uint8_t)reg, 1, 0);
            break;
        }
        case TOKEN_LESS: {
            emitABC(OP_LT, 1, rk_left, rk_right);
            int jmp = emitJump();
            emitABC(OP_LOADBOOL, (uint8_t)reg, 0, 1);
            patchJump(jmp);
            emitABC(OP_LOADBOOL, (uint8_t)reg, 1, 0);
            break;
        }
        case TOKEN_LESS_EQUAL: {
            emitABC(OP_LE, 1, rk_left, rk_right);
            int jmp = emitJump();
            emitABC(OP_LOADBOOL, (uint8_t)reg, 0, 1);
            patchJump(jmp);
            emitABC(OP_LOADBOOL, (uint8_t)reg, 1, 0);
            break;
        }
        case TOKEN_GREATER: {
            // a > b  =>  b < a
            emitABC(OP_LT, 1, rk_right, rk_left);
            int jmp = emitJump();
            emitABC(OP_LOADBOOL, (uint8_t)reg, 0, 1);
            patchJump(jmp);
            emitABC(OP_LOADBOOL, (uint8_t)reg, 1, 0);
            break;
        }
        case TOKEN_GREATER_EQUAL: {
            // a >= b  =>  b <= a
            emitABC(OP_LE, 1, rk_right, rk_left);
            int jmp = emitJump();
            emitABC(OP_LOADBOOL, (uint8_t)reg, 0, 1);
            patchJump(jmp);
            emitABC(OP_LOADBOOL, (uint8_t)reg, 1, 0);
            break;
        }
        default:
            fprintf(stderr, "Compiler: unknown binary operator %d\n", expr->op.type);
            break;
    }

    // Free temporaries used by left/right (restore to before we compiled them)
    setFreeReg(save_reg);
    if (dest < 0) {
        // We allocated reg ourselves, so it occupies one slot
        current_->free_reg = reg + 1;
        if (current_->free_reg > current_->max_reg)
            current_->max_reg = current_->free_reg;
    }

    return reg;
}

// --- Logical AND (short-circuit) ---

int Compiler::compileLogicalAnd(BinaryExpr* expr, int dest) {
    int reg = (dest >= 0) ? dest : allocReg();

    compileExpr(expr->left, reg);
    emitABC(OP_TEST, (uint8_t)reg, 0, 0);  // if R[reg] is false, skip next
    int jmp_false = emitJump();              // jump to end (short-circuit)
    compileExpr(expr->right, reg);
    patchJump(jmp_false);

    return reg;
}

// --- Logical OR (short-circuit) ---

int Compiler::compileLogicalOr(BinaryExpr* expr, int dest) {
    int reg = (dest >= 0) ? dest : allocReg();

    compileExpr(expr->left, reg);
    emitABC(OP_TEST, (uint8_t)reg, 0, 1);  // if R[reg] is true, skip next
    int jmp_true = emitJump();               // jump to end (short-circuit)
    compileExpr(expr->right, reg);
    patchJump(jmp_true);

    return reg;
}

// --- Unary ---

int Compiler::compileUnary(UnaryExpr* expr, int dest) {
    currentLine_ = expr->op.line;
    int reg = (dest >= 0) ? dest : allocReg();

    int save_reg = current_->free_reg;
    int operand = compileExpr(expr->right);

    switch (expr->op.type) {
        case TOKEN_MINUS:
            emitABC(OP_UNM, (uint8_t)reg, (uint8_t)operand, 0);
            break;
        case TOKEN_BANG:
        case TOKEN_NOT:
            emitABC(OP_NOT, (uint8_t)reg, (uint8_t)operand, 0);
            break;
        case TOKEN_TILDE:
            emitABC(OP_BNOT, (uint8_t)reg, (uint8_t)operand, 0);
            break;
        case TOKEN_PLUS:
            // Unary plus is a no-op; just move if needed
            if (reg != operand)
                emitABC(OP_MOVE, (uint8_t)reg, (uint8_t)operand, 0);
            break;
        default:
            fprintf(stderr, "Compiler: unknown unary operator %d\n", expr->op.type);
            break;
    }

    setFreeReg(save_reg);
    if (dest < 0) {
        current_->free_reg = reg + 1;
        if (current_->free_reg > current_->max_reg)
            current_->max_reg = current_->free_reg;
    }
    return reg;
}

// --- Assignment ---

int Compiler::compileAssignment(AssignmentExpr* expr, int dest) {
    currentLine_ = expr->name.line;
    const char* name = expr->name.identifier;

    int local = resolveLocal(name);
    if (local >= 0) {
        compileExpr(expr->value, local);
        if (dest >= 0 && dest != local) {
            emitABC(OP_MOVE, (uint8_t)dest, (uint8_t)local, 0);
            return dest;
        }
        return local;
    }

    int upvalue = resolveUpvalue(current_, name);
    if (upvalue >= 0) {
        int reg = (dest >= 0) ? dest : allocReg();
        compileExpr(expr->value, reg);
        emitABC(OP_SETUPVAL, (uint8_t)reg, (uint8_t)upvalue, 0);
        return reg;
    }

    // Global assignment
    int reg = (dest >= 0) ? dest : allocReg();
    compileExpr(expr->value, reg);
    int ki = stringConstant(name);
    emitABx(OP_SETGLOBAL, (uint8_t)reg, (uint16_t)ki);
    return reg;
}

// --- Call ---

int Compiler::compileCall(CallExpr* expr, int dest) {
    currentLine_ = expr->paren.line;
    int base = current_->free_reg;
    int func_reg = allocReg();

    // Compile the callee into func_reg
    if (expr->callee->type == EXPR_TABLE_DOT) {
        // module.func() — compile as table lookup
        compileTableDot(&expr->callee->as.table_dot, func_reg);
    } else {
        compileExpr(expr->callee, func_reg);
    }

    // Compile arguments into R[func_reg+1], R[func_reg+2], ...
    for (size_t i = 0; i < expr->arg_count; i++) {
        int arg_reg = allocReg();
        compileExpr(expr->arguments[i], arg_reg);
    }

    int nargs = (int)expr->arg_count + 1;  // B = nargs + 1
    int nresults = 2;                       // C = nresults + 1 (1 result)

    emitABC(OP_CALL, (uint8_t)func_reg, (uint8_t)nargs, (uint8_t)nresults);

    // After call, result is in func_reg. Free temporaries.
    setFreeReg(base);

    int result_reg;
    if (dest >= 0) {
        if (dest != func_reg) {
            emitABC(OP_MOVE, (uint8_t)dest, (uint8_t)func_reg, 0);
        }
        result_reg = dest;
    } else {
        result_reg = allocReg();
        if (result_reg != func_reg) {
            emitABC(OP_MOVE, (uint8_t)result_reg, (uint8_t)func_reg, 0);
        }
    }

    return result_reg;
}

// --- Grouping ---

int Compiler::compileGrouping(GroupingExpr* expr, int dest) {
    return compileExpr(expr->expression, dest);
}

// --- Array literal ---

int Compiler::compileArrayLiteral(ArrayLiteralExpr* expr, int dest) {
    int reg = (dest >= 0) ? dest : allocReg();
    emitABC(OP_NEWARRAY, (uint8_t)reg, (uint8_t)(expr->element_count & 0xFF), 0);

    for (size_t i = 0; i < expr->element_count; i++) {
        int save = current_->free_reg;
        int val_reg = compileExpr(expr->elements[i]);

        // SETTABLE array[i] = val
        int ki = current_->proto->addIntConstant((int64_t)i);
        emitABC(OP_SETTABLE, (uint8_t)reg, makeRK(ki), (uint8_t)val_reg);
        setFreeReg(save);
    }

    return reg;
}

// --- Array index ---

int Compiler::compileArrayIndex(ArrayIndexExpr* expr, int dest) {
    int reg = (dest >= 0) ? dest : allocReg();
    int save = current_->free_reg;

    int arr_reg = compileExpr(expr->array);
    int idx_reg = compileExpr(expr->index);

    emitABC(OP_GETTABLE, (uint8_t)reg, (uint8_t)arr_reg, (uint8_t)idx_reg);

    setFreeReg(save);
    if (dest < 0) {
        current_->free_reg = reg + 1;
        if (current_->free_reg > current_->max_reg)
            current_->max_reg = current_->free_reg;
    }
    return reg;
}

// --- Table literal ---

int Compiler::compileTableLiteral(TableLiteralExpr* expr, int dest) {
    int reg = (dest >= 0) ? dest : allocReg();
    emitABC(OP_NEWTABLE, (uint8_t)reg, 0, (uint8_t)(expr->pair_count & 0xFF));

    for (size_t i = 0; i < expr->pair_count; i++) {
        TablePair& pair = expr->pairs[i];
        int save = current_->free_reg;

        uint8_t rk_key;
        if (pair.key && pair.key->type == EXPR_LITERAL &&
            pair.key->as.literal.value.type == VAL_STRING) {
            int ki = current_->proto->addStringConstant(
                pair.key->as.literal.value.as.string);
            rk_key = makeRK(ki);
        } else if (pair.key) {
            int key_reg = compileExpr(pair.key);
            rk_key = (uint8_t)key_reg;
        } else {
            int ki = current_->proto->addIntConstant((int64_t)i);
            rk_key = makeRK(ki);
        }

        int val_reg = compileExpr(pair.value);
        emitABC(OP_SETTABLE, (uint8_t)reg, rk_key, (uint8_t)val_reg);

        setFreeReg(save);
    }

    return reg;
}

// --- Table index ---

int Compiler::compileTableIndex(TableIndexExpr* expr, int dest) {
    int reg = (dest >= 0) ? dest : allocReg();
    int save = current_->free_reg;

    int tbl_reg = compileExpr(expr->table);
    int idx_reg = compileExpr(expr->index);

    emitABC(OP_GETTABLE, (uint8_t)reg, (uint8_t)tbl_reg, (uint8_t)idx_reg);

    setFreeReg(save);
    if (dest < 0) {
        current_->free_reg = reg + 1;
        if (current_->free_reg > current_->max_reg)
            current_->max_reg = current_->free_reg;
    }
    return reg;
}

// --- Table dot access ---

int Compiler::compileTableDot(TableDotExpr* expr, int dest) {
    int reg = (dest >= 0) ? dest : allocReg();
    int save = current_->free_reg;

    int tbl_reg = compileExpr(expr->table);
    int ki = stringConstant(expr->key.identifier);
    uint8_t rk_key = makeRK(ki);

    emitABC(OP_GETTABLE, (uint8_t)reg, (uint8_t)tbl_reg, rk_key);

    setFreeReg(save);
    if (dest < 0) {
        current_->free_reg = reg + 1;
        if (current_->free_reg > current_->max_reg)
            current_->max_reg = current_->free_reg;
    }
    return reg;
}

// --- Enum access ---

int Compiler::compileEnumAccess(EnumAccessExpr* expr, int dest) {
    int reg = (dest >= 0) ? dest : allocReg();
    int save = current_->free_reg;

    // Compile as global lookup of __enum_<name> then member access
    char enum_var[256];
    snprintf(enum_var, sizeof(enum_var), "__enum_%s", expr->enum_name.identifier);
    const char* interned = pool_->intern(enum_var)->data;

    int enum_reg = allocReg();
    int ki = stringConstant(interned);
    emitABx(OP_GETGLOBAL, (uint8_t)enum_reg, (uint16_t)ki);

    int member_ki = stringConstant(expr->member_name.identifier);
    emitABC(OP_GETTABLE, (uint8_t)reg, (uint8_t)enum_reg, makeRK(member_ki));

    setFreeReg(save);
    if (dest < 0) {
        current_->free_reg = reg + 1;
        if (current_->free_reg > current_->max_reg)
            current_->max_reg = current_->free_reg;
    }
    return reg;
}

// --- Increment/Decrement ---

int Compiler::compileIncrement(IncrementExpr* expr, int dest) {
    currentLine_ = expr->op.line;
    const char* name = expr->name.identifier;

    int local = resolveLocal(name);
    OpCode op = expr->is_increment ? OP_INC : OP_DEC;

    if (local >= 0) {
        if (expr->is_prefix) {
            emitABC(op, (uint8_t)local, (uint8_t)local, 0);
            if (dest >= 0 && dest != local)
                emitABC(OP_MOVE, (uint8_t)dest, (uint8_t)local, 0);
            return (dest >= 0) ? dest : local;
        } else {
            int reg = (dest >= 0) ? dest : allocReg();
            emitABC(OP_MOVE, (uint8_t)reg, (uint8_t)local, 0);
            emitABC(op, (uint8_t)local, (uint8_t)local, 0);
            return reg;
        }
    }

    // Global increment: load, modify, store
    int reg = (dest >= 0) ? dest : allocReg();
    int ki = stringConstant(name);
    int tmp = allocReg();

    emitABx(OP_GETGLOBAL, (uint8_t)tmp, (uint16_t)ki);
    if (expr->is_prefix) {
        emitABC(op, (uint8_t)tmp, (uint8_t)tmp, 0);
        emitABx(OP_SETGLOBAL, (uint8_t)tmp, (uint16_t)ki);
        if (reg != tmp) emitABC(OP_MOVE, (uint8_t)reg, (uint8_t)tmp, 0);
    } else {
        emitABC(OP_MOVE, (uint8_t)reg, (uint8_t)tmp, 0);
        emitABC(op, (uint8_t)tmp, (uint8_t)tmp, 0);
        emitABx(OP_SETGLOBAL, (uint8_t)tmp, (uint16_t)ki);
    }

    freeReg(); // tmp
    return reg;
}

// ============================================================================
// Statement compilation
// ============================================================================

void Compiler::compileStmt(Stmt* stmt) {
    if (!stmt) return;

    switch (stmt->type) {
        case STMT_EXPRESSION:
            compileExpressionStmt(&stmt->as.expression);
            break;
        case STMT_PRINT:
            compilePrintStmt(&stmt->as.print);
            break;
        case STMT_VAR:
            compileVarStmt(&stmt->as.var);
            break;
        case STMT_BLOCK:
            compileBlockStmt(&stmt->as.block);
            break;
        case STMT_IF:
            compileIfStmt(&stmt->as.if_stmt);
            break;
        case STMT_WHILE:
            compileWhileStmt(&stmt->as.while_stmt);
            break;
        case STMT_FOR:
            compileForStmt(&stmt->as.for_stmt);
            break;
        case STMT_FUNCTION:
            compileFunctionStmt(&stmt->as.function);
            break;
        case STMT_RETURN:
            compileReturnStmt(&stmt->as.return_stmt);
            break;
        case STMT_SWITCH:
            compileSwitchStmt(&stmt->as.switch_stmt);
            break;
        case STMT_BREAK:
            compileBreakStmt();
            break;
        case STMT_CONTINUE:
            compileContinueStmt();
            break;
        case STMT_IMPORT:
            compileImportStmt(&stmt->as.import_stmt);
            break;
        case STMT_ENUM:
            compileEnumStmt(&stmt->as.enum_stmt);
            break;
        case STMT_PRAGMA:
            compilePragmaStmt(&stmt->as.pragma_stmt);
            break;
        default:
            fprintf(stderr, "Compiler: unknown statement type %d\n", stmt->type);
            break;
    }
}

void Compiler::compileBlock(Stmt** stmts, size_t count) {
    for (size_t i = 0; i < count; i++) {
        compileStmt(stmts[i]);
    }
}

// --- Expression statement ---

void Compiler::compileExpressionStmt(ExpressionStmt* stmt) {
    int save = current_->free_reg;
    compileExpr(stmt->expression);
    setFreeReg(save);
}

// --- Print statement ---

void Compiler::compilePrintStmt(PrintStmt* stmt) {
    // Compile as a call to the global "print" function
    int save = current_->free_reg;
    int func_reg = allocReg();
    int ki = stringConstant("print");
    emitABx(OP_GETGLOBAL, (uint8_t)func_reg, (uint16_t)ki);

    int arg_reg = allocReg();
    compileExpr(stmt->expression, arg_reg);

    emitABC(OP_CALL, (uint8_t)func_reg, 2, 1); // 1 arg, 0 results
    setFreeReg(save);
}

// --- Var statement ---

void Compiler::compileVarStmt(VarStmt* stmt) {
    currentLine_ = stmt->name.line;
    const char* name = stmt->name.identifier;

    if (current_->scope_depth > 0) {
        // Local variable
        int reg = addLocal(name);
        if (stmt->initializer) {
            compileExpr(stmt->initializer, reg);
        } else {
            emitABC(OP_LOADNIL, (uint8_t)reg, 0, 0);
        }
        if (stmt->is_annotated) {
            emitABC(OP_TYPECHECK, (uint8_t)reg, (uint8_t)stmt->type_hint, 0);
        }
    } else {
        // Global variable
        int save = current_->free_reg;
        int reg = allocReg();
        if (stmt->initializer) {
            compileExpr(stmt->initializer, reg);
        } else {
            emitABC(OP_LOADNIL, (uint8_t)reg, 0, 0);
        }
        if (stmt->is_annotated) {
            emitABC(OP_TYPECHECK, (uint8_t)reg, (uint8_t)stmt->type_hint, 0);
        }
        int ki = stringConstant(name);
        emitABx(OP_SETGLOBAL, (uint8_t)reg, (uint16_t)ki);
        setFreeReg(save);
    }
}

// --- Block statement ---

void Compiler::compileBlockStmt(BlockStmt* stmt) {
    beginScope();
    compileBlock(stmt->statements, stmt->count);
    endScope();
}

// --- If statement ---

void Compiler::compileIfStmt(IfStmt* stmt) {
    currentLine_ = 0;

    int save = current_->free_reg;
    int cond_reg = compileExpr(stmt->condition);

    // TEST cond_reg, skip if false
    emitABC(OP_TEST, (uint8_t)cond_reg, 0, 0);
    int jmp_else = emitJump();
    setFreeReg(save);

    // Then branch
    compileStmt(stmt->then_branch);

    if (stmt->else_branch) {
        int jmp_end = emitJump();
        patchJump(jmp_else);
        compileStmt(stmt->else_branch);
        patchJump(jmp_end);
    } else {
        patchJump(jmp_else);
    }
}

// --- While statement ---

void Compiler::compileWhileStmt(WhileStmt* stmt) {
    LoopContext loop;
    loop.start_pc = current_->proto->currentPC();
    loop.is_for_loop = false;
    loop.scope_depth = current_->scope_depth;
    current_->loops.push_back(loop);

    int save = current_->free_reg;
    int cond_reg = compileExpr(stmt->condition);
    emitABC(OP_TEST, (uint8_t)cond_reg, 0, 0);
    int jmp_exit = emitJump();
    setFreeReg(save);

    compileStmt(stmt->body);

    // Jump back to condition
    int loop_start = current_->loops.back().start_pc;
    int offset = loop_start - (current_->proto->currentPC() + 1);
    current_->proto->emitJump(offset, currentLine_);

    patchJump(jmp_exit);

    // Patch break jumps
    for (int jmp : current_->loops.back().break_jumps) {
        patchJump(jmp);
    }
    current_->loops.pop_back();
}

// --- For statement ---

void Compiler::compileForStmt(ForStmt* stmt) {
    beginScope();

    // Initializer
    if (stmt->initializer) {
        compileStmt(stmt->initializer);
    }

    LoopContext loop;
    loop.start_pc = current_->proto->currentPC();
    loop.is_for_loop = true;
    loop.scope_depth = current_->scope_depth;
    current_->loops.push_back(loop);

    int jmp_exit = -1;

    // Condition
    if (stmt->condition) {
        int save = current_->free_reg;
        int cond_reg = compileExpr(stmt->condition);
        emitABC(OP_TEST, (uint8_t)cond_reg, 0, 0);
        jmp_exit = emitJump();
        setFreeReg(save);
    }

    // Body
    compileStmt(stmt->body);

    // Patch continue jumps to here (the increment step)
    for (int jmp : current_->loops.back().continue_jumps) {
        patchJump(jmp);
    }

    if (stmt->increment) {
        int save = current_->free_reg;
        compileExpr(stmt->increment);
        setFreeReg(save);
    }

    // Jump back to condition
    int loop_start = current_->loops.back().start_pc;
    int offset = loop_start - (current_->proto->currentPC() + 1);
    current_->proto->emitJump(offset, currentLine_);

    if (jmp_exit >= 0) {
        patchJump(jmp_exit);
    }

    // Patch break jumps
    for (int jmp : current_->loops.back().break_jumps) {
        patchJump(jmp);
    }
    current_->loops.pop_back();

    endScope();
}

// --- Function statement ---

void Compiler::compileFunctionStmt(FunctionStmt* stmt) {
    currentLine_ = stmt->name.line;
    const char* name = stmt->name.identifier;

    // Save enclosing compiler state
    FunctionState* enclosing = current_;
    FunctionState child_fs;
    child_fs.proto = new Prototype();
    child_fs.proto->name = name ? name : "";
    child_fs.enclosing = enclosing;
    child_fs.scope_depth = 0;
    child_fs.free_reg = 0;
    child_fs.max_reg = 0;
    current_ = &child_fs;

    beginScope();

    // Define parameters as locals
    child_fs.proto->num_params = (int)stmt->param_count;
    for (size_t i = 0; i < stmt->param_count; i++) {
        addLocal(stmt->params[i].identifier);
    }

    // Compile function body
    compileBlock(stmt->body, stmt->body_count);

    // Implicit return at end
    emitReturn(0, 0);

    endScope();

    // Finalize child prototype
    Prototype* child_proto = child_fs.proto;
    child_proto->num_registers = child_fs.max_reg;

    // Restore enclosing state
    current_ = enclosing;

    // Add child prototype to parent and emit CLOSURE
    int proto_idx = (int)current_->proto->protos.size();
    current_->proto->protos.push_back(child_proto);

    if (current_->scope_depth > 0) {
        int reg = addLocal(name);
        emitABx(OP_CLOSURE, (uint8_t)reg, (uint16_t)proto_idx);
    } else {
        int save = current_->free_reg;
        int reg = allocReg();
        emitABx(OP_CLOSURE, (uint8_t)reg, (uint16_t)proto_idx);
        int ki = stringConstant(name);
        emitABx(OP_SETGLOBAL, (uint8_t)reg, (uint16_t)ki);
        setFreeReg(save);
    }
}

// --- Return statement ---

void Compiler::compileReturnStmt(ReturnStmt* stmt) {
    currentLine_ = stmt->keyword.line;

    if (stmt->value) {
        int save = current_->free_reg;
        int reg = compileExpr(stmt->value);
        emitReturn(reg, 1);
        setFreeReg(save);
    } else {
        emitReturn(0, 0);
    }
}

// --- Switch statement ---

void Compiler::compileSwitchStmt(SwitchStmt* stmt) {
    int save = current_->free_reg;
    int disc_reg = compileExpr(stmt->discriminant);

    // Push a pseudo-loop so 'break' statements inside case bodies
    // can target the end of the switch (same semantics as C switch).
    LoopContext switch_loop;
    switch_loop.start_pc = current_->proto->currentPC();
    switch_loop.is_for_loop = false;
    switch_loop.scope_depth = current_->scope_depth;
    current_->loops.push_back(switch_loop);

    std::vector<int> break_jumps;

    for (size_t i = 0; i < stmt->case_count; i++) {
        SwitchCase* sc = stmt->cases[i];

        // Multi-pattern OR-chain: if ANY pattern matches, jump to body.
        // For each pattern, emit a match check; on match, jump to body.
        // After all patterns, emit unconditional jump to no-match.
        std::vector<int> match_jumps;
        int final_no_match_jump = -1;
        bool has_destructure = false;
        CasePattern* destructure_pat = nullptr;

        for (size_t p = 0; p < sc->pattern_count; p++) {
            CasePattern* pat = sc->patterns[p];
            if (!pat) continue;

            switch (pat->type) {
                case PATTERN_VALUE: {
                    int ki = current_->proto->addConstant(pat->as.literal);
                    emitABC(OP_EQ, 1, (uint8_t)disc_reg, makeRK(ki));
                    match_jumps.push_back(emitJump());
                    break;
                }
                case PATTERN_EXPRESSION: {
                    int save2 = current_->free_reg;
                    int expr_reg = compileExpr(pat->as.expr_pattern.expression);
                    TokenType op = pat->as.expr_pattern.op;

                    bool is_relational = (op == TOKEN_LESS || op == TOKEN_LESS_EQUAL ||
                                          op == TOKEN_GREATER || op == TOKEN_GREATER_EQUAL);

                    if (is_relational) {
                        // If types are incompatible, skip this pattern (not an error in multi-pattern)
                        emitABC(OP_TYPECOMPAT, 1, (uint8_t)disc_reg, (uint8_t)expr_reg);
                        int compat_jump = emitJump();

                        // Types not compatible — skip to next pattern
                        int skip_pattern = emitJump();

                        patchJump(compat_jump);

                        switch (op) {
                            case TOKEN_LESS:
                                emitABC(OP_LT, 1, (uint8_t)disc_reg, (uint8_t)expr_reg); break;
                            case TOKEN_LESS_EQUAL:
                                emitABC(OP_LE, 1, (uint8_t)disc_reg, (uint8_t)expr_reg); break;
                            case TOKEN_GREATER:
                                emitABC(OP_LT, 1, (uint8_t)expr_reg, (uint8_t)disc_reg); break;
                            case TOKEN_GREATER_EQUAL:
                                emitABC(OP_LE, 1, (uint8_t)expr_reg, (uint8_t)disc_reg); break;
                            default: break;
                        }
                        match_jumps.push_back(emitJump());
                        patchJump(skip_pattern);
                    } else {
                        switch (op) {
                            case TOKEN_EQUAL_EQUAL:
                                emitABC(OP_EQ, 1, (uint8_t)disc_reg, (uint8_t)expr_reg); break;
                            case TOKEN_BANG_EQUAL:
                                emitABC(OP_EQ, 0, (uint8_t)disc_reg, (uint8_t)expr_reg); break;
                            default:
                                emitABC(OP_EQ, 1, (uint8_t)disc_reg, (uint8_t)expr_reg); break;
                        }
                        match_jumps.push_back(emitJump());
                    }
                    setFreeReg(save2);
                    break;
                }
                case PATTERN_RANGE: {
                    int save2 = current_->free_reg;
                    int start_reg = compileExpr(pat->as.range_pattern.start);
                    int end_reg = compileExpr(pat->as.range_pattern.end);

                    // Guard: types must be compatible
                    emitABC(OP_TYPECOMPAT, 1, (uint8_t)disc_reg, (uint8_t)start_reg);
                    int compat_jump = emitJump();
                    int skip_range = emitJump();
                    patchJump(compat_jump);

                    // start <= disc
                    emitABC(OP_LE, 1, (uint8_t)start_reg, (uint8_t)disc_reg);
                    int lower_ok = emitJump();
                    int lower_fail = emitJump();
                    patchJump(lower_ok);

                    // disc <= end (inclusive) or disc < end (exclusive)
                    if (pat->as.range_pattern.inclusive) {
                        emitABC(OP_LE, 1, (uint8_t)disc_reg, (uint8_t)end_reg);
                    } else {
                        emitABC(OP_LT, 1, (uint8_t)disc_reg, (uint8_t)end_reg);
                    }
                    match_jumps.push_back(emitJump());

                    patchJump(skip_range);
                    patchJump(lower_fail);
                    setFreeReg(save2);
                    break;
                }
                case PATTERN_ARRAY: {
                    int save2 = current_->free_reg;

                    // Type check: disc must be VAL_ARRAY
                    emitABC(OP_TYPEIS, 1, (uint8_t)disc_reg, (uint8_t)VAL_ARRAY);
                    int type_ok = emitJump();
                    int type_fail = emitJump();
                    patchJump(type_ok);

                    size_t elem_count = pat->as.array_pattern.element_count;
                    bool has_rest = pat->as.array_pattern.has_rest;

                    // Length check
                    int len_reg = allocReg();
                    emitABC(OP_LEN, (uint8_t)len_reg, (uint8_t)disc_reg, 0);
                    int expected_ki = current_->proto->addConstant(
                        make_integer_value(NUM_INT64, (int64_t)elem_count));

                    if (has_rest) {
                        // length >= elem_count
                        emitABC(OP_LE, 1, makeRK(expected_ki), (uint8_t)len_reg);
                    } else {
                        // length == elem_count
                        emitABC(OP_EQ, 1, (uint8_t)len_reg, makeRK(expected_ki));
                    }
                    match_jumps.push_back(emitJump());
                    patchJump(type_fail);

                    setFreeReg(save2);
                    has_destructure = true;
                    destructure_pat = pat;
                    break;
                }

                case PATTERN_TABLE: {
                    // Type check: disc must be VAL_TABLE
                    emitABC(OP_TYPEIS, 1, (uint8_t)disc_reg, (uint8_t)VAL_TABLE);
                    int ttype_ok = emitJump();
                    int ttype_fail = emitJump();
                    patchJump(ttype_ok);

                    size_t field_count = pat->as.table_pattern.field_count;

                    // For required fields, check each is non-nil
                    int save2 = current_->free_reg;
                    std::vector<int> field_fail_jumps;
                    for (size_t f = 0; f < field_count; f++) {
                        if (pat->as.table_pattern.fields[f].is_optional) continue;
                        const char* key = pat->as.table_pattern.fields[f].key;
                        int field_reg = allocReg();
                        int key_ki = stringConstant(key);
                        emitABC(OP_GETTABLE, (uint8_t)field_reg,
                                (uint8_t)disc_reg, makeRK(key_ki));
                        // Check field_reg is not nil (VAL_NIL == 0)
                        emitABC(OP_TYPEIS, 0, (uint8_t)field_reg, (uint8_t)VAL_NIL);
                        int not_nil = emitJump();
                        field_fail_jumps.push_back(emitJump());
                        patchJump(not_nil);
                    }
                    match_jumps.push_back(emitJump());

                    patchJump(ttype_fail);
                    for (int fj : field_fail_jumps) {
                        patchJump(fj);
                    }
                    setFreeReg(save2);
                    has_destructure = true;
                    destructure_pat = pat;
                    break;
                }

                case PATTERN_WILDCARD:
                    match_jumps.push_back(emitJump());
                    break;
                default:
                    break;
            }
        }

        // After all patterns: if we fall through, no pattern matched
        if (sc->pattern_count > 0 && !(sc->pattern_count == 1 &&
            sc->patterns[0] && sc->patterns[0]->type == PATTERN_WILDCARD)) {
            final_no_match_jump = emitJump();
        }

        // All match jumps land here (at the body)
        for (int jmp : match_jumps) {
            patchJump(jmp);
        }

        // Case body
        beginScope();

        // Emit destructuring bindings inside the body scope
        if (has_destructure && destructure_pat) {
            if (destructure_pat->type == PATTERN_ARRAY) {
                size_t elem_count = destructure_pat->as.array_pattern.element_count;
                for (size_t k = 0; k < elem_count; k++) {
                    const char* name = destructure_pat->as.array_pattern.elements[k].name;
                    if (name) {
                        const char* interned_name = pool_->intern(name)->data;
                        int local_reg = addLocal(interned_name);
                        int idx_ki = current_->proto->addConstant(
                            make_integer_value(NUM_INT64, (int64_t)k));
                        emitABC(OP_GETTABLE, (uint8_t)local_reg,
                                (uint8_t)disc_reg, makeRK(idx_ki));
                    }
                }
                if (destructure_pat->as.array_pattern.has_rest &&
                    destructure_pat->as.array_pattern.rest_name) {
                    const char* rest_interned = pool_->intern(destructure_pat->as.array_pattern.rest_name)->data;
                    int rest_reg = addLocal(rest_interned);
                    int len_reg = allocReg();
                    emitABC(OP_LEN, (uint8_t)len_reg, (uint8_t)disc_reg, 0);

                    int capacity = (int)elem_count > 0 ? 8 : 0;
                    emitABC(OP_NEWARRAY, (uint8_t)rest_reg, (uint8_t)capacity, 0);

                    // Build rest array: for i = elem_count .. len-1
                    int idx_reg = allocReg();
                    int elem_reg = allocReg();
                    int start_ki = current_->proto->addConstant(
                        make_integer_value(NUM_INT64, (int64_t)elem_count));
                    emitLoadK(idx_reg, start_ki);

                    // Simple loop: while idx_reg < len_reg
                    int loop_start = current_->proto->currentPC();
                    emitABC(OP_LT, 1, (uint8_t)idx_reg, (uint8_t)len_reg);
                    int loop_body = emitJump();
                    int loop_exit = emitJump();
                    patchJump(loop_body);

                    emitABC(OP_GETTABLE, (uint8_t)elem_reg,
                            (uint8_t)disc_reg, (uint8_t)idx_reg);
                    // Push to rest array via native call
                    // Use SETTABLE with integer key: rest[idx - elem_count] = elem
                    int offset_ki = current_->proto->addConstant(
                        make_integer_value(NUM_INT64, (int64_t)elem_count));
                    int offset_reg = allocReg();
                    emitABC(OP_SUB, (uint8_t)offset_reg,
                            (uint8_t)idx_reg, makeRK(offset_ki));
                    emitABC(OP_SETTABLE, (uint8_t)rest_reg,
                            (uint8_t)offset_reg, (uint8_t)elem_reg);
                    emitABC(OP_INC, (uint8_t)idx_reg, (uint8_t)idx_reg, 0);
                    int back_offset = loop_start - (current_->proto->currentPC() + 1);
                    current_->proto->emitJump(back_offset, currentLine_);
                    patchJump(loop_exit);

                    setFreeReg(rest_reg + 1);
                }
            } else if (destructure_pat->type == PATTERN_TABLE) {
                size_t field_count = destructure_pat->as.table_pattern.field_count;
                for (size_t k = 0; k < field_count; k++) {
                    const char* key = destructure_pat->as.table_pattern.fields[k].key;
                    const char* bind = destructure_pat->as.table_pattern.fields[k].bind_name;
                    const char* var_name = bind ? bind : key;
                    const char* interned_var = pool_->intern(var_name)->data;
                    int local_reg = addLocal(interned_var);
                    int key_ki = stringConstant(key);
                    emitABC(OP_GETTABLE, (uint8_t)local_reg,
                            (uint8_t)disc_reg, makeRK(key_ki));
                }
            }
        }

        compileBlock(sc->body, sc->body_count);
        endScope();

        if (sc->has_break) {
            break_jumps.push_back(emitJump());
        }

        if (final_no_match_jump >= 0) {
            patchJump(final_no_match_jump);
        }
    }

    // Default case
    if (stmt->default_body && stmt->default_body_count > 0) {
        beginScope();
        compileBlock(stmt->default_body, stmt->default_body_count);
        endScope();
    }

    // Patch all break jumps to here (from has_break on SwitchCase)
    for (int jmp : break_jumps) {
        patchJump(jmp);
    }

    // Patch break jumps from STMT_BREAK inside case bodies
    for (int jmp : current_->loops.back().break_jumps) {
        patchJump(jmp);
    }
    current_->loops.pop_back();

    setFreeReg(save);
}

// --- Break ---

void Compiler::compileBreakStmt() {
    if (current_->loops.empty()) {
        fprintf(stderr, "Compiler: 'break' outside of loop\n");
        return;
    }
    int jmp = emitJump();
    current_->loops.back().break_jumps.push_back(jmp);
}

// --- Continue ---

void Compiler::compileContinueStmt() {
    if (current_->loops.empty()) {
        fprintf(stderr, "Compiler: 'continue' outside of loop\n");
        return;
    }

    LoopContext& loop = current_->loops.back();

    if (loop.is_for_loop) {
        // For loop: the increment hasn't been emitted yet.
        // Emit a forward jump and record it for patching.
        int jmp = emitJump();
        loop.continue_jumps.push_back(jmp);
    } else {
        // While loop: jump back to the condition
        int offset = loop.start_pc - (current_->proto->currentPC() + 1);
        current_->proto->emitJump(offset, currentLine_);
    }
}

// --- Import ---

void Compiler::compileImportStmt(ImportStmt* stmt) {
    currentLine_ = stmt->keyword.line;

    int save = current_->free_reg;

    const char* mod_name = stmt->module_name.literal.string;
    int mod_ki = stringConstant(mod_name);

    const char* target_name = nullptr;
    if (stmt->has_alias) {
        // Dotted paths (e.g. "math.trig") are stored as TOKEN_STRING in literal.string.
        // Simple identifiers (e.g. "m") are TOKEN_IDENTIFIER in .identifier.
        if (stmt->alias.type == TOKEN_STRING && stmt->alias.literal.string) {
            target_name = stmt->alias.literal.string;
        } else if (stmt->alias.identifier) {
            target_name = stmt->alias.identifier;
        }
    }
    if (!target_name) {
        target_name = mod_name;
    }
    int alias_ki = stringConstant(target_name);

    // Single self-contained instruction: the VM handles _GLOBAL spreading,
    // simple namespace creation, and dotted namespace creation internally.
    int scratch = allocReg();
    emitABC(OP_IMPORT, (uint8_t)scratch, makeRK(mod_ki), makeRK(alias_ki));

    setFreeReg(save);
}

// --- Enum ---

void Compiler::compileEnumStmt(EnumStmt* stmt) {
    currentLine_ = stmt->keyword.line;

    int member_count = 0;
    for (EnumMemberDef* m = stmt->members; m; m = m->next) {
        member_count++;
    }

    int save = current_->free_reg;
    int enum_reg = allocReg();

    // Create a table to hold the enum members: { RED = 0, GREEN = 1, ... }
    emitABC(OP_NEWTABLE, (uint8_t)enum_reg, 0, (uint8_t)(member_count & 0xFF));

    int idx = 0;
    for (EnumMemberDef* m = stmt->members; m; m = m->next, idx++) {
        int inner_save = current_->free_reg;
        int val_reg;

        if (m->value) {
            val_reg = compileExpr(m->value);
        } else {
            val_reg = allocReg();
            emitAsBx(OP_LOADINT, (uint8_t)val_reg, idx);
        }

        int key_ki = stringConstant(m->name.identifier);
        emitABC(OP_SETTABLE, (uint8_t)enum_reg, makeRK(key_ki), (uint8_t)val_reg);
        setFreeReg(inner_save);
    }

    // Store as both the plain name and __enum_<name> so that table dot
    // access (Color.RED) and EXPR_ENUM_ACCESS (switch patterns) both work.
    int var_ki = stringConstant(stmt->name.identifier);
    emitABx(OP_SETGLOBAL, (uint8_t)enum_reg, (uint16_t)var_ki);

    char enum_var[256];
    snprintf(enum_var, sizeof(enum_var), "__enum_%s", stmt->name.identifier);
    const char* interned = pool_->intern(enum_var)->data;
    int enum_ki = stringConstant(interned);
    emitABx(OP_SETGLOBAL, (uint8_t)enum_reg, (uint16_t)enum_ki);

    setFreeReg(save);
}

// --- Pragma ---

void Compiler::compilePragmaStmt(PragmaStmt* stmt) {
    currentLine_ = stmt->keyword.line;

    int save = current_->free_reg;
    int reg = allocReg();

    const char* val = stmt->value.identifier;
    const char* sval = stmt->value.literal.string;

    if (val && strcmp(val, "true") == 0) {
        emitABC(OP_LOADBOOL, (uint8_t)reg, 1, 0);
    } else if (val && strcmp(val, "false") == 0) {
        emitABC(OP_LOADBOOL, (uint8_t)reg, 0, 0);
    } else if (val) {
        int ki = stringConstant(val);
        emitLoadK(reg, ki);
    } else if (sval) {
        int ki = stringConstant(sval);
        emitLoadK(reg, ki);
    } else if (stmt->value.type == TOKEN_TRUE) {
        emitABC(OP_LOADBOOL, (uint8_t)reg, 1, 0);
    } else if (stmt->value.type == TOKEN_FALSE) {
        emitABC(OP_LOADBOOL, (uint8_t)reg, 0, 0);
    } else {
        emitABC(OP_LOADBOOL, (uint8_t)reg, 1, 0);
    }

    int name_ki = stringConstant(stmt->name.identifier);
    emitABx(OP_PRAGMA, (uint8_t)reg, (uint16_t)name_ki);

    setFreeReg(save);
}
