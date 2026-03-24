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

    int args_base = caller.base + func_reg + 1;

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

    int child_base = caller.base + func_reg + 1;
    int needed = child_base + child->num_registers + 16;
    if (needed > (int)registers_.size()) {
        registers_.resize(needed, Value());
    }

    CallInfo child_ci;
    child_ci.proto = child;
    child_ci.ip = child->code.data();
    child_ci.base = child_base;
    child_ci.nresults = nresults;

    if (mf->upvalues && mf->upvalue_count > 0) {
        child_ci.open_upvalues.resize(mf->upvalue_count);
        for (int u = 0; u < mf->upvalue_count; u++) {
            child_ci.open_upvalues[u] = mf->upvalues[u];
        }
    }

    call_stack_.push_back(child_ci);

    return 1;
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
// VMFrame — bundles all dispatch-loop state into one struct so handler
// functions receive a single reference parameter.
// ============================================================================

struct VMFrame {
    CallInfo*   ci;
    uint32_t*   ip;
    Prototype*  proto;
    int         base;
    Value*      regs;

    inline void refresh(MobiusVM* vm);
};

// Defined after MobiusVM methods so we can access registers_ etc.
// However, since VMFrame::refresh needs access to vm internals we
// implement it via a public helper on MobiusVM instead. See below.

// ============================================================================
// Opcode handler functions
//
// Each handler is a MOBIUS_FORCEINLINE free function:
//   int vm_op_XXX(MobiusVM* vm, VMFrame& f, uint32_t inst)
//
// Return values:
//   0  = success, dispatch next instruction (VM_NEXT)
//  -1  = runtime error (already reported via vm->runtimeError)
//   1  = special: OP_RETURN signals exit from run()
//
// Handlers that modify ip do so through f.ip.
// Handlers that need REFRESH_FRAME (after calls) call vm->refreshFrame(f).
// ============================================================================

// Accessors matching the old macros, now operating on VMFrame
#define RA(inst)  f.regs[DECODE_A(inst)]
#define RB(inst)  f.regs[DECODE_B(inst)]
#define RC(inst)  f.regs[DECODE_C(inst)]
#define RKB(inst) (IS_CONSTANT(DECODE_B(inst)) \
    ? f.ci->proto->constants[RK_AS_CONSTANT(DECODE_B(inst))] \
    : f.regs[DECODE_B(inst)])
#define RKC(inst) (IS_CONSTANT(DECODE_C(inst)) \
    ? f.ci->proto->constants[RK_AS_CONSTANT(DECODE_C(inst))] \
    : f.regs[DECODE_C(inst)])
#define KBx(inst) f.proto->constants[DECODE_Bx(inst)]

// ---- Data movement ----

MOBIUS_FORCEINLINE static int vm_op_move(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    (void)vm;
    RA(inst) = RB(inst);
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_loadk(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    (void)vm;
    RA(inst) = KBx(inst);
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_loadnil(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    (void)vm;
    int a = DECODE_A(inst);
    int b = DECODE_B(inst);
    for (int i = a; i <= a + b; i++)
        f.regs[i] = Value();
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_loadbool(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    (void)vm;
    RA(inst) = make_bool_value(DECODE_B(inst) != 0);
    if (DECODE_C(inst)) f.ip++;
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_loadint(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    (void)vm;
    int sbx = DECODE_sBx(inst);
    RA(inst) = make_int64_value((int64_t)sbx);
    return 0;
}

// ---- Globals ----

MOBIUS_FORCEINLINE static int vm_op_getglobal(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    int slot = DECODE_Bx(inst);
    const Value& gv = vm->state_->globalSlot(slot);
    if (MOBIUS_UNLIKELY(!(gv.flags & VAL_FLAG_DEFINED))) {
        vm->runtimeError("Undefined variable '%s'", vm->state_->globalSlotName(slot));
        return -1;
    }
    RA(inst) = gv;
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_setglobal(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    int slot = DECODE_Bx(inst);
    Value& gv = vm->state_->globalSlot(slot);
    if (MOBIUS_UNLIKELY(gv.flags & VAL_FLAG_READONLY)) {
        vm->runtimeError("Cannot assign to read-only variable '%s'", vm->state_->globalSlotName(slot));
        return -1;
    }
    gv = RA(inst);
    gv.flags |= VAL_FLAG_DEFINED;
    return 0;
}

// ---- Upvalues ----

MOBIUS_FORCEINLINE static int vm_op_getupval(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    (void)vm;
    int b = DECODE_B(inst);
    if (b < (int)f.ci->open_upvalues.size() && f.ci->open_upvalues[b]) {
        RA(inst) = *f.ci->open_upvalues[b]->location;
    } else {
        RA(inst) = Value();
    }
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_setupval(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    (void)vm;
    int b = DECODE_B(inst);
    if (b < (int)f.ci->open_upvalues.size() && f.ci->open_upvalues[b]) {
        *f.ci->open_upvalues[b]->location = RA(inst);
    }
    return 0;
}

// ---- Tables and arrays ----

MOBIUS_FORCEINLINE static int vm_op_newtable(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    Table* tbl = new (std::nothrow) Table(vm->state_, DECODE_C(inst));
    if (!tbl) { vm->runtimeError("Failed to allocate table"); return -1; }
    RA(inst) = make_table_value(tbl);
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_newarray(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    ArrayValue* arr = new (std::nothrow) ArrayValue(DECODE_B(inst));
    if (!arr) { vm->runtimeError("Failed to allocate array"); return -1; }
    RA(inst) = make_array_value(arr);
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_gettable(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    const Value& tbl = RB(inst);
    const Value& key = RKC(inst);
    if (tbl.type == VAL_TABLE && tbl.as.table) {
        RA(inst) = tbl.as.table->get(key);
    } else if (tbl.type == VAL_ARRAY && tbl.as.array) {
        if (key.type == VAL_INT64) {
            int64_t idx = MobiusVM::vm_extract_int64(key);
            if (idx >= 0 && idx < (int64_t)tbl.as.array->length()) {
                RA(inst) = tbl.as.array->get((size_t)idx);
            } else {
                RA(inst) = Value();
            }
        } else {
            vm->runtimeError("Array index must be an integer");
            return -1;
        }
    } else {
        vm->runtimeError("Attempt to index a %s value", value_type_name(tbl.type));
        return -1;
    }
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_settable(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    Value& tbl = RA(inst);
    const Value& key = RKB(inst);
    const Value& val = RKC(inst);
    if (tbl.type == VAL_TABLE && tbl.as.table) {
        tbl.as.table->set(key, val);
    } else if (tbl.type == VAL_ARRAY && tbl.as.array) {
        if (key.type == VAL_INT64) {
            int64_t idx = MobiusVM::vm_extract_int64(key);
            if (idx >= 0) {
                while ((int64_t)tbl.as.array->length() <= idx)
                    tbl.as.array->push(Value());
                tbl.as.array->set((size_t)idx, val);
            }
        } else {
            vm->runtimeError("Array index must be an integer");
            return -1;
        }
    } else {
        vm->runtimeError("Attempt to index a %s value", value_type_name(tbl.type));
        return -1;
    }
    return 0;
}

// ---- Arithmetic ----

MOBIUS_FORCEINLINE static int vm_op_add(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    const Value& lhs = RKB(inst);
    const Value& rhs = RKC(inst);
    if (MOBIUS_LIKELY(lhs.type == VAL_INT64 && rhs.type == VAL_INT64)) {
        Value& dst = RA(inst);
        dst.as.i64 = lhs.as.i64 + rhs.as.i64;
        dst.type = VAL_INT64; dst.flags = 0;
    } else if (lhs.type == VAL_FLOAT64 && rhs.type == VAL_FLOAT64) {
        Value& dst = RA(inst);
        dst.as.double_val = lhs.as.double_val + rhs.as.double_val;
        dst.type = VAL_FLOAT64; dst.flags = 0;
    } else if ((lhs.type == VAL_INT64 || lhs.type == VAL_UINT64) &&
               (rhs.type == VAL_INT64 || rhs.type == VAL_UINT64)) {
        if (MobiusVM::vm_use_unsigned(lhs, rhs))
            RA(inst) = make_uint64_value(MobiusVM::vm_extract_uint64(lhs) + MobiusVM::vm_extract_uint64(rhs));
        else
            RA(inst) = make_int64_value(MobiusVM::vm_extract_int64(lhs) + MobiusVM::vm_extract_int64(rhs));
    } else if (lhs.type == VAL_FLOAT64 || rhs.type == VAL_FLOAT64) {
        Value& dst = RA(inst);
        dst.as.double_val = MobiusVM::vm_extract_double(lhs) + MobiusVM::vm_extract_double(rhs);
        dst.type = VAL_FLOAT64; dst.flags = 0;
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
        RA(inst) = make_string_value_from_cstr(vm->state_, result.c_str());
    } else if (lhs.type == VAL_TABLE || rhs.type == VAL_TABLE) {
        const Value& tbl = (lhs.type == VAL_TABLE) ? lhs : rhs;
        Value out;
        f.ci->ip = f.ip;
        int rc = vm->callMetamethod(tbl, vm->state_->metamethods()->add(), lhs, rhs, out);
        vm->refreshFrame(f);
        if (rc < 0) return -1;
        if (rc == 0) { vm->runtimeError("Cannot add: no __add metamethod on table"); return -1; }
        RA(inst) = out;
    } else {
        vm->runtimeError("Cannot add these types");
        return -1;
    }
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_sub(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    const Value& lhs = RKB(inst);
    const Value& rhs = RKC(inst);
    if (MOBIUS_LIKELY(lhs.type == VAL_INT64 && rhs.type == VAL_INT64)) {
        Value& dst = RA(inst);
        dst.as.i64 = lhs.as.i64 - rhs.as.i64;
        dst.type = VAL_INT64; dst.flags = 0;
    } else if (lhs.type == VAL_FLOAT64 && rhs.type == VAL_FLOAT64) {
        Value& dst = RA(inst);
        dst.as.double_val = lhs.as.double_val - rhs.as.double_val;
        dst.type = VAL_FLOAT64; dst.flags = 0;
    } else if ((lhs.type == VAL_INT64 || lhs.type == VAL_UINT64) &&
               (rhs.type == VAL_INT64 || rhs.type == VAL_UINT64)) {
        if (MobiusVM::vm_use_unsigned(lhs, rhs))
            RA(inst) = make_uint64_value(MobiusVM::vm_extract_uint64(lhs) - MobiusVM::vm_extract_uint64(rhs));
        else
            RA(inst) = make_int64_value(MobiusVM::vm_extract_int64(lhs) - MobiusVM::vm_extract_int64(rhs));
    } else if (lhs.type == VAL_FLOAT64 || rhs.type == VAL_FLOAT64) {
        Value& dst = RA(inst);
        dst.as.double_val = MobiusVM::vm_extract_double(lhs) - MobiusVM::vm_extract_double(rhs);
        dst.type = VAL_FLOAT64; dst.flags = 0;
    } else if (lhs.type == VAL_TABLE || rhs.type == VAL_TABLE) {
        const Value& tbl = (lhs.type == VAL_TABLE) ? lhs : rhs;
        Value out;
        f.ci->ip = f.ip;
        int rc = vm->callMetamethod(tbl, vm->state_->metamethods()->sub(), lhs, rhs, out);
        vm->refreshFrame(f);
        if (rc < 0) return -1;
        if (rc == 0) { vm->runtimeError("Cannot subtract: no __sub metamethod on table"); return -1; }
        RA(inst) = out;
    } else {
        vm->runtimeError("Cannot subtract these types");
        return -1;
    }
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_mul(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    const Value& lhs = RKB(inst);
    const Value& rhs = RKC(inst);
    if (MOBIUS_LIKELY(lhs.type == VAL_INT64 && rhs.type == VAL_INT64)) {
        Value& dst = RA(inst);
        dst.as.i64 = lhs.as.i64 * rhs.as.i64;
        dst.type = VAL_INT64; dst.flags = 0;
    } else if (lhs.type == VAL_FLOAT64 && rhs.type == VAL_FLOAT64) {
        Value& dst = RA(inst);
        dst.as.double_val = lhs.as.double_val * rhs.as.double_val;
        dst.type = VAL_FLOAT64; dst.flags = 0;
    } else if ((lhs.type == VAL_INT64 || lhs.type == VAL_UINT64) &&
               (rhs.type == VAL_INT64 || rhs.type == VAL_UINT64)) {
        if (MobiusVM::vm_use_unsigned(lhs, rhs))
            RA(inst) = make_uint64_value(MobiusVM::vm_extract_uint64(lhs) * MobiusVM::vm_extract_uint64(rhs));
        else
            RA(inst) = make_int64_value(MobiusVM::vm_extract_int64(lhs) * MobiusVM::vm_extract_int64(rhs));
    } else if (lhs.type == VAL_FLOAT64 || rhs.type == VAL_FLOAT64) {
        Value& dst = RA(inst);
        dst.as.double_val = MobiusVM::vm_extract_double(lhs) * MobiusVM::vm_extract_double(rhs);
        dst.type = VAL_FLOAT64; dst.flags = 0;
    } else if (lhs.type == VAL_TABLE || rhs.type == VAL_TABLE) {
        const Value& tbl = (lhs.type == VAL_TABLE) ? lhs : rhs;
        Value out;
        f.ci->ip = f.ip;
        int rc = vm->callMetamethod(tbl, vm->state_->metamethods()->mul(), lhs, rhs, out);
        vm->refreshFrame(f);
        if (rc < 0) return -1;
        if (rc == 0) { vm->runtimeError("Cannot multiply: no __mul metamethod on table"); return -1; }
        RA(inst) = out;
    } else {
        vm->runtimeError("Cannot multiply these types");
        return -1;
    }
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_div(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    const Value& lhs = RKB(inst);
    const Value& rhs = RKC(inst);
    if (MOBIUS_LIKELY(lhs.type == VAL_INT64 && rhs.type == VAL_INT64)) {
        int64_t rv = rhs.as.i64;
        if (MOBIUS_UNLIKELY(rv == 0)) { vm->runtimeError("Division by zero"); return -1; }
        Value& dst = RA(inst);
        dst.as.i64 = lhs.as.i64 / rv;
        dst.type = VAL_INT64; dst.flags = 0;
    } else if (lhs.type == VAL_TABLE || rhs.type == VAL_TABLE) {
        const Value& tbl = (lhs.type == VAL_TABLE) ? lhs : rhs;
        Value out;
        f.ci->ip = f.ip;
        int rc = vm->callMetamethod(tbl, vm->state_->metamethods()->div(), lhs, rhs, out);
        vm->refreshFrame(f);
        if (rc < 0) return -1;
        if (rc == 0) { vm->runtimeError("Cannot divide: no __div metamethod on table"); return -1; }
        RA(inst) = out;
    } else {
        double lv = MobiusVM::vm_extract_double(lhs);
        double rv = MobiusVM::vm_extract_double(rhs);
        if (rv == 0.0) { vm->runtimeError("Division by zero"); return -1; }
        Value& dst = RA(inst);
        dst.as.double_val = lv / rv;
        dst.type = VAL_FLOAT64; dst.flags = 0;
    }
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_mod(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    const Value& lhs = RKB(inst);
    const Value& rhs = RKC(inst);
    if (MOBIUS_LIKELY(lhs.type == VAL_INT64 && rhs.type == VAL_INT64)) {
        int64_t rv = rhs.as.i64;
        if (MOBIUS_UNLIKELY(rv == 0)) { vm->runtimeError("Modulo by zero"); return -1; }
        Value& dst = RA(inst);
        dst.as.i64 = lhs.as.i64 % rv;
        dst.type = VAL_INT64; dst.flags = 0;
    } else if ((lhs.type == VAL_INT64 || lhs.type == VAL_UINT64) &&
               (rhs.type == VAL_INT64 || rhs.type == VAL_UINT64)) {
        if (MobiusVM::vm_use_unsigned(lhs, rhs)) {
            uint64_t lv = MobiusVM::vm_extract_uint64(lhs);
            uint64_t rv = MobiusVM::vm_extract_uint64(rhs);
            if (rv == 0) { vm->runtimeError("Modulo by zero"); return -1; }
            RA(inst) = make_uint64_value(lv % rv);
        } else {
            int64_t lv = MobiusVM::vm_extract_int64(lhs);
            int64_t rv = MobiusVM::vm_extract_int64(rhs);
            if (rv == 0) { vm->runtimeError("Modulo by zero"); return -1; }
            RA(inst) = make_int64_value(lv % rv);
        }
    } else if (lhs.type == VAL_FLOAT64 && rhs.type == VAL_FLOAT64) {
        double rv = rhs.as.double_val;
        if (rv == 0.0) { vm->runtimeError("Modulo by zero"); return -1; }
        Value& dst = RA(inst);
        dst.as.double_val = fmod(lhs.as.double_val, rv);
        dst.type = VAL_FLOAT64; dst.flags = 0;
    } else if (lhs.type == VAL_FLOAT64 || rhs.type == VAL_FLOAT64) {
        double lv = MobiusVM::vm_extract_double(lhs);
        double rv = MobiusVM::vm_extract_double(rhs);
        if (rv == 0.0) { vm->runtimeError("Modulo by zero"); return -1; }
        Value& dst = RA(inst);
        dst.as.double_val = fmod(lv, rv);
        dst.type = VAL_FLOAT64; dst.flags = 0;
    } else if (lhs.type == VAL_TABLE || rhs.type == VAL_TABLE) {
        const Value& tbl = (lhs.type == VAL_TABLE) ? lhs : rhs;
        Value out;
        f.ci->ip = f.ip;
        int rc = vm->callMetamethod(tbl, vm->state_->metamethods()->mod(), lhs, rhs, out);
        vm->refreshFrame(f);
        if (rc < 0) return -1;
        if (rc == 0) { vm->runtimeError("Cannot modulo: no __mod metamethod on table"); return -1; }
        RA(inst) = out;
    } else {
        vm->runtimeError("Cannot modulo these types");
        return -1;
    }
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_unm(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    const Value& val = RB(inst);
    if (val.type == VAL_INT64) {
        RA(inst) = make_int64_value(-MobiusVM::vm_extract_int64(val));
    } else if (val.type == VAL_FLOAT64) {
        RA(inst) = make_float_value(-val.as.double_val);
    } else {
        vm->runtimeError("Attempt to negate a %s value", value_type_name(val.type));
        return -1;
    }
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_not(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    (void)vm;
    RA(inst) = make_bool_value(!is_truthy(RB(inst)));
    return 0;
}

// ---- Bitwise ----

MOBIUS_FORCEINLINE static int vm_op_bitwise(MobiusVM* vm, VMFrame& f, uint32_t inst, char op) {
    const Value& lhs = RKB(inst);
    const Value& rhs = RKC(inst);
    if ((lhs.type != VAL_INT64 && lhs.type != VAL_UINT64) ||
        (rhs.type != VAL_INT64 && rhs.type != VAL_UINT64)) {
        vm->runtimeError("Bitwise operations require integer operands");
        return -1;
    }
    if (MobiusVM::vm_use_unsigned(lhs, rhs)) {
        uint64_t l = MobiusVM::vm_extract_uint64(lhs);
        uint64_t r = MobiusVM::vm_extract_uint64(rhs);
        uint64_t res;
        switch (op) {
            case '&': res = l & r; break;
            case '|': res = l | r; break;
            case '^': res = l ^ r; break;
            case '<': res = l << r; break;
            case '>': res = l >> r; break;
            default:  res = 0; break;
        }
        RA(inst) = make_uint64_value(res);
    } else {
        int64_t l = MobiusVM::vm_extract_int64(lhs);
        int64_t r = MobiusVM::vm_extract_int64(rhs);
        int64_t res;
        switch (op) {
            case '&': res = l & r; break;
            case '|': res = l | r; break;
            case '^': res = l ^ r; break;
            case '<': res = l << r; break;
            case '>': res = l >> r; break;
            default:  res = 0; break;
        }
        RA(inst) = make_int64_value(res);
    }
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_bnot(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    const Value& val = RB(inst);
    if (val.type == VAL_UINT64) {
        RA(inst) = make_uint64_value(~MobiusVM::vm_extract_uint64(val));
    } else if (val.type == VAL_INT64) {
        RA(inst) = make_int64_value(~MobiusVM::vm_extract_int64(val));
    } else {
        vm->runtimeError("Bitwise NOT requires an integer operand");
        return -1;
    }
    return 0;
}

// ---- String concat ----

MOBIUS_FORCEINLINE static int vm_op_concat(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    int b_reg = DECODE_B(inst);
    int c_reg = DECODE_C(inst);
    std::string result;
    for (int i = b_reg; i <= c_reg; i++) {
        const Value& v = f.regs[i];
        if (v.type == VAL_STRING && v.as.string) {
            result.append(v.as.string->data, v.as.string->length);
        } else {
            char* s = value_to_string(v);
            if (s) { result.append(s); free(s); }
        }
    }
    RA(inst) = make_string_value_from_cstr(vm->state_, result.c_str());
    return 0;
}

// ---- Comparisons ----

MOBIUS_FORCEINLINE static int vm_op_eq(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    int a = DECODE_A(inst);
    const Value& lhs = RKB(inst);
    const Value& rhs = RKC(inst);
    bool eq;
    if (lhs.type == VAL_TABLE || rhs.type == VAL_TABLE) {
        const Value& tbl = (lhs.type == VAL_TABLE) ? lhs : rhs;
        Value out;
        f.ci->ip = f.ip;
        int rc = vm->callMetamethod(tbl, vm->state_->metamethods()->eq(), lhs, rhs, out);
        vm->refreshFrame(f);
        if (rc < 0) return -1;
        eq = (rc == 1) ? is_truthy(out) : (lhs == rhs);
    } else {
        eq = (lhs == rhs);
    }
    if (eq != (a != 0)) f.ip++;
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_lt(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    int a = DECODE_A(inst);
    const Value& lhs = RKB(inst);
    const Value& rhs = RKC(inst);
    bool l_num = (lhs.type == VAL_INT64 || lhs.type == VAL_UINT64 || lhs.type == VAL_FLOAT64);
    bool r_num = (rhs.type == VAL_INT64 || rhs.type == VAL_UINT64 || rhs.type == VAL_FLOAT64);
    bool lt;
    if (l_num && r_num) {
        if (lhs.type != VAL_FLOAT64 && rhs.type != VAL_FLOAT64) {
            if (MobiusVM::vm_use_unsigned(lhs, rhs))
                lt = MobiusVM::vm_extract_uint64(lhs) < MobiusVM::vm_extract_uint64(rhs);
            else
                lt = MobiusVM::vm_extract_int64(lhs) < MobiusVM::vm_extract_int64(rhs);
        } else {
            lt = MobiusVM::vm_extract_double(lhs) < MobiusVM::vm_extract_double(rhs);
        }
    } else if (lhs.type == VAL_STRING && rhs.type == VAL_STRING) {
        lt = strcmp(lhs.as.string->data, rhs.as.string->data) < 0;
    } else if (lhs.type == VAL_TABLE || rhs.type == VAL_TABLE) {
        const Value& tbl = (lhs.type == VAL_TABLE) ? lhs : rhs;
        Value out;
        f.ci->ip = f.ip;
        int rc = vm->callMetamethod(tbl, vm->state_->metamethods()->lt(), lhs, rhs, out);
        vm->refreshFrame(f);
        if (rc < 0) return -1;
        if (rc == 0) { vm->runtimeError("Cannot compare: no __lt metamethod on table"); return -1; }
        lt = is_truthy(out);
    } else {
        vm->runtimeError("Cannot compare incompatible types");
        return -1;
    }
    if (lt != (a != 0)) f.ip++;
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_le(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    int a = DECODE_A(inst);
    const Value& lhs = RKB(inst);
    const Value& rhs = RKC(inst);
    bool l_num = (lhs.type == VAL_INT64 || lhs.type == VAL_UINT64 || lhs.type == VAL_FLOAT64);
    bool r_num = (rhs.type == VAL_INT64 || rhs.type == VAL_UINT64 || rhs.type == VAL_FLOAT64);
    bool le;
    if (l_num && r_num) {
        if (lhs.type != VAL_FLOAT64 && rhs.type != VAL_FLOAT64) {
            if (MobiusVM::vm_use_unsigned(lhs, rhs))
                le = MobiusVM::vm_extract_uint64(lhs) <= MobiusVM::vm_extract_uint64(rhs);
            else
                le = MobiusVM::vm_extract_int64(lhs) <= MobiusVM::vm_extract_int64(rhs);
        } else {
            le = MobiusVM::vm_extract_double(lhs) <= MobiusVM::vm_extract_double(rhs);
        }
    } else if (lhs.type == VAL_STRING && rhs.type == VAL_STRING) {
        le = strcmp(lhs.as.string->data, rhs.as.string->data) <= 0;
    } else if (lhs.type == VAL_TABLE || rhs.type == VAL_TABLE) {
        const Value& tbl = (lhs.type == VAL_TABLE) ? lhs : rhs;
        Value out;
        f.ci->ip = f.ip;
        int rc = vm->callMetamethod(tbl, vm->state_->metamethods()->le(), lhs, rhs, out);
        vm->refreshFrame(f);
        if (rc < 0) return -1;
        if (rc == 0) { vm->runtimeError("Cannot compare: no __le metamethod on table"); return -1; }
        le = is_truthy(out);
    } else {
        vm->runtimeError("Cannot compare incompatible types");
        return -1;
    }
    if (le != (a != 0)) f.ip++;
    return 0;
}

// ---- Compare-with-immediate ----

#define VM_CMP_IMM_HANDLER(name, cmp_op, err_msg) \
MOBIUS_FORCEINLINE static int name(MobiusVM* vm, VMFrame& f, uint32_t inst) { \
    const Value& lhs = f.regs[DECODE_A(inst)]; \
    int imm = DECODE_sBx(inst); \
    bool result; \
    if (lhs.type == VAL_INT64) result = lhs.as.i64 cmp_op imm; \
    else if (lhs.type == VAL_UINT64) result = (imm < 0) ? (0 cmp_op 1) : lhs.as.u64 cmp_op (uint64_t)imm; \
    else if (lhs.type == VAL_FLOAT64) result = lhs.as.double_val cmp_op (double)imm; \
    else { vm->runtimeError(err_msg " requires numeric operand"); return -1; } \
    if (result) f.ip++; \
    return 0; \
}

VM_CMP_IMM_HANDLER(vm_op_lti, <, "LTI")
VM_CMP_IMM_HANDLER(vm_op_lei, <=, "LEI")
VM_CMP_IMM_HANDLER(vm_op_gti, >, "GTI")
VM_CMP_IMM_HANDLER(vm_op_gei, >=, "GEI")

#undef VM_CMP_IMM_HANDLER

MOBIUS_FORCEINLINE static int vm_op_eqi(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    (void)vm;
    const Value& lhs = f.regs[DECODE_A(inst)];
    int imm = DECODE_sBx(inst);
    bool result;
    if (lhs.type == VAL_INT64) result = lhs.as.i64 == imm;
    else if (lhs.type == VAL_UINT64) result = (imm < 0) ? false : lhs.as.u64 == (uint64_t)imm;
    else if (lhs.type == VAL_FLOAT64) result = lhs.as.double_val == (double)imm;
    else result = false;
    if (result) f.ip++;
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_nei(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    (void)vm;
    const Value& lhs = f.regs[DECODE_A(inst)];
    int imm = DECODE_sBx(inst);
    bool result;
    if (lhs.type == VAL_INT64) result = lhs.as.i64 != imm;
    else if (lhs.type == VAL_UINT64) result = (imm < 0) ? true : lhs.as.u64 != (uint64_t)imm;
    else if (lhs.type == VAL_FLOAT64) result = lhs.as.double_val != (double)imm;
    else result = true;
    if (result) f.ip++;
    return 0;
}

// ---- Logical test / set ----

MOBIUS_FORCEINLINE static int vm_op_test(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    (void)vm;
    bool truthy = is_truthy(RA(inst));
    int c = DECODE_C(inst);
    if (truthy != (c != 0)) f.ip++;
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_testjmp(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    (void)vm;
    if (!is_truthy(f.regs[DECODE_A(inst)]))
        f.ip += DECODE_sBx(inst);
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_testset(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    (void)vm;
    const Value& rb = RB(inst);
    bool truthy = is_truthy(rb);
    int c = DECODE_C(inst);
    if (truthy == (c != 0)) {
        RA(inst) = rb;
    } else {
        f.ip++;
    }
    return 0;
}

// ---- Type-specialized arithmetic ----

MOBIUS_FORCEINLINE static int vm_op_add_ii(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    (void)vm;
    Value& dst = RA(inst); dst.as.i64 = RKB(inst).as.i64 + RKC(inst).as.i64;
    dst.type = VAL_INT64; dst.flags = 0; return 0;
}
MOBIUS_FORCEINLINE static int vm_op_add_ff(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    (void)vm;
    Value& dst = RA(inst); dst.as.double_val = RKB(inst).as.double_val + RKC(inst).as.double_val;
    dst.type = VAL_FLOAT64; dst.flags = 0; return 0;
}
MOBIUS_FORCEINLINE static int vm_op_sub_ii(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    (void)vm;
    Value& dst = RA(inst); dst.as.i64 = RKB(inst).as.i64 - RKC(inst).as.i64;
    dst.type = VAL_INT64; dst.flags = 0; return 0;
}
MOBIUS_FORCEINLINE static int vm_op_sub_ff(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    (void)vm;
    Value& dst = RA(inst); dst.as.double_val = RKB(inst).as.double_val - RKC(inst).as.double_val;
    dst.type = VAL_FLOAT64; dst.flags = 0; return 0;
}
MOBIUS_FORCEINLINE static int vm_op_mul_ii(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    (void)vm;
    Value& dst = RA(inst); dst.as.i64 = RKB(inst).as.i64 * RKC(inst).as.i64;
    dst.type = VAL_INT64; dst.flags = 0; return 0;
}
MOBIUS_FORCEINLINE static int vm_op_mul_ff(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    (void)vm;
    Value& dst = RA(inst); dst.as.double_val = RKB(inst).as.double_val * RKC(inst).as.double_val;
    dst.type = VAL_FLOAT64; dst.flags = 0; return 0;
}
MOBIUS_FORCEINLINE static int vm_op_mod_ii(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    int64_t rv = RKC(inst).as.i64;
    if (MOBIUS_UNLIKELY(rv == 0)) { vm->runtimeError("Modulo by zero"); return -1; }
    Value& dst = RA(inst); dst.as.i64 = RKB(inst).as.i64 % rv;
    dst.type = VAL_INT64; dst.flags = 0; return 0;
}

// ---- Inline-data arithmetic ----

#define READ_INLINE64(f) ((uint64_t)(f).ip[0] | ((uint64_t)(f).ip[1] << 32)); (f).ip += 2

#define VM_INLINE_ARITH(name, op_char, is_mod) \
MOBIUS_FORCEINLINE static int name(MobiusVM* vm, VMFrame& f, uint32_t inst) { \
    uint8_t tag = DECODE_C(inst); \
    uint64_t raw = READ_INLINE64(f); \
    Value& dst = RA(inst); \
    const Value& src = f.regs[DECODE_B(inst)]; \
    if (MOBIUS_LIKELY(tag == VAL_INT64 && src.type == VAL_INT64)) { \
        int64_t k; memcpy(&k, &raw, 8); \
        if (is_mod && MOBIUS_UNLIKELY(k == 0)) { vm->runtimeError(#name ": division/modulo by zero"); return -1; } \
        dst.as.i64 = src.as.i64 op_char k; \
        dst.type = VAL_INT64; dst.flags = 0; \
    } else if (!is_mod && tag == VAL_FLOAT64 && src.type == VAL_FLOAT64) { \
        double k; memcpy(&k, &raw, 8); \
        dst.as.double_val = src.as.double_val op_char k; \
        dst.type = VAL_FLOAT64; dst.flags = 0; \
    } else { \
        double sv = MobiusVM::vm_extract_double(src); \
        double kv; \
        if (tag == VAL_INT64) { int64_t k; memcpy(&k, &raw, 8); kv = (double)k; } \
        else { memcpy(&kv, &raw, 8); } \
        if (is_mod && kv == 0.0) { vm->runtimeError(#name ": division/modulo by zero"); return -1; } \
        dst.as.double_val = is_mod ? fmod(sv, kv) : (sv op_char kv); \
        dst.type = VAL_FLOAT64; dst.flags = 0; \
    } \
    return 0; \
}

VM_INLINE_ARITH(vm_op_addk, +, false)
VM_INLINE_ARITH(vm_op_subk, -, false)
VM_INLINE_ARITH(vm_op_mulk, *, false)

#undef VM_INLINE_ARITH

MOBIUS_FORCEINLINE static int vm_op_divk(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    uint8_t tag = DECODE_C(inst);
    uint64_t raw = READ_INLINE64(f);
    Value& dst = RA(inst);
    const Value& src = f.regs[DECODE_B(inst)];
    if (MOBIUS_LIKELY(tag == VAL_INT64 && src.type == VAL_INT64)) {
        int64_t k; memcpy(&k, &raw, 8);
        if (MOBIUS_UNLIKELY(k == 0)) { vm->runtimeError("Division by zero"); return -1; }
        dst.as.i64 = src.as.i64 / k;
        dst.type = VAL_INT64; dst.flags = 0;
    } else {
        double sv = MobiusVM::vm_extract_double(src);
        double kv;
        if (tag == VAL_INT64) { int64_t k; memcpy(&k, &raw, 8); kv = (double)k; }
        else { memcpy(&kv, &raw, 8); }
        if (kv == 0.0) { vm->runtimeError("Division by zero"); return -1; }
        dst.as.double_val = sv / kv;
        dst.type = VAL_FLOAT64; dst.flags = 0;
    }
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_modk(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    uint8_t tag = DECODE_C(inst);
    uint64_t raw = READ_INLINE64(f);
    Value& dst = RA(inst);
    const Value& src = f.regs[DECODE_B(inst)];
    if (MOBIUS_LIKELY(tag == VAL_INT64 && src.type == VAL_INT64)) {
        int64_t k; memcpy(&k, &raw, 8);
        if (MOBIUS_UNLIKELY(k == 0)) { vm->runtimeError("Modulo by zero"); return -1; }
        dst.as.i64 = src.as.i64 % k;
        dst.type = VAL_INT64; dst.flags = 0;
    } else {
        double sv = MobiusVM::vm_extract_double(src);
        double kv;
        if (tag == VAL_INT64) { int64_t k; memcpy(&k, &raw, 8); kv = (double)k; }
        else { memcpy(&kv, &raw, 8); }
        if (kv == 0.0) { vm->runtimeError("Modulo by zero"); return -1; }
        dst.as.double_val = fmod(sv, kv);
        dst.type = VAL_FLOAT64; dst.flags = 0;
    }
    return 0;
}

#undef READ_INLINE64

// ---- Arithmetic with immediate ----

#define VM_ARITH_IMM(name, op_char, err_msg, check_zero) \
MOBIUS_FORCEINLINE static int name(MobiusVM* vm, VMFrame& f, uint32_t inst) { \
    int a = DECODE_A(inst); \
    int imm = DECODE_sBx(inst); \
    if (check_zero && MOBIUS_UNLIKELY(imm == 0)) { vm->runtimeError("Division by zero"); return -1; } \
    Value& val = f.regs[a]; \
    if (MOBIUS_LIKELY(val.type == VAL_INT64)) { val.as.i64 op_char##= imm; } \
    else if (val.type == VAL_UINT64) val.as.u64 op_char##= (uint64_t)(int64_t)imm; \
    else if (val.type == VAL_FLOAT64) val.as.double_val op_char##= imm; \
    else { vm->runtimeError(err_msg " requires numeric operand"); return -1; } \
    return 0; \
}

VM_ARITH_IMM(vm_op_addi, +, "ADDI", false)
VM_ARITH_IMM(vm_op_subi, -, "SUBI", false)
VM_ARITH_IMM(vm_op_muli, *, "MULI", false)
VM_ARITH_IMM(vm_op_divi, /, "DIVI", true)

#undef VM_ARITH_IMM

MOBIUS_FORCEINLINE static int vm_op_modi(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    int a = DECODE_A(inst);
    int imm = DECODE_sBx(inst);
    if (MOBIUS_UNLIKELY(imm == 0)) { vm->runtimeError("Modulo by zero"); return -1; }
    Value& val = f.regs[a];
    if (MOBIUS_LIKELY(val.type == VAL_INT64)) { val.as.i64 %= imm; }
    else if (val.type == VAL_UINT64) val.as.u64 %= (uint64_t)(int64_t)imm;
    else if (val.type == VAL_FLOAT64) val.as.double_val = fmod(val.as.double_val, (double)imm);
    else { vm->runtimeError("MODI requires numeric operand"); return -1; }
    return 0;
}

// ---- Jumps ----

MOBIUS_FORCEINLINE static int vm_op_jmp(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    (void)vm;
    f.ip += DECODE_sBx_wide(inst);
    return 0;
}

// ---- Increment / Decrement ----

MOBIUS_FORCEINLINE static int vm_op_inc(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    const Value& val = RB(inst);
    if (val.type != VAL_INT64) { vm->runtimeError("Increment requires an integer operand"); return -1; }
    bool success;
    RA(inst) = increment_integer(val, true, &success);
    if (!success) { vm->runtimeError("Failed to increment value"); return -1; }
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_dec(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    const Value& val = RB(inst);
    if (val.type != VAL_INT64) { vm->runtimeError("Decrement requires an integer operand"); return -1; }
    bool success;
    RA(inst) = increment_integer(val, false, &success);
    if (!success) { vm->runtimeError("Failed to decrement value"); return -1; }
    return 0;
}

// ---- Type checking ----

MOBIUS_FORCEINLINE static int vm_op_typecheck(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    NumberType target = (NumberType)DECODE_B(inst);
    TypeCheckConfig tc = {
        vm->state_->config().strict_mode,
        vm->state_->config().warn_on_conversion
    };
    TypeConversionResult conv = validate_and_convert_value(RA(inst), target, true, tc);
    if (!conv.success) {
        vm->runtimeError("%s", conv.error_message ? conv.error_message : "Type validation failed");
        free(conv.error_message);
        return -1;
    }
    if (conv.was_converted && vm->state_->config().warn_on_conversion) {
        fprintf(stderr, "Warning: Implicit type conversion at line %d\n", vm->currentLine());
    }
    RA(inst) = conv.converted_value;
    free(conv.error_message);
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_isnum(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    (void)vm;
    const Value& v = RB(inst);
    RA(inst) = make_bool_value(v.type == VAL_INT64 || v.type == VAL_FLOAT64);
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_typecompat(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    (void)vm;
    int a = DECODE_A(inst);
    const Value& lhs = RKB(inst);
    const Value& rhs = RKC(inst);
    bool l_num = (lhs.type == VAL_INT64 || lhs.type == VAL_FLOAT64);
    bool r_num = (rhs.type == VAL_INT64 || rhs.type == VAL_FLOAT64);
    bool compat = (l_num && r_num) || (lhs.type == VAL_STRING && rhs.type == VAL_STRING);
    if (compat != (a != 0)) f.ip++;
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_typeis(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    (void)vm;
    int a = DECODE_A(inst);
    const Value& val = RB(inst);
    uint8_t expected_type = DECODE_C(inst);
    bool match = ((uint8_t)val.type == expected_type);
    if (match != (a != 0)) f.ip++;
    return 0;
}

// ---- Length ----

MOBIUS_FORCEINLINE static int vm_op_len(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    const Value& val = RB(inst);
    if (val.type == VAL_ARRAY && val.as.array) {
        RA(inst) = make_int64_value((int64_t)val.as.array->length());
    } else if (val.type == VAL_TABLE && val.as.table) {
        RA(inst) = make_int64_value((int64_t)val.as.table->size());
    } else if (val.type == VAL_STRING && val.as.string) {
        RA(inst) = make_int64_value((int64_t)val.as.string->length);
    } else {
        vm->runtimeError("Attempt to get length of a %s value", value_type_name(val.type));
        return -1;
    }
    return 0;
}

// ---- For loops ----

MOBIUS_FORCEINLINE static int vm_op_forprep(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    (void)vm;
    int a = DECODE_A(inst);
    int sbx = DECODE_sBx(inst);
    Value& idx  = f.regs[a];
    Value& step = f.regs[a + 2];
    if (idx.type == VAL_INT64 && step.type == VAL_INT64) {
        idx = make_int64_value(MobiusVM::vm_extract_int64(idx) - MobiusVM::vm_extract_int64(step));
    } else {
        idx = make_float_value(MobiusVM::vm_extract_double(idx) - MobiusVM::vm_extract_double(step));
    }
    f.ip += sbx;
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_forloop(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    (void)vm;
    int a = DECODE_A(inst);
    int sbx = DECODE_sBx(inst);
    Value& idx   = f.regs[a];
    Value& limit = f.regs[a + 1];
    Value& step  = f.regs[a + 2];
    if (idx.type == VAL_INT64 && limit.type == VAL_INT64 && step.type == VAL_INT64) {
        int64_t iv = MobiusVM::vm_extract_int64(idx);
        int64_t sv = MobiusVM::vm_extract_int64(step);
        int64_t lv = MobiusVM::vm_extract_int64(limit);
        iv += sv;
        idx = make_int64_value(iv);
        if ((sv > 0) ? (iv <= lv) : (iv >= lv)) {
            f.ip += sbx;
            f.regs[a + 3] = idx;
        }
    } else {
        double iv = MobiusVM::vm_extract_double(idx);
        double sv = MobiusVM::vm_extract_double(step);
        double lv = MobiusVM::vm_extract_double(limit);
        iv += sv;
        idx = make_float_value(iv);
        if ((sv > 0) ? (iv <= lv) : (iv >= lv)) {
            f.ip += sbx;
            f.regs[a + 3] = idx;
        }
    }
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_iforprep(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    (void)vm;
    int a = DECODE_A(inst);
    f.regs[a].type = VAL_INT64;
    f.regs[a].as.i64 -= f.regs[a + 2].as.i64;
    f.ip += DECODE_sBx(inst);
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_iforloop(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    (void)vm;
    int a = DECODE_A(inst);
    int64_t sv = f.regs[a + 2].as.i64;
    int64_t iv = f.regs[a].as.i64 + sv;
    f.regs[a].as.i64 = iv;
    if ((sv > 0) ? (iv <= f.regs[a + 1].as.i64) : (iv >= f.regs[a + 1].as.i64)) {
        f.ip += DECODE_sBx(inst);
        f.regs[a + 3].as.i64 = iv;
        f.regs[a + 3].type = VAL_INT64;
    }
    return 0;
}

// ---- Superinstructions ----

MOBIUS_FORCEINLINE static int vm_op_move_addi(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    RA(inst) = RB(inst);
    uint32_t inst2 = *f.ip++;
    int a2 = DECODE_A(inst2);
    int imm = DECODE_sBx(inst2);
    Value& val = f.regs[a2];
    if (MOBIUS_LIKELY(val.type == VAL_INT64)) { val.as.i64 += imm; }
    else if (val.type == VAL_UINT64) val.as.u64 += (uint64_t)(int64_t)imm;
    else if (val.type == VAL_FLOAT64) val.as.double_val += imm;
    else { vm->runtimeError("ADDI requires numeric operand"); return -1; }
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_getglobal_gettable(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    int a = DECODE_A(inst);
    int slot = DECODE_Bx(inst);
    const Value& gv = vm->state_->globalSlot(slot);
    if (MOBIUS_UNLIKELY(!(gv.flags & VAL_FLAG_DEFINED))) {
        vm->runtimeError("Undefined variable '%s'", vm->state_->globalSlotName(slot));
        return -1;
    }
    f.regs[a] = gv;
    uint32_t inst2 = *f.ip++;
    uint8_t c2 = DECODE_C(inst2);
    const Value& key = IS_CONSTANT(c2)
        ? f.ci->proto->constants[RK_AS_CONSTANT(c2)]
        : f.regs[c2];
    const Value& tbl = f.regs[a];
    if (MOBIUS_LIKELY(tbl.type == VAL_TABLE && tbl.as.table)) {
        f.regs[a] = tbl.as.table->get(key);
    } else if (tbl.type == VAL_ARRAY && tbl.as.array) {
        if (key.type == VAL_INT64) {
            int64_t idx = MobiusVM::vm_extract_int64(key);
            if (idx >= 0 && idx < (int64_t)tbl.as.array->length())
                f.regs[a] = tbl.as.array->get((size_t)idx);
            else
                f.regs[a] = Value();
        } else {
            vm->runtimeError("Array index must be an integer");
            return -1;
        }
    } else {
        vm->runtimeError("Attempt to index a %s value", value_type_name(tbl.type));
        return -1;
    }
    return 0;
}

// ---- Call / Tailcall ----
// Return 0 = no frame switch, 2 = frame switched (caller must refresh), -1 = error
MOBIUS_FORCEINLINE static int vm_op_call(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    int a = DECODE_A(inst);
    int b = DECODE_B(inst);
    int c = DECODE_C(inst);
    int nargs = b - 1;
    f.ci->ip = f.ip;
    int rc = vm->callFunction(*f.ci, a, nargs, c);
    if (rc < 0) return -1;
    if (rc == 1) { vm->refreshFrame(f); return 0; }
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_tailcall(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    int a = DECODE_A(inst);
    int b = DECODE_B(inst);
    int nargs = b - 1;
    f.ci->ip = f.ip;
    int rc = vm->callFunction(*f.ci, a, nargs, 0);
    if (rc < 0) return -1;
    if (rc == 1) { vm->refreshFrame(f); return 0; }
    return 0;
}

// ---- Return ----
// Returns 1 = exit run(), 0 = continue, -1 = error
MOBIUS_FORCEINLINE static int vm_op_return(MobiusVM* vm, VMFrame& f, uint32_t inst, size_t base_depth) {
    int a = DECODE_A(inst);
    int b = DECODE_B(inst);
    if (vm->call_stack_.size() <= base_depth) return 1;
    int nresults_available = (b == 0) ? 0 : (b - 1);
    vm->closeUpvalues(*f.ci, 0);
    CallInfo returning = vm->call_stack_.back();
    vm->call_stack_.pop_back();
    int func_reg_abs = returning.base - 1;
    int nresults_wanted = returning.nresults;
    if (nresults_wanted != 0) {
        int to_copy = nresults_wanted - 1;
        for (int i = 0; i < to_copy; i++) {
            if (i < nresults_available)
                vm->registers_[func_reg_abs + i] = vm->registers_[returning.base + a + i];
            else
                vm->registers_[func_reg_abs + i] = Value();
        }
    }
    if (vm->call_stack_.size() <= base_depth) return 1;
    vm->refreshFrame(f);
    return 0;
}

// ---- Closure ----
MOBIUS_FORCEINLINE static int vm_op_closure(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    uint16_t bx = DECODE_Bx(inst);
    if (bx >= f.proto->protos.size()) {
        vm->runtimeError("Invalid prototype index %d", bx);
        return -1;
    }
    Prototype* child_proto = f.proto->protos[bx];
    MobiusFunction* mf = (MobiusFunction*)calloc(1, sizeof(MobiusFunction));
    mf->name = child_proto->name.empty() ? nullptr :
               vm->state_->stringPool()->intern(child_proto->name.c_str());
    mf->param_count = child_proto->num_params;
    mf->body = nullptr;
    mf->body_count = 0;
    mf->closure = vm->global_env_;
    if (mf->closure) mf->closure->retain();
    mf->ref_count = 1;
    mf->proto = child_proto;
    if (child_proto->num_params > 0 && !child_proto->local_vars.empty()) {
        mf->param_names = (MobiusString**)calloc(child_proto->num_params, sizeof(MobiusString*));
        for (int i = 0; i < child_proto->num_params && i < (int)child_proto->local_vars.size(); i++) {
            mf->param_names[i] = vm->state_->stringPool()->intern(child_proto->local_vars[i].name.c_str());
        }
    } else {
        mf->param_names = nullptr;
    }
    int nupvals = (int)child_proto->upvalues.size();
    if (nupvals > 0) {
        mf->upvalues = (Upvalue**)calloc(nupvals, sizeof(Upvalue*));
        mf->upvalue_count = nupvals;
        for (int u = 0; u < nupvals; u++) {
            const UpvalueDesc& desc = child_proto->upvalues[u];
            if (desc.in_stack) {
                Value* reg_ptr = &f.regs[desc.index];
                Upvalue* existing = nullptr;
                for (auto* ouv : f.ci->open_upvalues) {
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
                    f.ci->open_upvalues.push_back(uv);
                    mf->upvalues[u] = uv;
                }
            } else {
                if (desc.index < (int)f.ci->open_upvalues.size()) {
                    mf->upvalues[u] = f.ci->open_upvalues[desc.index];
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
    return 0;
}

// ---- Close ----
MOBIUS_FORCEINLINE static int vm_op_close(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    vm->closeUpvalues(*f.ci, DECODE_A(inst));
    return 0;
}

// ---- TForLoop ----
MOBIUS_FORCEINLINE static int vm_op_tforloop(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    (void)f; (void)inst;
    vm->runtimeError("Generic for-loop (TFORLOOP) not yet implemented");
    return -1;
}

// ---- Enum ----
MOBIUS_FORCEINLINE static int vm_op_newenum(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    uint16_t bx = DECODE_Bx(inst);
    const Value& name_val = f.proto->constants[bx];
    const char* enum_name = (name_val.type == VAL_STRING && name_val.as.string)
                            ? name_val.as.string->data : "unknown";
    EnumDefinition* edef = new (std::nothrow) EnumDefinition(enum_name, NUM_INT64);
    if (!edef) { vm->runtimeError("Failed to allocate enum"); return -1; }
    RA(inst) = make_userdata_value(edef,
        [](void* p) { if (p) static_cast<EnumDefinition*>(p)->release(); },
        "enum_definition", sizeof(EnumDefinition));
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_enumval(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    Value& enum_val = RA(inst);
    if (enum_val.type != VAL_USERDATA || !enum_val.as.userdata ||
        strcmp(enum_val.as.userdata->type_name, "enum_definition") != 0) {
        vm->runtimeError("ENUMVAL: target is not an enum definition");
        return -1;
    }
    EnumDefinition* edef = static_cast<EnumDefinition*>(enum_val.as.userdata->ptr);
    const Value& member_val = RB(inst);
    int member_idx = DECODE_C(inst);
    (void)member_idx;
    if (member_val.type == VAL_INT64) {
        int64_t iv = MobiusVM::vm_extract_int64(member_val);
        char name_buf[32];
        snprintf(name_buf, sizeof(name_buf), "member_%d", member_idx);
        edef->addMember(name_buf, iv);
    } else {
        edef->addAutoMember("member");
    }
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_getenum(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    const Value& enum_val = RB(inst);
    if (enum_val.type != VAL_USERDATA || !enum_val.as.userdata ||
        strcmp(enum_val.as.userdata->type_name, "enum_definition") != 0) {
        vm->runtimeError("GETENUM: target is not an enum definition");
        return -1;
    }
    EnumDefinition* edef = static_cast<EnumDefinition*>(enum_val.as.userdata->ptr);
    int member_idx = DECODE_C(inst);
    (void)member_idx; (void)edef;
    RA(inst) = Value();
    return 0;
}

// ---- Import ----
MOBIUS_FORCEINLINE static int vm_op_import(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    const Value& mod_name_val = IS_CONSTANT(DECODE_B(inst))
        ? f.ci->proto->constants[RK_AS_CONSTANT(DECODE_B(inst))]
        : f.regs[DECODE_B(inst)];
    const Value& alias_val = IS_CONSTANT(DECODE_C(inst))
        ? f.ci->proto->constants[RK_AS_CONSTANT(DECODE_C(inst))]
        : f.regs[DECODE_C(inst)];
    if (mod_name_val.type != VAL_STRING || !mod_name_val.as.string) {
        vm->runtimeError("IMPORT: invalid module name"); return -1;
    }
    if (alias_val.type != VAL_STRING || !alias_val.as.string) {
        vm->runtimeError("IMPORT: invalid alias"); return -1;
    }
    const char* module_name = mod_name_val.as.string->data;
    const char* alias_name  = alias_val.as.string->data;
    ModuleRegistry* registry = getGlobalRegistry();
    if (!registry) { vm->runtimeError("Module registry not initialized"); return -1; }
    LoadedModule* module = registry->findModule(module_name);
    if (!module) {
        PluginLoadResult result = registry->loadModuleByName(module_name);
        if (result.status != PLUGIN_STATUS_LOADED) {
            vm->runtimeError("Import failed - module '%s' not found", module_name); return -1;
        }
        module = registry->findModule(module_name);
    }
    if (!module || !module->plugin) {
        vm->runtimeError("Module '%s' has no plugin interface", module_name); return -1;
    }
    registry->incrementRefCount(module_name);
    Plugin* plugin = module->plugin;
    StringInternPool* pool = vm->state_->stringPool();
    bool is_global = (strcmp(alias_name, "_GLOBAL") == 0);
    if (is_global) {
        for (size_t i = 0; i < plugin->function_count; i++) {
            PluginFunction* func = &plugin->functions[i];
            if (!func || !func->name || !func->function) continue;
            Value func_val = make_native_function_value(func->function);
            int slot = vm->state_->assignGlobalSlot(func->name);
            func_val.flags |= VAL_FLAG_DEFINED;
            vm->state_->globalSlot(slot) = func_val;
            vm->global_env_->define(pool->intern(func->name), func_val);
        }
    } else if (strchr(alias_name, '.') != nullptr) {
        char buf[256];
        strncpy(buf, alias_name, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        const char* components[32];
        int ncomps = 0;
        char* tok = strtok(buf, ".");
        while (tok && ncomps < 32) { components[ncomps++] = tok; tok = strtok(nullptr, "."); }
        MobiusString* first = pool->intern(components[0]);
        bool found = false;
        Value cur_val;
        int ns_slot = vm->state_->findGlobalSlot(components[0]);
        if (ns_slot >= 0 && (vm->state_->globalSlot(ns_slot).flags & VAL_FLAG_DEFINED)) {
            cur_val = vm->state_->globalSlot(ns_slot); found = true;
        } else {
            cur_val = vm->global_env_->get(first, &found);
        }
        Table* cur_table = nullptr;
        if (found && cur_val.type == VAL_TABLE) {
            cur_table = cur_val.as.table;
        } else if (found) {
            vm->runtimeError("Cannot create nested namespace '%s': '%s' is not a table", alias_name, components[0]);
            return -1;
        } else {
            cur_table = new (std::nothrow) Table(vm->state_, 16);
            if (!cur_table) { vm->runtimeError("Failed to create namespace table"); return -1; }
            Value tval = make_table_value(cur_table);
            tval.flags |= VAL_FLAG_DEFINED;
            int s = vm->state_->assignGlobalSlot(components[0]);
            vm->state_->globalSlot(s) = tval;
            vm->global_env_->define(first, tval);
        }
        for (int i = 1; i < ncomps; i++) {
            Value key = make_string_value_from_cstr(vm->state_, components[i]);
            Value next = cur_table->get(key);
            if (next.type == VAL_TABLE) {
                cur_table = next.as.table;
            } else {
                Table* sub = new (std::nothrow) Table(vm->state_, 16);
                if (!sub) { vm->runtimeError("Failed to create nested namespace table"); return -1; }
                cur_table->set(key, make_table_value(sub));
                cur_table = sub;
            }
        }
        for (size_t i = 0; i < plugin->function_count; i++) {
            PluginFunction* func = &plugin->functions[i];
            if (!func || !func->name || !func->function) continue;
            Value func_key = make_string_value_from_cstr(vm->state_, func->name);
            Value func_val = make_native_function_value(func->function);
            cur_table->set(func_key, func_val);
        }
    } else {
        MobiusString* interned_alias = pool->intern(alias_name);
        bool found = false;
        Value existing;
        int alias_slot = vm->state_->findGlobalSlot(alias_name);
        if (alias_slot >= 0 && (vm->state_->globalSlot(alias_slot).flags & VAL_FLAG_DEFINED)) {
            existing = vm->state_->globalSlot(alias_slot); found = true;
        } else {
            existing = vm->global_env_->get(interned_alias, &found);
        }
        Table* mod_table = nullptr;
        if (found && existing.type == VAL_TABLE) {
            mod_table = existing.as.table;
        } else {
            mod_table = new (std::nothrow) Table(vm->state_, 16);
            if (!mod_table) { vm->runtimeError("Failed to create module table"); return -1; }
        }
        for (size_t i = 0; i < plugin->function_count; i++) {
            PluginFunction* func = &plugin->functions[i];
            if (!func || !func->name || !func->function) continue;
            Value func_key = make_string_value_from_cstr(vm->state_, func->name);
            Value func_val = make_native_function_value(func->function);
            mod_table->set(func_key, func_val);
        }
        if (!found || existing.type != VAL_TABLE) {
            Value tval = make_table_value(mod_table);
            tval.flags |= VAL_FLAG_DEFINED;
            int s = vm->state_->assignGlobalSlot(alias_name);
            vm->state_->globalSlot(s) = tval;
            vm->global_env_->define(interned_alias, tval);
        }
    }
    return 0;
}

// ---- Pragma ----
MOBIUS_FORCEINLINE static int vm_op_pragma(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    uint16_t bx = DECODE_Bx(inst);
    const Value& name_val = f.proto->constants[bx];
    if (name_val.type != VAL_STRING || !name_val.as.string) {
        vm->runtimeError("PRAGMA: invalid pragma name"); return -1;
    }
    const char* pragma_name = name_val.as.string->data;
    const Value& pval = RA(inst);
    if (strcmp(pragma_name, "strict_types") == 0) {
        vm->state_->config().strict_mode = is_truthy(pval);
    } else if (strcmp(pragma_name, "type_warnings") == 0) {
        vm->state_->config().warn_on_conversion = is_truthy(pval);
    } else if (strcmp(pragma_name, "override_behavior") == 0) {
        if (pval.type == VAL_STRING && pval.as.string) {
            const char* v = pval.as.string->data;
            if (strcmp(v, "error") == 0)
                vm->state_->config().override_behavior = MOBIUS_OVERRIDE_ERROR;
            else if (strcmp(v, "warn") == 0)
                vm->state_->config().override_behavior = MOBIUS_OVERRIDE_WARN;
            else if (strcmp(v, "quiet") == 0)
                vm->state_->config().override_behavior = MOBIUS_OVERRIDE_QUIET;
            else {
                vm->runtimeError("Invalid value for pragma override_behavior: '%s' "
                                 "(expected 'error', 'warn', or 'quiet')", v);
                return -1;
            }
        } else {
            vm->runtimeError("Invalid value for pragma override_behavior "
                             "(expected 'error', 'warn', or 'quiet')");
            return -1;
        }
    } else {
        vm->runtimeError("Unknown pragma: '%s'", pragma_name);
        return -1;
    }
    return 0;
}

// ---- NOP ----
MOBIUS_FORCEINLINE static int vm_op_nop(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    (void)vm; (void)f; (void)inst;
    return 0;
}

// Clean up handler macros before run()
#undef RA
#undef RB
#undef RC
#undef RKB
#undef RKC
#undef KBx

// ============================================================================
// Main dispatch loop
// ============================================================================

void MobiusVM::refreshFrame(VMFrame& f) {
    f.ci = &call_stack_.back();
    f.proto = f.ci->proto;
    f.base = f.ci->base;
    f.regs = registers_.data() + f.base;
    f.ip = f.ci->ip;
}

int MobiusVM::run(size_t base_depth) {
    VMFrame f;
    f.ci = &call_stack_.back();
    f.ip = f.ci->ip;
    f.proto = f.ci->proto;
    f.base = f.ci->base;
    f.regs = registers_.data() + f.base;

    uint32_t inst;

#if defined(__GNUC__) || defined(__clang__)
    static const void* dispatch_table[] = {
        &&L_OP_MOVE, &&L_OP_LOADK, &&L_OP_LOADNIL, &&L_OP_LOADBOOL, &&L_OP_LOADINT,
        &&L_OP_GETUPVAL, &&L_OP_SETUPVAL, &&L_OP_GETGLOBAL, &&L_OP_SETGLOBAL,
        &&L_OP_NEWTABLE, &&L_OP_NEWARRAY, &&L_OP_GETTABLE, &&L_OP_SETTABLE,
        &&L_OP_ADD, &&L_OP_SUB, &&L_OP_MUL, &&L_OP_DIV, &&L_OP_MOD,
        &&L_OP_UNM, &&L_OP_NOT,
        &&L_OP_BAND, &&L_OP_BOR, &&L_OP_BXOR, &&L_OP_BNOT, &&L_OP_SHL, &&L_OP_SHR,
        &&L_OP_CONCAT,
        &&L_OP_EQ, &&L_OP_LT, &&L_OP_LE,
        &&L_OP_TEST, &&L_OP_TESTSET,
        &&L_OP_JMP,
        &&L_OP_CALL, &&L_OP_TAILCALL, &&L_OP_RETURN,
        &&L_OP_CLOSURE, &&L_OP_CLOSE,
        &&L_OP_FORPREP, &&L_OP_FORLOOP, &&L_OP_IFORPREP, &&L_OP_IFORLOOP,
        &&L_OP_TFORLOOP,
        &&L_OP_NEWENUM, &&L_OP_ENUMVAL, &&L_OP_GETENUM,
        &&L_OP_IMPORT, &&L_OP_PRAGMA,
        &&L_OP_INC, &&L_OP_DEC,
        &&L_OP_TYPECHECK, &&L_OP_ISNUM, &&L_OP_TYPECOMPAT, &&L_OP_TYPEIS,
        &&L_OP_LTI, &&L_OP_LEI, &&L_OP_EQI, &&L_OP_NEI, &&L_OP_GTI, &&L_OP_GEI,
        &&L_OP_ADDI, &&L_OP_SUBI, &&L_OP_MULI, &&L_OP_DIVI, &&L_OP_MODI,
        &&L_OP_TESTJMP,
        &&L_OP_ADD_II, &&L_OP_ADD_FF, &&L_OP_SUB_II, &&L_OP_SUB_FF,
        &&L_OP_MUL_II, &&L_OP_MUL_FF, &&L_OP_MOD_II,
        &&L_OP_ADDK, &&L_OP_SUBK, &&L_OP_MULK, &&L_OP_DIVK, &&L_OP_MODK,
        &&L_OP_MOVE_ADDI, &&L_OP_GETGLOBAL_GETTABLE,
        &&L_OP_LEN,
        &&L_OP_NOP,
    };
    static_assert(sizeof(dispatch_table) / sizeof(dispatch_table[0]) == OP_MAX_OPCODE,
                  "dispatch_table must match OpCode enum");

    #define VM_DISPATCH() do { inst = *f.ip++; goto *dispatch_table[DECODE_OP(inst)]; } while(0)
    #define VM_CASE(op) L_##op:
    #define VM_NEXT() VM_DISPATCH()
    #define VM_DEFAULT() L_VM_DEFAULT:

    VM_DISPATCH();

#else
    #define VM_DISPATCH() for(;;) { inst = *f.ip++; switch(DECODE_OP(inst)) {
    #define VM_CASE(op) case op:
    #define VM_NEXT() break
    #define VM_DEFAULT() default:
    #define VM_END_DISPATCH() }} /* switch, for */

    VM_DISPATCH()

#endif

    // Handlers that delegate to extracted functions
    #define VM_HANDLER(op, fn) VM_CASE(op) { if (fn(this, f, inst) < 0) return -1; VM_NEXT(); }

    VM_HANDLER(OP_MOVE, vm_op_move)
    VM_HANDLER(OP_LOADK, vm_op_loadk)
    VM_HANDLER(OP_LOADNIL, vm_op_loadnil)
    VM_HANDLER(OP_LOADBOOL, vm_op_loadbool)
    VM_HANDLER(OP_LOADINT, vm_op_loadint)
    VM_HANDLER(OP_GETUPVAL, vm_op_getupval)
    VM_HANDLER(OP_SETUPVAL, vm_op_setupval)
    VM_HANDLER(OP_GETGLOBAL, vm_op_getglobal)
    VM_HANDLER(OP_SETGLOBAL, vm_op_setglobal)
    VM_HANDLER(OP_NEWTABLE, vm_op_newtable)
    VM_HANDLER(OP_NEWARRAY, vm_op_newarray)
    VM_HANDLER(OP_GETTABLE, vm_op_gettable)
    VM_HANDLER(OP_SETTABLE, vm_op_settable)
    VM_HANDLER(OP_ADD, vm_op_add)
    VM_HANDLER(OP_SUB, vm_op_sub)
    VM_HANDLER(OP_MUL, vm_op_mul)
    VM_HANDLER(OP_DIV, vm_op_div)
    VM_HANDLER(OP_MOD, vm_op_mod)
    VM_HANDLER(OP_UNM, vm_op_unm)
    VM_HANDLER(OP_NOT, vm_op_not)

    VM_CASE(OP_BAND) { if (vm_op_bitwise(this, f, inst, '&') < 0) return -1; VM_NEXT(); }
    VM_CASE(OP_BOR)  { if (vm_op_bitwise(this, f, inst, '|') < 0) return -1; VM_NEXT(); }
    VM_CASE(OP_BXOR) { if (vm_op_bitwise(this, f, inst, '^') < 0) return -1; VM_NEXT(); }
    VM_CASE(OP_SHL)  { if (vm_op_bitwise(this, f, inst, '<') < 0) return -1; VM_NEXT(); }
    VM_CASE(OP_SHR)  { if (vm_op_bitwise(this, f, inst, '>') < 0) return -1; VM_NEXT(); }
    VM_HANDLER(OP_BNOT, vm_op_bnot)

    VM_HANDLER(OP_CONCAT, vm_op_concat)
    VM_HANDLER(OP_EQ, vm_op_eq)
    VM_HANDLER(OP_LT, vm_op_lt)
    VM_HANDLER(OP_LE, vm_op_le)
    VM_HANDLER(OP_TEST, vm_op_test)
    VM_HANDLER(OP_TESTSET, vm_op_testset)
    VM_HANDLER(OP_TESTJMP, vm_op_testjmp)
    VM_HANDLER(OP_JMP, vm_op_jmp)

    VM_HANDLER(OP_LTI, vm_op_lti)
    VM_HANDLER(OP_LEI, vm_op_lei)
    VM_HANDLER(OP_EQI, vm_op_eqi)
    VM_HANDLER(OP_NEI, vm_op_nei)
    VM_HANDLER(OP_GTI, vm_op_gti)
    VM_HANDLER(OP_GEI, vm_op_gei)

    VM_HANDLER(OP_ADDI, vm_op_addi)
    VM_HANDLER(OP_SUBI, vm_op_subi)
    VM_HANDLER(OP_MULI, vm_op_muli)
    VM_HANDLER(OP_DIVI, vm_op_divi)
    VM_HANDLER(OP_MODI, vm_op_modi)

    VM_HANDLER(OP_ADD_II, vm_op_add_ii)
    VM_HANDLER(OP_ADD_FF, vm_op_add_ff)
    VM_HANDLER(OP_SUB_II, vm_op_sub_ii)
    VM_HANDLER(OP_SUB_FF, vm_op_sub_ff)
    VM_HANDLER(OP_MUL_II, vm_op_mul_ii)
    VM_HANDLER(OP_MUL_FF, vm_op_mul_ff)
    VM_HANDLER(OP_MOD_II, vm_op_mod_ii)

    VM_HANDLER(OP_ADDK, vm_op_addk)
    VM_HANDLER(OP_SUBK, vm_op_subk)
    VM_HANDLER(OP_MULK, vm_op_mulk)
    VM_HANDLER(OP_DIVK, vm_op_divk)
    VM_HANDLER(OP_MODK, vm_op_modk)

    VM_HANDLER(OP_INC, vm_op_inc)
    VM_HANDLER(OP_DEC, vm_op_dec)
    VM_HANDLER(OP_TYPECHECK, vm_op_typecheck)
    VM_HANDLER(OP_ISNUM, vm_op_isnum)
    VM_HANDLER(OP_TYPECOMPAT, vm_op_typecompat)
    VM_HANDLER(OP_TYPEIS, vm_op_typeis)
    VM_HANDLER(OP_LEN, vm_op_len)

    VM_HANDLER(OP_FORPREP, vm_op_forprep)
    VM_HANDLER(OP_FORLOOP, vm_op_forloop)
    VM_HANDLER(OP_IFORPREP, vm_op_iforprep)
    VM_HANDLER(OP_IFORLOOP, vm_op_iforloop)

    VM_HANDLER(OP_MOVE_ADDI, vm_op_move_addi)
    VM_HANDLER(OP_GETGLOBAL_GETTABLE, vm_op_getglobal_gettable)

    VM_HANDLER(OP_CALL, vm_op_call)
    VM_HANDLER(OP_TAILCALL, vm_op_tailcall)

    VM_CASE(OP_RETURN) {
        int rc = vm_op_return(this, f, inst, base_depth);
        if (rc < 0) return -1;
        if (rc == 1) return 0;
        VM_NEXT();
    }

    VM_HANDLER(OP_CLOSURE, vm_op_closure)
    VM_HANDLER(OP_CLOSE, vm_op_close)
    VM_HANDLER(OP_TFORLOOP, vm_op_tforloop)
    VM_HANDLER(OP_NEWENUM, vm_op_newenum)
    VM_HANDLER(OP_ENUMVAL, vm_op_enumval)
    VM_HANDLER(OP_GETENUM, vm_op_getenum)
    VM_HANDLER(OP_IMPORT, vm_op_import)
    VM_HANDLER(OP_PRAGMA, vm_op_pragma)
    VM_HANDLER(OP_NOP, vm_op_nop)

    VM_DEFAULT()
        runtimeError("Unknown opcode %d", DECODE_OP(inst));
        return -1;

#if !defined(__GNUC__) && !defined(__clang__)
    VM_END_DISPATCH()
#endif

    #undef VM_HANDLER
    #undef VM_DISPATCH
    #undef VM_CASE
    #undef VM_NEXT
    #undef VM_DEFAULT
#if !defined(__GNUC__) && !defined(__clang__)
    #undef VM_END_DISPATCH
#endif
}
