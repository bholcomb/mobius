#include "vm/compiler.h"
#include "state/mobius_state.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>

// ============================================================================
// Constructor
// ============================================================================

Compiler::Compiler(StringInternPool* pool, MobiusState* state)
    : current_(nullptr), pool_(pool), state_(state) {}

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
    if (name) {
        fs->proto->name = name;
        if (!enclosing) {
            fs->proto->source = name;
        }
    }
    // Nested functions inherit the source filename from the enclosing scope
    if (enclosing && !enclosing->proto->source.empty()) {
        fs->proto->source = enclosing->proto->source;
    }
    fs->scope_depth = 0;
    fs->free_reg = 0;
    fs->max_reg = 0;

    current_ = fs;
    return fs;
}

Prototype* Compiler::endCompiler() {
    Prototype* proto = current_->proto;
    proto->num_registers = current_->max_reg;

    peepholeOptimize(proto);

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

int Compiler::emitABC_D64(OpCode op, uint8_t a, uint8_t b, uint8_t c, uint64_t data) {
    return current_->proto->emitABC_D64(op, a, b, c, data, currentLine_);
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

void Compiler::emitGetGlobal(int reg, const char* name) {
    if (state_) {
        int slot = state_->assignGlobalSlot(name);
        emitABx(OP_GETGLOBAL, (uint8_t)reg, (uint16_t)slot);
    } else {
        int ki = stringConstant(name);
        emitABx(OP_GETGLOBAL, (uint8_t)reg, (uint16_t)ki);
    }
}

void Compiler::emitSetGlobal(int reg, const char* name) {
    if (state_) {
        int slot = state_->assignGlobalSlot(name);
        emitABx(OP_SETGLOBAL, (uint8_t)reg, (uint16_t)slot);
    } else {
        int ki = stringConstant(name);
        emitABx(OP_SETGLOBAL, (uint8_t)reg, (uint16_t)ki);
    }
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
        case EXPR_TERNARY:
            return compileTernary(&expr->as.ternary, dest);
        case EXPR_FUNCTION:
            return compileFunctionExpr(&expr->as.function_expr, dest);
        default:
            fprintf(stderr, "Compiler [%s]: unknown expression type %d\n",
                    current_->proto->source.c_str(), expr->type);
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
        case VAL_INT64: {
            int64_t iv = v.as.i64;
            if (iv >= -SBX16_BIAS && iv <= SBX16_BIAS) {
                emitAsBx(OP_LOADINT, (uint8_t)reg, (int)iv);
            } else {
                int ki = current_->proto->addIntConstant(iv);
                emitLoadK(reg, ki);
            }
            break;
        }
        case VAL_FLOAT64: {
            int ki = current_->proto->addFloatConstant(v.as.double_val);
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

    // Global — use flat slot index when state is available
    int reg = (dest >= 0) ? dest : allocReg();
    emitGetGlobal(reg, name);
    return reg;
}

// Try to represent an expression as an RK operand (constant pool ref or register).
// Returns true if the expression is a numeric literal that fits in the RK constant
// pool (index < 128). Sets *rk to the RK-encoded value. If false, the caller
// should compile normally to a register.
bool Compiler::tryExprAsRK(Expr* e, uint8_t* rk) {
    if (e->type != EXPR_LITERAL) return false;
    const Value& v = e->as.literal.value;
    int ki = -1;
    if (v.type == VAL_INT64)
        ki = current_->proto->addIntConstant(v.as.i64);
    else if (v.type == VAL_UINT64)
        ki = current_->proto->addIntConstant((int64_t)v.as.u64);
    else if (v.type == VAL_FLOAT64)
        ki = current_->proto->addFloatConstant(v.as.double_val);
    else
        return false;
    if (ki < 0 || ki > (int)RK_INDEX_MASK) return false;
    *rk = MAKE_RK((uint8_t)ki);
    return true;
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

    // Type-check operator: expr is type -> bool
    if (expr->op.type == TOKEN_IS) {
        int reg = (dest >= 0) ? dest : allocReg();
        int save = current_->free_reg;
        int val_reg = compileExpr(expr->left);
        uint8_t type_tag = (uint8_t)expr->right->as.literal.value.as.i64;

        emitABC(OP_TYPEIS, 1, (uint8_t)val_reg, type_tag);
        int jmp = emitJump();
        emitABC(OP_LOADBOOL, (uint8_t)reg, 0, 1);
        patchJump(jmp);
        emitABC(OP_LOADBOOL, (uint8_t)reg, 1, 0);

        setFreeReg(save);
        if (dest < 0) {
            current_->free_reg = reg + 1;
            if (current_->free_reg > current_->max_reg)
                current_->max_reg = current_->free_reg;
        }
        return reg;
    }

    // Constant folding: evaluate operations on two literal operands at compile time
    if (expr->left->type == EXPR_LITERAL && expr->right->type == EXPR_LITERAL) {
        const Value& lv = expr->left->as.literal.value;
        const Value& rv = expr->right->as.literal.value;
        bool l_int = (lv.type == VAL_INT64 || lv.type == VAL_UINT64);
        bool r_int = (rv.type == VAL_INT64 || rv.type == VAL_UINT64);
        bool l_flt = (lv.type == VAL_FLOAT64);
        bool r_flt = (rv.type == VAL_FLOAT64);
        bool both_num = (l_int || l_flt) && (r_int || r_flt);

        if (both_num) {
            auto to_double = [](const Value& v) -> double {
                if (v.type == VAL_UINT64)  return (double)v.as.u64;
                if (v.type == VAL_INT64) return (double)v.as.i64;
                return v.as.double_val;
            };

            bool folded = false;
            Value result;

            switch (expr->op.type) {
                case TOKEN_PLUS:
                case TOKEN_MINUS:
                case TOKEN_STAR: {
                    if (l_int && r_int) {
                        int64_t a = lv.as.i64;
                        int64_t b = rv.as.i64;
                        int64_t r;
                        if (expr->op.type == TOKEN_PLUS)       r = a + b;
                        else if (expr->op.type == TOKEN_MINUS)  r = a - b;
                        else                                    r = a * b;
                        result = make_int64_value(r);
                    } else {
                        double a = to_double(lv), b = to_double(rv), r;
                        if (expr->op.type == TOKEN_PLUS)       r = a + b;
                        else if (expr->op.type == TOKEN_MINUS)  r = a - b;
                        else                                    r = a * b;
                        result = make_float_value(r);
                    }
                    folded = true;
                    break;
                }
                case TOKEN_SLASH: {
                    double b = to_double(rv);
                    if (b != 0.0) {
                        result = make_float_value(to_double(lv) / b);
                        folded = true;
                    }
                    break;
                }
                case TOKEN_PERCENT: {
                    if (l_int && r_int) {
                        int64_t b = rv.as.i64;
                        if (b != 0) {
                            result = make_int64_value(lv.as.i64 % b);
                            folded = true;
                        }
                    } else {
                        double b = to_double(rv);
                        if (b != 0.0) {
                            result = make_float_value(fmod(to_double(lv), b));
                            folded = true;
                        }
                    }
                    break;
                }
                default: break;
            }

            if (folded) {
                int reg = (dest >= 0) ? dest : allocReg();
                if (result.type == VAL_BOOL) {
                    emitABC(OP_LOADBOOL, (uint8_t)reg, result.as.boolean ? 1 : 0, 0);
                } else if (result.type == VAL_INT64) {
                    int64_t iv = result.as.i64;
                    if (iv >= -SBX16_BIAS && iv <= SBX16_BIAS) {
                        emitAsBx(OP_LOADINT, (uint8_t)reg, (int)iv);
                    } else {
                        int ki = current_->proto->addIntConstant(iv);
                        emitLoadK(reg, ki);
                    }
                } else {
                    int ki = current_->proto->addFloatConstant(result.as.double_val);
                    emitLoadK(reg, ki);
                }
                return reg;
            }
        }

        // Comparison folding on numeric literals
        if (both_num) {
            auto to_double = [](const Value& v) -> double {
                if (v.type == VAL_UINT64)  return (double)v.as.u64;
                if (v.type == VAL_INT64) return (double)v.as.i64;
                return v.as.double_val;
            };
            bool folded = false;
            bool cmp_result = false;

            double a = to_double(lv), b = to_double(rv);

            switch (expr->op.type) {
                case TOKEN_LESS:          cmp_result = a < b;  folded = true; break;
                case TOKEN_LESS_EQUAL:    cmp_result = a <= b; folded = true; break;
                case TOKEN_GREATER:       cmp_result = a > b;  folded = true; break;
                case TOKEN_GREATER_EQUAL: cmp_result = a >= b; folded = true; break;
                case TOKEN_EQUAL_EQUAL:   cmp_result = a == b; folded = true; break;
                case TOKEN_BANG_EQUAL:    cmp_result = a != b; folded = true; break;
                default: break;
            }

            if (folded) {
                int reg = (dest >= 0) ? dest : allocReg();
                emitABC(OP_LOADBOOL, (uint8_t)reg, cmp_result ? 1 : 0, 0);
                return reg;
            }
        }

        // Boolean logic folding
        if (lv.type == VAL_BOOL && rv.type == VAL_BOOL) {
            bool folded = false;
            bool logic_result = false;

            switch (expr->op.type) {
                case TOKEN_AND:
                case TOKEN_AND_AND:
                    logic_result = lv.as.boolean && rv.as.boolean;
                    folded = true;
                    break;
                case TOKEN_OR:
                case TOKEN_OR_OR:
                    logic_result = lv.as.boolean || rv.as.boolean;
                    folded = true;
                    break;
                default: break;
            }

            if (folded) {
                int reg = (dest >= 0) ? dest : allocReg();
                emitABC(OP_LOADBOOL, (uint8_t)reg, logic_result ? 1 : 0, 0);
                return reg;
            }
        }

        // String concatenation folding: "hello" + " world" => "hello world"
        if (lv.type == VAL_STRING && rv.type == VAL_STRING &&
            lv.as.string && rv.as.string && expr->op.type == TOKEN_PLUS) {
            size_t total = lv.as.string->length + rv.as.string->length;
            char* buf = (char*)malloc(total + 1);
            memcpy(buf, lv.as.string->data, lv.as.string->length);
            memcpy(buf + lv.as.string->length, rv.as.string->data, rv.as.string->length);
            buf[total] = '\0';

            MobiusString* folded_str = pool_->intern(buf);
            free(buf);

            int reg = (dest >= 0) ? dest : allocReg();
            int ki = current_->proto->addStringConstant(folded_str);
            emitLoadK(reg, ki);
            return reg;
        }
    }

    // Try arithmetic-with-immediate (AsBx format): R[A] = R[A] op sBx
    // sBx range is -SBX16_BIAS..SBX16_BIAS (±32767)
    {
        OpCode imm_op = OP_NOP;
        switch (expr->op.type) {
            case TOKEN_PLUS:    imm_op = OP_ADDI; break;
            case TOKEN_MINUS:   imm_op = OP_SUBI; break;
            case TOKEN_STAR:    imm_op = OP_MULI; break;
            case TOKEN_SLASH:   imm_op = OP_DIVI; break;
            case TOKEN_PERCENT: imm_op = OP_MODI; break;
            default: break;
        }

        if (imm_op != OP_NOP) {
            auto try_imm = [](Expr* e, int* out) -> bool {
                if (e->type != EXPR_LITERAL) return false;
                const Value& v = e->as.literal.value;
                if (v.type != VAL_INT64) return false;
                int64_t iv = v.as.i64;
                if (iv < -SBX16_BIAS || iv > SBX16_BIAS) return false;
                *out = (int)iv;
                return true;
            };

            int imm;
            // Right operand is immediate: R[dest] = R[left] op imm
            if (try_imm(expr->right, &imm)) {
                int reg = (dest >= 0) ? dest : allocReg();
                int save_reg = current_->free_reg;
                int left_reg = compileExpr(expr->left, reg);
                if (left_reg != reg)
                    emitABC(OP_MOVE, (uint8_t)reg, (uint8_t)left_reg, 0);
                emitAsBx(imm_op, (uint8_t)reg, imm);
                setFreeReg(save_reg);
                if (dest < 0) {
                    current_->free_reg = reg + 1;
                    if (current_->free_reg > current_->max_reg)
                        current_->max_reg = current_->free_reg;
                }
                return reg;
            }
            // Left operand is immediate (commutative ops only: + and *)
            if ((imm_op == OP_ADDI || imm_op == OP_MULI) && try_imm(expr->left, &imm)) {
                int reg = (dest >= 0) ? dest : allocReg();
                int save_reg = current_->free_reg;
                int right_reg = compileExpr(expr->right, reg);
                if (right_reg != reg)
                    emitABC(OP_MOVE, (uint8_t)reg, (uint8_t)right_reg, 0);
                emitAsBx(imm_op, (uint8_t)reg, imm);
                setFreeReg(save_reg);
                if (dest < 0) {
                    current_->free_reg = reg + 1;
                    if (current_->free_reg > current_->max_reg)
                        current_->max_reg = current_->free_reg;
                }
                return reg;
            }
        }
    }

    // Try inline-data constant opcode (*K): one register operand, one 64-bit
    // constant embedded in the instruction stream. Handles any numeric literal
    // regardless of range.
    {
        OpCode k_op = OP_NOP;
        switch (expr->op.type) {
            case TOKEN_PLUS:    k_op = OP_ADDK; break;
            case TOKEN_MINUS:   k_op = OP_SUBK; break;
            case TOKEN_STAR:    k_op = OP_MULK; break;
            case TOKEN_SLASH:   k_op = OP_DIVK; break;
            case TOKEN_PERCENT: k_op = OP_MODK; break;
            default: break;
        }

        if (k_op != OP_NOP) {
            auto try_literal = [](Expr* e, uint64_t* out_raw, uint8_t* out_tag) -> bool {
                if (e->type != EXPR_LITERAL) return false;
                const Value& v = e->as.literal.value;
                if (v.type == VAL_INT64) {
                    uint64_t raw;
                    memcpy(&raw, &v.as.i64, 8);
                    *out_raw = raw;
                    *out_tag = (uint8_t)VAL_INT64;
                    return true;
                }
                if (v.type == VAL_FLOAT64) {
                    uint64_t raw;
                    memcpy(&raw, &v.as.double_val, 8);
                    *out_raw = raw;
                    *out_tag = (uint8_t)VAL_FLOAT64;
                    return true;
                }
                return false;
            };

            uint64_t raw;
            uint8_t tag;

            if (try_literal(expr->right, &raw, &tag)) {
                int reg = (dest >= 0) ? dest : allocReg();
                int save_reg = current_->free_reg;
                int src_reg = compileExpr(expr->left, reg);
                emitABC_D64(k_op, (uint8_t)reg, (uint8_t)src_reg, tag, raw);
                setFreeReg(save_reg);
                if (dest < 0) {
                    current_->free_reg = reg + 1;
                    if (current_->free_reg > current_->max_reg)
                        current_->max_reg = current_->free_reg;
                }
                return reg;
            }

            if ((k_op == OP_ADDK || k_op == OP_MULK) && try_literal(expr->left, &raw, &tag)) {
                int reg = (dest >= 0) ? dest : allocReg();
                int save_reg = current_->free_reg;
                int src_reg = compileExpr(expr->right, reg);
                emitABC_D64(k_op, (uint8_t)reg, (uint8_t)src_reg, tag, raw);
                setFreeReg(save_reg);
                if (dest < 0) {
                    current_->free_reg = reg + 1;
                    if (current_->free_reg > current_->max_reg)
                        current_->max_reg = current_->free_reg;
                }
                return reg;
            }
        }
    }

    int reg = (dest >= 0) ? dest : allocReg();
    int save_reg = current_->free_reg;

    // Try RK encoding: if either operand is a literal, reference it directly
    // from the constant pool instead of loading it into a register.
    uint8_t rk_left, rk_right;
    bool left_is_rk = tryExprAsRK(expr->left, &rk_left);
    bool right_is_rk = tryExprAsRK(expr->right, &rk_right);

    if (!left_is_rk) {
        int left = compileExpr(expr->left);
        rk_left = (uint8_t)left;
    }
    if (!right_is_rk) {
        int right = compileExpr(expr->right);
        rk_right = (uint8_t)right;
    }

    // Determine operand types for specialized opcode selection.
    // An RK operand from tryExprAsRK always came from a literal, so we can
    // recover its ValueType from the original expression node.
    auto exprLiteralType = [](Expr* e) -> ValueType {
        if (e->type == EXPR_LITERAL) return e->as.literal.value.type;
        return VAL_NIL;
    };
    ValueType lt = left_is_rk ? exprLiteralType(expr->left) : VAL_NIL;
    ValueType rt = right_is_rk ? exprLiteralType(expr->right) : VAL_NIL;
    bool both_i64 = (lt == VAL_INT64 && rt == VAL_INT64);
    bool both_f64 = (lt == VAL_FLOAT64 && rt == VAL_FLOAT64);

    switch (expr->op.type) {
        case TOKEN_PLUS:
            emitABC(both_i64 ? OP_ADD_II : both_f64 ? OP_ADD_FF : OP_ADD,
                    (uint8_t)reg, rk_left, rk_right);
            break;
        case TOKEN_MINUS:
            emitABC(both_i64 ? OP_SUB_II : both_f64 ? OP_SUB_FF : OP_SUB,
                    (uint8_t)reg, rk_left, rk_right);
            break;
        case TOKEN_STAR:
            emitABC(both_i64 ? OP_MUL_II : both_f64 ? OP_MUL_FF : OP_MUL,
                    (uint8_t)reg, rk_left, rk_right);
            break;
        case TOKEN_SLASH:
            emitABC(OP_DIV, (uint8_t)reg, rk_left, rk_right);
            break;
        case TOKEN_PERCENT:
            emitABC(both_i64 ? OP_MOD_II : OP_MOD,
                    (uint8_t)reg, rk_left, rk_right);
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
            fprintf(stderr, "Compiler [%s:%d]: unknown binary operator %d\n",
                    current_->proto->source.c_str(), currentLine_, expr->op.type);
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

    // Unary constant folding
    if (expr->right->type == EXPR_LITERAL) {
        const Value& v = expr->right->as.literal.value;
        bool folded = false;
        Value result;

        switch (expr->op.type) {
            case TOKEN_MINUS:
                if (v.type == VAL_INT64) {
                    result = make_int64_value(-v.as.i64);
                    folded = true;
                } else if (v.type == VAL_FLOAT64) {
                    result = make_float_value(-v.as.double_val);
                    folded = true;
                }
                break;
            case TOKEN_BANG:
            case TOKEN_NOT:
                if (v.type == VAL_BOOL) {
                    result = make_bool_value(!v.as.boolean);
                    folded = true;
                } else if (v.type == VAL_NIL) {
                    result = make_bool_value(true);
                    folded = true;
                }
                break;
            case TOKEN_TILDE:
                if (v.type == VAL_INT64) {
                    result = make_int64_value(~v.as.i64);
                    folded = true;
                }
                break;
            case TOKEN_PLUS:
                if (v.type == VAL_INT64 || v.type == VAL_FLOAT64) {
                    result = v;
                    folded = true;
                }
                break;
            default: break;
        }

        if (folded) {
            LiteralExpr lit;
            lit.value = result;
            return compileLiteral(&lit, dest);
        }
    }

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
            fprintf(stderr, "Compiler [%s:%d]: unknown unary operator %d\n",
                    current_->proto->source.c_str(), currentLine_, expr->op.type);
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

static int assignment_target_line(Expr* target) {
    if (!target) return 0;

    switch (target->type) {
        case EXPR_VARIABLE:
            return target->as.variable.name.line;
        case EXPR_TABLE_DOT:
            return target->as.table_dot.key.line;
        case EXPR_ARRAY_INDEX:
            return assignment_target_line(target->as.array_index.array);
        case EXPR_TABLE_INDEX:
            return assignment_target_line(target->as.table_index.table);
        default:
            return 0;
    }
}

int Compiler::compileAssignment(AssignmentExpr* expr, int dest) {
    currentLine_ = assignment_target_line(expr->target);

    auto finish_assignment = [&](int save_reg, int value_reg) -> int {
        setFreeReg(save_reg);

        int result_reg;
        if (dest >= 0) {
            if (dest != value_reg) {
                emitABC(OP_MOVE, (uint8_t)dest, (uint8_t)value_reg, 0);
            }
            result_reg = dest;
        } else {
            result_reg = allocReg();
            if (result_reg != value_reg) {
                emitABC(OP_MOVE, (uint8_t)result_reg, (uint8_t)value_reg, 0);
            }
        }

        return result_reg;
    };

    auto compile_index_assignment = [&](Expr* container_expr, Expr* index_expr) -> int {
        int save_reg = current_->free_reg;
        int container_reg = compileExpr(container_expr);

        uint8_t rk_key;
        if (index_expr->type == EXPR_LITERAL) {
            int ki = current_->proto->addConstant(index_expr->as.literal.value);
            rk_key = makeRK(ki);
        } else {
            int key_reg = compileExpr(index_expr);
            rk_key = (uint8_t)key_reg;
        }

        int value_reg = compileExpr(expr->value);
        emitABC(OP_SETTABLE, (uint8_t)container_reg, rk_key, (uint8_t)value_reg);

        return finish_assignment(save_reg, value_reg);
    };

    switch (expr->target->type) {
        case EXPR_VARIABLE: {
            const char* name = expr->target->as.variable.name.identifier;

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

            int reg = (dest >= 0) ? dest : allocReg();
            compileExpr(expr->value, reg);
            emitSetGlobal(reg, name);
            return reg;
        }

        case EXPR_ARRAY_INDEX:
            return compile_index_assignment(expr->target->as.array_index.array,
                                            expr->target->as.array_index.index);

        case EXPR_TABLE_INDEX:
            return compile_index_assignment(expr->target->as.table_index.table,
                                            expr->target->as.table_index.index);

        case EXPR_TABLE_DOT: {
            int save_reg = current_->free_reg;
            int table_reg = compileExpr(expr->target->as.table_dot.table);
            int ki = stringConstant(expr->target->as.table_dot.key.identifier);
            uint8_t rk_key = makeRK(ki);
            int value_reg = compileExpr(expr->value);
            emitABC(OP_SETTABLE, (uint8_t)table_reg, rk_key, (uint8_t)value_reg);
            return finish_assignment(save_reg, value_reg);
        }

        default:
            fprintf(stderr, "Compiler [%s:%d]: invalid assignment target %d\n",
                    current_->proto->source.c_str(), currentLine_, expr->target->type);
            return -1;
    }
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

// --- Tail Call ---

void Compiler::compileTailCall(CallExpr* expr) {
    currentLine_ = expr->paren.line;
    int base = current_->free_reg;
    int func_reg = allocReg();

    if (expr->callee->type == EXPR_TABLE_DOT) {
        compileTableDot(&expr->callee->as.table_dot, func_reg);
    } else {
        compileExpr(expr->callee, func_reg);
    }

    for (size_t i = 0; i < expr->arg_count; i++) {
        int arg_reg = allocReg();
        compileExpr(expr->arguments[i], arg_reg);
    }

    int nargs = (int)expr->arg_count + 1;  // B = nargs + 1
    emitABC(OP_TAILCALL, (uint8_t)func_reg, (uint8_t)nargs, 0);
    setFreeReg(base);
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

    bool is_enum = false;
    if (expr->table && expr->table->type == EXPR_VARIABLE) {
        const char* name = expr->table->as.variable.name.identifier;
        if (name && enum_names_.count(name)) {
            is_enum = true;
        }
    }

    int tbl_reg = compileExpr(expr->table);
    int ki = stringConstant(expr->key.identifier);
    uint8_t rk_key = makeRK(ki);

    if (is_enum) {
        emitABC(OP_GETENUM, (uint8_t)reg, (uint8_t)tbl_reg, rk_key);
    } else {
        emitABC(OP_GETTABLE, (uint8_t)reg, (uint8_t)tbl_reg, rk_key);
    }

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

    char enum_var[256];
    snprintf(enum_var, sizeof(enum_var), "__enum_%s", expr->enum_name.identifier);
    const char* interned = pool_->intern(enum_var)->data;

    int enum_reg = allocReg();
    emitGetGlobal(enum_reg, interned);

    int member_ki = stringConstant(expr->member_name.identifier);
    emitABC(OP_GETENUM, (uint8_t)reg, (uint8_t)enum_reg, makeRK(member_ki));

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
    int tmp = allocReg();

    emitGetGlobal(tmp, name);
    if (expr->is_prefix) {
        emitABC(op, (uint8_t)tmp, (uint8_t)tmp, 0);
        emitSetGlobal(tmp, name);
        if (reg != tmp) emitABC(OP_MOVE, (uint8_t)reg, (uint8_t)tmp, 0);
    } else {
        emitABC(OP_MOVE, (uint8_t)reg, (uint8_t)tmp, 0);
        emitABC(op, (uint8_t)tmp, (uint8_t)tmp, 0);
        emitSetGlobal(tmp, name);
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
        case STMT_FOR_IN:
            compileForInStmt(&stmt->as.for_in_stmt);
            break;
        case STMT_TRY_CATCH:
            compileTryCatchStmt(&stmt->as.try_catch_stmt);
            break;
        case STMT_THROW:
            compileThrowStmt(&stmt->as.throw_stmt);
            break;
        default:
            fprintf(stderr, "Compiler [%s]: unknown statement type %d\n",
                    current_->proto->source.c_str(), stmt->type);
            break;
    }
}

void Compiler::compileBlock(Stmt** stmts, size_t count) {
    for (size_t i = 0; i < count; i++) {
        compileStmt(stmts[i]);
        if (stmts[i]->type == STMT_RETURN)
            break;
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
    emitGetGlobal(func_reg, "print");

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
        emitSetGlobal(reg, name);
        setFreeReg(save);
    }
}

// --- Block statement ---

void Compiler::compileBlockStmt(BlockStmt* stmt) {
    beginScope();
    compileBlock(stmt->statements, stmt->count);
    endScope();
}

// --- Condition compilation (fused compare+branch) ---
// When a condition is a comparison, emit OP_LT/LE/EQ directly followed by
// OP_JMP, skipping the 4-instruction LOADBOOL+TEST pattern.
// Returns the jump index to patch (jumps when condition is FALSE).

int Compiler::compileConditionJump(Expr* condition) {
    if (condition->type == EXPR_BINARY) {
        BinaryExpr* bin = &condition->as.binary;
        TokenType op = bin->op.type;

        if (op == TOKEN_LESS || op == TOKEN_LESS_EQUAL ||
            op == TOKEN_GREATER || op == TOKEN_GREATER_EQUAL ||
            op == TOKEN_EQUAL_EQUAL || op == TOKEN_BANG_EQUAL) {

            currentLine_ = bin->op.line;

            // Try compare-with-immediate: RHS is an integer literal in sBx range
            if (bin->right->type == EXPR_LITERAL &&
                bin->right->as.literal.value.type == VAL_INT64) {
                int64_t iv = bin->right->as.literal.value.as.i64;
                if (iv >= -SBX16_BIAS && iv <= SBX16_BIAS) {
                    int save = current_->free_reg;
                    int left = compileExpr(bin->left);
                    int imm = (int)iv;

                    switch (op) {
                        case TOKEN_LESS:
                            emitAsBx(OP_LTI, (uint8_t)left, imm); break;
                        case TOKEN_LESS_EQUAL:
                            emitAsBx(OP_LEI, (uint8_t)left, imm); break;
                        case TOKEN_GREATER:
                            emitAsBx(OP_GTI, (uint8_t)left, imm); break;
                        case TOKEN_GREATER_EQUAL:
                            emitAsBx(OP_GEI, (uint8_t)left, imm); break;
                        case TOKEN_EQUAL_EQUAL:
                            emitAsBx(OP_EQI, (uint8_t)left, imm); break;
                        case TOKEN_BANG_EQUAL:
                            emitAsBx(OP_NEI, (uint8_t)left, imm); break;
                        default: break;
                    }

                    int jmp = emitJump();
                    setFreeReg(save);
                    return jmp;
                }
            }

            // Try compare-with-immediate: LHS is an integer literal in sBx range (swap operands)
            if (bin->left->type == EXPR_LITERAL &&
                bin->left->as.literal.value.type == VAL_INT64) {
                int64_t iv = bin->left->as.literal.value.as.i64;
                if (iv >= -SBX16_BIAS && iv <= SBX16_BIAS) {
                    int save = current_->free_reg;
                    int right = compileExpr(bin->right);
                    int imm = (int)iv;

                    // Swap: (imm < R) == (R > imm), etc.
                    switch (op) {
                        case TOKEN_LESS:
                            emitAsBx(OP_GTI, (uint8_t)right, imm); break;
                        case TOKEN_LESS_EQUAL:
                            emitAsBx(OP_GEI, (uint8_t)right, imm); break;
                        case TOKEN_GREATER:
                            emitAsBx(OP_LTI, (uint8_t)right, imm); break;
                        case TOKEN_GREATER_EQUAL:
                            emitAsBx(OP_LEI, (uint8_t)right, imm); break;
                        case TOKEN_EQUAL_EQUAL:
                            emitAsBx(OP_EQI, (uint8_t)right, imm); break;
                        case TOKEN_BANG_EQUAL:
                            emitAsBx(OP_NEI, (uint8_t)right, imm); break;
                        default: break;
                    }

                    int jmp = emitJump();
                    setFreeReg(save);
                    return jmp;
                }
            }

            // General compare path — use RK encoding when possible
            int save = current_->free_reg;
            uint8_t rk_left, rk_right;
            if (!tryExprAsRK(bin->left, &rk_left)) {
                int left = compileExpr(bin->left);
                rk_left = (uint8_t)left;
            }
            if (!tryExprAsRK(bin->right, &rk_right)) {
                int right = compileExpr(bin->right);
                rk_right = (uint8_t)right;
            }

            switch (op) {
                case TOKEN_LESS:
                    emitABC(OP_LT, 0, rk_left, rk_right); break;
                case TOKEN_LESS_EQUAL:
                    emitABC(OP_LE, 0, rk_left, rk_right); break;
                case TOKEN_GREATER:
                    emitABC(OP_LT, 0, rk_right, rk_left); break;
                case TOKEN_GREATER_EQUAL:
                    emitABC(OP_LE, 0, rk_right, rk_left); break;
                case TOKEN_EQUAL_EQUAL:
                    emitABC(OP_EQ, 0, rk_left, rk_right); break;
                case TOKEN_BANG_EQUAL:
                    emitABC(OP_EQ, 1, rk_left, rk_right); break;
                default: break;
            }

            int jmp = emitJump();
            setFreeReg(save);
            return jmp;
        }
    }

    // Fallback: compile expression to a register, then TESTJMP
    int save = current_->free_reg;
    int cond_reg = compileExpr(condition);
    int jmp = current_->proto->currentPC();
    emitAsBx(OP_TESTJMP, (uint8_t)cond_reg, 0);  // patched later
    setFreeReg(save);
    return jmp;
}

// --- If statement ---

void Compiler::compileIfStmt(IfStmt* stmt) {
    currentLine_ = 0;

    int jmp_else = compileConditionJump(stmt->condition);

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

    int jmp_exit = compileConditionJump(stmt->condition);

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

// Detect if a for-statement matches the numeric for-loop pattern:
//   for (var <name> = <start>; <name> < <limit>; <name> = <name> + <step>)
// Returns true if the pattern is matched and sets the output parameters.
static bool isNumericForLoop(ForStmt* stmt, const char** var_name,
                             Expr** start_expr, Expr** limit_expr,
                             int64_t* step_val, bool* count_up) {
    if (!stmt->initializer || !stmt->condition || !stmt->increment)
        return false;

    // Initializer must be a var statement
    if (stmt->initializer->type != STMT_VAR)
        return false;
    VarStmt* var = &stmt->initializer->as.var;
    if (!var->initializer)
        return false;
    *var_name = var->name.identifier;
    *start_expr = var->initializer;

    // Condition must be a binary comparison involving the loop variable
    if (stmt->condition->type != EXPR_BINARY)
        return false;
    BinaryExpr* cond = &stmt->condition->as.binary;
    TokenType cmp = cond->op.type;

    // <var> < limit  or  <var> <= limit  (counting up)
    if ((cmp == TOKEN_LESS || cmp == TOKEN_LESS_EQUAL) &&
        cond->left->type == EXPR_VARIABLE &&
        strcmp(cond->left->as.variable.name.identifier, *var_name) == 0) {
        *limit_expr = cond->right;
        *count_up = true;
    }
    // <var> > limit  or  <var> >= limit  (counting down)
    else if ((cmp == TOKEN_GREATER || cmp == TOKEN_GREATER_EQUAL) &&
             cond->left->type == EXPR_VARIABLE &&
             strcmp(cond->left->as.variable.name.identifier, *var_name) == 0) {
        *limit_expr = cond->right;
        *count_up = false;
    }
    else return false;

    // Increment: i = i + step, i = i - step, i++, or i--
    Expr* inc = stmt->increment;

    // Handle i++ / i--
    if (inc->type == EXPR_INCREMENT) {
        IncrementExpr* ie = &inc->as.increment;
        if (strcmp(ie->name.identifier, *var_name) != 0)
            return false;
        *step_val = ie->is_increment ? 1 : -1;
        return (*count_up == (*step_val > 0));
    }

    if (inc->type != EXPR_ASSIGNMENT)
        return false;
    AssignmentExpr* assign = &inc->as.assignment;

    // Target must be the loop variable
    if (assign->target->type != EXPR_VARIABLE ||
        strcmp(assign->target->as.variable.name.identifier, *var_name) != 0)
        return false;

    // Value must be <var> + <literal> or <var> - <literal>
    if (assign->value->type != EXPR_BINARY)
        return false;
    BinaryExpr* step_expr = &assign->value->as.binary;
    if (step_expr->op.type != TOKEN_PLUS && step_expr->op.type != TOKEN_MINUS)
        return false;

    // Left side of addition must be the loop var
    if (step_expr->left->type != EXPR_VARIABLE ||
        strcmp(step_expr->left->as.variable.name.identifier, *var_name) != 0)
        return false;

    // Right side must be an integer literal
    if (step_expr->right->type != EXPR_LITERAL ||
        step_expr->right->as.literal.value.type != VAL_INT64)
        return false;
    int64_t sv = step_expr->right->as.literal.value.as.i64;
    if (step_expr->op.type == TOKEN_MINUS) sv = -sv;
    if (sv == 0) return false;
    *step_val = sv;

    return (*count_up == (sv > 0));
}

void Compiler::compileForStmt(ForStmt* stmt) {
    // Try numeric for-loop optimization: FORPREP/FORLOOP
    const char* var_name = nullptr;
    Expr* start_expr = nullptr;
    Expr* limit_expr = nullptr;
    int64_t step_val = 0;
    bool count_up = true;

    if (isNumericForLoop(stmt, &var_name, &start_expr, &limit_expr,
                         &step_val, &count_up)) {
        beginScope();

        // Reserve 4 consecutive registers: index, limit, step, loop_var
        // Use addLocal with internal names for the 3 hidden registers so the
        // locals vector stays in sync with register indices.
        int base = current_->free_reg;
        addLocal("(for index)");
        addLocal("(for limit)");
        addLocal("(for step)");
        addLocal(var_name);
        int idx_reg = base;
        int limit_reg = base + 1;
        int step_reg = base + 2;

        // Compile start and limit values
        compileExpr(start_expr, idx_reg);
        compileExpr(limit_expr, limit_reg);

        // For <= comparisons, FORLOOP uses <=, which is what we want.
        // For < comparisons, adjust limit: limit = limit - 1 (integer only)
        BinaryExpr* cond = &stmt->condition->as.binary;
        if (count_up && cond->op.type == TOKEN_LESS) {
            emitAsBx(OP_SUBI, (uint8_t)limit_reg, 1);
        } else if (!count_up && cond->op.type == TOKEN_GREATER) {
            emitAsBx(OP_ADDI, (uint8_t)limit_reg, 1);
        }

        // Load step
        if (step_val >= -SBX16_BIAS && step_val <= SBX16_BIAS) {
            emitAsBx(OP_LOADINT, (uint8_t)step_reg, (int)step_val);
        } else {
            int ki = current_->proto->addIntConstant(step_val);
            emitLoadK(step_reg, ki);
        }

        // Use integer-specialized IFORPREP/IFORLOOP since the step
        // is always an integer literal (guaranteed by isNumericForLoop).
        int forprep_pc = current_->proto->currentPC();
        emitAsBx(OP_IFORPREP, (uint8_t)base, 0);  // patch later

        LoopContext loop;
        loop.start_pc = current_->proto->currentPC();
        loop.is_for_loop = true;
        loop.scope_depth = current_->scope_depth;
        current_->loops.push_back(loop);

        // Body
        compileStmt(stmt->body);

        // Patch continue jumps to IFORLOOP
        for (int jmp : current_->loops.back().continue_jumps) {
            patchJump(jmp);
        }

        int forloop_pc = current_->proto->currentPC();
        int body_start = loop.start_pc;
        int back_offset = body_start - (forloop_pc + 1);
        emitAsBx(OP_IFORLOOP, (uint8_t)base, back_offset);

        // Patch FORPREP to jump to FORLOOP
        current_->proto->patchJump(forprep_pc, forloop_pc);

        // Patch break jumps to after FORLOOP
        for (int jmp : current_->loops.back().break_jumps) {
            patchJump(jmp);
        }
        current_->loops.pop_back();

        endScope();
        return;
    }

    // General for-loop (non-numeric pattern)
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
        jmp_exit = compileConditionJump(stmt->condition);
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
    if (!enclosing->proto->source.empty())
        child_fs.proto->source = enclosing->proto->source;
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
        emitSetGlobal(reg, name);
        setFreeReg(save);
    }
}

// --- Return statement ---

void Compiler::compileReturnStmt(ReturnStmt* stmt) {
    currentLine_ = stmt->keyword.line;

    if (stmt->value) {
        // Tail call optimization: if the return value is a direct call,
        // emit OP_TAILCALL instead of OP_CALL + OP_RETURN.
        if (stmt->value->type == EXPR_CALL) {
            compileTailCall(&stmt->value->as.call);
            return;
        }

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
                        make_int64_value((int64_t)elem_count));

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

                case PATTERN_TYPE: {
                    emitABC(OP_TYPEIS, 1, (uint8_t)disc_reg,
                            (uint8_t)pat->as.type_pattern.value_type);
                    match_jumps.push_back(emitJump());
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

        int guard_fail_jump = -1;
        if (sc->guard) {
            int save2 = current_->free_reg;
            int guard_reg = compileExpr(sc->guard);
            emitABC(OP_TEST, (uint8_t)guard_reg, 0, 0);
            guard_fail_jump = emitJump();
            setFreeReg(save2);
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
                            make_int64_value((int64_t)k));
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
                        make_int64_value((int64_t)elem_count));
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
                        make_int64_value((int64_t)elem_count));
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

        if (guard_fail_jump >= 0) {
            patchJump(guard_fail_jump);
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
        fprintf(stderr, "Compiler [%s:%d]: 'break' outside of loop\n",
                current_->proto->source.c_str(), currentLine_);
        return;
    }
    int jmp = emitJump();
    current_->loops.back().break_jumps.push_back(jmp);
}

// --- Continue ---

void Compiler::compileContinueStmt() {
    if (current_->loops.empty()) {
        fprintf(stderr, "Compiler [%s:%d]: 'continue' outside of loop\n",
                current_->proto->source.c_str(), currentLine_);
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

    enum_names_.insert(stmt->name.identifier);

    int save = current_->free_reg;
    int enum_reg = allocReg();

    int name_ki = stringConstant(stmt->name.identifier);
    emitABx(OP_NEWENUM, (uint8_t)enum_reg, (uint16_t)name_ki);

    int idx = 0;
    for (EnumMemberDef* m = stmt->members; m; m = m->next, idx++) {
        int inner_save = current_->free_reg;
        int member_name_ki = stringConstant(m->name.identifier);

        if (m->value) {
            int val_reg = compileExpr(m->value);
            emitABC(OP_ENUMVAL, (uint8_t)enum_reg, (uint8_t)val_reg, makeRK(member_name_ki));
        } else {
            int val_reg = allocReg();
            emitAsBx(OP_LOADINT, (uint8_t)val_reg, idx);
            emitABC(OP_ENUMVAL, (uint8_t)enum_reg, (uint8_t)val_reg, makeRK(member_name_ki));
        }
        setFreeReg(inner_save);
    }

    emitSetGlobal(enum_reg, stmt->name.identifier);

    char enum_var[256];
    snprintf(enum_var, sizeof(enum_var), "__enum_%s", stmt->name.identifier);
    const char* interned = pool_->intern(enum_var)->data;
    emitSetGlobal(enum_reg, interned);

    setFreeReg(save);
}

// ============================================================================
// Peephole optimizer — fuse common instruction sequences into superinstructions
// ============================================================================

void Compiler::peepholeOptimize(Prototype* proto) {
    auto& code = proto->code;
    auto& lines = proto->line_info;
    size_t n = code.size();
    if (n < 2) return;

    // Build a set of jump targets so we don't fuse across them.
    // Any instruction that is a jump target cannot be the second half of a fused pair.
    std::vector<bool> is_target(n, false);
    for (size_t i = 0; i < n; i++) {
        OpCode op = (OpCode)DECODE_OP(code[i]);
        const auto& info = opcode_info(op);

        if (info.format == FMT_ABC_D) {
            i += 2; // skip inline data words
            continue;
        }
        if (info.format == FMT_FUSED2) {
            i += 1; // skip fused second word
            continue;
        }

        int target = -1;
        if (info.format == FMT_sBx) {
            int offset = DECODE_sBx_wide(code[i]);
            target = (int)i + 1 + offset;
        } else if (info.format == FMT_AsBx) {
            if (op == OP_TESTJMP ||
                op == OP_IFORPREP || op == OP_IFORLOOP) {
                int offset = DECODE_sBx(code[i]);
                target = (int)i + 1 + offset;
            }
        }
        if (target >= 0 && target < (int)n) {
            is_target[target] = true;
        }
    }

    // Scan for fusible pairs. We build a new code/lines vector in place.
    std::vector<uint32_t> new_code;
    std::vector<int> new_lines;
    new_code.reserve(n);
    new_lines.reserve(n);

    // Map from old instruction index to new instruction index for jump patching.
    std::vector<int> remap(n, -1);

    for (size_t i = 0; i < n; i++) {
        remap[i] = (int)new_code.size();
        OpCode op = (OpCode)DECODE_OP(code[i]);
        const auto& info = opcode_info(op);

        // Skip multi-word instructions (copy them verbatim)
        if (info.format == FMT_ABC_D) {
            new_code.push_back(code[i]);
            new_lines.push_back(lines[i]);
            if (i + 1 < n) { remap[i+1] = (int)new_code.size(); new_code.push_back(code[i+1]); new_lines.push_back(lines[i+1]); }
            if (i + 2 < n) { remap[i+2] = (int)new_code.size(); new_code.push_back(code[i+2]); new_lines.push_back(lines[i+2]); }
            i += 2;
            continue;
        }
        if (info.format == FMT_FUSED2) {
            new_code.push_back(code[i]);
            new_lines.push_back(lines[i]);
            if (i + 1 < n) { remap[i+1] = (int)new_code.size(); new_code.push_back(code[i+1]); new_lines.push_back(lines[i+1]); }
            i += 1;
            continue;
        }

        // Pattern 1: MOVE A,B,_ + ADDI A,sBx  =>  MOVE_ADDI A,B,_ | AsBx(_,A,sBx)
        if (op == OP_MOVE && i + 1 < n && !is_target[i + 1]) {
            OpCode op2 = (OpCode)DECODE_OP(code[i + 1]);
            if (op2 == OP_ADDI) {
                uint8_t move_a = DECODE_A(code[i]);
                uint8_t move_b = DECODE_B(code[i]);
                uint8_t addi_a = DECODE_A(code[i + 1]);
                if (move_a == addi_a) {
                    new_code.push_back(ENCODE_ABC(OP_MOVE_ADDI, move_a, move_b, 0));
                    new_lines.push_back(lines[i]);
                    remap[i + 1] = (int)new_code.size();
                    new_code.push_back(code[i + 1]); // keep ADDI encoding as data word
                    new_lines.push_back(lines[i + 1]);
                    i++;
                    continue;
                }
            }
        }

        // Pattern 2: GETGLOBAL A,Bx + GETTABLE A,A,RK(C)  =>  GETGLOBAL_GETTABLE
        if (op == OP_GETGLOBAL && i + 1 < n && !is_target[i + 1]) {
            OpCode op2 = (OpCode)DECODE_OP(code[i + 1]);
            if (op2 == OP_GETTABLE) {
                uint8_t gg_a = DECODE_A(code[i]);
                uint16_t gg_bx = DECODE_Bx(code[i]);
                uint8_t gt_a = DECODE_A(code[i + 1]);
                uint8_t gt_b = DECODE_B(code[i + 1]);
                if (gg_a == gt_a && gg_a == gt_b) {
                    new_code.push_back(ENCODE_ABx(OP_GETGLOBAL_GETTABLE, gg_a, gg_bx));
                    new_lines.push_back(lines[i]);
                    remap[i + 1] = (int)new_code.size();
                    new_code.push_back(code[i + 1]); // keep GETTABLE encoding as data word
                    new_lines.push_back(lines[i + 1]);
                    i++;
                    continue;
                }
            }
        }

        // No fusion — copy verbatim
        new_code.push_back(code[i]);
        new_lines.push_back(lines[i]);
    }

    // Fill any unmapped positions (shouldn't happen, but safety)
    for (size_t i = 0; i < n; i++) {
        if (remap[i] < 0) {
            remap[i] = (i > 0) ? remap[i - 1] : 0;
        }
    }

    // Remap jump targets in the new code
    for (size_t i = 0; i < new_code.size(); i++) {
        OpCode op = (OpCode)DECODE_OP(new_code[i]);
        const auto& info = opcode_info(op);

        if (info.format == FMT_ABC_D) { i += 2; continue; }
        if (info.format == FMT_FUSED2) { i += 1; continue; }

        bool is_jump = false;
        int old_target = -1;

        if (info.format == FMT_sBx) {
            // Find original instruction index for this new position
            // We need the reverse map: new_i -> old_i
            // Instead, compute old target from the old offset stored in the instruction
            // Since we kept the original instruction encoding, the offset is relative
            // to the original position. We need to find the original index.
            is_jump = true;
        } else if (info.format == FMT_AsBx) {
            if (op == OP_TESTJMP ||
                op == OP_IFORPREP || op == OP_IFORLOOP) {
                is_jump = true;
            }
        }

        if (!is_jump) continue;

        // Find the original instruction index that maps to new index i
        int orig_i = -1;
        for (size_t j = 0; j < n; j++) {
            if (remap[j] == (int)i) { orig_i = (int)j; break; }
        }
        if (orig_i < 0) continue;

        if (info.format == FMT_sBx) {
            int old_offset = DECODE_sBx_wide(new_code[i]);
            old_target = orig_i + 1 + old_offset;
            if (old_target >= 0 && old_target < (int)n) {
                int new_target = remap[old_target];
                int new_offset = new_target - ((int)i + 1);
                new_code[i] = ENCODE_sBx(op, new_offset);
            }
        } else {
            uint8_t a = DECODE_A(new_code[i]);
            int old_offset = DECODE_sBx(new_code[i]);
            old_target = orig_i + 1 + old_offset;
            if (old_target >= 0 && old_target < (int)n) {
                int new_target = remap[old_target];
                int new_offset = new_target - ((int)i + 1);
                new_code[i] = ENCODE_AsBx(op, a, new_offset);
            }
        }
    }

    code = std::move(new_code);
    lines = std::move(new_lines);
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

// --- Ternary expression ---

int Compiler::compileTernary(TernaryExpr* expr, int dest) {
    int result_reg = (dest >= 0) ? dest : allocReg();

    int jmp_else = compileConditionJump(expr->condition);

    compileExpr(expr->then_expr, result_reg);
    int jmp_end = emitJump();

    patchJump(jmp_else);
    compileExpr(expr->else_expr, result_reg);
    patchJump(jmp_end);

    return result_reg;
}

// --- Function expression (lambda) ---

int Compiler::compileFunctionExpr(FunctionExpr* expr, int dest) {
    currentLine_ = expr->name.line;
    const char* name = expr->name.identifier ? expr->name.identifier : "<lambda>";

    FunctionState* enclosing = current_;
    FunctionState child_fs;
    child_fs.proto = new Prototype();
    child_fs.proto->name = name;
    if (!enclosing->proto->source.empty())
        child_fs.proto->source = enclosing->proto->source;
    child_fs.enclosing = enclosing;
    child_fs.scope_depth = 0;
    child_fs.free_reg = 0;
    child_fs.max_reg = 0;
    current_ = &child_fs;

    beginScope();

    child_fs.proto->num_params = (int)expr->param_count;
    for (size_t i = 0; i < expr->param_count; i++) {
        addLocal(expr->params[i].identifier);
    }

    compileBlock(expr->body, expr->body_count);
    emitReturn(0, 0);
    endScope();

    Prototype* child_proto = child_fs.proto;
    child_proto->num_registers = child_fs.max_reg;

    current_ = enclosing;

    int proto_idx = (int)current_->proto->protos.size();
    current_->proto->protos.push_back(child_proto);

    int result_reg = (dest >= 0) ? dest : allocReg();
    emitABx(OP_CLOSURE, (uint8_t)result_reg, (uint16_t)proto_idx);

    return result_reg;
}

// --- For-in statement ---

void Compiler::compileForInStmt(ForInStmt* stmt) {
    currentLine_ = stmt->var_name.line;
    beginScope();

    int save = current_->free_reg;

    int iter_reg = addLocal("(for iterator)");
    int state_reg = addLocal("(for state)");
    addLocal("(for control)");

    uint8_t num_vars = 1;
    if (stmt->has_two_vars) {
        addLocal(stmt->var_name.identifier);   // R[A+3] = key
        addLocal(stmt->var_name2.identifier);  // R[A+4] = value
        num_vars = 2;
    } else {
        addLocal(stmt->var_name.identifier);   // R[A+3] = value
    }

    compileExpr(stmt->iterable, iter_reg);
    emitABC(OP_LOADNIL, (uint8_t)state_reg, 1, 0);

    LoopContext loop;
    loop.start_pc = (int)current_->proto->code.size();
    loop.scope_depth = current_->scope_depth;
    current_->loops.push_back(loop);

    emitABC(OP_TFORLOOP, (uint8_t)iter_reg, 0, num_vars);

    int jmp_exit = emitJump();

    compileStmt(stmt->body);

    int loop_offset = loop.start_pc - ((int)current_->proto->code.size() + 1);
    current_->proto->code.push_back(ENCODE_sBx(OP_JMP, loop_offset));
    current_->proto->line_info.push_back(currentLine_);

    patchJump(jmp_exit);

    for (int jmp : current_->loops.back().break_jumps) {
        patchJump(jmp);
    }
    current_->loops.pop_back();

    endScope();
    setFreeReg(save);
}

// --- Try-catch ---

void Compiler::compileTryCatchStmt(TryCatchStmt* stmt) {
    currentLine_ = stmt->catch_var.line;
    beginScope();

    int save = current_->free_reg;
    int catch_var_reg = addLocal(stmt->catch_var.identifier);

    int try_begin_pc = emitAsBx(OP_TRY_BEGIN, (uint8_t)catch_var_reg, 0);

    compileBlock(stmt->try_body, stmt->try_body_count);

    emitABC(OP_TRY_END, 0, 0, 0);
    int jmp_past_catch = emitJump();

    int catch_target = (int)current_->proto->code.size();
    int offset = catch_target - (try_begin_pc + 1);
    current_->proto->code[try_begin_pc] = ENCODE_AsBx(OP_TRY_BEGIN, (uint8_t)catch_var_reg, offset);

    compileBlock(stmt->catch_body, stmt->catch_body_count);

    patchJump(jmp_past_catch);

    if (stmt->finally_body && stmt->finally_body_count > 0) {
        compileBlock(stmt->finally_body, stmt->finally_body_count);
    }

    endScope();
    setFreeReg(save);
}

// --- Throw ---

void Compiler::compileThrowStmt(ThrowStmt* stmt) {
    currentLine_ = stmt->keyword.line;

    int save = current_->free_reg;
    int reg = allocReg();

    if (stmt->value) {
        compileExpr(stmt->value, reg);
    } else {
        emitABC(OP_LOADNIL, (uint8_t)reg, 0, 0);
    }

    emitABC(OP_THROW, (uint8_t)reg, 0, 0);
    setFreeReg(save);
}
