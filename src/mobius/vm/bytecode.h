#ifndef MOBIUS_VM_BYTECODE_H
#define MOBIUS_VM_BYTECODE_H

#include "vm/opcodes.h"
#include "data/value.h"

#include <vector>
#include <string>
#include <cstdint>
#include <cstring>
#include <unordered_map>

// ============================================================================
// UpvalueDesc — describes how a closure captures a variable
// ============================================================================

struct UpvalueDesc {
    uint8_t index;     // register index (if in_stack) or parent upvalue index
    bool    in_stack;  // true = captured from enclosing function's registers
                       // false = captured from enclosing function's upvalues
    ValueType type = VAL_UNKNOWN;  // inferred type of captured value
    bool maybe_shared = false;     // true if the captured binding may hold a SharedCell
};

// ============================================================================
// Prototype — the compiled representation of a function
//
// Every function (including the top-level script chunk) compiles to one
// Prototype. Nested function definitions produce child Prototypes referenced
// by the OP_CLOSURE instruction.
// ============================================================================

struct Prototype {
    // -- Bytecode --
    std::vector<uint32_t> code;

    // -- Constant pool --
    // Referenced by LOADK (index via Bx), and by RK() encoding in B/C fields.
    // Stores string keys, numeric literals, and other compile-time constants.
    std::vector<Value> constants;

    // -- Nested function prototypes --
    // OP_CLOSURE references these by index.
    std::vector<Prototype*> protos;

    // -- Upvalue descriptors --
    // One per upvalue this function captures. OP_GETUPVAL / OP_SETUPVAL
    // index into the runtime upvalue array using these descriptors.
    std::vector<UpvalueDesc> upvalues;

    // -- Function metadata --
    int num_params   = 0;   // declared parameter count
    int num_registers = 2;  // max registers needed (locals + temporaries)
    bool is_vararg   = false;
    bool has_type_locks = false;  // true if any OP_TYPELOCK/OP_TYPECHECK_LOCKED emitted
    ValueType return_type = VAL_UNKNOWN;  // inferred from return statements
    bool return_maybe_shared = false;     // true if any return path may produce a SharedCell
    std::vector<uint8_t> param_unwrap_on_entry;  // 1 when the call boundary should unwrap a shared argument

    // -- Debug information --
    // One entry per instruction in `code`, recording the source line.
    std::vector<int> line_info;

    // Source file and function name (for error messages and stack traces).
    std::string source;
    std::string name;

    // -- Local variable debug info (optional, for debugger / disassembler) --
    struct LocalVar {
        std::string name;
        int start_pc;  // first instruction where the local is active
        int end_pc;    // last instruction + 1
    };
    std::vector<LocalVar> local_vars;

    Prototype() = default;
    ~Prototype() {
        for (auto* p : protos) {
            delete p;
        }
    }

    Prototype(const Prototype&) = delete;
    Prototype& operator=(const Prototype&) = delete;

    // -- Constant pool helpers (with deduplication) --

    int addConstant(const Value& val) {
        if (constants.size() >= BX_MASK) return -1;
        constants.push_back(val);
        return (int)(constants.size() - 1);
    }

    int addStringConstant(MobiusString* str) {
        auto it = string_const_map_.find(str->data);
        if (it != string_const_map_.end()) return it->second;

        Value v;
        v.type = VAL_STRING;
        v.as.string = str;
        int idx = addConstant(v);
        if (idx >= 0) string_const_map_[str->data] = idx;
        return idx;
    }

    int addIntConstant(int64_t val) {
        auto it = int_const_map_.find(val);
        if (it != int_const_map_.end()) return it->second;

        Value v = make_integer_value(NUM_INT64, val);
        int idx = addConstant(v);
        if (idx >= 0) int_const_map_[val] = idx;
        return idx;
    }

    int addFloatConstant(double val) {
        uint64_t bits;
        memcpy(&bits, &val, sizeof(bits));
        auto it = float_const_map_.find(bits);
        if (it != float_const_map_.end()) return it->second;

        Value v = make_float_value(val);
        int idx = addConstant(v);
        if (idx >= 0) float_const_map_[bits] = idx;
        return idx;
    }

private:
    std::unordered_map<const char*, int> string_const_map_;
    std::unordered_map<int64_t, int>     int_const_map_;
    std::unordered_map<uint64_t, int>    float_const_map_;

public:

    // -- Code emission helpers --

    int emit(uint32_t instruction, int line = 0) {
        int idx = (int)code.size();
        code.push_back(instruction);
        line_info.push_back(line);
        return idx;
    }

    int emitABC(OpCode op, uint8_t a, uint8_t b, uint8_t c, int line = 0) {
        return emit(ENCODE_ABC(op, a, b, c), line);
    }

    int emitABx(OpCode op, uint8_t a, uint16_t bx, int line = 0) {
        return emit(ENCODE_ABx(op, a, bx), line);
    }

    int emitAsBx(OpCode op, uint8_t a, int sbx, int line = 0) {
        return emit(ENCODE_AsBx(op, a, sbx), line);
    }

    int emitABC_D64(OpCode op, uint8_t a, uint8_t b, uint8_t c,
                    uint64_t data, int line = 0) {
        int idx = emitABC(op, a, b, c, line);
        emit((uint32_t)(data & 0xFFFFFFFF), line);
        emit((uint32_t)(data >> 32), line);
        return idx;
    }

    int emitJump(int offset, int line = 0) {
        return emit(ENCODE_sBx(OP_JMP, offset), line);
    }

    // Patch a previously emitted jump instruction's offset.
    // `jump_idx` is the index returned by emitJump or similar.
    // `target` is the instruction index to jump to.
    void patchJump(int jump_idx, int target) {
        int offset = target - (jump_idx + 1);
        uint32_t inst = code[jump_idx];
        OpCode op = (OpCode)DECODE_OP(inst);

        if (opcode_info(op).format == FMT_sBx) {
            code[jump_idx] = ENCODE_sBx(op, offset);
        } else {
            uint8_t a = DECODE_A(inst);
            code[jump_idx] = ENCODE_AsBx(op, a, offset);
        }
    }

    int currentPC() const { return (int)code.size(); }
};

// ============================================================================
// Disassembly (for debugging)
// ============================================================================

void disassemble_prototype(const Prototype* proto, const char* label = nullptr);
void disassemble_instruction(const Prototype* proto, int offset);

#endif // MOBIUS_VM_BYTECODE_H
