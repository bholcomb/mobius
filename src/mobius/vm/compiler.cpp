#include "vm/compiler.h"
#include "state/mobius_state.h"
#include "library/library.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>

// ============================================================================
// Constructor
// ============================================================================

Compiler::Compiler(StringInternPool* pool, MobiusState* state)
    : current_(nullptr), pool_(pool), state_(state)
{
    const PluginFunction* reg = get_library_registry();
    if (reg) {
        for (size_t i = 0; reg[i].name != nullptr; i++) {
            native_return_types_[reg[i].name] = (ValueType)reg[i].return_type;
            global_types_[reg[i].name] = VAL_NATIVE_FUNCTION;
        }
    }
}

// ============================================================================
// Public API
// ============================================================================

Prototype* Compiler::compile(Stmt** statements, size_t count,
                             const char* source_name) {

    had_error_ = false;
    unreachable_ = false;
    FunctionState fs;
    initCompiler(nullptr, source_name);

    compileBlock(statements, count);
    emitReturn(0, 0);

    Prototype* proto = endCompiler();
    if (had_error_) {
        delete proto;
        return nullptr;
    }
    return proto;
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
    if (reg > 255) {
        fprintf(stderr, "Compile error: register overflow (> 255) — function uses too many locals/temporaries\n");
        had_error_ = true;
    }
    return reg;
}

void Compiler::allocRegs(int count) {
    current_->free_reg += count;
    if (current_->free_reg > current_->max_reg) {
        current_->max_reg = current_->free_reg;
    }
    if (current_->free_reg > 256) {
        fprintf(stderr, "Compile error: register overflow (> 255) — function uses too many locals/temporaries\n");
        had_error_ = true;
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

int Compiler::addLocal(const char* name, bool maybe_shared) {
    int reg = allocReg();
    current_->locals.push_back({name, current_->scope_depth, false, VAL_UNKNOWN, maybe_shared});
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
            return addUpvalue(fs, (uint8_t)i, true,
                              enclosing->locals[i].inferred_type,
                              enclosing->locals[i].maybe_shared);
        }
    }

    // Check enclosing function's upvalues (recursive)
    int upvalue = resolveUpvalue(enclosing, name);
    if (upvalue != -1) {
        ValueType uv_type = enclosing->proto->upvalues[upvalue].type;
        bool uv_shared = enclosing->proto->upvalues[upvalue].maybe_shared;
        return addUpvalue(fs, (uint8_t)upvalue, false, uv_type, uv_shared);
    }

    return -1;
}

int Compiler::addUpvalue(FunctionState* fs, uint8_t index, bool is_local,
                         ValueType type, bool maybe_shared) {
    auto& upvalues = fs->proto->upvalues;

    // Reuse existing upvalue if already captured
    for (int i = 0; i < (int)upvalues.size(); i++) {
        if (upvalues[i].index == index && upvalues[i].in_stack == is_local) {
            return i;
        }
    }

    upvalues.push_back({index, is_local, type, maybe_shared});
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
// Type inference
// ============================================================================

ValueType Compiler::localType(int reg) {
    if (reg >= 0 && reg < (int)current_->locals.size())
        return current_->locals[reg].inferred_type;
    return VAL_UNKNOWN;
}

bool Compiler::localMayBeShared(int reg) {
    return reg >= 0 && reg < (int)current_->locals.size() && current_->locals[reg].maybe_shared;
}

ValueType Compiler::globalType(const char* name) {
    auto it = global_types_.find(name);
    if (it != global_types_.end()) return it->second;
    return VAL_UNKNOWN;
}

bool Compiler::globalMayBeShared(const char* name) {
    auto it = global_maybe_shared_.find(name);
    return it != global_maybe_shared_.end() && it->second;
}

ValueType Compiler::nativeReturnType(const char* name) {
    auto it = native_return_types_.find(name);
    if (it != native_return_types_.end()) return it->second;
    return VAL_UNKNOWN;
}

bool Compiler::callMayBeShared(CallExpr* expr) {
    if (!expr) return true;

    Expr* callee = expr->callee;
    if (callee->type != EXPR_VARIABLE) return true;

    const char* name = callee->as.variable.name.identifier;
    if (!name) return true;

    if (current_->proto->name == name &&
        current_->proto->return_type != VAL_UNKNOWN) {
        return current_->proto->return_maybe_shared;
    }

    ValueType native_type = nativeReturnType(name);
    if (native_type != VAL_UNKNOWN) return false;

    for (auto* p : current_->proto->protos) {
        if (p->name == name && p->return_type != VAL_UNKNOWN) {
            return p->return_maybe_shared;
        }
    }

    for (FunctionState* fs = current_->enclosing; fs; fs = fs->enclosing) {
        for (auto* p : fs->proto->protos) {
            if (p->name == name && p->return_type != VAL_UNKNOWN) {
                return p->return_maybe_shared;
            }
        }
    }

    return true;
}

bool Compiler::exprMayBeShared(Expr* expr) {
    if (!expr) return false;

    switch (expr->type) {
        case EXPR_LITERAL:
        case EXPR_BINARY:
        case EXPR_UNARY:
        case EXPR_INCREMENT:
        case EXPR_DECREMENT:
        case EXPR_ARRAY_LITERAL:
        case EXPR_TABLE_LITERAL:
        case EXPR_ENUM_ACCESS:
        case EXPR_FUNCTION:
        case EXPR_SPAWN:
            return false;

        case EXPR_VARIABLE: {
            const char* vname = expr->as.variable.name.identifier;
            int local = resolveLocal(vname);
            if (local >= 0) return localMayBeShared(local);
            int uv = resolveUpvalue(current_, vname);
            if (uv >= 0) return current_->proto->upvalues[uv].maybe_shared;
            return globalMayBeShared(vname);
        }

        case EXPR_ASSIGNMENT:
            return exprMayBeShared(expr->as.assignment.value);

        case EXPR_CALL:
            return callMayBeShared(&expr->as.call);
        case EXPR_AWAIT:
        case EXPR_ARRAY_INDEX:
        case EXPR_TABLE_INDEX:
        case EXPR_TABLE_DOT:
        case EXPR_METHOD_DOT:
        case EXPR_SHARED:
            return true;

        case EXPR_GROUPING:
            return exprMayBeShared(expr->as.grouping.expression);

        case EXPR_TERNARY:
            return exprMayBeShared(expr->as.ternary.then_expr) ||
                   exprMayBeShared(expr->as.ternary.else_expr);

        case EXPR_ATOMIC:
            return exprMayBeShared(expr->as.atomic.body);
    }

    return true;
}

ValueType Compiler::inferExprType(Expr* expr) {
    if (!expr) return VAL_UNKNOWN;

    switch (expr->type) {
        case EXPR_LITERAL: {
            ValueType t = expr->as.literal.value.type;
            return (t == VAL_NIL) ? VAL_UNKNOWN : t;
        }

        case EXPR_VARIABLE: {
            const char* vname = expr->as.variable.name.identifier;
            int local = resolveLocal(vname);
            if (local >= 0) return localType(local);
            int uv = resolveUpvalue(current_, vname);
            if (uv >= 0) return current_->proto->upvalues[uv].type;
            return globalType(vname);
        }

        case EXPR_BINARY: {
            BinaryExpr* bin = &expr->as.binary;
            switch (bin->op.type) {
                case TOKEN_PLUS: case TOKEN_MINUS: case TOKEN_STAR:
                case TOKEN_SLASH: case TOKEN_PERCENT: {
                    ValueType lt = inferExprType(bin->left);
                    ValueType rt = inferExprType(bin->right);
                    if (lt == VAL_INT64 && rt == VAL_INT64) return VAL_INT64;
                    if ((lt == VAL_FLOAT64 || lt == VAL_INT64) &&
                        (rt == VAL_FLOAT64 || rt == VAL_INT64)) {
                        if (lt == VAL_FLOAT64 || rt == VAL_FLOAT64) return VAL_FLOAT64;
                        return VAL_INT64;
                    }
                    if (bin->op.type == TOKEN_PLUS && lt == VAL_STRING && rt == VAL_STRING)
                        return VAL_STRING;
                    return VAL_UNKNOWN;
                }
                case TOKEN_LESS: case TOKEN_LESS_EQUAL:
                case TOKEN_GREATER: case TOKEN_GREATER_EQUAL:
                case TOKEN_EQUAL_EQUAL: case TOKEN_BANG_EQUAL:
                case TOKEN_IS:
                    return VAL_BOOL;
                case TOKEN_AMPERSAND: case TOKEN_PIPE: case TOKEN_CARET:
                case TOKEN_LEFT_SHIFT: case TOKEN_RIGHT_SHIFT:
                    return VAL_INT64;
                case TOKEN_AND: case TOKEN_OR:
                    return VAL_BOOL;
                default:
                    return VAL_UNKNOWN;
            }
        }

        case EXPR_UNARY: {
            if (expr->as.unary.op.type == TOKEN_BANG) return VAL_BOOL;
            if (expr->as.unary.op.type == TOKEN_MINUS) return inferExprType(expr->as.unary.right);
            if (expr->as.unary.op.type == TOKEN_TILDE) return VAL_INT64;
            return VAL_UNKNOWN;
        }

        case EXPR_INCREMENT:
        case EXPR_DECREMENT:
            return VAL_INT64;

        case EXPR_ARRAY_LITERAL:
            return VAL_ARRAY;

        case EXPR_TABLE_LITERAL:
            return VAL_TABLE;

        case EXPR_FUNCTION:
            return VAL_FUNCTION;

        case EXPR_SPAWN:
            return VAL_FUTURE;

        case EXPR_GROUPING:
            return inferExprType(expr->as.grouping.expression);

        case EXPR_TERNARY: {
            ValueType then_t = inferExprType(expr->as.ternary.then_expr);
            ValueType else_t = inferExprType(expr->as.ternary.else_expr);
            if (then_t == VAL_UNKNOWN) return else_t;
            if (else_t == VAL_UNKNOWN) return then_t;
            if (then_t == else_t) return then_t;
            return VAL_UNKNOWN;
        }

        case EXPR_CALL: {
            Expr* callee = expr->as.call.callee;
            if (callee->type == EXPR_VARIABLE) {
                const char* name = callee->as.variable.name.identifier;

                // Self-recursive call: use return type already set (from
                // declaration or earlier return statements in this function)
                if (current_->proto->name == name &&
                    current_->proto->return_type != VAL_UNKNOWN) {
                    return current_->proto->return_type;
                }

                // Check native function return types (stdlib globals)
                ValueType nrt = nativeReturnType(name);
                if (nrt != VAL_UNKNOWN) return nrt;

                // Check if it's a previously compiled function with known return type
                for (auto* p : current_->proto->protos) {
                    if (p->name == name && p->return_type != VAL_UNKNOWN) {
                        return p->return_type;
                    }
                }
                // Check enclosing scopes' prototypes
                for (FunctionState* fs = current_->enclosing; fs; fs = fs->enclosing) {
                    for (auto* p : fs->proto->protos) {
                        if (p->name == name && p->return_type != VAL_UNKNOWN) {
                            return p->return_type;
                        }
                    }
                }
            }
            return VAL_UNKNOWN;
        }

        case EXPR_ASSIGNMENT:
            return inferExprType(expr->as.assignment.value);

        default:
            return VAL_UNKNOWN;
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
        case EXPR_METHOD_DOT:
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
        case EXPR_SPAWN:
            return compileSpawn(&expr->as.spawn, dest);
        case EXPR_AWAIT:
            return compileAwait(&expr->as.await, dest);
        case EXPR_SHARED:
            return compileShared(&expr->as.shared, dest);
        case EXPR_ATOMIC:
            return compileAtomic(&expr->as.atomic, dest);
        default:
            fprintf(stderr, "Compile error [%s]: unknown expression type %d\n",
                    current_->proto->source.c_str(), expr->type);
            had_error_ = true;
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

    // Check loop-invariant hoisted globals — emit a MOVE (not a
    // direct register return) so the caller's contiguous register
    // layout (e.g. for call frames) is preserved.
    for (auto it = hoisted_globals_stack_.rbegin(); it != hoisted_globals_stack_.rend(); ++it) {
        for (const auto& e : it->entries) {
            if (e.name == name) {
                int reg = (dest >= 0) ? dest : allocReg();
                emitABC(OP_MOVE, (uint8_t)reg, (uint8_t)e.reg, 0);
                return reg;
            }
        }
    }

    // Global — use flat slot index when state is available
    int reg = (dest >= 0) ? dest : allocReg();
    emitGetGlobal(reg, name);
    return reg;
}

int Compiler::compileUnwrappedExpr(Expr* expr, int dest) {
    if (!exprMayBeShared(expr)) {
        return compileExpr(expr, dest);
    }

    int raw_reg = compileExpr(expr);
    int reg = (dest >= 0) ? dest : allocReg();
    emitABC(OP_SHARED_LOAD, (uint8_t)reg, (uint8_t)raw_reg, 0);
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

    bool left_maybe_shared = exprMayBeShared(expr->left);
    bool right_maybe_shared = exprMayBeShared(expr->right);

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
                int left_reg = compileUnwrappedExpr(expr->left, reg);
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
                int right_reg = compileUnwrappedExpr(expr->right, reg);
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
                int src_reg = compileUnwrappedExpr(expr->left, reg);
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
                int src_reg = compileUnwrappedExpr(expr->right, reg);
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
    bool left_is_rk = !left_maybe_shared && tryExprAsRK(expr->left, &rk_left);
    bool right_is_rk = !right_maybe_shared && tryExprAsRK(expr->right, &rk_right);

    if (!left_is_rk) {
        int left = compileUnwrappedExpr(expr->left);
        rk_left = (uint8_t)left;
    }
    if (!right_is_rk) {
        int right = compileUnwrappedExpr(expr->right);
        rk_right = (uint8_t)right;
    }

    ValueType lt = inferExprType(expr->left);
    ValueType rt = inferExprType(expr->right);
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
            emitABC(both_i64 ? OP_DIV_II : both_f64 ? OP_DIV_FF : OP_DIV,
                    (uint8_t)reg, rk_left, rk_right);
            break;
        case TOKEN_PERCENT:
            emitABC(both_i64 ? OP_MOD_II : both_f64 ? OP_MOD_FF : OP_MOD,
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
            OpCode cmp = both_i64 ? OP_EQ_II : both_f64 ? OP_EQ_FF : OP_EQ;
            emitABC(cmp, 1, rk_left, rk_right);
            int jmp = emitJump();
            emitABC(OP_LOADBOOL, (uint8_t)reg, 0, 1);
            patchJump(jmp);
            emitABC(OP_LOADBOOL, (uint8_t)reg, 1, 0);
            break;
        }
        case TOKEN_BANG_EQUAL: {
            OpCode cmp = both_i64 ? OP_EQ_II : both_f64 ? OP_EQ_FF : OP_EQ;
            emitABC(cmp, 0, rk_left, rk_right);
            int jmp = emitJump();
            emitABC(OP_LOADBOOL, (uint8_t)reg, 0, 1);
            patchJump(jmp);
            emitABC(OP_LOADBOOL, (uint8_t)reg, 1, 0);
            break;
        }
        case TOKEN_LESS: {
            OpCode cmp = both_i64 ? OP_LT_II : both_f64 ? OP_LT_FF : OP_LT;
            emitABC(cmp, 1, rk_left, rk_right);
            int jmp = emitJump();
            emitABC(OP_LOADBOOL, (uint8_t)reg, 0, 1);
            patchJump(jmp);
            emitABC(OP_LOADBOOL, (uint8_t)reg, 1, 0);
            break;
        }
        case TOKEN_LESS_EQUAL: {
            OpCode cmp = both_i64 ? OP_LE_II : both_f64 ? OP_LE_FF : OP_LE;
            emitABC(cmp, 1, rk_left, rk_right);
            int jmp = emitJump();
            emitABC(OP_LOADBOOL, (uint8_t)reg, 0, 1);
            patchJump(jmp);
            emitABC(OP_LOADBOOL, (uint8_t)reg, 1, 0);
            break;
        }
        case TOKEN_GREATER: {
            // a > b  =>  b < a
            OpCode cmp = both_i64 ? OP_LT_II : both_f64 ? OP_LT_FF : OP_LT;
            emitABC(cmp, 1, rk_right, rk_left);
            int jmp = emitJump();
            emitABC(OP_LOADBOOL, (uint8_t)reg, 0, 1);
            patchJump(jmp);
            emitABC(OP_LOADBOOL, (uint8_t)reg, 1, 0);
            break;
        }
        case TOKEN_GREATER_EQUAL: {
            // a >= b  =>  b <= a
            OpCode cmp = both_i64 ? OP_LE_II : both_f64 ? OP_LE_FF : OP_LE;
            emitABC(cmp, 1, rk_right, rk_left);
            int jmp = emitJump();
            emitABC(OP_LOADBOOL, (uint8_t)reg, 0, 1);
            patchJump(jmp);
            emitABC(OP_LOADBOOL, (uint8_t)reg, 1, 0);
            break;
        }
        default:
            fprintf(stderr, "Compile error [%s:%d]: unknown binary operator %d\n",
                    current_->proto->source.c_str(), currentLine_, expr->op.type);
            had_error_ = true;
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
            fprintf(stderr, "Compile error [%s:%d]: unknown unary operator %d\n",
                    current_->proto->source.c_str(), currentLine_, expr->op.type);
            had_error_ = true;
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
        case EXPR_METHOD_DOT:
            return target->as.table_dot.key.line;
        case EXPR_ARRAY_INDEX:
            return assignment_target_line(target->as.array_index.array);
        case EXPR_TABLE_INDEX:
            return assignment_target_line(target->as.table_index.table);
        default:
            return 0;
    }
}

namespace {
bool type_prefers_plain_binding(ValueType type) {
    switch (type) {
        case VAL_BOOL:
        case VAL_INT64:
        case VAL_UINT64:
        case VAL_FLOAT64:
        case VAL_CHAR:
        case VAL_STRING:
            return true;
        default:
            return false;
    }
}

bool is_scalar_binary_context(TokenType op) {
    switch (op) {
        case TOKEN_PLUS:
        case TOKEN_MINUS:
        case TOKEN_STAR:
        case TOKEN_SLASH:
        case TOKEN_PERCENT:
        case TOKEN_LESS:
        case TOKEN_LESS_EQUAL:
        case TOKEN_GREATER:
        case TOKEN_GREATER_EQUAL:
        case TOKEN_EQUAL_EQUAL:
        case TOKEN_BANG_EQUAL:
        case TOKEN_AMPERSAND:
        case TOKEN_PIPE:
        case TOKEN_CARET:
        case TOKEN_LEFT_SHIFT:
        case TOKEN_RIGHT_SHIFT:
        case TOKEN_AND:
        case TOKEN_OR:
            return true;
        default:
            return false;
    }
}

bool is_scalar_unary_context(TokenType op) {
    switch (op) {
        case TOKEN_MINUS:
        case TOKEN_PLUS:
        case TOKEN_BANG:
        case TOKEN_NOT:
        case TOKEN_TILDE:
            return true;
        default:
            return false;
    }
}

bool expr_requires_shared_identity(Expr* expr, const char* name, bool scalar_context = false) {
    if (!expr) return false;

    switch (expr->type) {
        case EXPR_LITERAL:
        case EXPR_ENUM_ACCESS:
            return false;

        case EXPR_VARIABLE:
            return strcmp(expr->as.variable.name.identifier, name) == 0 && !scalar_context;

        case EXPR_GROUPING:
            return expr_requires_shared_identity(expr->as.grouping.expression, name, scalar_context);

        case EXPR_BINARY: {
            bool child_scalar = is_scalar_binary_context(expr->as.binary.op.type);
            return expr_requires_shared_identity(expr->as.binary.left, name, child_scalar) ||
                   expr_requires_shared_identity(expr->as.binary.right, name, child_scalar);
        }

        case EXPR_UNARY:
            return expr_requires_shared_identity(expr->as.unary.right, name,
                                                 is_scalar_unary_context(expr->as.unary.op.type));

        case EXPR_TERNARY:
            return expr_requires_shared_identity(expr->as.ternary.condition, name, true) ||
                   expr_requires_shared_identity(expr->as.ternary.then_expr, name, scalar_context) ||
                   expr_requires_shared_identity(expr->as.ternary.else_expr, name, scalar_context);

        case EXPR_ASSIGNMENT:
            if (expr->as.assignment.target->type == EXPR_VARIABLE &&
                strcmp(expr->as.assignment.target->as.variable.name.identifier, name) == 0) {
                return true;
            }
            return expr_requires_shared_identity(expr->as.assignment.target, name, false) ||
                   expr_requires_shared_identity(expr->as.assignment.value, name, false);

        case EXPR_INCREMENT:
        case EXPR_DECREMENT:
            return strcmp(expr->as.increment.name.identifier, name) == 0;

        case EXPR_CALL: {
            if (expr_requires_shared_identity(expr->as.call.callee, name, false)) return true;
            for (size_t i = 0; i < expr->as.call.arg_count; i++) {
                if (expr_requires_shared_identity(expr->as.call.arguments[i], name, false)) return true;
            }
            return false;
        }

        case EXPR_ARRAY_LITERAL:
            for (size_t i = 0; i < expr->as.array_literal.element_count; i++) {
                if (expr_requires_shared_identity(expr->as.array_literal.elements[i], name, false)) return true;
            }
            return false;

        case EXPR_ARRAY_INDEX:
            return expr_requires_shared_identity(expr->as.array_index.array, name, false) ||
                   expr_requires_shared_identity(expr->as.array_index.index, name, true);

        case EXPR_TABLE_LITERAL:
            for (size_t i = 0; i < expr->as.table_literal.pair_count; i++) {
                TablePair& pair = expr->as.table_literal.pairs[i];
                if (pair.key && expr_requires_shared_identity(pair.key, name, false)) return true;
                if (expr_requires_shared_identity(pair.value, name, false)) return true;
            }
            return false;

        case EXPR_TABLE_INDEX:
            return expr_requires_shared_identity(expr->as.table_index.table, name, false) ||
                   expr_requires_shared_identity(expr->as.table_index.index, name, true);

        case EXPR_TABLE_DOT:
        case EXPR_METHOD_DOT:
            return expr_requires_shared_identity(expr->as.table_dot.table, name, false);

        case EXPR_FUNCTION:
        case EXPR_SPAWN:
        case EXPR_AWAIT:
        case EXPR_SHARED:
        case EXPR_ATOMIC:
            return true;
    }

    return true;
}

bool stmt_requires_shared_identity(Stmt* stmt, const char* name, ValueType return_type) {
    if (!stmt) return false;

    switch (stmt->type) {
        case STMT_EXPRESSION:
            return expr_requires_shared_identity(stmt->as.expression.expression, name, false);
        case STMT_PRINT:
            return expr_requires_shared_identity(stmt->as.print.expression, name, false);
        case STMT_VAR:
            return expr_requires_shared_identity(stmt->as.var.initializer, name, false);
        case STMT_BLOCK:
            for (size_t i = 0; i < stmt->as.block.count; i++) {
                if (stmt_requires_shared_identity(stmt->as.block.statements[i], name, return_type)) return true;
            }
            return false;
        case STMT_IF:
            return expr_requires_shared_identity(stmt->as.if_stmt.condition, name, true) ||
                   stmt_requires_shared_identity(stmt->as.if_stmt.then_branch, name, return_type) ||
                   stmt_requires_shared_identity(stmt->as.if_stmt.else_branch, name, return_type);
        case STMT_WHILE:
            return expr_requires_shared_identity(stmt->as.while_stmt.condition, name, true) ||
                   stmt_requires_shared_identity(stmt->as.while_stmt.body, name, return_type);
        case STMT_FOR:
            return stmt_requires_shared_identity(stmt->as.for_stmt.initializer, name, return_type) ||
                   expr_requires_shared_identity(stmt->as.for_stmt.condition, name, true) ||
                   expr_requires_shared_identity(stmt->as.for_stmt.increment, name, false) ||
                   stmt_requires_shared_identity(stmt->as.for_stmt.body, name, return_type);
        case STMT_RETURN:
            return expr_requires_shared_identity(stmt->as.return_stmt.value, name,
                                                 type_prefers_plain_binding(return_type));
        case STMT_SWITCH:
            if (expr_requires_shared_identity(stmt->as.switch_stmt.discriminant, name, false)) return true;
            for (size_t i = 0; i < stmt->as.switch_stmt.case_count; i++) {
                SwitchCase* case_clause = stmt->as.switch_stmt.cases[i];
                if (case_clause->guard && expr_requires_shared_identity(case_clause->guard, name, true)) return true;
                for (size_t j = 0; j < case_clause->body_count; j++) {
                    if (stmt_requires_shared_identity(case_clause->body[j], name, return_type)) return true;
                }
            }
            for (size_t i = 0; i < stmt->as.switch_stmt.default_body_count; i++) {
                if (stmt_requires_shared_identity(stmt->as.switch_stmt.default_body[i], name, return_type)) return true;
            }
            return false;
        case STMT_FOR_IN:
            return expr_requires_shared_identity(stmt->as.for_in_stmt.iterable, name, false) ||
                   stmt_requires_shared_identity(stmt->as.for_in_stmt.body, name, return_type);
        case STMT_TRY_CATCH:
            for (size_t i = 0; i < stmt->as.try_catch_stmt.try_body_count; i++) {
                if (stmt_requires_shared_identity(stmt->as.try_catch_stmt.try_body[i], name, return_type)) return true;
            }
            for (size_t i = 0; i < stmt->as.try_catch_stmt.catch_body_count; i++) {
                if (stmt_requires_shared_identity(stmt->as.try_catch_stmt.catch_body[i], name, return_type)) return true;
            }
            for (size_t i = 0; i < stmt->as.try_catch_stmt.finally_body_count; i++) {
                if (stmt_requires_shared_identity(stmt->as.try_catch_stmt.finally_body[i], name, return_type)) return true;
            }
            return false;
        case STMT_THROW:
            return expr_requires_shared_identity(stmt->as.throw_stmt.value, name, false);
        default:
            return false;
    }
}

std::vector<bool> compute_param_shared_identity_flags(Token* params, size_t param_count,
                                                      Stmt** body, size_t body_count,
                                                      ValueType return_type) {
    std::vector<bool> needs_identity(param_count, false);
    for (size_t i = 0; i < param_count; i++) {
        const char* name = params[i].identifier;
        for (size_t j = 0; j < body_count; j++) {
            if (stmt_requires_shared_identity(body[j], name, return_type)) {
                needs_identity[i] = true;
                break;
            }
        }
    }
    return needs_identity;
}
} // namespace

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

    auto compile_index_assignment = [&](Expr* container_expr, Expr* index_expr, OpCode set_op) -> int {
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
        emitABC(set_op, (uint8_t)container_reg, rk_key, (uint8_t)value_reg);

        return finish_assignment(save_reg, value_reg);
    };

    switch (expr->target->type) {
        case EXPR_VARIABLE: {
            const char* name = expr->target->as.variable.name.identifier;
            bool rhs_maybe_shared = exprMayBeShared(expr->value);

            int local = resolveLocal(name);
            if (local >= 0) {
                ValueType target_t = localType(local);
                ValueType rhs_t = inferExprType(expr->value);
                bool keep_plain = !current_->locals[local].maybe_shared &&
                                  rhs_maybe_shared &&
                                  type_prefers_plain_binding(target_t);

                if (target_t != VAL_UNKNOWN && rhs_t != VAL_UNKNOWN && rhs_t != target_t) {
                    fprintf(stderr, "Compile error [%s:%d]: cannot assign type %d to variable '%s' "
                            "of type %d\n",
                            current_->proto->source.c_str(), currentLine_,
                            (int)rhs_t, name, (int)target_t);
                }

                int save_reg = current_->free_reg;
                bool target_maybe_shared = keep_plain
                    ? false
                    : (current_->locals[local].maybe_shared || rhs_maybe_shared);
                current_->locals[local].maybe_shared = target_maybe_shared;
                int value_reg;
                if (target_maybe_shared) {
                    emitABC(OP_LOCK_SHARED, (uint8_t)local, 0, 0);
                    value_reg = compileExpr(expr->value);
                    emitABC(OP_SHARED_STORE, (uint8_t)local, (uint8_t)value_reg, 0);
                    emitABC(OP_UNLOCK_SHARED, (uint8_t)local, 0, 0);
                } else if (keep_plain) {
                    value_reg = compileUnwrappedExpr(expr->value, local);
                } else {
                    value_reg = compileExpr(expr->value, local);
                }

                if (target_t != VAL_UNKNOWN && rhs_t == VAL_UNKNOWN) {
                    emitABC(OP_TYPECHECK_LOCKED, (uint8_t)local, 0, 0);
                    current_->proto->has_type_locks = true;
                }
                if (target_t == VAL_UNKNOWN) {
                    emitABC(OP_TYPECHECK_LOCKED, (uint8_t)local, 0, 0);
                    current_->proto->has_type_locks = true;
                }

                setFreeReg(save_reg);
                if (dest >= 0 && dest != local) {
                    emitABC(OP_MOVE, (uint8_t)dest, (uint8_t)local, 0);
                    return dest;
                }
                return local;
            }

            int upvalue = resolveUpvalue(current_, name);
            if (upvalue >= 0) {
                int save_reg = current_->free_reg;
                int lock_reg = allocReg();
                int reg = (dest >= 0) ? dest : allocReg();
                bool target_maybe_shared = current_->proto->upvalues[upvalue].maybe_shared || rhs_maybe_shared;
                current_->proto->upvalues[upvalue].maybe_shared = target_maybe_shared;
                emitABC(OP_GETUPVAL, (uint8_t)lock_reg, (uint8_t)upvalue, 0);
                if (target_maybe_shared) {
                    emitABC(OP_LOCK_SHARED, (uint8_t)lock_reg, 0, 0);
                }
                compileExpr(expr->value, reg);
                emitABC(OP_SETUPVAL, (uint8_t)reg, (uint8_t)upvalue, 0);
                if (target_maybe_shared) {
                    emitABC(OP_UNLOCK_SHARED, (uint8_t)lock_reg, 0, 0);
                }
                setFreeReg(save_reg);
                return reg;
            }

            int save_reg = current_->free_reg;
            int lock_reg = allocReg();
            int reg = (dest >= 0) ? dest : allocReg();
            ValueType target_t = globalType(name);
            bool keep_plain = !globalMayBeShared(name) &&
                              rhs_maybe_shared &&
                              type_prefers_plain_binding(target_t);
            bool target_maybe_shared = keep_plain ? false : (globalMayBeShared(name) || rhs_maybe_shared);
            if (target_maybe_shared) {
                global_maybe_shared_[name] = true;
                emitGetGlobal(lock_reg, name);
                emitABC(OP_LOCK_SHARED, (uint8_t)lock_reg, 0, 0);
            }
            if (keep_plain) {
                compileUnwrappedExpr(expr->value, reg);
            } else {
                compileExpr(expr->value, reg);
            }
            emitSetGlobal(reg, name);
            if (target_maybe_shared) {
                emitABC(OP_UNLOCK_SHARED, (uint8_t)lock_reg, 0, 0);
            }
            setFreeReg(save_reg);
            return reg;
        }

        case EXPR_ARRAY_INDEX:
            return compile_index_assignment(expr->target->as.array_index.array,
                                            expr->target->as.array_index.index,
                                            OP_INDEX_SET);

        case EXPR_TABLE_INDEX:
            return compile_index_assignment(expr->target->as.table_index.table,
                                            expr->target->as.table_index.index,
                                            OP_INDEX_SET);

        case EXPR_TABLE_DOT: {
            int save_reg = current_->free_reg;
            int table_reg = compileExpr(expr->target->as.table_dot.table);
            int ki = stringConstant(expr->target->as.table_dot.key.identifier);
            uint8_t rk_key = makeRK(ki);
            int value_reg = compileExpr(expr->value);
            emitABC(OP_INDEX_SET, (uint8_t)table_reg, rk_key, (uint8_t)value_reg);
            return finish_assignment(save_reg, value_reg);
        }

        default:
            fprintf(stderr, "Compile error [%s:%d]: invalid assignment target %d\n",
                    current_->proto->source.c_str(), currentLine_, expr->target->type);
            had_error_ = true;
            return -1;
    }
}

// --- Call ---

int Compiler::compileCall(CallExpr* expr, int dest) {
    currentLine_ = expr->paren.line;

    if (expr->callee->type == EXPR_VARIABLE && expr->arg_count == 2) {
        const char* fn_name = expr->callee->as.variable.name.identifier;
        if (fn_name && strcmp(fn_name, "array_push") == 0) {
            int save = current_->free_reg;
            int arr_reg = compileExpr(expr->arguments[0]);
            int val_reg = compileExpr(expr->arguments[1]);
            emitABC(OP_ARRAY_PUSH, (uint8_t)arr_reg, (uint8_t)val_reg, 0);
            setFreeReg(save);
            int result_reg = (dest >= 0) ? dest : allocReg();
            emitABC(OP_LOADNIL, (uint8_t)result_reg, 0, 0);
            return result_reg;
        }
    }

    int base = current_->free_reg;
    int func_reg = allocReg();

    bool is_method = (expr->callee->type == EXPR_METHOD_DOT);

    if (is_method) {
        TableDotExpr* dot = &expr->callee->as.table_dot;
        int tbl_reg = compileExpr(dot->table);
        int ki = stringConstant(dot->key.identifier);
        uint8_t rk_key = makeRK(ki);
        emitABC(OP_SELF, (uint8_t)func_reg, (uint8_t)tbl_reg, rk_key);
        setFreeReg(func_reg + 2);
    } else if (expr->callee->type == EXPR_TABLE_DOT) {
        compileTableDot(&expr->callee->as.table_dot, func_reg);
    } else {
        compileExpr(expr->callee, func_reg);
    }

    for (size_t i = 0; i < expr->arg_count; i++) {
        int arg_reg = allocReg();
        compileExpr(expr->arguments[i], arg_reg);
    }

    int nargs = (int)expr->arg_count + 1 + (is_method ? 1 : 0);  // B = nargs + 1, +1 for self
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

    bool is_method = (expr->callee->type == EXPR_METHOD_DOT);

    if (is_method) {
        TableDotExpr* dot = &expr->callee->as.table_dot;
        int tbl_reg = compileExpr(dot->table);
        int ki = stringConstant(dot->key.identifier);
        uint8_t rk_key = makeRK(ki);
        emitABC(OP_SELF, (uint8_t)func_reg, (uint8_t)tbl_reg, rk_key);
        setFreeReg(func_reg + 2);
    } else if (expr->callee->type == EXPR_TABLE_DOT) {
        compileTableDot(&expr->callee->as.table_dot, func_reg);
    } else {
        compileExpr(expr->callee, func_reg);
    }

    for (size_t i = 0; i < expr->arg_count; i++) {
        int arg_reg = allocReg();
        compileExpr(expr->arguments[i], arg_reg);
    }

    int nargs = (int)expr->arg_count + 1 + (is_method ? 1 : 0);
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

        int ki = current_->proto->addIntConstant((int64_t)i);
        emitABC(OP_INDEX_SET, (uint8_t)reg, makeRK(ki), (uint8_t)val_reg);
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

    emitABC(OP_INDEX_GET, (uint8_t)reg, (uint8_t)arr_reg, (uint8_t)idx_reg);

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
        emitABC(OP_INDEX_SET, (uint8_t)reg, rk_key, (uint8_t)val_reg);

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

    emitABC(OP_INDEX_GET, (uint8_t)reg, (uint8_t)tbl_reg, (uint8_t)idx_reg);

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
        emitABC(OP_INDEX_GET, (uint8_t)reg, (uint8_t)tbl_reg, rk_key);
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
        int save = current_->free_reg;
        if (expr->is_prefix) {
            int result_reg = (dest >= 0) ? dest : allocReg();
            emitABC(OP_LOCK_SHARED, (uint8_t)local, 0, 0);
            emitABC(OP_SHARED_LOAD, (uint8_t)result_reg, (uint8_t)local, 0);
            emitABC(op, (uint8_t)result_reg, (uint8_t)result_reg, 0);
            emitABC(OP_SHARED_STORE, (uint8_t)local, (uint8_t)result_reg, 0);
            emitABC(OP_UNLOCK_SHARED, (uint8_t)local, 0, 0);
            if (dest < 0) setFreeReg(result_reg + 1);
            else setFreeReg(save);
            return result_reg;
        } else {
            int reg = (dest >= 0) ? dest : allocReg();
            int work_reg = allocReg();
            emitABC(OP_LOCK_SHARED, (uint8_t)local, 0, 0);
            emitABC(OP_SHARED_LOAD, (uint8_t)reg, (uint8_t)local, 0);
            emitABC(OP_SHARED_LOAD, (uint8_t)work_reg, (uint8_t)local, 0);
            emitABC(op, (uint8_t)work_reg, (uint8_t)work_reg, 0);
            emitABC(OP_SHARED_STORE, (uint8_t)local, (uint8_t)work_reg, 0);
            emitABC(OP_UNLOCK_SHARED, (uint8_t)local, 0, 0);
            if (dest < 0) setFreeReg(reg + 1);
            else setFreeReg(save);
            return reg;
        }
    }

    // Global increment: load, modify, store
    int save = current_->free_reg;
    int reg = (dest >= 0) ? dest : allocReg();
    int tmp = allocReg();

    emitGetGlobal(tmp, name);
    emitABC(OP_LOCK_SHARED, (uint8_t)tmp, 0, 0);
    if (expr->is_prefix) {
        emitABC(OP_SHARED_LOAD, (uint8_t)reg, (uint8_t)tmp, 0);
        emitABC(op, (uint8_t)reg, (uint8_t)reg, 0);
        emitSetGlobal(reg, name);
    } else {
        emitABC(OP_SHARED_LOAD, (uint8_t)reg, (uint8_t)tmp, 0);
        emitABC(OP_SHARED_LOAD, (uint8_t)tmp, (uint8_t)tmp, 0);
        emitABC(op, (uint8_t)tmp, (uint8_t)tmp, 0);
        emitSetGlobal(tmp, name);
    }
    emitGetGlobal(tmp, name);
    emitABC(OP_UNLOCK_SHARED, (uint8_t)tmp, 0, 0);
    if (dest < 0) setFreeReg(reg + 1);
    else setFreeReg(save);
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
            unreachable_ = true;
            break;
        case STMT_SWITCH:
            compileSwitchStmt(&stmt->as.switch_stmt);
            break;
        case STMT_BREAK:
            compileBreakStmt();
            unreachable_ = true;
            break;
        case STMT_CONTINUE:
            compileContinueStmt();
            unreachable_ = true;
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
            unreachable_ = true;
            break;
        case STMT_YIELD:
            emitABC(OP_YIELD, 0, 0, 0);
            break;
        default:
            fprintf(stderr, "Compile error [%s]: unknown statement type %d\n",
                    current_->proto->source.c_str(), stmt->type);
            had_error_ = true;
            break;
    }
}

void Compiler::compileBlock(Stmt** stmts, size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (unreachable_) break;
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
    int save = current_->free_reg;
    int func_reg = allocReg();

    // Use hoisted "print" if available (MOVE), otherwise GETGLOBAL
    bool found_hoisted = false;
    for (auto it = hoisted_globals_stack_.rbegin(); it != hoisted_globals_stack_.rend(); ++it) {
        for (const auto& e : it->entries) {
            if (e.name == "print") {
                emitABC(OP_MOVE, (uint8_t)func_reg, (uint8_t)e.reg, 0);
                found_hoisted = true;
                break;
            }
        }
        if (found_hoisted) break;
    }
    if (!found_hoisted) {
        emitGetGlobal(func_reg, "print");
    }

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
        ValueType inferred = stmt->initializer ? inferExprType(stmt->initializer) : VAL_UNKNOWN;
        bool rhs_maybe_shared = stmt->initializer ? exprMayBeShared(stmt->initializer) : false;
        bool keep_plain = rhs_maybe_shared && type_prefers_plain_binding(inferred);
        bool maybe_shared = rhs_maybe_shared && !keep_plain;
        int reg = addLocal(name, maybe_shared);
        current_->locals.back().inferred_type = inferred;

        if (stmt->initializer) {
            if (keep_plain) {
                compileUnwrappedExpr(stmt->initializer, reg);
            } else {
                compileExpr(stmt->initializer, reg);
            }
        } else {
            emitABC(OP_LOADNIL, (uint8_t)reg, 0, 0);
        }
        if (stmt->is_annotated) {
            emitABC(OP_TYPECHECK, (uint8_t)reg, (uint8_t)stmt->type_hint, 0);
        }
        if (inferred == VAL_UNKNOWN && stmt->initializer) {
            emitABC(OP_TYPELOCK, (uint8_t)reg, 0, 0);
            current_->proto->has_type_locks = true;
        }
    } else {
        // Global variable
        ValueType inferred = stmt->initializer ? inferExprType(stmt->initializer) : VAL_UNKNOWN;
        bool rhs_maybe_shared = stmt->initializer ? exprMayBeShared(stmt->initializer) : false;
        bool keep_plain = rhs_maybe_shared && type_prefers_plain_binding(inferred);
        bool maybe_shared = rhs_maybe_shared && !keep_plain;
        int save = current_->free_reg;
        int reg = allocReg();
        if (stmt->initializer) {
            if (keep_plain) {
                compileUnwrappedExpr(stmt->initializer, reg);
            } else {
                compileExpr(stmt->initializer, reg);
            }
        } else {
            emitABC(OP_LOADNIL, (uint8_t)reg, 0, 0);
        }
        if (stmt->is_annotated) {
            emitABC(OP_TYPECHECK, (uint8_t)reg, (uint8_t)stmt->type_hint, 0);
            inferred = (ValueType)stmt->type_hint;
        }
        emitSetGlobal(reg, name);
        if (inferred != VAL_UNKNOWN) {
            global_types_[name] = inferred;
        }
        if (maybe_shared) {
            global_maybe_shared_[name] = true;
        }
        setFreeReg(save);
    }
}

// --- Block statement ---

void Compiler::compileBlockStmt(BlockStmt* stmt) {
    beginScope();
    bool saved_unreachable = unreachable_;
    unreachable_ = false;
    compileBlock(stmt->statements, stmt->count);
    bool block_unreachable = unreachable_;
    unreachable_ = saved_unreachable || block_unreachable;
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

            bool left_maybe_shared = exprMayBeShared(bin->left);
            bool right_maybe_shared = exprMayBeShared(bin->right);

            // Try compare-with-immediate: RHS is an integer literal in sBx range
            if (bin->right->type == EXPR_LITERAL &&
                bin->right->as.literal.value.type == VAL_INT64) {
                int64_t iv = bin->right->as.literal.value.as.i64;
                if (iv >= -SBX16_BIAS && iv <= SBX16_BIAS) {
                    int save = current_->free_reg;
                    int left = compileUnwrappedExpr(bin->left);
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
                    int right = compileUnwrappedExpr(bin->right);
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
            if (!left_maybe_shared && tryExprAsRK(bin->left, &rk_left)) {
            } else {
                int left = compileUnwrappedExpr(bin->left);
                rk_left = (uint8_t)left;
            }
            if (!right_maybe_shared && tryExprAsRK(bin->right, &rk_right)) {
            } else {
                int right = compileUnwrappedExpr(bin->right);
                rk_right = (uint8_t)right;
            }

            ValueType lt = inferExprType(bin->left);
            ValueType rt = inferExprType(bin->right);
            bool ii = (lt == VAL_INT64 && rt == VAL_INT64);
            bool ff = (lt == VAL_FLOAT64 && rt == VAL_FLOAT64);

            switch (op) {
                case TOKEN_LESS:
                    emitABC(ii ? OP_LT_II : ff ? OP_LT_FF : OP_LT, 0, rk_left, rk_right); break;
                case TOKEN_LESS_EQUAL:
                    emitABC(ii ? OP_LE_II : ff ? OP_LE_FF : OP_LE, 0, rk_left, rk_right); break;
                case TOKEN_GREATER:
                    emitABC(ii ? OP_LT_II : ff ? OP_LT_FF : OP_LT, 0, rk_right, rk_left); break;
                case TOKEN_GREATER_EQUAL:
                    emitABC(ii ? OP_LE_II : ff ? OP_LE_FF : OP_LE, 0, rk_right, rk_left); break;
                case TOKEN_EQUAL_EQUAL:
                    emitABC(ii ? OP_EQ_II : ff ? OP_EQ_FF : OP_EQ, 0, rk_left, rk_right); break;
                case TOKEN_BANG_EQUAL:
                    emitABC(ii ? OP_EQ_II : ff ? OP_EQ_FF : OP_EQ, 1, rk_left, rk_right); break;
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
    unreachable_ = false;
    compileStmt(stmt->then_branch);
    bool then_unreachable = unreachable_;

    if (stmt->else_branch) {
        unreachable_ = false;
        int jmp_end = emitJump();
        patchJump(jmp_else);
        compileStmt(stmt->else_branch);
        bool else_unreachable = unreachable_;
        patchJump(jmp_end);
        unreachable_ = then_unreachable && else_unreachable;
    } else {
        patchJump(jmp_else);
        unreachable_ = false;
    }
}

// --- While statement ---

void Compiler::compileWhileStmt(WhileStmt* stmt) {
    int hoisted = hoistLoopGlobals(stmt->body);

    LoopContext loop;
    loop.start_pc = current_->proto->currentPC();
    loop.is_for_loop = false;
    loop.scope_depth = current_->scope_depth;
    current_->loops.push_back(loop);

    int jmp_exit = compileConditionJump(stmt->condition);

    unreachable_ = false;
    compileStmt(stmt->body);

    // Jump back to condition
    if (!unreachable_) {
        int loop_start = current_->loops.back().start_pc;
        int offset = loop_start - (current_->proto->currentPC() + 1);
        current_->proto->emitJump(offset, currentLine_);
    }

    patchJump(jmp_exit);

    // Patch break jumps
    for (int jmp : current_->loops.back().break_jumps) {
        patchJump(jmp);
    }
    current_->loops.pop_back();

    if (hoisted > 0) hoisted_globals_stack_.pop_back();
    unreachable_ = false;
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

        int hoisted = hoistLoopGlobals(stmt->body);

        // Reserve 4 consecutive registers: index, limit, step, loop_var
        // Use addLocal with internal names for the 3 hidden registers so the
        // locals vector stays in sync with register indices.
        int base = current_->free_reg;
        addLocal("(for index)");
        current_->locals.back().inferred_type = VAL_INT64;
        addLocal("(for limit)");
        current_->locals.back().inferred_type = VAL_INT64;
        addLocal("(for step)");
        current_->locals.back().inferred_type = VAL_INT64;
        addLocal(var_name);
        current_->locals.back().inferred_type = VAL_INT64;
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
        unreachable_ = false;
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
        if (hoisted > 0) hoisted_globals_stack_.pop_back();
        endScope();
        unreachable_ = false;
        return;
    }

    // General for-loop (non-numeric pattern)
    beginScope();

    // Initializer
    if (stmt->initializer) {
        compileStmt(stmt->initializer);
    }

    int hoisted = hoistLoopGlobals(stmt->body);

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
    unreachable_ = false;
    compileStmt(stmt->body);

    // Patch continue jumps to here (the increment step)
    for (int jmp : current_->loops.back().continue_jumps) {
        patchJump(jmp);
    }

    unreachable_ = false;
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
    if (hoisted > 0) hoisted_globals_stack_.pop_back();
    endScope();
    unreachable_ = false;
}

// --- Function statement ---

void Compiler::compileFunctionStmt(FunctionStmt* stmt) {
    currentLine_ = stmt->name.line;
    const char* name = stmt->name.identifier;

    bool saved_unreachable = unreachable_;
    unreachable_ = false;
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

    if (stmt->return_type != VAL_UNKNOWN)
        child_fs.proto->return_type = stmt->return_type;

    current_ = &child_fs;

    beginScope();

    child_fs.proto->num_params = (int)stmt->param_count;
    child_fs.proto->param_unwrap_on_entry.assign(stmt->param_count, 0);
    std::vector<bool> param_needs_identity =
        compute_param_shared_identity_flags(stmt->params, stmt->param_count,
                                            stmt->body, stmt->body_count,
                                            child_fs.proto->return_type);
    for (size_t i = 0; i < stmt->param_count; i++) {
        bool maybe_shared = param_needs_identity[i];
        addLocal(stmt->params[i].identifier, maybe_shared);
        if (stmt->param_types && stmt->param_types[i] != VAL_UNKNOWN)
            current_->locals.back().inferred_type = stmt->param_types[i];
        child_fs.proto->param_unwrap_on_entry[i] = maybe_shared ? 0 : 1;
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
    unreachable_ = saved_unreachable;

    // Add child prototype to parent and emit CLOSURE
    int proto_idx = (int)current_->proto->protos.size();
    current_->proto->protos.push_back(child_proto);

    if (current_->scope_depth > 0) {
        int reg = addLocal(name);
        current_->locals.back().inferred_type = VAL_FUNCTION;
        emitABx(OP_CLOSURE, (uint8_t)reg, (uint16_t)proto_idx);
    } else {
        int save = current_->free_reg;
        int reg = allocReg();
        emitABx(OP_CLOSURE, (uint8_t)reg, (uint16_t)proto_idx);
        emitSetGlobal(reg, name);
        global_types_[name] = VAL_FUNCTION;
        setFreeReg(save);
    }
}

// --- Return statement ---

void Compiler::compileReturnStmt(ReturnStmt* stmt) {
    currentLine_ = stmt->keyword.line;

    if (stmt->value) {
        ValueType ret_t = inferExprType(stmt->value);
        if (exprMayBeShared(stmt->value)) {
            current_->proto->return_maybe_shared = true;
        }
        if (ret_t != VAL_UNKNOWN) {
            if (current_->proto->return_type == VAL_UNKNOWN) {
                current_->proto->return_type = ret_t;
            } else if (current_->proto->return_type != ret_t) {
                fprintf(stderr, "Compile error [%s:%d]: function has inconsistent return types: "
                        "expected type %d, got type %d\n",
                        current_->proto->source.c_str(), currentLine_,
                        (int)current_->proto->return_type, (int)ret_t);
            }
        }

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
                        emitABC(OP_INDEX_GET, (uint8_t)field_reg,
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
                        emitABC(OP_INDEX_GET, (uint8_t)local_reg,
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

                    emitABC(OP_INDEX_GET, (uint8_t)elem_reg,
                            (uint8_t)disc_reg, (uint8_t)idx_reg);
                    // Push to rest array via native call
                    // Use SETTABLE with integer key: rest[idx - elem_count] = elem
                    int offset_ki = current_->proto->addConstant(
                        make_int64_value((int64_t)elem_count));
                    int offset_reg = allocReg();
                    emitABC(OP_SUB, (uint8_t)offset_reg,
                            (uint8_t)idx_reg, makeRK(offset_ki));
                    emitABC(OP_INDEX_SET, (uint8_t)rest_reg,
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
                    emitABC(OP_INDEX_GET, (uint8_t)local_reg,
                            (uint8_t)disc_reg, makeRK(key_ki));
                }
            }
        }

        unreachable_ = false;
        compileBlock(sc->body, sc->body_count);
        endScope();
        unreachable_ = false;

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
        unreachable_ = false;
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
    unreachable_ = false;
}

// --- Break ---

void Compiler::compileBreakStmt() {
    if (current_->loops.empty()) {
        fprintf(stderr, "Compile error [%s:%d]: 'break' outside of loop\n",
                current_->proto->source.c_str(), currentLine_);
        had_error_ = true;
        return;
    }
    int jmp = emitJump();
    current_->loops.back().break_jumps.push_back(jmp);
}

// --- Continue ---

void Compiler::compileContinueStmt() {
    if (current_->loops.empty()) {
        fprintf(stderr, "Compile error [%s:%d]: 'continue' outside of loop\n",
                current_->proto->source.c_str(), currentLine_);
        had_error_ = true;
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
// ============================================================================
// Loop-invariant global hoisting — collect global names used as call targets
// ============================================================================

void Compiler::collectGlobalCallNamesFromExpr(Expr* expr,
                                               std::unordered_set<std::string>& names) {
    if (!expr) return;
    switch (expr->type) {
        case EXPR_CALL: {
            CallExpr* c = &expr->as.call;
            if (c->callee->type == EXPR_VARIABLE) {
                const char* name = c->callee->as.variable.name.identifier;
                if (name && resolveLocal(name) < 0 &&
                    resolveUpvalue(current_, name) < 0) {
                    names.insert(name);
                }
            }
            collectGlobalCallNamesFromExpr(c->callee, names);
            for (size_t i = 0; i < c->arg_count; i++)
                collectGlobalCallNamesFromExpr(c->arguments[i], names);
            break;
        }
        case EXPR_BINARY:
            collectGlobalCallNamesFromExpr(expr->as.binary.left, names);
            collectGlobalCallNamesFromExpr(expr->as.binary.right, names);
            break;
        case EXPR_UNARY:
            collectGlobalCallNamesFromExpr(expr->as.unary.right, names);
            break;
        case EXPR_ASSIGNMENT:
            collectGlobalCallNamesFromExpr(expr->as.assignment.target, names);
            collectGlobalCallNamesFromExpr(expr->as.assignment.value, names);
            break;
        case EXPR_GROUPING:
            collectGlobalCallNamesFromExpr(expr->as.grouping.expression, names);
            break;
        case EXPR_ARRAY_LITERAL:
            for (size_t i = 0; i < expr->as.array_literal.element_count; i++)
                collectGlobalCallNamesFromExpr(expr->as.array_literal.elements[i], names);
            break;
        case EXPR_ARRAY_INDEX:
            collectGlobalCallNamesFromExpr(expr->as.array_index.array, names);
            collectGlobalCallNamesFromExpr(expr->as.array_index.index, names);
            break;
        case EXPR_TABLE_LITERAL:
            for (size_t i = 0; i < expr->as.table_literal.pair_count; i++) {
                collectGlobalCallNamesFromExpr(expr->as.table_literal.pairs[i].key, names);
                collectGlobalCallNamesFromExpr(expr->as.table_literal.pairs[i].value, names);
            }
            break;
        case EXPR_TABLE_INDEX:
            collectGlobalCallNamesFromExpr(expr->as.table_index.table, names);
            collectGlobalCallNamesFromExpr(expr->as.table_index.index, names);
            break;
        case EXPR_TABLE_DOT:
        case EXPR_METHOD_DOT:
            collectGlobalCallNamesFromExpr(expr->as.table_dot.table, names);
            break;
        case EXPR_TERNARY:
            collectGlobalCallNamesFromExpr(expr->as.ternary.condition, names);
            collectGlobalCallNamesFromExpr(expr->as.ternary.then_expr, names);
            collectGlobalCallNamesFromExpr(expr->as.ternary.else_expr, names);
            break;
        case EXPR_SPAWN:
            collectGlobalCallNamesFromExpr(expr->as.spawn.callee, names);
            for (size_t i = 0; i < expr->as.spawn.arg_count; i++)
                collectGlobalCallNamesFromExpr(expr->as.spawn.arguments[i], names);
            break;
        case EXPR_AWAIT:
            collectGlobalCallNamesFromExpr(expr->as.await.operand, names);
            break;
        case EXPR_SHARED:
            collectGlobalCallNamesFromExpr(expr->as.shared.operand, names);
            break;
        case EXPR_ATOMIC:
            collectGlobalCallNamesFromExpr(expr->as.atomic.body, names);
            break;
        default:
            break;
    }
}

void Compiler::collectGlobalCallNames(Stmt* stmt,
                                       std::unordered_set<std::string>& names) {
    if (!stmt) return;
    switch (stmt->type) {
        case STMT_EXPRESSION:
            collectGlobalCallNamesFromExpr(stmt->as.expression.expression, names);
            break;
        case STMT_PRINT:
            collectGlobalCallNamesFromExpr(stmt->as.print.expression, names);
            break;
        case STMT_VAR:
            collectGlobalCallNamesFromExpr(stmt->as.var.initializer, names);
            break;
        case STMT_BLOCK:
            for (size_t i = 0; i < stmt->as.block.count; i++)
                collectGlobalCallNames(stmt->as.block.statements[i], names);
            break;
        case STMT_IF:
            collectGlobalCallNamesFromExpr(stmt->as.if_stmt.condition, names);
            collectGlobalCallNames(stmt->as.if_stmt.then_branch, names);
            collectGlobalCallNames(stmt->as.if_stmt.else_branch, names);
            break;
        case STMT_WHILE:
            collectGlobalCallNamesFromExpr(stmt->as.while_stmt.condition, names);
            collectGlobalCallNames(stmt->as.while_stmt.body, names);
            break;
        case STMT_FOR:
            collectGlobalCallNames(stmt->as.for_stmt.initializer, names);
            collectGlobalCallNamesFromExpr(stmt->as.for_stmt.condition, names);
            collectGlobalCallNamesFromExpr(stmt->as.for_stmt.increment, names);
            collectGlobalCallNames(stmt->as.for_stmt.body, names);
            break;
        case STMT_FOR_IN:
            collectGlobalCallNamesFromExpr(stmt->as.for_in_stmt.iterable, names);
            collectGlobalCallNames(stmt->as.for_in_stmt.body, names);
            break;
        case STMT_RETURN:
            collectGlobalCallNamesFromExpr(stmt->as.return_stmt.value, names);
            break;
        case STMT_THROW:
            collectGlobalCallNamesFromExpr(stmt->as.throw_stmt.value, names);
            break;
        case STMT_TRY_CATCH:
            for (size_t i = 0; i < stmt->as.try_catch_stmt.try_body_count; i++)
                collectGlobalCallNames(stmt->as.try_catch_stmt.try_body[i], names);
            for (size_t i = 0; i < stmt->as.try_catch_stmt.catch_body_count; i++)
                collectGlobalCallNames(stmt->as.try_catch_stmt.catch_body[i], names);
            if (stmt->as.try_catch_stmt.finally_body) {
                for (size_t i = 0; i < stmt->as.try_catch_stmt.finally_body_count; i++)
                    collectGlobalCallNames(stmt->as.try_catch_stmt.finally_body[i], names);
            }
            break;
        case STMT_SWITCH:
            collectGlobalCallNamesFromExpr(stmt->as.switch_stmt.discriminant, names);
            for (size_t i = 0; i < stmt->as.switch_stmt.case_count; i++) {
                SwitchCase* sc = stmt->as.switch_stmt.cases[i];
                collectGlobalCallNamesFromExpr(sc->guard, names);
                for (size_t j = 0; j < sc->body_count; j++)
                    collectGlobalCallNames(sc->body[j], names);
            }
            if (stmt->as.switch_stmt.default_body) {
                for (size_t j = 0; j < stmt->as.switch_stmt.default_body_count; j++)
                    collectGlobalCallNames(stmt->as.switch_stmt.default_body[j], names);
            }
            break;
        default:
            break;
    }
}

// hoistLoopGlobals — pre-load globals used as call targets before a loop body.
// Returns the number of registers consumed (for cleanup after loop).
int Compiler::hoistLoopGlobals(Stmt* body) {
    std::unordered_set<std::string> globals;
    collectGlobalCallNames(body, globals);
    if (globals.empty()) return 0;

    HoistedGlobals hg;
    for (const auto& name : globals) {
        std::string local_name = "(hoisted:" + name + ")";
        int reg = addLocal(pool_->intern(local_name.c_str())->data);
        emitGetGlobal(reg, name.c_str());
        hg.entries.push_back({name, reg});
    }
    hoisted_globals_stack_.push_back(std::move(hg));
    return (int)globals.size();
}

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

        // Pattern 2: GETGLOBAL A,Bx + INDEX_GET A,A,RK(C)  =>  GETGLOBAL_INDEX_GET
        if (op == OP_GETGLOBAL && i + 1 < n && !is_target[i + 1]) {
            OpCode op2 = (OpCode)DECODE_OP(code[i + 1]);
            if (op2 == OP_INDEX_GET) {
                uint8_t gg_a = DECODE_A(code[i]);
                uint16_t gg_bx = DECODE_Bx(code[i]);
                uint8_t gt_a = DECODE_A(code[i + 1]);
                uint8_t gt_b = DECODE_B(code[i + 1]);
                if (gg_a == gt_a && gg_a == gt_b) {
                    new_code.push_back(ENCODE_ABx(OP_GETGLOBAL_INDEX_GET, gg_a, gg_bx));
                    new_lines.push_back(lines[i]);
                    remap[i + 1] = (int)new_code.size();
                    new_code.push_back(code[i + 1]); // keep INDEX_GET encoding as data word
                    new_lines.push_back(lines[i + 1]);
                    i++;
                    continue;
                }
            }
        }

        // Pattern 3: GETGLOBAL A,Bx + CALL/CALL_PLAIN A,B,C  =>  fused GETGLOBAL_CALL(_PLAIN)
        if (op == OP_GETGLOBAL && i + 1 < n && !is_target[i + 1]) {
            OpCode op2 = (OpCode)DECODE_OP(code[i + 1]);
            if (op2 == OP_CALL || op2 == OP_CALL_PLAIN) {
                uint8_t gg_a = DECODE_A(code[i]);
                uint16_t gg_bx = DECODE_Bx(code[i]);
                uint8_t call_a = DECODE_A(code[i + 1]);
                if (gg_a == call_a) {
                    OpCode fused = (op2 == OP_CALL_PLAIN) ? OP_GETGLOBAL_CALL_PLAIN
                                                          : OP_GETGLOBAL_CALL;
                    new_code.push_back(ENCODE_ABx(fused, gg_a, gg_bx));
                    new_lines.push_back(lines[i]);
                    remap[i + 1] = (int)new_code.size();
                    new_code.push_back(code[i + 1]); // keep CALL encoding as data word
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

    bool saved_unreachable = unreachable_;
    unreachable_ = false;
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

    if (expr->return_type != VAL_UNKNOWN)
        child_fs.proto->return_type = expr->return_type;

    current_ = &child_fs;

    beginScope();

    child_fs.proto->num_params = (int)expr->param_count;
    child_fs.proto->param_unwrap_on_entry.assign(expr->param_count, 0);
    std::vector<bool> param_needs_identity =
        compute_param_shared_identity_flags(expr->params, expr->param_count,
                                            expr->body, expr->body_count,
                                            child_fs.proto->return_type);
    for (size_t i = 0; i < expr->param_count; i++) {
        bool maybe_shared = param_needs_identity[i];
        addLocal(expr->params[i].identifier, maybe_shared);
        if (expr->param_types && expr->param_types[i] != VAL_UNKNOWN)
            current_->locals.back().inferred_type = expr->param_types[i];
        child_fs.proto->param_unwrap_on_entry[i] = maybe_shared ? 0 : 1;
    }

    compileBlock(expr->body, expr->body_count);
    emitReturn(0, 0);
    endScope();

    Prototype* child_proto = child_fs.proto;
    child_proto->num_registers = child_fs.max_reg;

    current_ = enclosing;
    unreachable_ = saved_unreachable;

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

    unreachable_ = false;
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
    unreachable_ = false;
}

// --- Try-catch ---

void Compiler::compileTryCatchStmt(TryCatchStmt* stmt) {
    currentLine_ = stmt->catch_var.line;
    beginScope();

    int save = current_->free_reg;
    int catch_var_reg = addLocal(stmt->catch_var.identifier);

    int try_begin_pc = emitAsBx(OP_TRY_BEGIN, (uint8_t)catch_var_reg, 0);

    unreachable_ = false;
    compileBlock(stmt->try_body, stmt->try_body_count);
    bool try_unreachable = unreachable_;

    emitABC(OP_TRY_END, 0, 0, 0);
    int jmp_past_catch = emitJump();

    int catch_target = (int)current_->proto->code.size();
    int offset = catch_target - (try_begin_pc + 1);
    current_->proto->code[try_begin_pc] = ENCODE_AsBx(OP_TRY_BEGIN, (uint8_t)catch_var_reg, offset);

    unreachable_ = false;
    compileBlock(stmt->catch_body, stmt->catch_body_count);
    bool catch_unreachable = unreachable_;

    patchJump(jmp_past_catch);

    if (stmt->finally_body && stmt->finally_body_count > 0) {
        unreachable_ = false;
        compileBlock(stmt->finally_body, stmt->finally_body_count);
    }

    unreachable_ = try_unreachable && catch_unreachable;
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

// OP_SPAWN A B C -- spawn function R[B] with C-1 args; result (future) into R[A]
int Compiler::compileSpawn(SpawnExpr* expr, int dest) {
    int base = current_->free_reg;
    int func_reg = allocReg();

    compileExpr(expr->callee, func_reg);

    for (size_t i = 0; i < expr->arg_count; i++) {
        int arg_reg = allocReg();
        compileExpr(expr->arguments[i], arg_reg);
    }

    int nargs = (int)expr->arg_count + 1;

    int result_reg = (dest >= 0) ? dest : func_reg;
    emitABC(OP_SPAWN, (uint8_t)result_reg, (uint8_t)func_reg, (uint8_t)nargs);

    setFreeReg(base);
    if (dest < 0) {
        result_reg = allocReg();
        if (result_reg != func_reg) {
            emitABC(OP_MOVE, (uint8_t)result_reg, (uint8_t)func_reg, 0);
        }
    }
    return result_reg;
}

// OP_AWAIT A B -- await future in R[B], result into R[A]
int Compiler::compileAwait(AwaitExpr* expr, int dest) {
    int operand_reg = compileExpr(expr->operand);
    int result_reg = (dest >= 0) ? dest : operand_reg;
    emitABC(OP_AWAIT, (uint8_t)result_reg, (uint8_t)operand_reg, 0);
    return result_reg;
}

// OP_SHARE A -- mark R[A] as shared (deep propagation to nested containers)
int Compiler::compileShared(SharedExpr* expr, int dest) {
    int result_reg = compileExpr(expr->operand, dest);
    emitABC(OP_SHARE, (uint8_t)result_reg, 0, 0);
    return result_reg;
}

static Expr* findAtomicContainer(Expr* body) {
    if (!body) return nullptr;
    if (body->type == EXPR_ASSIGNMENT) {
        Expr* target = body->as.assignment.target;
        if (target->type == EXPR_VARIABLE) return target;
        if (target->type == EXPR_ARRAY_INDEX) return target->as.array_index.array;
        if (target->type == EXPR_TABLE_INDEX) return target->as.table_index.table;
        if (target->type == EXPR_TABLE_DOT)   return target->as.table_dot.table;
    }
    if (body->type == EXPR_VARIABLE) return body;
    if (body->type == EXPR_ARRAY_INDEX) return body->as.array_index.array;
    if (body->type == EXPR_TABLE_INDEX) return body->as.table_index.table;
    if (body->type == EXPR_TABLE_DOT)   return body->as.table_dot.table;
    return nullptr;
}

int Compiler::compileAtomic(AtomicExpr* expr, int dest) {
    Expr* container_expr = findAtomicContainer(expr->body);
    if (!container_expr) {
        fprintf(stderr, "Compile error [%s:%d]: atomic() requires an expression on a "
                "shared value or shared array/table element (e.g. atomic(counter = counter + 1))\n",
                current_->proto->source.c_str(), currentLine_);
        had_error_ = true;
        return -1;
    }

    int container_reg = compileExpr(container_expr);
    emitABC(OP_ATOMIC_BEGIN, (uint8_t)container_reg, 0, 0);
    int result_reg = compileExpr(expr->body, dest);
    emitABC(OP_ATOMIC_END, (uint8_t)container_reg, 0, 0);
    return result_reg;
}
