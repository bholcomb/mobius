#include "vm/vm.h"
#include "state/mobius_state.h"
#include "state/environment.h"
#include "data/table.h"
#include "data/array.h"
#include "data/enum.h"
#include "data/function.h"
#include "data/metamethods.h"
#include "plugin/module_registry.h"
#include "internal/string_intern.h"

#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <cstdlib>
#include <cmath>
#include <new>

// ============================================================================
// Integer extraction helpers (duplicated from eval_arithmatic.cpp so the VM
// dispatch loop can inline them without cross-TU overhead)
// ============================================================================

inline int64_t MobiusVM::vm_extract_int64(const Value& v) {
    return v.as.i64;
}

inline uint64_t MobiusVM::vm_extract_uint64(const Value& v) {
    return v.as.u64;
}

inline double MobiusVM::vm_extract_double(const Value& v) {
    if (v.type == VAL_FLOAT64)  return v.as.double_val;
    if (v.type == VAL_UINT64)   return (double)v.as.u64;
    if (v.type == VAL_INT64)  return (double)v.as.i64;
    return 0.0;
}

inline bool MobiusVM::vm_use_unsigned(const Value& l, const Value& r) {
    return l.type == VAL_UINT64 || r.type == VAL_UINT64;
}

// ============================================================================
// Constructor
// ============================================================================

MobiusVM::MobiusVM(MobiusState* state)
    : state_(state), global_env_(state->globalEnv()) {}

// ============================================================================
// Error handling
// ============================================================================

void MobiusVM::runtimeError(const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    int line = currentLine();
    state_->setError(1, buf, nullptr, line, 0, nullptr);
}

int MobiusVM::currentLine() const {
    if (call_stack_.empty()) return 0;
    const CallInfo& ci = call_stack_.back();
    int pc = (int)(ci.ip - ci.proto->code.data()) - 1;
    if (pc >= 0 && pc < (int)ci.proto->line_info.size())
        return ci.proto->line_info[pc];
    return 0;
}

// ============================================================================
// Public entry point
// ============================================================================

int MobiusVM::execute(Prototype* proto) {
    registers_.resize(proto->num_registers + 256, Value());

    size_t depth_before = call_stack_.size();

    CallInfo ci;
    ci.proto = proto;
    ci.ip = proto->code.data();
    ci.base = 0;
    ci.nresults = 0;
    call_stack_.push_back(ci);

    int rc = run(depth_before);

    if (!call_stack_.empty() && call_stack_.size() > depth_before) {
        call_stack_.pop_back();
    }
    return rc;
}

// ============================================================================
// Native function bridge
// ============================================================================

int MobiusVM::callNative(MobiusCFunction func, int func_reg, int nargs, int nresults) {
    ExecutionContext* ctx = state_->mainContext();
    CallInfo& caller = call_stack_.back();

    // Point the C-API stack directly at the argument slots in the register array.
    // Arguments live at R[func_reg+1] .. R[func_reg+nargs].
    int args_base = caller.base + func_reg + 1;

    // Ensure there is room for the native function to push results beyond the args.
    int needed = args_base + nargs + 16;
    if (needed > (int)registers_.size()) {
        registers_.resize(needed, Value());
    }

    NativeCallContext nctx;
    nctx.registers = registers_.data();
    nctx.base      = args_base;
    nctx.top       = args_base + nargs;
    nctx.capacity  = (int)registers_.size();
    state_->setNativeContext(&nctx);

    ctx->pushFrame("native", nullptr, currentLine(), 0,
                   FUNCTION_TYPE_NATIVE, nullptr, nullptr);
    int rc = func(state_, nargs);
    ctx->popFrame();

    state_->setNativeContext(nullptr);

    if (rc < 0) return -1;

    // Results were pushed into registers_[args_base..nctx.top-1].
    // Shift them to start at func_reg (overwriting the function slot).
    int dest = caller.base + func_reg;
    int n = (nresults == 0) ? rc : (nresults - 1);
    for (int i = 0; i < n; i++) {
        if (args_base + i < nctx.top) {
            registers_[dest + i] = registers_[args_base + i];
        } else {
            registers_[dest + i] = Value();
        }
    }

    return 0;
}

// ============================================================================
// Mobius function call
// ============================================================================

int MobiusVM::callFunction(CallInfo& caller, int func_reg, int nargs, int nresults) {
    Value& func_val = R(caller, func_reg);

    if (func_val.type == VAL_NATIVE_FUNCTION) {
        return callNative(func_val.as.native_function, func_reg, nargs, nresults);
    }

    if (func_val.type != VAL_FUNCTION || !func_val.as.function) {
        runtimeError("Attempt to call a non-function value (type: %s)",
                     value_type_name(func_val.type));
        return -1;
    }

    MobiusFunction* mf = func_val.as.function;

    if (!mf->proto) {
        runtimeError("Function '%s' has no bytecode prototype",
                     mf->name ? mf->name->data : "anonymous");
        return -1;
    }

    if ((int)mf->param_count != nargs) {
        runtimeError("Function '%s' expects %zu arguments but got %d",
                     mf->name ? mf->name->data : "anonymous", mf->param_count, nargs);
        return -1;
    }

    Prototype* child = mf->proto;

    // Child frame base is placed right after the func register + args.
    // Args are in caller's R[func_reg+1] .. R[func_reg+nargs].
    // Child sees them as R[0]..R[nargs-1] relative to child_base.
    int child_base = caller.base + func_reg + 1;
    int needed = child_base + child->num_registers + 16;
    if (needed > (int)registers_.size()) {
        registers_.resize(needed, Value());
    }

    // caller.ip is already saved by OP_CALL before calling us.
    CallInfo child_ci;
    child_ci.proto = child;
    child_ci.ip = child->code.data();
    child_ci.base = child_base;
    child_ci.nresults = nresults;

    // Propagate captured upvalues from the closure
    if (mf->upvalues && mf->upvalue_count > 0) {
        child_ci.open_upvalues.resize(mf->upvalue_count);
        for (int u = 0; u < mf->upvalue_count; u++) {
            child_ci.open_upvalues[u] = mf->upvalues[u];
        }
    }

    call_stack_.push_back(child_ci);

    return 1;  // signal: switched frame, caller updates dispatch locals
}

// ============================================================================
// Upvalue management
// ============================================================================

void MobiusVM::closeUpvalues(CallInfo& ci, int from_reg) {
    for (auto* uv : ci.open_upvalues) {
        if (uv->is_open) {
            int reg_idx = (int)(uv->location - registers_.data());
            if (reg_idx >= ci.base + from_reg) {
                uv->closed = *uv->location;
                uv->location = &uv->closed;
                uv->is_open = false;
            }
        }
    }
}

// ============================================================================
// VM-native metamethod dispatch
// ============================================================================

int MobiusVM::callMetamethod(const Value& table_val, MobiusString* mm_name,
                             const Value& lhs, const Value& rhs, Value& out) {
    if (table_val.type != VAL_TABLE || !table_val.as.table) return 0;

    Value method = table_val.as.table->getMetamethod(mm_name);
    if (method.type == VAL_NIL) return 0;

    if (method.type == VAL_NATIVE_FUNCTION) {
        ExecutionContext* ctx = state_->mainContext();

        // Place lhs and rhs in scratch registers beyond the current frame.
        int caller_base = call_stack_.back().base;
        int caller_regs = call_stack_.back().proto->num_registers;
        int scratch = caller_base + caller_regs;

        int needed = scratch + 4;
        if (needed > (int)registers_.size())
            registers_.resize(needed, Value());

        registers_[scratch]     = lhs;
        registers_[scratch + 1] = rhs;

        NativeCallContext nctx;
        nctx.registers = registers_.data();
        nctx.base      = scratch;
        nctx.top       = scratch + 2;
        nctx.capacity  = (int)registers_.size();
        state_->setNativeContext(&nctx);

        ctx->pushFrame("metamethod", nullptr, currentLine(), 0,
                       FUNCTION_TYPE_NATIVE, nullptr, nullptr);
        int rc = method.as.native_function(state_, 2);
        ctx->popFrame();
        state_->setNativeContext(nullptr);

        if (rc < 0) {
            runtimeError("Metamethod '%s' failed", mm_name->data);
            return -1;
        }
        if (rc > 0 && nctx.top > scratch) {
            out = registers_[scratch];
        } else {
            out = Value();
        }
        return 1;
    }

    if (method.type == VAL_FUNCTION && method.as.function) {
        MobiusFunction* mf = method.as.function;
        if (!mf->proto) {
            runtimeError("Metamethod '%s' has no bytecode prototype", mm_name->data);
            return -1;
        }
        if ((int)mf->param_count != 2) {
            runtimeError("Metamethod '%s' expects 2 arguments but got %zu params",
                         mm_name->data, mf->param_count);
            return -1;
        }

        int caller_base = call_stack_.back().base;
        int caller_num_regs = call_stack_.back().proto->num_registers;
        int scratch = caller_base + caller_num_regs;

        Prototype* child = mf->proto;
        int needed = scratch + 3 + child->num_registers + 16;
        if (needed > (int)registers_.size())
            registers_.resize(needed, Value());

        registers_[scratch]     = method;
        registers_[scratch + 1] = lhs;
        registers_[scratch + 2] = rhs;

        int child_base = scratch + 1;
        CallInfo child_ci;
        child_ci.proto = child;
        child_ci.ip = child->code.data();
        child_ci.base = child_base;
        child_ci.nresults = 2;

        size_t stop_depth = call_stack_.size();
        call_stack_.push_back(child_ci);

        int rc = run(stop_depth);

        if (rc < 0) return -1;

        out = registers_[scratch];
        return 1;
    }

    runtimeError("'%s' metamethod must be a function", mm_name->data);
    return -1;
}

// ============================================================================
// Main dispatch loop
// ============================================================================

int MobiusVM::run(size_t base_depth) {
    CallInfo* ci = &call_stack_.back();
    uint32_t* ip = ci->ip;
    Prototype* proto = ci->proto;
    int base = ci->base;
    Value* regs = registers_.data() + base;

    #define RA(inst)  regs[DECODE_A(inst)]
    #define RB(inst)  regs[DECODE_B(inst)]
    #define RC(inst)  regs[DECODE_C(inst)]
    #define RKB(inst) RK(*ci, DECODE_B(inst))
    #define RKC(inst) RK(*ci, DECODE_C(inst))
    #define KBx(inst) proto->constants[DECODE_Bx(inst)]
    #define REFRESH_FRAME() do { \
        ci = &call_stack_.back(); \
        proto = ci->proto; \
        base = ci->base; \
        regs = registers_.data() + base; \
    } while(0)

    for (;;) {
        uint32_t inst = *ip++;

        switch (DECODE_OP(inst)) {

        // ================================================================
        // Data movement
        // ================================================================

        case OP_MOVE: {
            RA(inst) = RB(inst);
            break;
        }

        case OP_LOADK: {
            RA(inst) = KBx(inst);
            break;
        }

        case OP_LOADNIL: {
            int a = DECODE_A(inst);
            int b = DECODE_B(inst);
            for (int i = a; i <= a + b; i++)
                regs[i] = Value();
            break;
        }

        case OP_LOADBOOL: {
            RA(inst) = make_bool_value(DECODE_B(inst) != 0);
            if (DECODE_C(inst)) ip++;
            break;
        }

        case OP_LOADINT: {
            int sbx = DECODE_sBx(inst);
            RA(inst) = make_int64_value((int64_t)sbx);
            break;
        }

        // ================================================================
        // Globals
        // ================================================================

        case OP_GETGLOBAL: {
            int slot = DECODE_Bx(inst);
            const Value& gv = state_->globalSlot(slot);
            if (__builtin_expect(!(gv.flags & VAL_FLAG_DEFINED), 0)) {
                runtimeError("Undefined variable '%s'", state_->globalSlotName(slot));
                return -1;
            }
            RA(inst) = gv;
            break;
        }

        case OP_SETGLOBAL: {
            int slot = DECODE_Bx(inst);
            Value& gv = state_->globalSlot(slot);
            if (__builtin_expect(!!(gv.flags & VAL_FLAG_READONLY), 0)) {
                runtimeError("Cannot assign to read-only variable '%s'", state_->globalSlotName(slot));
                return -1;
            }
            gv = RA(inst);
            gv.flags |= VAL_FLAG_DEFINED;
            break;
        }

        // ================================================================
        // Upvalues
        // ================================================================

        case OP_GETUPVAL: {
            int b = DECODE_B(inst);
            if (b < (int)ci->open_upvalues.size() && ci->open_upvalues[b]) {
                RA(inst) = *ci->open_upvalues[b]->location;
            } else {
                RA(inst) = Value();
            }
            break;
        }

        case OP_SETUPVAL: {
            int b = DECODE_B(inst);
            if (b < (int)ci->open_upvalues.size() && ci->open_upvalues[b]) {
                *ci->open_upvalues[b]->location = RA(inst);
            }
            break;
        }

        // ================================================================
        // Tables and arrays
        // ================================================================

        case OP_NEWTABLE: {
            Table* tbl = new (std::nothrow) Table(state_, DECODE_C(inst));
            if (!tbl) { runtimeError("Failed to allocate table"); return -1; }
            RA(inst) = make_table_value(tbl);
            break;
        }

        case OP_NEWARRAY: {
            ArrayValue* arr = new (std::nothrow) ArrayValue(DECODE_B(inst));
            if (!arr) { runtimeError("Failed to allocate array"); return -1; }
            RA(inst) = make_array_value(arr);
            break;
        }

        case OP_GETTABLE: {
            const Value& tbl = RB(inst);
            const Value& key = RKC(inst);

            if (tbl.type == VAL_TABLE && tbl.as.table) {
                RA(inst) = tbl.as.table->get(key);
            } else if (tbl.type == VAL_ARRAY && tbl.as.array) {
                if (key.type == VAL_INT64) {
                    int64_t idx = vm_extract_int64(key);
                    if (idx >= 0 && idx < (int64_t)tbl.as.array->length()) {
                        RA(inst) = tbl.as.array->get((size_t)idx);
                    } else {
                        RA(inst) = Value();
                    }
                } else {
                    runtimeError("Array index must be an integer");
                    return -1;
                }
            } else {
                runtimeError("Attempt to index a %s value", value_type_name(tbl.type));
                return -1;
            }
            break;
        }

        case OP_SETTABLE: {
            Value& tbl = RA(inst);
            const Value& key = RKB(inst);
            const Value& val = RKC(inst);

            if (tbl.type == VAL_TABLE && tbl.as.table) {
                tbl.as.table->set(key, val);
            } else if (tbl.type == VAL_ARRAY && tbl.as.array) {
                if (key.type == VAL_INT64) {
                    int64_t idx = vm_extract_int64(key);
                    if (idx >= 0) {
                        while ((int64_t)tbl.as.array->length() <= idx)
                            tbl.as.array->push(Value());
                        tbl.as.array->set((size_t)idx, val);
                    }
                } else {
                    runtimeError("Array index must be an integer");
                    return -1;
                }
            } else {
                runtimeError("Attempt to index a %s value", value_type_name(tbl.type));
                return -1;
            }
            break;
        }

        // ================================================================
        // Arithmetic — fully inline, no tree-walker delegation
        // ================================================================

        case OP_ADD: {
            const Value& lhs = RKB(inst);
            const Value& rhs = RKC(inst);
            if ((lhs.type == VAL_INT64 || lhs.type == VAL_UINT64) &&
                (rhs.type == VAL_INT64 || rhs.type == VAL_UINT64)) {
                if (vm_use_unsigned(lhs, rhs))
                    RA(inst) = make_uint64_value(vm_extract_uint64(lhs) + vm_extract_uint64(rhs));
                else
                    RA(inst) = make_int64_value(vm_extract_int64(lhs) + vm_extract_int64(rhs));
            } else if (lhs.type == VAL_STRING || rhs.type == VAL_STRING) {
                std::string result;
                auto append_val = [&](const Value& v) {
                    if (v.type == VAL_STRING && v.as.string) {
                        result.append(v.as.string->data, v.as.string->length);
                    } else {
                        char* s = value_to_string(v);
                        if (s) { result.append(s); free(s); }
                    }
                };
                append_val(lhs);
                append_val(rhs);
                RA(inst) = make_string_value_from_cstr(state_, result.c_str());
            } else if (lhs.type == VAL_FLOAT64 || rhs.type == VAL_FLOAT64) {
                RA(inst) = make_float_value(vm_extract_double(lhs) + vm_extract_double(rhs));
            } else if (lhs.type == VAL_TABLE || rhs.type == VAL_TABLE) {
                const Value& tbl = (lhs.type == VAL_TABLE) ? lhs : rhs;
                Value out;
                ci->ip = ip;
                int rc = callMetamethod(tbl, state_->metamethods()->add(), lhs, rhs, out);
                REFRESH_FRAME();
                if (rc < 0) return -1;
                if (rc == 0) { runtimeError("Cannot add: no __add metamethod on table"); return -1; }
                RA(inst) = out;
            } else {
                runtimeError("Cannot add these types");
                return -1;
            }
            break;
        }

        case OP_SUB: {
            const Value& lhs = RKB(inst);
            const Value& rhs = RKC(inst);
            if ((lhs.type == VAL_INT64 || lhs.type == VAL_UINT64) &&
                (rhs.type == VAL_INT64 || rhs.type == VAL_UINT64)) {
                if (vm_use_unsigned(lhs, rhs))
                    RA(inst) = make_uint64_value(vm_extract_uint64(lhs) - vm_extract_uint64(rhs));
                else
                    RA(inst) = make_int64_value(vm_extract_int64(lhs) - vm_extract_int64(rhs));
            } else if (lhs.type == VAL_FLOAT64 || rhs.type == VAL_FLOAT64) {
                RA(inst) = make_float_value(vm_extract_double(lhs) - vm_extract_double(rhs));
            } else if (lhs.type == VAL_TABLE || rhs.type == VAL_TABLE) {
                const Value& tbl = (lhs.type == VAL_TABLE) ? lhs : rhs;
                Value out;
                ci->ip = ip;
                int rc = callMetamethod(tbl, state_->metamethods()->sub(), lhs, rhs, out);
                REFRESH_FRAME();
                if (rc < 0) return -1;
                if (rc == 0) { runtimeError("Cannot subtract: no __sub metamethod on table"); return -1; }
                RA(inst) = out;
            } else {
                runtimeError("Cannot subtract these types");
                return -1;
            }
            break;
        }

        case OP_MUL: {
            const Value& lhs = RKB(inst);
            const Value& rhs = RKC(inst);
            if ((lhs.type == VAL_INT64 || lhs.type == VAL_UINT64) &&
                (rhs.type == VAL_INT64 || rhs.type == VAL_UINT64)) {
                if (vm_use_unsigned(lhs, rhs))
                    RA(inst) = make_uint64_value(vm_extract_uint64(lhs) * vm_extract_uint64(rhs));
                else
                    RA(inst) = make_int64_value(vm_extract_int64(lhs) * vm_extract_int64(rhs));
            } else if (lhs.type == VAL_FLOAT64 || rhs.type == VAL_FLOAT64) {
                RA(inst) = make_float_value(vm_extract_double(lhs) * vm_extract_double(rhs));
            } else if (lhs.type == VAL_TABLE || rhs.type == VAL_TABLE) {
                const Value& tbl = (lhs.type == VAL_TABLE) ? lhs : rhs;
                Value out;
                ci->ip = ip;
                int rc = callMetamethod(tbl, state_->metamethods()->mul(), lhs, rhs, out);
                REFRESH_FRAME();
                if (rc < 0) return -1;
                if (rc == 0) { runtimeError("Cannot multiply: no __mul metamethod on table"); return -1; }
                RA(inst) = out;
            } else {
                runtimeError("Cannot multiply these types");
                return -1;
            }
            break;
        }

        case OP_DIV: {
            const Value& lhs = RKB(inst);
            const Value& rhs = RKC(inst);
            if (lhs.type == VAL_TABLE || rhs.type == VAL_TABLE) {
                const Value& tbl = (lhs.type == VAL_TABLE) ? lhs : rhs;
                Value out;
                ci->ip = ip;
                int rc = callMetamethod(tbl, state_->metamethods()->div(), lhs, rhs, out);
                REFRESH_FRAME();
                if (rc < 0) return -1;
                if (rc == 0) { runtimeError("Cannot divide: no __div metamethod on table"); return -1; }
                RA(inst) = out;
            } else {
                double lv = vm_extract_double(lhs);
                double rv = vm_extract_double(rhs);
                if (rv == 0.0) { runtimeError("Division by zero"); return -1; }
                RA(inst) = make_float_value(lv / rv);
            }
            break;
        }

        case OP_MOD: {
            const Value& lhs = RKB(inst);
            const Value& rhs = RKC(inst);
            if ((lhs.type == VAL_INT64 || lhs.type == VAL_UINT64) &&
                (rhs.type == VAL_INT64 || rhs.type == VAL_UINT64)) {
                if (vm_use_unsigned(lhs, rhs)) {
                    uint64_t lv = vm_extract_uint64(lhs);
                    uint64_t rv = vm_extract_uint64(rhs);
                    if (rv == 0) { runtimeError("Modulo by zero"); return -1; }
                    RA(inst) = make_uint64_value(lv % rv);
                } else {
                    int64_t lv = vm_extract_int64(lhs);
                    int64_t rv = vm_extract_int64(rhs);
                    if (rv == 0) { runtimeError("Modulo by zero"); return -1; }
                    RA(inst) = make_int64_value(lv % rv);
                }
            } else if (lhs.type == VAL_TABLE || rhs.type == VAL_TABLE) {
                const Value& tbl = (lhs.type == VAL_TABLE) ? lhs : rhs;
                Value out;
                ci->ip = ip;
                int rc = callMetamethod(tbl, state_->metamethods()->mod(), lhs, rhs, out);
                REFRESH_FRAME();
                if (rc < 0) return -1;
                if (rc == 0) { runtimeError("Cannot modulo: no __mod metamethod on table"); return -1; }
                RA(inst) = out;
            } else if (lhs.type == VAL_FLOAT64 || rhs.type == VAL_FLOAT64) {
                double lv = vm_extract_double(lhs);
                double rv = vm_extract_double(rhs);
                if (rv == 0.0) { runtimeError("Modulo by zero"); return -1; }
                RA(inst) = make_float_value(fmod(lv, rv));
            } else {
                runtimeError("Cannot modulo these types");
                return -1;
            }
            break;
        }

        case OP_UNM: {
            const Value& val = RB(inst);
            if (val.type == VAL_INT64) {
                RA(inst) = make_int64_value(-vm_extract_int64(val));
            } else if (val.type == VAL_FLOAT64) {
                RA(inst) = make_float_value(-val.as.double_val);
            } else {
                runtimeError("Attempt to negate a %s value", value_type_name(val.type));
                return -1;
            }
            break;
        }

        case OP_NOT: {
            RA(inst) = make_bool_value(!is_truthy(RB(inst)));
            break;
        }

        // ================================================================
        // Bitwise operations (integer-only)
        // ================================================================

        #define VM_BITWISE_OP(op_char) {                                                    \
            const Value& lhs = RKB(inst);                                                   \
            const Value& rhs = RKC(inst);                                                   \
            if ((lhs.type != VAL_INT64 && lhs.type != VAL_UINT64) ||                      \
                (rhs.type != VAL_INT64 && rhs.type != VAL_UINT64)) {                      \
                runtimeError("Bitwise operations require integer operands");                \
                return -1;                                                                   \
            }                                                                               \
            if (vm_use_unsigned(lhs, rhs)) {                                                \
                RA(inst) = make_uint64_value(vm_extract_uint64(lhs) op_char vm_extract_uint64(rhs)); \
            } else {                                                                        \
                RA(inst) = make_int64_value(vm_extract_int64(lhs) op_char vm_extract_int64(rhs));    \
            }                                                                               \
            break;                                                                          \
        }

        case OP_BAND: VM_BITWISE_OP(&)
        case OP_BOR:  VM_BITWISE_OP(|)
        case OP_BXOR: VM_BITWISE_OP(^)
        case OP_SHL:  VM_BITWISE_OP(<<)
        case OP_SHR:  VM_BITWISE_OP(>>)

        #undef VM_BITWISE_OP

        case OP_BNOT: {
            const Value& val = RB(inst);
            if (val.type == VAL_UINT64) {
                RA(inst) = make_uint64_value(~vm_extract_uint64(val));
            } else if (val.type == VAL_INT64) {
                RA(inst) = make_int64_value(~vm_extract_int64(val));
            } else {
                runtimeError("Bitwise NOT requires an integer operand");
                return -1;
            }
            break;
        }

        // ================================================================
        // String concatenation
        // ================================================================

        case OP_CONCAT: {
            int b_reg = DECODE_B(inst);
            int c_reg = DECODE_C(inst);

            // Build concatenated string from R[B]..R[C]
            std::string result;
            for (int i = b_reg; i <= c_reg; i++) {
                const Value& v = regs[i];
                if (v.type == VAL_STRING && v.as.string) {
                    result.append(v.as.string->data, v.as.string->length);
                } else {
                    char* s = value_to_string(v);
                    if (s) { result.append(s); free(s); }
                }
            }
            RA(inst) = make_string_value_from_cstr(state_, result.c_str());
            break;
        }

        // ================================================================
        // Comparisons — conditional skip pattern
        // ================================================================

        case OP_EQ: {
            int a = DECODE_A(inst);
            const Value& lhs = RKB(inst);
            const Value& rhs = RKC(inst);
            bool eq;
            if (lhs.type == VAL_TABLE || rhs.type == VAL_TABLE) {
                const Value& tbl = (lhs.type == VAL_TABLE) ? lhs : rhs;
                Value out;
                ci->ip = ip;
                int rc = callMetamethod(tbl, state_->metamethods()->eq(), lhs, rhs, out);
                REFRESH_FRAME();
                if (rc < 0) return -1;
                eq = (rc == 1) ? is_truthy(out) : (lhs == rhs);
            } else {
                eq = (lhs == rhs);
            }
            if (eq != (a != 0)) ip++;
            break;
        }

        case OP_LT: {
            int a = DECODE_A(inst);
            const Value& lhs = RKB(inst);
            const Value& rhs = RKC(inst);
            bool l_num = (lhs.type == VAL_INT64 || lhs.type == VAL_UINT64 || lhs.type == VAL_FLOAT64);
            bool r_num = (rhs.type == VAL_INT64 || rhs.type == VAL_UINT64 || rhs.type == VAL_FLOAT64);
            bool lt;
            if (l_num && r_num) {
                if (lhs.type != VAL_FLOAT64 && rhs.type != VAL_FLOAT64) {
                    if (vm_use_unsigned(lhs, rhs))
                        lt = vm_extract_uint64(lhs) < vm_extract_uint64(rhs);
                    else
                        lt = vm_extract_int64(lhs) < vm_extract_int64(rhs);
                } else {
                    lt = vm_extract_double(lhs) < vm_extract_double(rhs);
                }
            } else if (lhs.type == VAL_STRING && rhs.type == VAL_STRING) {
                lt = strcmp(lhs.as.string->data, rhs.as.string->data) < 0;
            } else if (lhs.type == VAL_TABLE || rhs.type == VAL_TABLE) {
                const Value& tbl = (lhs.type == VAL_TABLE) ? lhs : rhs;
                Value out;
                ci->ip = ip;
                int rc = callMetamethod(tbl, state_->metamethods()->lt(), lhs, rhs, out);
                REFRESH_FRAME();
                if (rc < 0) return -1;
                if (rc == 0) { runtimeError("Cannot compare: no __lt metamethod on table"); return -1; }
                lt = is_truthy(out);
            } else {
                runtimeError("Cannot compare incompatible types");
                return -1;
            }
            if (lt != (a != 0)) ip++;
            break;
        }

        case OP_LE: {
            int a = DECODE_A(inst);
            const Value& lhs = RKB(inst);
            const Value& rhs = RKC(inst);
            bool l_num = (lhs.type == VAL_INT64 || lhs.type == VAL_UINT64 || lhs.type == VAL_FLOAT64);
            bool r_num = (rhs.type == VAL_INT64 || rhs.type == VAL_UINT64 || rhs.type == VAL_FLOAT64);
            bool le;
            if (l_num && r_num) {
                if (lhs.type != VAL_FLOAT64 && rhs.type != VAL_FLOAT64) {
                    if (vm_use_unsigned(lhs, rhs))
                        le = vm_extract_uint64(lhs) <= vm_extract_uint64(rhs);
                    else
                        le = vm_extract_int64(lhs) <= vm_extract_int64(rhs);
                } else {
                    le = vm_extract_double(lhs) <= vm_extract_double(rhs);
                }
            } else if (lhs.type == VAL_STRING && rhs.type == VAL_STRING) {
                le = strcmp(lhs.as.string->data, rhs.as.string->data) <= 0;
            } else if (lhs.type == VAL_TABLE || rhs.type == VAL_TABLE) {
                const Value& tbl = (lhs.type == VAL_TABLE) ? lhs : rhs;
                Value out;
                ci->ip = ip;
                int rc = callMetamethod(tbl, state_->metamethods()->le(), lhs, rhs, out);
                REFRESH_FRAME();
                if (rc < 0) return -1;
                if (rc == 0) { runtimeError("Cannot compare: no __le metamethod on table"); return -1; }
                le = is_truthy(out);
            } else {
                runtimeError("Cannot compare incompatible types");
                return -1;
            }
            if (le != (a != 0)) ip++;
            break;
        }

        // ================================================================
        // Comparison with immediate
        // ================================================================

        case OP_LTI: {
            const Value& lhs = regs[DECODE_A(inst)];
            int imm = DECODE_sBx(inst);
            bool result;
            if (lhs.type == VAL_INT64) result = lhs.as.i64 < imm;
            else if (lhs.type == VAL_UINT64) result = (imm < 0) ? false : lhs.as.u64 < (uint64_t)imm;
            else if (lhs.type == VAL_FLOAT64) result = lhs.as.double_val < (double)imm;
            else { runtimeError("LTI requires numeric operand"); return -1; }
            if (result) ip++;
            break;
        }

        case OP_LEI: {
            const Value& lhs = regs[DECODE_A(inst)];
            int imm = DECODE_sBx(inst);
            bool result;
            if (lhs.type == VAL_INT64) result = lhs.as.i64 <= imm;
            else if (lhs.type == VAL_UINT64) result = (imm < 0) ? false : lhs.as.u64 <= (uint64_t)imm;
            else if (lhs.type == VAL_FLOAT64) result = lhs.as.double_val <= (double)imm;
            else { runtimeError("LEI requires numeric operand"); return -1; }
            if (result) ip++;
            break;
        }

        case OP_EQI: {
            const Value& lhs = regs[DECODE_A(inst)];
            int imm = DECODE_sBx(inst);
            bool result;
            if (lhs.type == VAL_INT64) result = lhs.as.i64 == imm;
            else if (lhs.type == VAL_UINT64) result = (imm < 0) ? false : lhs.as.u64 == (uint64_t)imm;
            else if (lhs.type == VAL_FLOAT64) result = lhs.as.double_val == (double)imm;
            else result = false;
            if (result) ip++;
            break;
        }

        case OP_NEI: {
            const Value& lhs = regs[DECODE_A(inst)];
            int imm = DECODE_sBx(inst);
            bool result;
            if (lhs.type == VAL_INT64) result = lhs.as.i64 != imm;
            else if (lhs.type == VAL_UINT64) result = (imm < 0) ? true : lhs.as.u64 != (uint64_t)imm;
            else if (lhs.type == VAL_FLOAT64) result = lhs.as.double_val != (double)imm;
            else result = true;
            if (result) ip++;
            break;
        }

        case OP_GTI: {
            const Value& lhs = regs[DECODE_A(inst)];
            int imm = DECODE_sBx(inst);
            bool result;
            if (lhs.type == VAL_INT64) result = lhs.as.i64 > imm;
            else if (lhs.type == VAL_UINT64) result = (imm < 0) ? true : lhs.as.u64 > (uint64_t)imm;
            else if (lhs.type == VAL_FLOAT64) result = lhs.as.double_val > (double)imm;
            else { runtimeError("GTI requires numeric operand"); return -1; }
            if (result) ip++;
            break;
        }

        case OP_GEI: {
            const Value& lhs = regs[DECODE_A(inst)];
            int imm = DECODE_sBx(inst);
            bool result;
            if (lhs.type == VAL_INT64) result = lhs.as.i64 >= imm;
            else if (lhs.type == VAL_UINT64) result = (imm < 0) ? true : lhs.as.u64 >= (uint64_t)imm;
            else if (lhs.type == VAL_FLOAT64) result = lhs.as.double_val >= (double)imm;
            else { runtimeError("GEI requires numeric operand"); return -1; }
            if (result) ip++;
            break;
        }

        // ================================================================
        // Logical test / set
        // ================================================================

        case OP_TEST: {
            bool truthy = is_truthy(RA(inst));
            int c = DECODE_C(inst);
            if (truthy != (c != 0)) ip++;
            break;
        }

        case OP_TESTJMP: {
            if (!is_truthy(regs[DECODE_A(inst)]))
                ip += DECODE_sBx(inst);
            break;
        }

        case OP_TESTSET: {
            const Value& rb = RB(inst);
            bool truthy = is_truthy(rb);
            int c = DECODE_C(inst);
            if (truthy == (c != 0)) {
                RA(inst) = rb;
            } else {
                ip++;
            }
            break;
        }

        // ================================================================
        // Jumps
        // ================================================================

        case OP_JMP: {
            int offset = DECODE_sBx_wide(inst);
            ip += offset;
            break;
        }

        // ================================================================
        // Function calls
        // ================================================================

        case OP_CALL: {
            int a = DECODE_A(inst);
            int b = DECODE_B(inst);
            int c = DECODE_C(inst);
            int nargs = b - 1;

            ci->ip = ip;
            int rc = callFunction(*ci, a, nargs, c);
            if (rc < 0) return -1;
            if (rc == 1) {
                ci = &call_stack_.back();
                ip = ci->ip;
                proto = ci->proto;
                base = ci->base;
                regs = registers_.data() + base;
            }
            break;
        }

        case OP_TAILCALL: {
            int a = DECODE_A(inst);
            int b = DECODE_B(inst);
            int nargs = b - 1;

            ci->ip = ip;
            int rc = callFunction(*ci, a, nargs, 0);
            if (rc < 0) return -1;
            if (rc == 1) {
                ci = &call_stack_.back();
                ip = ci->ip;
                proto = ci->proto;
                base = ci->base;
                regs = registers_.data() + base;
            }
            break;
        }

        case OP_RETURN: {
            int a = DECODE_A(inst);
            int b = DECODE_B(inst);

            if (call_stack_.size() <= base_depth) {
                return 0;
            }

            int nresults_available = (b == 0) ? 0 : (b - 1);
            closeUpvalues(*ci, 0);

            CallInfo returning = call_stack_.back();
            call_stack_.pop_back();

            // Copy return values before checking depth boundary
            int func_reg_abs = returning.base - 1;
            int nresults_wanted = returning.nresults;
            if (nresults_wanted != 0) {
                int to_copy = nresults_wanted - 1;
                for (int i = 0; i < to_copy; i++) {
                    if (i < nresults_available) {
                        registers_[func_reg_abs + i] = registers_[returning.base + a + i];
                    } else {
                        registers_[func_reg_abs + i] = Value();
                    }
                }
            }

            if (call_stack_.size() <= base_depth) {
                return 0;
            }

            ci = &call_stack_.back();
            ip = ci->ip;
            proto = ci->proto;
            base = ci->base;
            regs = registers_.data() + base;
            break;
        }

        // ================================================================
        // Closures
        // ================================================================

        case OP_CLOSURE: {
            uint16_t bx = DECODE_Bx(inst);
            if (bx >= proto->protos.size()) {
                runtimeError("Invalid prototype index %d", bx);
                return -1;
            }
            Prototype* child_proto = proto->protos[bx];

            MobiusFunction* mf = (MobiusFunction*)calloc(1, sizeof(MobiusFunction));
            mf->name = child_proto->name.empty() ? nullptr :
                       state_->stringPool()->intern(child_proto->name.c_str());
            mf->param_count = child_proto->num_params;
            mf->body = nullptr;
            mf->body_count = 0;
            mf->closure = global_env_;
            if (mf->closure) mf->closure->retain();
            mf->ref_count = 1;
            mf->proto = child_proto;

            if (child_proto->num_params > 0 && !child_proto->local_vars.empty()) {
                mf->param_names = (MobiusString**)calloc(child_proto->num_params, sizeof(MobiusString*));
                for (int i = 0; i < child_proto->num_params && i < (int)child_proto->local_vars.size(); i++) {
                    mf->param_names[i] = state_->stringPool()->intern(
                        child_proto->local_vars[i].name.c_str());
                }
            } else {
                mf->param_names = nullptr;
            }

            // Wire upvalues from the child prototype's upvalue descriptors
            int nupvals = (int)child_proto->upvalues.size();
            if (nupvals > 0) {
                mf->upvalues = (Upvalue**)calloc(nupvals, sizeof(Upvalue*));
                mf->upvalue_count = nupvals;
                for (int u = 0; u < nupvals; u++) {
                    const UpvalueDesc& desc = child_proto->upvalues[u];
                    if (desc.in_stack) {
                        // Capture from current frame's registers
                        Value* reg_ptr = &regs[desc.index];
                        // Check if we already have an open upvalue for this register
                        Upvalue* existing = nullptr;
                        for (auto* ouv : ci->open_upvalues) {
                            if (ouv->is_open && ouv->location == reg_ptr) {
                                existing = ouv;
                                break;
                            }
                        }
                        if (existing) {
                            mf->upvalues[u] = existing;
                        } else {
                            Upvalue* uv = new Upvalue();
                            uv->location = reg_ptr;
                            uv->is_open = true;
                            ci->open_upvalues.push_back(uv);
                            mf->upvalues[u] = uv;
                        }
                    } else {
                        // Capture from enclosing function's upvalues
                        if (desc.index < (int)ci->open_upvalues.size()) {
                            mf->upvalues[u] = ci->open_upvalues[desc.index];
                        } else {
                            mf->upvalues[u] = new Upvalue();
                        }
                    }
                }
            } else {
                mf->upvalues = nullptr;
                mf->upvalue_count = 0;
            }

            RA(inst) = make_function_value(mf);
            break;
        }

        case OP_CLOSE: {
            int a = DECODE_A(inst);
            closeUpvalues(*ci, a);
            break;
        }

        // ================================================================
        // Numeric for-loop
        // ================================================================

        case OP_FORPREP: {
            int a = DECODE_A(inst);
            int sbx = DECODE_sBx(inst);
            Value& idx  = regs[a];
            Value& step = regs[a + 2];
            if (idx.type == VAL_INT64 && step.type == VAL_INT64) {
                int64_t iv = vm_extract_int64(idx);
                int64_t sv = vm_extract_int64(step);
                idx = make_int64_value(iv - sv);
            } else {
                double iv = vm_extract_double(idx);
                double sv = vm_extract_double(step);
                idx = make_float_value(iv - sv);
            }
            ip += sbx;
            break;
        }

        case OP_FORLOOP: {
            int a = DECODE_A(inst);
            int sbx = DECODE_sBx(inst);
            Value& idx   = regs[a];
            Value& limit = regs[a + 1];
            Value& step  = regs[a + 2];

            if (idx.type == VAL_INT64 && limit.type == VAL_INT64 && step.type == VAL_INT64) {
                int64_t iv = vm_extract_int64(idx);
                int64_t sv = vm_extract_int64(step);
                int64_t lv = vm_extract_int64(limit);
                iv += sv;
                idx = make_int64_value(iv);
                bool in_range = (sv > 0) ? (iv <= lv) : (iv >= lv);
                if (in_range) {
                    ip += sbx;
                    regs[a + 3] = idx;
                }
            } else {
                double iv = vm_extract_double(idx);
                double sv = vm_extract_double(step);
                double lv = vm_extract_double(limit);
                iv += sv;
                idx = make_float_value(iv);
                bool in_range = (sv > 0) ? (iv <= lv) : (iv >= lv);
                if (in_range) {
                    ip += sbx;
                    regs[a + 3] = idx;
                }
            }
            break;
        }

        case OP_IFORPREP: {
            int a = DECODE_A(inst);
            regs[a].type = VAL_INT64;
            regs[a].as.i64 -= regs[a + 2].as.i64;
            ip += DECODE_sBx(inst);
            break;
        }

        case OP_IFORLOOP: {
            int a = DECODE_A(inst);
            int64_t sv = regs[a + 2].as.i64;
            int64_t iv = regs[a].as.i64 + sv;
            regs[a].as.i64 = iv;
            if ((sv > 0) ? (iv <= regs[a + 1].as.i64)
                         : (iv >= regs[a + 1].as.i64)) {
                ip += DECODE_sBx(inst);
                regs[a + 3].as.i64 = iv;
                regs[a + 3].type = VAL_INT64;
            }
            break;
        }

        case OP_TFORLOOP: {
            // Generic for-loop — future iterator protocol
            runtimeError("Generic for-loop (TFORLOOP) not yet implemented");
            return -1;
        }

        // ================================================================
        // Enum operations
        // ================================================================

        case OP_NEWENUM: {
            uint16_t bx = DECODE_Bx(inst);
            const Value& name_val = proto->constants[bx];
            const char* enum_name = (name_val.type == VAL_STRING && name_val.as.string)
                                    ? name_val.as.string->data : "unknown";
            EnumDefinition* edef = new (std::nothrow) EnumDefinition(enum_name, NUM_INT64);
            if (!edef) { runtimeError("Failed to allocate enum"); return -1; }
            RA(inst) = make_userdata_value(edef,
                [](void* p) { if (p) static_cast<EnumDefinition*>(p)->release(); },
                "enum_definition", sizeof(EnumDefinition));
            break;
        }

        case OP_ENUMVAL: {
            Value& enum_val = RA(inst);
            if (enum_val.type != VAL_USERDATA || !enum_val.as.userdata ||
                strcmp(enum_val.as.userdata->type_name, "enum_definition") != 0) {
                runtimeError("ENUMVAL: target is not an enum definition");
                return -1;
            }
            EnumDefinition* edef = static_cast<EnumDefinition*>(enum_val.as.userdata->ptr);
            const Value& member_val = RB(inst);
            int member_idx = DECODE_C(inst);
            (void)member_idx;
            if (member_val.type == VAL_INT64) {
                int64_t iv = vm_extract_int64(member_val);
                // Use auto-member with the given value
                char name_buf[32];
                snprintf(name_buf, sizeof(name_buf), "member_%d", member_idx);
                edef->addMember(name_buf, iv);
            } else {
                edef->addAutoMember("member");
            }
            break;
        }

        case OP_GETENUM: {
            // R[A] = R[B].member[C]
            const Value& enum_val = RB(inst);
            if (enum_val.type != VAL_USERDATA || !enum_val.as.userdata ||
                strcmp(enum_val.as.userdata->type_name, "enum_definition") != 0) {
                runtimeError("GETENUM: target is not an enum definition");
                return -1;
            }
            EnumDefinition* edef = static_cast<EnumDefinition*>(enum_val.as.userdata->ptr);
            int member_idx = DECODE_C(inst);
            // Find by index (walk the members)
            const EnumMember* member = nullptr;
            // EnumDefinition doesn't expose by-index lookup directly,
            // so for now use findMemberByValue with the index as a fallback
            (void)member_idx;
            (void)member;
            (void)edef;
            RA(inst) = Value(); // Placeholder — full enum access uses GETGLOBAL + GETTABLE
            break;
        }

        // ================================================================
        // Import
        // ================================================================

        case OP_IMPORT: {
            // ABC format: B = module name (RK), C = alias/target name (RK)
            const Value& mod_name_val = RK(*ci, DECODE_B(inst));
            const Value& alias_val    = RK(*ci, DECODE_C(inst));

            if (mod_name_val.type != VAL_STRING || !mod_name_val.as.string) {
                runtimeError("IMPORT: invalid module name");
                return -1;
            }
            if (alias_val.type != VAL_STRING || !alias_val.as.string) {
                runtimeError("IMPORT: invalid alias");
                return -1;
            }
            const char* module_name = mod_name_val.as.string->data;
            const char* alias_name  = alias_val.as.string->data;

            // --- Load the module ---
            ModuleRegistry* registry = getGlobalRegistry();
            if (!registry) {
                runtimeError("Module registry not initialized");
                return -1;
            }

            LoadedModule* module = registry->findModule(module_name);
            if (!module) {
                PluginLoadResult result = registry->loadModuleByName(module_name);
                if (result.status != PLUGIN_STATUS_LOADED) {
                    runtimeError("Import failed - module '%s' not found", module_name);
                    return -1;
                }
                module = registry->findModule(module_name);
            }
            if (!module || !module->plugin) {
                runtimeError("Module '%s' has no plugin interface", module_name);
                return -1;
            }

            registry->incrementRefCount(module_name);
            Plugin* plugin = module->plugin;
            StringInternPool* pool = state_->stringPool();

            bool is_global = (strcmp(alias_name, "_GLOBAL") == 0);

            if (is_global) {
                // _GLOBAL: define each module function directly in the root environment
                for (size_t i = 0; i < plugin->function_count; i++) {
                    PluginFunction* func = &plugin->functions[i];
                    if (!func || !func->name || !func->function) continue;
                    Value func_val = make_native_function_value(func->function);
                    int slot = state_->assignGlobalSlot(func->name);
                    func_val.flags |= VAL_FLAG_DEFINED;
                    state_->globalSlot(slot) = func_val;
                    global_env_->define(pool->intern(func->name), func_val);
                }
            } else if (strchr(alias_name, '.') != nullptr) {
                // Dotted path (e.g. "math.trig"): create/reuse nested tables
                // Parse into components
                char buf[256];
                strncpy(buf, alias_name, sizeof(buf) - 1);
                buf[sizeof(buf) - 1] = '\0';

                // Split by '.' — find components
                const char* components[32];
                int ncomps = 0;
                char* tok = strtok(buf, ".");
                while (tok && ncomps < 32) {
                    components[ncomps++] = tok;
                    tok = strtok(nullptr, ".");
                }

                // Walk/create the nested table chain
                // First component: get or create in global env
                MobiusString* first = pool->intern(components[0]);
                bool found = false;
                Value cur_val;
                int ns_slot = state_->findGlobalSlot(components[0]);
                if (ns_slot >= 0 && (state_->globalSlot(ns_slot).flags & VAL_FLAG_DEFINED)) {
                    cur_val = state_->globalSlot(ns_slot);
                    found = true;
                } else {
                    cur_val = global_env_->get(first, &found);
                }
                Table* cur_table = nullptr;

                if (found && cur_val.type == VAL_TABLE) {
                    cur_table = cur_val.as.table;
                } else if (found) {
                    runtimeError("Cannot create nested namespace '%s': "
                                 "'%s' is not a table", alias_name, components[0]);
                    return -1;
                } else {
                    cur_table = new (std::nothrow) Table(state_, 16);
                    if (!cur_table) { runtimeError("Failed to create namespace table"); return -1; }
                    Value tval = make_table_value(cur_table);
                    tval.flags |= VAL_FLAG_DEFINED;
                    int s = state_->assignGlobalSlot(components[0]);
                    state_->globalSlot(s) = tval;
                    global_env_->define(first, tval);
                }

                // Intermediate components
                for (int i = 1; i < ncomps; i++) {
                    Value key = make_string_value_from_cstr(state_, components[i]);
                    Value next = cur_table->get(key);
                    if (next.type == VAL_TABLE) {
                        cur_table = next.as.table;
                    } else {
                        Table* sub = new (std::nothrow) Table(state_, 16);
                        if (!sub) { runtimeError("Failed to create nested namespace table"); return -1; }
                        cur_table->set(key, make_table_value(sub));
                        cur_table = sub;
                    }
                }

                // Fill the leaf table with module functions
                for (size_t i = 0; i < plugin->function_count; i++) {
                    PluginFunction* func = &plugin->functions[i];
                    if (!func || !func->name || !func->function) continue;
                    Value func_key = make_string_value_from_cstr(state_, func->name);
                    Value func_val = make_native_function_value(func->function);
                    cur_table->set(func_key, func_val);
                }
            } else {
                // Simple alias (e.g. "math"): create table, define as global
                MobiusString* interned_alias = pool->intern(alias_name);
                bool found = false;
                Value existing;
                int alias_slot = state_->findGlobalSlot(alias_name);
                if (alias_slot >= 0 && (state_->globalSlot(alias_slot).flags & VAL_FLAG_DEFINED)) {
                    existing = state_->globalSlot(alias_slot);
                    found = true;
                } else {
                    existing = global_env_->get(interned_alias, &found);
                }
                Table* mod_table = nullptr;

                if (found && existing.type == VAL_TABLE) {
                    mod_table = existing.as.table;
                } else {
                    mod_table = new (std::nothrow) Table(state_, 16);
                    if (!mod_table) { runtimeError("Failed to create module table"); return -1; }
                }

                for (size_t i = 0; i < plugin->function_count; i++) {
                    PluginFunction* func = &plugin->functions[i];
                    if (!func || !func->name || !func->function) continue;
                    Value func_key = make_string_value_from_cstr(state_, func->name);
                    Value func_val = make_native_function_value(func->function);
                    mod_table->set(func_key, func_val);
                }

                if (!found || existing.type != VAL_TABLE) {
                    Value tval = make_table_value(mod_table);
                    tval.flags |= VAL_FLAG_DEFINED;
                    int s = state_->assignGlobalSlot(alias_name);
                    state_->globalSlot(s) = tval;
                    global_env_->define(interned_alias, tval);
                }
            }
            break;
        }

        // ================================================================
        // Pragma
        // ================================================================

        case OP_PRAGMA: {
            uint16_t bx = DECODE_Bx(inst);
            const Value& name_val = proto->constants[bx];
            if (name_val.type != VAL_STRING || !name_val.as.string) {
                runtimeError("PRAGMA: invalid pragma name");
                return -1;
            }
            const char* pragma_name = name_val.as.string->data;
            const Value& pval = RA(inst);

            if (strcmp(pragma_name, "strict_types") == 0) {
                state_->config().strict_mode = is_truthy(pval);
            } else if (strcmp(pragma_name, "type_warnings") == 0) {
                state_->config().warn_on_conversion = is_truthy(pval);
            } else if (strcmp(pragma_name, "override_behavior") == 0) {
                if (pval.type == VAL_STRING && pval.as.string) {
                    const char* v = pval.as.string->data;
                    if (strcmp(v, "error") == 0)
                        state_->config().override_behavior = MOBIUS_OVERRIDE_ERROR;
                    else if (strcmp(v, "warn") == 0)
                        state_->config().override_behavior = MOBIUS_OVERRIDE_WARN;
                    else if (strcmp(v, "quiet") == 0)
                        state_->config().override_behavior = MOBIUS_OVERRIDE_QUIET;
                    else {
                        runtimeError("Invalid value for pragma override_behavior: '%s' "
                                     "(expected 'error', 'warn', or 'quiet')", v);
                        return -1;
                    }
                } else {
                    runtimeError("Invalid value for pragma override_behavior "
                                 "(expected 'error', 'warn', or 'quiet')");
                    return -1;
                }
            } else {
                runtimeError("Unknown pragma: '%s'", pragma_name);
                return -1;
            }
            break;
        }

        // ================================================================
        // Increment / Decrement
        // ================================================================

        case OP_INC: {
            const Value& val = RB(inst);
            if (val.type != VAL_INT64) {
                runtimeError("Increment requires an integer operand");
                return -1;
            }
            bool success;
            RA(inst) = increment_integer(val, true, &success);
            if (!success) { runtimeError("Failed to increment value"); return -1; }
            break;
        }

        case OP_DEC: {
            const Value& val = RB(inst);
            if (val.type != VAL_INT64) {
                runtimeError("Decrement requires an integer operand");
                return -1;
            }
            bool success;
            RA(inst) = increment_integer(val, false, &success);
            if (!success) { runtimeError("Failed to decrement value"); return -1; }
            break;
        }

        // ================================================================
        // Arithmetic with immediate
        // ================================================================

        case OP_ADDI: {
            int a = DECODE_A(inst);
            int imm = DECODE_sBx(inst);
            Value& val = regs[a];
            if (val.type == VAL_INT64)
                val = make_int64_value(val.as.i64 + imm);
            else if (val.type == VAL_UINT64)
                val = make_uint64_value(val.as.u64 + (uint64_t)(int64_t)imm);
            else if (val.type == VAL_FLOAT64)
                val = make_float_value(val.as.double_val + imm);
            else { runtimeError("ADDI requires numeric operand"); return -1; }
            break;
        }

        case OP_SUBI: {
            int a = DECODE_A(inst);
            int imm = DECODE_sBx(inst);
            Value& val = regs[a];
            if (val.type == VAL_INT64)
                val = make_int64_value(val.as.i64 - imm);
            else if (val.type == VAL_UINT64)
                val = make_uint64_value(val.as.u64 - (uint64_t)(int64_t)imm);
            else if (val.type == VAL_FLOAT64)
                val = make_float_value(val.as.double_val - imm);
            else { runtimeError("SUBI requires numeric operand"); return -1; }
            break;
        }

        case OP_MULI: {
            int a = DECODE_A(inst);
            int imm = DECODE_sBx(inst);
            Value& val = regs[a];
            if (val.type == VAL_INT64)
                val = make_int64_value(val.as.i64 * imm);
            else if (val.type == VAL_UINT64)
                val = make_uint64_value(val.as.u64 * (uint64_t)(int64_t)imm);
            else if (val.type == VAL_FLOAT64)
                val = make_float_value(val.as.double_val * imm);
            else { runtimeError("MULI requires numeric operand"); return -1; }
            break;
        }

        case OP_MODI: {
            int a = DECODE_A(inst);
            int imm = DECODE_sBx(inst);
            if (imm == 0) { runtimeError("Modulo by zero"); return -1; }
            Value& val = regs[a];
            if (val.type == VAL_INT64)
                val = make_int64_value(val.as.i64 % imm);
            else if (val.type == VAL_UINT64)
                val = make_uint64_value(val.as.u64 % (uint64_t)(int64_t)imm);
            else if (val.type == VAL_FLOAT64)
                val = make_float_value(fmod(val.as.double_val, (double)imm));
            else { runtimeError("MODI requires numeric operand"); return -1; }
            break;
        }

        // ================================================================
        // Type checking
        // ================================================================

        case OP_TYPECHECK: {
            NumberType target = (NumberType)DECODE_B(inst);
            TypeCheckConfig tc = {
                state_->config().strict_mode,
                state_->config().warn_on_conversion
            };
            TypeConversionResult conv = validate_and_convert_value(
                RA(inst), target, true, tc);
            if (!conv.success) {
                runtimeError("%s", conv.error_message
                             ? conv.error_message
                             : "Type validation failed");
                free(conv.error_message);
                return -1;
            }
            if (conv.was_converted && state_->config().warn_on_conversion) {
                fprintf(stderr, "Warning: Implicit type conversion at line %d\n",
                        currentLine());
            }
            RA(inst) = conv.converted_value;
            free(conv.error_message);
            break;
        }

        case OP_ISNUM: {
            const Value& v = RB(inst);
            bool num = (v.type == VAL_INT64 || v.type == VAL_FLOAT64);
            RA(inst) = make_bool_value(num);
            break;
        }

        case OP_TYPECOMPAT: {
            int a = DECODE_A(inst);
            const Value& lhs = RKB(inst);
            const Value& rhs = RKC(inst);
            bool l_num = (lhs.type == VAL_INT64 || lhs.type == VAL_FLOAT64);
            bool r_num = (rhs.type == VAL_INT64 || rhs.type == VAL_FLOAT64);
            bool compat = (l_num && r_num) || (lhs.type == VAL_STRING && rhs.type == VAL_STRING);
            if (compat != (a != 0)) ip++;
            break;
        }

        case OP_TYPEIS: {
            int a = DECODE_A(inst);
            const Value& val = RB(inst);
            uint8_t expected_type = DECODE_C(inst);
            bool match = ((uint8_t)val.type == expected_type);
            if (match != (a != 0)) ip++;
            break;
        }

        // ================================================================
        // Length
        // ================================================================

        case OP_LEN: {
            const Value& val = RB(inst);
            if (val.type == VAL_ARRAY && val.as.array) {
                RA(inst) = make_int64_value((int64_t)val.as.array->length());
            } else if (val.type == VAL_TABLE && val.as.table) {
                RA(inst) = make_int64_value((int64_t)val.as.table->size());
            } else if (val.type == VAL_STRING && val.as.string) {
                RA(inst) = make_int64_value((int64_t)val.as.string->length);
            } else {
                runtimeError("Attempt to get length of a %s value", value_type_name(val.type));
                return -1;
            }
            break;
        }

        // ================================================================
        // NOP
        // ================================================================

        case OP_NOP:
            break;

        default:
            runtimeError("Unknown opcode %d", DECODE_OP(inst));
            return -1;

        } // switch
    } // for(;;)

    #undef RA
    #undef RB
    #undef RC
    #undef RKB
    #undef RKC
    #undef KBx
    #undef REFRESH_FRAME
}
