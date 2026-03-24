#ifndef MOBIUS_VM_OPCODES_H
#define MOBIUS_VM_OPCODES_H

#include <cstdint>

// ============================================================================
// Instruction encoding
//
// All instructions are 32 bits wide. Three formats:
//
//   ABC:    [opcode:8] [A:8]  [B:8]  [C:8]
//   ABx:    [opcode:8] [A:8]  [Bx:16]             (unsigned 16-bit)
//   AsBx:   [opcode:8] [A:8]  [sBx:16]            (signed, biased by 0x7FFF)
//   sBx:    [opcode:8] [sBx:24]                    (signed, biased by 0x7FFFFF)
//
// Registers are referenced by 8-bit indices (0–255).
// Constants in the pool are referenced by 16-bit Bx indices (0–65535).
// Jump offsets use signed sBx (relative to the next instruction).
// ============================================================================

// Field widths
constexpr int OP_WIDTH  = 8;
constexpr int A_WIDTH   = 8;
constexpr int B_WIDTH   = 8;
constexpr int C_WIDTH   = 8;
constexpr int BX_WIDTH  = 16;  // B + C combined
constexpr int SBX_WIDTH = 24;  // A + B + C combined (for wide jumps)

// Field positions (LSB-first packing)
constexpr int OP_POS = 0;
constexpr int A_POS  = OP_WIDTH;
constexpr int B_POS  = OP_WIDTH + A_WIDTH;
constexpr int C_POS  = OP_WIDTH + A_WIDTH + B_WIDTH;
constexpr int BX_POS = B_POS;
constexpr int SBX_POS = A_POS;  // for wide jump format

// Field masks
constexpr uint32_t OP_MASK  = (1u << OP_WIDTH) - 1;
constexpr uint32_t A_MASK   = (1u << A_WIDTH) - 1;
constexpr uint32_t B_MASK   = (1u << B_WIDTH) - 1;
constexpr uint32_t C_MASK   = (1u << C_WIDTH) - 1;
constexpr uint32_t BX_MASK  = (1u << BX_WIDTH) - 1;
constexpr uint32_t SBX_MASK = (1u << SBX_WIDTH) - 1;

// Signed bias for sBx fields
constexpr int SBX16_BIAS = 0x7FFF;   // for 16-bit sBx (ABx format)
constexpr int SBX24_BIAS = 0x7FFFFF; // for 24-bit sBx (wide jump format)

// ============================================================================
// Encoding helpers
// ============================================================================

inline uint32_t ENCODE_ABC(uint8_t op, uint8_t a, uint8_t b, uint8_t c) {
    return (uint32_t)op
         | ((uint32_t)a << A_POS)
         | ((uint32_t)b << B_POS)
         | ((uint32_t)c << C_POS);
}

inline uint32_t ENCODE_ABx(uint8_t op, uint8_t a, uint16_t bx) {
    return (uint32_t)op
         | ((uint32_t)a << A_POS)
         | ((uint32_t)bx << BX_POS);
}

inline uint32_t ENCODE_AsBx(uint8_t op, uint8_t a, int sbx) {
    uint16_t encoded = (uint16_t)(sbx + SBX16_BIAS);
    return (uint32_t)op
         | ((uint32_t)a << A_POS)
         | ((uint32_t)encoded << BX_POS);
}

inline uint32_t ENCODE_sBx(uint8_t op, int sbx) {
    uint32_t encoded = (uint32_t)(sbx + SBX24_BIAS);
    return (uint32_t)op
         | (encoded << A_POS);
}

// ============================================================================
// Decoding helpers
// ============================================================================

inline uint8_t  DECODE_OP(uint32_t inst)  { return (uint8_t)(inst & OP_MASK); }
inline uint8_t  DECODE_A(uint32_t inst)   { return (uint8_t)((inst >> A_POS) & A_MASK); }
inline uint8_t  DECODE_B(uint32_t inst)   { return (uint8_t)((inst >> B_POS) & B_MASK); }
inline uint8_t  DECODE_C(uint32_t inst)   { return (uint8_t)((inst >> C_POS) & C_MASK); }
inline uint16_t DECODE_Bx(uint32_t inst)  { return (uint16_t)((inst >> BX_POS) & BX_MASK); }
inline int      DECODE_sBx(uint32_t inst) { return (int)((inst >> BX_POS) & BX_MASK) - SBX16_BIAS; }
inline int      DECODE_sBx_wide(uint32_t inst) { return (int)((inst >> A_POS) & SBX_MASK) - SBX24_BIAS; }

// ============================================================================
// Constant-as-register encoding
//
// When B or C field refers to a constant instead of a register, bit 8 is set.
// This allows instructions like ADD A, B, Kc where Kc indexes the constant
// pool. Since B and C are 8-bit, the top bit (0x80) flags "constant" and
// the lower 7 bits (0–127) index into the constant pool for that operand.
// ============================================================================

constexpr uint8_t RK_CONSTANT_BIT = 0x80;
constexpr uint8_t RK_INDEX_MASK   = 0x7F;

inline bool     IS_CONSTANT(uint8_t rk)    { return (rk & RK_CONSTANT_BIT) != 0; }
inline uint8_t  RK_AS_CONSTANT(uint8_t rk) { return rk & RK_INDEX_MASK; }
inline uint8_t  MAKE_RK(uint8_t idx)       { return idx | RK_CONSTANT_BIT; }

// ============================================================================
// Opcode enumeration
// ============================================================================

enum OpCode : uint8_t {
    // -- Data movement --
    OP_MOVE,        // A B       R[A] = R[B]
    OP_LOADK,       // A Bx      R[A] = K[Bx]
    OP_LOADNIL,     // A B       R[A], R[A+1], ..., R[A+B] = nil
    OP_LOADBOOL,    // A B C     R[A] = (bool)B; if C, skip next instruction
    OP_LOADINT,     // A sBx     R[A] = (int64_t)sBx  (small integer literal)

    // -- Upvalues and globals --
    OP_GETUPVAL,    // A B       R[A] = UpValue[B]
    OP_SETUPVAL,    // A B       UpValue[B] = R[A]
    OP_GETGLOBAL,   // A Bx      R[A] = globals[K[Bx]]
    OP_SETGLOBAL,   // A Bx      globals[K[Bx]] = R[A]

    // -- Table and array operations --
    OP_NEWTABLE,    // A B C     R[A] = new Table(B array slots, C hash slots)
    OP_NEWARRAY,    // A B       R[A] = new Array(B capacity)
    OP_GETTABLE,    // A B C     R[A] = R[B][RK(C)]  (use RK(C) with constant for .field)
    OP_SETTABLE,    // A B C     R[A][RK(B)] = RK(C)

    // -- Arithmetic --
    OP_ADD,         // A B C     R[A] = RK(B) + RK(C)
    OP_SUB,         // A B C     R[A] = RK(B) - RK(C)
    OP_MUL,         // A B C     R[A] = RK(B) * RK(C)
    OP_DIV,         // A B C     R[A] = RK(B) / RK(C)
    OP_MOD,         // A B C     R[A] = RK(B) % RK(C)
    OP_UNM,         // A B       R[A] = -R[B]
    OP_NOT,         // A B       R[A] = not R[B]

    // -- Bitwise --
    OP_BAND,        // A B C     R[A] = RK(B) & RK(C)
    OP_BOR,         // A B C     R[A] = RK(B) | RK(C)
    OP_BXOR,        // A B C     R[A] = RK(B) ^ RK(C)
    OP_BNOT,        // A B       R[A] = ~R[B]
    OP_SHL,         // A B C     R[A] = RK(B) << RK(C)
    OP_SHR,         // A B C     R[A] = RK(B) >> RK(C)

    // -- String --
    OP_CONCAT,      // A B C     R[A] = R[B] .. R[B+1] .. ... .. R[C]

    // -- Comparison (result: skip next instruction if test fails) --
    OP_EQ,          // A B C     if (RK(B) == RK(C)) != A then skip next
    OP_LT,          // A B C     if (RK(B) <  RK(C)) != A then skip next
    OP_LE,          // A B C     if (RK(B) <= RK(C)) != A then skip next

    // -- Logical test + set --
    OP_TEST,        // A   C     if (bool)R[A] != C then skip next
    OP_TESTSET,     // A B C     if (bool)R[B] == C then R[A] = R[B] else skip next

    // -- Jumps --
    OP_JMP,         // sBx       pc += sBx  (24-bit wide jump)

    // -- Function calls --
    OP_CALL,        // A B C     call R[A] with B-1 args (R[A+1]..R[A+B-1]);
                    //           C-1 results into R[A]..R[A+C-2]
                    //           B=0: args are R[A+1]..top; C=0: multiple returns
    OP_TAILCALL,    // A B       tail-call R[A] with B-1 args (same as CALL but reuses frame)
    OP_RETURN,      // A B       return R[A], ..., R[A+B-2]; B=0: return R[A]..top; B=1: return nothing

    // -- Closures --
    OP_CLOSURE,     // A Bx      R[A] = closure(proto[Bx])
                    //           followed by pseudo-instructions for upvalue binding
    OP_CLOSE,       // A         close all upvalues >= R[A]

    // -- Numeric for-loop --
    OP_FORPREP,     // A sBx     R[A] -= R[A+2]; pc += sBx (jump to FORLOOP)
    OP_FORLOOP,     // A sBx     R[A] += R[A+2]; if R[A] <= R[A+1] then { pc += sBx; R[A+3] = R[A] }
    OP_IFORPREP,    // A sBx     R[A].i64 -= R[A+2].i64; pc += sBx (integer-only)
    OP_IFORLOOP,    // A sBx     R[A].i64 += R[A+2].i64; if in range then { pc += sBx; R[A+3].i64 = R[A].i64 }
                    //           R[A]=index, R[A+1]=limit, R[A+2]=step, R[A+3]=loop var

    // -- Generic for-loop (future, for iterator protocol) --
    OP_TFORLOOP,    // A C       call R[A](R[A+1], R[A+2]); if R[A+3] != nil then R[A+2] = R[A+3]

    // -- Enum --
    OP_NEWENUM,     // A B Bx    R[A] = new EnumDefinition with B members, name at K[Bx]
    OP_ENUMVAL,     // A B C     set member C of enum R[A] to value R[B]
    OP_GETENUM,     // A B C     R[A] = R[B].member[C]

    // -- Import / pragma --
    OP_IMPORT,      // A B C     import module K/R[B] as alias K/R[C]
    OP_PRAGMA,      // A Bx      set pragma K[Bx] to value R[A]

    // -- Increment / decrement --
    OP_INC,         // A B       R[A] = R[B] + 1 (integer increment)
    OP_DEC,         // A B       R[A] = R[B] - 1 (integer decrement)

    // -- Type checking --
    OP_TYPECHECK,   // A B       validate/convert R[A] to NumberType B; errors in strict mode
    OP_ISNUM,       // A B       R[A] = (R[B] is integer or float)
    OP_TYPECOMPAT,  // A B C     if (comparable(RK(B), RK(C)) != A) then skip next
    OP_TYPEIS,      // A B C     if (R[B].type == C) != A then skip next

    // -- Comparison with immediate (AsBx: A=register, sBx=signed 16-bit immediate) --
    OP_LTI,         // A sBx     if R[A] <  sBx then skip next
    OP_LEI,         // A sBx     if R[A] <= sBx then skip next
    OP_EQI,         // A sBx     if R[A] == sBx then skip next
    OP_NEI,         // A sBx     if R[A] != sBx then skip next
    OP_GTI,         // A sBx     if R[A] >  sBx then skip next
    OP_GEI,         // A sBx     if R[A] >= sBx then skip next

    // -- Arithmetic with immediate --
    OP_ADDI,        // A sBx     R[A] = R[A] + sBx
    OP_SUBI,        // A sBx     R[A] = R[A] - sBx
    OP_MULI,        // A sBx     R[A] = R[A] * sBx
    OP_MODI,        // A sBx     R[A] = R[A] % sBx

    // -- Fused test-and-jump --
    OP_TESTJMP,     // A sBx     if !is_truthy(R[A]) then pc += sBx

    // -- Miscellaneous --
    OP_LEN,         // A B       R[A] = length(R[B])  (array length or table size)

    // -- Debug / sentinel --
    OP_NOP,         //           no operation (padding / breakpoint target)

    OP_MAX_OPCODE
};

// ============================================================================
// Opcode metadata (for debugging, disassembly, validation)
// ============================================================================

enum InstructionFormat {
    FMT_ABC,    // 3-register operands
    FMT_ABx,    // register + unsigned 16-bit
    FMT_AsBx,   // register + signed 16-bit
    FMT_sBx,    // signed 24-bit (wide jump)
};

struct OpcodeInfo {
    const char* name;
    InstructionFormat format;
};

inline const OpcodeInfo& opcode_info(OpCode op) {
    static const OpcodeInfo info[] = {
        {"MOVE",      FMT_ABC},
        {"LOADK",     FMT_ABx},
        {"LOADNIL",   FMT_ABC},
        {"LOADBOOL",  FMT_ABC},
        {"LOADINT",   FMT_AsBx},

        {"GETUPVAL",  FMT_ABC},
        {"SETUPVAL",  FMT_ABC},
        {"GETGLOBAL", FMT_ABx},
        {"SETGLOBAL", FMT_ABx},

        {"NEWTABLE",  FMT_ABC},
        {"NEWARRAY",  FMT_ABC},
        {"GETTABLE",  FMT_ABC},
        {"SETTABLE",  FMT_ABC},

        {"ADD",       FMT_ABC},
        {"SUB",       FMT_ABC},
        {"MUL",       FMT_ABC},
        {"DIV",       FMT_ABC},
        {"MOD",       FMT_ABC},
        {"UNM",       FMT_ABC},
        {"NOT",       FMT_ABC},

        {"BAND",      FMT_ABC},
        {"BOR",       FMT_ABC},
        {"BXOR",      FMT_ABC},
        {"BNOT",      FMT_ABC},
        {"SHL",       FMT_ABC},
        {"SHR",       FMT_ABC},

        {"CONCAT",    FMT_ABC},

        {"EQ",        FMT_ABC},
        {"LT",        FMT_ABC},
        {"LE",        FMT_ABC},

        {"TEST",      FMT_ABC},
        {"TESTSET",   FMT_ABC},

        {"JMP",       FMT_sBx},

        {"CALL",      FMT_ABC},
        {"TAILCALL",  FMT_ABC},
        {"RETURN",    FMT_ABC},

        {"CLOSURE",   FMT_ABx},
        {"CLOSE",     FMT_ABC},

        {"FORPREP",   FMT_AsBx},
        {"FORLOOP",   FMT_AsBx},
        {"IFORPREP",  FMT_AsBx},
        {"IFORLOOP",  FMT_AsBx},
        {"TFORLOOP",  FMT_ABC},

        {"NEWENUM",   FMT_ABx},
        {"ENUMVAL",   FMT_ABC},
        {"GETENUM",   FMT_ABC},

        {"IMPORT",    FMT_ABC},
        {"PRAGMA",    FMT_ABx},

        {"INC",       FMT_ABC},
        {"DEC",       FMT_ABC},

        {"TYPECHECK", FMT_ABC},
        {"ISNUM",     FMT_ABC},
        {"TYPECOMPAT",FMT_ABC},
        {"TYPEIS",    FMT_ABC},

        {"LTI",       FMT_AsBx},
        {"LEI",       FMT_AsBx},
        {"EQI",       FMT_AsBx},
        {"NEI",       FMT_AsBx},
        {"GTI",       FMT_AsBx},
        {"GEI",       FMT_AsBx},

        {"ADDI",      FMT_AsBx},
        {"SUBI",      FMT_AsBx},
        {"MULI",      FMT_AsBx},
        {"MODI",      FMT_AsBx},

        {"TESTJMP",   FMT_AsBx},

        {"LEN",       FMT_ABC},

        {"NOP",       FMT_ABC},
    };
    static_assert(sizeof(info) / sizeof(info[0]) == OP_MAX_OPCODE,
                  "opcode_info table must match OpCode enum");
    return info[op];
}

#endif // MOBIUS_VM_OPCODES_H
