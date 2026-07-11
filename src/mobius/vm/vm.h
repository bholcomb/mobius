#ifndef MOBIUS_VM_VM_H
#define MOBIUS_VM_VM_H

#include "vm/bytecode.h"
#include "data/value.h"
#include <mobius/mobius.h>

#include <vector>
#include <cstdint>
#include <limits>
#include <new>

class MobiusState;
class ExecutionContext;
class FutureValue;
struct InternalError;

#include "state/mobius_state.h"

// ============================================================================
// Upvalue — runtime representation of a captured variable
// ============================================================================

// Reference counted: an Upvalue is legitimately shared — between sibling
// closures capturing the same variable, between a closure and the frame that
// tracks it for closing, and across fibers when a closure is spawned. Before
// refcounting, nothing ever freed these: every captured variable leaked its
// Upvalue plus whatever the closed Value pinned. Atomic because closures
// cross fiber worker threads.
struct Upvalue {
    Value* location;
    Value  closed;
    bool   is_open;
    std::atomic<int> refcount;
    GcHeader gc_;   // tracing-GC registry link

    Upvalue() : location(nullptr), is_open(true), refcount(1) {
        gc_track(&gc_, GC_UPVALUE, this);
    }
    ~Upvalue() { gc_untrack(&gc_); }

    void retain() { refcount.fetch_add(1, std::memory_order_relaxed); }
    void release() {
        if (refcount.fetch_sub(1, std::memory_order_acq_rel) == 1) delete this;
    }
};

// ============================================================================
// CallInfo — one per active function invocation in the VM
//
// Uses pointer+count for upvalues instead of std::vector to eliminate
// heap allocation on every function call.
// ============================================================================

static constexpr int CALLINFO_INLINE_UPVALS = 4;

struct CallInfo {
    Prototype*  proto;
    uint32_t*   ip;
    int         base;
    int         nresults;

    Upvalue**   upvalues;
    int         upvalue_count;
    int         upvalue_capacity;
    Upvalue*    inline_upvals[CALLINFO_INLINE_UPVALS];

    CallInfo() : proto(nullptr), ip(nullptr), base(0), nresults(0),
                 upvalues(inline_upvals), upvalue_count(0),
                 upvalue_capacity(CALLINFO_INLINE_UPVALS) {}

    CallInfo(const CallInfo&) = delete;
    CallInfo& operator=(const CallInfo&) = delete;

    // The list owns one reference to each entry: retained on insertion
    // (pushUpvalue / setUpvaluesFrom), released whenever entries are dropped.

    void releaseEntries() {
        for (int i = 0; i < upvalue_count; i++)
            if (upvalues[i]) upvalues[i]->release();
    }

    void initUpvalues() {
        releaseEntries();
        if (upvalues != inline_upvals) delete[] upvalues;
        upvalues = inline_upvals;
        upvalue_count = 0;
        upvalue_capacity = CALLINFO_INLINE_UPVALS;
    }

    void reset(Prototype* p, uint32_t* i, int b, int nr) {
        proto = p; ip = i; base = b; nresults = nr;
        if (MOBIUS_UNLIKELY(upvalue_count > 0)) initUpvalues();
    }

    bool setUpvaluesFrom(Upvalue** src, int count) {
        releaseEntries();
        if (upvalues != inline_upvals) delete[] upvalues;
        upvalue_count = 0;
        if (count <= CALLINFO_INLINE_UPVALS) {
            upvalues = inline_upvals;
            upvalue_capacity = CALLINFO_INLINE_UPVALS;
        } else {
            upvalues = new (std::nothrow) Upvalue*[count];
            if (!upvalues) {
                upvalues = inline_upvals;
                upvalue_capacity = CALLINFO_INLINE_UPVALS;
                return false;
            }
            upvalue_capacity = count;
        }
        for (int i = 0; i < count; i++) {
            upvalues[i] = src[i];
            if (src[i]) src[i]->retain();
        }
        upvalue_count = count;
        return true;
    }

    bool pushUpvalue(Upvalue* uv) {
        if (upvalue_count >= upvalue_capacity) {
            if (upvalue_capacity > std::numeric_limits<int>::max() / 2) {
                return false;
            }
            int new_cap = upvalue_capacity * 2;
            Upvalue** new_buf = new (std::nothrow) Upvalue*[new_cap];
            if (!new_buf) {
                return false;
            }
            for (int i = 0; i < upvalue_count; i++) new_buf[i] = upvalues[i];
            if (upvalues != inline_upvals) delete[] upvalues;
            upvalues = new_buf;
            upvalue_capacity = new_cap;
        }
        upvalues[upvalue_count++] = uv;
        uv->retain();
        return true;
    }

    void clearUpvalues() {
        releaseEntries();
        if (upvalues != inline_upvals) delete[] upvalues;
        upvalues = inline_upvals;
        upvalue_count = 0;
        upvalue_capacity = CALLINFO_INLINE_UPVALS;
    }

    ~CallInfo() {
        releaseEntries();
        if (upvalues != inline_upvals) delete[] upvalues;
    }
};

// ============================================================================
// MobiusVM — register-based bytecode virtual machine
// ============================================================================

static constexpr size_t VM_INITIAL_CALL_STACK = 512;
static constexpr size_t VM_INITIAL_REGISTERS  = 8192;

struct VMFrame;

class MobiusVM {
public:
    explicit MobiusVM(MobiusState* state);
    ~MobiusVM();

    int execute(Prototype* proto);
    int executeDirect(Prototype* proto);
    MOBIUS_FORCEINLINE void refreshFrame(VMFrame& f);

    static inline int64_t  vm_extract_int64(const Value& v);
    static inline uint64_t vm_extract_uint64(const Value& v);
    static inline double   vm_extract_double(const Value& v);
    static inline bool     vm_use_unsigned(const Value& l, const Value& r);

    void runtimeError(const char* fmt, ...);
    int currentLine() const;

    int callMetamethod(const Value& table_val, MobiusString* mm_name,
                       const Value& lhs, const Value& rhs, Value& out);
    int callTernaryMetamethod(const Value& table_val, MobiusString* mm_name,
                              const Value& a, const Value& b, const Value& c);

    static thread_local MobiusVM* t_current_vm;

    MobiusState* state_;
    MobiusMetrics* metrics_;

    bool strict_mode_;
    bool warn_on_conversion_;
    MobiusOverrideBehavior override_behavior_;

    NativeCallContext native_ctx_;
    InternalError* last_error_;
    const char* source_code_;
    ExecutionContext* exec_context_;
    FutureValue* future_ = nullptr;

    std::vector<Value> registers_;
    std::vector<ValueType> type_tags_;
    int        register_capacity_;

    CallInfo*  call_stack_;
    size_t     call_depth_;
    size_t     call_stack_capacity_;

    CallInfo& callStackTop() { return call_stack_[call_depth_]; }
    const CallInfo& callStackTop() const { return call_stack_[call_depth_]; }
    size_t callStackSize() const { return call_depth_ + 1; }

    CallInfo& callStackPush(Prototype* proto, uint32_t* ip, int base, int nresults) {
        call_depth_++;
        if (MOBIUS_UNLIKELY(call_depth_ >= call_stack_capacity_)) growCallStack();
        CallInfo& ci = call_stack_[call_depth_];
        ci.reset(proto, ip, base, nresults);
        return ci;
    }

    void callStackPop() {
        CallInfo& ci = call_stack_[call_depth_];
        if (MOBIUS_UNLIKELY(ci.upvalue_count > 0)) ci.clearUpvalues();
        call_depth_--;
    }

    int callFunction(CallInfo& caller, int func_reg, int nargs, int nresults);
    void closeUpvalues(CallInfo& ci, int from_reg);

    struct AtomicLock {
        std::recursive_mutex* mutex = nullptr;
    };
    std::vector<AtomicLock> atomic_locks_;

    // Heap strings pinned on behalf of native code. mobius_stack_asString and
    // friends hand out borrowed `const char*` pointers; plugins routinely pop
    // the Value and keep using the pointer, which was safe when every string
    // was interned (immortal) but dangles for refcounted heap strings. Each
    // borrowed heap string is retained here and released when the enclosing
    // native call returns (callNative trims to its entry size, handling
    // nesting).
    std::vector<MobiusString*> native_keepalive_;
    void pinForNative(MobiusString* s) {
        if (s && !s->isImmortal()) { s->retain(); native_keepalive_.push_back(s); }
    }
    void trimNativeKeepalive(size_t keep) {
        while (native_keepalive_.size() > keep) {
            native_keepalive_.back()->release();
            native_keepalive_.pop_back();
        }
    }

    struct TryBlock {
        size_t call_stack_depth;
        uint32_t* catch_ip;
        int catch_reg;
        int base;
        // atomic_locks_.size() at TRY_BEGIN: locks acquired inside the try
        // must be released when an exception unwinds to this handler,
        // otherwise a caught error inside atomic(){} leaves the cell mutex
        // held and the next atomic on another fiber deadlocks.
        size_t atomic_depth;
    };
    std::vector<TryBlock> try_stack_;

    int run(size_t base_depth);

    void growCallStack();

    void ensureRegisters(int needed) {
        if (MOBIUS_UNLIKELY(needed > register_capacity_)) growRegisters(needed);
    }
    void growRegisters(int needed);   // rebases open upvalues on reallocation

private:
    int callNative(MobiusCFunction func, int func_reg, int nargs, int nresults);

    inline const Value& RK(const CallInfo& ci, uint8_t field) const {
        if (IS_CONSTANT(field))
            return ci.proto->constants[RK_AS_CONSTANT(field)];
        return registers_[ci.base + field];
    }

    inline Value& R(const CallInfo& ci, int idx) {
        return registers_[ci.base + idx];
    }
    inline const Value& R(const CallInfo& ci, int idx) const {
        return registers_[ci.base + idx];
    }
};

#endif // MOBIUS_VM_VM_H
