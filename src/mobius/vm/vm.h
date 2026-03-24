#ifndef MOBIUS_VM_VM_H
#define MOBIUS_VM_VM_H

#include "vm/bytecode.h"
#include "data/value.h"

#include <vector>
#include <cstdint>

class MobiusState;
class Environment;

// ============================================================================
// Upvalue — runtime representation of a captured variable
//
// "Open" upvalues point into the register array of the enclosing CallInfo.
// When the enclosing scope exits, open upvalues are "closed" by copying the
// register value into the Upvalue's own storage.
// ============================================================================

struct Upvalue {
    Value* location;    // points into register array while open
    Value  closed;      // holds the value after closing
    bool   is_open;

    Upvalue() : location(nullptr), is_open(true) {}
};

// ============================================================================
// CallInfo — one per active function invocation in the VM
// ============================================================================

struct CallInfo {
    Prototype*  proto;
    uint32_t*   ip;         // instruction pointer (into proto->code)
    int         base;       // base register index in the flat register array
    int         nresults;   // expected result count (from OP_CALL's C field)

    std::vector<Upvalue*> open_upvalues;
};

// ============================================================================
// MobiusVM — register-based bytecode virtual machine
//
// Usage:
//   MobiusVM vm(state);
//   Prototype* proto = compiler.compile(stmts, count, "script.mob");
//   int rc = vm.execute(proto);
// ============================================================================

struct VMFrame;

class MobiusVM {
public:
    explicit MobiusVM(MobiusState* state);

    // Execute a top-level prototype. Returns 0 on success, -1 on error.
    int execute(Prototype* proto);

    // Refresh a VMFrame after a call/return changes the active CallInfo.
    void refreshFrame(VMFrame& f);

    // Integer extraction helpers
    static inline int64_t  vm_extract_int64(const Value& v);
    static inline uint64_t vm_extract_uint64(const Value& v);
    static inline double   vm_extract_double(const Value& v);
    static inline bool     vm_use_unsigned(const Value& l, const Value& r);

    // Error reporting
    void runtimeError(const char* fmt, ...);
    int currentLine() const;

    // Metamethod dispatch
    int callMetamethod(const Value& table_val, MobiusString* mm_name,
                       const Value& lhs, const Value& rhs, Value& out);

    // VM state — public so MOBIUS_FORCEINLINE handler functions can access them
    MobiusState* state_;
    Environment* global_env_;
    std::vector<Value>    registers_;
    std::vector<CallInfo> call_stack_;

    int callFunction(CallInfo& caller, int func_reg, int nargs, int nresults);
    void closeUpvalues(CallInfo& ci, int from_reg);

    struct TryBlock {
        size_t call_stack_depth;  // call stack depth at time of TRY_BEGIN
        uint32_t* catch_ip;       // instruction pointer to jump to on error
        int catch_reg;            // register to store the error value
        int base;                 // base register of the frame that contains the try
    };
    std::vector<TryBlock> try_stack_;

    int run(size_t base_depth);

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
