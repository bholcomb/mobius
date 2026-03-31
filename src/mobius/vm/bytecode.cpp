#include "vm/bytecode.h"

#include <cstdio>
#include <cstring>

static void print_constant(const Prototype* proto, int idx) {
    if (idx < 0 || idx >= (int)proto->constants.size()) {
        printf("K[?]");
        return;
    }
    const Value& v = proto->constants[idx];
    switch (v.type) {
        case VAL_NIL:
            printf("nil");
            break;
        case VAL_BOOL:
            printf("%s", v.as.boolean ? "true" : "false");
            break;
        case VAL_INT64:
            printf("%ld", (long)v.as.i64);
            break;
        case VAL_FLOAT64:
            printf("%g", v.as.double_val);
            break;
        case VAL_STRING:
            printf("\"%s\"", v.as.string ? v.as.string->data : "(null)");
            break;
        default:
            printf("<%s>", value_type_name(v.type));
            break;
    }
}

static void print_rk(const Prototype* proto, uint8_t rk) {
    if (IS_CONSTANT(rk)) {
        int idx = RK_AS_CONSTANT(rk);
        printf("K[%d]=", idx);
        print_constant(proto, idx);
    } else {
        printf("R[%d]", rk);
    }
}

void disassemble_instruction(const Prototype* proto, int offset) {
    uint32_t inst = proto->code[offset];
    OpCode op = (OpCode)DECODE_OP(inst);
    const OpcodeInfo& info = opcode_info(op);

    int line = (offset < (int)proto->line_info.size()) ? proto->line_info[offset] : 0;
    printf("%04d ", offset);

    if (line > 0) {
        printf("[L%4d] ", line);
    } else {
        printf("[     ] ");
    }

    printf("%-12s", info.name);

    switch (info.format) {
        case FMT_ABC: {
            uint8_t a = DECODE_A(inst);
            uint8_t b = DECODE_B(inst);
            uint8_t c = DECODE_C(inst);

            switch (op) {
                case OP_MOVE:
                    printf("R[%d] = R[%d]", a, b);
                    break;
                case OP_LOADNIL:
                    printf("R[%d]..R[%d] = nil", a, a + b);
                    break;
                case OP_LOADBOOL:
                    printf("R[%d] = %s", a, b ? "true" : "false");
                    if (c) printf("; skip next");
                    break;
                case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_MOD:
                case OP_BAND: case OP_BOR: case OP_BXOR: case OP_SHL: case OP_SHR: {
                    const char* sym = "?";
                    switch (op) {
                        case OP_ADD: sym = "+"; break;
                        case OP_SUB: sym = "-"; break;
                        case OP_MUL: sym = "*"; break;
                        case OP_DIV: sym = "/"; break;
                        case OP_MOD: sym = "%"; break;
                        case OP_BAND: sym = "&"; break;
                        case OP_BOR:  sym = "|"; break;
                        case OP_BXOR: sym = "^"; break;
                        case OP_SHL:  sym = "<<"; break;
                        case OP_SHR:  sym = ">>"; break;
                        default: break;
                    }
                    printf("R[%d] = ", a);
                    print_rk(proto, b);
                    printf(" %s ", sym);
                    print_rk(proto, c);
                    break;
                }
                case OP_UNM:
                    printf("R[%d] = -R[%d]", a, b);
                    break;
                case OP_NOT:
                    printf("R[%d] = not R[%d]", a, b);
                    break;
                case OP_BNOT:
                    printf("R[%d] = ~R[%d]", a, b);
                    break;
                case OP_EQ: case OP_LT: case OP_LE: {
                    const char* sym = "?";
                    switch (op) {
                        case OP_EQ: sym = "=="; break;
                        case OP_LT: sym = "<";  break;
                        case OP_LE: sym = "<="; break;
                        default: break;
                    }
                    printf("if (");
                    print_rk(proto, b);
                    printf(" %s ", sym);
                    print_rk(proto, c);
                    printf(") %s= %d then skip", op == OP_EQ ? "!" : "!", a);
                    break;
                }
                case OP_TEST:
                    printf("if R[%d] %s then skip", a, c ? "is false" : "is true");
                    break;
                case OP_TESTSET:
                    printf("if R[%d] %s then R[%d] = R[%d] else skip",
                           b, c ? "is true" : "is false", a, b);
                    break;
                case OP_CALL:
                    printf("R[%d](", a);
                    if (b == 0) printf("varargs");
                    else if (b == 1) printf("");
                    else printf("%d args", b - 1);
                    printf(") -> ");
                    if (c == 0) printf("multi-ret");
                    else if (c == 1) printf("0 results");
                    else printf("%d results", c - 1);
                    break;
                case OP_TAILCALL:
                    printf("tailcall R[%d](%d args)", a, b - 1);
                    break;
                case OP_RETURN:
                    if (b == 1) printf("return");
                    else if (b == 0) printf("return R[%d]..top", a);
                    else printf("return R[%d]..R[%d]", a, a + b - 2);
                    break;
                case OP_INDEX_GET:
                    printf("R[%d] = R[%d][", a, b);
                    print_rk(proto, c);
                    printf("]");
                    break;
                case OP_INDEX_SET:
                    printf("R[%d][", a);
                    print_rk(proto, b);
                    printf("] = ");
                    print_rk(proto, c);
                    break;
                case OP_NEWTABLE:
                    printf("R[%d] = {} (array=%d, hash=%d)", a, b, c);
                    break;
                case OP_NEWARRAY:
                    printf("R[%d] = [] (cap=%d)", a, b);
                    break;
                case OP_GETUPVAL:
                    printf("R[%d] = UpVal[%d]", a, b);
                    break;
                case OP_SETUPVAL:
                    printf("UpVal[%d] = R[%d]", b, a);
                    break;
                case OP_INC:
                    printf("R[%d] = R[%d] + 1", a, b);
                    break;
                case OP_DEC:
                    printf("R[%d] = R[%d] - 1", a, b);
                    break;
                case OP_ENUMVAL:
                    printf("enum R[%d].member[%d] = R[%d]", a, c, b);
                    break;
                case OP_GETENUM:
                    printf("R[%d] = R[%d].member[%d]", a, b, c);
                    break;
                case OP_TFORLOOP:
                    printf("tforloop R[%d], %d results", a, c);
                    break;
                case OP_IMPORT:
                    printf("import ");
                    print_rk(proto, b);
                    printf(" as ");
                    print_rk(proto, c);
                    break;
                case OP_TYPECHECK:
                    printf("typecheck R[%d] as type %d", a, b);
                    break;
                case OP_ISNUM:
                    printf("R[%d] = isnumeric(R[%d])", a, b);
                    break;
                case OP_TYPECOMPAT:
                    printf("if comparable(RK(%d), RK(%d)) != %d skip", b, c, a);
                    break;
                case OP_NOP:
                    printf("(nop)");
                    break;
                default:
                    printf("A=%d B=%d C=%d", a, b, c);
                    break;
            }
            break;
        }

        case FMT_ABx: {
            uint8_t a = DECODE_A(inst);
            uint16_t bx = DECODE_Bx(inst);

            switch (op) {
                case OP_LOADK:
                    printf("R[%d] = ", a);
                    print_constant(proto, bx);
                    break;
                case OP_GETGLOBAL:
                    printf("R[%d] = globals[", a);
                    print_constant(proto, bx);
                    printf("]");
                    break;
                case OP_SETGLOBAL:
                    printf("globals[");
                    print_constant(proto, bx);
                    printf("] = R[%d]", a);
                    break;
                case OP_CLOSURE:
                    printf("R[%d] = closure(proto[%d])", a, bx);
                    break;
                case OP_NEWENUM:
                    printf("R[%d] = enum ", a);
                    print_constant(proto, bx);
                    break;
                // OP_IMPORT is now FMT_ABC — handled in ABC branch
                case OP_PRAGMA:
                    printf("pragma ");
                    print_constant(proto, bx);
                    printf(" = R[%d]", a);
                    break;
                default:
                    printf("A=%d Bx=%d", a, bx);
                    break;
            }
            break;
        }

        case FMT_AsBx: {
            uint8_t a = DECODE_A(inst);
            int sbx = DECODE_sBx(inst);

            switch (op) {
                case OP_LOADINT:
                    printf("R[%d] = %d", a, sbx);
                    break;
                default:
                    printf("A=%d sBx=%d", a, sbx);
                    break;
            }
            break;
        }

        case FMT_sBx: {
            int sbx = DECODE_sBx_wide(inst);
            printf("pc += %d (-> %04d)", sbx, offset + 1 + sbx);
            break;
        }

        case FMT_ABC_D: {
            uint8_t a = DECODE_A(inst);
            uint8_t b = DECODE_B(inst);
            uint8_t c = DECODE_C(inst);
            uint64_t raw = 0;
            if (offset + 2 < (int)proto->code.size()) {
                raw = (uint64_t)proto->code[offset + 1]
                    | ((uint64_t)proto->code[offset + 2] << 32);
            }
            const char* sym = "?";
            switch (op) {
                case OP_ADDK: sym = "+"; break;
                case OP_SUBK: sym = "-"; break;
                case OP_MULK: sym = "*"; break;
                case OP_DIVK: sym = "/"; break;
                case OP_MODK: sym = "%"; break;
                default: break;
            }
            printf("R[%d] = R[%d] %s ", a, b, sym);
            if (c == VAL_INT64) {
                int64_t k; memcpy(&k, &raw, 8);
                printf("%ld", (long)k);
            } else {
                double k; memcpy(&k, &raw, 8);
                printf("%g", k);
            }
            printf(" (+2 data words)");
            break;
        }

        case FMT_FUSED2: {
            uint8_t a = DECODE_A(inst);
            uint8_t b = DECODE_B(inst);
            if (op == OP_MOVE_ADDI) {
                uint32_t inst2 = (offset + 1 < (int)proto->code.size()) ? proto->code[offset + 1] : 0;
                int sbx = DECODE_sBx(inst2);
                printf("R[%d] = R[%d]; R[%d] += %d (+1 fused word)", a, b, a, sbx);
            } else if (op == OP_GETGLOBAL_INDEX_GET) {
                uint32_t inst2 = (offset + 1 < (int)proto->code.size()) ? proto->code[offset + 1] : 0;
                uint8_t c2 = DECODE_C(inst2);
                uint16_t bx = DECODE_Bx(inst);
                printf("R[%d] = globals[%d][", a, bx);
                print_rk(proto, c2);
                printf("] (+1 fused word)");
            } else if (op == OP_GETGLOBAL_CALL) {
                uint32_t inst2 = (offset + 1 < (int)proto->code.size()) ? proto->code[offset + 1] : 0;
                uint8_t b2 = DECODE_B(inst2);
                uint8_t c2 = DECODE_C(inst2);
                uint16_t bx = DECODE_Bx(inst);
                printf("R[%d] = globals[%d]; call R[%d] %d args %d results (+1 fused word)",
                       a, bx, a, b2 - 1, c2 - 1);
            } else {
                printf("A=%d B=%d (+fused)", a, b);
            }
            break;
        }
    }

    printf("\n");
}

void disassemble_prototype(const Prototype* proto, const char* label) {
    if (label) {
        printf("== %s ==\n", label);
    } else if (!proto->name.empty()) {
        printf("== %s ==\n", proto->name.c_str());
    } else {
        printf("== <script> ==\n");
    }

    printf("params: %d, registers: %d, upvalues: %zu\n",
           proto->num_params, proto->num_registers, proto->upvalues.size());

    if (!proto->constants.empty()) {
        printf("constants (%zu):\n", proto->constants.size());
        for (int i = 0; i < (int)proto->constants.size(); i++) {
            printf("  K[%d] = ", i);
            print_constant(proto, i);
            printf("\n");
        }
    }

    printf("code (%zu instructions):\n", proto->code.size());
    for (int i = 0; i < (int)proto->code.size(); i++) {
        printf("  ");
        disassemble_instruction(proto, i);
        OpCode op = (OpCode)DECODE_OP(proto->code[i]);
        const OpcodeInfo& inf = opcode_info(op);
        if (inf.format == FMT_ABC_D) i += 2;
        else if (inf.format == FMT_FUSED2) i += 1;
    }

    for (int i = 0; i < (int)proto->protos.size(); i++) {
        printf("\n");
        char buf[128];
        snprintf(buf, sizeof(buf), "%s.proto[%d]",
                 proto->name.empty() ? "<script>" : proto->name.c_str(), i);
        disassemble_prototype(proto->protos[i], buf);
    }
}
