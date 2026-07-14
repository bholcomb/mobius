#include "vm/vm.h"
#include "state/mobius_state.h"
#include "data/table.h"
#include "data/array.h"
#include "data/array_slice.h"
#include "data/buffer.h"
#include "data/shared_cell.h"
#include "data/enum.h"
#include "data/function.h"
#include "data/future.h"
#include "data/metamethods.h"
#include "plugin/module_registry.h"
#include "internal/string_intern.h"
#include "internal/gc.h"
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


// ----------------------------------------------------------------------------
// Pool-backed operator new/delete. Exact-size allocations use the per-thread
// GC pools; any other size (a subclass) uses the global heap. The unsized /
// nothrow deletes assume the class size — a larger (glibc-origin) chunk that
// reaches the pool is absorbed safely: chunks never return to the global
// heap, so the size routing can never mismatch an actual glibc free.
// ----------------------------------------------------------------------------
void* Upvalue::operator new(size_t sz) {
    if (sz == sizeof(Upvalue))
        if (void* p = gc_object_alloc(GC_UPVALUE, sz)) return p;
    return ::operator new(sz);
}
void* Upvalue::operator new(size_t sz, const std::nothrow_t&) noexcept {
    if (sz == sizeof(Upvalue))
        if (void* p = gc_object_alloc(GC_UPVALUE, sz)) return p;
    return ::operator new(sz, std::nothrow);
}
void Upvalue::operator delete(void* p, size_t sz) noexcept {
    (void)sz;
    if (p) gc_object_free(GC_UPVALUE, p);
}
void Upvalue::operator delete(void* p) noexcept {
    if (p) gc_object_free(GC_UPVALUE, p);
}
void Upvalue::operator delete(void* p, const std::nothrow_t&) noexcept {
    if (p) gc_object_free(GC_UPVALUE, p);
}
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

// Container-subscript extraction. The old code read v.as.i64 raw, so a float
// (or any non-integer) key had its bit pattern reinterpreted as an index —
// a[1.5] read a garbage element on get, and on set could try to grow the
// array to ~10^18 elements. Integral floats coerce (a[2.0] == a[2]); anything
// else yields -1, which every index path already treats as out of bounds.
static MOBIUS_FORCEINLINE int64_t vm_index_or_neg1(const Value& v) {
    if (MOBIUS_LIKELY(v.type == VAL_INT64)) return v.as.i64;
    if (v.type == VAL_UINT64)
        return v.as.u64 <= (uint64_t)INT64_MAX ? (int64_t)v.as.u64 : -1;
    if (v.type == VAL_FLOAT64) {
        double d = v.as.double_val;
        if (d >= 0.0 && d < 9223372036854775808.0) {
            int64_t i = (int64_t)d;
            if ((double)i == d) return i;
        }
    }
    return -1;
}

// Lexicographic string ordering that respects the tracked length (strings can
// contain NULs, which strcmp would treat as terminators).
static MOBIUS_FORCEINLINE bool mobius_string_less(const MobiusString* a, const MobiusString* b) {
    size_t n = a->length < b->length ? a->length : b->length;
    int c = memcmp(a->data, b->data, n);
    if (c != 0) return c < 0;
    return a->length < b->length;
}

static inline bool vm_value_to_byte(const Value& value, uint8_t* out) {
    if (value.type == VAL_INT64) {
        if (value.as.i64 < 0 || value.as.i64 > 255) return false;
        *out = (uint8_t)value.as.i64;
        return true;
    }
    if (value.type == VAL_UINT64) {
        if (value.as.u64 > 255) return false;
        *out = (uint8_t)value.as.u64;
        return true;
    }
    return false;
}

static inline Value make_retained_table_value(Table* table) {
    if (!table) return Value();
    table->retain();
    return make_table_value(table);
}

// Error messages contain dynamic data (names, indices, formatted values), so
// interning them would grow the string pool forever in a loop that catches
// errors. Build a refcounted heap string instead.
static Value make_error_string_value(const char* msg) {
    if (!msg) return make_nil_value();
    MobiusString* s = StringInternPool::createHeap(msg, strlen(msg));
    return s ? make_string_value_adopt(s) : make_nil_value();
}

static inline void release_atomic_locks(MobiusVM* vm, size_t keep_depth) {
    while (vm->atomic_locks_.size() > keep_depth) {
        MobiusVM::AtomicLock held = vm->atomic_locks_.back();
        vm->atomic_locks_.pop_back();
        if (held.mutex) {
            held.mutex->unlock();
        }
    }
}

// Walk the metatable __index chain from `start`, invoking the first *function*
// __index found and descending through *table* __index links — i.e. full
// Lua-style __index resolution, with the function check applied at every level.
// `getByString`/`get` already resolve a value through a table __index chain;
// this complements that for the function case (and is used identically for
// tables and userdata). `receiver` is the original object passed to __index.
// Returns 1 (+*out) if a function __index produced a value, 0 if none applies,
// -1 on error. Called only on a miss, so the common fast path is untouched.
static int vm_index_function_fallback(MobiusVM* vm, const Value& receiver, Table* start,
                                      const Value& key, Value* out) {
    MobiusState* state = vm->state_;
    MobiusString* index_name = state->metamethods()->index();
    Value receiver_copy = receiver;
    Value key_copy = key;
    Table* cur = start;
    for (int guard = 0; cur && guard < 1000; guard++) {
        Table* mt = cur->getMetatable();
        if (!mt) return 0;
        Value idx = mt->getByString(index_name);
        if (idx.type == VAL_FUNCTION || idx.type == VAL_NATIVE_FUNCTION) {
            Value result;
            int rc = vm->callMetamethod(make_retained_table_value(mt), index_name,
                                        receiver_copy, key_copy, result);
            if (rc < 0) return -1;
            if (rc > 0) { if (out) *out = result; return 1; }
            return 0;
        }
        if (idx.type == VAL_TABLE && idx.as.table) { cur = idx.as.table; continue; }
        return 0;
    }
    return 0;
}

static inline int vm_userdata_lookup(MobiusVM* vm, const Value& value, const Value& key, Value* out) {
    MobiusState* state = vm->state_;
    if (out) *out = Value();
    if (value.type != VAL_USERDATA || !value.as.userdata) return 0;
    Value value_copy = value;
    Value key_copy = key;

    auto try_metatable = [&](Table* mt) -> int {
        // 1) value via direct entries and any *table* __index chain.
        Value result = (key_copy.type == VAL_STRING) ? mt->getByString(key_copy.as.string)
                                                     : mt->get(key_copy);
        if (result.type != VAL_NIL) { if (out) *out = result; return 1; }
        // 2) a *function* __index anywhere along the metatable chain.
        int rc = vm_index_function_fallback(vm, value_copy, mt, key_copy, out);
        if (rc != 0) return rc;
        // 3) struct-view convention: __index is a native function stored as a
        //    direct key on the type metatable itself (not on its metatable).
        Value direct = mt->getByString(state->metamethods()->index());
        if (direct.type == VAL_FUNCTION || direct.type == VAL_NATIVE_FUNCTION) {
            Value res;
            int crc = vm->callMetamethod(make_retained_table_value(mt), state->metamethods()->index(),
                                         value_copy, key_copy, res);
            if (crc < 0) return -1;
            if (crc > 0) { if (out) *out = res; return 1; }
        }
        return 0;
    };

    Table* specific = value_copy.as.userdata->type_tag
        ? state->userdataTypeMetatable(value_copy.as.userdata->type_tag)
        : nullptr;
    if (specific) {
        int rc = try_metatable(specific);
        if (rc != 0) return rc;
    }

    Table* generic = state->typeMetatable(VAL_USERDATA);
    if (generic) {
        int rc = try_metatable(generic);
        if (rc != 0) return rc;
    }

    return 0;
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

thread_local MobiusVM* MobiusVM::t_current_vm = nullptr;

namespace {
struct ScopedCurrentVM {
    MobiusVM* prev;

    explicit ScopedCurrentVM(MobiusVM* vm) : prev(MobiusVM::t_current_vm) {
        MobiusVM::t_current_vm = vm;
    }

    ~ScopedCurrentVM() {
        MobiusVM::t_current_vm = prev;
    }
};
} // namespace

MobiusVM::MobiusVM(MobiusState* state)
    : state_(state), metrics_(&state->metrics()),
      strict_mode_(state->config().strict_mode),
      warn_on_conversion_(state->config().warn_on_conversion),
      override_behavior_(state->config().override_behavior),
      native_ctx_{}, last_error_(nullptr),
      source_code_(nullptr), exec_context_(nullptr),
      register_capacity_(0),
      call_depth_(0), call_stack_capacity_(VM_INITIAL_CALL_STACK)
{
    registers_.resize(VM_INITIAL_REGISTERS);
    type_tags_.resize(VM_INITIAL_REGISTERS, VAL_UNKNOWN);
    register_capacity_ = (int)registers_.size();
    regs_data_ = registers_.data();
    tags_data_ = type_tags_.data();
    call_stack_ = new CallInfo[VM_INITIAL_CALL_STACK];
    call_stack_[0].initUpvalues();

    native_ctx_.registers = registers_.data();
    native_ctx_.base      = 0;
    native_ctx_.top       = 0;
    native_ctx_.capacity  = register_capacity_;

    exec_context_ = new ExecutionContext(state, state->config().max_call_depth);
}

MobiusVM::~MobiusVM() {
    delete[] call_stack_;
    delete exec_context_;
    if (last_error_) {
        free_internal_error(last_error_);
    }
}

void MobiusVM::growRegisters(int needed) {
    // Open upvalues hold raw Value* into registers_; if resize moves the
    // buffer they dangle, and GETUPVAL/SETUPVAL would touch freed memory.
    // Every open upvalue is tracked in the upvalue list of the frame whose
    // register it points into, so walking live frames finds them all.
    uintptr_t old_data = (uintptr_t)registers_.data();
    registers_.resize(needed, Value());
    type_tags_.resize(needed, VAL_UNKNOWN);
    uintptr_t new_data = (uintptr_t)registers_.data();

    if (MOBIUS_UNLIKELY(new_data != old_data)) {
        for (size_t d = 0; d <= call_depth_; d++) {
            CallInfo& ci = call_stack_[d];
            for (int i = 0; i < ci.upvalue_count; i++) {
                Upvalue* uv = ci.upvalues[i];
                if (uv && uv->is_open) {
                    uintptr_t off = (uintptr_t)uv->location - old_data;
                    uv->location = (Value*)(new_data + off);
                }
            }
        }
    }

    register_capacity_ = (int)registers_.size();
    regs_data_ = registers_.data();
    tags_data_ = type_tags_.data();
    native_ctx_.registers = regs_data_;
    native_ctx_.capacity  = register_capacity_;
    if ((size_t)register_capacity_ > metrics_->peak_registers)
        metrics_->peak_registers = register_capacity_;
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
            // Inline: copy elements, fix pointer. Ownership of the entries'
            // references moves with them — zero the source count so the old
            // slot's destructor doesn't release them a second time.
            for (int j = 0; j < src.upvalue_count; j++)
                dst.inline_upvals[j] = src.inline_upvals[j];
            dst.upvalues = dst.inline_upvals;
            dst.upvalue_count = src.upvalue_count;
            dst.upvalue_capacity = CALLINFO_INLINE_UPVALS;
            src.upvalue_count = 0;
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
    JobSystem* js = state_->jobSystem();

    // Only use executeAsMainFiber for the top-level call (not from within
    // a fiber, e.g. during import/require which re-enters execute).
    if (js && !js->currentFiber()) {
        MobiusVM* vm = this;
        Prototype* p = proto;
        return js->executeAsMainFiber([vm, p]() -> int {
            return vm->executeDirect(p);
        });
    }

    return executeDirect(proto);
}

int MobiusVM::executeDirect(Prototype* proto) {
    ensureRegisters(proto->num_registers + 256);

    size_t saved_call_depth = call_depth_;
    size_t saved_try_depth = try_stack_.size();
    int saved_native_base = native_ctx_.base;
    int saved_native_top = native_ctx_.top;

    callStackPush(proto, 0, 0).ip = proto->code.data();

    ScopedCurrentVM bind_vm(this);

    JobSystem* js = state_->jobSystem();
    if (js) {
        MobiusFiber* self = js->currentFiber();
        if (self) self->vm = this;
    }

    uint64_t t0 = get_time_ns_vm();
    int rc = run(saved_call_depth + 1);
    metrics_->total_execution_time_ns += get_time_ns_vm() - t0;

    try_stack_.resize(saved_try_depth);
    while (call_depth_ > saved_call_depth) {
        if (MOBIUS_UNLIKELY(callStackTop().upvalue_count > 0))
            closeUpvalues(callStackTop(), 0);
        callStackPop();
    }

    native_ctx_.registers = registers_.data();
    native_ctx_.capacity  = register_capacity_;
    native_ctx_.base      = saved_native_base;
    native_ctx_.top       = saved_native_top;

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

    int saved_base = native_ctx_.base;
    int saved_top  = native_ctx_.top;

    native_ctx_.registers = registers_.data();
    native_ctx_.base      = args_base;
    native_ctx_.top       = args_base + nargs;
    native_ctx_.capacity  = (int)registers_.size();

    size_t keepalive_mark = native_keepalive_.size();
    native_depth_++;
    int rc = func(state_, nargs);
    native_depth_--;
    trimNativeKeepalive(keepalive_mark);   // release strings pinned for this call

    int result_top = native_ctx_.top;

    native_ctx_.base = saved_base;
    native_ctx_.top  = saved_top;

    if (rc < 0) return -1;

    int dest = caller_base + func_reg;
    int n = (nresults == 0) ? rc : (nresults - 1);
    for (int i = 0; i < n; i++) {
        if (args_base + i < result_top) {
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

    if (!child->param_unwrap_on_entry.empty()) {
        for (int i = 0; i < nargs; i++) {
            Value& arg = registers_[child_base + i];
            if (i < (int)child->param_unwrap_on_entry.size() &&
                child->param_unwrap_on_entry[i] &&
                arg.type == VAL_SHARED_CELL && arg.as.shared_cell) {
                arg = arg.as.shared_cell->load();
            }
        }
    }

    if (MOBIUS_UNLIKELY(child->has_type_locks)) {
        memset(&type_tags_[child_base], (uint8_t)VAL_UNKNOWN, child->num_registers);
    }

    CallInfo& new_ci = callStackPush(child, child_base, nresults);
    new_ci.ip = child->code.data();
    if (mf->upvalues && mf->upvalue_count > 0) {
        if (!new_ci.setUpvaluesFrom(mf->upvalues, mf->upvalue_count)) {
            runtimeError("Failed to allocate closure upvalues");
            return -1;
        }
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
    if (method.type == VAL_NIL) {
        method = table_val.as.table->getByString(mm_name);
    }
    if (method.type == VAL_NIL) return 0;

    if (method.type == VAL_NATIVE_FUNCTION) {
        int caller_base = callStackTop().base;
        int caller_regs = callStackTop().proto->num_registers;
        int scratch = caller_base + caller_regs;
    Value lhs_copy = lhs;
    Value rhs_copy = rhs;

        int needed = scratch + 4;
        ensureRegisters(needed);

    registers_[scratch]     = lhs_copy;
    registers_[scratch + 1] = rhs_copy;

        int saved_base = native_ctx_.base;
        int saved_top  = native_ctx_.top;

        native_ctx_.registers = registers_.data();
        native_ctx_.base      = scratch;
        native_ctx_.top       = scratch + 2;
        native_ctx_.capacity  = (int)registers_.size();

        size_t keepalive_mark = native_keepalive_.size();
        native_depth_++;
        int rc = method.as.native_function(state_, 2);
        native_depth_--;
        trimNativeKeepalive(keepalive_mark);

        int result_top = native_ctx_.top;
        native_ctx_.base = saved_base;
        native_ctx_.top  = saved_top;

        if (rc < 0) {
            runtimeError("Metamethod '%s' failed", mm_name->data);
            return -1;
        }
        if (rc > 0 && result_top > scratch) {
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
        // Copy operands before ensureRegisters: a caller passing references
        // into registers_ would otherwise see them invalidated by the resize
        // (the native branch above already copies defensively).
        Value lhs_copy = lhs;
        Value rhs_copy = rhs;
        ensureRegisters(scratch + 3 + child->num_registers + 16);

        registers_[scratch]     = method;
        registers_[scratch + 1] = lhs_copy;
        registers_[scratch + 2] = rhs_copy;

        int child_base = scratch + 1;
        if (!child->param_unwrap_on_entry.empty()) {
            for (int i = 0; i < 2; i++) {
                Value& arg = registers_[child_base + i];
                if (i < (int)child->param_unwrap_on_entry.size() &&
                    child->param_unwrap_on_entry[i] &&
                    arg.type == VAL_SHARED_CELL && arg.as.shared_cell) {
                    arg = arg.as.shared_cell->load();
                }
            }
        }
        if (MOBIUS_UNLIKELY(child->has_type_locks)) {
            memset(&type_tags_[child_base], (uint8_t)VAL_UNKNOWN, child->num_registers);
        }
        size_t stop_depth = callStackSize();
        callStackPush(child, child_base, 2).ip = child->code.data();

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
    Value* __restrict__      regs;
    ValueType* __restrict__  tags;
};

static MOBIUS_FORCEINLINE GlobalEnvironment* frame_globals(MobiusVM* vm, const VMFrame& f) {
    return f.proto && f.proto->globals ? f.proto->globals : vm->state_->rootGlobalEnvironment();
}

// Point the frame cache at a freshly pushed child frame, using values the
// caller already has in registers rather than re-reading them from the new
// CallInfo (as refreshFrame would). `ci` must be the pushed CallInfo.
static MOBIUS_FORCEINLINE void enter_child_frame(MobiusVM* vm, VMFrame& f, CallInfo* ci,
                                                 Prototype* child, int child_base) {
    f.ci    = ci;
    f.proto = child;
    f.base  = child_base;
    f.regs  = vm->regs_data_ + child_base;
    f.tags  = vm->tags_data_ + child_base;
    f.ip    = child->code.data();
}

MOBIUS_FORCEINLINE void MobiusVM::refreshFrame(VMFrame& f) {
    f.ci = &callStackTop();
    f.proto = f.ci->proto;
    f.base = f.ci->base;
    f.regs = regs_data_ + f.base;
    f.tags = tags_data_ + f.base;
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

// Bind an operand without copying on the common (non-cell) path: a Value copy
// is 16 bytes plus a retain/release pair for refcounted types, paid per
// operand per instruction in the generic handlers. `storage` must outlive the
// returned reference. CAUTION: the reference may alias a VM register — any
// branch that can re-enter the VM (metamethods) or reallocate registers_ must
// copy first.
static MOBIUS_FORCEINLINE const Value& shared_peek(const Value& v, Value& storage) {
    if (MOBIUS_UNLIKELY(v.type == VAL_SHARED_CELL && v.as.shared_cell)) {
        storage = v.as.shared_cell->load();
        return storage;
    }
    return v;
}

static MOBIUS_FORCEINLINE Value shared_unwrap(const Value& value) {
    if (value.type == VAL_SHARED_CELL && value.as.shared_cell) {
        return value.as.shared_cell->load();
    }
    return value;
}

static MOBIUS_FORCEINLINE bool shared_store(Value& dst, const Value& src) {
    if (dst.type == VAL_SHARED_CELL && dst.as.shared_cell) {
        dst.as.shared_cell->store(shared_unwrap(src));
        return true;
    }
    return false;
}

static MOBIUS_FORCEINLINE Value prepare_param_value(const Prototype* proto, int param_idx,
                                                   const Value& value) {
    if (proto &&
        param_idx >= 0 &&
        param_idx < (int)proto->param_unwrap_on_entry.size() &&
        proto->param_unwrap_on_entry[param_idx]) {
        return shared_unwrap(value);
    }
    return value;
}

// ---- Data movement ----

MOBIUS_FORCEINLINE static int vm_op_move(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    (void)vm;
    const Value& src = RB(inst);
    if (MOBIUS_LIKELY(src.type < VAL_FIRST_REFCOUNTED)) {
        Value& dst = RA(inst);
        if (MOBIUS_LIKELY(dst.type < VAL_FIRST_REFCOUNTED)) {
            dst.rawCopyFrom(src);
        } else {
            dst = src;
        }
    } else {
        RA(inst) = src;
    }
    return 0;
}

int MobiusVM::callTernaryMetamethod(const Value& table_val, MobiusString* mm_name,
                                    const Value& a, const Value& b, const Value& c) {
    if (table_val.type != VAL_TABLE || !table_val.as.table) return 0;

    Value method = table_val.as.table->getMetamethod(mm_name);
    if (method.type == VAL_NIL) {
        method = table_val.as.table->getByString(mm_name);
    }
    if (method.type == VAL_NIL) return 0;

    if (method.type != VAL_NATIVE_FUNCTION) {
        runtimeError("Metamethod '%s' must be native", mm_name->data);
        return -1;
    }

    int caller_base = callStackTop().base;
    int caller_regs = callStackTop().proto->num_registers;
    int scratch = caller_base + caller_regs;
    Value a_copy = a;
    Value b_copy = b;
    Value c_copy = c;

    int needed = scratch + 5;
    ensureRegisters(needed);

    registers_[scratch] = a_copy;
    registers_[scratch + 1] = b_copy;
    registers_[scratch + 2] = c_copy;

    int saved_base = native_ctx_.base;
    int saved_top = native_ctx_.top;

    native_ctx_.registers = registers_.data();
    native_ctx_.base = scratch;
    native_ctx_.top = scratch + 3;
    native_ctx_.capacity = (int)registers_.size();

    size_t keepalive_mark = native_keepalive_.size();
    native_depth_++;
    int rc = method.as.native_function(state_, 3);
    native_depth_--;
    trimNativeKeepalive(keepalive_mark);

    native_ctx_.base = saved_base;
    native_ctx_.top = saved_top;

    if (rc < 0) {
        runtimeError("Metamethod '%s' failed", mm_name->data);
        return -1;
    }
    return 1;
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

MOBIUS_FORCEINLINE static int vm_op_shared_load(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    (void)vm;
    // Assign straight from the source register on the common (non-cell) path.
    // Going through shared_unwrap() would materialize a by-value temporary,
    // costing an extra Value copy plus a retain/release pair on every unwrap.
    const Value& src = RB(inst);
    if (MOBIUS_UNLIKELY(src.type == VAL_SHARED_CELL && src.as.shared_cell)) {
        RA(inst) = src.as.shared_cell->load();
    } else {
        RA(inst) = src;
    }
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_shared_store(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    (void)vm;
    Value& dst = RA(inst);
    const Value& src = RB(inst);
    if (!shared_store(dst, src)) {
        dst = src;
    }
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_lock_shared(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    (void)vm;
    Value& val = RA(inst);
    if (val.type == VAL_SHARED_CELL && val.as.shared_cell) {
        val.as.shared_cell->mutex().lock();
    } else if (val.type == VAL_ARRAY_SLICE && val.as.array_slice && val.as.array_slice->ownerCell()) {
        val.as.array_slice->ownerCell()->mutex().lock();
    }
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_unlock_shared(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    (void)vm;
    Value& val = RA(inst);
    if (val.type == VAL_SHARED_CELL && val.as.shared_cell) {
        val.as.shared_cell->mutex().unlock();
    } else if (val.type == VAL_ARRAY_SLICE && val.as.array_slice && val.as.array_slice->ownerCell()) {
        val.as.array_slice->ownerCell()->mutex().unlock();
    }
    return 0;
}

// ---- Globals ----

MOBIUS_FORCEINLINE static int vm_op_getglobal(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    int slot = DECODE_Bx(inst);
    GlobalEnvironment* globals = frame_globals(vm, f);
    Value& dst = RA(inst);
    if (MOBIUS_UNLIKELY(!vm->state_->copyGlobalValue(slot, &dst, globals) ||
                        !(dst.flags & VAL_FLAG_DEFINED))) {
        VM_ERROR(vm, f, "Undefined variable '%s'", vm->state_->globalSlotName(slot, globals));
        return -1;
    }
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_setglobal(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    int slot = DECODE_Bx(inst);
    GlobalEnvironment* globals = frame_globals(vm, f);
    Value gv = vm->state_->getGlobalValue(slot, globals);
    if (MOBIUS_UNLIKELY(gv.flags & VAL_FLAG_READONLY)) {
        VM_ERROR(vm, f, "Cannot assign to read-only variable '%s'", vm->state_->globalSlotName(slot, globals));
        return -1;
    }
    if (!shared_store(gv, RA(inst))) {
        gv = RA(inst);
    }
    gv.flags |= VAL_FLAG_DEFINED;
    if (gv.type == VAL_ENUM && gv.aux == -1) {
        gv.flags |= VAL_FLAG_READONLY;
    }
    vm->state_->setGlobalValue(slot, gv, globals, false);
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_global_readonly(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    (void)f;
    int slot = DECODE_Bx(inst);
    bool readonly = DECODE_A(inst) != 0;
    vm->state_->setGlobalReadonly(slot, readonly, frame_globals(vm, f));
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
        Value& dst = *f.ci->upvalues[b]->location;
        if (!shared_store(dst, RA(inst))) {
            dst = RA(inst);
        }
    }
    return 0;
}

// ---- Tables and arrays ----

MOBIUS_FORCEINLINE static int vm_op_newtable(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    // Allocation ops are the collection safepoints: pressure builds exactly
    // here, and a call-free allocating loop has no other safe boundary.
    if (MOBIUS_UNLIKELY(g_gc_pending)) gc_safepoint(vm);
    Table* tbl = new (std::nothrow) Table(vm->state_, DECODE_C(inst));
    if (!tbl) { VM_ERROR(vm, f, "Failed to allocate table"); return -1; }
    RA(inst) = make_table_value(tbl);
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_newarray(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    if (MOBIUS_UNLIKELY(g_gc_pending)) gc_safepoint(vm);
    ArrayValue* arr = new (std::nothrow) ArrayValue(DECODE_B(inst));
    if (!arr) { VM_ERROR(vm, f, "Failed to allocate array"); return -1; }
    RA(inst) = make_array_value(arr);
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_index_get(MobiusVM* vm, VMFrame& f, uint32_t inst);
MOBIUS_FORCEINLINE static int vm_op_index_set(MobiusVM* vm, VMFrame& f, uint32_t inst);

// ---- Constant-string field access (compiler-proven table-dot sites) ----
// Same operand encoding as INDEX_GET/INDEX_SET, so the generic handlers are
// the fallback for every shape the fast path doesn't cover (shared cells,
// non-tables, metatable-driven lookups, new-key inserts).

MOBIUS_FORCEINLINE static int vm_op_getfield(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    const Value& obj = RB(inst);
    if (MOBIUS_LIKELY(obj.type == VAL_TABLE && obj.as.table &&
                      IS_CONSTANT(DECODE_C(inst)))) {
        const Value& key = f.ci->proto->constants[RK_AS_CONSTANT(DECODE_C(inst))];
        const Value* hit = obj.as.table->findString(key.as.string);
        if (MOBIUS_LIKELY(hit != nullptr)) {
            RA(inst) = *hit;
            return 0;
        }
    }
    return vm_op_index_get(vm, f, inst);
}

MOBIUS_FORCEINLINE static int vm_op_setfield(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    Value& obj = RA(inst);
    if (MOBIUS_LIKELY(obj.type == VAL_TABLE && obj.as.table &&
                      IS_CONSTANT(DECODE_B(inst)))) {
        const Value& key = f.ci->proto->constants[RK_AS_CONSTANT(DECODE_B(inst))];
        Value* slot = obj.as.table->findStringSlot(key.as.string);
        if (MOBIUS_LIKELY(slot != nullptr)) {
            // Existing-key overwrite: __newindex only fires on new keys.
            *slot = RKC(inst);
            return 0;
        }
    }
    return vm_op_index_set(vm, f, inst);
}

MOBIUS_FORCEINLINE static int vm_op_index_get(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    const Value& obj = RB(inst);
    const Value& key = RKC(inst);
    if (obj.type == VAL_SHARED_CELL && obj.as.shared_cell) {
        std::unique_lock<std::recursive_mutex> lock(obj.as.shared_cell->mutex());
        const Value& inner = obj.as.shared_cell->unsafeValue();
        if (inner.type == VAL_ARRAY && inner.as.array) {
            ArrayValue* arr = inner.as.array;
            int64_t idx = vm_index_or_neg1(key);
            if (MOBIUS_LIKELY(idx >= 0 && idx < (int64_t)arr->length())) {
                RA(inst) = arr->unsafeGet((size_t)idx);
            } else {
                RA(inst) = Value();
            }
        } else if (inner.type == VAL_TABLE && inner.as.table) {
            Value inner_tbl = inner;   // retains the table for use outside the lock
            Table* tbl = inner_tbl.as.table;
            Value result = (key.type == VAL_STRING) ? tbl->getByString(key.as.string)
                                                    : tbl->get(key);
            if (MOBIUS_UNLIKELY(result.type == VAL_NIL && tbl->getMetatable())) {
                lock.unlock();   // release the cell lock before invoking script
                f.ci->ip = f.ip;
        uint32_t* saved_ip = f.ip;
                Value looked;
                int rc = vm_index_function_fallback(vm, inner_tbl, tbl, key, &looked);
                vm->refreshFrame(f);
                f.ip = saved_ip;
                f.ci->ip = saved_ip;
                if (rc < 0) return -1;
                if (rc > 0) result = looked;
            }
            RA(inst) = result;
        } else if (inner.type == VAL_ARRAY_SLICE && inner.as.array_slice) {
            int64_t idx = vm_index_or_neg1(key);
            if (MOBIUS_LIKELY(idx >= 0 && idx < (int64_t)inner.as.array_slice->length())) {
                RA(inst) = inner.as.array_slice->get((size_t)idx);
            } else {
                RA(inst) = Value();
            }
        } else if (inner.type == VAL_BUFFER && inner.as.buffer) {
            int64_t idx = vm_index_or_neg1(key);
            if (MOBIUS_LIKELY(idx >= 0 && idx < (int64_t)inner.as.buffer->size())) {
                RA(inst) = make_int64_value((int64_t)inner.as.buffer->get((size_t)idx));
            } else {
                RA(inst) = Value();
            }
        } else if (inner.type == VAL_USERDATA && inner.as.userdata) {
            Value inner_ud = inner;   // retains the userdata for use outside the lock
            lock.unlock();            // release the cell lock before invoking script
            f.ci->ip = f.ip;
        uint32_t* saved_ip = f.ip;
            Value looked;
            int lookup_rc = vm_userdata_lookup(vm, inner_ud, key, &looked);
            vm->refreshFrame(f);
            f.ip = saved_ip;
            f.ci->ip = saved_ip;
            if (lookup_rc < 0) return -1;
            RA(inst) = looked;
        } else {
            VM_ERROR(vm, f, "Attempt to index a shared %s value", value_type_name(inner.type));
            return -1;
        }
    } else if (MOBIUS_LIKELY(obj.type == VAL_ARRAY && obj.as.array)) {
        ArrayValue* arr = obj.as.array;
        int64_t idx = vm_index_or_neg1(key);
        if (MOBIUS_LIKELY(idx >= 0 && idx < (int64_t)arr->length())) {
            RA(inst) = arr->unsafeGet((size_t)idx);
        } else {
            RA(inst) = Value();
        }
    } else if (obj.type == VAL_TABLE && obj.as.table) {
        Table* tbl = obj.as.table;
        if (MOBIUS_LIKELY(key.type == VAL_STRING)) {
            const Value* hit = tbl->findString(key.as.string);
            RA(inst) = hit ? *hit : tbl->getByString(key.as.string);
        } else {
            RA(inst) = tbl->get(key);
        }
        // On a miss, follow a *function* __index up the metatable chain (a
        // *table* __index was already handled by getByString/get above).
        if (MOBIUS_UNLIKELY(RA(inst).type == VAL_NIL && tbl->getMetatable())) {
            f.ci->ip = f.ip;
        uint32_t* saved_ip = f.ip;
            Value looked;
            int rc = vm_index_function_fallback(vm, obj, tbl, key, &looked);
            vm->refreshFrame(f);
            f.ip = saved_ip;
            f.ci->ip = saved_ip;
            if (rc < 0) return -1;
            if (rc > 0) RA(inst) = looked;
        }
    } else if (obj.type == VAL_ENUM && obj.as.enum_def) {
        if (key.type != VAL_STRING || !key.as.string) {
            VM_ERROR(vm, f, "Enum member lookup expects a string key");
            return -1;
        }
        const char* member_name = key.as.string->data;
        const EnumMember* member = obj.as.enum_def->findMember(member_name);
        if (!member) {
            VM_ERROR(vm, f, "Invalid enum member '%s' on enum '%s'",
                            member_name, obj.as.enum_def->name().c_str());
            return -1;
        }
        RA(inst) = Value::makeEnum(obj.as.enum_def, member->value);
    } else if (obj.type == VAL_ARRAY_SLICE && obj.as.array_slice) {
        int64_t idx = vm_index_or_neg1(key);
        if (MOBIUS_LIKELY(idx >= 0 && idx < (int64_t)obj.as.array_slice->length())) {
            RA(inst) = obj.as.array_slice->get((size_t)idx);
        } else {
            RA(inst) = Value();
        }
    } else if (obj.type == VAL_BUFFER && obj.as.buffer) {
        int64_t idx = vm_index_or_neg1(key);
        if (MOBIUS_LIKELY(idx >= 0 && idx < (int64_t)obj.as.buffer->size())) {
            RA(inst) = make_int64_value((int64_t)obj.as.buffer->get((size_t)idx));
        } else {
            RA(inst) = Value();
        }
    } else if (obj.type == VAL_USERDATA && obj.as.userdata) {
        f.ci->ip = f.ip;
        uint32_t* saved_ip = f.ip;
        Value looked;
        int lookup_rc = vm_userdata_lookup(vm, obj, key, &looked);
        vm->refreshFrame(f);
        f.ip = saved_ip;
        f.ci->ip = saved_ip;
        if (lookup_rc < 0) return -1;
        RA(inst) = looked;
    } else if (obj.type == VAL_STRING && obj.as.string) {
        if (key.type == VAL_INT64) {
            int64_t idx = vm_index_or_neg1(key);
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
    if (obj.type == VAL_SHARED_CELL && obj.as.shared_cell) {
        std::lock_guard<std::recursive_mutex> lock(obj.as.shared_cell->mutex());
        Value& inner = obj.as.shared_cell->unsafeValue();
        if (inner.type == VAL_ARRAY && inner.as.array) {
            int64_t idx = vm_index_or_neg1(key);
            if (MOBIUS_LIKELY(idx >= 0)) {
                ArrayValue* arr = inner.as.array;
                if ((int64_t)arr->length() <= idx && arr->hasActiveSlices()) {
                    VM_ERROR(vm, f, "cannot resize array while slices are alive");
                    return -1;
                }
                while ((int64_t)arr->length() <= idx)
                    arr->push(Value());
                arr->set((size_t)idx, val);
            }
        } else if (inner.type == VAL_TABLE && inner.as.table) {
            if (MOBIUS_LIKELY(key.type == VAL_STRING))
                inner.as.table->setByString(key.as.string, val);
            else
                inner.as.table->set(key, val);
        } else if (inner.type == VAL_ARRAY_SLICE && inner.as.array_slice) {
            int64_t idx = vm_index_or_neg1(key);
            if (idx >= 0 && idx < (int64_t)inner.as.array_slice->length()) {
                inner.as.array_slice->set((size_t)idx, val);
            } else {
                VM_ERROR(vm, f, "Array slice index %ld out of range [0, %zu)",
                                 (long)idx, inner.as.array_slice->length());
                return -1;
            }
        } else if (inner.type == VAL_BUFFER && inner.as.buffer) {
            int64_t idx = vm_index_or_neg1(key);
            uint8_t byte = 0;
            if (idx < 0) {
                VM_ERROR(vm, f, "Buffer index must be non-negative");
                return -1;
            }
            if (!vm_value_to_byte(val, &byte)) {
                VM_ERROR(vm, f, "Buffer values must be integers in [0, 255]");
                return -1;
            }
            if ((size_t)idx >= inner.as.buffer->size()) {
                if (!inner.as.buffer->resize((size_t)idx + 1, 0)) {
                    VM_ERROR(vm, f, inner.as.buffer->isFixed()
                                     ? "cannot resize fixed buffer"
                                     : "failed to resize buffer");
                    return -1;
                }
            }
            if (!inner.as.buffer->set((size_t)idx, byte)) {
                VM_ERROR(vm, f, "failed to write buffer byte");
                return -1;
            }
        } else if (inner.type == VAL_USERDATA && inner.as.userdata) {
            Table* mt = inner.as.userdata->type_tag
                ? vm->state_->userdataTypeMetatable(inner.as.userdata->type_tag)
                : nullptr;
            int rc = 0;
            if (mt) {
                rc = vm->callTernaryMetamethod(make_retained_table_value(mt),
                                              vm->state_->metamethods()->newindex(),
                                              inner, key, val);
            }
            if (rc == 0) {
                Table* generic = vm->state_->typeMetatable(VAL_USERDATA);
                if (generic) {
                    rc = vm->callTernaryMetamethod(make_retained_table_value(generic),
                                                  vm->state_->metamethods()->newindex(),
                                                  inner, key, val);
                }
            }
            if (rc < 0) return -1;
            if (rc == 0) {
                VM_ERROR(vm, f, "Attempt to assign field on a shared userdata value");
                return -1;
            }
        } else {
            VM_ERROR(vm, f, "Attempt to index a shared %s value", value_type_name(inner.type));
            return -1;
        }
    } else if (MOBIUS_LIKELY(obj.type == VAL_ARRAY && obj.as.array)) {
        int64_t idx = vm_index_or_neg1(key);
        if (MOBIUS_LIKELY(idx >= 0)) {
            ArrayValue* arr = obj.as.array;
            if ((int64_t)arr->length() <= idx && arr->hasActiveSlices()) {
                VM_ERROR(vm, f, "cannot resize array while slices are alive");
                return -1;
            }
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
        int64_t idx = vm_index_or_neg1(key);
        if (idx >= 0 && idx < (int64_t)obj.as.array_slice->length()) {
            obj.as.array_slice->set((size_t)idx, val);
        } else {
            VM_ERROR(vm, f, "Array slice index %ld out of range [0, %zu)",
                             (long)idx, obj.as.array_slice->length());
            return -1;
        }
    } else if (obj.type == VAL_BUFFER && obj.as.buffer) {
        int64_t idx = vm_index_or_neg1(key);
        uint8_t byte = 0;
        if (idx < 0) {
            VM_ERROR(vm, f, "Buffer index must be non-negative");
            return -1;
        }
        if (!vm_value_to_byte(val, &byte)) {
            VM_ERROR(vm, f, "Buffer values must be integers in [0, 255]");
            return -1;
        }
        if ((size_t)idx >= obj.as.buffer->size()) {
            if (!obj.as.buffer->resize((size_t)idx + 1, 0)) {
                VM_ERROR(vm, f, obj.as.buffer->isFixed()
                                 ? "cannot resize fixed buffer"
                                 : "failed to resize buffer");
                return -1;
            }
        }
        if (!obj.as.buffer->set((size_t)idx, byte)) {
            VM_ERROR(vm, f, "failed to write buffer byte");
            return -1;
        }
    } else if (obj.type == VAL_USERDATA && obj.as.userdata) {
        Table* mt = obj.as.userdata->type_tag
            ? vm->state_->userdataTypeMetatable(obj.as.userdata->type_tag)
            : nullptr;
        int rc = 0;
        if (mt) {
            rc = vm->callTernaryMetamethod(make_retained_table_value(mt),
                                          vm->state_->metamethods()->newindex(),
                                          obj, key, val);
        }
        if (rc == 0) {
            Table* generic = vm->state_->typeMetatable(VAL_USERDATA);
            if (generic) {
                rc = vm->callTernaryMetamethod(make_retained_table_value(generic),
                                              vm->state_->metamethods()->newindex(),
                                              obj, key, val);
            }
        }
        if (rc < 0) return -1;
        if (rc == 0) {
            VM_ERROR(vm, f, "Attempt to assign field on a userdata value");
            return -1;
        }
    } else {
        VM_ERROR(vm, f, "Attempt to index a %s value", value_type_name(obj.type));
        return -1;
    }
    return 0;
}

// ---- Array fast paths (OP_AGET / OP_ASET) ----
//
// Emitted when the compiler has inferred the container is an array. They peel
// the plain-array integer-index case out of the generic INDEX_GET/INDEX_SET
// dispatch (which also branches on shared cells, tables, slices, buffers, and
// userdata). Anything the fast path does not recognize falls through to the
// generic handler, so behaviour is identical — the inference only needs to be
// right often enough to be worth the extra opcode.

MOBIUS_FORCEINLINE static int vm_op_aget(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    const Value& obj = RB(inst);
    if (MOBIUS_LIKELY(obj.type == VAL_ARRAY && obj.as.array &&
                      !IS_CONSTANT(DECODE_C(inst)))) {   // C must be a register here
        const Value& key = RC(inst);
        if (MOBIUS_LIKELY(key.type == VAL_INT64)) {
            ArrayValue* arr = obj.as.array;
            int64_t idx = key.as.i64;
            if (MOBIUS_LIKELY(idx >= 0 && idx < (int64_t)arr->length()))
                RA(inst) = arr->unsafeGet((size_t)idx);
            else
                RA(inst) = Value();
            return 0;
        }
    }
    return vm_op_index_get(vm, f, inst);
}

// Fused AGET + INDEX_GET: table lookup keyed by an array element. The fast
// path reads the key straight out of the array — the element stays pinned by
// the array for the whole op and the lookup only reads it, so the register
// copy (and its refcount ticks on heap-string keys) is skipped entirely.
// Any shape mismatch replays both original instructions generically, which
// also materializes the scratch register exactly as unfused code would.
MOBIUS_FORCEINLINE static int vm_op_aget_index_get(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    uint32_t inst2 = *f.ip++;   // original AGET word
    const Value& tbl_v = RB(inst);
    const Value& arr_v = f.regs[DECODE_B(inst2)];
    if (MOBIUS_LIKELY(tbl_v.type == VAL_TABLE && tbl_v.as.table &&
                      arr_v.type == VAL_ARRAY && arr_v.as.array &&
                      !IS_CONSTANT(DECODE_C(inst2)))) {
        const Value& idx_v = f.regs[DECODE_C(inst2)];
        ArrayValue* arr = arr_v.as.array;
        if (MOBIUS_LIKELY(idx_v.type == VAL_INT64 &&
                          idx_v.as.i64 >= 0 && idx_v.as.i64 < (int64_t)arr->length())) {
            const Value& key = arr->unsafeGet((size_t)idx_v.as.i64);
            if (MOBIUS_LIKELY(key.type == VAL_STRING && key.as.string)) {
                const Value* hit = tbl_v.as.table->findString(key.as.string);
                if (MOBIUS_LIKELY(hit != nullptr)) {
                    RA(inst) = *hit;
                    return 0;
                }
            }
        }
    }
    // Generic replay: run the AGET (fills the scratch register), then the
    // INDEX_GET that reads it.
    int rc = vm_op_aget(vm, f, inst2);
    if (MOBIUS_UNLIKELY(rc != 0)) return rc;
    return vm_op_index_get(vm, f, inst);
}

MOBIUS_FORCEINLINE static int vm_op_aset(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    Value& obj = RA(inst);
    if (MOBIUS_LIKELY(obj.type == VAL_ARRAY && obj.as.array)) {
        const Value& key = RKB(inst);
        if (MOBIUS_LIKELY(key.type == VAL_INT64)) {
            ArrayValue* arr = obj.as.array;
            int64_t idx = key.as.i64;
            // In-bounds store into a plain array is the whole fast path.
            // Negative indices, growth, and live-slice checks stay in the
            // generic handler.
            if (MOBIUS_LIKELY(idx >= 0 && idx < (int64_t)arr->length())) {
                arr->set((size_t)idx, RKC(inst));
                return 0;
            }
        }
    }
    return vm_op_index_set(vm, f, inst);
}

// ---- Method self (OP_SELF) ----
MOBIUS_FORCEINLINE static int vm_op_self(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    int a = DECODE_A(inst);
    const Value& obj = RB(inst);
    const Value& key = RKC(inst);

    f.regs[a + 1] = obj;

    if (obj.type == VAL_SHARED_CELL && obj.as.shared_cell) {
        // Snapshot the inner value (retained) and release the cell lock before
        // any method resolution, which may invoke a function __index.
        Value inner;
        {
            std::lock_guard<std::recursive_mutex> lock(obj.as.shared_cell->mutex());
            inner = obj.as.shared_cell->unsafeValue();
        }
        if (inner.type == VAL_USERDATA) {
            f.ci->ip = f.ip;
        uint32_t* saved_ip = f.ip;
            Value method;
            int lookup_rc = vm_userdata_lookup(vm, inner, key, &method);
            vm->refreshFrame(f);
            f.ip = saved_ip;
            f.ci->ip = saved_ip;
            if (lookup_rc < 0) return -1;
            if (method.type != VAL_NIL) {
                f.regs[a] = method;
                return 0;
            }
        } else {
            Table* mt = vm->state_->typeMetatable(inner.type);
            if (mt && key.type == VAL_STRING) {
                const Value& method = mt->getByString(key.as.string);
                if (method.type != VAL_NIL) {
                    f.regs[a] = method;
                    return 0;
                }
            }
        }
        if (inner.type == VAL_TABLE && inner.as.table) {
            Table* tbl = inner.as.table;
            Value method;
            if (MOBIUS_LIKELY(key.type == VAL_STRING))
                method = tbl->getByString(key.as.string);
            else
                method = tbl->get(key);
            if (method.type != VAL_NIL) {
                f.regs[a] = method;
                return 0;
            }
            if (tbl->getMetatable()) {
                f.ci->ip = f.ip;
        uint32_t* saved_ip = f.ip;
                Value looked;
                int rc = vm_index_function_fallback(vm, inner, tbl, key, &looked);
                vm->refreshFrame(f);
                f.ip = saved_ip;
                f.ci->ip = saved_ip;
                if (rc < 0) return -1;
                if (rc > 0) { f.regs[a] = looked; return 0; }
            }
        }
        VM_ERROR(vm, f, "Attempt to call method on a shared %s value", value_type_name(inner.type));
        return -1;
    }

    if (obj.type == VAL_TABLE && obj.as.table) {
        Table* tbl = obj.as.table;
        Value method;
        if (MOBIUS_LIKELY(key.type == VAL_STRING))
            method = tbl->getByString(key.as.string);
        else
            method = tbl->get(key);

        if (method.type != VAL_NIL) {
            f.regs[a] = method;
            return 0;
        }
        // On a miss, follow a *function* __index up the metatable chain.
        if (tbl->getMetatable()) {
            f.ci->ip = f.ip;
        uint32_t* saved_ip = f.ip;
            Value looked;
            int rc = vm_index_function_fallback(vm, obj, tbl, key, &looked);
            vm->refreshFrame(f);
            f.ip = saved_ip;
            f.ci->ip = saved_ip;
            if (rc < 0) return -1;
            if (rc > 0) { f.regs[a] = looked; return 0; }
        }
    }

    Value method;
    // Capture before any callback: `obj` references a register, and the
    // lookup below can re-enter the VM and reallocate registers_.
    ValueType obj_type = obj.type;
    if (obj_type == VAL_USERDATA) {
        Value obj_copy = obj;
        f.ci->ip = f.ip;
        uint32_t* saved_ip = f.ip;
        int lookup_rc = vm_userdata_lookup(vm, obj_copy, key, &method);
        vm->refreshFrame(f);
        f.ip = saved_ip;
        f.ci->ip = saved_ip;
        if (lookup_rc < 0) return -1;
    }
    if (obj_type != VAL_USERDATA) {
        Table* mt = vm->state_->typeMetatable(obj_type);
        if (mt && key.type == VAL_STRING) {
            method = mt->getByString(key.as.string);
        }
    }
    if (method.type != VAL_NIL) {
        f.regs[a] = method;
        return 0;
    }

    VM_ERROR(vm, f, "Attempt to call method on a %s value", value_type_name(obj_type));
    return -1;
}

// ---- Array fast-path ----

// ---- Intrinsified builtins (compiler-proven calls to size/str/concat) ----

MOBIUS_FORCEINLINE static int vm_op_size(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    Value arg = RB(inst);
    if (arg.type == VAL_SHARED_CELL && arg.as.shared_cell)
        arg = arg.as.shared_cell->load();
    int64_t n;
    switch (arg.type) {
        case VAL_STRING:      n = arg.as.string ? (int64_t)arg.as.string->length : 0; break;
        case VAL_ARRAY:       n = arg.as.array ? (int64_t)arg.as.array->length() : 0; break;
        case VAL_ARRAY_SLICE: n = arg.as.array_slice ? (int64_t)arg.as.array_slice->length() : 0; break;
        case VAL_BUFFER:      n = arg.as.buffer ? (int64_t)arg.as.buffer->size() : 0; break;
        case VAL_TABLE:       n = arg.as.table ? (int64_t)arg.as.table->size() : 0; break;
        default:
            VM_ERROR(vm, f, "size expects a string, array, table, or buffer argument");
            return -1;
    }
    RA(inst) = make_int64_value(n);
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_tostr(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    // The compiler never emits this for values that may be tables (the
    // __tostring metamethod path stays on the native call).
    RA(inst) = value_to_string_value(vm->state_, RB(inst));
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_concat2(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    const Value& a = RB(inst);
    const Value& b = RKC(inst);
    if (MOBIUS_UNLIKELY(a.type != VAL_STRING || !a.as.string ||
                        b.type != VAL_STRING || !b.as.string)) {
        VM_ERROR(vm, f, "concat expects all arguments to be strings");
        return -1;
    }
    MobiusString* sa = a.as.string;
    MobiusString* sb = b.as.string;
    if (sa->length == 0) { RA(inst) = make_string_value(sb); return 0; }
    if (sb->length == 0) { RA(inst) = make_string_value(sa); return 0; }
    size_t total = (size_t)sa->length + (size_t)sb->length;
    MobiusString* out = StringInternPool::allocHeap(total);
    if (MOBIUS_UNLIKELY(!out)) {
        VM_ERROR(vm, f, "concat: out of memory");
        return -1;
    }
    char* buf = out->mutableData();
    memcpy(buf, sa->data, sa->length);
    memcpy(buf + sa->length, sb->data, sb->length);
    StringInternPool::finishHeap(out, total);
    RA(inst) = make_string_value_adopt(out);
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_array_push(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    Value& arr_val = RA(inst);
    const Value& val = RB(inst);
    if (arr_val.type == VAL_SHARED_CELL && arr_val.as.shared_cell) {
        std::lock_guard<std::recursive_mutex> lock(arr_val.as.shared_cell->mutex());
        Value& inner = arr_val.as.shared_cell->unsafeValue();
        if (inner.type != VAL_ARRAY || !inner.as.array) {
            VM_ERROR(vm, f, "OP_ARRAY_PUSH applied to non-array (%s)", value_type_name(inner.type));
            return -1;
        }
        if (inner.as.array->hasActiveSlices()) {
            VM_ERROR(vm, f, "cannot resize array while slices are alive");
            return -1;
        }
        inner.as.array->push(val);
    } else if (MOBIUS_LIKELY(arr_val.type == VAL_ARRAY && arr_val.as.array)) {
        if (arr_val.as.array->hasActiveSlices()) {
            VM_ERROR(vm, f, "cannot resize array while slices are alive");
            return -1;
        }
        arr_val.as.array->push(val);
    } else {
        VM_ERROR(vm, f, "OP_ARRAY_PUSH applied to non-array (%s)", value_type_name(arr_val.type));
        return -1;
    }
    return 0;
}

// ---- Arithmetic ----

// Generic arithmetic dispatch — parameterized by the integer/float/unsigned
// operators, the metamethod accessor, and the human-readable verb.
template<typename IntOp, typename UIntOp, typename FloatOp>
MOBIUS_FORCEINLINE static int vm_arith_generic(
        MobiusVM* vm, VMFrame& f, uint32_t inst,
        IntOp int_op, UIntOp uint_op, FloatOp float_op,
        MobiusString* (Metamethods::*mm)() const,
        const char* verb, bool supports_string_concat = false) {
    Value lhs_s, rhs_s;
    const Value& lhs = shared_peek(RKB(inst), lhs_s);
    const Value& rhs = shared_peek(RKC(inst), rhs_s);
    if (MOBIUS_LIKELY(lhs.type == VAL_INT64 && rhs.type == VAL_INT64)) {
        Value& dst = RA(inst);
        dst.as.i64 = int_op(lhs.as.i64, rhs.as.i64);
        dst.type = VAL_INT64; dst.flags = 0;
    } else if (lhs.type == VAL_FLOAT64 && rhs.type == VAL_FLOAT64) {
        Value& dst = RA(inst);
        dst.as.double_val = float_op(lhs.as.double_val, rhs.as.double_val);
        dst.type = VAL_FLOAT64; dst.flags = 0;
    } else if ((lhs.type == VAL_INT64 || lhs.type == VAL_UINT64) &&
               (rhs.type == VAL_INT64 || rhs.type == VAL_UINT64)) {
        if (MobiusVM::vm_use_unsigned(lhs, rhs))
            RA(inst) = make_uint64_value(uint_op(MobiusVM::vm_extract_uint64(lhs),
                                                  MobiusVM::vm_extract_uint64(rhs)));
        else
            RA(inst) = make_int64_value(int_op(MobiusVM::vm_extract_int64(lhs),
                                                MobiusVM::vm_extract_int64(rhs)));
    } else if ((lhs.type == VAL_FLOAT64 || rhs.type == VAL_FLOAT64) &&
               (lhs.type == VAL_INT64 || lhs.type == VAL_UINT64 || lhs.type == VAL_FLOAT64) &&
               (rhs.type == VAL_INT64 || rhs.type == VAL_UINT64 || rhs.type == VAL_FLOAT64)) {
        // Mixed float arithmetic requires BOTH operands numeric. This branch
        // used to fire on `3.5 + "x"`, extracting 0.0 from the string and
        // returning 3.5 instead of falling through to concatenation.
        Value& dst = RA(inst);
        dst.as.double_val = float_op(MobiusVM::vm_extract_double(lhs),
                                     MobiusVM::vm_extract_double(rhs));
        dst.type = VAL_FLOAT64; dst.flags = 0;
    } else if (supports_string_concat && (lhs.type == VAL_STRING || rhs.type == VAL_STRING)) {
        // Build the result as a refcounted heap string, sized up front.
        // The previous implementation appended through a std::string and then
        // INTERNED the result — so every distinct concat was immortal (a leak),
        // cost ~4 allocations + a shard mutex, and `.c_str()` truncated at
        // embedded NULs. Non-string operands are stringified through
        // value_to_string_value, which yields heap strings for the unbounded
        // cases (numbers) and interned ones only for bounded types (nil, bool).
        Value ltmp = (lhs.type == VAL_STRING) ? lhs : value_to_string_value(vm->state_, lhs);
        Value rtmp = (rhs.type == VAL_STRING) ? rhs : value_to_string_value(vm->state_, rhs);
        const MobiusString* ls = (ltmp.type == VAL_STRING) ? ltmp.as.string : nullptr;
        const MobiusString* rs = (rtmp.type == VAL_STRING) ? rtmp.as.string : nullptr;
        size_t ll = ls ? ls->length : 0;
        size_t rl = rs ? rs->length : 0;
        MobiusString* out_s = StringInternPool::allocHeap(ll + rl);
        if (MOBIUS_UNLIKELY(!out_s)) {
            VM_ERROR(vm, f, "Out of memory in string concatenation");
            return -1;
        }
        if (ll) memcpy(out_s->mutableData(), ls->data, ll);
        if (rl) memcpy(out_s->mutableData() + ll, rs->data, rl);
        StringInternPool::finishHeap(out_s, ll + rl);
        RA(inst) = make_string_value_adopt(out_s);
    } else if (lhs.type == VAL_TABLE || rhs.type == VAL_TABLE) {
        // Materialize copies: the metamethod re-enters the VM, which may
        // reallocate registers_ and invalidate the operand references.
        Value lhs_c = lhs, rhs_c = rhs;
        const Value& tbl = (lhs_c.type == VAL_TABLE) ? lhs_c : rhs_c;
        Value out;
        f.ci->ip = f.ip;
        int rc = vm->callMetamethod(tbl, (vm->state_->metamethods()->*mm)(), lhs_c, rhs_c, out);
        vm->refreshFrame(f);
        if (rc < 0) return -1;
        if (rc == 0) {
            VM_ERROR(vm, f, "Cannot %s: no metamethod on table", verb);
            return -1;
        }
        RA(inst) = out;
    } else {
        VM_ERROR(vm, f, "Cannot %s %s and %s", verb,
                 value_type_name(lhs.type), value_type_name(rhs.type));
        return -1;
    }
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_add(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    return vm_arith_generic(vm, f, inst,
        [](int64_t a, int64_t b) { return a + b; },
        [](uint64_t a, uint64_t b) { return a + b; },
        [](double a, double b) { return a + b; },
        &Metamethods::add, "add", true);
}

MOBIUS_FORCEINLINE static int vm_op_sub(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    return vm_arith_generic(vm, f, inst,
        [](int64_t a, int64_t b) { return a - b; },
        [](uint64_t a, uint64_t b) { return a - b; },
        [](double a, double b) { return a - b; },
        &Metamethods::sub, "subtract");
}

MOBIUS_FORCEINLINE static int vm_op_mul(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    return vm_arith_generic(vm, f, inst,
        [](int64_t a, int64_t b) { return a * b; },
        [](uint64_t a, uint64_t b) { return a * b; },
        [](double a, double b) { return a * b; },
        &Metamethods::mul, "multiply");
}

MOBIUS_FORCEINLINE static int vm_op_div(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    Value lhs_s, rhs_s;
    const Value& lhs = shared_peek(RKB(inst), lhs_s);
    const Value& rhs = shared_peek(RKC(inst), rhs_s);
    if (MOBIUS_LIKELY(lhs.type == VAL_INT64 && rhs.type == VAL_INT64)) {
        int64_t rv = rhs.as.i64;
        if (MOBIUS_UNLIKELY(rv == 0)) { VM_ERROR(vm, f, "Division by zero"); return -1; }
        Value& dst = RA(inst);
        dst.as.i64 = lhs.as.i64 / rv;
        dst.type = VAL_INT64; dst.flags = 0;
    } else if (lhs.type == VAL_TABLE || rhs.type == VAL_TABLE) {
        Value lhs_c = lhs, rhs_c = rhs;   // metamethod may realloc registers_
        const Value& tbl = (lhs_c.type == VAL_TABLE) ? lhs_c : rhs_c;
        Value out;
        f.ci->ip = f.ip;
        int rc = vm->callMetamethod(tbl, vm->state_->metamethods()->div(), lhs_c, rhs_c, out);
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
    Value lhs_s, rhs_s;
    const Value& lhs = shared_peek(RKB(inst), lhs_s);
    const Value& rhs = shared_peek(RKC(inst), rhs_s);
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
        Value lhs_c = lhs, rhs_c = rhs;   // metamethod may realloc registers_
        const Value& tbl = (lhs_c.type == VAL_TABLE) ? lhs_c : rhs_c;
        Value out;
        f.ci->ip = f.ip;
        int rc = vm->callMetamethod(tbl, vm->state_->metamethods()->mod(), lhs_c, rhs_c, out);
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
    Value val = shared_unwrap(RB(inst));
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
    RA(inst) = make_bool_value(!is_truthy(shared_unwrap(RB(inst))));
    return 0;
}

// ---- Bitwise ----

MOBIUS_FORCEINLINE static int vm_op_bitwise(MobiusVM* vm, VMFrame& f, uint32_t inst, char op) {
    Value lhs_s, rhs_s;
    const Value& lhs = shared_peek(RKB(inst), lhs_s);
    const Value& rhs = shared_peek(RKC(inst), rhs_s);
    if ((lhs.type != VAL_INT64 && lhs.type != VAL_UINT64) ||
        (rhs.type != VAL_INT64 && rhs.type != VAL_UINT64)) {
        if (lhs.type == VAL_TABLE || rhs.type == VAL_TABLE) {
            Value lhs_c = lhs, rhs_c = rhs;   // metamethod may realloc registers_
            const Value& tbl = (lhs_c.type == VAL_TABLE) ? lhs_c : rhs_c;
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
                int rc = vm->callMetamethod(tbl, mm, lhs_c, rhs_c, out);
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
    Value val = shared_unwrap(RB(inst));
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
    Value lhs_s, rhs_s;
    const Value& lhs = shared_peek(RKB(inst), lhs_s);
    const Value& rhs = shared_peek(RKC(inst), rhs_s);
    bool eq;
    if (lhs.type == VAL_TABLE || rhs.type == VAL_TABLE) {
        const Value& tbl = (lhs.type == VAL_TABLE) ? lhs : rhs;
        // Without a metatable there can be no __eq: skip the metamethod
        // machinery (which costs a hash probe of the table itself) and use
        // identity. Table == with no metamethod is by far the common case.
        if (MOBIUS_LIKELY(!tbl.as.table || !tbl.as.table->getMetatable())) {
            eq = (lhs == rhs);
        } else {
            Value lhs_c = lhs, rhs_c = rhs;   // metamethod may realloc registers_
            Value tbl_c = tbl;
            Value out;
            f.ci->ip = f.ip;
            int rc = vm->callMetamethod(tbl_c, vm->state_->metamethods()->eq(), lhs_c, rhs_c, out);
            vm->refreshFrame(f);
            if (rc < 0) return -1;
            eq = (rc == 1) ? is_truthy(out) : (lhs_c == rhs_c);
        }
    } else {
        eq = (lhs == rhs);
    }
    if (eq != (a != 0)) f.ip++;
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_lt(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    int a = DECODE_A(inst);
    Value lhs_s, rhs_s;
    const Value& lhs = shared_peek(RKB(inst), lhs_s);
    const Value& rhs = shared_peek(RKC(inst), rhs_s);
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
        lt = mobius_string_less(lhs.as.string, rhs.as.string);
    } else if (lhs.type == VAL_TABLE || rhs.type == VAL_TABLE) {
        Value lhs_c = lhs, rhs_c = rhs;   // metamethod may realloc registers_
        const Value& tbl = (lhs_c.type == VAL_TABLE) ? lhs_c : rhs_c;
        Value out;
        f.ci->ip = f.ip;
        int rc = vm->callMetamethod(tbl, vm->state_->metamethods()->lt(), lhs_c, rhs_c, out);
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
    Value lhs_s, rhs_s;
    const Value& lhs = shared_peek(RKB(inst), lhs_s);
    const Value& rhs = shared_peek(RKC(inst), rhs_s);
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
        le = !mobius_string_less(rhs.as.string, lhs.as.string);
    } else if (lhs.type == VAL_TABLE || rhs.type == VAL_TABLE) {
        Value lhs_c = lhs, rhs_c = rhs;   // metamethod may realloc registers_
        const Value& tbl = (lhs_c.type == VAL_TABLE) ? lhs_c : rhs_c;
        Value out;
        f.ci->ip = f.ip;
        int rc = vm->callMetamethod(tbl, vm->state_->metamethods()->le(), lhs_c, rhs_c, out);
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
    bool truthy = is_truthy(shared_unwrap(RA(inst)));
    int c = DECODE_C(inst);
    if (truthy != (c != 0)) f.ip++;
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_testjmp(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    (void)vm;
    if (!is_truthy(shared_unwrap(f.regs[DECODE_A(inst)])))
        f.ip += DECODE_sBx(inst);
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_testset(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    (void)vm;
    Value rb = shared_unwrap(RB(inst));
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
MOBIUS_FORCEINLINE static int vm_op_div_ii(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    int64_t rv = RKC(inst).as.i64;
    if (MOBIUS_UNLIKELY(rv == 0)) { VM_ERROR(vm, f, "Division by zero"); return -1; }
    Value& dst = RA(inst); dst.as.i64 = RKB(inst).as.i64 / rv;
    dst.type = VAL_INT64; dst.flags = 0; return 0;
}
MOBIUS_FORCEINLINE static int vm_op_div_ff(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    double rv = RKC(inst).as.double_val;
    if (MOBIUS_UNLIKELY(rv == 0.0)) { VM_ERROR(vm, f, "Division by zero"); return -1; }
    Value& dst = RA(inst); dst.as.double_val = RKB(inst).as.double_val / rv;
    dst.type = VAL_FLOAT64; dst.flags = 0; return 0;
}
MOBIUS_FORCEINLINE static int vm_op_mod_ff(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    double rv = RKC(inst).as.double_val;
    if (MOBIUS_UNLIKELY(rv == 0.0)) { VM_ERROR(vm, f, "Modulo by zero"); return -1; }
    Value& dst = RA(inst); dst.as.double_val = fmod(RKB(inst).as.double_val, rv);
    dst.type = VAL_FLOAT64; dst.flags = 0; return 0;
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
    Value& dst = RA(inst);
    const Value& src = RB(inst);
    if (&dst == &src && dst.type == VAL_SHARED_CELL && dst.as.shared_cell) {
        std::lock_guard<std::recursive_mutex> lock(dst.as.shared_cell->mutex());
        Value current = dst.as.shared_cell->unsafeValue();
        if (current.type != VAL_INT64 && current.type != VAL_UINT64) {
            VM_ERROR(vm, f, "Increment requires an integer operand");
            return -1;
        }
        bool success;
        Value next = increment_integer(current, true, &success);
        if (!success) { VM_ERROR(vm, f, "Failed to increment value"); return -1; }
        dst.as.shared_cell->unsafeValue() = next;
        return 0;
    }

    Value val = shared_unwrap(src);
    if (val.type != VAL_INT64 && val.type != VAL_UINT64) { VM_ERROR(vm, f, "Increment requires an integer operand"); return -1; }
    bool success;
    dst = increment_integer(val, true, &success);
    if (!success) { VM_ERROR(vm, f, "Failed to increment value"); return -1; }
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_dec(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    Value& dst = RA(inst);
    const Value& src = RB(inst);
    if (&dst == &src && dst.type == VAL_SHARED_CELL && dst.as.shared_cell) {
        std::lock_guard<std::recursive_mutex> lock(dst.as.shared_cell->mutex());
        Value current = dst.as.shared_cell->unsafeValue();
        if (current.type != VAL_INT64 && current.type != VAL_UINT64) {
            VM_ERROR(vm, f, "Decrement requires an integer operand");
            return -1;
        }
        bool success;
        Value next = increment_integer(current, false, &success);
        if (!success) { VM_ERROR(vm, f, "Failed to decrement value"); return -1; }
        dst.as.shared_cell->unsafeValue() = next;
        return 0;
    }

    Value val = shared_unwrap(src);
    if (val.type != VAL_INT64 && val.type != VAL_UINT64) { VM_ERROR(vm, f, "Decrement requires an integer operand"); return -1; }
    bool success;
    dst = increment_integer(val, false, &success);
    if (!success) { VM_ERROR(vm, f, "Failed to decrement value"); return -1; }
    return 0;
}

// ---- Type checking ----

MOBIUS_FORCEINLINE static int vm_op_typecheck(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    NumberType target = (NumberType)DECODE_B(inst);
    Value input = shared_unwrap(RA(inst));
    TypeCheckConfig tc = {
        vm->strict_mode_,
        vm->warn_on_conversion_
    };
    TypeConversionResult conv = validate_and_convert_value(input, target, true, tc);
    if (!conv.success) {
        VM_ERROR(vm, f, "%s", conv.error_message ? conv.error_message : "Type validation failed");
        free(conv.error_message);
        return -1;
    }
    if (conv.was_converted && vm->warn_on_conversion_) {
        fprintf(stderr, "Warning: Implicit type conversion at line %d\n", vm->currentLine());
    }
    if (!shared_store(RA(inst), conv.converted_value)) {
        RA(inst) = conv.converted_value;
    }
    free(conv.error_message);
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_isnum(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    (void)vm;
    Value v = shared_unwrap(RB(inst));
    RA(inst) = make_bool_value(v.type == VAL_INT64 || v.type == VAL_FLOAT64);
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_typecompat(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    (void)vm;
    int a = DECODE_A(inst);
    Value lhs_s, rhs_s;
    const Value& lhs = shared_peek(RKB(inst), lhs_s);
    const Value& rhs = shared_peek(RKC(inst), rhs_s);
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

// Fused ADD + TYPECHECK_LOCKED (the `locked_var = locked_var + x` loop
// accumulator pattern): one dispatch instead of two. Defined after both
// constituent handlers — no forward declarations (declaring an always_inline
// function before its body poisons its inlining at every call site).
MOBIUS_FORCEINLINE static int vm_op_add_check(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    uint32_t inst2 = *f.ip++;   // original TYPECHECK_LOCKED word
    // Accumulator fast path: int64 += int64 into an int64-locked (or not-
    // yet-locked) register — the shape of every `sum = sum + x` loop where
    // x's type can't be proven at compile time. One branch tree, no generic
    // dispatch, and the lock check collapses into the same test.
    int b = DECODE_B(inst), c = DECODE_C(inst);
    if (MOBIUS_LIKELY(!IS_CONSTANT(b) && !IS_CONSTANT(c))) {
        const Value& lhs = f.regs[b];
        const Value& rhs = f.regs[c];
        if (MOBIUS_LIKELY(lhs.type == VAL_INT64 && rhs.type == VAL_INT64)) {
            int a = DECODE_A(inst);
            ValueType tag = f.tags[DECODE_A(inst2)];
            if (MOBIUS_LIKELY(tag == VAL_INT64 || tag == VAL_UNKNOWN)) {
                int64_t r = lhs.as.i64 + rhs.as.i64;
                Value& dst = f.regs[a];
                dst = Value();             // releases if dst held a ref type
                dst.type = VAL_INT64;
                dst.as.i64 = r;
                if (tag == VAL_UNKNOWN) f.tags[DECODE_A(inst2)] = VAL_INT64;
                return 0;
            }
        }
    }
    int rc = vm_op_add(vm, f, inst);
    if (MOBIUS_UNLIKELY(rc != 0)) return rc;
    return vm_op_typecheck_locked(vm, f, inst2);
}

// Fused AGET + ADD + TYPECHECK_LOCKED: `sum = sum + arr[i]` in one dispatch.
// Three words: this op (A = accumulator), the original AGET, the original
// TYPECHECK. The fast path never materializes the element in a register.
MOBIUS_FORCEINLINE static int vm_op_aget_add_check(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    uint32_t inst2 = *f.ip++;   // AGET word: A=scratch B=arr C=idx
    uint32_t inst3 = *f.ip++;   // TYPECHECK word: A=lock reg
    const Value& arr_v = f.regs[DECODE_B(inst2)];
    int a = DECODE_A(inst);
    Value& sum = f.regs[a];
    if (MOBIUS_LIKELY(arr_v.type == VAL_ARRAY && arr_v.as.array &&
                      !IS_CONSTANT(DECODE_C(inst2)) &&
                      sum.type == VAL_INT64)) {
        const Value& idx_v = f.regs[DECODE_C(inst2)];
        ArrayValue* arr = arr_v.as.array;
        if (MOBIUS_LIKELY(idx_v.type == VAL_INT64 &&
                          idx_v.as.i64 >= 0 && idx_v.as.i64 < (int64_t)arr->length())) {
            const Value& elem = arr->unsafeGet((size_t)idx_v.as.i64);
            ValueType tag = f.tags[DECODE_A(inst3)];
            if (MOBIUS_LIKELY(elem.type == VAL_INT64 &&
                              (tag == VAL_INT64 || tag == VAL_UNKNOWN))) {
                sum.as.i64 += elem.as.i64;
                if (tag == VAL_UNKNOWN) f.tags[DECODE_A(inst3)] = VAL_INT64;
                return 0;
            }
        }
    }
    // Generic replay: AGET into its scratch register, then the accumulator
    // add (A = B = sum, C = the scratch), then the lock check.
    int rc = vm_op_aget(vm, f, inst2);
    if (MOBIUS_UNLIKELY(rc != 0)) return rc;
    uint32_t synth_add = ENCODE_ABC(OP_ADD, (uint8_t)a, (uint8_t)a, DECODE_A(inst2));
    rc = vm_op_add(vm, f, synth_add);
    if (MOBIUS_UNLIKELY(rc != 0)) return rc;
    return vm_op_typecheck_locked(vm, f, inst3);
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
    const Value& raw = RB(inst);
    if (raw.type == VAL_SHARED_CELL && raw.as.shared_cell) {
        std::lock_guard<std::recursive_mutex> lock(raw.as.shared_cell->mutex());
        const Value& inner = raw.as.shared_cell->unsafeValue();
        if (inner.type == VAL_ARRAY && inner.as.array) {
            RA(inst) = make_int64_value((int64_t)inner.as.array->length());
        } else if (inner.type == VAL_TABLE && inner.as.table) {
            RA(inst) = make_int64_value((int64_t)inner.as.table->size());
        } else if (inner.type == VAL_ARRAY_SLICE && inner.as.array_slice) {
            RA(inst) = make_int64_value((int64_t)inner.as.array_slice->length());
        } else if (inner.type == VAL_STRING && inner.as.string) {
            RA(inst) = make_int64_value((int64_t)inner.as.string->length);
        } else if (inner.type == VAL_BUFFER && inner.as.buffer) {
            RA(inst) = make_int64_value((int64_t)inner.as.buffer->size());
        } else {
            VM_ERROR(vm, f, "Attempt to get length of a shared %s value", value_type_name(inner.type));
            return -1;
        }
    } else if (raw.type == VAL_ARRAY && raw.as.array) {
        RA(inst) = make_int64_value((int64_t)raw.as.array->length());
    } else if (raw.type == VAL_TABLE && raw.as.table) {
        RA(inst) = make_int64_value((int64_t)raw.as.table->size());
    } else if (raw.type == VAL_STRING && raw.as.string) {
        RA(inst) = make_int64_value((int64_t)raw.as.string->length);
    } else if (raw.type == VAL_ARRAY_SLICE && raw.as.array_slice) {
        RA(inst) = make_int64_value((int64_t)raw.as.array_slice->length());
    } else if (raw.type == VAL_BUFFER && raw.as.buffer) {
        RA(inst) = make_int64_value((int64_t)raw.as.buffer->size());
    } else {
        VM_ERROR(vm, f, "Attempt to get length of a %s value", value_type_name(raw.type));
        return -1;
    }
    return 0;
}

// ---- For loops ----

// Validate one IFOR register (index/limit). The compiler only emits the IFOR
// shape when start and limit statically infer as int64, so this guard is a
// backstop for cases the inference can't see (a shared cell slipped through,
// a wrong annotation). Unwraps cells; coerces integral floats; errors rather
// than reinterpreting float bits as a trip count.
static bool ifor_coerce_int64(Value& v) {
    if (MOBIUS_LIKELY(v.type == VAL_INT64)) return true;
    if (v.type == VAL_SHARED_CELL && v.as.shared_cell) {
        v = v.as.shared_cell->load();
        return ifor_coerce_int64(v);
    }
    if (v.type == VAL_UINT64 && v.as.u64 <= (uint64_t)INT64_MAX) {
        v = make_int64_value((int64_t)v.as.u64);
        return true;
    }
    if (v.type == VAL_FLOAT64) {
        double d = v.as.double_val;
        int64_t i = (int64_t)d;
        if ((double)i == d) { v = make_int64_value(i); return true; }
    }
    return false;
}

MOBIUS_FORCEINLINE static int vm_op_iforprep(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    int a = DECODE_A(inst);
    if (MOBIUS_UNLIKELY(!ifor_coerce_int64(f.regs[a]) ||
                        !ifor_coerce_int64(f.regs[a + 1]))) {
        VM_ERROR(vm, f, "for loop bounds must be integers (got %s and %s)",
                 value_type_name(f.regs[a].type), value_type_name(f.regs[a + 1].type));
        return -1;
    }
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

MOBIUS_FORCEINLINE static int vm_op_move_muli(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    RA(inst) = RB(inst);
    uint32_t inst2 = *f.ip++;
    int a2 = DECODE_A(inst2);
    int imm = DECODE_sBx(inst2);
    Value& val = f.regs[a2];
    if (MOBIUS_LIKELY(val.type == VAL_INT64)) { val.as.i64 *= imm; }
    else if (val.type == VAL_UINT64) val.as.u64 *= (uint64_t)(int64_t)imm;
    else if (val.type == VAL_FLOAT64) val.as.double_val *= imm;
    else { VM_ERROR(vm, f, "MULI requires numeric operand"); return -1; }
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_getglobal_index_get(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    int a = DECODE_A(inst);
    int slot = DECODE_Bx(inst);
    GlobalEnvironment* globals = frame_globals(vm, f);
    if (MOBIUS_UNLIKELY(!vm->state_->copyGlobalValue(slot, &f.regs[a], globals) ||
                        !(f.regs[a].flags & VAL_FLAG_DEFINED))) {
        VM_ERROR(vm, f, "Undefined variable '%s'", vm->state_->globalSlotName(slot, globals));
        return -1;
    }
    // The second fused word is the original access instruction (INDEX_GET
    // or GETFIELD — the peephole fuses both). Delegate by ITS opcode rather
    // than re-implementing indexing: an earlier inline copy here silently
    // diverged from INDEX_GET (no function-__index fallback, no enum access,
    // no slice indexing), so whether those features worked depended on
    // whether the peephole had fused the pair. The fusion's win — one
    // dispatch saved and the fused global load — is preserved.
    uint32_t inst2 = *f.ip++;
    if (DECODE_OP(inst2) == OP_GETFIELD)
        return vm_op_getfield(vm, f, inst2);
    return vm_op_index_get(vm, f, inst2);
}

// ---- Call / Tailcall ----
MOBIUS_FORCEINLINE static int vm_call_direct_impl(MobiusVM* vm, VMFrame& f, uint32_t inst,
                                                  bool prepare_params) {
    int a = DECODE_A(inst);
    uint16_t proto_idx = DECODE_Bx(inst);
    uint32_t inst2 = *f.ip++;
    int b = DECODE_B(inst2);
    int c = DECODE_C(inst2);
    int nargs = b - 1;

    Prototype* child = nullptr;
    if (proto_idx == BX_MASK) {
        child = f.ci->proto;                              // self-recursion
    } else if (proto_idx & 0x8000u) {                     // readonly global function
        uint16_t ext = proto_idx & 0x7FFFu;
        if ((size_t)ext < f.ci->proto->extern_protos.size())
            child = f.ci->proto->extern_protos[ext];
    } else if ((size_t)proto_idx < f.ci->proto->protos.size()) {
        child = f.ci->proto->protos[proto_idx];           // nested prototype
    }

    if (!child) {
        VM_ERROR(vm, f, "Direct call target is invalid");
        return -1;
    }
    if (MOBIUS_UNLIKELY(!child->upvalues.empty())) {
        VM_ERROR(vm, f, "Direct call target '%s' requires a closure environment",
                         child->name.empty() ? "anonymous" : child->name.c_str());
        return -1;
    }
    if (MOBIUS_UNLIKELY(child->num_params != nargs)) {
        VM_ERROR(vm, f, "Function '%s' expects %d arguments but got %d",
                         child->name.empty() ? "anonymous" : child->name.c_str(),
                         child->num_params, nargs);
        return -1;
    }

    int child_base = f.ci->base + a + 1;
    vm->ensureRegisters(child_base + child->num_registers + 16);

    if (prepare_params && !child->param_unwrap_on_entry.empty()) {
        for (int i = 0; i < nargs; i++) {
            Value& arg = vm->registers_[child_base + i];
            if (i < (int)child->param_unwrap_on_entry.size() &&
                child->param_unwrap_on_entry[i] &&
                arg.type == VAL_SHARED_CELL && arg.as.shared_cell) {
                arg = shared_unwrap(arg);
            }
        }
    }

    if (MOBIUS_UNLIKELY(child->has_type_locks)) {
        memset(&vm->type_tags_[child_base], (uint8_t)VAL_UNKNOWN, child->num_registers);
    }

    f.ci->ip = f.ip;
    CallInfo& new_ci = vm->callStackPush(child, child_base, c);
    enter_child_frame(vm, f, &new_ci, child, child_base);
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_call_impl(MobiusVM* vm, VMFrame& f, uint32_t inst,
                                              bool prepare_params) {
    if (MOBIUS_UNLIKELY(g_gc_shadow_mode | (int)g_gc_pending)) gc_safepoint(vm);
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

        if (prepare_params && !child->param_unwrap_on_entry.empty()) {
            for (int i = 0; i < nargs; i++) {
                Value& arg = vm->registers_[child_base + i];
                if (i < (int)child->param_unwrap_on_entry.size() &&
                    child->param_unwrap_on_entry[i] &&
                    arg.type == VAL_SHARED_CELL && arg.as.shared_cell) {
                    arg = shared_unwrap(arg);
                }
            }
        }

        if (MOBIUS_UNLIKELY(child->has_type_locks)) {
            memset(&vm->type_tags_[child_base], (uint8_t)VAL_UNKNOWN, child->num_registers);
        }

        CallInfo& new_ci = vm->callStackPush(child, child_base, c);
        if (mf->upvalues && mf->upvalue_count > 0) {
            if (!new_ci.setUpvaluesFrom(mf->upvalues, mf->upvalue_count)) {
                // The pushed frame is the backtrace top but has no ip yet
                // (reset() no longer initializes it); give it one before
                // reporting so currentLine() reads defined memory.
                new_ci.ip = child->code.data();
                VM_ERROR(vm, f, "Failed to allocate closure upvalues");
                return -1;
            }
        }

        enter_child_frame(vm, f, &new_ci, child, child_base);
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

MOBIUS_FORCEINLINE static int vm_op_call(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    return vm_op_call_impl(vm, f, inst, true);
}

MOBIUS_FORCEINLINE static int vm_op_call_plain(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    return vm_op_call_impl(vm, f, inst, false);
}

MOBIUS_FORCEINLINE static int vm_op_getglobal_call(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    int a = DECODE_A(inst);
    int slot = DECODE_Bx(inst);
    GlobalEnvironment* globals = frame_globals(vm, f);
    if (MOBIUS_UNLIKELY(!vm->state_->copyGlobalValue(slot, &f.regs[a], globals) ||
                        !(f.regs[a].flags & VAL_FLAG_DEFINED))) {
        VM_ERROR(vm, f, "Undefined variable '%s'", vm->state_->globalSlotName(slot, globals));
        return -1;
    }
    uint32_t inst2 = *f.ip++;
    return vm_op_call(vm, f, inst2);
}

MOBIUS_FORCEINLINE static int vm_op_getglobal_call_plain(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    int a = DECODE_A(inst);
    int slot = DECODE_Bx(inst);
    GlobalEnvironment* globals = frame_globals(vm, f);
    if (MOBIUS_UNLIKELY(!vm->state_->copyGlobalValue(slot, &f.regs[a], globals) ||
                        !(f.regs[a].flags & VAL_FLAG_DEFINED))) {
        VM_ERROR(vm, f, "Undefined variable '%s'", vm->state_->globalSlotName(slot, globals));
        return -1;
    }
    uint32_t inst2 = *f.ip++;
    return vm_op_call_plain(vm, f, inst2);
}

MOBIUS_FORCEINLINE static int vm_op_call_direct(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    return vm_call_direct_impl(vm, f, inst, true);
}

MOBIUS_FORCEINLINE static int vm_op_call_direct_plain(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    return vm_call_direct_impl(vm, f, inst, false);
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
    Value* args = (nargs <= 256) ? arg_buf : new (std::nothrow) Value[nargs];
    if (!args) {
        VM_ERROR(vm, f, "Failed to allocate argument buffer");
        return -1;
    }
    int src_base = f.ci->base + a + 1;
    for (int i = 0; i < nargs; i++) {
        args[i] = prepare_param_value(child, i, vm->registers_[src_base + i]);
    }

    if (MOBIUS_UNLIKELY(f.ci->upvalue_count > 0)) vm->closeUpvalues(*f.ci, 0);

    for (int i = 0; i < nargs; i++) {
        vm->registers_[f.ci->base + i] = args[i];
    }
    if (args != arg_buf) delete[] args;

    vm->ensureRegisters(f.ci->base + child->num_registers + 16);
    if (MOBIUS_UNLIKELY(child->has_type_locks)) {
        memset(&vm->type_tags_[f.ci->base], (uint8_t)VAL_UNKNOWN, child->num_registers);
    }
    for (int i = nargs; i < child->num_registers; i++) {
        vm->registers_[f.ci->base + i] = Value();
    }

    f.ci->proto = child;
    f.ci->ip = child->code.data();
    f.ci->clearUpvalues();
    if (mf->upvalues && mf->upvalue_count > 0) {
        if (!f.ci->setUpvaluesFrom(mf->upvalues, mf->upvalue_count)) {
            VM_ERROR(vm, f, "Failed to allocate closure upvalues");
            return -1;
        }
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
    int ret_regs = f.ci->proto ? f.ci->proto->num_registers : 0;
    vm->callStackPop();
    // Discard try handlers registered by the frame being returned from
    // (i.e. `return` inside `try`). A stale handler would catch a later,
    // unrelated throw and jump into the dead frame's code. The compiler also
    // emits TRY_END before returns; this is the backstop for paths it can't
    // see (native unwinds, tailcalls).
    if (MOBIUS_UNLIKELY(!vm->try_stack_.empty())) {
        while (!vm->try_stack_.empty() &&
               vm->try_stack_.back().call_stack_depth > vm->callStackSize()) {
            release_atomic_locks(vm, vm->try_stack_.back().atomic_depth);
            vm->try_stack_.pop_back();
        }
    }
    int func_reg_abs = ret_base - 1;
    if (MOBIUS_LIKELY(ret_nresults == 2)) {
        // One expected result — every plain `x = f(...)` call site.
        if (MOBIUS_LIKELY(nresults_available >= 1))
            vm->regs_data_[func_reg_abs] = vm->regs_data_[ret_base + a];
        else
            vm->regs_data_[func_reg_abs] = Value();
    } else if (ret_nresults != 0) {
        int to_copy = ret_nresults - 1;
        for (int i = 0; i < to_copy; i++) {
            if (i < nresults_available)
                vm->registers_[func_reg_abs + i] = vm->registers_[ret_base + a + i];
            else
                vm->registers_[func_reg_abs + i] = Value();
        }
    }
    if (vm->callStackSize() <= base_depth) return 1;

    // Release the dead frame's register window. Registers hold owning
    // references, so without this a returned frame keeps pinning its dead
    // locals until some later call happens to overwrite the slots — in
    // practice unbounded (a wide frame could pin its whole working set for
    // the rest of the program). Results were already copied below ret_base.
    // Only slots holding refcounted values do any work; the releases are
    // merely moved earlier, not added.
    Value* dead = vm->regs_data_ + ret_base;
    for (int i = 0; i < ret_regs; i++) {
        if (dead[i].type >= VAL_FIRST_REFCOUNTED) dead[i] = Value();
    }
    vm->refreshFrame(f);
    return 0;
}

// ---- Closure ----
MOBIUS_FORCEINLINE static int vm_op_closure(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    if (MOBIUS_UNLIKELY(g_gc_pending)) gc_safepoint(vm);
    uint16_t bx = DECODE_Bx(inst);
    if (bx >= f.proto->protos.size()) {
        VM_ERROR(vm, f, "Invalid prototype index %d", bx);
        return -1;
    }
    Prototype* child_proto = f.proto->protos[bx];
    MobiusFunction* mf = new (std::nothrow) MobiusFunction();
    if (!mf) {
        VM_ERROR(vm, f, "Failed to allocate function closure");
        return -1;
    }
    auto cleanup_function = [&]() {
        delete[] mf->param_names;
        if (mf->upvalues) {
            for (int u2 = 0; u2 < mf->upvalue_count; u2++)
                if (mf->upvalues[u2]) mf->upvalues[u2]->release();
            delete[] mf->upvalues;
        }
        delete mf;
    };
    mf->name = child_proto->name.empty() ? nullptr :
               vm->state_->stringPool()->intern(child_proto->name.c_str());
    mf->param_count = child_proto->num_params;
    mf->body = nullptr;
    mf->body_count = 0;
    mf->ref_count.store(1, std::memory_order_relaxed);
    mf->proto = child_proto;
    mf->param_names = nullptr;
    mf->upvalues = nullptr;
    mf->upvalue_count = 0;
    if (child_proto->num_params > 0 && !child_proto->local_vars.empty()) {
        mf->param_names = new (std::nothrow) MobiusString*[child_proto->num_params]();
        if (!mf->param_names) {
            cleanup_function();
            VM_ERROR(vm, f, "Failed to allocate function parameter names");
            return -1;
        }
        for (int i = 0; i < child_proto->num_params && i < (int)child_proto->local_vars.size(); i++) {
            mf->param_names[i] = vm->state_->stringPool()->intern(child_proto->local_vars[i].name.c_str());
        }
    }
    int nupvals = (int)child_proto->upvalues.size();
    if (nupvals > 0) {
        mf->upvalues = new (std::nothrow) Upvalue*[nupvals]();
        if (!mf->upvalues) {
            cleanup_function();
            VM_ERROR(vm, f, "Failed to allocate function upvalue table");
            return -1;
        }
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
                    existing->retain();              // mf's own reference
                    mf->upvalues[u] = existing;
                } else {
                    Upvalue* uv = new (std::nothrow) Upvalue();
                    if (!uv) {
                        cleanup_function();
                        VM_ERROR(vm, f, "Failed to allocate open upvalue");
                        return -1;
                    }
                    uv->location = reg_ptr;
                    uv->is_open = true;
                    if (!f.ci->pushUpvalue(uv)) {
                        uv->release();               // drop the creation reference
                        cleanup_function();
                        VM_ERROR(vm, f, "Failed to grow open upvalue list");
                        return -1;
                    }
                    if ((size_t)f.ci->upvalue_count > vm->metrics_->peak_upvalues)
                        vm->metrics_->peak_upvalues = (size_t)f.ci->upvalue_count;
                    mf->upvalues[u] = uv;
                }
            } else {
                if (desc.index < f.ci->upvalue_count) {
                    f.ci->upvalues[desc.index]->retain();   // mf's own reference
                    mf->upvalues[u] = f.ci->upvalues[desc.index];
                } else {
                    mf->upvalues[u] = new (std::nothrow) Upvalue();
                    if (!mf->upvalues[u]) {
                        cleanup_function();
                        VM_ERROR(vm, f, "Failed to allocate closed upvalue");
                        return -1;
                    }
                }
            }
        }
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
    // Request two results (nresults = count + 1): a two-variable
    // `for k, v in iterFn` previously requested one and never wrote the
    // second variable, which kept whatever stale value occupied its register.
    int rc = vm->callFunction(vm->callStackTop(), call_reg, 2, 3);
    if (rc < 0) return -1;

    if (rc > 0) {
        rc = vm->run(vm->callStackSize() - 1);
        if (rc < 0) return -1;
    }

    vm->refreshFrame(f);

    Value r1 = f.regs[call_reg];
    Value r2 = f.regs[call_reg + 1];
    if (r1.type == VAL_NIL) {
        return 0;
    }

    f.regs[a + 2] = r1;          // control value for the next call
    f.regs[a + 3] = r1;          // first user variable
    if (c >= 2) f.regs[a + 4] = r2;

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
        const std::string& registry_error = registry->lastError();
        if (!registry_error.empty()) {
            VM_ERROR(vm, f, "%s", registry_error.c_str());
        } else {
            VM_ERROR(vm, f, "Import failed - module '%s' not found", module_name);
        }
        return -1;
    }

    if (is_global) {
        GlobalEnvironment* globals = frame_globals(vm, f);
        // Spread table entries into individual globals
        const auto& entries = mod_table->entries();
        const auto& tags = mod_table->tags();
        for (size_t i = 0; i < entries.size(); i++) {
            if (tags[i] == Table::TAG_EMPTY) continue;
            const Value& key = entries[i].key;
            if (key.type != VAL_STRING || !key.as.string) continue;
            Value val = entries[i].value;
            val.flags |= VAL_FLAG_DEFINED;
            int slot = vm->state_->assignGlobalSlot(key.as.string->data, globals);
            if (slot < 0) {
                VM_ERROR(vm, f, "Global slot capacity exceeded while importing '%s'", module_name);
                return -1;
            }
            vm->state_->setGlobalValue(slot, val, globals, false);
        }
    } else if (strchr(alias_name, '.') != nullptr) {
        GlobalEnvironment* globals = frame_globals(vm, f);
        // Dotted path: walk/create nested tables, put module table at the leaf
        std::vector<std::string> components;
        components.reserve(8);
        const char* part_start = alias_name;
        const char* cursor = alias_name;
        while (true) {
            if (*cursor == '.' || *cursor == '\0') {
                if (cursor == part_start) {
                    VM_ERROR(vm, f, "Invalid import alias '%s'", alias_name);
                    return -1;
                }
                components.emplace_back(part_start, (size_t)(cursor - part_start));
                if (*cursor == '\0') break;
                part_start = cursor + 1;
                if (components.size() >= 32) {
                    VM_ERROR(vm, f, "Import alias '%s' is too deeply nested", alias_name);
                    return -1;
                }
            }
            cursor++;
        }
        int ncomps = (int)components.size();
        if (ncomps == 0) {
            VM_ERROR(vm, f, "Invalid import alias '%s'", alias_name);
            return -1;
        }

        Table* cur_table = nullptr;
        int ns_slot = vm->state_->findGlobalSlot(components[0].c_str(), globals);
        Value first_value = (ns_slot >= 0) ? vm->state_->getGlobalValue(ns_slot, globals)
                                           : make_nil_value();
        bool first_exists = (ns_slot >= 0 && (first_value.flags & VAL_FLAG_DEFINED));
        if (first_exists && first_value.type == VAL_TABLE) {
            cur_table = first_value.as.table;
        } else if (first_exists) {
            VM_ERROR(vm, f, "Cannot create nested namespace '%s': '%s' is not a table",
                             alias_name, components[0].c_str());
            return -1;
        } else {
            cur_table = new (std::nothrow) Table(vm->state_, 16);
            if (!cur_table) { VM_ERROR(vm, f, "Failed to create namespace table"); return -1; }
            Value tval = make_table_value(cur_table);
            tval.flags |= VAL_FLAG_DEFINED;
            int s = vm->state_->assignGlobalSlot(components[0].c_str(), globals);
            if (s < 0) {
                VM_ERROR(vm, f, "Global slot capacity exceeded while importing '%s'", module_name);
                return -1;
            }
            vm->state_->setGlobalValue(s, tval, globals, false);
        }

        // Walk/create intermediate tables
        for (int i = 1; i < ncomps - 1; i++) {
            Value key = make_string_value_from_cstr(vm->state_, components[i].c_str());
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
            Value leaf_key = make_string_value_from_cstr(vm->state_, components[ncomps - 1].c_str());
            cur_table->set(leaf_key, make_retained_table_value(mod_table));
        }
    } else {
        GlobalEnvironment* globals = frame_globals(vm, f);
        // Simple alias: bind module table to a single global
        Value tval = make_retained_table_value(mod_table);
        tval.flags |= VAL_FLAG_DEFINED;
        int s = vm->state_->assignGlobalSlot(alias_name, globals);
        if (s < 0) {
            VM_ERROR(vm, f, "Global slot capacity exceeded while importing '%s'", module_name);
            return -1;
        }
        vm->state_->setGlobalValue(s, tval, globals, false);
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
    tb.atomic_depth = vm->atomic_locks_.size();
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

MOBIUS_FORCEINLINE static int vm_op_throw(MobiusVM* vm, VMFrame& f, uint32_t inst, size_t base_depth) {
    uint8_t a = DECODE_A(inst);
    Value thrown_value = f.regs[a];

    // A handler is usable only if it was registered by a frame belonging to
    // THIS run() invocation (call_stack_depth > base_depth). Handlers below
    // base_depth belong to an outer dispatch loop — we are inside a nested
    // run() started by a metamethod or iterator call. Unwinding to them from
    // here would pop past this run's base and leave it executing outer-frame
    // code; instead convert to an error and propagate out through the nested
    // run's caller, so each run() level unwinds itself.
    if (vm->try_stack_.empty() ||
        vm->try_stack_.back().call_stack_depth <= base_depth) {
        if (thrown_value.type == VAL_STRING && thrown_value.as.string) {
            VM_ERROR(vm, f, "%s", thrown_value.as.string->data);
        } else {
            char* s = value_to_string(thrown_value);
            VM_ERROR(vm, f, "%s", s ? s : "Uncaught exception");
            free(s);
        }
        return -1;
    }

    MobiusVM::TryBlock& tb = vm->try_stack_.back();
    release_atomic_locks(vm, tb.atomic_depth);

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

// A value crosses the spawn boundary by deep copy only if it's a *non-shared*
// array or table. Shared containers (already mutex-protected), shared cells,
// array spans, scalars, and functions are passed by reference so explicit
// sharing is preserved across fibers.
static inline bool value_needs_spawn_copy(const Value& v) {
    if ((v.type == VAL_ARRAY && v.as.array) || (v.type == VAL_TABLE && v.as.table)) {
        return (v.flags & VAL_FLAG_SHARED) == 0;
    }
    return false;
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
        FutureValue* future = new FutureValue();

        std::vector<Value> args;
        args.reserve(nargs);
        for (int i = 0; i < nargs; i++) {
            const Value& arg = f.regs[b + 1 + i];
            if (value_needs_spawn_copy(arg)) {
                args.push_back(deep_copy_value_for_spawn(arg));
            } else {
                args.push_back(arg);
            }
        }

        // Snapshot captured upvalues using the same value semantics as
        // arguments: non-shared arrays/tables are deep-copied (each fiber gets
        // its own), shared cells and spans pass by reference, scalars copy.
        std::vector<Value> upvalue_snapshot;
        upvalue_snapshot.reserve(mf->upvalue_count);
        for (int i = 0; i < mf->upvalue_count; i++) {
            Upvalue* uv = mf->upvalues[i];
            Value captured = (uv && uv->location) ? *uv->location : Value();
            if (value_needs_spawn_copy(captured)) {
                upvalue_snapshot.push_back(deep_copy_value_for_spawn(captured));
            } else {
                upvalue_snapshot.push_back(captured);
            }
        }

        Prototype* proto = mf->proto;
        MobiusState* state = vm->state_;

        ((RefCounted*)future)->retain();

        JobDecl job;
        job.entry = [state, proto, args_copy = std::move(args),
                     upvals = std::move(upvalue_snapshot), future]() mutable {
            MobiusVM fiber_vm(state);
            fiber_vm.future_ = future;
            int nargs = (int)args_copy.size();
            int needed = proto->num_registers + nargs + 256 + 1;
            fiber_vm.ensureRegisters(needed);

            int base = 1;
            for (int i = 0; i < nargs; i++) {
                fiber_vm.registers_[base + i] = prepare_param_value(proto, i, args_copy[i]);
            }

            if (MOBIUS_UNLIKELY(proto->has_type_locks)) {
                memset(&fiber_vm.type_tags_[base], (uint8_t)VAL_UNKNOWN, proto->num_registers);
            }

            ScopedCurrentVM bind_vm(&fiber_vm);

            JobSystem* js = state->jobSystem();
            if (js) {
                MobiusFiber* self = js->currentFiber();
                if (self) self->vm = &fiber_vm;
            }

            size_t depth_before = fiber_vm.callStackSize();
            fiber_vm.callStackPush(proto, base, 2).ip = proto->code.data();

            // Recreate the captured upvalues as closed cells owned by this
            // fiber, and attach them to the running frame so OP_GETUPVAL /
            // OP_SETUPVAL resolve. Closed upvalues are skipped by closeUpvalues
            // at frame pop, so this fiber owns and frees them after run().
            std::vector<Upvalue*> fiber_upvalues;
            if (!upvals.empty()) {
                fiber_upvalues.reserve(upvals.size());
                for (Value& v : upvals) {
                    Upvalue* uv = new (std::nothrow) Upvalue();
                    if (!uv) break;
                    uv->closed = v;
                    uv->location = &uv->closed;
                    uv->is_open = false;
                    fiber_upvalues.push_back(uv);
                }
                fiber_vm.callStackTop().setUpvaluesFrom(fiber_upvalues.data(),
                                                        (int)fiber_upvalues.size());
            }

            uint64_t t0 = get_time_ns_vm();
            int rc = fiber_vm.run(depth_before);
            fiber_vm.metrics_->total_execution_time_ns += get_time_ns_vm() - t0;

            if (rc == 0) {
                // The result crosses a fiber boundary: deep-copy non-shared
                // containers, like spawn arguments and channel sends. Without
                // this, `return {...}` handed the parent a pointer into the
                // dead fiber's heap — the last aliasing escape route.
                future->resolve(deep_copy_value_for_spawn(fiber_vm.registers_[0]));
            } else {
                Value err;
                if (fiber_vm.last_error_ && fiber_vm.last_error_->message) {
                    err = make_string_value_from_cstr(state, fiber_vm.last_error_->message);
                } else {
                    err = make_string_value_from_cstr(state, "spawn: fiber execution failed");
                }
                future->reject(err);
            }

            for (Upvalue* uv : fiber_upvalues) uv->release();

            ((RefCounted*)future)->release();
        };

        JobSystem* js = state->jobSystem();
        if (MOBIUS_UNLIKELY(!js)) {
            // The job holds the +1 retained above; drop it along with the
            // creation reference before erroring, or the future leaks.
            ((RefCounted*)future)->release();
            ((RefCounted*)future)->release();
            VM_ERROR(vm, f, "spawn requires the fiber job system, which is not running");
            return -1;
        }
        frame_globals(vm, f)->shared.store(true, std::memory_order_release);
        js->submit(std::move(job));

        RA(inst) = make_future_value(future);   // retains (+1) for the register
        // Drop the creation reference from `new FutureValue()` above. Live refs
        // are now exactly: the job's (released when the fiber finishes) and the
        // register's (released when the script drops the value). Without this,
        // every spawn leaked its future.
        ((RefCounted*)future)->release();
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

    while (!future->isDone()) {
        JobSystem* js = vm->state_->jobSystem();
        if (js) js->yieldFiber();
        else std::this_thread::yield();
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
    (void)f; (void)inst;
    JobSystem* js = vm->state_->jobSystem();
    if (js) js->yieldFiber();
    else std::this_thread::yield();
    return 0;
}

// OP_CANCEL_CHECK -- if current fiber has been cancelled, throw CancellationError
MOBIUS_FORCEINLINE static int vm_op_cancel_check(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    (void)f; (void)inst;
    if (MOBIUS_UNLIKELY(g_gc_shadow_mode | (int)g_gc_pending)) gc_safepoint(vm);
    if (vm->future_ && vm->future_->isCancelled()) {
        VM_ERROR(vm, f, "CancellationError: fiber was cancelled");
        return -1;
    }
    return 0;
}

// OP_SHARE A -- wrap R[A] in a SharedCell
MOBIUS_FORCEINLINE static int vm_op_share(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    uint8_t a = DECODE_A(inst);
    Value& val = f.regs[a];

    if (val.type == VAL_SHARED_CELL && val.as.shared_cell) {
        val.flags |= VAL_FLAG_SHARED;
        return 0;
    }

    SharedCell* cell = new (std::nothrow) SharedCell(val);
    if (!cell) {
        VM_ERROR(vm, f, "shared: failed to allocate shared cell");
        return -1;
    }
    val = make_shared_cell_value(cell);
    val.flags |= VAL_FLAG_SHARED;
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_atomic_begin(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    uint8_t a = DECODE_A(inst);
    Value& val = f.regs[a];
    std::recursive_mutex* held_mutex = nullptr;

    if (val.type == VAL_SHARED_CELL && val.as.shared_cell) {
        held_mutex = &val.as.shared_cell->mutex();
    } else if (val.type == VAL_ARRAY_SLICE && val.as.array_slice) {
        if (!val.as.array_slice->ownerCell()) {
            VM_ERROR(vm, f, "atomic: slice is not shared; use 'shared var' to declare the parent array");
            return -1;
        }
        held_mutex = &val.as.array_slice->ownerCell()->mutex();
    } else if (val.type == VAL_ARRAY && val.as.array) {
        VM_ERROR(vm, f, "atomic: array is not shared; use 'shared var' to declare it");
        return -1;
    } else if (val.type == VAL_TABLE && val.as.table) {
        VM_ERROR(vm, f, "atomic: table is not shared; use 'shared var' to declare it");
        return -1;
    } else {
        VM_ERROR(vm, f, "atomic: expected a shared array or table, got %s", value_type_name(val.type));
        return -1;
    }

    held_mutex->lock();
    vm->atomic_locks_.push_back({held_mutex});
    return 0;
}

MOBIUS_FORCEINLINE static int vm_op_atomic_end(MobiusVM* vm, VMFrame& f, uint32_t inst) {
    (void)inst;
    if (vm->atomic_locks_.empty()) {
        VM_ERROR(vm, f, "atomic_end: no matching atomic_begin lock");
        return -1;
    }
    MobiusVM::AtomicLock held = vm->atomic_locks_.back();
    vm->atomic_locks_.pop_back();
    if (held.mutex) held.mutex->unlock();
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

MOBIUS_NOINLINE int MobiusVM::handleHandlerError(size_t base_depth) {
    if (!try_stack_.empty() &&
        try_stack_.back().call_stack_depth > base_depth) {
        TryBlock& tb = try_stack_.back();
        release_atomic_locks(this, tb.atomic_depth);
        while (callStackSize() > tb.call_stack_depth) {
            if (callStackTop().upvalue_count > 0)
                closeUpvalues(callStackTop(), 0);
            callStackPop();
        }
        InternalError* ie = state_->getLastError();
        const char* err = ie ? ie->message : nullptr;
        registers_[tb.base + tb.catch_reg] = make_error_string_value(err);
        callStackTop().ip = tb.catch_ip;
        try_stack_.pop_back();
        return 0;
    }
    return -1;
}

int MobiusVM::run(size_t base_depth) {
    size_t saved_atomic_depth = atomic_locks_.size();
    struct AtomicLockCleanup {
        MobiusVM* vm;
        size_t keep_depth;
        ~AtomicLockCleanup() {
            release_atomic_locks(vm, keep_depth);
        }
    } atomic_cleanup{this, saved_atomic_depth};

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
        &&L_OP_GETUPVAL, &&L_OP_SETUPVAL, &&L_OP_GETGLOBAL, &&L_OP_SETGLOBAL, &&L_OP_GLOBAL_READONLY,
        &&L_OP_NEWTABLE, &&L_OP_NEWARRAY, &&L_OP_INDEX_GET, &&L_OP_INDEX_SET,
        &&L_OP_ADD, &&L_OP_SUB, &&L_OP_MUL, &&L_OP_DIV, &&L_OP_MOD,
        &&L_OP_UNM, &&L_OP_NOT,
        &&L_OP_BAND, &&L_OP_BOR, &&L_OP_BXOR, &&L_OP_BNOT, &&L_OP_SHL, &&L_OP_SHR,
        &&L_OP_EQ, &&L_OP_LT, &&L_OP_LE,
        &&L_OP_TEST, &&L_OP_TESTSET,
        &&L_OP_JMP,
        &&L_OP_CALL, &&L_OP_CALL_PLAIN, &&L_OP_TAILCALL, &&L_OP_RETURN,
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
        &&L_OP_DIV_II, &&L_OP_DIV_FF, &&L_OP_MOD_FF,
        &&L_OP_ADDK, &&L_OP_SUBK, &&L_OP_MULK, &&L_OP_DIVK, &&L_OP_MODK,
        &&L_OP_MOVE_ADDI, &&L_OP_MOVE_MULI, &&L_OP_GETGLOBAL_INDEX_GET, &&L_OP_GETGLOBAL_CALL,
        &&L_OP_GETGLOBAL_CALL_PLAIN, &&L_OP_CALL_DIRECT, &&L_OP_CALL_DIRECT_PLAIN,
        &&L_OP_ARRAY_PUSH,
        &&L_OP_GETFIELD, &&L_OP_SETFIELD,
        &&L_OP_SIZE, &&L_OP_TOSTR, &&L_OP_CONCAT2,
        &&L_OP_LEN,
        &&L_OP_TRY_BEGIN, &&L_OP_TRY_END, &&L_OP_THROW,
        &&L_OP_SPAWN, &&L_OP_AWAIT, &&L_OP_YIELD,
        &&L_OP_SHARE, &&L_OP_SHARED_LOAD, &&L_OP_SHARED_STORE,
        &&L_OP_LOCK_SHARED, &&L_OP_UNLOCK_SHARED,
        &&L_OP_CANCEL_CHECK,
        &&L_OP_ATOMIC_BEGIN,
        &&L_OP_ATOMIC_END,
        &&L_OP_SELF,
        &&L_OP_LT_II, &&L_OP_LE_II, &&L_OP_EQ_II,
        &&L_OP_LT_FF, &&L_OP_LE_FF, &&L_OP_EQ_FF,
        &&L_OP_TYPELOCK,
        &&L_OP_TYPECHECK_LOCKED,
        &&L_OP_NOP,
        &&L_OP_AGET, &&L_OP_AGET_INDEX_GET, &&L_OP_ADD_CHECK, &&L_OP_AGET_ADD_CHECK, &&L_OP_ASET,
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
    // On error: use the innermost try handler, but ONLY if it was registered
    // by a frame belonging to this run() invocation (depth > base_depth).
    // Handlers below base_depth belong to an outer dispatch loop; unwinding to
    // them from a nested run (metamethod / iterator call) would corrupt both
    // runs' frame state — instead return -1 so each run level unwinds itself.
    // The try-unwind machinery is outlined into handleHandlerError: inlined
    // into every one of ~120 labels it bloated run() past GCC's register-
    // allocation comfort zone (adding any two labels regressed unrelated
    // benchmarks ~20%). The frame mirror is refreshed HERE, inline, so its
    // address never escapes into the call.
    #define VM_HANDLER(op, fn) VM_CASE(op) {                            \
        int _rc = fn(this, f, inst);                                    \
        if (MOBIUS_UNLIKELY(_rc < 0)) {                                 \
            if (handleHandlerError(base_depth) < 0) return -1;          \
            refreshFrame(f);                                            \
            VM_NEXT();                                                  \
        }                                                               \
        VM_NEXT();                                                      \
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
    VM_HANDLER(OP_GLOBAL_READONLY, vm_op_global_readonly)
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
    VM_HANDLER(OP_DIV_II, vm_op_div_ii)
    VM_HANDLER(OP_DIV_FF, vm_op_div_ff)
    VM_HANDLER(OP_MOD_FF, vm_op_mod_ff)

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
    VM_HANDLER(OP_MOVE_MULI, vm_op_move_muli)
    VM_HANDLER(OP_GETGLOBAL_INDEX_GET, vm_op_getglobal_index_get)
    VM_HANDLER(OP_GETGLOBAL_CALL, vm_op_getglobal_call)
    VM_HANDLER(OP_GETGLOBAL_CALL_PLAIN, vm_op_getglobal_call_plain)
    VM_HANDLER(OP_CALL_DIRECT, vm_op_call_direct)
    VM_HANDLER(OP_CALL_DIRECT_PLAIN, vm_op_call_direct_plain)

    VM_HANDLER(OP_ARRAY_PUSH, vm_op_array_push)
    VM_HANDLER(OP_GETFIELD, vm_op_getfield)
    VM_HANDLER(OP_SETFIELD, vm_op_setfield)
    VM_HANDLER(OP_SIZE, vm_op_size)
    VM_HANDLER(OP_TOSTR, vm_op_tostr)
    VM_HANDLER(OP_CONCAT2, vm_op_concat2)

    VM_HANDLER(OP_CALL, vm_op_call)
    VM_HANDLER(OP_CALL_PLAIN, vm_op_call_plain)

    VM_CASE(OP_TAILCALL) {
        int rc = vm_op_tailcall(this, f, inst, base_depth);
        if (MOBIUS_UNLIKELY(rc < 0)) {
            // Same handler-selection rules as VM_HANDLER: only a try block
            // registered within this run() may catch, and its atomic locks
            // must be released on the way to the catch.
            if (!try_stack_.empty() &&
                try_stack_.back().call_stack_depth > base_depth) {
                TryBlock& _tb = try_stack_.back();
                release_atomic_locks(this, _tb.atomic_depth);
                while (callStackSize() > _tb.call_stack_depth) {
                    if (callStackTop().upvalue_count > 0)
                        closeUpvalues(callStackTop(), 0);
                    callStackPop();
                }
                InternalError* _ie = state_->getLastError();
                const char* err = _ie ? _ie->message : nullptr;
                registers_[_tb.base + _tb.catch_reg] = make_error_string_value(err);
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
    VM_HANDLER(OP_SHARED_LOAD, vm_op_shared_load)
    VM_HANDLER(OP_SHARED_STORE, vm_op_shared_store)
    VM_HANDLER(OP_LOCK_SHARED, vm_op_lock_shared)
    VM_HANDLER(OP_UNLOCK_SHARED, vm_op_unlock_shared)
    VM_HANDLER(OP_CANCEL_CHECK, vm_op_cancel_check)
    VM_HANDLER(OP_ATOMIC_BEGIN, vm_op_atomic_begin)
    VM_HANDLER(OP_ATOMIC_END, vm_op_atomic_end)
    VM_CASE(OP_THROW) {
        // vm_op_throw needs base_depth for the nested-run guard; on failure
        // (no usable handler in this run) the error simply exits this run —
        // the guard inside vm_op_throw already established there is no
        // handler above base_depth, so no catch dispatch is needed here.
        int rc = vm_op_throw(this, f, inst, base_depth);
        if (MOBIUS_UNLIKELY(rc < 0)) return -1;
        VM_NEXT();
    }
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
    VM_HANDLER(OP_AGET, vm_op_aget)
    VM_HANDLER(OP_AGET_INDEX_GET, vm_op_aget_index_get)
    VM_HANDLER(OP_ADD_CHECK, vm_op_add_check)
    VM_HANDLER(OP_AGET_ADD_CHECK, vm_op_aget_add_check)
    VM_HANDLER(OP_ASET, vm_op_aset)

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
