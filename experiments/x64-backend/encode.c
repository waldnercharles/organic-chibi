#include "../../src/common.h"

#include <stdio.h>
#include <string.h>

enum
{
    INFO_SCALE = 0x03,
    INFO_FLOAT = 0x04,
    INFO_SIGNED = 0x08,
};

typedef struct
{
    u32 at;
    u32 next;
} Fixup;

#include "../../src/jit/x64_target.h"

typedef struct
{
    Code *code;
    Code *at;
    Fixup *fixups;
    u32 nfixups;
    u32 fixups_cap;
    const char *error;
} Assembler;

static u32
here(const Assembler *as)
{
    return (u32)(as->at - as->code);
}

static void
add_fixup(Assembler *as, u32 *list)
{
    (void)list;
    as->nfixups++;
}

static bool
is_float(u8 info)
{
    return info & INFO_FLOAT;
}

static u32
bits_of(u8 info)
{
    return 8u << (info & INFO_SCALE);
}

static bool
is_signed(u8 info)
{
    return info & INFO_SIGNED;
}

#include "../../src/jit/x64_encode.h"

static const char *const R64[] = { "rax", "rcx", "rdx", "rbx", "rsp", "rbp",
                                   "rsi", "rdi", "r8",  "r9",  "r10", "r11",
                                   "r12", "r13", "r14", "r15" };
static const char *const R32[] = { "eax",  "ecx",  "edx",  "ebx",
                                   "esp",  "ebp",  "esi",  "edi",
                                   "r8d",  "r9d",  "r10d", "r11d",
                                   "r12d", "r13d", "r14d", "r15d" };
static const char *const R16[] = { "ax",   "cx",   "dx",   "bx",
                                   "sp",   "bp",   "si",   "di",
                                   "r8w",  "r9w",  "r10w", "r11w",
                                   "r12w", "r13w", "r14w", "r15w" };
static const char *const R8N[] = { "al",   "cl",   "dl",   "bl",
                                   "spl",  "bpl",  "sil",  "dil",
                                   "r8b",  "r9b",  "r10b", "r11b",
                                   "r12b", "r13b", "r14b", "r15b" };
static const char *const ALU[] = { "add", "or",  "adc", "sbb",
                                   "and", "sub", "xor", "cmp" };
static const char *const CC[] = { "o", "no", "b", "ae", "e", "ne", "be", "a",
                                  "s", "ns", "p", "np", "l", "ge", "le", "g" };

static Code buffer[64];
static Assembler as;

static void
start(void)
{
    as = (Assembler){ .code = buffer, .at = buffer };
}

static void
line(const char *text)
{
    for (Code *p = buffer; p < as.at; p++) {
        printf("%02x", *p);
    }
    printf("\t%s\n", text);
}

static const char *
reg(u32 w, u32 r)
{
    return w ? R64[r] : R32[r];
}

static void
mem(char *out, size_t size, const char *kind, u32 base, int32_t disp)
{
    if (disp) {
        snprintf(out, size, "%s ptr [%s %c %d]", kind, R64[base], disp < 0 ? '-' : '+', disp < 0 ? -disp : disp);
    } else {
        snprintf(out, size, "%s ptr [%s]", kind, R64[base]);
    }
}

int
main(void)
{
    static const int64_t IMMS[] = { 0,    1,    -1,         127,          128,
                                    -128, -129, 0x7FFFFFFF, -0x80000000LL };
    static const u64 MOVS[] = { 0,
                                5,
                                0xFFFFFFFFu,
                                0x100000000ull,
                                0xFFFFFFFFFFFFFFFFull,
                                0xFFFFFFFF80000000ull,
                                0x123456789ABCDEFull };
    static const int32_t DISPS[] = { 0, 8, -8, 127, 128, -129, 4096, 0x7FFFFFF0 };
    char text[160], m[96];

    for (u32 w = 0; w < 2; w++) {
        for (u32 d = 0; d < 16; d++) {
            for (u32 s = 0; s < 16; s++) {
                if (d != s) {
                    start();
                    mov_rr(&as, w, d, s);
                    snprintf(text, sizeof text, "mov %s, %s", reg(w, d), reg(w, s));
                    line(text);
                }
                for (u32 k = 0; k < 8; k++) {
                    start();
                    alu_rr(&as, k, w, d, s);
                    snprintf(text, sizeof text, "%s %s, %s", ALU[k], reg(w, d), reg(w, s));
                    line(text);
                }
                start();
                test_rr(&as, w, d, s);
                snprintf(text, sizeof text, "test %s, %s", reg(w, d), reg(w, s));
                line(text);
                start();
                imul_rr(&as, w, d, s);
                snprintf(text, sizeof text, "imul %s, %s", reg(w, d), reg(w, s));
                line(text);
                start();
                imul_imm(&as, w, d, s, 100);
                snprintf(text, sizeof text, "imul %s, %s, 100", reg(w, d), reg(w, s));
                line(text);
                start();
                imul_imm(&as, w, d, s, 1000);
                snprintf(text, sizeof text, "imul %s, %s, 1000", reg(w, d), reg(w, s));
                line(text);
                start();
                extend(&as, 8, false, d, s);
                snprintf(text, sizeof text, "movzx %s, %s", R32[d], R8N[s]);
                line(text);
                start();
                extend(&as, 8, true, d, s);
                snprintf(text, sizeof text, "movsx %s, %s", R32[d], R8N[s]);
                line(text);
                start();
                extend(&as, 16, false, d, s);
                snprintf(text, sizeof text, "movzx %s, %s", R32[d], R16[s]);
                line(text);
                start();
                extend(&as, 16, true, d, s);
                snprintf(text, sizeof text, "movsx %s, %s", R32[d], R16[s]);
                line(text);
                start();
                extend(&as, 32, true, d, s);
                snprintf(text, sizeof text, "movsxd %s, %s", R64[d], R32[s]);
                line(text);
                start();
                sign_extend_wide(&as, 8, d, s);
                snprintf(text, sizeof text, "movsx %s, %s", R64[d], R8N[s]);
                line(text);
                start();
                sign_extend_wide(&as, 16, d, s);
                snprintf(text, sizeof text, "movsx %s, %s", R64[d], R16[s]);
                line(text);
            }
            for (u32 k = 0; k < 8; k++) {
                for (u32 j = 0; j < sizeof IMMS / sizeof IMMS[0]; j++) {
                    start();
                    alu_imm(&as, k, w, d, (int32_t)IMMS[j]);
                    snprintf(text, sizeof text, "%s %s, %lld", ALU[k], reg(w, d), (long long)IMMS[j]);
                    line(text);
                }
            }
            start();
            test_imm(&as, w, d, 0x1000);
            snprintf(text, sizeof text, "test %s, 4096", reg(w, d));
            line(text);
            start();
            unary_r(&as, UNARY_NEG, w, d);
            snprintf(text, sizeof text, "neg %s", reg(w, d));
            line(text);
            start();
            unary_r(&as, UNARY_NOT, w, d);
            snprintf(text, sizeof text, "not %s", reg(w, d));
            line(text);
            start();
            shift_imm(&as, SHIFT_SHR, w, d, 7);
            snprintf(text, sizeof text, "shr %s, 7", reg(w, d));
            line(text);
            for (u32 j = 0; j < sizeof MOVS / sizeof MOVS[0]; j++) {
                start();
                mov_imm(&as, d, MOVS[j], w);
                u64 v = w ? MOVS[j] : MOVS[j] & 0xFFFFFFFFu;
                if (w && v > 0xFFFFFFFFu && (int64_t)v >= INT32_MIN && (int64_t)v < 0) {
                    snprintf(text, sizeof text, "mov %s, %lld", R64[d], (long long)(int64_t)v);
                } else if (w && v > 0xFFFFFFFFu) {
                    snprintf(text, sizeof text, "movabs %s, %llu", R64[d], (unsigned long long)v);
                } else {
                    snprintf(text, sizeof text, "mov %s, %llu", R32[d], (unsigned long long)v);
                }
                line(text);
            }
        }
    }
    for (u32 d = 0; d < 16; d++) {
        for (u32 c = 0; c < 16; c++) {
            start();
            rr(&as, 0, OP_SETCC | c, 0, 0, d, byte_reg(d));
            snprintf(text, sizeof text, "set%s %s", CC[c], R8N[d]);
            line(text);
        }
        start();
        push_reg(&as, d);
        snprintf(text, sizeof text, "push %s", R64[d]);
        line(text);
        start();
        pop_reg(&as, d);
        snprintf(text, sizeof text, "pop %s", R64[d]);
        line(text);
        for (u32 b = 0; b < 16; b++) {
            for (u32 j = 0; j < sizeof DISPS / sizeof DISPS[0]; j++) {
                int32_t disp = DISPS[j];
                start();
                lea(&as, d, b, disp);
                mem(m, sizeof m, "qword", b, disp);
                snprintf(text, sizeof text, "lea %s, %s", R64[d], m + 10);
                line(text);
                start();
                rm(&as, 0, OP_MOV_RM_R, 1, d, b, disp, false);
                mem(m, sizeof m, "qword", b, disp);
                snprintf(text, sizeof text, "mov %s, %s", m, R64[d]);
                line(text);
                start();
                rm(&as, 0, OP_MOV_R_RM, 0, d, b, disp, false);
                mem(m, sizeof m, "dword", b, disp);
                snprintf(text, sizeof text, "mov %s, %s", R32[d], m);
                line(text);
                start();
                rm(&as, 0, OP_MOV_RM_R8, 0, d, b, disp, byte_reg(d));
                mem(m, sizeof m, "byte", b, disp);
                snprintf(text, sizeof text, "mov %s, %s", m, R8N[d]);
                line(text);
                start();
                rm(&as, PREFIX_16, OP_MOV_RM_R, 0, d, b, disp, false);
                mem(m, sizeof m, "word", b, disp);
                snprintf(text, sizeof text, "mov %s, %s", m, R16[d]);
                line(text);
                start();
                rm(&as, 0, OP_MOVSX16, 0, d, b, disp, false);
                mem(m, sizeof m, "word", b, disp);
                snprintf(text, sizeof text, "movsx %s, %s", R32[d], m);
                line(text);
            }
        }
    }
    for (u32 j = 0; j < sizeof DISPS / sizeof DISPS[0]; j++) {
        int32_t disp = DISPS[j];
        for (u32 bits = 8; bits <= 64; bits *= 2) {
            start();
            store_imm(
                &as,
                (u8)(bits == 8    ? 0
                     : bits == 16 ? 1
                     : bits == 32 ? 2
                                  : 3),
                -5,
                (u32)disp
            );
            const char *kind = bits == 8  ? "byte"
                             : bits == 16 ? "word"
                             : bits == 32 ? "dword"
                                          : "qword";
            mem(m, sizeof m, kind, RSP, disp);
            snprintf(text, sizeof text, "mov %s, -5", m);
            line(text);
        }
        start();
        store_spill(&as, 32 + 3, (u32)disp);
        mem(m, sizeof m, "qword", RSP, disp);
        snprintf(text, sizeof text, "movsd %s, xmm3", m);
        line(text);
        start();
        load_spill(&as, 12, (u32)disp, true);
        snprintf(text, sizeof text, "movsd xmm12, %s", m);
        line(text);
    }
    for (u32 k = 1; k < 16; k++) {
        start();
        trap_kind(&as, k);
        snprintf(text, sizeof text, "ud1 eax, dword ptr [rax + %u]", k);
        line(text);
    }
    start();
    move_reg(&as, 3, 12, true);
    line("movaps xmm3, xmm12");
    start();
    load_immediate(&as, 9, 0, INFO_FLOAT | 3);
    line("xorps xmm9, xmm9");
    start();
    put(&as, OP_LEAVE);
    line("leave");
    start();
    touch_stack(&as);
    line("or qword ptr [rsp], 0");
    start();
    sub_rsp(&as, 4096);
    line("sub rsp, 4096");
    start();
    sub_rsp(&as, 24);
    line("sub rsp, 24");
    return 0;
}
