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
    OP_GLOBAL_READONLY, // A Bx  set readonly flag on global slot Bx when A!=0

    // -- Table and array operations --
    OP_NEWTABLE,    // A B C     R[A] = new Table(B array slots, C hash slots)
    OP_NEWARRAY,    // A B       R[A] = new Array(B capacity)
    OP_INDEX_GET,   // A B C     R[A] = R[B][RK(C)]  (array/table/string polymorphic)
    OP_INDEX_SET,   // A B C     R[A][RK(B)] = RK(C) (array/table polymorphic)

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
    OP_CALL_PLAIN,  // A B C     same as OP_CALL, but explicit args are statically known non-shared
    OP_TAILCALL,    // A B       tail-call R[A] with B-1 args (same as CALL but reuses frame)
    OP_RETURN,      // A B       return R[A], ..., R[A+B-2]; B=0: return R[A]..top; B=1: return nothing

    // -- Closures --
    OP_CLOSURE,     // A Bx      R[A] = closure(proto[Bx])
                    //           followed by pseudo-instructions for upvalue binding
    OP_CLOSE,       // A         close all upvalues >= R[A]

    // -- Numeric for-loop (integer-only fast path) --
    OP_IFORPREP,    // A sBx     R[A].i64 -= R[A+2].i64; pc += sBx (integer-only)
    OP_IFORLOOP,    // A sBx     R[A].i64 += R[A+2].i64; if in range then { pc += sBx; R[A+3].i64 = R[A].i64 }
                    //           R[A]=index, R[A+1]=limit, R[A+2]=step, R[A+3]=loop var

    // -- Generic for-loop (future, for iterator protocol) --
    OP_TFORLOOP,    // A C       call R[A](R[A+1], R[A+2]); if R[A+3] != nil then R[A+2] = R[A+3]

    // -- Enum --
    OP_NEWENUM,     // A Bx      R[A] = new EnumDefinition, name at K[Bx]
    OP_ENUMVAL,     // A B C     add member RK(C) to enum R[A] with value RK(B)
    OP_GETENUM,     // A B C     R[A] = enum R[B], member named RK(C)

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
    OP_DIVI,        // A sBx     R[A] = R[A] / sBx
    OP_MODI,        // A sBx     R[A] = R[A] % sBx

    // -- Fused test-and-jump --
    OP_TESTJMP,     // A sBx     if !is_truthy(R[A]) then pc += sBx

    // -- Type-specialized arithmetic (both operands same type, no type checks) --
    OP_ADD_II,      // A B C     R[A] = RK(B).i64  + RK(C).i64   -> VAL_INT64
    OP_ADD_FF,      // A B C     R[A] = RK(B).f64  + RK(C).f64   -> VAL_FLOAT64
    OP_SUB_II,      // A B C     R[A] = RK(B).i64  - RK(C).i64   -> VAL_INT64
    OP_SUB_FF,      // A B C     R[A] = RK(B).f64  - RK(C).f64   -> VAL_FLOAT64
    OP_MUL_II,      // A B C     R[A] = RK(B).i64  * RK(C).i64   -> VAL_INT64
    OP_MUL_FF,      // A B C     R[A] = RK(B).f64  * RK(C).f64   -> VAL_FLOAT64
    OP_MOD_II,      // A B C     R[A] = RK(B).i64  % RK(C).i64   -> VAL_INT64
    OP_DIV_II,      // A B C     R[A] = RK(B).i64  / RK(C).i64   -> VAL_INT64
    OP_DIV_FF,      // A B C     R[A] = RK(B).f64  / RK(C).f64   -> VAL_FLOAT64
    OP_MOD_FF,      // A B C     R[A] = fmod(RK(B).f64, RK(C).f64) -> VAL_FLOAT64

    // -- Inline-data arithmetic (ABC + 2 data words for 64-bit constant) --
    OP_ADDK,        // A B C     R[A] = R[B] + inline64; C=type_tag; ip+=2
    OP_SUBK,        // A B C     R[A] = R[B] - inline64; C=type_tag; ip+=2
    OP_MULK,        // A B C     R[A] = R[B] * inline64; C=type_tag; ip+=2
    OP_DIVK,        // A B C     R[A] = R[B] / inline64; C=type_tag; ip+=2
    OP_MODK,        // A B C     R[A] = R[B] % inline64; C=type_tag; ip+=2

    // -- Superinstructions (fused multi-instruction patterns) --
    OP_MOVE_ADDI,   // A B + next word is AsBx(ADDI): R[A]=R[B]; R[A]+=sBx; ip+=1
    OP_GETGLOBAL_INDEX_GET, // consumes 2 words: GETGLOBAL(A,Bx) then INDEX_GET(A,A,RK(C))
    OP_GETGLOBAL_CALL, // consumes 2 words: GETGLOBAL(A,Bx) then CALL(A,B,C)
    OP_GETGLOBAL_CALL_PLAIN, // consumes 2 words: GETGLOBAL(A,Bx) then CALL_PLAIN(A,B,C)
    OP_CALL_DIRECT, // consumes 2 words: direct script call to proto[Bx] (or self when Bx=0xFFFF)
    OP_CALL_DIRECT_PLAIN, // same as OP_CALL_DIRECT, but explicit args are statically known non-shared

    // -- Array fast-path --
    OP_ARRAY_PUSH,  // A B       R[A].array.push(R[B])     (array-only append)

    // -- Miscellaneous --
    OP_LEN,         // A B       R[A] = length(R[B])  (array length or table size)

    // -- Error handling --
    OP_TRY_BEGIN,   // A sBx     push recovery point: catch at pc+sBx, error into R[A]
    OP_TRY_END,     //           pop recovery point (try body completed successfully)
    OP_THROW,       // A         throw R[A] as error

    // -- Fiber concurrency --
    OP_SPAWN,       // A B C     R[A] = spawn R[B] with C-1 args; result is FutureValue
    OP_AWAIT,       // A B       R[A] = await FutureValue in R[B]; yields fiber if not ready
    OP_YIELD,       // A         cooperatively yield current fiber (reschedule)
    OP_SHARE,       // A         mark R[A] as shared (deep); sets VAL_FLAG_SHARED
    OP_SHARED_LOAD, // A B       if R[B] is SharedCell, R[A] = load(R[B]); else R[A] = R[B]
    OP_SHARED_STORE,// A B       if R[A] is SharedCell, store unwrap(R[B]) into it; else R[A] = R[B]
    OP_LOCK_SHARED, // A         lock shared value in R[A] if it is share-backed; otherwise no-op
    OP_UNLOCK_SHARED,// A        unlock shared value in R[A] if it is share-backed; otherwise no-op
    OP_CANCEL_CHECK,// --        check if current fiber is cancelled; throw CancellationError if so
    OP_ATOMIC_BEGIN,// A         acquire unique lock on shared container R[A]
    OP_ATOMIC_END,  // A         release unique lock on shared container R[A]

    // -- Method dispatch --
    OP_SELF,        // A B C     R[A+1] = R[B]; R[A] = R[B][RK(C)]  (method lookup with self)

    // -- Type-specialized comparison (both operands same type, no type checks) --
    OP_LT_II,      // A B C     if (RK(B).i64 < RK(C).i64) != A then skip next
    OP_LE_II,      // A B C     if (RK(B).i64 <= RK(C).i64) != A then skip next
    OP_EQ_II,      // A B C     if (RK(B).i64 == RK(C).i64) != A then skip next
    OP_LT_FF,      // A B C     if (RK(B).f64 < RK(C).f64) != A then skip next
    OP_LE_FF,      // A B C     if (RK(B).f64 <= RK(C).f64) != A then skip next
    OP_EQ_FF,      // A B C     if (RK(B).f64 == RK(C).f64) != A then skip next

    // -- Type locking --
    OP_TYPELOCK,    // A         lock R[A]'s type on first non-nil value
    OP_TYPECHECK_LOCKED, // A    verify R[A] matches locked type (or is nil); error on mismatch

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
    FMT_ABC_D,  // ABC header + 2 inline data words (64-bit constant)
    FMT_FUSED2, // fused: consumes 2 instruction words
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
        {"GLOBAL_READONLY", FMT_ABx},

        {"NEWTABLE",  FMT_ABC},
        {"NEWARRAY",  FMT_ABC},
        {"INDEX_GET", FMT_ABC},
        {"INDEX_SET", FMT_ABC},

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

        {"EQ",        FMT_ABC},
        {"LT",        FMT_ABC},
        {"LE",        FMT_ABC},

        {"TEST",      FMT_ABC},
        {"TESTSET",   FMT_ABC},

        {"JMP",       FMT_sBx},

        {"CALL",      FMT_ABC},
        {"CALL_PLAIN",FMT_ABC},
        {"TAILCALL",  FMT_ABC},
        {"RETURN",    FMT_ABC},

        {"CLOSURE",   FMT_ABx},
        {"CLOSE",     FMT_ABC},

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
        {"DIVI",      FMT_AsBx},
        {"MODI",      FMT_AsBx},

        {"TESTJMP",   FMT_AsBx},

        {"ADD_II",    FMT_ABC},
        {"ADD_FF",    FMT_ABC},
        {"SUB_II",    FMT_ABC},
        {"SUB_FF",    FMT_ABC},
        {"MUL_II",    FMT_ABC},
        {"MUL_FF",    FMT_ABC},
        {"MOD_II",    FMT_ABC},
        {"DIV_II",    FMT_ABC},
        {"DIV_FF",    FMT_ABC},
        {"MOD_FF",    FMT_ABC},

        {"ADDK",      FMT_ABC_D},
        {"SUBK",      FMT_ABC_D},
        {"MULK",      FMT_ABC_D},
        {"DIVK",      FMT_ABC_D},
        {"MODK",      FMT_ABC_D},

        {"MOVE_ADDI", FMT_FUSED2},
        {"GETGLOBAL_INDEX_GET", FMT_FUSED2},
        {"GETGLOBAL_CALL", FMT_FUSED2},
        {"GETGLOBAL_CALL_PLAIN", FMT_FUSED2},
        {"CALL_DIRECT", FMT_FUSED2},
        {"CALL_DIRECT_PLAIN", FMT_FUSED2},

        {"ARRAY_PUSH",FMT_ABC},

        {"LEN",       FMT_ABC},

        {"TRY_BEGIN", FMT_AsBx},
        {"TRY_END",   FMT_ABC},
        {"THROW",     FMT_ABC},

        {"SPAWN",     FMT_ABC},
        {"AWAIT",     FMT_ABC},
        {"YIELD",     FMT_ABC},
        {"SHARE",     FMT_ABC},
        {"SHARED_LOAD", FMT_ABC},
        {"SHARED_STORE", FMT_ABC},
        {"LOCK_SHARED", FMT_ABC},
        {"UNLOCK_SHARED", FMT_ABC},
        {"CANCEL_CHECK", FMT_ABC},
        {"ATOMIC_BEGIN", FMT_ABC},
        {"ATOMIC_END",   FMT_ABC},

        {"SELF",      FMT_ABC},

        {"LT_II",    FMT_ABC},
        {"LE_II",    FMT_ABC},
        {"EQ_II",    FMT_ABC},
        {"LT_FF",    FMT_ABC},
        {"LE_FF",    FMT_ABC},
        {"EQ_FF",    FMT_ABC},

        {"TYPELOCK",           FMT_ABC},
        {"TYPECHECK_LOCKED",   FMT_ABC},

        {"NOP",       FMT_ABC},
    };
    static_assert(sizeof(info) / sizeof(info[0]) == OP_MAX_OPCODE,
                  "opcode_info table must match OpCode enum");
    return info[op];
}

#endif // MOBIUS_VM_OPCODES_H
