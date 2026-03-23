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

class MobiusVM {
public:
    explicit MobiusVM(MobiusState* state);

    // Execute a top-level prototype. Returns 0 on success, -1 on error.
    int execute(Prototype* proto);

private:
    MobiusState* state_;
    Environment* global_env_;

    std::vector<Value>    registers_;
    std::vector<CallInfo> call_stack_;

    // Resolve a B or C field that may reference a register or a constant.
    inline const Value& RK(const CallInfo& ci, uint8_t field) const {
        if (IS_CONSTANT(field))
            return ci.proto->constants[RK_AS_CONSTANT(field)];
        return registers_[ci.base + field];
    }

    // Register access relative to current frame base.
    inline Value& R(const CallInfo& ci, int idx) {
        return registers_[ci.base + idx];
    }
    inline const Value& R(const CallInfo& ci, int idx) const {
        return registers_[ci.base + idx];
    }

    // Integer extraction (mirrors the helpers in eval_arithmatic.cpp)
    static inline int64_t  vm_extract_int64(const Value& v);
    static inline uint64_t vm_extract_uint64(const Value& v);
    static inline double   vm_extract_double(const Value& v);
    static inline bool     vm_use_unsigned(const Value& l, const Value& r);

    // The main dispatch loop. Returns 0 on success, -1 on error.
    int run();

    // Helpers
    int callFunction(CallInfo& caller, int func_reg, int nargs, int nresults);
    int callNative(MobiusCFunction func, int func_reg, int nargs, int nresults);
    void closeUpvalues(CallInfo& ci, int from_reg);
    void runtimeError(const char* fmt, ...);

    int currentLine() const;
};

#endif // MOBIUS_VM_VM_H
