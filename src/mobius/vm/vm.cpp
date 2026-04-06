#include "vm/vm.h"
#include "state/mobius_state.h"
#include "data/table.h"
#include "data/array.h"
#include "data/array_slice.h"
#include "data/enum.h"
#include "data/function.h"
#include "data/future.h"
#include "data/metamethods.h"
#include "plugin/module_registry.h"
#include "internal/string_intern.h"
#include "fiber/job_system.h"

#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <cstdlib>
#include <cmath>
#include <new>
#include <mutex>
#include <thread>
#include <time.h>

static uint64_t get_time_ns_vm() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

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
// Constructor / Destructor
// ============================================================================

thread_local MobiusVM* MobiusVM::t_current_vm = nullptr;

MobiusVM::MobiusVM(MobiusState* state)
    : state_(state), metrics_(&state->metrics()),
      strict_mode_(state->config().strict_mode),
      warn_on_conversion_(state->config().warn_on_conversion),
      override_behavior_(state->config().override_behavior),
      native_ctx_(nullptr), last_error_(nullptr),
      source_code_(nullptr), exec_context_(nullptr),
      register_capacity_(0),
      call_depth_(0), call_stack_capacity_(VM_INITIAL_CALL_STACK)
{
    registers_.reserve(VM_INITIAL_REGISTERS);
    type_tags_.reserve(VM_INITIAL_REGISTERS);
    register_capacity_ = (int)registers_.size();
    call_stack_ = new CallInfo[VM_INITIAL_CALL_STACK];
    call_stack_[0].initUpvalues();

    exec_context_ = new ExecutionContext(state, state->config().max_call_depth);
}

MobiusVM::~MobiusVM() {
    delete[] call_stack_;
    delete exec_context_;
    if (last_error_) {
        free_internal_error(last_error_);
    }
}

void MobiusVM::growCallStack() {
    size_t new_cap = call_stack_capacity_ * 2;
    CallInfo* new_stack = new CallInfo[new_cap];
    for (size_t i = 0; i <= call_depth_; i++) {
        CallInfo& src = call_stack_[i];
        CallInfo& dst = new_stack[i];
        dst.proto = src.proto;
        dst.ip = src.ip;
        dst.base = src.base;
        dst.nresults = src.nresults;
        if (src.upvalues != src.inline_upvals) {
            // Heap-allocated: transfer ownership
            dst.upvalues = src.upvalues;
            dst.upvalue_count = src.upvalue_count;
            dst.upvalue_capacity = src.upvalue_capacity;
            src.upvalues = src.inline_upvals;
            src.upvalue_count = 0;
        } else {
            // Inline: copy elements, fix pointer
            for (int j = 0; j < src.upvalue_count; j++)
                dst.inline_upvals[j] = src.inline_upvals[j];
            dst.upvalues = dst.inline_upvals;
            dst.upvalue_count = src.upvalue_count;
            dst.upvalue_capacity = CALLINFO_INLINE_UPVALS;
        }
    }
    delete[] call_stack_;
    call_stack_ = new_stack;
    call_stack_capacity_ = new_cap;
    if (call_depth_ > metrics_->peak_call_depth)
        metrics_->peak_call_depth = call_depth_;
}

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
    const char* source = nullptr;
    const std::string& src = callStackTop().proto->source;
    if (!src.empty()) source = src.c_str();
    state_->setError(MOBIUS_ERROR_RUNTIME, buf, nullptr, line, 0, nullptr, source);
}

int MobiusVM::currentLine() const {
    const CallInfo& ci = callStackTop();
    int pc = (int)(ci.ip - ci.proto->code.data()) - 1;
    if (pc >= 0 && pc < (int)ci.proto->line_info.size())
        return ci.proto->line_info[pc];
    return 0;
}

// ============================================================================
// Public entry point
// ============================================================================

int MobiusVM::execute(Prototype* proto) {
    ensureRegisters(proto->num_registers + 256);

    size_t depth_before = callStackSize();
    callStackPush(proto, proto->code.data(), 0, 0);

    MobiusVM* prev_vm = t_current_vm;
    t_current_vm = this;

    uint64_t t0 = get_time_ns_vm();
    int rc = run(depth_before);
    metrics_->total_execution_time_ns += get_time_ns_vm() - t0;

    t_current_vm = prev_vm;

    if (callStackSize() > depth_before) {
        callStackPop();
    }
    return rc;
}

// ============================================================================
// Native function bridge
// ============================================================================

int MobiusVM::callNative(MobiusCFunction func, int func_reg, int nargs, int nresults) {
    int caller_base = callStackTop().base;
    int args_base = caller_base + func_reg + 1;

    int needed = args_base + nargs + 16;
    ensureRegisters(needed);

    NativeCallContext nctx;
    nctx.registers = registers_.data();
    nctx.base      = args_base;
    nctx.top       = args_base + nargs;
    nctx.capacity  = (int)registers_.size();
    native_ctx_ = &nctx;

    int rc = func(state_, nargs);

    native_ctx_ = nullptr;

    if (rc < 0) return -1;

    int dest = caller_base + func_reg;
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

    if (func_val.type == VAL_TABLE && func_val.as.table) {
        Value call_mm = func_val.as.table->getMetamethod(state_->metamethods()->call());
        if (call_mm.type == VAL_NATIVE_FUNCTION) {
            registers_[caller.base + func_reg] = call_mm;
            return callNative(call_mm.as.native_function, func_reg, nargs, nresults);
        }
        if (call_mm.type == VAL_FUNCTION && call_mm.as.function) {
            func_val = call_mm;
        } else {
            runtimeError("Attempt to call a table value (no __call metamethod)");
            return -1;
        }
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
    ensureRegisters(needed);

    std::fill(type_tags_.begin() + child_base,
              type_tags_.begin() + child_base + child->num_registers, VAL_UNKNOWN);

    CallInfo& new_ci = callStackPush(child, child->code.data(), child_base, nresults);
    if (mf->upvalues && mf->upvalue_count > 0) {
        new_ci.setUpvaluesFrom(mf->upvalues, mf->upvalue_count);
    }

    return 1;
}

// ============================================================================
// Upvalue management
// ============================================================================

void MobiusVM::closeUpvalues(CallInfo& ci, int from_reg) {
    for (int i = 0; i < ci.upvalue_count; i++) {
        Upvalue* uv = ci.upvalues[i];
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
        int caller_base = callStackTop().base;
        int caller_regs = callStackTop().proto->num_registers;
        int scratch = caller_base + caller_regs;

        int needed = scratch + 4;
        ensureRegisters(needed);

        registers_[scratch]     = lhs;
        registers_[scratch + 1] = rhs;

        NativeCallContext nctx;
        nctx.registers = registers_.data();
        nctx.base      = scratch;
        nctx.top       = scratch + 2;
        nctx.capacity  = (int)registers_.size();
        native_ctx_ = &nctx;

        int rc = method.as.native_function(state_, 2);
        native_ctx_ = nullptr;

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

        int caller_base = callStackTop().base;
        int caller_num_regs = callStackTop().proto->num_registers;
        int scratch = caller_base + caller_num_regs;

        Prototype* child = mf->proto;
        ensureRegisters(scratch + 3 + child->num_registers + 16);

        registers_[scratch]     = method;
        registers_[scratch + 1] = lhs;
        registers_[scratch + 2] = rhs;

        int child_base = scratch + 1;
        std::fill(type_tags_.begin() + child_base,
                  type_tags_.begin() + child_base + child->num_registers, VAL_UNKNOWN);
        size_t stop_depth = callStackSize();
        callStackPush(child, child->code.data(), child_base, 2);

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
    ValueType*  tags;
};

MOBIUS_FORCEINLINE void MobiusVM::refreshFrame(VMFrame& f) {
    f.ci = &callStackTop();
    f.proto = f.ci->proto;
    f.base = f.ci->base;
    f.regs = registers_.data() + f.base;
    f.tags = type_tags_.data() + f.base;
    f.ip = f.ci->ip;
}

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

// Sync IP for error reporting and invoke runtimeError
#define VM_ERROR(vm, f, ...) do { f.ci->ip = f.ip; vm->runtimeError(__VA_ARGS__); } while(0)

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
    const Value& src = RB(inst);
    if (MOBIUS_LIKELY(src.type < VAL_STRING)) {
        Value& dst = RA(inst);
        if (MOBIUS_LIKELY(dst.type < VAL_STRING)) {
            dst.rawCopyFrom(src);
        } else {
            dst = src;
        }
    } else {
        RA(inst) = src;
    }
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
    for (int i = a; i <= a + b; i++) {
        f.regs[i] = Value();
        f.tags[i] = VAL_UNKNOWN;
    }
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
        VM_ERROR(vm, f, "Undefined variable '%s'", vm->state_->globalSlotName(slot));
        return -1;
    }
    RA(inst) = gv;
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_setglobal(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    int slot = DECODE_Bx(inst);
    Value& gv = vm->state_->globalSlot(slot);
    if (MOBIUS_UNLIKELY(gv.flags & VAL_FLAG_READONLY)) {
        VM_ERROR(vm, f, "Cannot assign to read-only variable '%s'", vm->state_->globalSlotName(slot));
        return -1;
    }
    gv = RA(inst);
    gv.flags |= VAL_FLAG_DEFINED;
    if (gv.type == VAL_ENUM && gv.aux == -1) {
        gv.flags |= VAL_FLAG_READONLY;
    }
    return 0;
}

// ---- Upvalues ----

MOBIUS_FORCEINLINE static int vm_op_getupval(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    (void)vm;
    int b = DECODE_B(inst);
    if (b < f.ci->upvalue_count && f.ci->upvalues[b]) {
        RA(inst) = *f.ci->upvalues[b]->location;
    } else {
        RA(inst) = Value();
    }
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_setupval(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    (void)vm;
    int b = DECODE_B(inst);
    if (b < f.ci->upvalue_count && f.ci->upvalues[b]) {
        *f.ci->upvalues[b]->location = RA(inst);
    }
    return 0;
}

// ---- Tables and arrays ----

MOBIUS_FORCEINLINE static int vm_op_newtable(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    Table* tbl = new (std::nothrow) Table(vm->state_, DECODE_C(inst));
    if (!tbl) { VM_ERROR(vm, f, "Failed to allocate table"); return -1; }
    RA(inst) = make_table_value(tbl);
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_newarray(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    ArrayValue* arr = new (std::nothrow) ArrayValue(DECODE_B(inst));
    if (!arr) { VM_ERROR(vm, f, "Failed to allocate array"); return -1; }
    RA(inst) = make_array_value(arr);
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_index_get(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    const Value& obj = RB(inst);
    const Value& key = RKC(inst);
    if (MOBIUS_LIKELY(obj.type == VAL_ARRAY && obj.as.array)) {
        ArrayValue* arr = obj.as.array;
        int64_t idx = MobiusVM::vm_extract_int64(key);
        if (MOBIUS_LIKELY(idx >= 0 && idx < (int64_t)arr->length())) {
            RA(inst) = arr->isShared() ? arr->get((size_t)idx) : arr->unsafeGet((size_t)idx);
        } else {
            RA(inst) = Value();
        }
    } else if (obj.type == VAL_TABLE && obj.as.table) {
        if (MOBIUS_LIKELY(key.type == VAL_STRING))
            RA(inst) = obj.as.table->getByString(key.as.string);
        else
            RA(inst) = obj.as.table->get(key);
    } else if (obj.type == VAL_ARRAY_SLICE && obj.as.array_slice) {
        int64_t idx = MobiusVM::vm_extract_int64(key);
        if (MOBIUS_LIKELY(idx >= 0 && idx < (int64_t)obj.as.array_slice->length())) {
            RA(inst) = obj.as.array_slice->get((size_t)idx);
        } else {
            RA(inst) = Value();
        }
    } else if (obj.type == VAL_STRING && obj.as.string) {
        if (key.type == VAL_INT64) {
            int64_t idx = MobiusVM::vm_extract_int64(key);
            MobiusString* s = obj.as.string;
            if (idx >= 0 && idx < (int64_t)s->length) {
                char buf[2] = { s->data[idx], '\0' };
                RA(inst) = make_string_value(vm->state_->stringPool()->intern(buf, 1));
            } else {
                RA(inst) = Value();
            }
        } else {
            VM_ERROR(vm, f, "String index must be an integer");
            return -1;
        }
    } else {
        VM_ERROR(vm, f, "Attempt to index a %s value", value_type_name(obj.type));
        return -1;
    }
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_index_set(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    Value& obj = RA(inst);
    const Value& key = RKB(inst);
    const Value& val = RKC(inst);
    if (MOBIUS_LIKELY(obj.type == VAL_ARRAY && obj.as.array)) {
        int64_t idx = MobiusVM::vm_extract_int64(key);
        if (MOBIUS_LIKELY(idx >= 0)) {
            ArrayValue* arr = obj.as.array;
            while ((int64_t)arr->length() <= idx)
                arr->push(Value());
            arr->set((size_t)idx, val);
        }
    } else if (obj.type == VAL_TABLE && obj.as.table) {
        if (MOBIUS_LIKELY(key.type == VAL_STRING))
            obj.as.table->setByString(key.as.string, val);
        else
            obj.as.table->set(key, val);
    } else if (obj.type == VAL_ARRAY_SLICE && obj.as.array_slice) {
        int64_t idx = MobiusVM::vm_extract_int64(key);
        if (idx >= 0 && idx < (int64_t)obj.as.array_slice->length()) {
            obj.as.array_slice->set((size_t)idx, val);
        } else {
            VM_ERROR(vm, f, "Array slice index %ld out of range [0, %zu)",
                             (long)idx, obj.as.array_slice->length());
            return -1;
        }
    } else {
        VM_ERROR(vm, f, "Attempt to index a %s value", value_type_name(obj.type));
        return -1;
    }
    return 0;
}

// ---- Method self (OP_SELF) ----
MOBIUS_FORCEINLINE static int vm_op_self(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    int a = DECODE_A(inst);
    const Value& obj = RB(inst);
    const Value& key = RKC(inst);

    f.regs[a + 1] = obj;

    if (obj.type == VAL_TABLE && obj.as.table) {
        Value method;
        if (MOBIUS_LIKELY(key.type == VAL_STRING))
            method = obj.as.table->getByString(key.as.string);
        else
            method = obj.as.table->get(key);

        if (method.type != VAL_NIL) {
            f.regs[a] = method;
            return 0;
        }
    }

    Table* mt = vm->state_->typeMetatable(obj.type);
    if (mt && key.type == VAL_STRING) {
        const Value& method = mt->getByString(key.as.string);
        if (method.type != VAL_NIL) {
            f.regs[a] = method;
            return 0;
        }
    }

    VM_ERROR(vm, f, "Attempt to call method on a %s value", value_type_name(obj.type));
    return -1;
}

// ---- Array fast-path ----

MOBIUS_FORCEINLINE static int vm_op_array_push(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    Value& arr_val = RA(inst);
    const Value& val = RB(inst);
    if (MOBIUS_LIKELY(arr_val.type == VAL_ARRAY && arr_val.as.array)) {
        arr_val.as.array->push(val);
    } else {
        VM_ERROR(vm, f, "OP_ARRAY_PUSH applied to non-array (%s)", value_type_name(arr_val.type));
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
        if (rc == 0) { VM_ERROR(vm, f, "Cannot add: no __add metamethod on table"); return -1; }
        RA(inst) = out;
    } else {
        VM_ERROR(vm, f, "Cannot add %s and %s", value_type_name(lhs.type), value_type_name(rhs.type));
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
        if (rc == 0) { VM_ERROR(vm, f, "Cannot subtract: no __sub metamethod on table"); return -1; }
        RA(inst) = out;
    } else {
        VM_ERROR(vm, f, "Cannot subtract %s and %s", value_type_name(lhs.type), value_type_name(rhs.type));
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
        if (rc == 0) { VM_ERROR(vm, f, "Cannot multiply: no __mul metamethod on table"); return -1; }
        RA(inst) = out;
    } else {
        VM_ERROR(vm, f, "Cannot multiply %s and %s", value_type_name(lhs.type), value_type_name(rhs.type));
        return -1;
    }
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_div(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    const Value& lhs = RKB(inst);
    const Value& rhs = RKC(inst);
    if (MOBIUS_LIKELY(lhs.type == VAL_INT64 && rhs.type == VAL_INT64)) {
        int64_t rv = rhs.as.i64;
        if (MOBIUS_UNLIKELY(rv == 0)) { VM_ERROR(vm, f, "Division by zero"); return -1; }
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
        if (rc == 0) { VM_ERROR(vm, f, "Cannot divide: no __div metamethod on table"); return -1; }
        RA(inst) = out;
    } else {
        double lv = MobiusVM::vm_extract_double(lhs);
        double rv = MobiusVM::vm_extract_double(rhs);
        if (rv == 0.0) { VM_ERROR(vm, f, "Division by zero"); return -1; }
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
        if (MOBIUS_UNLIKELY(rv == 0)) { VM_ERROR(vm, f, "Modulo by zero"); return -1; }
        Value& dst = RA(inst);
        dst.as.i64 = lhs.as.i64 % rv;
        dst.type = VAL_INT64; dst.flags = 0;
    } else if ((lhs.type == VAL_INT64 || lhs.type == VAL_UINT64) &&
               (rhs.type == VAL_INT64 || rhs.type == VAL_UINT64)) {
        if (MobiusVM::vm_use_unsigned(lhs, rhs)) {
            uint64_t lv = MobiusVM::vm_extract_uint64(lhs);
            uint64_t rv = MobiusVM::vm_extract_uint64(rhs);
            if (rv == 0) { VM_ERROR(vm, f, "Modulo by zero"); return -1; }
            RA(inst) = make_uint64_value(lv % rv);
        } else {
            int64_t lv = MobiusVM::vm_extract_int64(lhs);
            int64_t rv = MobiusVM::vm_extract_int64(rhs);
            if (rv == 0) { VM_ERROR(vm, f, "Modulo by zero"); return -1; }
            RA(inst) = make_int64_value(lv % rv);
        }
    } else if (lhs.type == VAL_FLOAT64 && rhs.type == VAL_FLOAT64) {
        double rv = rhs.as.double_val;
        if (rv == 0.0) { VM_ERROR(vm, f, "Modulo by zero"); return -1; }
        Value& dst = RA(inst);
        dst.as.double_val = fmod(lhs.as.double_val, rv);
        dst.type = VAL_FLOAT64; dst.flags = 0;
    } else if (lhs.type == VAL_FLOAT64 || rhs.type == VAL_FLOAT64) {
        double lv = MobiusVM::vm_extract_double(lhs);
        double rv = MobiusVM::vm_extract_double(rhs);
        if (rv == 0.0) { VM_ERROR(vm, f, "Modulo by zero"); return -1; }
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
        if (rc == 0) { VM_ERROR(vm, f, "Cannot modulo: no __mod metamethod on table"); return -1; }
        RA(inst) = out;
    } else {
        VM_ERROR(vm, f, "Cannot modulo %s and %s", value_type_name(lhs.type), value_type_name(rhs.type));
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
    } else if (val.type == VAL_TABLE) {
        Value out;
        f.ci->ip = f.ip;
        int rc = vm->callMetamethod(val, vm->state_->metamethods()->unm(), val, val, out);
        vm->refreshFrame(f);
        if (rc < 0) return -1;
        if (rc == 0) { VM_ERROR(vm, f, "Attempt to negate a table value: no __unm metamethod"); return -1; }
        RA(inst) = out;
    } else {
        VM_ERROR(vm, f, "Attempt to negate a %s value", value_type_name(val.type));
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
        if (lhs.type == VAL_TABLE || rhs.type == VAL_TABLE) {
            const Value& tbl = (lhs.type == VAL_TABLE) ? lhs : rhs;
            MobiusString* mm = nullptr;
            const char* name = nullptr;
            switch (op) {
                case '&': mm = vm->state_->metamethods()->band(); name = "__band"; break;
                case '|': mm = vm->state_->metamethods()->bor();  name = "__bor";  break;
                case '^': mm = vm->state_->metamethods()->bxor(); name = "__bxor"; break;
                case '<': mm = vm->state_->metamethods()->shl();  name = "__shl";  break;
                case '>': mm = vm->state_->metamethods()->shr();  name = "__shr";  break;
            }
            if (mm) {
                Value out;
                f.ci->ip = f.ip;
                int rc = vm->callMetamethod(tbl, mm, lhs, rhs, out);
                vm->refreshFrame(f);
                if (rc < 0) return -1;
                if (rc == 1) { RA(inst) = out; return 0; }
            }
            VM_ERROR(vm, f, "Cannot apply bitwise op: no %s metamethod on table", name ? name : "?");
            return -1;
        }
        VM_ERROR(vm, f, "Bitwise operations require integer operands");
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
    } else if (val.type == VAL_TABLE) {
        Value out;
        f.ci->ip = f.ip;
        int rc = vm->callMetamethod(val, vm->state_->metamethods()->bnot(), val, val, out);
        vm->refreshFrame(f);
        if (rc < 0) return -1;
        if (rc == 0) { VM_ERROR(vm, f, "Bitwise NOT on table: no __bnot metamethod"); return -1; }
        RA(inst) = out;
    } else {
        VM_ERROR(vm, f, "Bitwise NOT requires an integer operand");
        return -1;
    }
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
        if (rc == 0) { VM_ERROR(vm, f, "Cannot compare: no __lt metamethod on table"); return -1; }
        lt = is_truthy(out);
    } else {
        VM_ERROR(vm, f, "Cannot compare incompatible types");
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
        if (rc == 0) { VM_ERROR(vm, f, "Cannot compare: no __le metamethod on table"); return -1; }
        le = is_truthy(out);
    } else {
        VM_ERROR(vm, f, "Cannot compare incompatible types");
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
    else { VM_ERROR(vm, f, err_msg " requires numeric operand"); return -1; } \
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
    if (MOBIUS_UNLIKELY(rv == 0)) { VM_ERROR(vm, f, "Modulo by zero"); return -1; }
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
        if (is_mod && MOBIUS_UNLIKELY(k == 0)) { VM_ERROR(vm, f, #name ": division/modulo by zero"); return -1; } \
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
        if (is_mod && kv == 0.0) { VM_ERROR(vm, f, #name ": division/modulo by zero"); return -1; } \
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
        if (MOBIUS_UNLIKELY(k == 0)) { VM_ERROR(vm, f, "Division by zero"); return -1; }
        dst.as.i64 = src.as.i64 / k;
        dst.type = VAL_INT64; dst.flags = 0;
    } else {
        double sv = MobiusVM::vm_extract_double(src);
        double kv;
        if (tag == VAL_INT64) { int64_t k; memcpy(&k, &raw, 8); kv = (double)k; }
        else { memcpy(&kv, &raw, 8); }
        if (kv == 0.0) { VM_ERROR(vm, f, "Division by zero"); return -1; }
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
        if (MOBIUS_UNLIKELY(k == 0)) { VM_ERROR(vm, f, "Modulo by zero"); return -1; }
        dst.as.i64 = src.as.i64 % k;
        dst.type = VAL_INT64; dst.flags = 0;
    } else {
        double sv = MobiusVM::vm_extract_double(src);
        double kv;
        if (tag == VAL_INT64) { int64_t k; memcpy(&k, &raw, 8); kv = (double)k; }
        else { memcpy(&kv, &raw, 8); }
        if (kv == 0.0) { VM_ERROR(vm, f, "Modulo by zero"); return -1; }
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
    if (check_zero && MOBIUS_UNLIKELY(imm == 0)) { VM_ERROR(vm, f, "Division by zero"); return -1; } \
    Value& val = f.regs[a]; \
    if (MOBIUS_LIKELY(val.type == VAL_INT64)) { val.as.i64 op_char##= imm; } \
    else if (val.type == VAL_UINT64) val.as.u64 op_char##= (uint64_t)(int64_t)imm; \
    else if (val.type == VAL_FLOAT64) val.as.double_val op_char##= imm; \
    else { VM_ERROR(vm, f, err_msg " requires numeric operand"); return -1; } \
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
    if (MOBIUS_UNLIKELY(imm == 0)) { VM_ERROR(vm, f, "Modulo by zero"); return -1; }
    Value& val = f.regs[a];
    if (MOBIUS_LIKELY(val.type == VAL_INT64)) { val.as.i64 %= imm; }
    else if (val.type == VAL_UINT64) val.as.u64 %= (uint64_t)(int64_t)imm;
    else if (val.type == VAL_FLOAT64) val.as.double_val = fmod(val.as.double_val, (double)imm);
    else { VM_ERROR(vm, f, "MODI requires numeric operand"); return -1; }
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
    if (val.type != VAL_INT64) { VM_ERROR(vm, f, "Increment requires an integer operand"); return -1; }
    bool success;
    RA(inst) = increment_integer(val, true, &success);
    if (!success) { VM_ERROR(vm, f, "Failed to increment value"); return -1; }
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_dec(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    const Value& val = RB(inst);
    if (val.type != VAL_INT64) { VM_ERROR(vm, f, "Decrement requires an integer operand"); return -1; }
    bool success;
    RA(inst) = increment_integer(val, false, &success);
    if (!success) { VM_ERROR(vm, f, "Failed to decrement value"); return -1; }
    return 0;
}

// ---- Type checking ----

MOBIUS_FORCEINLINE static int vm_op_typecheck(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    NumberType target = (NumberType)DECODE_B(inst);
    TypeCheckConfig tc = {
        vm->strict_mode_,
        vm->warn_on_conversion_
    };
    TypeConversionResult conv = validate_and_convert_value(RA(inst), target, true, tc);
    if (!conv.success) {
        VM_ERROR(vm, f, "%s", conv.error_message ? conv.error_message : "Type validation failed");
        free(conv.error_message);
        return -1;
    }
    if (conv.was_converted && vm->warn_on_conversion_) {
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

// ---- Type locking ----

MOBIUS_FORCEINLINE static int vm_op_typelock(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    (void)vm;
    uint8_t a = DECODE_A(inst);
    const Value& val = f.regs[a];
    if (val.type != VAL_NIL) {
        f.tags[a] = val.type;
    } else {
        f.tags[a] = VAL_UNKNOWN;
    }
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_typecheck_locked(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    uint8_t a = DECODE_A(inst);
    const Value& val = f.regs[a];
    ValueType tag = f.tags[a];
    if (tag == VAL_UNKNOWN) {
        if (val.type != VAL_NIL) {
            f.tags[a] = val.type;
        }
    } else if (val.type != VAL_NIL && val.type != tag) {
        VM_ERROR(vm, f, "Type error: cannot assign %s to variable of type %s",
                 value_type_name(val.type), value_type_name(tag));
        return -1;
    }
    return 0;
}

// ---- Typed comparisons ----

MOBIUS_FORCEINLINE static int vm_op_lt_ii(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    (void)vm;
    int a = DECODE_A(inst);
    bool result = RKB(inst).as.i64 < RKC(inst).as.i64;
    if (result != (a != 0)) f.ip++;
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_le_ii(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    (void)vm;
    int a = DECODE_A(inst);
    bool result = RKB(inst).as.i64 <= RKC(inst).as.i64;
    if (result != (a != 0)) f.ip++;
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_eq_ii(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    (void)vm;
    int a = DECODE_A(inst);
    bool result = RKB(inst).as.i64 == RKC(inst).as.i64;
    if (result != (a != 0)) f.ip++;
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_lt_ff(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    (void)vm;
    int a = DECODE_A(inst);
    bool result = RKB(inst).as.double_val < RKC(inst).as.double_val;
    if (result != (a != 0)) f.ip++;
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_le_ff(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    (void)vm;
    int a = DECODE_A(inst);
    bool result = RKB(inst).as.double_val <= RKC(inst).as.double_val;
    if (result != (a != 0)) f.ip++;
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_eq_ff(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    (void)vm;
    int a = DECODE_A(inst);
    bool result = RKB(inst).as.double_val == RKC(inst).as.double_val;
    if (result != (a != 0)) f.ip++;
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
    } else if (val.type == VAL_ARRAY_SLICE && val.as.array_slice) {
        RA(inst) = make_int64_value((int64_t)val.as.array_slice->length());
    } else {
        VM_ERROR(vm, f, "Attempt to get length of a %s value", value_type_name(val.type));
        return -1;
    }
    return 0;
}

// ---- For loops ----

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
    else { VM_ERROR(vm, f, "ADDI requires numeric operand"); return -1; }
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_getglobal_index_get(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    int a = DECODE_A(inst);
    int slot = DECODE_Bx(inst);
    const Value& gv = vm->state_->globalSlot(slot);
    if (MOBIUS_UNLIKELY(!(gv.flags & VAL_FLAG_DEFINED))) {
        VM_ERROR(vm, f, "Undefined variable '%s'", vm->state_->globalSlotName(slot));
        return -1;
    }
    f.regs[a] = gv;
    uint32_t inst2 = *f.ip++;
    uint8_t c2 = DECODE_C(inst2);
    const Value& key = IS_CONSTANT(c2)
        ? f.ci->proto->constants[RK_AS_CONSTANT(c2)]
        : f.regs[c2];
    const Value& obj = f.regs[a];
    if (MOBIUS_LIKELY(obj.type == VAL_ARRAY && obj.as.array)) {
        ArrayValue* arr = obj.as.array;
        int64_t idx = MobiusVM::vm_extract_int64(key);
        if (MOBIUS_LIKELY(idx >= 0 && idx < (int64_t)arr->length()))
            f.regs[a] = arr->isShared() ? arr->get((size_t)idx) : arr->unsafeGet((size_t)idx);
        else
            f.regs[a] = Value();
    } else if (obj.type == VAL_TABLE && obj.as.table) {
        if (MOBIUS_LIKELY(key.type == VAL_STRING))
            f.regs[a] = obj.as.table->getByString(key.as.string);
        else
            f.regs[a] = obj.as.table->get(key);
    } else if (obj.type == VAL_STRING && obj.as.string) {
        if (key.type == VAL_INT64) {
            int64_t idx = MobiusVM::vm_extract_int64(key);
            MobiusString* s = obj.as.string;
            if (idx >= 0 && idx < (int64_t)s->length) {
                char buf[2] = { s->data[idx], '\0' };
                f.regs[a] = make_string_value(vm->state_->stringPool()->intern(buf, 1));
            } else {
                f.regs[a] = Value();
            }
        } else {
            VM_ERROR(vm, f, "String index must be an integer");
            return -1;
        }
    } else {
        VM_ERROR(vm, f, "Attempt to index a %s value", value_type_name(obj.type));
        return -1;
    }
    return 0;
}

// ---- Call / Tailcall ----
MOBIUS_FORCEINLINE static int vm_op_call(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    int a = DECODE_A(inst);
    int b = DECODE_B(inst);
    int c = DECODE_C(inst);
    int nargs = b - 1;
    f.ci->ip = f.ip;

    Value& func_val = f.regs[a];

    if (MOBIUS_LIKELY(func_val.type == VAL_FUNCTION && func_val.as.function)) {
        MobiusFunction* mf = func_val.as.function;
        if (MOBIUS_UNLIKELY(!mf->proto)) {
            VM_ERROR(vm, f, "Function '%s' has no bytecode prototype",
                             mf->name ? mf->name->data : "anonymous");
            return -1;
        }
        if (MOBIUS_UNLIKELY((int)mf->param_count != nargs)) {
            VM_ERROR(vm, f, "Function '%s' expects %zu arguments but got %d",
                             mf->name ? mf->name->data : "anonymous", mf->param_count, nargs);
            return -1;
        }

        Prototype* child = mf->proto;
        int child_base = f.ci->base + a + 1;
        vm->ensureRegisters(child_base + child->num_registers + 16);

        std::fill(vm->type_tags_.begin() + child_base,
                  vm->type_tags_.begin() + child_base + child->num_registers, VAL_UNKNOWN);

        CallInfo& new_ci = vm->callStackPush(child, child->code.data(), child_base, c);
        if (mf->upvalues && mf->upvalue_count > 0) {
            new_ci.setUpvaluesFrom(mf->upvalues, mf->upvalue_count);
        }

        vm->refreshFrame(f);
        return 0;
    }

    int rc = vm->callFunction(*f.ci, a, nargs, c);
    if (rc < 0) return -1;
    if (rc == 1) {
        vm->refreshFrame(f);
    } else {
        f.regs = vm->registers_.data() + f.base;
        f.ci = &vm->callStackTop();
    }
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_getglobal_call(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    int a = DECODE_A(inst);
    int slot = DECODE_Bx(inst);
    const Value& gv = vm->state_->globalSlot(slot);
    if (MOBIUS_UNLIKELY(!(gv.flags & VAL_FLAG_DEFINED))) {
        VM_ERROR(vm, f, "Undefined variable '%s'", vm->state_->globalSlotName(slot));
        return -1;
    }
    f.regs[a] = gv;
    uint32_t inst2 = *f.ip++;
    return vm_op_call(vm, f, inst2);
}

MOBIUS_FORCEINLINE static int vm_op_tailcall(MobiusVM* vm, VMFrame& f, uint32_t inst, size_t base_depth) {
    int a = DECODE_A(inst);
    int b = DECODE_B(inst);
    int nargs = b - 1;

    Value func_val = f.regs[a];

    if (func_val.type == VAL_NATIVE_FUNCTION) {
        f.ci->ip = f.ip;
        int rc = vm->callFunction(*f.ci, a, nargs, f.ci->nresults);
        if (rc < 0) return -1;

        if (vm->callStackSize() <= base_depth) return 1;
        if (MOBIUS_UNLIKELY(f.ci->upvalue_count > 0)) vm->closeUpvalues(*f.ci, 0);
        int ret_base = f.ci->base;
        int ret_nresults = f.ci->nresults;
        vm->callStackPop();
        int func_reg_abs = ret_base - 1;
        if (ret_nresults != 0) {
            int to_copy = ret_nresults - 1;
            for (int i = 0; i < to_copy; i++) {
                vm->registers_[func_reg_abs + i] = vm->registers_[ret_base + a + i];
            }
        }
        if (vm->callStackSize() <= base_depth) return 1;
        vm->refreshFrame(f);
        return 0;
    }

    if (func_val.type != VAL_FUNCTION || !func_val.as.function) {
        VM_ERROR(vm, f, "Attempt to call a non-function value (type: %s)",
                         value_type_name(func_val.type));
        return -1;
    }

    MobiusFunction* mf = func_val.as.function;
    if (!mf->proto) {
        VM_ERROR(vm, f, "Function '%s' has no bytecode prototype",
                         mf->name ? mf->name->data : "anonymous");
        return -1;
    }
    if ((int)mf->param_count != nargs) {
        VM_ERROR(vm, f, "Function '%s' expects %zu arguments but got %d",
                         mf->name ? mf->name->data : "anonymous", mf->param_count, nargs);
        return -1;
    }

    Prototype* child = mf->proto;

    Value arg_buf[256];
    Value* args = (nargs <= 256) ? arg_buf : new Value[nargs];
    int src_base = f.ci->base + a + 1;
    for (int i = 0; i < nargs; i++) {
        args[i] = vm->registers_[src_base + i];
    }

    if (MOBIUS_UNLIKELY(f.ci->upvalue_count > 0)) vm->closeUpvalues(*f.ci, 0);

    for (int i = 0; i < nargs; i++) {
        vm->registers_[f.ci->base + i] = args[i];
    }
    if (args != arg_buf) delete[] args;

    vm->ensureRegisters(f.ci->base + child->num_registers + 16);
    std::fill(vm->type_tags_.begin() + f.ci->base,
              vm->type_tags_.begin() + f.ci->base + child->num_registers, VAL_UNKNOWN);
    for (int i = nargs; i < child->num_registers; i++) {
        vm->registers_[f.ci->base + i] = Value();
    }

    f.ci->proto = child;
    f.ci->ip = child->code.data();
    f.ci->clearUpvalues();
    if (mf->upvalues && mf->upvalue_count > 0) {
        f.ci->setUpvaluesFrom(mf->upvalues, mf->upvalue_count);
    }

    f.proto = child;
    f.ip = f.ci->ip;
    f.regs = vm->registers_.data() + f.base;

    return 0;
}

// ---- Return ----
// Returns 1 = exit run(), 0 = continue, -1 = error
MOBIUS_FORCEINLINE static int vm_op_return(MobiusVM* vm, VMFrame& f, uint32_t inst, size_t base_depth) {
    int a = DECODE_A(inst);
    int b = DECODE_B(inst);
    if (vm->callStackSize() <= base_depth) return 1;
    int nresults_available = (b == 0) ? 0 : (b - 1);
    if (MOBIUS_UNLIKELY(f.ci->upvalue_count > 0)) vm->closeUpvalues(*f.ci, 0);
    int ret_base = f.ci->base;
    int ret_nresults = f.ci->nresults;
    vm->callStackPop();
    int func_reg_abs = ret_base - 1;
    if (ret_nresults != 0) {
        int to_copy = ret_nresults - 1;
        for (int i = 0; i < to_copy; i++) {
            if (i < nresults_available)
                vm->registers_[func_reg_abs + i] = vm->registers_[ret_base + a + i];
            else
                vm->registers_[func_reg_abs + i] = Value();
        }
    }
    if (vm->callStackSize() <= base_depth) return 1;
    vm->refreshFrame(f);
    return 0;
}

// ---- Closure ----
MOBIUS_FORCEINLINE static int vm_op_closure(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    uint16_t bx = DECODE_Bx(inst);
    if (bx >= f.proto->protos.size()) {
        VM_ERROR(vm, f, "Invalid prototype index %d", bx);
        return -1;
    }
    Prototype* child_proto = f.proto->protos[bx];
    MobiusFunction* mf = new MobiusFunction();
    mf->name = child_proto->name.empty() ? nullptr :
               vm->state_->stringPool()->intern(child_proto->name.c_str());
    mf->param_count = child_proto->num_params;
    mf->body = nullptr;
    mf->body_count = 0;
    mf->ref_count.store(1, std::memory_order_relaxed);
    mf->proto = child_proto;
    if (child_proto->num_params > 0 && !child_proto->local_vars.empty()) {
        mf->param_names = new MobiusString*[child_proto->num_params]();
        for (int i = 0; i < child_proto->num_params && i < (int)child_proto->local_vars.size(); i++) {
            mf->param_names[i] = vm->state_->stringPool()->intern(child_proto->local_vars[i].name.c_str());
        }
    } else {
        mf->param_names = nullptr;
    }
    int nupvals = (int)child_proto->upvalues.size();
    if (nupvals > 0) {
        mf->upvalues = new Upvalue*[nupvals]();
        mf->upvalue_count = nupvals;
        for (int u = 0; u < nupvals; u++) {
            const UpvalueDesc& desc = child_proto->upvalues[u];
            if (desc.in_stack) {
                Value* reg_ptr = &f.regs[desc.index];
                Upvalue* existing = nullptr;
                for (int k = 0; k < f.ci->upvalue_count; k++) {
                    Upvalue* ouv = f.ci->upvalues[k];
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
                    f.ci->pushUpvalue(uv);
                    if ((size_t)f.ci->upvalue_count > vm->metrics_->peak_upvalues)
                        vm->metrics_->peak_upvalues = (size_t)f.ci->upvalue_count;
                    mf->upvalues[u] = uv;
                }
            } else {
                if (desc.index < f.ci->upvalue_count) {
                    mf->upvalues[u] = f.ci->upvalues[desc.index];
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
    if (MOBIUS_UNLIKELY(f.ci->upvalue_count > 0)) vm->closeUpvalues(*f.ci, DECODE_A(inst));
    return 0;
}

// ---- TForLoop ----
MOBIUS_FORCEINLINE static int vm_op_tforloop(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    uint8_t a = DECODE_A(inst);
    uint8_t c = DECODE_C(inst);  // number of user variables (1 or 2)

    Value& iter_val = f.regs[a];

    if (iter_val.type == VAL_ARRAY && iter_val.as.array) {
        ArrayValue* arr = iter_val.as.array;
        int64_t idx = (f.regs[a + 2].type == VAL_NIL) ? 0 : f.regs[a + 2].as.i64 + 1;
        if (idx >= (int64_t)arr->length()) {
            return 0;
        }
        f.regs[a + 2] = make_int64_value(idx);
        if (c >= 2) {
            f.regs[a + 3] = make_int64_value(idx);
            f.regs[a + 4] = arr->get((size_t)idx);
        } else {
            f.regs[a + 3] = arr->get((size_t)idx);
        }
        f.ip++;
        return 0;
    }

    if (iter_val.type == VAL_TABLE && iter_val.as.table) {
        Table* tbl = iter_val.as.table;
        int64_t slot = (f.regs[a + 2].type == VAL_NIL) ? 0 : f.regs[a + 2].as.i64 + 1;
        const auto& entries = tbl->entries();
        const auto& tags = tbl->tags();
        size_t cap = entries.size();
        while ((size_t)slot < cap && tags[slot] == Table::TAG_EMPTY) {
            slot++;
        }
        if ((size_t)slot >= cap) {
            return 0;
        }
        f.regs[a + 2] = make_int64_value(slot);
        if (c >= 2) {
            f.regs[a + 3] = entries[slot].key;
            f.regs[a + 4] = entries[slot].value;
        } else {
            f.regs[a + 3] = entries[slot].key;
        }
        f.ip++;
        return 0;
    }

    // Function iterator path (existing behavior)
    int call_reg = a + 3;
    f.regs[call_reg]     = f.regs[a];
    f.regs[call_reg + 1] = f.regs[a + 1];
    f.regs[call_reg + 2] = f.regs[a + 2];

    f.ci->ip = f.ip;
    int rc = vm->callFunction(vm->callStackTop(), call_reg, 2, 2);
    if (rc < 0) return -1;

    if (rc > 0) {
        rc = vm->run(vm->callStackSize() - 1);
        if (rc < 0) return -1;
    }

    vm->refreshFrame(f);

    Value& result = f.regs[call_reg];
    if (result.type == VAL_NIL) {
        return 0;
    }

    f.regs[a + 2] = result;
    f.regs[a + 3] = result;

    f.ip++;
    return 0;
}

// ---- Enum ----
MOBIUS_FORCEINLINE static int vm_op_newenum(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    uint16_t bx = DECODE_Bx(inst);
    const Value& name_val = f.proto->constants[bx];
    const char* enum_name = (name_val.type == VAL_STRING && name_val.as.string)
                            ? name_val.as.string->data : "unknown";
    EnumDefinition* edef = new (std::nothrow) EnumDefinition(enum_name, NUM_INT64);
    if (!edef) { VM_ERROR(vm, f, "Failed to allocate enum"); return -1; }
    RA(inst) = Value::makeEnum(edef, -1);
    edef->release();
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_enumval(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    Value& enum_val = RA(inst);
    if (enum_val.type != VAL_ENUM || !enum_val.as.enum_def) {
        VM_ERROR(vm, f, "ENUMVAL: target is not an enum definition");
        return -1;
    }
    EnumDefinition* edef = enum_val.as.enum_def;

    uint8_t b = DECODE_B(inst);
    const Value& member_val = IS_CONSTANT(b)
        ? f.proto->constants[RK_AS_CONSTANT(b)]
        : f.regs[b];

    uint8_t c = DECODE_C(inst);
    const Value& name_const = IS_CONSTANT(c)
        ? f.proto->constants[RK_AS_CONSTANT(c)]
        : f.regs[c];
    if (name_const.type != VAL_STRING || !name_const.as.string) {
        VM_ERROR(vm, f, "ENUMVAL: member name must be a string");
        return -1;
    }
    const char* member_name = name_const.as.string->data;

    if (member_val.type == VAL_INT64) {
        edef->addMember(member_name, MobiusVM::vm_extract_int64(member_val));
    } else {
        edef->addAutoMember(member_name);
    }
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_getenum(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    const Value& enum_val = RB(inst);
    if (enum_val.type != VAL_ENUM || !enum_val.as.enum_def) {
        VM_ERROR(vm, f, "GETENUM: target is not an enum definition");
        return -1;
    }
    EnumDefinition* edef = enum_val.as.enum_def;

    uint8_t c = DECODE_C(inst);
    const Value& name_const = IS_CONSTANT(c)
        ? f.proto->constants[RK_AS_CONSTANT(c)]
        : f.regs[c];
    if (name_const.type != VAL_STRING || !name_const.as.string) {
        VM_ERROR(vm, f, "GETENUM: member name must be a string");
        return -1;
    }
    const char* member_name = name_const.as.string->data;

    const EnumMember* member = edef->findMember(member_name);
    if (!member) {
        VM_ERROR(vm, f, "Invalid enum member '%s' on enum '%s'",
                         member_name, edef->name().c_str());
        return -1;
    }
    RA(inst) = Value::makeEnum(edef, member->value);
    return 0;
}

// ---- Import ----
MOBIUS_FORCEINLINE static int vm_op_import(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    std::lock_guard<std::mutex> lock(vm->state_->importMutex());

    const Value& mod_name_val = IS_CONSTANT(DECODE_B(inst))
        ? f.ci->proto->constants[RK_AS_CONSTANT(DECODE_B(inst))]
        : f.regs[DECODE_B(inst)];
    const Value& alias_val = IS_CONSTANT(DECODE_C(inst))
        ? f.ci->proto->constants[RK_AS_CONSTANT(DECODE_C(inst))]
        : f.regs[DECODE_C(inst)];
    if (mod_name_val.type != VAL_STRING || !mod_name_val.as.string) {
        VM_ERROR(vm, f, "IMPORT: invalid module name"); return -1;
    }
    if (alias_val.type != VAL_STRING || !alias_val.as.string) {
        VM_ERROR(vm, f, "IMPORT: invalid alias"); return -1;
    }
    const char* module_name = mod_name_val.as.string->data;
    const char* alias_name  = alias_val.as.string->data;

    bool is_global = (strcmp(alias_name, "_GLOBAL") == 0);

    ModuleRegistry* registry = getGlobalRegistry();
    if (!registry) { VM_ERROR(vm, f, "Module registry not initialized"); return -1; }

    const char* caller_source = f.ci->proto->source.c_str();
    Table* mod_table = registry->resolveModule(module_name, caller_source, vm->state_);
    if (!mod_table) {
        VM_ERROR(vm, f, "Import failed - module '%s' not found", module_name);
        return -1;
    }

    // The registry owns mod_table (cached). Retain so the Values we create
    // below can safely release() when they go out of scope.
    mod_table->retain();

    if (is_global) {
        // Spread table entries into individual globals
        const auto& entries = mod_table->entries();
        const auto& tags = mod_table->tags();
        for (size_t i = 0; i < entries.size(); i++) {
            if (tags[i] == Table::TAG_EMPTY) continue;
            const Value& key = entries[i].key;
            if (key.type != VAL_STRING || !key.as.string) continue;
            Value val = entries[i].value;
            val.flags |= VAL_FLAG_DEFINED;
            int slot = vm->state_->assignGlobalSlot(key.as.string->data);
            vm->state_->globalSlot(slot) = val;
        }
    } else if (strchr(alias_name, '.') != nullptr) {
        // Dotted path: walk/create nested tables, put module table at the leaf
        char buf[256];
        strncpy(buf, alias_name, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        const char* components[32];
        int ncomps = 0;
        char* tok = strtok(buf, ".");
        while (tok && ncomps < 32) { components[ncomps++] = tok; tok = strtok(nullptr, "."); }

        Table* cur_table = nullptr;
        int ns_slot = vm->state_->findGlobalSlot(components[0]);
        bool first_exists = (ns_slot >= 0 && (vm->state_->globalSlot(ns_slot).flags & VAL_FLAG_DEFINED));
        if (first_exists && vm->state_->globalSlot(ns_slot).type == VAL_TABLE) {
            cur_table = vm->state_->globalSlot(ns_slot).as.table;
        } else if (first_exists) {
            VM_ERROR(vm, f, "Cannot create nested namespace '%s': '%s' is not a table",
                             alias_name, components[0]);
            return -1;
        } else {
            cur_table = new (std::nothrow) Table(vm->state_, 16);
            if (!cur_table) { VM_ERROR(vm, f, "Failed to create namespace table"); return -1; }
            Value tval = make_table_value(cur_table);
            tval.flags |= VAL_FLAG_DEFINED;
            int s = vm->state_->assignGlobalSlot(components[0]);
            vm->state_->globalSlot(s) = tval;
        }

        // Walk/create intermediate tables
        for (int i = 1; i < ncomps - 1; i++) {
            Value key = make_string_value_from_cstr(vm->state_, components[i]);
            Value next = cur_table->get(key);
            if (next.type == VAL_TABLE) {
                cur_table = next.as.table;
            } else {
                Table* sub = new (std::nothrow) Table(vm->state_, 16);
                if (!sub) { VM_ERROR(vm, f, "Failed to create nested namespace table"); return -1; }
                cur_table->set(key, make_table_value(sub));
                cur_table = sub;
            }
        }

        // Set the last component to point at the module table
        if (ncomps > 1) {
            Value leaf_key = make_string_value_from_cstr(vm->state_, components[ncomps - 1]);
            cur_table->set(leaf_key, make_table_value(mod_table));
        }
    } else {
        // Simple alias: bind module table to a single global
        Value tval = make_table_value(mod_table);
        tval.flags |= VAL_FLAG_DEFINED;
        int s = vm->state_->assignGlobalSlot(alias_name);
        vm->state_->globalSlot(s) = tval;
    }

    return 0;
}

// ---- Pragma ----
MOBIUS_FORCEINLINE static int vm_op_pragma(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    uint16_t bx = DECODE_Bx(inst);
    const Value& name_val = f.proto->constants[bx];
    if (name_val.type != VAL_STRING || !name_val.as.string) {
        VM_ERROR(vm, f, "PRAGMA: invalid pragma name"); return -1;
    }
    const char* pragma_name = name_val.as.string->data;
    const Value& pval = RA(inst);
    if (strcmp(pragma_name, "strict_types") == 0) {
        vm->strict_mode_ = is_truthy(pval);
    } else if (strcmp(pragma_name, "type_warnings") == 0) {
        vm->warn_on_conversion_ = is_truthy(pval);
    } else if (strcmp(pragma_name, "override_behavior") == 0) {
        if (pval.type == VAL_STRING && pval.as.string) {
            const char* v = pval.as.string->data;
            if (strcmp(v, "error") == 0)
                vm->override_behavior_ = MOBIUS_OVERRIDE_ERROR;
            else if (strcmp(v, "warn") == 0)
                vm->override_behavior_ = MOBIUS_OVERRIDE_WARN;
            else if (strcmp(v, "quiet") == 0)
                vm->override_behavior_ = MOBIUS_OVERRIDE_QUIET;
            else {
                VM_ERROR(vm, f, "Invalid value for pragma override_behavior: '%s' "
                                 "(expected 'error', 'warn', or 'quiet')", v);
                return -1;
            }
        } else {
            VM_ERROR(vm, f, "Invalid value for pragma override_behavior "
                             "(expected 'error', 'warn', or 'quiet')");
            return -1;
        }
    } else {
        VM_ERROR(vm, f, "Unknown pragma: '%s'", pragma_name);
        return -1;
    }
    return 0;
}

// ---- Error handling ----
MOBIUS_FORCEINLINE static int vm_op_try_begin(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    uint8_t a = DECODE_A(inst);
    int sbx = DECODE_sBx(inst);

    MobiusVM::TryBlock tb;
    tb.call_stack_depth = vm->callStackSize();
    tb.catch_ip = f.ip + sbx;
    tb.catch_reg = a;
    tb.base = f.base;
    vm->try_stack_.push_back(tb);
    if (vm->try_stack_.size() > vm->metrics_->peak_try_depth)
        vm->metrics_->peak_try_depth = vm->try_stack_.size();
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_try_end(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    (void)f; (void)inst;
    if (!vm->try_stack_.empty()) {
        vm->try_stack_.pop_back();
    }
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_throw(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    uint8_t a = DECODE_A(inst);
    Value thrown_value = f.regs[a];

    if (vm->try_stack_.empty()) {
        if (thrown_value.type == VAL_STRING && thrown_value.as.string) {
            VM_ERROR(vm, f, "%s", thrown_value.as.string->data);
        } else {
            VM_ERROR(vm, f, "Uncaught exception");
        }
        return -1;
    }

    MobiusVM::TryBlock& tb = vm->try_stack_.back();

    while (vm->callStackSize() > tb.call_stack_depth) {
        if (MOBIUS_UNLIKELY(vm->callStackTop().upvalue_count > 0))
            vm->closeUpvalues(vm->callStackTop(), 0);
        vm->callStackPop();
    }

    vm->registers_[tb.base + tb.catch_reg] = thrown_value;

    vm->callStackTop().ip = tb.catch_ip;

    vm->try_stack_.pop_back();

    vm->refreshFrame(f);
    return 0;
}

// ---- NOP ----
// OP_SPAWN A B C -- spawn R[B] with C-1 args; R[A] = FutureValue
MOBIUS_FORCEINLINE static int vm_op_spawn(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    uint8_t b = DECODE_B(inst);
    uint8_t c = DECODE_C(inst);
    int nargs = c - 1;

    Value& func_val = f.regs[b];

    if (func_val.type == VAL_FUNCTION) {
        MobiusFunction* mf = func_val.as.function;
        if (!mf || !mf->proto) {
            VM_ERROR(vm, f, "spawn: function has no bytecode prototype");
            return -1;
        }
        if (mf->upvalue_count > 0) {
            VM_ERROR(vm, f, "cannot spawn closure with captured variables; pass values as function arguments");
            return -1;
        }

        FutureValue* future = new FutureValue();

        std::vector<Value> args;
        args.reserve(nargs);
        for (int i = 0; i < nargs; i++) {
            args.push_back(f.regs[b + 1 + i]);
        }

        Prototype* proto = mf->proto;
        MobiusState* state = vm->state_;

        ((RefCounted*)future)->retain();

        JobDecl job;
        job.entry = [state, proto, args_copy = std::move(args), future]() mutable {
            MobiusVM fiber_vm(state);
            fiber_vm.future_ = future;
            int nargs = (int)args_copy.size();
            int needed = proto->num_registers + nargs + 256 + 1;
            fiber_vm.ensureRegisters(needed);

            int base = 1;
            for (int i = 0; i < nargs; i++) {
                fiber_vm.registers_[base + i] = std::move(args_copy[i]);
            }

            std::fill(fiber_vm.type_tags_.begin() + base,
                      fiber_vm.type_tags_.begin() + base + proto->num_registers, VAL_UNKNOWN);

            MobiusVM* prev_vm = MobiusVM::t_current_vm;
            MobiusVM::t_current_vm = &fiber_vm;

            size_t depth_before = fiber_vm.callStackSize();
            fiber_vm.callStackPush(proto, proto->code.data(), base, 2);

            uint64_t t0 = get_time_ns_vm();
            int rc = fiber_vm.run(depth_before);
            fiber_vm.metrics_->total_execution_time_ns += get_time_ns_vm() - t0;

            MobiusVM::t_current_vm = prev_vm;

            if (rc == 0) {
                future->resolve(fiber_vm.registers_[0]);
            } else {
                Value err;
                if (fiber_vm.last_error_ && fiber_vm.last_error_->message) {
                    err = make_string_value_from_cstr(state, fiber_vm.last_error_->message);
                } else {
                    err = make_string_value_from_cstr(state, "spawn: fiber execution failed");
                }
                future->reject(err);
            }
            ((RefCounted*)future)->release();
        };

        JobSystem* js = state->jobSystem();
        js->submit(std::move(job));

        RA(inst) = make_future_value(future);
        return 0;

    } else if (func_val.type == VAL_NATIVE_FUNCTION) {
        VM_ERROR(vm, f, "spawn: cannot spawn native functions");
        return -1;
    } else {
        VM_ERROR(vm, f, "spawn: expected a function, got %s", value_type_name(func_val.type));
        return -1;
    }
}

// OP_AWAIT A B -- R[A] = await future in R[B]
MOBIUS_FORCEINLINE static int vm_op_await(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    uint8_t b = DECODE_B(inst);

    Value& future_val = f.regs[b];
    if (future_val.type != VAL_FUTURE || !future_val.as.future) {
        VM_ERROR(vm, f, "await: expected a future value, got %s", value_type_name(future_val.type));
        return -1;
    }

    FutureValue* future = future_val.as.future;

    if (!future->isDone()) {
        std::unique_lock<std::mutex> lock(future->mutex());
        future->cv().wait(lock, [future]() { return future->isDone(); });
    }

    if (future->isResolved()) {
        RA(inst) = future->result();
    } else {
        const Value& err = future->error();
        if (err.type == VAL_STRING && err.as.string) {
            VM_ERROR(vm, f, "spawned fiber failed: %s", err.as.string->data);
        } else {
            VM_ERROR(vm, f, "spawned fiber failed");
        }
        return -1;
    }

    return 0;
}

// OP_YIELD A -- cooperatively yield current fiber
MOBIUS_FORCEINLINE static int vm_op_yield(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    (void)vm; (void)f; (void)inst;
    std::this_thread::yield();
    return 0;
}

// OP_CANCEL_CHECK -- if current fiber has been cancelled, throw CancellationError
MOBIUS_FORCEINLINE static int vm_op_cancel_check(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    (void)f; (void)inst;
    if (vm->future_ && vm->future_->isCancelled()) {
        VM_ERROR(vm, f, "CancellationError: fiber was cancelled");
        return -1;
    }
    return 0;
}

// OP_SHARE A -- mark R[A] as shared, deeply propagating to nested containers
MOBIUS_FORCEINLINE static int vm_op_share(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    uint8_t a = DECODE_A(inst);
    Value& val = f.regs[a];

    if (val.type == VAL_ARRAY && val.as.array) {
        val.as.array->markShared();
        val.flags |= VAL_FLAG_SHARED;
    } else if (val.type == VAL_TABLE && val.as.table) {
        val.as.table->markShared();
        val.flags |= VAL_FLAG_SHARED;
    } else {
        VM_ERROR(vm, f, "shared: expected an array or table, got %s", value_type_name(val.type));
        return -1;
    }
    return 0;
}

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

int MobiusVM::run(size_t base_depth) {
    VMFrame f;
    f.ci = &callStackTop();
    f.ip = f.ci->ip;
    f.proto = f.ci->proto;
    f.base = f.ci->base;
    f.regs = registers_.data() + f.base;
    f.tags = type_tags_.data() + f.base;

    uint32_t inst;

#if defined(__GNUC__) || defined(__clang__)
    static const void* dispatch_table[] = {
        &&L_OP_MOVE, &&L_OP_LOADK, &&L_OP_LOADNIL, &&L_OP_LOADBOOL, &&L_OP_LOADINT,
        &&L_OP_GETUPVAL, &&L_OP_SETUPVAL, &&L_OP_GETGLOBAL, &&L_OP_SETGLOBAL,
        &&L_OP_NEWTABLE, &&L_OP_NEWARRAY, &&L_OP_INDEX_GET, &&L_OP_INDEX_SET,
        &&L_OP_ADD, &&L_OP_SUB, &&L_OP_MUL, &&L_OP_DIV, &&L_OP_MOD,
        &&L_OP_UNM, &&L_OP_NOT,
        &&L_OP_BAND, &&L_OP_BOR, &&L_OP_BXOR, &&L_OP_BNOT, &&L_OP_SHL, &&L_OP_SHR,
        &&L_OP_EQ, &&L_OP_LT, &&L_OP_LE,
        &&L_OP_TEST, &&L_OP_TESTSET,
        &&L_OP_JMP,
        &&L_OP_CALL, &&L_OP_TAILCALL, &&L_OP_RETURN,
        &&L_OP_CLOSURE, &&L_OP_CLOSE,
        &&L_OP_IFORPREP, &&L_OP_IFORLOOP,
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
        &&L_OP_MOVE_ADDI, &&L_OP_GETGLOBAL_INDEX_GET, &&L_OP_GETGLOBAL_CALL,
        &&L_OP_ARRAY_PUSH,
        &&L_OP_LEN,
        &&L_OP_TRY_BEGIN, &&L_OP_TRY_END, &&L_OP_THROW,
        &&L_OP_SPAWN, &&L_OP_AWAIT, &&L_OP_YIELD,
        &&L_OP_SHARE,
        &&L_OP_CANCEL_CHECK,
        &&L_OP_SELF,
        &&L_OP_LT_II, &&L_OP_LE_II, &&L_OP_EQ_II,
        &&L_OP_LT_FF, &&L_OP_LE_FF, &&L_OP_EQ_FF,
        &&L_OP_TYPELOCK,
        &&L_OP_TYPECHECK_LOCKED,
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
    #define VM_HANDLER(op, fn) VM_CASE(op) {                           \
        int _rc = fn(this, f, inst);                                    \
        if (MOBIUS_UNLIKELY(_rc < 0)) {                                 \
            if (!try_stack_.empty()) {                                   \
                TryBlock& _tb = try_stack_.back();                       \
                while (callStackSize() > _tb.call_stack_depth) {         \
                    if (callStackTop().upvalue_count > 0)                 \
                        closeUpvalues(callStackTop(), 0);                 \
                    callStackPop();                                       \
                }                                                        \
                InternalError* _ie = state_->getLastError();              \
                const char* err = _ie ? _ie->message : nullptr;          \
                if (err) {                                               \
                    registers_[_tb.base + _tb.catch_reg] =               \
                        make_string_value_from_cstr(state_, err);        \
                } else {                                                 \
                    registers_[_tb.base + _tb.catch_reg] = make_nil_value();  \
                }                                                        \
                callStackTop().ip = _tb.catch_ip;                        \
                try_stack_.pop_back();                                    \
                refreshFrame(f);                                         \
                VM_NEXT();                                               \
            }                                                            \
            return -1;                                                   \
        }                                                                \
        VM_NEXT();                                                       \
    }

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
    VM_HANDLER(OP_INDEX_GET, vm_op_index_get)
    VM_HANDLER(OP_INDEX_SET, vm_op_index_set)
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

    VM_HANDLER(OP_IFORPREP, vm_op_iforprep)
    VM_HANDLER(OP_IFORLOOP, vm_op_iforloop)

    VM_HANDLER(OP_MOVE_ADDI, vm_op_move_addi)
    VM_HANDLER(OP_GETGLOBAL_INDEX_GET, vm_op_getglobal_index_get)
    VM_HANDLER(OP_GETGLOBAL_CALL, vm_op_getglobal_call)

    VM_HANDLER(OP_ARRAY_PUSH, vm_op_array_push)

    VM_HANDLER(OP_CALL, vm_op_call)

    VM_CASE(OP_TAILCALL) {
        int rc = vm_op_tailcall(this, f, inst, base_depth);
        if (MOBIUS_UNLIKELY(rc < 0)) {
            if (!try_stack_.empty()) {
                TryBlock& _tb = try_stack_.back();
                while (callStackSize() > _tb.call_stack_depth) {
                    if (callStackTop().upvalue_count > 0)
                        closeUpvalues(callStackTop(), 0);
                    callStackPop();
                }
                InternalError* _ie = state_->getLastError();
                const char* err = _ie ? _ie->message : nullptr;
                if (err) {
                    registers_[_tb.base + _tb.catch_reg] =
                        make_string_value_from_cstr(state_, err);
                } else {
                    registers_[_tb.base + _tb.catch_reg] = make_nil_value();
                }
                callStackTop().ip = _tb.catch_ip;
                try_stack_.pop_back();
                refreshFrame(f);
                VM_NEXT();
            }
            return -1;
        }
        if (rc == 1) return 0;
        VM_NEXT();
    }

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
    VM_HANDLER(OP_TRY_BEGIN, vm_op_try_begin)
    VM_HANDLER(OP_TRY_END, vm_op_try_end)
    VM_HANDLER(OP_SPAWN, vm_op_spawn)
    VM_HANDLER(OP_AWAIT, vm_op_await)
    VM_HANDLER(OP_YIELD, vm_op_yield)
    VM_HANDLER(OP_SHARE, vm_op_share)
    VM_HANDLER(OP_CANCEL_CHECK, vm_op_cancel_check)
    VM_HANDLER(OP_THROW, vm_op_throw)
    VM_HANDLER(OP_SELF, vm_op_self)
    VM_HANDLER(OP_LT_II, vm_op_lt_ii)
    VM_HANDLER(OP_LE_II, vm_op_le_ii)
    VM_HANDLER(OP_EQ_II, vm_op_eq_ii)
    VM_HANDLER(OP_LT_FF, vm_op_lt_ff)
    VM_HANDLER(OP_LE_FF, vm_op_le_ff)
    VM_HANDLER(OP_EQ_FF, vm_op_eq_ff)
    VM_HANDLER(OP_TYPELOCK, vm_op_typelock)
    VM_HANDLER(OP_TYPECHECK_LOCKED, vm_op_typecheck_locked)
    VM_HANDLER(OP_NOP, vm_op_nop)

    VM_DEFAULT()
        f.ci->ip = f.ip;
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
