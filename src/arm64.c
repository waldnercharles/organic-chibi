#define _DEFAULT_SOURCE

#include "arm64.h"

#include <assert.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#ifdef __APPLE__
#include <libkern/OSCacheControl.h>
#include <pthread.h>
#endif

enum
{
    NO_REG = 0xFF,
    NO_VALUE = 0xFFFFFFFFu,
    X8 = 8,
    X16 = 16,
    X17 = 17,
    X29 = 29,
    ZR = 31,
    SP = 31,
    V29 = 29,
    V30 = 30,
    V31 = 31,
    INT_POOL = 0x0000FE00u,
    FLOAT_POOL = 0x1FFF0000u,
    CALLEE_INT = 0x1FF80000u,
    CALLEE_FLOAT = 0x0000FF00u,
    ARGUMENTS = 0x000000FFu,
    ALIAS = 64,
    PROLOGUE_WORDS = 13,
    PROBE_INTERVAL = 4096,
    FRAME_LIMIT = 1u << 24,
    UNROLLED_BYTES = 64,
    WORDS_PER_FUNCTION = 96,
    WORDS_PER_INSTRUCTION = 32,
    WORDS_PER_ARGUMENT = 8,
    WORDS_PER_ADAPTER = 24,
    WORDS_PER_ADAPTER_PARAM = 8,
    ADAPTER_STACK_LIMIT = 4064,
};

enum
{
    COND_EQ,
    COND_NE,
    COND_HS,
    COND_LO,
    COND_MI,
    COND_PL,
    COND_VS,
    COND_VC,
    COND_HI,
    COND_LS,
    COND_GE,
    COND_LT,
    COND_GT,
    COND_LE,
};

enum
{
    OP_ADD = 0x0B000000u,
    OP_ADDS = 0x2B000000u,
    OP_SUB = 0x4B000000u,
    OP_SUBS = 0x6B000000u,
    OP_AND = 0x0A000000u,
    OP_ORR = 0x2A000000u,
    OP_EOR = 0x4A000000u,
    OP_ORN = 0x2A200000u,
    OP_LSLV = 0x1AC02000u,
    OP_LSRV = 0x1AC02400u,
    OP_ASRV = 0x1AC02800u,
    OP_SDIV = 0x1AC00C00u,
    OP_UDIV = 0x1AC00800u,
    OP_MUL = 0x1B007C00u,
    OP_MADD = 0x1B000000u,
    OP_MSUB = 0x1B008000u,
    OP_SMULL = 0x9B207C00u,
    OP_UMULL = 0x9BA07C00u,
    OP_SMULH = 0x9B407C00u,
    OP_UMULH = 0x9BC07C00u,
    OP_ADD_IMM = 0x11000000u,
    OP_ADDS_IMM = 0x31000000u,
    OP_SUB_IMM = 0x51000000u,
    OP_SUBS_IMM = 0x71000000u,
    OP_MOVZ = 0x52800000u,
    OP_MOVN = 0x12800000u,
    OP_MOVK = 0x72800000u,
    OP_SXTB = 0x13001C00u,
    OP_SXTH = 0x13003C00u,
    OP_SXTW = 0x93407C00u,
    OP_UXTB = 0x53001C00u,
    OP_UXTH = 0x53003C00u,
    OP_CSET = 0x1A9F07E0u,
    OP_CSNEG = 0x5A800400u,
    OP_AND_IMM = 0x12000000u,
    OP_ORR_IMM = 0x32000000u,
    OP_EOR_IMM = 0x52000000u,
    OP_ANDS_IMM = 0x72000000u,
    OP_ASR_IMM = 0x13007C00u,
    OP_LSR_IMM = 0x53007C00u,
    SHIFT_LSR = 0x00400000u,
    SHIFT_ASR = 0x00800000u,
    SHIFT_IMM12 = 0x00400000u,
    OP_SUBS_EXT = 0x6B200000u,
    OP_ANDS = 0x6A000000u,
    OP_EOR_ONE = 0x52000000u,
    OP_B = 0x14000000u,
    OP_B_COND = 0x54000000u,
    OP_CBZ = 0x34000000u,
    OP_CBNZ = 0x35000000u,
    OP_BLR_X16 = 0xD63F0200u,
    OP_BR = 0xD61F0000u,
    OP_ADR = 0x10000000u,
    OP_LDRSW_SCALED = 0xB8A07800u,
    OP_RET = 0xD65F03C0u,
    OP_BRK = 0xD4200000u,
    OP_NOP = 0xD503201Fu,
    OP_ADRP = 0x90000000u,
    OP_LDR_X16_X16 = 0xF9400210u,
    OP_PUSH_FRAME = 0xA9BF7BFDu,
    OP_PUSH_CONTEXT_FRAME = 0xA9BE7BFDu,
    OP_SAVE_CONTEXT = 0xF9000BA0u,
    OP_LOAD_CONTEXT = 0xF9400BA0u,
    OP_SET_FRAME = 0x910003FDu,
    OP_RESET_SP = 0x910003BFu,
    OP_POP_FRAME = 0xA8C17BFDu,
    OP_POP_CONTEXT_FRAME = 0xA8C27BFDu,
    OP_SUB_SP = 0xD10003FFu,
    OP_STP_X = 0xA9000000u,
    OP_STP_X_POST = 0xA8800000u,
    OP_LDP_Q = 0xAD400000u,
    OP_STP_Q = 0xAD000000u,
    OP_LDP_Q_POST = 0xACC00000u,
    OP_STP_Q_POST = 0xAC800000u,
    OP_LDR_Q = 0x3DC00000u,
    OP_STR_Q = 0x3D800000u,
    OP_STR_ZR_SP = 0xF90003FFu,
    OP_LDP_X = 0xA9400000u,
    OP_STP_D = 0x6D000000u,
    OP_LDP_D = 0x6D400000u,
    OP_STP_S = 0x2D000000u,
    OP_LDP_S = 0x2D400000u,
    OP_SUB_SP_HIGH = 0xD14003FFu,
    OP_ADD_FROM_SP = 0x910003E0u,
    OP_ADD_HIGH = 0x91400000u,
    OP_LDRB = 0x39400000u,
    OP_LDRSB = 0x39C00000u,
    OP_LDRH = 0x79400000u,
    OP_LDRSH = 0x79C00000u,
    OP_LDRW = 0xB9400000u,
    OP_LDRX = 0xF9400000u,
    OP_STRB = 0x39000000u,
    OP_STRH = 0x79000000u,
    OP_STRW = 0xB9000000u,
    OP_STRX = 0xF9000000u,
    OP_LDRS = 0xBD400000u,
    OP_LDRD = 0xFD400000u,
    OP_STRS = 0xBD000000u,
    OP_STRD = 0xFD000000u,
    OP_FADD = 0x1E202800u,
    OP_FSUB = 0x1E203800u,
    OP_FMUL = 0x1E200800u,
    OP_FDIV = 0x1E201800u,
    OP_FNEG = 0x1E214000u,
    OP_FMOV = 0x1E204000u,
    OP_FCMP = 0x1E202000u,
    OP_FMOV_FROM_W = 0x1E270000u,
    OP_FMOV_FROM_X = 0x9E670000u,
    OP_FCVT_TO_D = 0x1E22C000u,
    OP_FCVT_TO_S = 0x1E624000u,
    OP_SCVTF = 0x1E220000u,
    OP_UCVTF = 0x1E230000u,
    OP_FCVTZS = 0x1E380000u,
    OP_FCVTZU = 0x1E390000u,
    OP_FMOV_TO_W = 0x1E260000u,
    OP_FMOV_TO_X = 0x9E660000u,
    OP_CNT_8B = 0x0E205800u,
    OP_ADDV_8B = 0x0E31B800u,
    OP_RBIT = 0x5AC00000u,
    OP_CLZ = 0x5AC01000u,
    OP_VADD = 0x4E208400u,
    OP_VSUB = 0x6E208400u,
    OP_VMUL = 0x4E209C00u,
    OP_VAND = 0x4E201C00u,
    OP_VORR = 0x4EA01C00u,
    OP_VEOR = 0x6E201C00u,
    OP_CMEQ = 0x6E208C00u,
    OP_CMGT = 0x4E203400u,
    OP_CMGE = 0x4E203C00u,
    OP_CMHI = 0x6E203400u,
    OP_CMHS = 0x6E203C00u,
    OP_TBL = 0x4E000000u,
    OP_SSHL = 0x4E204400u,
    OP_USHL = 0x6E204400u,
    OP_VNEG = 0x6E20B800u,
    OP_VNOT = 0x6E205800u,
    OP_DUP = 0x4E000C00u,
    OP_UMOV_D = 0x4E083C00u,
    OP_UMOV_B = 0x0E013C00u,
    OP_INS_D1 = 0x4E181C00u,
    OP_SHL_IMM = 0x4F005400u,
    OP_SSHR_IMM = 0x4F000400u,
    OP_USHR_IMM = 0x6F000400u,
    OP_USRA = 0x6F001400u,
    OP_MOVI_8 = 0x4F00E400u,
    OP_MOVI_16 = 0x4F008400u,
    OP_MOVI_32 = 0x4F000400u,
    OP_MOVI_64 = 0x6F00E400u,
    OP_ADC = 0x1A000000u,
    OP_ADCS = 0x3A000000u,
    OP_SBC = 0x5A000000u,
    OP_SBCS = 0x7A000000u,
    OP_BICS = 0x6A200000u,
    OP_CCMP = 0x7A400000u,
    OP_CCMP_IMM = 0x7A400800u,
    OP_CSEL = 0x1A800000u,
    OP_CSINC = 0x1A800400u,
    OP_CSINV = 0x5A800000u,
    OP_EXTR = 0x93C00000u,
    OP_LSL_IMM = 0xD3400000u,
    OP_BL = 0x94000000u,
    OP_TBZ = 0x36000000u,
    OP_TBNZ = 0x37000000u,
    OP_LDP_X_POST = 0xA8C00000u,
    OP_STP_X_PRE = 0xA9800000u,
    OP_FABS = 0x1E20C000u,
    OP_FCMP_ZERO = 0x1E202008u,
    OP_FCSEL = 0x1E200C00u,
    SF = 0x80000000u,
    DOUBLE = 0x00400000u,
};

enum
{
    INFO_SCALE = 0x03,
    INFO_FLOAT = 0x04,
    INFO_SIGNED = 0x08,
    INFO_SMALL = 0x10,
    INFO_UNSUPPORTED = 0x20,
    INFO_MEMORY = 0x40,
    INFO_PAIR = 0x80,
    INFO_VECTOR = INFO_UNSUPPORTED | INFO_MEMORY,
    INFO_WIDE = INFO_VECTOR | INFO_PAIR | 3,
};

typedef struct
{
    u32 at;
    u32 next;
} Fixup;

typedef struct
{
    u32 at;
    u32 target;
} Branch;

typedef struct
{
    u64 lo;
    u64 hi;
    u32 target;
} Range;

typedef struct
{
    u32 target;
    u32 start;
    u32 skip;
    u32 ends;
    u32 first_branch;
    u32 first_arm;
    u32 table;
    u32 span;
    u32 stub;
    u64 min;
    u8 kind;
    u8 result;
    bool dead_else;
} Open;

typedef struct
{
    const Checker *c;
    const IrCode *ir;
    const IrFunc *fn;
    const u8 *info;
    const u8 *data;
    u8 *globals;
    u32 *code;
    u32 *at;
    u8 *regs;
    u8 *vinfo;
    u32 *spills;
    u32 values_cap;
    u32 holders[64];
    u32 free_int;
    u32 free_float;
    u32 free_callee_int;
    u32 free_callee_float;
    u32 saved_int;
    u32 saved_float;
    u32 pinned_int;
    u32 pinned_float;
    u8 *slot_regs;
    u32 slot_regs_cap;
    u32 *aliases;
    u32 *alias_at;
    u32 naliases;
    u32 epilogue;
    u32 *spill_free;
    u32 nspill_free;
    u32 spill_free_cap;
    u32 spill_top;
    u32 spill_max;
    u32 locals;
    u32 *slot_offsets;
    u32 slot_offsets_cap;
    const IrSlot *slots;
    IrSlot *retyped;
    u32 retyped_cap;
    u32 by_ref;
    u32 copies_at;
    u32 copied;
    u32 result_at;
    u32 place;
    bool called;
    bool context_saved;
    bool wide_record;
    void *process;
    Open *open;
    u32 nopen;
    u32 open_cap;
    Fixup *fixups;
    u32 nfixups;
    u32 fixups_cap;
    Branch *branches;
    u32 nbranches;
    u32 branches_cap;
    u32 *arm_at;
    u32 narms;
    u32 arms_cap;
    Range *ranges;
    u32 ranges_cap;
    u32 traps[NTRAPS];
    u32 divisions;
    u32 products;
    u32 fused;
    u8 fused_cond;
    u32 tested;
    const char *error;
    const u64 *adapters;
    u32 *homes;
    u32 nhomes;
    u32 homes_cap;
} Emit;

static void *
grow(void *p, u32 *cap, u32 need, size_t size)
{
    if (need <= *cap) {
        return p;
    }
    u32 c = *cap ? *cap : 16;
    while (c < need) {
        c *= 2;
    }
    *cap = c;
    return realloc(p, c * size);
}

static void
put(Emit *e, u32 word)
{
    *e->at++ = word;
}

static u32
here(const Emit *e)
{
    return (u32)(e->at - e->code);
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

static bool
is_small(u8 info)
{
    return info & INFO_SMALL;
}

static u32
width(u8 info)
{
    return (info & INFO_SCALE) == 3 ? SF : 0;
}

static u32
precision(u8 info)
{
    return (info & INFO_SCALE) == 3 ? DOUBLE : 0;
}

static bool
is_vector(u8 info)
{
    return (info & INFO_VECTOR) == INFO_VECTOR;
}

static bool
is_wide(u8 info)
{
    return (info & (INFO_VECTOR | INFO_PAIR)) == (INFO_VECTOR | INFO_PAIR);
}

static u8
type_info(const Checker *c, u32 type)
{
    u8 kind = c->types.kinds[type];
    if (kind == TYPE_ENUM) {
        u32 backing = (u32)c->types.arg1[type];
        return backing ? type_info(c, backing) : INFO_UNSUPPORTED;
    }
    if (kind == TYPE_POINTER || kind == TYPE_MANY || kind == TYPE_FN ||
        kind == TYPE_OPTIONAL) {
        return 3;
    }
    if (kind == TYPE_SLICE) {
        return 3 | INFO_PAIR;
    }
    if (kind == TYPE_ARRAY || kind == TYPE_STRUCT) {
        return INFO_MEMORY;
    }
    if (kind == TYPE_VECTOR) {
        return INFO_VECTOR |
               (type_info(c, c->types.arg0[type]) & (INFO_SCALE | INFO_SIGNED));
    }
    u32 bits = c->types.arg0[type];
    u8 scale = bits == 64 ? 3 : bits == 32 ? 2 : bits == 16 ? 1 : 0;
    bool integer = kind == TYPE_INT;
    if (integer && bits == 128) {
        return INFO_WIDE | (c->types.arg1[type] ? INFO_SIGNED : 0);
    }
    bool supported = kind == TYPE_NONE || kind == TYPE_VOID ||
                     kind == TYPE_BOOL || (integer && bits <= 64) ||
                     kind == TYPE_UINTPTR || kind == TYPE_FLOAT;
    return (u8)(scale | (kind == TYPE_FLOAT ? INFO_FLOAT : 0) |
                (supported ? 0 : INFO_UNSUPPORTED) |
                (integer && c->types.arg1[type] ? INFO_SIGNED : 0) |
                (integer && bits < 32 ? INFO_SMALL : 0));
}

static u8
operand_info(const Emit *e, u32 x)
{
    if (x & IR_CONST) {
        return e->info[e->ir->consts[e->fn->first_const + (x & ~IR_CONST)].type];
    }
    return e->vinfo[x];
}

static u64
constant_bits(const Emit *e, u32 x)
{
    return e->ir->consts[e->fn->first_const + (x & ~IR_CONST)].bits;
}

static u64
constant_high(const Emit *e, u32 x)
{
    return e->ir->consts[e->fn->first_const + (x & ~IR_CONST) + 1].bits;
}

static void
add_fixup(Emit *e, u32 *list)
{
    e->fixups = grow(e->fixups, &e->fixups_cap, e->nfixups + 1, sizeof *e->fixups);
    e->fixups[e->nfixups] = (Fixup){ here(e), *list };
    *list = e->nfixups++;
}

static void
patch_one(Emit *e, u32 at, u32 target)
{
    int32_t delta = (int32_t)target - (int32_t)at;
    u32 word = e->code[at];
    if ((word & 0x7C000000u) == OP_B) {
        e->code[at] = word | ((u32)delta & 0x03FFFFFFu);
    } else {
        if (delta < -(1 << 18) || delta >= (1 << 18)) {
            e->error = "a function is too large for a conditional branch";
        }
        e->code[at] = word | ((u32)delta & 0x7FFFFu) << 5;
    }
}

static void
patch(Emit *e, u32 list, u32 target)
{
    for (u32 f = list; f; f = e->fixups[f].next) {
        patch_one(e, e->fixups[f].at, target);
    }
}

static void
trap_if(Emit *e, u8 cond, u8 kind)
{
    add_fixup(e, &e->traps[kind]);
    put(e, OP_B_COND | cond);
}

static void
trap_unless_zero(Emit *e, u32 reg, u32 sf, u8 kind)
{
    add_fixup(e, &e->traps[kind]);
    put(e, OP_CBNZ | sf | reg);
}

static void
trap_if_zero(Emit *e, u32 reg, u32 sf, u8 kind)
{
    add_fixup(e, &e->traps[kind]);
    put(e, OP_CBZ | sf | reg);
}

static void
mov_imm(Emit *e, u32 reg, u64 value, u32 sf)
{
    u32 halves = sf ? 4 : 2;
    if (!sf) {
        value &= 0xFFFFFFFFu;
    }
    u32 zeros = 0, ones = 0;
    for (u32 i = 0; i < halves; i++) {
        u32 h = (u32)(value >> (16 * i)) & 0xFFFF;
        zeros += h == 0;
        ones += h == 0xFFFF;
    }

    bool inverted = ones > zeros;
    u32 fill = inverted ? 0xFFFF : 0;
    bool first = true;
    for (u32 i = 0; i < halves; i++) {
        u32 h = (u32)(value >> (16 * i)) & 0xFFFF;
        if (h == fill) {
            continue;
        }
        if (first) {
            u32 imm = inverted ? ~h & 0xFFFF : h;
            put(e, (inverted ? OP_MOVN : OP_MOVZ) | sf | i << 21 | imm << 5 | reg);
            first = false;
        } else {
            put(e, OP_MOVK | sf | i << 21 | h << 5 | reg);
        }
    }
    if (first) {
        put(e, (inverted ? OP_MOVN : OP_MOVZ) | sf | reg);
    }
}

static void
mov(Emit *e, u32 sf, u32 d, u32 m)
{
    if (d != m) {
        put(e, OP_ORR | sf | m << 16 | ZR << 5 | d);
    }
}

static void
fmov(Emit *e, u32 d, u32 n)
{
    if (d != n) {
        put(e, OP_FMOV | DOUBLE | n << 5 | d);
    }
}

static void
float_imm(Emit *e, u32 reg, u64 bits, bool is_double)
{
    if (bits == 0) {
        put(e, (is_double ? OP_FMOV_FROM_X : OP_FMOV_FROM_W) | ZR << 5 | reg);
        return;
    }
    mov_imm(e, X8, bits, is_double ? SF : 0);
    put(e, (is_double ? OP_FMOV_FROM_X : OP_FMOV_FROM_W) | X8 << 5 | reg);
}

static void
adrp(Emit *e, u32 d, u64 to)
{
    u64 pc = (u64)(uintptr_t)(e->code + here(e));
    int64_t pages = (int64_t)((to & ~0xFFFull) - (pc & ~0xFFFull)) >> 12;
    put(e, OP_ADRP | ((u32)pages & 3) << 29 | (((u32)pages >> 2) & 0x7FFFFu) << 5 | d);
}

static void
sp_address(Emit *e, u32 d, u32 offset)
{
    if (offset >= (1u << 24)) {
        e->error = "a function's frame is larger than 16 MB";
        return;
    }
    put(e, OP_ADD_FROM_SP | (offset & 0xFFF) << 10 | d);
    if (offset >> 12) {
        put(e, OP_ADD_HIGH | (offset >> 12) << 10 | d << 5 | d);
    }
}

static void
memory(Emit *e, u32 op, u32 scale, u32 rt, u32 offset, u32 address)
{
    if ((offset & ((1u << scale) - 1)) == 0 && (offset >> scale) < 4096) {
        put(e, op | (offset >> scale) << 10 | SP << 5 | rt);
        return;
    }
    sp_address(e, address, offset);
    put(e, op | address << 5 | rt);
}

static void
load_typed(Emit *e, u32 type, u32 rt, u32 offset)
{
    u8 t = e->info[type];
    if (is_float(t)) {
        bool d = bits_of(t) == 64;
        memory(e, d ? OP_LDRD : OP_LDRS, d ? 3 : 2, rt, offset, X8);
        return;
    }
    bool s = is_signed(t);
    switch (bits_of(t)) {
        case 8: memory(e, s ? OP_LDRSB : OP_LDRB, 0, rt, offset, rt); break;
        case 16: memory(e, s ? OP_LDRSH : OP_LDRH, 1, rt, offset, rt); break;
        case 32: memory(e, OP_LDRW, 2, rt, offset, rt); break;
        default: memory(e, OP_LDRX, 3, rt, offset, rt); break;
    }
}

static void
store_typed(Emit *e, u32 type, u32 rt, u32 offset)
{
    u8 t = e->info[type];
    if (is_float(t) && rt != ZR) {
        bool d = bits_of(t) == 64;
        memory(e, d ? OP_STRD : OP_STRS, d ? 3 : 2, rt, offset, X8);
        return;
    }
    switch (bits_of(t)) {
        case 8: memory(e, OP_STRB, 0, rt, offset, X8); break;
        case 16: memory(e, OP_STRH, 1, rt, offset, X8); break;
        case 32: memory(e, OP_STRW, 2, rt, offset, X8); break;
        default: memory(e, OP_STRX, 3, rt, offset, X8); break;
    }
}

static u32
load_op(u8 t)
{
    if (is_float(t)) {
        return bits_of(t) == 64 ? OP_LDRD : OP_LDRS;
    }
    bool s = is_signed(t);
    switch (bits_of(t)) {
        case 8: return s ? OP_LDRSB : OP_LDRB;
        case 16: return s ? OP_LDRSH : OP_LDRH;
        case 32: return OP_LDRW;
        default: return OP_LDRX;
    }
}

static u32
store_op(u8 t, u32 rt)
{
    if (is_float(t) && rt != ZR) {
        return bits_of(t) == 64 ? OP_STRD : OP_STRS;
    }
    switch (bits_of(t)) {
        case 8: return OP_STRB;
        case 16: return OP_STRH;
        case 32: return OP_STRW;
        default: return OP_STRX;
    }
}

static u32
slot_type(const Emit *e, u32 slot)
{
    return e->ir->slots[e->fn->first_slot + slot].type;
}

static u32
new_spill(Emit *e)
{
    if (e->nspill_free) {
        return e->spill_free[--e->nspill_free];
    }
    u32 offset = e->locals + e->spill_top;
    e->spill_top += 8;
    if (e->spill_top > e->spill_max) {
        e->spill_max = e->spill_top;
    }
    return offset;
}

static void
free_holder(Emit *e, u32 h)
{
    e->holders[h] = NO_VALUE;
    if (h >= 32) {
        u32 bit = 1u << (h - 32);
        *(bit & CALLEE_FLOAT ? &e->free_callee_float : &e->free_float) |= bit;
    } else {
        u32 bit = 1u << h;
        *(bit & CALLEE_INT ? &e->free_callee_int : &e->free_int) |= bit;
    }
}

static void
spill_reg(Emit *e, u32 h)
{
    u32 v = e->holders[h];
    u32 offset = new_spill(e);
    memory(e, h >= 32 ? OP_STRD : OP_STRX, 3, h & 31, offset, X8);
    free_holder(e, h);
    e->regs[v] = NO_REG;
    e->spills[v] = offset;
}

static void
spill_holders(Emit *e, u32 used, u32 base)
{
    for (; used; used &= used - 1) {
        u32 h = base + (u32)__builtin_ctz(used);
        if (e->holders[h] != NO_VALUE) {
            spill_reg(e, h);
        }
    }
}

static void
spill_all(Emit *e)
{
    u32 ints = (~e->free_int & INT_POOL) |
               (CALLEE_INT & ~e->free_callee_int & ~e->pinned_int);
    u32 floats = (~e->free_float & FLOAT_POOL) |
                 (CALLEE_FLOAT & ~e->free_callee_float & ~e->pinned_float);
    if ((ints | floats | e->naliases) == 0) {
        return;
    }
    spill_holders(e, ints, 0);
    spill_holders(e, floats, 32);
    for (u32 k = 0; k < e->naliases; k++) {
        u32 v = e->aliases[k];
        u32 h = e->regs[v] - ALIAS;
        u32 offset = new_spill(e);
        memory(e, h >= 32 ? OP_STRD : OP_STRX, 3, h & 31, offset, X8);
        e->regs[v] = NO_REG;
        e->spills[v] = offset;
    }
    e->naliases = 0;
}

static u32
take_reg(Emit *e, bool fl)
{
    u32 *free_mask = fl ? &e->free_float : &e->free_int;
    if (*free_mask == 0) {
        u32 oldest = NO_VALUE, at = 0;
        u32 base = fl ? 32 : 0;
        for (u32 used = fl ? FLOAT_POOL : INT_POOL; used; used &= used - 1) {
            u32 h = base + (u32)__builtin_ctz(used);
            if (e->holders[h] < oldest) {
                oldest = e->holders[h];
                at = h;
            }
        }
        assert(oldest != NO_VALUE);
        spill_reg(e, at);
    }
    u32 reg = (u32)__builtin_ctz(*free_mask);
    *free_mask &= *free_mask - 1;
    return reg;
}

static u32
alloc(Emit *e, u32 v, u8 info)
{
    bool fl = is_float(info);
    u32 reg = take_reg(e, fl);
    u32 h = (fl ? 32 : 0) + reg;
    e->holders[h] = v;
    e->regs[v] = (u8)h;
    e->vinfo[v] = info;
    return reg;
}

static void
add_alias(Emit *e, u32 v, u32 h, u8 info)
{
    e->alias_at[v] = e->naliases;
    e->aliases[e->naliases++] = v;
    e->regs[v] = (u8)(ALIAS + h);
    e->vinfo[v] = info;
}

static void
drop_alias(Emit *e, u32 k)
{
    assert(k < e->naliases);
    u32 last = e->aliases[--e->naliases];
    e->aliases[k] = last;
    e->alias_at[last] = k;
}

static bool
aliased(const Emit *e, u32 h)
{
    for (u32 k = 0; k < e->naliases; k++) {
        if (e->regs[e->aliases[k]] == ALIAS + h) {
            return true;
        }
    }
    return false;
}

static NOINLINE u32
late_arg(Emit *e, const u32 *args, u32 v, u8 info, u32 ints)
{
    bool fl = is_float(info);
    u32 same = fl ? 0 : ints;
    for (u32 j = 1; j <= args[0] && same < 8; j++) {
        if (args[j] == v) {
            add_alias(e, v, (fl ? 32 : 0) + same, info);
            return same;
        }
        u8 t = operand_info(e, args[j]);
        if (is_vector(t)) {
            same += fl;
        } else if (t & INFO_MEMORY) {
            u32 regs = check_struct_regs(e->c, e->ir->types[e->fn->first + args[j]]);
            same += (bool)(regs & REGS_FLOAT) == fl ? regs & REGS_COUNT : 0;
        } else {
            same += is_float(t) == fl;
        }
    }
    return alloc(e, v, info);
}

static u32
result(Emit *e, u32 v, u8 info)
{
    if (v + 1 < e->fn->count) {
        u32 at = e->fn->first + v + 1;
        u8 kind = e->ir->kinds[at];
        if (kind == IR_STORE && e->ir->bs[at] == v) {
            u8 h = e->slot_regs[e->ir->as[at]];
            if (h != NO_REG && !aliased(e, h)) {
                add_alias(e, v, h, info);
                return h & 31;
            }
        } else if (kind == IR_RET && e->ir->as[at] == v && e->ir->types[at] != TYPE_ID_VOID) {
            add_alias(e, v, is_float(info) ? 32 : 0, info);
            return 0;
        } else if (kind == IR_CALL || kind == IR_CALL_C) {
            const u32 *args = e->ir->extra + e->fn->first_extra + e->ir->bs[at];
            u32 ints = kind == IR_CALL ? FIRST_INT_ARG : 0;
            bool fl = is_float(info);
            u32 same = fl ? 0 : ints;
            for (u32 j = 1; j <= args[0] && same < 8; j++) {
                if (args[j] == v) {
                    add_alias(e, v, (fl ? 32 : 0) + same, info);
                    return same;
                }
                u8 t = operand_info(e, args[j]);
                if (t & INFO_MEMORY) {
                    return late_arg(e, args, v, info, ints);
                }
                same += is_float(t) == fl;
            }
        }
    }
    return alloc(e, v, info);
}

static NOINLINE void
data_address(Emit *e, u32 i, u32 type, u32 offset)
{
    u32 d = result(e, i, e->info[type]);
    u64 to = (u64)(uintptr_t)(e->data + offset);

    adrp(e, d, to);
    if (to & 0xFFF) {
        put(e, SF | OP_ADD_IMM | (u32)(to & 0xFFF) << 10 | d << 5 | d);
    }
}

static NOINLINE void
global_address(Emit *e, u32 i, u32 type, u32 offset)
{
    u32 d = result(e, i, e->info[type]);
    mov_imm(e, d, (u64)(uintptr_t)(e->globals + offset), SF);
}

static u32
fetch(Emit *e, u32 x, u32 scratch)
{
    if (x & IR_CONST) {
        u8 t = operand_info(e, x);
        if (is_float(t)) {
            float_imm(e, scratch, constant_bits(e, x), bits_of(t) == 64);
        } else {
            mov_imm(e, scratch, constant_bits(e, x), width(t));
        }
        return scratch;
    }
    u8 h = e->regs[x];
    if (h != NO_REG) {
        return h & 31;
    }
    bool fl = is_float(e->vinfo[x]);
    memory(e, fl ? OP_LDRD : OP_LDRX, 3, scratch, e->spills[x], fl ? X8 : scratch);
    return scratch;
}

static u32
take_over(Emit *e, u32 from, u32 v, u8 info)
{
    u8 h = e->regs[from];
    e->regs[from] = NO_REG;
    e->regs[v] = h;
    e->vinfo[v] = info;
    if (h >= ALIAS) {
        u32 k = e->alias_at[from];
        e->aliases[k] = v;
        e->alias_at[v] = k;
        return (h - ALIAS) & 31u;
    }
    e->holders[h] = v;
    return h & 31u;
}

static void
release(Emit *e, u32 x)
{
    if (x & IR_CONST) {
        return;
    }
    u8 h = e->regs[x];
    if (h == NO_REG) {
        e->spill_free = grow(
            e->spill_free,
            &e->spill_free_cap,
            e->nspill_free + 1,
            sizeof *e->spill_free
        );
        e->spill_free[e->nspill_free++] = e->spills[x];
        return;
    }
    if (h >= ALIAS) {
        drop_alias(e, e->alias_at[x]);
    } else {
        free_holder(e, h);
    }
    e->regs[x] = NO_REG;
}

static ALWAYS_INLINE void
save_for_call(Emit *e, u32 used, bool fl)
{
    u32 base = fl ? 32 : 0;
    u32 *callee = fl ? &e->free_callee_float : &e->free_callee_int;
    for (; used; used &= used - 1) {
        u32 reg = (u32)__builtin_ctz(used);
        u32 v = e->holders[base + reg];
        if (v == NO_VALUE) {
            continue;
        }
        if (*callee == 0) {
            spill_reg(e, base + reg);
            continue;
        }
        u32 to = (u32)__builtin_ctz(*callee);
        *callee &= *callee - 1;
        *(fl ? &e->saved_float : &e->saved_int) |= 1u << to;
        if (fl) {
            fmov(e, to, reg);
        } else {
            mov(e, SF, to, reg);
        }
        free_holder(e, base + reg);
        e->holders[base + to] = v;
        e->regs[v] = (u8)(base + to);
    }
}

static void
materialize(Emit *e, u32 k)
{
    u32 v = e->aliases[k];
    u32 h = e->regs[v] - ALIAS;
    drop_alias(e, k);
    u32 r = alloc(e, v, e->vinfo[v]);
    if (h >= 32) {
        fmov(e, r, h & 31);
    } else {
        mov(e, SF, r, h);
    }
}

static bool
imm12(u64 v, u32 *field)
{
    if (v < 4096) {
        *field = (u32)v << 10;
        return true;
    }
    if ((v & 0xFFF) == 0 && v >> 12 < 4096) {
        *field = SHIFT_IMM12 | (u32)(v >> 12) << 10;
        return true;
    }
    return false;
}

static bool
arith_imm(const Emit *e, u32 x, u8 t, bool negate, u32 *field, bool *flip)
{
    *flip = false;
    if (!(x & IR_CONST) || is_float(t)) {
        return false;
    }
    u64 v = constant_bits(e, x);
    int64_t sv = (int64_t)v;
    if (!width(t)) {
        v &= 0xFFFFFFFFu;
        sv = (int32_t)(u32)v;
    }
    if (imm12(v, field)) {
        return true;
    }
    *flip = negate && sv < 0 && imm12(0 - (u64)sv, field);
    return *flip;
}

static void
swap(u32 *a, u32 *b)
{
    u32 t = *a;
    *a = *b;
    *b = t;
}

static u32
extend_op(u8 t)
{
    bool s = is_signed(t);
    return bits_of(t) == 8 ? (s ? OP_SXTB : OP_UXTB) : (s ? OP_SXTH : OP_UXTH);
}

static void
normalize(Emit *e, u8 t, u32 d, u32 n)
{
    if (is_small(t)) {
        put(e, extend_op(t) | n << 5 | d);
    } else {
        mov(e, width(t), d, n);
    }
}

static u32
extension(u32 bits, bool s)
{
    return (bits == 8 ? 0u : bits == 16 ? 1u : 2u) | (s ? 4u : 0u);
}

static void
take_from_c(Emit *e, u8 info, u32 d, u32 r)
{
    if (bits_of(info) < 32) {
        put(e, extend_op(info) | r << 5 | d);
    } else if (bits_of(info) == 32) {
        put(e, OP_ORR | r << 16 | ZR << 5 | d);
    } else {
        mov(e, SF, d, r);
    }
}

static void
check_small(Emit *e, u8 t, u32 d)
{
    put(e, OP_SUBS_EXT | d << 16 | extension(bits_of(t), is_signed(t)) << 13 | d << 5 | ZR);
    trap_if(e, COND_NE, TRAP_OVERFLOW);
}

static void
add_sub(Emit *e, u32 i, u8 kind, u32 type, u32 a, u32 b)
{
    u8 t = e->info[type];
    bool sub = kind == IR_SUB || kind == IR_SUB_WRAP;
    if (is_float(t)) {
        u32 ra = fetch(e, a, V30), rb = fetch(e, b, V31);
        release(e, a);
        release(e, b);
        u32 d = result(e, i, t);
        put(e, (sub ? OP_FSUB : OP_FADD) | precision(t) | rb << 16 | ra << 5 | d);
        return;
    }

    bool checked = kind == IR_ADD || kind == IR_SUB;
    bool small = is_small(t);
    if (!sub && (a & IR_CONST) && !(b & IR_CONST)) {
        swap(&a, &b);
    }
    u32 field;
    bool flip;
    bool imm = arith_imm(e, b, t, is_signed(t) || !checked, &field, &flip);
    u32 sf = width(t);
    u32 ra = fetch(e, a, X16);
    u32 rb = imm ? 0 : fetch(e, b, X17);
    release(e, a);
    release(e, b);
    u32 d = result(e, i, t);
    bool flags = checked && !small;
    bool negative = sub != flip;
    u32 op = imm ? (negative ? (flags ? OP_SUBS_IMM : OP_SUB_IMM)
                             : (flags ? OP_ADDS_IMM : OP_ADD_IMM))
                 : (sub ? (flags ? OP_SUBS : OP_SUB) : (flags ? OP_ADDS : OP_ADD));
    u32 operand = imm ? field : rb << 16;
    put(e, op | sf | operand | ra << 5 | d);
    if (flags) {
        trap_if(e, is_signed(t) ? COND_VS : (sub ? COND_LO : COND_HS), TRAP_OVERFLOW);
    } else if (small) {
        if (checked) {
            check_small(e, t, d);
        } else {
            normalize(e, t, d, d);
        }
    }
}

static void
multiply(Emit *e, u32 i, u8 kind, u32 type, u32 a, u32 b)
{
    u8 t = e->info[type];
    if (is_float(t)) {
        u32 ra = fetch(e, a, V30), rb = fetch(e, b, V31);
        release(e, a);
        release(e, b);
        u32 d = result(e, i, t);
        put(e, OP_FMUL | precision(t) | rb << 16 | ra << 5 | d);
        return;
    }

    u32 ra = fetch(e, a, X16), rb = fetch(e, b, X17);
    release(e, a);
    release(e, b);
    u32 d = result(e, i, t);
    u32 sf = width(t);
    bool s = is_signed(t);
    if (kind == IR_MUL_HIGH) {
        put(e, OP_UMULH | rb << 16 | ra << 5 | d);
    } else if (kind == IR_MUL_WRAP || is_small(t)) {
        put(e, OP_MUL | sf | rb << 16 | ra << 5 | d);
        if (kind == IR_MUL_WRAP) {
            normalize(e, t, d, d);
        } else {
            check_small(e, t, d);
        }
    } else if (!sf) {
        put(e, (s ? OP_SMULL : OP_UMULL) | rb << 16 | ra << 5 | X8);
        put(e, 0xEB20001Fu | (s ? 0xC000u : 0x4000u) | X8 << 16 | X8 << 5);
        trap_if(e, COND_NE, TRAP_OVERFLOW);
        mov(e, 0, d, X8);
    } else {
        put(e, (s ? OP_SMULH : OP_UMULH) | rb << 16 | ra << 5 | X8);
        put(e, OP_MUL | SF | rb << 16 | ra << 5 | d);
        if (s) {
            put(e, 0xEB80FC1Fu | d << 16 | X8 << 5);
            trap_if(e, COND_NE, TRAP_OVERFLOW);
        } else {
            trap_unless_zero(e, X8, SF, TRAP_OVERFLOW);
        }
    }
}

static bool
shifted_mask(u64 v)
{
    u64 filled = (v - 1) | v;
    return v && ((filled + 1) & filled) == 0;
}

static bool
logical_imm(u64 value, u32 sf, u32 *field)
{
    u32 size = sf ? 64 : 32;
    if (!sf) {
        value &= 0xFFFFFFFFu;
    }
    u64 all = sf ? ~0ull : 0xFFFFFFFFull;
    if (value == 0 || value == all) {
        return false;
    }
    while (size > 2) {
        u32 half = size / 2;
        u64 mask = (1ull << half) - 1;
        if ((value & mask) != ((value >> half) & mask)) {
            break;
        }
        size = half;
    }
    u64 mask = ~0ull >> (64 - size);
    u64 v = value & mask;
    u32 zeros, ones;
    if (shifted_mask(v)) {
        zeros = (u32)__builtin_ctzll(v);
        ones = (u32)__builtin_ctzll(~(v >> zeros));
    } else {
        v |= ~mask;
        if (!shifted_mask(~v)) {
            return false;
        }
        u32 leading = (u32)__builtin_clzll(~v);
        zeros = 64 - leading;
        ones = leading + (u32)__builtin_ctzll(~v) - (64 - size);
    }
    u32 immr = (size - zeros) & (size - 1);
    u64 nimms = (~(u64)(size - 1) << 1) | (ones - 1);
    u32 n = (u32)((nimms >> 6) & 1) ^ 1;
    *field = n << 22 | immr << 16 | (u32)(nimms & 0x3F) << 10;
    return true;
}

static void
signed_magic(int64_t d, u32 w, int64_t *magic, u32 *shift)
{
    u64 two = 1ull << (w - 1);
    u64 ad = d < 0 ? 0 - (u64)d : (u64)d;
    u64 t = two + (d < 0);
    u64 anc = t - 1 - t % ad;
    u32 p = w - 1;
    u64 q1 = two / anc, r1 = two - q1 * anc;
    u64 q2 = two / ad, r2 = two - q2 * ad;
    u64 delta;
    do {
        p++;
        q1 *= 2;
        r1 *= 2;
        if (r1 >= anc) {
            q1++;
            r1 -= anc;
        }
        q2 *= 2;
        r2 *= 2;
        if (r2 >= ad) {
            q2++;
            r2 -= ad;
        }
        delta = ad - r2;
    } while (q1 < delta || (q1 == delta && r1 == 0));
    u64 m = q2 + 1;
    if (d < 0) {
        m = 0 - m;
    }
    if (w == 32) {
        m = (u64)(int64_t)(int32_t)(u32)m;
    }
    *magic = (int64_t)m;
    *shift = p - w;
}

static void
unsigned_magic(u64 d, u64 *magic, bool *add, u32 *shift)
{
    const u64 high = 0x8000000000000000ull;
    *add = false;
    u64 nc = ~0ull - (0 - d) % d;
    u32 p = 63;
    u64 q1 = high / nc, r1 = high - q1 * nc;
    u64 q2 = (high - 1) / d, r2 = (high - 1) - q2 * d;
    u64 delta;
    do {
        p++;
        if (r1 >= nc - r1) {
            q1 = 2 * q1 + 1;
            r1 = 2 * r1 - nc;
        } else {
            q1 = 2 * q1;
            r1 = 2 * r1;
        }
        if (r2 + 1 >= d - r2) {
            *add |= q2 >= high - 1;
            q2 = 2 * q2 + 1;
            r2 = 2 * r2 + 1 - d;
        } else {
            *add |= q2 >= high;
            q2 = 2 * q2;
            r2 = 2 * r2 + 1;
        }
        delta = d - 1 - r2;
    } while (p < 128 && (q1 < delta || (q1 == delta && r1 == 0)));
    *magic = q2 + 1;
    *shift = p - 64;
}

static void
shift_imm(Emit *e, u32 op, u32 sf, u32 d, u32 n, u32 count)
{
    put(e, op | sf | (sf ? 0x00408000u : 0) | count << 16 | n << 5 | d);
}

static void
lsl_imm(Emit *e, u32 d, u32 n, u32 count)
{
    put(e, OP_LSL_IMM | ((64 - count) & 63) << 16 | (63 - count) << 10 | n << 5 | d);
}

static bool
tests_zero(const Emit *e, u32 i)
{
    if (i + 1 >= e->fn->count) {
        return false;
    }
    u32 at = e->fn->first + i + 1;
    u8 kind = e->ir->kinds[at];
    u32 b = e->ir->bs[at];
    return (kind == IR_EQ || kind == IR_NE) && e->ir->as[at] == i &&
           (b & IR_CONST) && constant_bits(e, b) == 0;
}

static void
divide_constant(Emit *e, u32 i, u8 kind, u8 t, u32 a, u64 divisor)
{
    u32 sf = width(t);
    u32 w = sf ? 64 : 32;
    bool s = is_signed(t);
    bool rem = kind == IR_REM;
    bool pow2 = (!s || (int64_t)divisor > 0) && !(divisor & (divisor - 1));
    u32 k = (u32)__builtin_ctzll(divisor);
    u32 field = 0;
    if (rem && pow2 && k > 0 && tests_zero(e, i)) {
        u32 ra = fetch(e, a, X16);
        release(e, a);
        [[maybe_unused]] bool mask = logical_imm(divisor - 1, sf, &field);
        assert(mask);
        put(e, OP_ANDS_IMM | sf | field | ra << 5 | ZR);
        e->tested = i + 1;
        return;
    }

    u32 ra = fetch(e, a, X16);
    release(e, a);
    u32 d = result(e, i, t);
    if (pow2 && k == 0) {
        mov(e, sf, d, rem ? ZR : ra);
        return;
    }
    if (pow2 && !s) {
        if (rem) {
            [[maybe_unused]] bool mask = logical_imm(divisor - 1, sf, &field);
            assert(mask);
            put(e, OP_AND_IMM | sf | field | ra << 5 | d);
        } else {
            shift_imm(e, OP_LSR_IMM, sf, d, ra, k);
        }
        return;
    }
    if (pow2) {
        [[maybe_unused]] bool mask = logical_imm(divisor - 1, sf, &field);
        assert(mask);
        if (rem) {
            put(e, OP_SUBS | sf | ra << 16 | ZR << 5 | X8);
            put(e, OP_AND_IMM | sf | field | ra << 5 | X17);
            put(e, OP_AND_IMM | sf | field | X8 << 5 | X8);
            put(e, OP_CSNEG | sf | X8 << 16 | (u32)COND_MI << 12 | X17 << 5 | d);
            return;
        }
        if (k == 1) {
            put(e, OP_ADD | sf | SHIFT_LSR | ra << 16 | (w - 1) << 10 | ra << 5 | X8);
        } else {
            shift_imm(e, OP_ASR_IMM, sf, X8, ra, w - 1);
            put(e, OP_ADD | sf | SHIFT_LSR | X8 << 16 | (w - k) << 10 | ra << 5 | X8);
        }
        shift_imm(e, OP_ASR_IMM, sf, d, X8, k);
        return;
    }

    if (s) {
        int64_t magic;
        u32 shift;
        signed_magic((int64_t)divisor, w, &magic, &shift);
        mov_imm(e, X17, (u64)magic, sf);
        if (sf) {
            put(e, OP_SMULH | X17 << 16 | ra << 5 | X8);
        } else {
            put(e, OP_SMULL | X17 << 16 | ra << 5 | X8);
            shift_imm(e, OP_ASR_IMM, SF, X8, X8, 32);
        }
        if ((int64_t)divisor > 0 && magic < 0) {
            put(e, OP_ADD | sf | ra << 16 | X8 << 5 | X8);
        } else if ((int64_t)divisor < 0 && magic > 0) {
            put(e, OP_SUB | sf | ra << 16 | X8 << 5 | X8);
        }
        if (shift) {
            shift_imm(e, OP_ASR_IMM, sf, X8, X8, shift);
        }
        u32 q = rem ? X8 : d;
        put(e, OP_ADD | sf | SHIFT_LSR | X8 << 16 | (w - 1) << 10 | X8 << 5 | q);
    } else {
        u64 magic;
        bool add;
        u32 shift;
        unsigned_magic(divisor, &magic, &add, &shift);
        mov_imm(e, X17, magic, SF);
        put(e, OP_UMULH | X17 << 16 | ra << 5 | X8);
        u32 q = rem ? X8 : d;
        if (add) {
            put(e, OP_SUB | SF | X8 << 16 | ra << 5 | X17);
            put(e, OP_ADD | SF | SHIFT_LSR | X17 << 16 | 1u << 10 | X8 << 5 | X8);
            shift_imm(e, OP_LSR_IMM, SF, q, X8, shift - 1);
        } else if (shift) {
            shift_imm(e, OP_LSR_IMM, SF, q, X8, shift);
        } else {
            mov(e, SF, q, X8);
        }
    }
    if (rem) {
        mov_imm(e, X17, divisor, sf);
        put(e, OP_MSUB | sf | X17 << 16 | ra << 10 | X8 << 5 | d);
    }
}

static void
divide(Emit *e, u32 i, u8 kind, u32 type, u32 a, u32 b)
{
    u8 t = e->info[type];
    if (is_float(t)) {
        u32 ra = fetch(e, a, V30), rb = fetch(e, b, V31);
        release(e, a);
        release(e, b);
        u32 d = result(e, i, t);
        put(e, OP_FDIV | precision(t) | rb << 16 | ra << 5 | d);
        return;
    }
    if (b & IR_CONST) {
        u64 divisor = constant_bits(e, b);
        if (!width(t)) {
            divisor = is_signed(t) ? (u64)(int64_t)(int32_t)(u32)divisor
                                   : divisor & 0xFFFFFFFFu;
        }
        if (divisor != 0 && (!is_signed(t) || divisor != ~0ull)) {
            divide_constant(e, i, kind, t, a, divisor);
            return;
        }
    }

    u32 sf = width(t);
    bool s = is_signed(t);
    bool known = (b & IR_CONST) != 0;
    u64 divisor = known ? constant_bits(e, b) : 0;
    u64 minus_one = sf ? ~0ull : 0xFFFFFFFFull;
    if (known && !sf) {
        divisor &= 0xFFFFFFFFu;
    }

    u32 ra = fetch(e, a, X16), rb = fetch(e, b, X17);
    release(e, a);
    release(e, b);
    u32 d = result(e, i, t);
    if (!known || divisor == 0) {
        trap_if_zero(e, rb, sf, TRAP_DIVIDE);
    }
    bool full = bits_of(t) >= 32;
    if (s && kind == IR_DIV && full && (!known || divisor == minus_one)) {
        put(e, OP_ADDS_IMM | sf | 1 << 10 | rb << 5 | ZR);
        u32 skip = here(e);
        put(e, OP_B_COND | COND_NE);
        mov_imm(e, X8, sf ? 0x8000000000000000ull : 0x80000000ull, sf);
        put(e, OP_SUBS | sf | X8 << 16 | ra << 5 | ZR);
        trap_if(e, COND_EQ, TRAP_OVERFLOW);
        e->code[skip] |= ((here(e) - skip) & 0x7FFFFu) << 5;
    }

    u32 op = s ? OP_SDIV : OP_UDIV;
    if (kind == IR_DIV) {
        put(e, op | sf | rb << 16 | ra << 5 | d);
        if (s && !full) {
            check_small(e, t, d);
        }
    } else {
        put(e, op | sf | rb << 16 | ra << 5 | X8);
        put(e, OP_MSUB | sf | rb << 16 | ra << 10 | X8 << 5 | d);
    }
}

static void
bitwise(Emit *e, u32 i, u8 kind, u32 type, u32 a, u32 b)
{
    u8 t = e->info[type];
    u32 sf = width(t);
    if ((a & IR_CONST) && !(b & IR_CONST)) {
        swap(&a, &b);
    }
    u32 field = 0;
    bool imm = (b & IR_CONST) && logical_imm(constant_bits(e, b), sf, &field);
    u32 ra = fetch(e, a, X16);
    u32 rb = imm ? 0 : fetch(e, b, X17);
    release(e, a);
    release(e, b);
    if (kind == IR_AND && tests_zero(e, i)) {
        put(e, (imm ? OP_ANDS_IMM | field : OP_ANDS | rb << 16) | sf | ra << 5 | ZR);
        e->tested = i + 1;
        return;
    }
    u32 d = result(e, i, t);
    if (imm) {
        u32 op = kind == IR_AND ? OP_AND_IMM
               : kind == IR_OR  ? OP_ORR_IMM
                                : OP_EOR_IMM;
        put(e, op | sf | field | ra << 5 | d);
    } else {
        u32 op = kind == IR_AND ? OP_AND : kind == IR_OR ? OP_ORR : OP_EOR;
        put(e, op | sf | rb << 16 | ra << 5 | d);
    }
}

static void
release_home(Emit *e, u32 home)
{
    e->homes = grow(e->homes, &e->homes_cap, e->nhomes + 1, sizeof *e->homes);
    e->homes[e->nhomes++] = home;
}

static void
pair_at(Emit *e, u32 op, u32 lo, u32 hi, u32 home)
{
    if (home < 512) {
        put(e, op | (home >> 3) << 15 | hi << 10 | SP << 5 | lo);
        return;
    }
    sp_address(e, X8, home);
    put(e, op | hi << 10 | X8 << 5 | lo);
}

static void
fetch_wide(Emit *e, u32 x, u32 lo, u32 hi)
{
    if (x & IR_CONST) {
        mov_imm(e, lo, constant_bits(e, x), SF);
        mov_imm(e, hi, constant_high(e, x), SF);
        return;
    }
    u32 home = e->spills[x];
    if (home == NO_VALUE) {
        put(e, OP_FMOV_TO_X | V29 << 5 | lo);
        put(e, OP_UMOV_D | 1u << 20 | V29 << 5 | hi);
        return;
    }
    pair_at(e, OP_LDP_X, lo, hi, home);
    release_home(e, home);
}

static NOINLINE void
check_wide_count(Emit *e, u32 b, u32 bits, u32 d)
{
    fetch_wide(e, b, d, X8);
    trap_unless_zero(e, X8, SF, TRAP_SHIFT);
    put(e, OP_SUBS_IMM | SF | bits << 10 | d << 5 | ZR);
    trap_if(e, COND_HS, TRAP_SHIFT);
}

static void
shift_by(Emit *e, u8 kind, u8 t, u32 d, u32 n, u32 count)
{
    u32 op = kind == IR_SHL ? OP_LSLV : is_signed(t) ? OP_ASRV : OP_LSRV;
    put(e, op | width(t) | count << 16 | n << 5 | d);
    if (kind == IR_SHL && is_small(t)) {
        normalize(e, t, d, d);
    }
}

static void
shift(Emit *e, u32 i, u8 kind, u32 type, u32 a, u32 b)
{
    u8 t = e->info[type];
    u32 sf = width(t);
    u32 bits = sf ? 64 : 32;
    bool s = is_signed(t);
    u32 ra = fetch(e, a, X16);
    if (b & IR_CONST) {
        u32 count = (u32)constant_bits(e, b);
        release(e, a);
        u32 d = result(e, i, t);
        u32 base = sf ? 0xD3400000u : 0x53000000u;
        if (kind == IR_SHL) {
            put(e,
                base | ((bits - count) & (bits - 1)) << 16 |
                    (bits - 1 - count) << 10 | ra << 5 | d);
        } else {
            u32 op = s ? (sf ? 0x93400000u : 0x13000000u) : base;
            put(e, op | count << 16 | (bits - 1) << 10 | ra << 5 | d);
        }
        if (kind == IR_SHL && is_small(t)) {
            normalize(e, t, d, d);
        }
        return;
    }

    u8 count_info = operand_info(e, b);
    if (UNLIKELY(count_info & INFO_UNSUPPORTED)) {
        check_wide_count(e, b, bits_of(t), X17);
        release(e, a);
        shift_by(e, kind, t, result(e, i, t), ra, X17);
        return;
    }
    u32 rb = fetch(e, b, X17);
    release(e, a);
    release(e, b);
    u32 d = result(e, i, t);
    put(e, OP_SUBS_IMM | width(count_info) | bits_of(t) << 10 | rb << 5 | ZR);
    trap_if(e, COND_HS, TRAP_SHIFT);
    shift_by(e, kind, t, d, ra, rb);
}

static u8
compare_cond(const Emit *e, u8 kind, u32 type)
{
    u8 t = e->info[type];
    if (kind == IR_EQ) {
        return COND_EQ;
    }
    if (kind == IR_NE) {
        return COND_NE;
    }
    if (is_float(t)) {
        static const u8 conds[] = { COND_MI, COND_LS, COND_GT, COND_GE };
        return conds[kind - IR_LT];
    }
    if (is_signed(t)) {
        static const u8 conds[] = { COND_LT, COND_LE, COND_GT, COND_GE };
        return conds[kind - IR_LT];
    }
    static const u8 conds[] = { COND_LO, COND_LS, COND_HI, COND_HS };
    return conds[kind - IR_LT];
}

static void
flag_result(Emit *e, u32 i, u8 cond)
{
    u32 next = e->fn->first + i + 1;
    if (i + 1 < e->fn->count && e->ir->kinds[next] == IR_IF && e->ir->as[next] == i) {
        e->fused = i;
        e->fused_cond = cond;
        return;
    }
    u32 d = result(e, i, e->info[TYPE_ID_BOOL]);
    put(e, OP_CSET | (u32)(cond ^ 1) << 12 | d);
}

static void
compare(Emit *e, u32 i, u8 kind, u32 type, u32 a, u32 b)
{
    u8 t = e->info[type];
    if (e->tested == i) {
        e->tested = NO_VALUE;
    } else if (is_float(t)) {
        u32 ra = fetch(e, a, V30), rb = fetch(e, b, V31);
        release(e, a);
        release(e, b);
        put(e, OP_FCMP | precision(t) | rb << 16 | ra << 5);
    } else {
        if ((a & IR_CONST) && !(b & IR_CONST)) {
            swap(&a, &b);
            static const u8 mirrored[] = { IR_GT, IR_GE, IR_LT, IR_LE };
            kind = kind >= IR_LT ? mirrored[kind - IR_LT] : kind;
        }
        u32 field;
        bool flip;
        bool imm = arith_imm(e, b, t, is_signed(t) || kind <= IR_NE, &field, &flip);
        u32 ra = fetch(e, a, X16);
        u32 rb = imm ? 0 : fetch(e, b, X17);
        release(e, a);
        release(e, b);
        u32 sf = width(t);
        if (imm) {
            put(e, (flip ? OP_ADDS_IMM : OP_SUBS_IMM) | sf | field | ra << 5 | ZR);
        } else {
            put(e, OP_SUBS | sf | rb << 16 | ra << 5 | ZR);
        }
    }

    flag_result(e, i, compare_cond(e, kind, type));
}

static void
unary(Emit *e, u32 i, u8 kind, u32 type, u32 a)
{
    u8 t = e->info[type];
    if (is_float(t)) {
        u32 ra = fetch(e, a, V30);
        release(e, a);
        u32 d = result(e, i, t);
        put(e, OP_FNEG | precision(t) | ra << 5 | d);
        return;
    }

    u32 ra = fetch(e, a, X16);
    release(e, a);
    u32 d = result(e, i, t);
    u32 sf = width(t);
    bool small = is_small(t);
    switch (kind) {
        case IR_NOT: put(e, OP_EOR_ONE | ra << 5 | d); break;
        case IR_BIT_NOT:
            put(e, OP_ORN | sf | ra << 16 | ZR << 5 | d);
            if (small && !is_signed(t)) {
                normalize(e, t, d, d);
            }
            break;
        case IR_NEG_WRAP:
            put(e, OP_SUB | sf | ra << 16 | ZR << 5 | d);
            if (small) {
                normalize(e, t, d, d);
            }
            break;
        case IR_NEG:
            if (!is_signed(t)) {
                trap_unless_zero(e, ra, sf, TRAP_OVERFLOW);
                mov(e, sf, d, ZR);
            } else if (small) {
                put(e, OP_SUB | ra << 16 | ZR << 5 | d);
                check_small(e, t, d);
            } else {
                put(e, OP_SUBS | sf | ra << 16 | ZR << 5 | d);
                trap_if(e, COND_VS, TRAP_OVERFLOW);
            }
            break;
        default: assert(false && "not a unary instruction");
    }
}

static NOINLINE void
count_wide(Emit *e, u32 i, u8 kind, u32 type, u32 a)
{
    fetch_wide(e, a, X16, X17);
    u32 d = result(e, i, e->info[type]);

    switch (kind) {
        case IR_TRAILING_ZEROS:
            put(e, OP_RBIT | SF | X16 << 5 | X8);
            put(e, OP_CLZ | SF | X8 << 5 | X8);
            put(e, OP_RBIT | SF | X17 << 5 | d);
            put(e, OP_CLZ | SF | d << 5 | d);
            put(e, OP_ADD_IMM | SF | 64u << 10 | d << 5 | d);
            put(e, OP_SUBS_IMM | SF | X16 << 5 | ZR);
            put(e, OP_CSEL | SF | d << 16 | (u32)COND_NE << 12 | X8 << 5 | d);
            break;
        case IR_LEADING_ZEROS:
            put(e, OP_CLZ | SF | X17 << 5 | X8);
            put(e, OP_CLZ | SF | X16 << 5 | d);
            put(e, OP_ADD_IMM | SF | 64u << 10 | d << 5 | d);
            put(e, OP_SUBS_IMM | SF | X17 << 5 | ZR);
            put(e, OP_CSEL | SF | d << 16 | (u32)COND_NE << 12 | X8 << 5 | d);
            break;
        case IR_COUNT_ONES:
            put(e, OP_FMOV_FROM_X | X16 << 5 | V31);
            put(e, OP_INS_D1 | X17 << 5 | V31);
            put(e, OP_CNT_8B | 1u << 30 | V31 << 5 | V31);
            put(e, OP_ADDV_8B | 1u << 30 | V31 << 5 | V31);
            put(e, OP_FMOV_TO_W | V31 << 5 | d);
            break;
        default: assert(false && "not a bit count");
    }
}

static NOINLINE void
count_bits(Emit *e, u32 i, u8 kind, u32 type, u32 a)
{
    u8 from = operand_info(e, a);
    if (UNLIKELY(from & INFO_UNSUPPORTED)) {
        count_wide(e, i, kind, type, a);
        return;
    }
    u32 bits = bits_of(from);
    u32 sf = width(from);
    u32 ra = fetch(e, a, X16);
    release(e, a);
    u32 d = result(e, i, e->info[type]);

    if (kind != IR_TRAILING_ZEROS && is_small(from) && is_signed(from)) {
        put(e, (bits == 8 ? OP_UXTB : OP_UXTH) | ra << 5 | d);
        ra = d;
    }
    switch (kind) {
        case IR_TRAILING_ZEROS:
            if (bits < 32) {
                put(e, OP_ORR_IMM | (32 - bits) << 16 | ra << 5 | d);
                ra = d;
            }
            put(e, OP_RBIT | sf | ra << 5 | d);
            put(e, OP_CLZ | sf | d << 5 | d);
            break;
        case IR_LEADING_ZEROS:
            put(e, OP_CLZ | sf | ra << 5 | d);
            if (bits < 32) {
                put(e, OP_SUB_IMM | (32 - bits) << 10 | d << 5 | d);
            }
            break;
        case IR_COUNT_ONES:
            put(e, (sf ? OP_FMOV_FROM_X : OP_FMOV_FROM_W) | ra << 5 | V31);
            put(e, OP_CNT_8B | V31 << 5 | V31);
            put(e, OP_ADDV_8B | V31 << 5 | V31);
            put(e, OP_FMOV_TO_W | V31 << 5 | d);
            break;
        default: assert(false && "not a bit count");
    }
}

static NOINLINE void
test_overflow(Emit *e, u32 i, u8 kind, u32 type, u32 a, u32 b)
{
    u8 t = e->info[type];
    u32 sf = width(t);
    bool s = is_signed(t);
    bool sub = kind == IR_SUB_OVERFLOWS;
    if (!sub && (a & IR_CONST) && !(b & IR_CONST)) {
        swap(&a, &b);
    }
    u32 field = 0;
    bool flip = false;
    bool imm = kind != IR_MUL_OVERFLOWS && arith_imm(e, b, t, s, &field, &flip);
    u32 ra = fetch(e, a, X16);
    u32 rb = imm ? 0 : fetch(e, b, X17);
    release(e, a);
    release(e, b);

    u32 operand = imm ? field : rb << 16;
    bool negative = sub != flip;
    u32 extend = extension(bits_of(t), s) << 13;
    u8 cond = COND_NE;
    if (kind == IR_MUL_OVERFLOWS && !sf) {
        put(e, (s ? OP_SMULL : OP_UMULL) | rb << 16 | ra << 5 | X16);
        put(e, OP_SUBS_EXT | SF | X16 << 16 | extend | X16 << 5 | ZR);
    } else if (kind == IR_MUL_OVERFLOWS && s) {
        put(e, OP_SMULH | rb << 16 | ra << 5 | X8);
        put(e, OP_MUL | SF | rb << 16 | ra << 5 | X16);
        put(e, OP_SUBS | SF | SHIFT_ASR | X16 << 16 | 63u << 10 | X8 << 5 | ZR);
    } else if (kind == IR_MUL_OVERFLOWS) {
        put(e, OP_UMULH | rb << 16 | ra << 5 | X16);
        put(e, OP_SUBS_IMM | SF | X16 << 5 | ZR);
    } else if (is_small(t)) {
        u32 op = imm ? (negative ? OP_SUB_IMM : OP_ADD_IMM) : (sub ? OP_SUB : OP_ADD);
        put(e, op | operand | ra << 5 | X16);
        put(e, OP_SUBS_EXT | X16 << 16 | extend | X16 << 5 | ZR);
    } else {
        u32 op = imm ? (negative ? OP_SUBS_IMM : OP_ADDS_IMM)
                     : (sub ? OP_SUBS : OP_ADDS);
        put(e, op | sf | operand | ra << 5 | ZR);
        cond = s ? COND_VS : sub ? COND_LO : COND_HS;
    }
    flag_result(e, i, cond);
}

static void
negate_if(Emit *e, u32 lo, u32 hi, u32 mask)
{
    put(e, OP_EOR | SF | mask << 16 | lo << 5 | lo);
    put(e, OP_EOR | SF | mask << 16 | hi << 5 | hi);
    put(e, OP_SUBS | SF | mask << 16 | lo << 5 | lo);
    put(e, OP_SBC | SF | mask << 16 | hi << 5 | hi);
}

static NOINLINE void
float_from_wide(Emit *e, u32 i, u8 tt, u8 from, u32 a)
{
    bool s = is_signed(from);
    bool dbl = bits_of(tt) == 64;
    u32 t0 = take_reg(e, false);
    u32 t1 = take_reg(e, false);
    u32 sign = s ? take_reg(e, false) : ZR;
    fetch_wide(e, a, X16, X17);
    u32 d = result(e, i, tt);

    if (s) {
        shift_imm(e, OP_ASR_IMM, SF, sign, X17, 63);
        negate_if(e, X16, X17, sign);
    }
    u32 big = here(e);
    put(e, OP_CBNZ | SF | X17);
    put(e, OP_UCVTF | SF | precision(tt) | X16 << 5 | d);
    u32 done = here(e);
    put(e, OP_B);
    patch_one(e, big, here(e));

    put(e, OP_CLZ | SF | X17 << 5 | X8);
    put(e, OP_LSLV | SF | X8 << 16 | X17 << 5 | X17);
    put(e, OP_ORN | SF | X8 << 16 | ZR << 5 | t0);
    shift_imm(e, OP_LSR_IMM, SF, t1, X16, 1);
    put(e, OP_LSRV | SF | t0 << 16 | t1 << 5 | t1);
    put(e, OP_ORR | SF | t1 << 16 | X17 << 5 | X17);
    put(e, OP_LSLV | SF | X8 << 16 | X16 << 5 | X16);
    put(e, OP_SUBS_IMM | SF | X16 << 5 | ZR);
    put(e, OP_CSET | (u32)(COND_NE ^ 1) << 12 | t1);
    put(e, OP_ORR | SF | t1 << 16 | X17 << 5 | X17);
    put(e, OP_UCVTF | SF | precision(tt) | X17 << 5 | d);

    mov_imm(e, t0, dbl ? 1087 : 191, 0);
    put(e, OP_SUB | X8 << 16 | t0 << 5 | t0);
    lsl_imm(e, t0, t0, dbl ? 52 : 23);
    put(e, (dbl ? OP_FMOV_FROM_X : OP_FMOV_FROM_W) | t0 << 5 | V31);
    put(e, OP_FMUL | precision(tt) | V31 << 16 | d << 5 | d);
    patch_one(e, done, here(e));

    if (s) {
        put(e, OP_SUBS_IMM | SF | sign << 5 | ZR);
        put(e, OP_FNEG | precision(tt) | d << 5 | V31);
        put(e, OP_FCSEL | precision(tt) | d << 16 | (u32)COND_NE << 12 | V31 << 5 | d);
        e->free_int |= 1u << sign;
    }
    e->free_int |= 1u << t0 | 1u << t1;
}

static void
convert(Emit *e, u32 i, u32 to, u32 a)
{
    u8 tt = e->info[to];
    u8 from = operand_info(e, a);
    if (UNLIKELY(from & INFO_UNSUPPORTED)) {
        float_from_wide(e, i, tt, from, a);
        return;
    }
    bool float_from = is_float(from);
    bool float_to = is_float(tt);
    if (!float_from && !float_to) {
        u32 ra = fetch(e, a, X16);
        release(e, a);
        u32 d = result(e, i, tt);
        if (width(tt) && !width(from)) {
            put(e,
                is_signed(from) ? OP_SXTW | ra << 5 | d
                                : OP_ORR | ra << 16 | ZR << 5 | d);
        } else {
            mov(e, width(tt), d, ra);
        }
    } else if (!float_from) {
        u32 ra = fetch(e, a, X16);
        release(e, a);
        u32 d = result(e, i, tt);
        u32 op = is_signed(from) ? OP_SCVTF : OP_UCVTF;
        put(e, op | width(from) | precision(tt) | ra << 5 | d);
    } else {
        assert(float_to);
        u32 ra = fetch(e, a, V30);
        release(e, a);
        u32 d = result(e, i, tt);
        if (bits_of(from) == bits_of(tt)) {
            fmov(e, d, ra);
        } else {
            put(e,
                (float_to && bits_of(tt) == 64 ? OP_FCVT_TO_D : OP_FCVT_TO_S) |
                    ra << 5 | d);
        }
    }
}

static u64
power_bits(u8 t, int32_t k)
{
    return bits_of(t) == 64 ? (u64)(1023 + k) << 52 : (u64)(127 + k) << 23;
}

static void
float_bound(Emit *e, u32 reg, u8 t, double value)
{
    if (bits_of(t) == 64) {
        u64 bits;
        memcpy(&bits, &value, sizeof bits);
        float_imm(e, reg, bits, true);
    } else {
        float f = (float)value;
        u32 bits;
        memcpy(&bits, &f, sizeof bits);
        float_imm(e, reg, bits, false);
    }
}

static ALWAYS_INLINE void
check_float_range(Emit *e, u32 ra, u8 from, u32 to_bits, bool to_signed)
{
    u32 fp = precision(from);
    bool dbl = bits_of(from) == 64;
    u32 mantissa = dbl ? 53 : 24;
    float_imm(e, V29, power_bits(from, (int32_t)(to_signed ? to_bits - 1 : to_bits)), dbl);
    put(e, OP_FCMP | fp | V29 << 16 | ra << 5);
    trap_if(e, COND_PL, TRAP_RANGE);
    if (!to_signed) {
        float_bound(e, V29, from, -1.0);
        put(e, OP_FCMP | fp | V29 << 16 | ra << 5);
        trap_if(e, COND_LE, TRAP_RANGE);
    } else if (to_bits - 1 < mantissa) {
        float_bound(e, V29, from, -(double)(1ull << (to_bits - 1)) - 1.0);
        put(e, OP_FCMP | fp | V29 << 16 | ra << 5);
        trap_if(e, COND_LE, TRAP_RANGE);
    } else {
        u64 sign = dbl ? 1ull << 63 : 1ull << 31;
        float_imm(e, V29, sign | power_bits(from, (int32_t)to_bits - 1), dbl);
        put(e, OP_FCMP | fp | V29 << 16 | ra << 5);
        trap_if(e, COND_LT, TRAP_RANGE);
    }
}

static void
compare_imm(Emit *e, u32 reg, u64 v, u32 sf, bool may_negate)
{
    if (!sf) {
        v &= 0xFFFFFFFFu;
    }
    u32 field;
    if (imm12(v, &field)) {
        put(e, OP_SUBS_IMM | sf | field | reg << 5 | ZR);
        return;
    }
    u64 negated = sf ? 0 - v : (0 - v) & 0xFFFFFFFFu;
    if (may_negate && imm12(negated, &field)) {
        put(e, OP_ADDS_IMM | sf | field | reg << 5 | ZR);
        return;
    }
    u32 temp = reg == X17 ? X8 : X17;
    mov_imm(e, temp, v, sf);
    put(e, OP_SUBS | sf | temp << 16 | reg << 5 | ZR);
}

static void
subtract_imm(Emit *e, u32 d, u32 n, u64 v, u32 sf)
{
    if (!sf) {
        v &= 0xFFFFFFFFu;
    }
    u32 field;
    if (v == 0) {
        put(e, OP_ORR | sf | n << 16 | ZR << 5 | d);
    } else if (imm12(v, &field)) {
        put(e, OP_SUB_IMM | sf | field | n << 5 | d);
    } else if (imm12(sf ? 0 - v : (0 - v) & 0xFFFFFFFFu, &field)) {
        put(e, OP_ADD_IMM | sf | field | n << 5 | d);
    } else {
        mov_imm(e, X8, v, sf);
        put(e, OP_SUB | sf | X8 << 16 | n << 5 | d);
    }
}

static void
branch_to(Emit *e, u32 op, u32 target, u8 trap)
{
    if (target == NO_VALUE) {
        add_fixup(e, &e->traps[trap]);
    } else {
        e->branches = grow(
            e->branches,
            &e->branches_cap,
            e->nbranches + 1,
            sizeof *e->branches
        );
        e->branches[e->nbranches++] = (Branch){ here(e), target };
    }
    put(e, op);
}

static void
test_range(Emit *e, u32 r, u8 t, Range range, u8 trap)
{
    u32 sf = width(t);
    if (range.lo == range.hi) {
        compare_imm(e, r, range.lo, sf, true);
        branch_to(e, OP_B_COND | COND_EQ, range.target, trap);
        return;
    }
    subtract_imm(e, X17, r, range.lo, sf);
    compare_imm(e, X17, range.hi - range.lo, sf, false);
    branch_to(e, OP_B_COND | COND_LS, range.target, trap);
}

static void
search(Emit *e, u32 r, u8 t, const Range *ranges, u32 n, u32 fallback, u8 trap)
{
    if (n <= 3) {
        for (u32 k = 0; k < n; k++) {
            test_range(e, r, t, ranges[k], trap);
        }
        branch_to(e, OP_B, fallback, trap);
        return;
    }
    u32 mid = n / 2;
    compare_imm(e, r, ranges[mid].lo, width(t), is_signed(t));
    u32 left = 0;
    add_fixup(e, &left);
    put(e, OP_B_COND | (is_signed(t) ? COND_LT : COND_LO));
    search(e, r, t, ranges + mid, n - mid, fallback, trap);
    patch(e, left, here(e));
    search(e, r, t, ranges, mid, fallback, trap);
}

static void
check_member(Emit *e, u32 d, u8 t, const u32 *runs)
{
    u32 n = runs[0] / 4;
    e->ranges = grow(e->ranges, &e->ranges_cap, n + 1, sizeof *e->ranges);
    for (u32 k = 0; k < n; k++) {
        const u32 *w = runs + 1 + 4 * k;
        e->ranges[k] = (Range){ w[0] | (u64)w[1] << 32, w[2] | (u64)w[3] << 32, 0 };
    }
    u32 first = e->nbranches;
    search(e, d, t, e->ranges, n, NO_VALUE, TRAP_RANGE);
    for (u32 k = first; k < e->nbranches; k++) {
        patch_one(e, e->branches[k].at, here(e));
    }
    e->nbranches = first;
}

static NOINLINE u8
narrow_wide(Emit *e, u32 a, u8 from, bool to_signed)
{
    fetch_wide(e, a, X16, X17);
    if (is_signed(from) && to_signed) {
        put(e, OP_SUBS | SF | SHIFT_ASR | X16 << 16 | 63u << 10 | X17 << 5 | ZR);
        trap_if(e, COND_NE, TRAP_RANGE);
        return e->info[TYPE_ID_I64];
    }
    trap_unless_zero(e, X17, SF, TRAP_RANGE);
    return e->info[TYPE_ID_U64];
}

static void
narrow(Emit *e, u32 i, u32 to, u32 a)
{
    u8 tt = e->info[to];
    u8 from = operand_info(e, a);
    u32 to_bits = bits_of(tt);
    bool to_signed = is_signed(tt);
    if (is_float(from)) {
        u32 ra = fetch(e, a, V30);
        release(e, a);
        check_float_range(e, ra, from, to_bits, to_signed);
        u32 d = result(e, i, tt);
        put(e,
            (to_signed ? OP_FCVTZS : OP_FCVTZU) | width(tt) | precision(from) |
                ra << 5 | d);
        return;
    }

    u32 ra = X16;
    if (UNLIKELY(from & INFO_UNSUPPORTED)) {
        from = narrow_wide(e, a, from, to_signed);
    } else {
        ra = fetch(e, a, X16);
        release(e, a);
    }
    u32 sf = width(from);
    u32 from_bits = bits_of(from);
    if (is_signed(from)) {
        if (to_signed && to_bits >= from_bits) {
        } else if (to_bits >= from_bits) {
            put(e, OP_SUBS_IMM | sf | ra << 5 | ZR);
            trap_if(e, COND_LT, TRAP_RANGE);
        } else {
            put(e,
                OP_SUBS_EXT | sf | ra << 16 |
                    extension(to_bits, to_signed) << 13 | ra << 5 | ZR);
            trap_if(e, COND_NE, TRAP_RANGE);
        }
    } else if (to_bits - to_signed < from_bits) {
        u32 field = 0;
        [[maybe_unused]] bool
            mask = logical_imm(~((1ull << (to_bits - to_signed)) - 1), sf, &field);
        assert(mask);
        put(e, OP_ANDS_IMM | sf | field | ra << 5 | ZR);
        trap_if(e, COND_NE, TRAP_RANGE);
    }
    u32 d = result(e, i, tt);
    if (width(tt) && !sf && is_signed(from) && to_signed) {
        put(e, OP_SXTW | ra << 5 | d);
    } else if (width(tt) && !sf) {
        put(e, OP_ORR | ra << 16 | ZR << 5 | d);
    } else {
        mov(e, width(tt), d, ra);
    }
    if (e->c->types.kinds[to] == TYPE_ENUM) {
        check_member(
            e,
            d,
            tt,
            e->ir->extra + e->fn->first_extra + e->ir->bs[e->fn->first + i]
        );
    }
}

static void
truncate_to(Emit *e, u32 i, u32 to, u32 a)
{
    u8 tt = e->info[to];
    u32 ra = X16;
    if (UNLIKELY(operand_info(e, a) & INFO_UNSUPPORTED)) {
        fetch_wide(e, a, X16, X8);
    } else {
        ra = fetch(e, a, X16);
        release(e, a);
    }
    u32 d = result(e, i, tt);
    if (is_small(tt)) {
        put(e, extend_op(tt) | ra << 5 | d);
    } else {
        put(e, OP_ORR | width(tt) | ra << 16 | ZR << 5 | d);
    }
}

static bool
consumed_next(const Emit *e, u32 v)
{
    if (v + 1 >= e->fn->count) {
        return false;
    }
    u8 kind = e->ir->kinds[e->fn->first + v + 1];
    return kind == IR_STORE || kind == IR_RET || kind == IR_CALL || kind == IR_CALL_C;
}

static bool
can_take_over(const Emit *e, u32 i, u32 a)
{
    return !(a & IR_CONST) && e->regs[a] != NO_REG && !consumed_next(e, i);
}

static NOINLINE void
bitcast_to(Emit *e, u32 i, u32 to, u32 a)
{
    u8 tt = e->info[to];
    u8 from = operand_info(e, a);
    if (is_float(from) == is_float(tt)) {
        if (bits_of(tt) == 64 && can_take_over(e, i, a)) {
            take_over(e, a, i, tt);
        } else {
            truncate_to(e, i, to, a);
        }
        return;
    }

    bool wide = bits_of(tt) == 64;
    u32 ra = fetch(e, a, is_float(from) ? V30 : X16);
    release(e, a);
    u32 d = result(e, i, tt);
    u32 op = is_float(tt) ? (wide ? OP_FMOV_FROM_X : OP_FMOV_FROM_W)
                          : (wide ? OP_FMOV_TO_X : OP_FMOV_TO_W);
    put(e, op | ra << 5 | d);
}

static NOINLINE void
check_align(Emit *e, u32 i, u32 type, u32 a, u32 align)
{
    u32 field = 0;
    [[maybe_unused]] bool mask = logical_imm(align - 1, SF, &field);
    assert(mask);

    u32 ra = fetch(e, a, X16);
    put(e, OP_ANDS_IMM | SF | field | ra << 5 | ZR);
    trap_if(e, COND_NE, TRAP_ALIGN);
    if (can_take_over(e, i, a)) {
        take_over(e, a, i, e->info[type]);
        return;
    }
    release(e, a);
    mov(e, SF, result(e, i, e->info[type]), ra);
}

static NOINLINE void
check_nil(Emit *e, u32 i, u32 type, u32 a)
{
    u32 ra = fetch(e, a, X16);
    trap_if_zero(e, ra, SF, TRAP_NIL);
    if (can_take_over(e, i, a)) {
        take_over(e, a, i, e->info[type]);
        return;
    }
    release(e, a);
    mov(e, SF, result(e, i, e->info[type]), ra);
}

static NOINLINE void
check_assert(Emit *e, u32 a)
{
    u32 ra = fetch(e, a, X16);
    trap_if_zero(e, ra, 0, TRAP_ASSERT);
    release(e, a);
}

static void
add_offset(Emit *e, u32 d, u32 n, u64 offset)
{
    u32 field;
    if (offset == 0) {
        mov(e, SF, d, n);
    } else if (imm12(offset, &field)) {
        put(e, OP_ADD_IMM | SF | field | n << 5 | d);
    } else {
        mov_imm(e, X17, offset, SF);
        put(e, OP_ADD | SF | X17 << 16 | n << 5 | d);
    }
}

static u32
fetch_index(Emit *e, u32 x, u32 scratch)
{
    u8 t = operand_info(e, x);
    u32 r = fetch(e, x, scratch);
    if (bits_of(t) < 64) {
        put(e,
            is_signed(t) ? OP_SXTW | r << 5 | scratch
                         : OP_ORR | r << 16 | ZR << 5 | scratch);
        r = scratch;
    }
    return r;
}

static u32
offset_address(Emit *e, u32 i, u8 info, u32 a, u64 offset)
{
    if (offset == 0 && can_take_over(e, i, a)) {
        return take_over(e, a, i, info);
    }
    u32 p = fetch(e, a, X16);
    release(e, a);
    u32 d = result(e, i, info);
    add_offset(e, d, p, offset);
    return d;
}

static void
scaled_address(Emit *e, u32 d, u32 p, u32 r, u64 size)
{
    if (size == 0) {
        mov(e, SF, d, p);
    } else if ((size & (size - 1)) == 0) {
        u32 shift = (u32)__builtin_ctzll(size);
        put(e, OP_ADD | SF | r << 16 | shift << 10 | p << 5 | d);
    } else {
        mov_imm(e, X8, size, SF);
        put(e, OP_MADD | SF | X8 << 16 | p << 10 | r << 5 | d);
    }
}

static void
index_address(Emit *e, u32 i, u32 type, u32 a, u32 b)
{
    const Types *t = &e->c->types;
    u64 size = check_type_size(e->c, t->arg0[type]);
    if (b & IR_CONST) {
        offset_address(e, i, e->info[type], a, constant_bits(e, b) * size);
        return;
    }

    u32 base = e->ir->types[e->fn->first + a];
    u32 p = fetch(e, a, X16);
    u32 r = fetch_index(e, b, X17);
    if (t->kinds[base] == TYPE_POINTER) {
        compare_imm(e, r, t->arg1[t->arg0[base]], SF, false);
        trap_if(e, COND_HS, TRAP_BOUNDS);
    }
    release(e, a);
    release(e, b);

    u32 d = result(e, i, e->info[type]);
    scaled_address(e, d, p, r, size);
}

static void
index_len(Emit *e, u32 i, u32 type, u32 a, u32 b)
{
    const u32 *x = e->ir->extra + e->fn->first_extra + b;
    u32 len = x[0];
    u32 at = x[1];
    u64 size = check_type_size(e->c, e->c->types.arg0[type]);
    if (at & IR_CONST) {
        u64 k = constant_bits(e, at);
        u32 n = fetch(e, len, X17);
        compare_imm(e, n, k, SF, false);
        trap_if(e, COND_LS, TRAP_BOUNDS);
        release(e, len);
        offset_address(e, i, e->info[type], a, k * size);
        return;
    }

    u32 r = fetch_index(e, at, X17);
    if (len & IR_CONST) {
        compare_imm(e, r, constant_bits(e, len), SF, false);
    } else {
        u32 n = fetch(e, len, X16);
        put(e, OP_SUBS | SF | n << 16 | r << 5 | ZR);
        release(e, len);
    }
    trap_if(e, COND_HS, TRAP_BOUNDS);

    u32 p = fetch(e, a, X16);
    release(e, a);
    release(e, at);
    u32 d = result(e, i, e->info[type]);
    scaled_address(e, d, p, r, size);
}

static u8
half_target(const Emit *e, u32 i, u32 half)
{
    u32 v = i + half;
    u32 at = i + 2;
    if (at >= e->fn->count) {
        return NO_REG;
    }
    u32 g = e->fn->first + at;
    switch (e->ir->kinds[g]) {
        case IR_STORE: {
            u32 s = g + half;
            if (at + half >= e->fn->count || e->ir->kinds[s] != IR_STORE ||
                e->ir->bs[s] != v) {
                return NO_REG;
            }
            u8 h = e->slot_regs[e->ir->as[s]];
            return h != NO_REG && !aliased(e, h) ? h : NO_REG;
        }
        case IR_RET:
            return e->ir->as[g] == i && e->ir->bs[g] == i + 1 && !aliased(e, half)
                     ? (u8)half
                     : NO_REG;
        case IR_CALL: {
            const u32 *args = e->ir->extra + e->fn->first_extra + e->ir->bs[g];
            u32 same = FIRST_INT_ARG;
            for (u32 j = 1; j <= args[0] && same < 8; j++) {
                if (args[j] == v) {
                    return (u8)same;
                }
                u8 t = operand_info(e, args[j]);
                assert(args[j] == i || !(t & INFO_MEMORY) || is_vector(t));
                same += args[j] == i || !(is_float(t) || is_vector(t));
            }
            return NO_REG;
        }
        default: return NO_REG;
    }
}

static u32
half_result(Emit *e, u32 i, u32 half, u8 info)
{
    u8 h = half_target(e, i, half);
    if (h == NO_REG) {
        return alloc(e, i + half, info);
    }
    add_alias(e, i + half, h, info);
    return h & 31u;
}

static void
move_half(Emit *e, u32 i, u32 half, u8 info, u32 x)
{
    u8 h = half_target(e, i, half);
    if (h == NO_REG && !(x & IR_CONST) && e->regs[x] != NO_REG) {
        take_over(e, x, i + half, info);
        return;
    }
    u32 r = fetch(e, x, X16);
    release(e, x);
    u32 d = half_result(e, i, half, info);
    mov(e, SF, d, r);
}

static void
check_order(Emit *e, u32 x, u32 y)
{
    if ((x & IR_CONST) && ((y & IR_CONST) || constant_bits(e, x) == 0)) {
        return;
    }
    if (x & IR_CONST) {
        u32 r = fetch_index(e, y, X16);
        compare_imm(e, r, constant_bits(e, x), SF, false);
        trap_if(e, COND_LO, TRAP_BOUNDS);
        return;
    }
    u32 r = fetch_index(e, x, X16);
    if (y & IR_CONST) {
        compare_imm(e, r, constant_bits(e, y), SF, false);
    } else {
        u32 q = fetch_index(e, y, X17);
        put(e, OP_SUBS | SF | q << 16 | r << 5 | ZR);
    }
    trap_if(e, COND_HI, TRAP_BOUNDS);
}

static void
slice(Emit *e, u32 i, u8 kind, u32 type, u32 a, u32 b)
{
    const u32 *x = e->ir->extra + e->fn->first_extra + b;
    u32 len = x[0];
    u32 lo = x[1];
    u32 hi = kind == IR_SLICE ? x[2] : len;
    u64 size = check_type_size(e->c, e->c->types.arg0[type]);
    u8 t = e->info[type];
    u8 n = e->info[TYPE_ID_I64];
    if (kind == IR_SLICE) {
        check_order(e, hi, len);
        release(e, len);
    }
    check_order(e, lo, hi);

    if (lo & IR_CONST) {
        u64 k = constant_bits(e, lo);
        if (hi & IR_CONST) {
            u32 d = half_result(e, i, 1, n);
            mov_imm(e, d, constant_bits(e, hi) - k, SF);
        } else if (k == 0) {
            move_half(e, i, 1, n, hi);
        } else {
            u32 r = fetch_index(e, hi, X17);
            release(e, hi);
            u32 d = half_result(e, i, 1, n);
            subtract_imm(e, d, r, k, SF);
        }
        if (k == 0) {
            move_half(e, i, 0, t, a);
        } else {
            u32 p = fetch(e, a, X16);
            release(e, a);
            u32 d = half_result(e, i, 0, t);
            add_offset(e, d, p, k * size);
        }
        return;
    }

    u32 d0 = half_result(e, i, 0, t);
    u32 d1 = half_result(e, i, 1, n);
    u32 r = fetch_index(e, lo, X17);
    u32 q = (hi & IR_CONST) ? X16 : fetch_index(e, hi, X16);
    if (hi & IR_CONST) {
        mov_imm(e, X16, constant_bits(e, hi), SF);
    }
    put(e, OP_SUB | SF | r << 16 | q << 5 | d1);
    u32 p = fetch(e, a, X16);
    scaled_address(e, d0, p, r, size);
    release(e, a);
    release(e, lo);
    release(e, hi);
}

static void
store_zeros(Emit *e, u32 p, u64 size)
{
    u32 k = 0;
    for (; size >= 16; size -= 16, k += 16) {
        put(e, OP_STP_X | (k / 8) << 15 | ZR << 10 | p << 5 | ZR);
    }
    if (size & 8) {
        put(e, OP_STRX | (k / 8) << 10 | p << 5 | ZR);
        k += 8;
    }
    if (size & 4) {
        put(e, OP_STRW | (k / 4) << 10 | p << 5 | ZR);
        k += 4;
    }
    if (size & 2) {
        put(e, OP_STRH | (k / 2) << 10 | p << 5 | ZR);
        k += 2;
    }
    if (size & 1) {
        put(e, OP_STRB | k << 10 | p << 5 | ZR);
    }
}

static void
zero_memory(Emit *e, u32 type, u32 a)
{
    u64 size = check_type_size(e->c, type);
    u32 p = fetch(e, a, X16);
    release(e, a);
    if (size > UNROLLED_BYTES) {
        mov(e, SF, X16, p);
        p = X16;
        mov_imm(e, X17, size / 32, SF);
        put(e, OP_STP_X | 2 << 15 | ZR << 10 | X16 << 5 | ZR);
        put(e, OP_STP_X_POST | 4 << 15 | ZR << 10 | X16 << 5 | ZR);
        put(e, OP_SUBS_IMM | SF | 1 << 10 | X17 << 5 | X17);
        put(e, OP_B_COND | ((u32)-3 & 0x7FFFFu) << 5 | COND_NE);
        size %= 32;
    }
    store_zeros(e, p, size);
}

static void
copy_bytes(Emit *e, u32 d, u32 s, u64 size)
{
    u32 k = 0;
    for (; size >= 32; size -= 32, k += 32) {
        put(e, OP_LDP_Q | (k / 16) << 15 | V31 << 10 | s << 5 | V30);
        put(e, OP_STP_Q | (k / 16) << 15 | V31 << 10 | d << 5 | V30);
    }
    if (size & 16) {
        put(e, OP_LDR_Q | (k / 16) << 10 | s << 5 | V30);
        put(e, OP_STR_Q | (k / 16) << 10 | d << 5 | V30);
        k += 16;
    }
    if (size & 8) {
        put(e, OP_LDRX | (k / 8) << 10 | s << 5 | X8);
        put(e, OP_STRX | (k / 8) << 10 | d << 5 | X8);
        k += 8;
    }
    if (size & 4) {
        put(e, OP_LDRW | (k / 4) << 10 | s << 5 | X8);
        put(e, OP_STRW | (k / 4) << 10 | d << 5 | X8);
        k += 4;
    }
    if (size & 2) {
        put(e, OP_LDRH | (k / 2) << 10 | s << 5 | X8);
        put(e, OP_STRH | (k / 2) << 10 | d << 5 | X8);
        k += 2;
    }
    if (size & 1) {
        put(e, OP_LDRB | k << 10 | s << 5 | X8);
        put(e, OP_STRB | k << 10 | d << 5 | X8);
    }
}

static void
copy_between(Emit *e, u32 d, u32 s, u64 size)
{
    assert(s != X16);
    if (size > UNROLLED_BYTES) {
        mov(e, SF, X16, d);
        mov(e, SF, X17, s);
        d = X16;
        s = X17;
        mov_imm(e, X8, size / 32, SF);
        put(e, OP_LDP_Q_POST | 2 << 15 | V31 << 10 | X17 << 5 | V30);
        put(e, OP_STP_Q_POST | 2 << 15 | V31 << 10 | X16 << 5 | V30);
        put(e, OP_SUBS_IMM | SF | 1 << 10 | X8 << 5 | X8);
        put(e, OP_B_COND | ((u32)-3 & 0x7FFFFu) << 5 | COND_NE);
        size %= 32;
    }
    copy_bytes(e, d, s, size);
}

static void
copy_memory(Emit *e, u32 type, u32 a, u32 b)
{
    u64 size = check_type_size(e->c, type);
    u32 d = fetch(e, a, X16);
    u32 s = fetch(e, b, X17);
    release(e, a);
    release(e, b);
    copy_between(e, d, s, size);
}

static void
move_into(Emit *e, u32 d, u32 x, bool fl)
{
    u8 t = operand_info(e, x);
    if (x & IR_CONST) {
        if (fl) {
            float_imm(e, d, constant_bits(e, x), bits_of(t) == 64);
        } else {
            mov_imm(e, d, constant_bits(e, x), width(t));
        }
        return;
    }
    if (e->regs[x] != NO_REG) {
        if (fl) {
            fmov(e, d, e->regs[x] & 31u);
        } else {
            mov(e, SF, d, e->regs[x] & 31u);
        }
    } else {
        memory(e, fl ? OP_LDRD : OP_LDRX, 3, d, e->spills[x], fl ? X8 : d);
    }
    release(e, x);
}

static void
piece(Emit *e, u32 op, u32 scale, u32 rt, u32 base, u32 offset)
{
    if (base == SP) {
        memory(e, op, scale, rt, offset, X16);
        return;
    }
    put(e, op | (offset >> scale) << 10 | base << 5 | rt);
}

static bool
pairs_at(u32 offset, u32 scale)
{
    return (offset & ((1u << scale) - 1)) == 0 && (offset >> scale) < 64;
}

static void
load_chunk(Emit *e, u32 d, u32 base, u32 offset, u32 n)
{
    static const u32 ops[4] = { OP_LDRB, OP_LDRH, OP_LDRW, OP_LDRX };
    u32 s = n >= 8 ? 3 : n >= 4 ? 2 : n >= 2 ? 1 : 0;
    piece(e, ops[s], s, d, base, offset);
    u32 done = 1u << s;
    while (done < n) {
        s = n - done >= 2 ? 1 : 0;
        piece(e, ops[s], s, X17, base, offset + done);
        put(e, OP_ORR | SF | X17 << 16 | (done * 8) << 10 | d << 5 | d);
        done += 1u << s;
    }
}

static void
store_chunk(Emit *e, u32 r, u32 base, u32 offset, u32 n)
{
    static const u32 ops[4] = { OP_STRB, OP_STRH, OP_STRW, OP_STRX };
    u32 s = n >= 8 ? 3 : n >= 4 ? 2 : n >= 2 ? 1 : 0;
    piece(e, ops[s], s, r, base, offset);
    u32 done = 1u << s;
    while (done < n) {
        s = n - done >= 2 ? 1 : 0;
        shift_imm(e, OP_LSR_IMM, SF, X17, r, done * 8);
        piece(e, ops[s], s, X17, base, offset + done);
        done += 1u << s;
    }
}

static void
move_struct(Emit *e, bool load, u32 type, u32 first, u32 base, u32 offset)
{
    u32 regs = check_struct_regs(e->c, type);
    u32 n = regs & REGS_COUNT;
    if (regs & REGS_FLOAT) {
        bool d = regs & REGS_DOUBLE;
        u32 scale = d ? 3 : 2;
        u32 one = load ? (d ? OP_LDRD : OP_LDRS) : (d ? OP_STRD : OP_STRS);
        u32 two = load ? (d ? OP_LDP_D : OP_LDP_S) : (d ? OP_STP_D : OP_STP_S);
        for (u32 k = 0; k < n; k += 2) {
            u32 at = offset + (k << scale);
            if (k + 1 < n && pairs_at(at, scale)) {
                put(e,
                    two | (at >> scale) << 15 | (first + k + 1) << 10 |
                        base << 5 | (first + k));
            } else {
                piece(e, one, scale, first + k, base, at);
                if (k + 1 < n) {
                    piece(e, one, scale, first + k + 1, base, at + (1u << scale));
                }
            }
        }
        return;
    }

    u32 size = (u32)check_type_size(e->c, type);
    if (size == 16 && pairs_at(offset, 3)) {
        put(e,
            (load ? OP_LDP_X : OP_STP_X) | (offset >> 3) << 15 |
                (first + 1) << 10 | base << 5 | first);
        return;
    }
    for (u32 k = 0; k < n; k++) {
        u32 bytes = size - 8 * k < 8 ? size - 8 * k : 8;
        if (load) {
            load_chunk(e, first + k, base, offset + 8 * k, bytes);
        } else {
            store_chunk(e, first + k, base, offset + 8 * k, bytes);
        }
    }
}

static void
pass_pointer(Emit *e, u32 v, u32 type, u32 d)
{
    u32 s = fetch(e, v, X17);
    release(e, v);

    u32 root = e->ir->bs[e->fn->first + v];
    if (root == DATA_ROOT ||
        (root < DATA_ROOT &&
         e->ir->slots[e->fn->first_slot + root].weight != WEIGHT_ADDRESSED)) {
        mov(e, SF, d, s);
        return;
    }

    u64 size = check_type_size(e->c, type);
    sp_address(e, d, e->copies_at + e->copied);
    copy_between(e, d, s, size);
    e->copied += (u32)((size + 7) & ~(u64)7);
    assert(e->copied <= e->fn->copies);
}

static u32
place_struct(const Emit *e, ArgCursor *at, u32 regs)
{
    u32 n = regs & REGS_COUNT;
    if (regs & REGS_FLOAT) {
        u32 m = regs & REGS_DOUBLE ? 8 : 4;
        return check_place_arg(at, regs, n * m, m, e->place);
    }
    return check_place_arg(at, regs, 8 * n, 8, e->place);
}

static NOINLINE void
stack_struct(Emit *e, u32 v, u32 type, u32 regs, u32 offset)
{
    u32 s = fetch(e, v, X17);
    release(e, v);
    if (!(regs & REGS_POINTER)) {
        sp_address(e, X16, offset);
        copy_bytes(e, X16, s, check_type_size(e->c, type));
        return;
    }

    u32 root = e->ir->bs[e->fn->first + v];
    if (root == DATA_ROOT ||
        (root < DATA_ROOT &&
         e->ir->slots[e->fn->first_slot + root].weight != WEIGHT_ADDRESSED)) {
        memory(e, OP_STRX, 3, s, offset, X16);
        return;
    }

    u64 size = check_type_size(e->c, type);
    u32 copy = e->copies_at + e->copied;
    sp_address(e, X16, copy);
    copy_between(e, X16, s, size);
    e->copied += (u32)((size + 7) & ~(u64)7);
    assert(e->copied <= e->fn->copies);
    sp_address(e, X17, copy);
    memory(e, OP_STRX, 3, X17, offset, X16);
}

static void
lane_constant(Emit *e, u32 d, u8 t, u64 bits)
{
    u32 scale = t & INFO_SCALE;
    u64 ones = scale == 3 ? ~0ull : (1ull << (8u << scale)) - 1;
    u64 lane = bits & ones;
    if (lane == 0 || lane == ones) {
        u32 imm = lane ? 0xFF : 0;
        put(e, OP_MOVI_64 | (imm >> 5) << 16 | (imm & 31) << 5 | d);
        return;
    }
    if (lane < 256 && scale < 3) {
        static const u32 movi[] = { OP_MOVI_8, OP_MOVI_16, OP_MOVI_32 };
        put(e, movi[scale] | (u32)(lane >> 5) << 16 | (u32)(lane & 31) << 5 | d);
        return;
    }
    mov_imm(e, X16, lane, scale == 3 ? SF : 0);
    put(e, OP_DUP | (1u << scale) << 16 | X16 << 5 | d);
}

static void
wide_constant(Emit *e, u32 d, u64 lo, u64 hi)
{
    if ((lo == 0 || lo == ~0ull) && hi == lo) {
        u32 imm = lo ? 0xFF : 0;
        put(e, OP_MOVI_64 | (imm >> 5) << 16 | (imm & 31) << 5 | d);
        return;
    }
    mov_imm(e, X16, lo, SF);
    put(e, OP_FMOV_FROM_X | X16 << 5 | d);
    if (hi) {
        mov_imm(e, X16, hi, SF);
        put(e, OP_INS_D1 | X16 << 5 | d);
    }
}

static u32
fetch_lanes(Emit *e, u32 x, u32 scratch)
{
    if (x & IR_CONST) {
        u8 t = operand_info(e, x);
        if (is_wide(t)) {
            wide_constant(e, scratch, constant_bits(e, x), constant_high(e, x));
        } else {
            lane_constant(e, scratch, t, constant_bits(e, x));
        }
        return scratch;
    }
    u32 home = e->spills[x];
    if (home == NO_VALUE) {
        return V29;
    }
    memory(e, OP_LDR_Q, 4, scratch, home, X8);
    release_home(e, home);
    return scratch;
}

static void
move_lanes(Emit *e, u32 d, u32 x)
{
    u32 r = fetch_lanes(e, x, d);
    if (r != d) {
        put(e, OP_VORR | r << 16 | r << 5 | d);
    }
}

static NOINLINE void
pass_vector(Emit *e, u32 v, ArgCursor *at)
{
    if (at->floats < 8) {
        move_lanes(e, at->floats++, v);
        return;
    }

    u32 place = check_place_arg(at, 1 | REGS_FLOAT, 16, 16, e->place);
    assert(place & PLACE_STACK);
    u32 r = fetch_lanes(e, v, V30);
    memory(e, OP_STR_Q, 4, r, place & ~PLACE_STACK, X16);
}

static NOINLINE void
pass_struct(Emit *e, u32 v, ArgCursor *at)
{
    u32 type = e->ir->types[e->fn->first + v];
    u32 regs = check_struct_regs(e->c, type);
    u32 *next = regs & REGS_FLOAT ? &at->floats : &at->ints;
    if (*next + (regs & REGS_COUNT) > 8) {
        u32 place = place_struct(e, at, regs);
        assert(place & PLACE_STACK);
        stack_struct(e, v, type, regs, place & ~PLACE_STACK);
        return;
    }

    u32 first = *next;
    *next += regs & REGS_COUNT;
    if (regs & REGS_POINTER) {
        pass_pointer(e, v, type, first);
        return;
    }
    u32 p = fetch(e, v, X16);
    move_struct(e, true, type, first, p, 0);
    release(e, v);
}

static NOINLINE void
stack_arg(Emit *e, u32 x, u8 info, ArgCursor *at, u32 how)
{
    bool fl = is_float(info);
    u32 scale = info & INFO_SCALE;
    u32 regs = ((info & INFO_PAIR) ? 2 : 1) | (fl ? REGS_FLOAT : 0);
    u32 place = check_place_arg(at, regs, 1u << scale, 1u << scale, how);
    assert(place & PLACE_STACK);

    u32 r = fetch(e, x, fl ? V30 : X17);
    release(e, x);
    if (!fl && how != PLACE_APPLE) {
        scale = 3;
        info = (u8)(info & ~INFO_SCALE) | 3;
    }
    memory(e, store_op(info, r), scale, r, place & ~PLACE_STACK, X16);
}

static NOINLINE void
pass_args(Emit *e, const u32 *args, u32 ints, u32 named)
{
    ArgCursor at = { ints, 0, 0 };
    e->copied = 0;
    for (u32 j = 1; j <= named; j++) {
        u8 info = operand_info(e, args[j]);
        if (info & INFO_MEMORY) {
            if (is_vector(info)) {
                pass_vector(e, args[j], &at);
            } else {
                pass_struct(e, args[j], &at);
            }
            continue;
        }
        bool fl = is_float(info);
        u32 *next = fl ? &at.floats : &at.ints;
        if (*next + ((info & INFO_PAIR) != 0) < 8) {
            move_into(e, (*next)++, args[j], fl);
            continue;
        }
        stack_arg(e, args[j], info, &at, e->place);
    }
    for (u32 j = named + 1; j <= args[0]; j++) {
        stack_arg(e, args[j], operand_info(e, args[j]), &at, PLACE_APPLE | PLACE_VARIADIC);
    }
    assert(at.stack <= e->fn->stack_args);
}

static NOINLINE u32
by_pointer(const Emit *e, u32 type)
{
    return (check_struct_regs(e->c, type) & REGS_POINTER) != 0;
}

static NOINLINE void
point_result(Emit *e, u32 type, const u32 *args)
{
    if (by_pointer(e, type)) {
        sp_address(e, X8, e->slot_offsets[args[args[0] + 1]]);
    }
}

static NOINLINE void
receive_result(Emit *e, u32 type, const u32 *args)
{
    if (is_vector(e->info[type])) {
        put(e, OP_VORR | 0 << 16 | 0 << 5 | V29);
        return;
    }
    if (!by_pointer(e, type)) {
        move_struct(e, false, type, 0, SP, e->slot_offsets[args[args[0] + 1]]);
    }
}

static bool
inside_loop(const Emit *e)
{
    for (u32 k = 0; k < e->nopen; k++) {
        if (e->open[k].kind == IR_LOOP) {
            return true;
        }
    }
    return false;
}

static NOINLINE bool
bind_c(Emit *e, const Native *n, u32 decl)
{
    Name name = names_get(e->c->names, e->c->decls.names[decl]);
    bool global = e->c->decls.kinds[decl] == DECL_VAR;
    char symbol[256];
    if (name.len >= sizeof symbol) {
        e->error = global
                     ? "the name of an `extern` global is longer than 255 bytes"
                     : "the name of an `extern fn` is longer than 255 bytes";
        return false;
    }
    memcpy(symbol, name.text, name.len);
    symbol[name.len] = 0;

    if (!e->process) {
        e->process = dlopen(NULL, RTLD_LAZY);
    }
    void *address = dlsym(e->process, symbol);
    if (!address) {
        e->error = global
                     ? "an `extern` global names a C variable that isn't loaded"
                     : "an `extern fn` names a C function that is not loaded";
        return false;
    }
    n->entries[decl] = (u64)(uintptr_t)address;
    return true;
}

static NOINLINE void
callback_address(Emit *e, u32 i, u32 type, u32 k, u32 decl, const Native *n)
{
    if (e->c->decls.bodies[decl] != NO_BODY) {
        u32 d = result(e, i, e->info[type]);
        u64 to = e->adapters[k];
        adrp(e, d, to);
        if (to & 0xFFF) {
            put(e, SF | OP_ADD_IMM | (u32)(to & 0xFFF) << 10 | d << 5 | d);
        }
        return;
    }
    if (!n->entries[decl] && !bind_c(e, n, decl)) {
        return;
    }

    u32 d = result(e, i, e->info[type]);
    u64 slot = (u64)(uintptr_t)(n->entries + decl);
    adrp(e, d, slot);
    put(e, OP_LDRX | (u32)((slot & 0xFFF) >> 3) << 10 | d << 5 | d);
}

static NOINLINE void
c_global_address(Emit *e, u32 i, u32 type, u32 decl, const Native *n)
{
    if (!n->entries[decl] && !bind_c(e, n, decl)) {
        return;
    }

    u32 d = result(e, i, e->info[type]);
    u64 slot = (u64)(uintptr_t)(n->entries + decl);
    adrp(e, d, slot);
    put(e, OP_LDRX | (u32)((slot & 0xFFF) >> 3) << 10 | d << 5 | d);
}

static ALWAYS_INLINE void
call(Emit *e, u32 i, u32 type, u32 decl, u32 b, const Native *n, u32 ints)
{
    assert((ints == 0) == (e->c->decls.bodies[decl] == NO_BODY));
    u8 t = e->info[type];
    const u32 *args = e->ir->extra + e->fn->first_extra + b;
    if (!ints && !n->entries[decl] && !bind_c(e, n, decl)) {
        return;
    }
    u32 named = args[0];
    if (e->place && !ints) {
        named = e->c->lists[e->c->types.arg0[e->c->decls.types[decl]]];
    }
    pass_args(e, args, ints, named);
    save_for_call(e, ~e->free_int & INT_POOL, false);
    save_for_call(e, ~e->free_float & FLOAT_POOL, true);
    if (t & INFO_MEMORY) {
        point_result(e, type, args);
    }
    if (ints && (e->called || inside_loop(e))) {
        put(e, OP_LOAD_CONTEXT);
        e->context_saved = true;
    }
    e->called = true;

    u64 entry = (u64)(uintptr_t)(n->entries + decl);
    adrp(e, X16, entry);
    put(e, OP_LDR_X16_X16 | (u32)((entry & 0xFFF) >> 3) << 10);
    put(e, OP_BLR_X16);

    if (t & INFO_MEMORY) {
        receive_result(e, type, args);
    } else if (t & INFO_PAIR) {
        u32 d0 = half_result(e, i, 0, t);
        u32 d1 = half_result(e, i, 1, e->info[TYPE_ID_I64]);
        assert(d1 != 0);
        if (d0 == 1) {
            mov(e, SF, d1, 1);
            mov(e, SF, d0, 0);
        } else {
            mov(e, SF, d0, 0);
            mov(e, SF, d1, 1);
        }
    } else if (type != TYPE_ID_VOID) {
        bool fl = is_float(t);
        u32 d = result(e, i, e->info[type]);
        if (fl) {
            fmov(e, d, 0);
        } else if (ints == 0) {
            take_from_c(e, t, d, 0);
        } else {
            mov(e, SF, d, 0);
        }
    }
}

static bool
in_register(const Emit *e, u32 x, u32 r)
{
    if (x & IR_CONST) {
        return false;
    }
    u8 h = e->regs[x];
    return h != NO_REG && (h >= ALIAS ? h - ALIAS : h) == r;
}

static NOINLINE void
load_pair(Emit *e, u32 i, u32 p, u8 t)
{
    u32 d0 = half_result(e, i, 0, t);
    u32 d1 = half_result(e, i, 1, e->info[TYPE_ID_I64]);
    put(e, OP_LDP_X | d1 << 10 | p << 5 | d0);
}

static NOINLINE void
store_pair(Emit *e, u32 a, u32 b)
{
    const u32 *x = e->ir->extra + e->fn->first_extra + b;
    u32 p = fetch(e, a, X16);
    u32 r0 = (x[0] & IR_CONST) && constant_bits(e, x[0]) == 0 ? ZR
                                                              : fetch(e, x[0], X17);
    u32 r1 = (x[1] & IR_CONST) && constant_bits(e, x[1]) == 0 ? ZR
                                                              : fetch(e, x[1], X8);
    put(e, OP_STP_X | r1 << 10 | p << 5 | r0);
    release(e, a);
    release(e, x[0]);
    release(e, x[1]);
}

static void
return_pair(Emit *e, u32 a, u32 b)
{
    if (in_register(e, a, 1) && in_register(e, b, 0)) {
        mov(e, SF, X16, 0);
        mov(e, SF, 0, 1);
        mov(e, SF, 1, X16);
        release(e, a);
        release(e, b);
    } else if (in_register(e, b, 0)) {
        move_into(e, 1, b, false);
        move_into(e, 0, a, false);
    } else {
        move_into(e, 0, a, false);
        move_into(e, 1, b, false);
    }
}

static void
open_switch(Emit *e, u32 i, u32 type, u32 a, u32 b)
{
    u8 t = e->info[type];
    u32 sf = width(t);
    u64 mask = sf ? ~0ull : 0xFFFFFFFFull;
    const u32 *x = e->ir->extra + e->fn->first_extra + b;
    u32 n = x[0];
    u32 fallback = x[1];
    u32 r = fetch(e, a, X16);
    release(e, a);
    spill_all(e);

    Open o = { .target = i,
               .kind = IR_SWITCH,
               .result = NO_REG,
               .first_branch = e->nbranches,
               .first_arm = e->narms,
               .table = NO_VALUE,
               .stub = NO_VALUE };
    e->ranges = grow(e->ranges, &e->ranges_cap, n + 1, sizeof *e->ranges);
    u64 count = 0;
    for (u32 k = 0; k < n; k++) {
        const u32 *w = x + 2 + 5 * k;
        Range range = { w[0] | (u64)w[1] << 32, w[2] | (u64)w[3] << 32, w[4] };
        e->ranges[k] = range;
        count += ((range.hi - range.lo) & mask) + 1;
    }

    u64 span = n ? ((e->ranges[n - 1].hi - e->ranges[0].lo) & mask) + 1 : 0;
    if (n >= 4 && span != 0 && span <= 8ull * n && 10 * count >= 4 * span) {
        o.min = e->ranges[0].lo;
        o.span = (u32)span;
        subtract_imm(e, X17, r, o.min, sf);
        compare_imm(e, X17, span - 1, sf, false);
        branch_to(e, OP_B_COND | COND_HI, fallback, TRAP_UNREACHABLE);
        put(e, OP_ADR | 4u << 5 | X16);
        put(e, OP_LDRSW_SCALED | X17 << 16 | X16 << 5 | X17);
        put(e, OP_ADD | SF | X17 << 16 | X16 << 5 | X16);
        put(e, OP_BR | X16 << 5);
        o.table = here(e);
        for (u64 k = 0; k < span; k++) {
            put(e, 0);
        }
        if (count < span && fallback == NO_VALUE) {
            o.stub = here(e);
            branch_to(e, OP_B, NO_VALUE, TRAP_UNREACHABLE);
        }
    } else {
        search(e, r, t, e->ranges, n, fallback, TRAP_UNREACHABLE);
    }

    e->arm_at = grow(e->arm_at, &e->arms_cap, e->narms + 1, sizeof *e->arm_at);
    e->arm_at[e->narms++] = here(e);
    e->open = grow(e->open, &e->open_cap, e->nopen + 1, sizeof *e->open);
    e->open[e->nopen++] = o;
}

static void
start_arm(Emit *e, u32 i)
{
    Open *o = &e->open[e->nopen - 1];
    u8 before = e->ir->kinds[e->fn->first + i - 1];
    if (before != IR_RET && before != IR_EXIT && before != IR_REPEAT) {
        add_fixup(e, &o->ends);
        put(e, OP_B);
    }
    e->arm_at = grow(e->arm_at, &e->arms_cap, e->narms + 1, sizeof *e->arm_at);
    e->arm_at[e->narms++] = here(e);
}

static void
finish_switch(Emit *e, const Open *o)
{
    const u32 *arms = e->arm_at + o->first_arm;
    for (u32 k = o->first_branch; k < e->nbranches; k++) {
        patch_one(e, e->branches[k].at, arms[e->branches[k].target]);
    }
    if (o->table != NO_VALUE) {
        const u32 *x = e->ir->extra + e->fn->first_extra +
                       e->ir->bs[e->fn->first + o->target];
        u32 fallback = x[1] == NO_VALUE ? o->stub : arms[x[1]];
        for (u32 k = 0; k < o->span; k++) {
            e->code[o->table + k] = (fallback - o->table) * 4;
        }
        for (u32 k = 0; k < x[0]; k++) {
            const u32 *w = x + 2 + 5 * k;
            u32 lo = (u32)((w[0] | (u64)w[1] << 32) - o->min);
            u32 hi = (u32)((w[2] | (u64)w[3] << 32) - o->min);
            u32 offset = (arms[w[4]] - o->table) * 4;
            for (u32 v = lo; v <= hi; v++) {
                e->code[o->table + v] = offset;
            }
        }
    }
    e->nbranches = o->first_branch;
    e->narms = o->first_arm;
}

static void
align_loop(Emit *e)
{
    put(e, OP_NOP);
    while ((uintptr_t)e->at & 15) {
        put(e, OP_NOP);
    }
}

static Open *
find_open(Emit *e, u32 target)
{
    for (u32 k = e->nopen; k-- > 0;) {
        if (e->open[k].target == target) {
            return &e->open[k];
        }
    }
    assert(false && "branch to a construct that isn't open");
    return NULL;
}

static const char UNSUPPORTED_TYPE[] = "the arm64 backend supports only "
                                       "integers, floats, bool, pointers, "
                                       "arrays, slices and vectors so far";

static u32
arm_end(const Emit *e, u32 i)
{
    const u8 *kinds = e->ir->kinds + e->fn->first;
    u32 depth = 0;
    for (u32 k = i + 1;; k++) {
        u8 kind = kinds[k];
        if (kind == IR_BLOCK || kind == IR_LOOP || kind == IR_IF || kind == IR_SWITCH) {
            depth++;
        } else if (kind == IR_END) {
            if (depth == 0) {
                return k;
            }
            depth--;
        } else if (kind == IR_ELSE && depth == 0) {
            return k;
        }
    }
}

static bool
in_pool(u8 h)
{
    return h < 32 ? (INT_POOL >> h) & 1 : h < 64 && (FLOAT_POOL >> (h - 32)) & 1;
}

static void
hand_over(Emit *e, Open *o, u32 type, u32 v)
{
    bool fl = is_float(e->info[type]);
    if (o->result == NO_REG) {
        if (!(v & IR_CONST) && in_pool(e->regs[v])) {
            o->result = e->regs[v] & 31u;
        } else {
            o->result = (u8)take_reg(e, fl);
            *(fl ? &e->free_float : &e->free_int) |= 1u << o->result;
        }
    }
    move_into(e, o->result, v, fl);
}

static void
store_pinned(Emit *e, u8 h, u32 type, u32 b)
{
    for (u32 k = e->naliases; k-- > 0;) {
        u32 v = e->aliases[k];
        if (v != b && e->regs[v] == ALIAS + h) {
            materialize(e, k);
        }
    }
    bool fl = h >= 32;
    u32 r = h & 31u;
    u8 t = e->info[type];
    if (b & IR_CONST) {
        if (fl) {
            float_imm(e, r, constant_bits(e, b), bits_of(t) == 64);
        } else {
            mov_imm(e, r, constant_bits(e, b), width(t));
        }
    } else if (e->regs[b] == ALIAS + h) {
        release(e, b);
    } else {
        move_into(e, r, b, fl);
    }
}

static void
pin_by_weight(Emit *e, bool fl, u32 first, u32 cheap, u32 callee)
{
    const IrSlot *slots = e->slots;
    u32 top[32];
    u32 ntop = 0;
    u32 room = (u32)__builtin_popcount(cheap | callee);
    for (u32 s = first; s < e->fn->nslots; s++) {
        u8 info = e->info[slots[s].type];
        u32 w = slots[s].weight;
        if (!w || is_float(info) != fl || e->slot_regs[s] != NO_REG ||
            (info & (INFO_UNSUPPORTED | INFO_MEMORY)) || w == WEIGHT_ADDRESSED ||
            (ntop == room && w <= slots[top[room - 1]].weight)) {
            continue;
        }
        u32 at = ntop < room ? ntop++ : room - 1;
        while (at > 0 && slots[top[at - 1]].weight < w) {
            top[at] = top[at - 1];
            at--;
        }
        top[at] = s;
    }

    u32 base = fl ? 32 : 0;
    for (u32 k = 0; k < ntop; k++) {
        u32 s = top[k];
        if (cheap) {
            e->slot_regs[s] = (u8)(base + (u32)__builtin_ctz(cheap));
            cheap &= cheap - 1;
        } else if (slots[s].weight >= 2) {
            u32 r = (u32)__builtin_ctz(callee);
            callee &= callee - 1;
            e->slot_regs[s] = (u8)(base + r);
            *(fl ? &e->saved_float : &e->saved_int) |= 1u << r;
        }
    }
}

static NOINLINE void
skip_memory_param(const Emit *e, u32 type, u32 *used)
{
    if (is_vector(e->info[type])) {
        used[1]++;
        return;
    }
    u32 regs = check_struct_regs(e->c, type);
    used[(regs & REGS_FLOAT) != 0] += regs & REGS_COUNT;
}

static bool
late_by_pointer(const Emit *e, const IrSlot *slots, u32 p)
{
    return (e->info[slots[p].type] & INFO_MEMORY) && by_pointer(e, slots[p].type);
}

static NOINLINE const IrSlot *
retype_params(Emit *e, const IrSlot *slots)
{
    u32 n = e->fn->nslots;
    e->retyped = grow(e->retyped, &e->retyped_cap, n, sizeof *e->retyped);
    memcpy(e->retyped, slots, n * sizeof *slots);
    for (u32 m = e->by_ref; m; m &= m - 1) {
        e->retyped[__builtin_ctz(m)].type = TYPE_ID_I64;
    }
    for (u32 p = 32; p < e->fn->nparams; p++) {
        if (late_by_pointer(e, slots, p)) {
            e->retyped[p].type = TYPE_ID_I64;
        }
    }
    return e->retyped;
}

static void
pin_locals(Emit *e)
{
    const IrFunc *fn = e->fn;
    const IrSlot *slots = e->ir->slots + fn->first_slot;
    e->slot_regs = grow(
        e->slot_regs,
        &e->slot_regs_cap,
        fn->nslots + 1,
        sizeof *e->slot_regs
    );
    memset(e->slot_regs, NO_REG, fn->nslots);
    e->saved_int = 0;
    e->saved_float = 0;
    e->by_ref = 0;
    for (u32 p = 0; p < fn->nparams && p < 32; p++) {
        if (e->info[slots[p].type] & INFO_MEMORY) {
            e->by_ref |= by_pointer(e, slots[p].type) << p;
        }
    }
    bool late = false;
    for (u32 p = 32; p < fn->nparams; p++) {
        late |= late_by_pointer(e, slots, p);
    }
    if (e->by_ref || late) {
        slots = retype_params(e, slots);
    }
    e->slots = slots;

    u32 cheap[2] = { 0, 0 };
    u32 first = 0;
    if (fn->calls == 0) {
        cheap[0] = ARGUMENTS;
        cheap[1] = ARGUMENTS;
        u32 used[2] = { FIRST_INT_ARG, 0 };
        for (u32 p = 0; p < fn->nparams; p++) {
            u8 info = e->info[slots[p].type];
            if (info & INFO_MEMORY) {
                skip_memory_param(e, slots[p].type, used);
                continue;
            }
            bool fl = is_float(info);
            u32 r = used[fl]++;
            if (r < 8 && slots[p].weight) {
                e->slot_regs[p] = (u8)((fl ? 32 : 0) + r);
                cheap[fl] &= ~(1u << r);
            }
        }
        first = fn->nparams;
    }

    u32 wanted[2] = { 0, 0 };
    for (u32 s = first; s < fn->nslots; s++) {
        u8 info = e->info[slots[s].type];
        u32 w = slots[s].weight;
        wanted[is_float(info)] += w != 0 && w != WEIGHT_ADDRESSED &&
                                  !(info & INFO_MEMORY) && e->slot_regs[s] == NO_REG;
    }
    u32 callee[2] = { CALLEE_INT, CALLEE_FLOAT };
    for (u32 c = 0; c < 2; c++) {
        if (wanted[c] > (u32)__builtin_popcount(cheap[c] | callee[c])) {
            pin_by_weight(e, c, first, cheap[c], callee[c]);
            continue;
        }
        u32 base = c ? 32 : 0;
        for (u32 s = first; s < fn->nslots && wanted[c]; s++) {
            u8 info = e->info[slots[s].type];
            u32 w = slots[s].weight;
            if (is_float(info) != c || !w || e->slot_regs[s] != NO_REG ||
                (info & (INFO_UNSUPPORTED | INFO_MEMORY)) || w == WEIGHT_ADDRESSED) {
                continue;
            }
            wanted[c]--;
            if (cheap[c]) {
                e->slot_regs[s] = (u8)(base + (u32)__builtin_ctz(cheap[c]));
                cheap[c] &= cheap[c] - 1;
            } else if (w >= 2) {
                u32 r = (u32)__builtin_ctz(callee[c]);
                callee[c] &= callee[c] - 1;
                e->slot_regs[s] = (u8)(base + r);
                *(c ? &e->saved_float : &e->saved_int) |= 1u << r;
            }
        }
    }
    e->pinned_int = e->saved_int;
    e->pinned_float = e->saved_float;
    e->free_callee_int = CALLEE_INT & ~e->saved_int;
    e->free_callee_float = CALLEE_FLOAT & ~e->saved_float;
}

static void
lay_out_frame(Emit *e)
{
    const IrFunc *fn = e->fn;
    const IrSlot *slots = e->slots;
    e->slot_offsets = grow(
        e->slot_offsets,
        &e->slot_offsets_cap,
        fn->nslots,
        sizeof *e->slot_offsets
    );
    u32 counts[4] = { 0 };
    u32 memory = 0;
    for (u32 s = 0; s < fn->nslots; s++) {
        u8 info = e->info[slots[s].type];
        if ((info & INFO_UNSUPPORTED) && !is_vector(info)) {
            e->error = UNSUPPORTED_TYPE;
        }
        if (e->slot_regs[s] == NO_REG && slots[s].weight) {
            memory += (info & INFO_MEMORY) != 0;
            counts[info & INFO_SCALE] += (info & INFO_MEMORY) == 0;
        }
    }
    u32 bases[4];
    u32 offset = fn->stack_args;
    for (u32 k = 0; k < 4; k++) {
        offset = (offset + (1u << k) - 1) & ~((1u << k) - 1);
        bases[k] = offset;
        offset += counts[k] << k;
    }
    for (u32 s = 0; s < fn->nslots; s++) {
        u8 info = e->info[slots[s].type];
        if (e->slot_regs[s] != NO_REG || !slots[s].weight || (info & INFO_MEMORY)) {
            continue;
        }
        u32 k = info & INFO_SCALE;
        e->slot_offsets[s] = bases[k];
        bases[k] += 1u << k;
    }

    for (u32 align = 16; memory && align > 0; align /= 2) {
        for (u32 s = 0; s < fn->nslots; s++) {
            u32 type = slots[s].type;
            if (!(e->info[type] & INFO_MEMORY) || !slots[s].weight ||
                check_type_align(e->c, type) != align) {
                continue;
            }
            offset = (offset + align - 1) & ~(align - 1);
            u64 size = check_type_size(e->c, type);
            if (size >= FRAME_LIMIT - offset) {
                e->error = "a function's frame is larger than 16 MB";
                return;
            }
            e->slot_offsets[s] = offset;
            offset += (u32)size;
            memory--;
        }
    }

    offset = (offset + 7) & ~7u;
    if (fn->copies >= FRAME_LIMIT - offset) {
        e->error = "a function's frame is larger than 16 MB";
        return;
    }
    e->copies_at = offset;
    offset += fn->copies;

    u32 ret = (u32)e->c->types.arg1[e->c->decls.types[fn->decl]];
    e->result_at = NO_VALUE;
    if ((e->info[ret] & INFO_MEMORY) && by_pointer(e, ret)) {
        e->result_at = offset;
        offset += 8;
    }
    e->locals = offset;
}

static u32
callee_words(u32 *out, bool load, u32 saved_int, u32 saved_float)
{
    u32 n = 0;
    for (u32 r = 19; r < 29; r += 2) {
        if (saved_int & (3u << r)) {
            u32 imm = (u32) - (int32_t)(2 * (n + 1)) & 0x7F;
            out[n++] = (load ? OP_LDP_X : OP_STP_X) | imm << 15 |
                       (r + 1) << 10 | X29 << 5 | r;
        }
    }
    for (u32 r = 8; r < 16; r += 2) {
        if (saved_float & (3u << r)) {
            u32 imm = (u32) - (int32_t)(2 * (n + 1)) & 0x7F;
            out[n++] = (load ? OP_LDP_D : OP_STP_D) | imm << 15 |
                       (r + 1) << 10 | X29 << 5 | r;
        }
    }
    return n;
}

static void
incoming_address(Emit *e, u32 d, u32 offset)
{
    offset += 32;
    if (offset < 4096) {
        put(e, OP_ADD_IMM | SF | offset << 10 | X29 << 5 | d);
        return;
    }
    mov_imm(e, d, offset, SF);
    put(e, OP_ADD | SF | d << 16 | X29 << 5 | d);
}

static NOINLINE void
receive_vector(Emit *e, u32 p, ArgCursor *at)
{
    bool used = e->slots[p].weight != 0;
    if (at->floats < 8) {
        if (used) {
            memory(e, OP_STR_Q, 4, at->floats, e->slot_offsets[p], X16);
        }
        at->floats++;
        return;
    }

    u32 place = check_place_arg(at, 1 | REGS_FLOAT, 16, 16, e->place);
    assert(place & PLACE_STACK);
    e->wide_record = true;
    if (used) {
        incoming_address(e, X16, place & ~PLACE_STACK);
        put(e, OP_LDR_Q | X16 << 5 | V30);
        memory(e, OP_STR_Q, 4, V30, e->slot_offsets[p], X16);
    }
}

static NOINLINE void
receive_param(Emit *e, u32 p, ArgCursor *at)
{
    const IrSlot *slot = e->ir->slots + e->fn->first_slot + p;
    if (is_vector(e->info[slot->type])) {
        receive_vector(e, p, at);
        return;
    }
    u32 regs = check_struct_regs(e->c, slot->type);
    u32 *next = regs & REGS_FLOAT ? &at->floats : &at->ints;
    if (*next + (regs & REGS_COUNT) <= 8) {
        if (slot->weight) {
            move_struct(e, false, slot->type, *next, SP, e->slot_offsets[p]);
        }
        *next += regs & REGS_COUNT;
        return;
    }

    u32 place = place_struct(e, at, regs);
    assert(place & PLACE_STACK);
    e->wide_record = true;
    if (slot->weight) {
        incoming_address(e, X17, place & ~PLACE_STACK);
        sp_address(e, X16, e->slot_offsets[p]);
        copy_bytes(e, X16, X17, check_type_size(e->c, slot->type));
    }
}

static NOINLINE void
receive_stack(Emit *e, u32 p, u8 info, ArgCursor *at)
{
    bool fl = is_float(info);
    u32 scale = info & INFO_SCALE;
    u32 regs = ((info & INFO_PAIR) ? 2 : 1) | (fl ? REGS_FLOAT : 0);
    u32 place = check_place_arg(at, regs, 1u << scale, 1u << scale, e->place);
    assert(place & PLACE_STACK);
    e->wide_record = true;

    u8 h = e->slot_regs[p];
    if (h == NO_REG && !e->slots[p].weight) {
        return;
    }
    u32 r = h != NO_REG ? h & 31u : fl ? V30 : X17;
    u32 offset = (place & ~PLACE_STACK) + 32;
    assert((offset & ((1u << scale) - 1)) == 0);
    if ((offset >> scale) < 4096) {
        put(e, load_op(info) | (offset >> scale) << 10 | X29 << 5 | r);
    } else {
        incoming_address(e, X16, offset - 32);
        put(e, load_op(info) | X16 << 5 | r);
    }
    if (h == NO_REG) {
        store_typed(e, e->slots[p].type, r, e->slot_offsets[p]);
    }
}

static NOINLINE void
hold_struct(Emit *e, u32 i, u32 a)
{
    if (e->regs[a] != NO_REG) {
        take_over(e, a, i, INFO_MEMORY | 3);
        return;
    }
    e->spills[i] = e->spills[a];
    e->vinfo[i] = INFO_MEMORY | 3;
}

static void
param_address(Emit *e, u32 i, u32 p, u8 info)
{
    u8 h = e->slot_regs[p];
    if (h != NO_REG) {
        add_alias(e, i, h, info);
        return;
    }
    u32 d = result(e, i, info);
    memory(e, OP_LDRX, 3, d, e->slot_offsets[p], d);
}

static NOINLINE void
return_struct(Emit *e, u32 type, u32 a)
{
    if (e->result_at != NO_VALUE) {
        u32 s = fetch(e, a, X17);
        release(e, a);
        memory(e, OP_LDRX, 3, X16, e->result_at, X16);
        copy_between(e, X16, s, check_type_size(e->c, type));
        return;
    }
    u32 p = fetch(e, a, X16);
    if (p < 8) {
        mov(e, SF, X16, p);
        p = X16;
    }
    move_struct(e, true, type, 0, p, 0);
    release(e, a);
}

static bool
reads_lanes_next(const Emit *e, u32 v)
{
    if (v + 1 >= e->fn->count) {
        return false;
    }
    u32 at = e->fn->first + v + 1;
    if (e->ir->kinds[at] == IR_CALL) {
        const u32 *args = e->ir->extra + e->fn->first_extra + e->ir->bs[at];
        for (u32 k = 1; k <= args[0]; k++) {
            if (args[k] == v) {
                return true;
            }
        }
        return false;
    }
    if (!is_vector(e->info[e->ir->types[at]])) {
        return false;
    }
    switch (e->ir->kinds[at]) {
        case IR_STORE:
        case IR_EXIT: return e->ir->bs[at] == v;
        case IR_LOAD:
        case IR_LOAD_AT:
        case IR_SPLAT:
        case IR_END: return false;
        default: return e->ir->as[at] == v || e->ir->bs[at] == v;
    }
}

static u32
new_home(Emit *e)
{
    if (e->nhomes) {
        return e->homes[--e->nhomes];
    }
    u32 offset = (e->locals + e->spill_top + 15) & ~15u;
    e->spill_top = offset + 16 - e->locals;
    if (e->spill_top > e->spill_max) {
        e->spill_max = e->spill_top;
    }
    return offset;
}

static void
keep_lanes(Emit *e, u32 v)
{
    if (reads_lanes_next(e, v)) {
        e->spills[v] = NO_VALUE;
        return;
    }
    u32 home = new_home(e);
    memory(e, OP_STR_Q, 4, V29, home, X8);
    e->spills[v] = home;
}

static void
shift_lanes(Emit *e, u8 kind, u8 t, u32 a, u32 b)
{
    u32 scale = t & INFO_SCALE;
    u32 bits = 8u << scale;
    if (b & IR_CONST) {
        u32 count = (u32)constant_bits(e, b);
        u32 r = fetch_lanes(e, a, V30);
        if (kind == IR_SHL || count == 0) {
            put(e, OP_SHL_IMM | (bits + count) << 16 | r << 5 | V29);
        } else {
            u32 op = is_signed(t) ? OP_SSHR_IMM : OP_USHR_IMM;
            put(e, op | (2 * bits - count) << 16 | r << 5 | V29);
        }
        return;
    }

    u8 count_info = operand_info(e, b);
    u32 rb = X17;
    if (UNLIKELY(count_info & INFO_UNSUPPORTED)) {
        check_wide_count(e, b, bits, X17);
    } else {
        rb = fetch(e, b, X17);
        put(e, OP_SUBS_IMM | width(count_info) | bits << 10 | rb << 5 | ZR);
        trap_if(e, COND_HS, TRAP_SHIFT);
        release(e, b);
    }
    if (kind == IR_SHR) {
        put(e, OP_SUB | rb << 16 | ZR << 5 | X16);
        rb = X16;
    }
    put(e, OP_DUP | (1u << scale) << 16 | rb << 5 | V31);

    u32 r = fetch_lanes(e, a, V30);
    put(e, (is_signed(t) ? OP_SSHL : OP_USHL) | scale << 22 | V31 << 16 | r << 5 | V29);
}

static void
gather_top_bits(Emit *e, u32 i, u8 t, u32 a)
{
    u32 scale = t & INFO_SCALE;
    u32 bits = 8u << scale;
    u32 d = result(e, i, e->info[scale ? TYPE_ID_U8 : TYPE_ID_U16]);
    u32 r = fetch_lanes(e, a, V30);

    put(e, OP_USHR_IMM | (bits + 1) << 16 | r << 5 | V29);
    for (u32 n = 1, w = bits; w < 64; n *= 2, w *= 2) {
        put(e, OP_USRA | (3 * w + n) << 16 | V29 << 5 | V29);
    }
    put(e, OP_UMOV_B | V29 << 5 | X16);
    put(e, OP_UMOV_B | 8u << 17 | V29 << 5 | X17);
    put(e, OP_ORR | X17 << 16 | (8u >> scale) << 10 | X16 << 5 | d);
}

static u32
lane_op(u8 kind, u8 t)
{
    u32 size = (u32)(t & INFO_SCALE) << 22;
    bool s = is_signed(t);
    switch (kind) {
        case IR_ADD_WRAP: return OP_VADD | size;
        case IR_SUB_WRAP: return OP_VSUB | size;
        case IR_MUL_WRAP: return OP_VMUL | size;
        case IR_AND: return OP_VAND;
        case IR_OR: return OP_VORR;
        case IR_XOR: return OP_VEOR;
        case IR_EQ:
        case IR_NE: return OP_CMEQ | size;
        case IR_LT:
        case IR_GT: return (s ? OP_CMGT : OP_CMHI) | size;
        case IR_LE:
        case IR_GE: return (s ? OP_CMGE : OP_CMHS) | size;
        case IR_LOOKUP: return OP_TBL;
        default: assert(false && "unknown vector instruction"); return 0;
    }
}

static void
multiply_wide_lanes(Emit *e, u32 ra, u32 rb)
{
    put(e, OP_UMOV_D | ra << 5 | X16);
    put(e, OP_UMOV_D | rb << 5 | X17);
    put(e, OP_MUL | SF | X17 << 16 | X16 << 5 | X16);
    put(e, OP_UMOV_D | 1u << 20 | ra << 5 | X17);
    put(e, OP_UMOV_D | 1u << 20 | rb << 5 | X8);
    put(e, OP_MUL | SF | X8 << 16 | X17 << 5 | X17);
    put(e, OP_FMOV_FROM_X | X16 << 5 | V29);
    put(e, OP_INS_D1 | X17 << 5 | V29);
}

static u32
logical_field(u64 value, u32 sf)
{
    u32 field = 0;
    [[maybe_unused]] bool ok = logical_imm(value, sf, &field);
    assert(ok);
    return field;
}

static void
keep_wide(Emit *e, u32 v, u8 t, u32 lo, u32 hi)
{
    u32 home = new_home(e);
    pair_at(e, OP_STP_X, lo, hi, home);
    e->spills[v] = home;
    e->vinfo[v] = t;
}

static u32
wide_home(Emit *e, u32 x)
{
    if (x & IR_CONST) {
        u32 home = new_home(e);
        fetch_wide(e, x, X16, X17);
        pair_at(e, OP_STP_X, X16, X17, home);
        return home;
    }
    u32 home = e->spills[x];
    if (home == NO_VALUE) {
        home = new_home(e);
        memory(e, OP_STR_Q, 4, V29, home, X8);
    }
    return home;
}

static u32
call_routine(Emit *e, u32 a, u32 b, u32 *calls, u32 mode)
{
    u32 ha = wide_home(e, a);
    u32 hb = wide_home(e, b);
    u32 hr = new_home(e);

    sp_address(e, X16, ha);
    sp_address(e, X17, hb);
    sp_address(e, X8, hr);
    if (mode) {
        put(e, OP_ADD_IMM | SF | mode << 10 | X8 << 5 | X8);
    }
    add_fixup(e, calls);
    put(e, OP_BL);

    release_home(e, ha);
    release_home(e, hb);
    return hr;
}

static void
add_sub_wide(Emit *e, u32 i, u8 kind, u8 t, u32 a, u32 b)
{
    bool sub = kind == IR_SUB || kind == IR_SUB_WRAP;
    bool checked = kind == IR_ADD || kind == IR_SUB;
    u32 b1 = take_reg(e, false);
    fetch_wide(e, a, X16, X17);
    fetch_wide(e, b, X8, b1);

    u32 carry = sub ? (checked ? OP_SBCS : OP_SBC) : (checked ? OP_ADCS : OP_ADC);
    put(e, (sub ? OP_SUBS : OP_ADDS) | SF | X8 << 16 | X16 << 5 | X16);
    put(e, carry | SF | b1 << 16 | X17 << 5 | X17);
    if (checked) {
        trap_if(e, is_signed(t) ? COND_VS : (sub ? COND_LO : COND_HS), TRAP_OVERFLOW);
    }
    e->free_int |= 1u << b1;
    keep_wide(e, i, t, X16, X17);
}

static void
unsigned_product(Emit *e, u32 b0, u32 b1, u32 *overflow)
{
    put(e, OP_SUBS_IMM | SF | X17 << 5 | ZR);
    put(e, OP_CCMP_IMM | SF | (u32)COND_NE << 12 | b1 << 5 | 4);
    add_fixup(e, overflow);
    put(e, OP_B_COND | COND_NE);
    put(e, OP_UMULH | b0 << 16 | X17 << 5 | X8);
    add_fixup(e, overflow);
    put(e, OP_CBNZ | SF | X8);
    put(e, OP_UMULH | b1 << 16 | X16 << 5 | X8);
    add_fixup(e, overflow);
    put(e, OP_CBNZ | SF | X8);
    put(e, OP_MUL | SF | b0 << 16 | X17 << 5 | X8);
    put(e, OP_MADD | SF | b1 << 16 | X8 << 10 | X16 << 5 | X8);
    put(e, OP_UMULH | b0 << 16 | X16 << 5 | X17);
    put(e, OP_ADDS | SF | X8 << 16 | X17 << 5 | X17);
    add_fixup(e, overflow);
    put(e, OP_B_COND | COND_HS);
    put(e, OP_MUL | SF | b0 << 16 | X16 << 5 | X16);
}

static void
multiply_wide(Emit *e, u32 i, u8 kind, u8 t, u32 a, u32 b)
{
    if (kind == IR_MUL && is_signed(t)) {
        e->spills[i] = call_routine(e, a, b, &e->products, 0);
        e->vinfo[i] = t;
        trap_if(e, COND_NE, TRAP_OVERFLOW);
        return;
    }

    u32 b0 = take_reg(e, false);
    u32 b1 = take_reg(e, false);
    fetch_wide(e, a, X16, X17);
    fetch_wide(e, b, b0, b1);

    if (kind == IR_MUL) {
        unsigned_product(e, b0, b1, &e->traps[TRAP_OVERFLOW]);
    } else {
        put(e, OP_MUL | SF | b1 << 16 | X16 << 5 | b1);
        put(e, OP_MADD | SF | b0 << 16 | b1 << 10 | X17 << 5 | X17);
        put(e, OP_UMULH | b0 << 16 | X16 << 5 | b1);
        put(e, OP_ADD | SF | b1 << 16 | X17 << 5 | X17);
        put(e, OP_MUL | SF | b0 << 16 | X16 << 5 | X16);
    }
    e->free_int |= 1u << b0 | 1u << b1;
    keep_wide(e, i, t, X16, X17);
}

static void
shift_wide(Emit *e, u32 i, u8 kind, u8 t, u32 a, u32 b)
{
    bool s = is_signed(t);
    u32 shr = s ? OP_ASR_IMM : OP_LSR_IMM;
    if (b & IR_CONST) {
        u32 n = (u32)constant_bits(e, b);
        fetch_wide(e, a, X16, X17);
        u32 lo = X16, hi = X17;
        if (kind == IR_SHL && n >= 64) {
            lsl_imm(e, X17, X16, n - 64);
            lo = ZR;
        } else if (kind == IR_SHL && n > 0) {
            put(e, OP_EXTR | X16 << 16 | (64 - n) << 10 | X17 << 5 | X17);
            lsl_imm(e, X16, X16, n);
        } else if (n >= 64) {
            shift_imm(e, shr, SF, X16, X17, n - 64);
            if (s) {
                shift_imm(e, OP_ASR_IMM, SF, X17, X17, 63);
            } else {
                hi = ZR;
            }
        } else if (n > 0) {
            put(e, OP_EXTR | X16 << 16 | n << 10 | X17 << 5 | X16);
            shift_imm(e, shr, SF, X17, X17, n);
        }
        keep_wide(e, i, t, lo, hi);
        return;
    }

    u8 count_info = operand_info(e, b);
    u32 c = take_reg(e, false);
    u32 t1 = take_reg(e, false);
    u32 n = c;
    if (count_info & INFO_UNSUPPORTED) {
        check_wide_count(e, b, 128, c);
    } else {
        n = fetch(e, b, c);
        put(e, OP_SUBS_IMM | width(count_info) | 128u << 10 | n << 5 | ZR);
        trap_if(e, COND_HS, TRAP_SHIFT);
        release(e, b);
    }
    fetch_wide(e, a, X16, X17);

    u32 field = logical_field(64, 0);
    put(e, OP_ORN | SF | n << 16 | ZR << 5 | X8);
    if (kind == IR_SHL) {
        shift_imm(e, OP_LSR_IMM, SF, t1, X16, 1);
        put(e, OP_LSRV | SF | X8 << 16 | t1 << 5 | t1);
        put(e, OP_LSLV | SF | n << 16 | X17 << 5 | X8);
        put(e, OP_ORR | SF | X8 << 16 | t1 << 5 | t1);
        put(e, OP_LSLV | SF | n << 16 | X16 << 5 | X8);
        put(e, OP_ANDS_IMM | field | n << 5 | ZR);
        put(e, OP_CSEL | SF | t1 << 16 | (u32)COND_NE << 12 | X8 << 5 | X17);
        put(e, OP_CSEL | SF | X8 << 16 | (u32)COND_NE << 12 | ZR << 5 | X16);
    } else {
        lsl_imm(e, t1, X17, 1);
        put(e, OP_LSLV | SF | X8 << 16 | t1 << 5 | t1);
        put(e, OP_LSRV | SF | n << 16 | X16 << 5 | X8);
        put(e, OP_ORR | SF | X8 << 16 | t1 << 5 | t1);
        put(e, (s ? OP_ASRV : OP_LSRV) | SF | n << 16 | X17 << 5 | X8);
        put(e, OP_ANDS_IMM | field | n << 5 | ZR);
        put(e, OP_CSEL | SF | t1 << 16 | (u32)COND_NE << 12 | X8 << 5 | X16);
        u32 fill = ZR;
        if (s) {
            shift_imm(e, OP_ASR_IMM, SF, t1, X17, 63);
            fill = t1;
        }
        put(e, OP_CSEL | SF | X8 << 16 | (u32)COND_NE << 12 | fill << 5 | X17);
    }
    e->free_int |= 1u << c | 1u << t1;
    keep_wide(e, i, t, X16, X17);
}

static void
negate_wide(Emit *e, u32 i, u8 kind, u8 t, u32 a)
{
    fetch_wide(e, a, X16, X17);
    if (kind == IR_NEG && !is_signed(t)) {
        put(e, OP_ORR | SF | X17 << 16 | X16 << 5 | X8);
        trap_unless_zero(e, X8, SF, TRAP_OVERFLOW);
        keep_wide(e, i, t, ZR, ZR);
        return;
    }
    put(e, OP_SUBS | SF | X16 << 16 | ZR << 5 | X16);
    put(e, (kind == IR_NEG ? OP_SBCS : OP_SBC) | SF | X17 << 16 | ZR << 5 | X17);
    if (kind == IR_NEG) {
        trap_if(e, COND_VS, TRAP_OVERFLOW);
    }
    keep_wide(e, i, t, X16, X17);
}

static void
compare_wide(Emit *e, u32 i, u8 kind, u8 t, u32 a, u32 b)
{
    u32 b1 = take_reg(e, false);
    fetch_wide(e, a, X16, X17);
    fetch_wide(e, b, X8, b1);

    u8 cond;
    if (kind == IR_EQ || kind == IR_NE) {
        put(e, OP_SUBS | SF | X8 << 16 | X16 << 5 | ZR);
        put(e, OP_CCMP | SF | b1 << 16 | (u32)COND_EQ << 12 | X17 << 5);
        cond = kind == IR_EQ ? COND_EQ : COND_NE;
    } else {
        bool swapped = kind == IR_GT || kind == IR_LE;
        u32 l0 = swapped ? X8 : X16, l1 = swapped ? b1 : X17;
        u32 r0 = swapped ? X16 : X8, r1 = swapped ? X17 : b1;
        put(e, OP_SUBS | SF | r0 << 16 | l0 << 5 | ZR);
        put(e, OP_SBCS | SF | r1 << 16 | l1 << 5 | ZR);
        bool less = kind == IR_LT || kind == IR_GT;
        cond = is_signed(t) ? (less ? COND_LT : COND_GE) : (less ? COND_LO : COND_HS);
    }
    e->free_int |= 1u << b1;
    flag_result(e, i, cond);
}

static void
overflows_wide(Emit *e, u32 i, u8 kind, u8 t, u32 a, u32 b)
{
    bool s = is_signed(t);
    if (kind == IR_MUL_OVERFLOWS && s) {
        release_home(e, call_routine(e, a, b, &e->products, 0));
        flag_result(e, i, COND_NE);
        return;
    }
    if (kind == IR_MUL_OVERFLOWS) {
        u32 b0 = take_reg(e, false);
        u32 b1 = take_reg(e, false);
        fetch_wide(e, a, X16, X17);
        fetch_wide(e, b, b0, b1);
        u32 d = result(e, i, e->info[TYPE_ID_BOOL]);

        u32 done = 0;
        put(e, OP_MOVZ | 1u << 5 | d);
        unsigned_product(e, b0, b1, &done);
        put(e, OP_MOVZ | d);
        patch(e, done, here(e));
        e->free_int |= 1u << b0 | 1u << b1;
        return;
    }

    bool sub = kind == IR_SUB_OVERFLOWS;
    u32 b1 = take_reg(e, false);
    fetch_wide(e, a, X16, X17);
    fetch_wide(e, b, X8, b1);
    put(e, (sub ? OP_SUBS : OP_ADDS) | SF | X8 << 16 | X16 << 5 | ZR);
    put(e, (sub ? OP_SBCS : OP_ADCS) | SF | b1 << 16 | X17 << 5 | ZR);
    e->free_int |= 1u << b1;
    flag_result(e, i, s ? COND_VS : sub ? COND_LO : COND_HS);
}

static NOINLINE void
wide_from_float(Emit *e, u32 i, u8 t, u8 from, u32 a)
{
    bool s = is_signed(t);
    bool dbl = bits_of(from) == 64;
    u32 fp = precision(from);
    u32 ra = fetch(e, a, V30);
    release(e, a);
    check_float_range(e, ra, from, 128, s);

    u32 src = ra;
    if (s) {
        put(e, OP_FCMP_ZERO | fp | ra << 5);
        put(e, OP_FABS | fp | ra << 5 | V30);
        src = V30;
    }
    float_imm(e, V29, power_bits(from, -64), dbl);
    put(e, OP_FMUL | fp | V29 << 16 | src << 5 | V31);
    put(e, OP_FCVTZU | SF | fp | V31 << 5 | X17);
    put(e, OP_UCVTF | SF | fp | X17 << 5 | V31);
    float_imm(e, V29, power_bits(from, 64), dbl);
    put(e, OP_FMUL | fp | V29 << 16 | V31 << 5 | V31);
    put(e, OP_FSUB | fp | V31 << 16 | src << 5 | V31);
    put(e, OP_FCVTZU | SF | fp | V31 << 5 | X16);
    if (s) {
        put(e, OP_CSINV | SF | ZR << 16 | (u32)(COND_MI ^ 1) << 12 | ZR << 5 | X8);
        negate_if(e, X16, X17, X8);
    }
    keep_wide(e, i, t, X16, X17);
}

static void
convert_to_wide(Emit *e, u32 i, u8 kind, u8 t, u32 a)
{
    u8 from = operand_info(e, a);
    if (is_float(from)) {
        wide_from_float(e, i, t, from, a);
        return;
    }
    if (is_wide(from) && kind != IR_NARROW) {
        move_lanes(e, V29, a);
        e->vinfo[i] = t;
        keep_lanes(e, i);
        return;
    }
    if (is_wide(from)) {
        fetch_wide(e, a, X16, X17);
        put(e, OP_SUBS_IMM | SF | X17 << 5 | ZR);
        trap_if(e, COND_LT, TRAP_RANGE);
        keep_wide(e, i, t, X16, X17);
        return;
    }

    u32 ra = fetch(e, a, X16);
    release(e, a);
    if (kind == IR_NARROW) {
        put(e, OP_SUBS_IMM | width(from) | ra << 5 | ZR);
        trap_if(e, COND_LT, TRAP_RANGE);
    }
    if (width(from)) {
        mov(e, SF, X16, ra);
    } else {
        put(e,
            is_signed(from) ? OP_SXTW | ra << 5 | X16
                            : OP_ORR | ra << 16 | ZR << 5 | X16);
    }
    u32 hi = ZR;
    if (is_signed(from)) {
        shift_imm(e, OP_ASR_IMM, SF, X17, X16, 63);
        hi = X17;
    }
    keep_wide(e, i, t, X16, hi);
}

static NOINLINE void
wide_int(Emit *e, u32 i, u8 kind, u32 type, u32 a, u32 b)
{
    u8 t = e->info[type];
    switch (kind) {
        case IR_ADD:
        case IR_SUB:
        case IR_ADD_WRAP:
        case IR_SUB_WRAP: add_sub_wide(e, i, kind, t, a, b); break;
        case IR_MUL:
        case IR_MUL_WRAP: multiply_wide(e, i, kind, t, a, b); break;
        case IR_DIV:
        case IR_REM: {
            u32 mode = (is_signed(t) ? 2u : 0u) | (kind == IR_REM ? 1u : 0u);
            e->spills[i] = call_routine(e, a, b, &e->divisions, mode);
            e->vinfo[i] = t;
            break;
        }
        case IR_SHL:
        case IR_SHR: shift_wide(e, i, kind, t, a, b); break;
        case IR_NEG:
        case IR_NEG_WRAP: negate_wide(e, i, kind, t, a); break;
        case IR_EQ:
        case IR_NE:
        case IR_LT:
        case IR_LE:
        case IR_GT:
        case IR_GE: compare_wide(e, i, kind, t, a, b); break;
        case IR_ADD_OVERFLOWS:
        case IR_SUB_OVERFLOWS:
        case IR_MUL_OVERFLOWS: overflows_wide(e, i, kind, t, a, b); break;
        case IR_CONVERT:
        case IR_NARROW:
        case IR_TRUNCATE: convert_to_wide(e, i, kind, t, a); break;
        default: assert(false && "not a 128-bit instruction");
    }
}

static NOINLINE void
lanes(Emit *e, u32 i, u8 kind, u32 type, u32 a, u32 b, const Native *n)
{
    u8 t = e->info[type];
    if (is_wide(t) && kind >= IR_ADD && kind <= IR_TRUNCATE && kind != IR_AND &&
        kind != IR_OR && kind != IR_XOR && kind != IR_BIT_NOT) {
        wide_int(e, i, kind, type, a, b);
        return;
    }
    switch (kind) {
        case IR_LOAD:
            memory(e, OP_LDR_Q, 4, V29, e->slot_offsets[a], X8);
            break;
        case IR_LOAD_AT: {
            u32 p = fetch(e, a, X16);
            release(e, a);
            put(e, OP_LDR_Q | p << 5 | V29);
            break;
        }
        case IR_SPLAT: {
            u32 r = fetch(e, a, X16);
            release(e, a);
            put(e, OP_DUP | (1u << (t & INFO_SCALE)) << 16 | r << 5 | V29);
            break;
        }
        case IR_SHL:
        case IR_SHR: shift_lanes(e, kind, t, a, b); break;
        case IR_NEG_WRAP: {
            u32 r = fetch_lanes(e, a, V30);
            put(e, OP_VNEG | (u32)(t & INFO_SCALE) << 22 | r << 5 | V29);
            break;
        }
        case IR_BIT_NOT: {
            u32 r = fetch_lanes(e, a, V30);
            put(e, OP_VNOT | r << 5 | V29);
            break;
        }
        case IR_STORE: {
            u32 r = fetch_lanes(e, b, V30);
            memory(e, OP_STR_Q, 4, r, e->slot_offsets[a], X8);
            return;
        }
        case IR_STORE_AT: {
            u32 r = fetch_lanes(e, b, V30);
            u32 p = fetch(e, a, X16);
            put(e, OP_STR_Q | p << 5 | r);
            release(e, a);
            return;
        }
        case IR_TOP_BITS: gather_top_bits(e, i, t, a); return;
        case IR_BITCAST: move_lanes(e, V29, a); break;
        case IR_CALL: call(e, i, type, a, b, n, FIRST_INT_ARG); break;
        case IR_RET:
            move_lanes(e, 0, a);
            if (i + 1 < e->fn->count) {
                add_fixup(e, &e->epilogue);
                put(e, OP_B);
            }
            return;
        case IR_IF: {
            e->open = grow(e->open, &e->open_cap, e->nopen + 1, sizeof *e->open);
            Open o = { .target = i, .kind = IR_IF, .result = NO_REG };
            if (e->fused == a) {
                spill_all(e);
                add_fixup(e, &o.skip);
                put(e, OP_B_COND | (u32)(e->fused_cond ^ 1));
                e->fused = NO_VALUE;
            } else {
                u32 r = fetch(e, a, X16);
                release(e, a);
                spill_all(e);
                add_fixup(e, &o.skip);
                put(e, OP_CBZ | r);
            }
            e->open[e->nopen++] = o;
            return;
        }
        case IR_ELSE: {
            Open *o = &e->open[e->nopen - 1];
            move_lanes(e, V29, a);
            add_fixup(e, &o->ends);
            put(e, OP_B);
            patch(e, o->skip, here(e));
            o->skip = 0;
            return;
        }
        case IR_EXIT: {
            Open *o = find_open(e, a);
            fetch_lanes(e, b, V29);
            bool falls = i + 1 < e->fn->count &&
                         e->ir->kinds[e->fn->first + i + 1] == IR_END &&
                         o == &e->open[e->nopen - 1];
            if (!falls) {
                add_fixup(e, &o->ends);
                put(e, OP_B);
            }
            return;
        }
        case IR_END: {
            Open o = e->open[--e->nopen];
            assert(o.kind == IR_BLOCK || o.kind == IR_IF);
            if (o.kind == IR_IF) {
                move_lanes(e, V29, a);
            }
            patch(e, o.ends, here(e));
            break;
        }
        default: {
            u32 ra = fetch_lanes(e, a, V30);
            u32 rb = fetch_lanes(e, b, V31);
            if (kind == IR_MUL_WRAP && (t & INFO_SCALE) == 3) {
                multiply_wide_lanes(e, ra, rb);
                break;
            }
            if (kind == IR_LT || kind == IR_LE) {
                swap(&ra, &rb);
            }
            put(e, lane_op(kind, t) | rb << 16 | ra << 5 | V29);
            if (kind == IR_NE) {
                put(e, OP_VNOT | V29 << 5 | V29);
            }
            break;
        }
    }
    e->vinfo[i] = t;
    keep_lanes(e, i);
}

static const u8 routine_saves[] = { 0, 2, 4, 6, 9 };

static void
save_routine_regs(Emit *e)
{
    for (u32 k = 0; k < sizeof routine_saves; k++) {
        u32 r = routine_saves[k];
        put(e, OP_STP_X_PRE | 0x7Eu << 15 | (r + 1) << 10 | SP << 5 | r);
    }
}

static void
restore_routine_regs(Emit *e)
{
    for (u32 k = sizeof routine_saves; k-- > 0;) {
        u32 r = routine_saves[k];
        put(e, OP_LDP_X_POST | 2u << 15 | (r + 1) << 10 | SP << 5 | r);
    }
}

static void
patch_test(Emit *e, u32 at)
{
    e->code[at] |= ((here(e) - at) & 0x3FFFu) << 5;
}

static NOINLINE void
emit_divmod(Emit *e)
{
    save_routine_regs(e);
    put(e, OP_LDP_X | 1u << 10 | X16 << 5 | 0);
    put(e, OP_LDP_X | 3u << 10 | X17 << 5 | 2);
    put(e, OP_ORR | SF | 3u << 16 | 2u << 5 | 9);
    trap_if_zero(e, 9, SF, TRAP_DIVIDE);

    u32 to_unsigned = here(e);
    put(e, OP_TBZ | 1u << 19 | X8);
    put(e, OP_EOR | SF | 3u << 16 | 1u << 5 | 10);
    put(e, OP_ANDS_IMM | SF | logical_field(1, SF) | X8 << 5 | ZR);
    put(e, OP_CSEL | SF | 10u << 16 | (u32)COND_NE << 12 | 1u << 5 | 10);
    shift_imm(e, OP_ASR_IMM, SF, 9, 1, 63);
    negate_if(e, 0, 1, 9);
    shift_imm(e, OP_ASR_IMM, SF, 9, 3, 63);
    negate_if(e, 2, 3, 9);
    patch_test(e, to_unsigned);

    put(e, OP_ORR | SF | 3u << 16 | 1u << 5 | 9);
    u32 to_slow = here(e);
    put(e, OP_CBNZ | SF | 9);
    put(e, OP_UDIV | SF | 2u << 16 | 0u << 5 | 9);
    put(e, OP_MSUB | SF | 2u << 16 | 0u << 10 | 9u << 5 | 4);
    mov(e, SF, 0, 9);
    mov(e, SF, 5, ZR);
    u32 to_select = here(e);
    put(e, OP_B);
    patch_one(e, to_slow, here(e));

    mov(e, SF, 4, ZR);
    mov(e, SF, 5, ZR);
    put(e, OP_MOVZ | 128u << 5 | 9);
    u32 loop = here(e);
    put(e, OP_EXTR | 4u << 16 | 63u << 10 | 5u << 5 | 5);
    put(e, OP_EXTR | 1u << 16 | 63u << 10 | 4u << 5 | 4);
    put(e, OP_EXTR | 0u << 16 | 63u << 10 | 1u << 5 | 1);
    lsl_imm(e, 0, 0, 1);
    put(e, OP_SUBS | SF | 2u << 16 | 4u << 5 | 6);
    put(e, OP_SBCS | SF | 3u << 16 | 5u << 5 | 7);
    put(e, OP_CSEL | SF | 4u << 16 | (u32)COND_HS << 12 | 6u << 5 | 4);
    put(e, OP_CSEL | SF | 5u << 16 | (u32)COND_HS << 12 | 7u << 5 | 5);
    put(e, OP_CSINC | SF | 0u << 16 | (u32)COND_LO << 12 | 0u << 5 | 0);
    put(e, OP_SUBS_IMM | SF | 1u << 10 | 9u << 5 | 9);
    put(e, OP_B_COND | ((loop - here(e)) & 0x7FFFFu) << 5 | COND_NE);
    patch_one(e, to_select, here(e));

    put(e, OP_ANDS_IMM | SF | logical_field(1, SF) | X8 << 5 | ZR);
    put(e, OP_CSEL | SF | 0u << 16 | (u32)COND_NE << 12 | 4u << 5 | 0);
    put(e, OP_CSEL | SF | 1u << 16 | (u32)COND_NE << 12 | 5u << 5 | 1);
    u32 to_store = here(e);
    put(e, OP_TBZ | 1u << 19 | X8);
    shift_imm(e, OP_ASR_IMM, SF, 10, 10, 63);
    u32 to_negate = here(e);
    put(e, OP_TBNZ | X8);
    put(e, OP_BICS | SF | 10u << 16 | 1u << 5 | ZR);
    trap_if(e, COND_MI, TRAP_OVERFLOW);
    patch_test(e, to_negate);
    negate_if(e, 0, 1, 10);
    patch_test(e, to_store);

    put(e, OP_AND_IMM | SF | logical_field(~3ull, SF) | X8 << 5 | X8);
    put(e, OP_STP_X | 1u << 10 | X8 << 5 | 0);
    restore_routine_regs(e);
    put(e, OP_RET);
}

static NOINLINE void
emit_signed_multiply(Emit *e)
{
    save_routine_regs(e);
    put(e, OP_LDP_X | 1u << 10 | X16 << 5 | 0);
    put(e, OP_LDP_X | 3u << 10 | X17 << 5 | 2);
    put(e, OP_EOR | SF | 3u << 16 | 1u << 5 | 10);
    shift_imm(e, OP_ASR_IMM, SF, 10, 10, 63);
    shift_imm(e, OP_ASR_IMM, SF, 9, 1, 63);
    negate_if(e, 0, 1, 9);
    shift_imm(e, OP_ASR_IMM, SF, 9, 3, 63);
    negate_if(e, 2, 3, 9);

    put(e, OP_SUBS_IMM | SF | 1u << 5 | ZR);
    put(e, OP_CCMP_IMM | SF | (u32)COND_NE << 12 | 3u << 5 | 4);
    put(e, OP_CSET | (u32)(COND_NE ^ 1) << 12 | 6);
    put(e, OP_UMULH | 2u << 16 | 1u << 5 | 4);
    put(e, OP_UMULH | 3u << 16 | 0u << 5 | 5);
    put(e, OP_ORR | SF | 5u << 16 | 4u << 5 | 4);
    put(e, OP_SUBS_IMM | SF | 4u << 5 | ZR);
    put(e, OP_CSINC | 6u << 16 | (u32)COND_EQ << 12 | 6u << 5 | 6);
    put(e, OP_MUL | SF | 2u << 16 | 1u << 5 | 4);
    put(e, OP_MADD | SF | 3u << 16 | 4u << 10 | 0u << 5 | 4);
    put(e, OP_UMULH | 2u << 16 | 0u << 5 | 5);
    put(e, OP_ADDS | SF | 4u << 16 | 5u << 5 | 5);
    put(e, OP_CSINC | 6u << 16 | (u32)COND_LO << 12 | 6u << 5 | 6);
    put(e, OP_MUL | SF | 2u << 16 | 0u << 5 | 4);

    mov_imm(e, 9, 0x7FFFFFFFFFFFFFFFull, SF);
    put(e, OP_SUB | SF | 10u << 16 | 9u << 5 | 9);
    put(e, OP_ORN | SF | 10u << 16 | ZR << 5 | 7);
    put(e, OP_SUBS | SF | 4u << 16 | 7u << 5 | ZR);
    put(e, OP_SBCS | SF | 5u << 16 | 9u << 5 | ZR);
    put(e, OP_CSINC | 6u << 16 | (u32)COND_HS << 12 | 6u << 5 | 6);
    negate_if(e, 4, 5, 10);
    put(e, OP_STP_X | 5u << 10 | X8 << 5 | 4);
    put(e, OP_SUBS_IMM | 6u << 5 | ZR);
    restore_routine_regs(e);
    put(e, OP_RET);
}

static u32
emit_function(Emit *e, const IrFunc *fn, u32 *code, const Native *n)
{
    const IrCode *ir = e->ir;
    e->fn = fn;
    e->code = code;
    e->nopen = 0;
    e->nfixups = 1;
    e->nspill_free = 0;
    e->nhomes = 0;
    e->spill_top = 0;
    e->spill_max = 0;
    e->naliases = 0;
    e->epilogue = 0;
    e->called = false;
    e->context_saved = false;
    e->wide_record = false;
    e->free_int = INT_POOL;
    e->free_float = FLOAT_POOL;
    e->fused = NO_VALUE;
    e->tested = NO_VALUE;
    e->divisions = 0;
    e->products = 0;
    memset(e->traps, 0, sizeof e->traps);
    memset(e->holders, 0xFF, sizeof e->holders);
    if (fn->count > e->values_cap) {
        e->values_cap = fn->count * 2;
        e->regs = realloc(e->regs, e->values_cap * sizeof *e->regs);
        e->spills = realloc(e->spills, e->values_cap * sizeof *e->spills);
        e->vinfo = realloc(e->vinfo, e->values_cap * sizeof *e->vinfo);
        e->aliases = realloc(e->aliases, e->values_cap * sizeof *e->aliases);
        e->alias_at = realloc(e->alias_at, e->values_cap * sizeof *e->alias_at);
    }
    memset(e->regs, NO_REG, fn->count);
    pin_locals(e);
    lay_out_frame(e);
    memset(code, 0, PROLOGUE_WORDS * sizeof *code);
    e->at = code + PROLOGUE_WORDS;

    const IrSlot *slots = e->slots;
    if (e->result_at != NO_VALUE) {
        memory(e, OP_STRX, 3, X8, e->result_at, X16);
    }
    ArgCursor args = { FIRST_INT_ARG, 0, 0 };
    for (u32 p = 0; p < fn->nparams; p++) {
        u32 type = slots[p].type;
        u8 info = e->info[type];
        if (info & INFO_MEMORY) {
            receive_param(e, p, &args);
            continue;
        }
        bool fl = is_float(info);
        u32 *next = fl ? &args.floats : &args.ints;
        if (*next + ((info & INFO_PAIR) != 0) >= 8) {
            receive_stack(e, p, info, &args);
            continue;
        }
        u32 r = (*next)++;
        u8 h = e->slot_regs[p];
        if (h != NO_REG) {
            if (fl) {
                fmov(e, h & 31u, r);
            } else {
                take_from_c(e, info, h, r);
            }
        } else if (slots[p].weight) {
            store_typed(e, type, r, e->slot_offsets[p]);
        }
    }

    u32 i = 0;
    u32 count = fn->count;
    u32 first = fn->first;
    while (i < count && !e->error) {
        for (; i < count && !e->error; i++) {
            u32 at = first + i;
            u8 kind = ir->kinds[at];
            u32 type = ir->types[at];
            u32 a = ir->as[at];
            u32 b = ir->bs[at];
            if (UNLIKELY(e->info[type] & INFO_UNSUPPORTED)) {
                break;
            }
            switch (kind) {
                case IR_LOAD: {
                    u32 st = slot_type(e, a);
                    if (e->slot_regs[a] != NO_REG) {
                        add_alias(e, i, e->slot_regs[a], e->info[st]);
                        break;
                    }
                    u32 d = result(e, i, e->info[st]);
                    load_typed(e, st, d, e->slot_offsets[a]);
                    break;
                }
                case IR_STORE: {
                    u8 h = e->slot_regs[a];
                    if (h != NO_REG) {
                        store_pinned(e, h, type, b);
                        break;
                    }
                    u32 r;
                    if ((b & IR_CONST) && constant_bits(e, b) == 0) {
                        r = ZR;
                    } else {
                        r = fetch(e, b, is_float(e->info[type]) ? V30 : X16);
                        release(e, b);
                    }
                    store_typed(e, type, r, e->slot_offsets[a]);
                    break;
                }
                case IR_ADDR:
                    if (a < 32 ? e->by_ref >> a & 1
                               : a < fn->nparams && slots[a].type != slot_type(e, a)) {
                        param_address(e, i, a, e->info[type]);
                        break;
                    }
                    sp_address(e, result(e, i, e->info[type]), e->slot_offsets[a]);
                    break;
                case IR_DATA: data_address(e, i, type, a); break;
                case IR_GLOBAL: global_address(e, i, type, a); break;
                case IR_C_GLOBAL: c_global_address(e, i, type, b, n); break;
                case IR_CALLBACK: callback_address(e, i, type, a, b, n); break;
                case IR_LOAD_AT: {
                    u8 t = e->info[type];
                    if (t & INFO_MEMORY) {
                        hold_struct(e, i, a);
                        break;
                    }
                    u32 p = fetch(e, a, X16);
                    release(e, a);
                    if (t & INFO_PAIR) {
                        load_pair(e, i, p, t);
                        break;
                    }
                    u32 d = result(e, i, t);
                    put(e, load_op(t) | p << 5 | d);
                    break;
                }
                case IR_INDEX: index_address(e, i, type, a, b); break;
                case IR_FIELD:
                    offset_address(e, i, e->info[type], a, check_field_offset(e->c, b));
                    break;
                case IR_INDEX_LEN: index_len(e, i, type, a, b); break;
                case IR_SLICE:
                case IR_SLICE_FROM: slice(e, i, kind, type, a, b); break;
                case IR_SECOND: break;
                case IR_COPY: copy_memory(e, type, a, b); break;
                case IR_ZERO: zero_memory(e, type, a); break;
                case IR_STORE_AT: {
                    u8 t = e->info[type];
                    u32 p = fetch(e, a, X16);
                    u32 r = (b & IR_CONST) && constant_bits(e, b) == 0
                              ? ZR
                              : fetch(e, b, is_float(t) ? V30 : X17);
                    put(e, store_op(t, r) | p << 5 | r);
                    release(e, a);
                    release(e, b);
                    break;
                }
                case IR_STORE_PAIR: store_pair(e, a, b); break;
                case IR_ADD:
                case IR_SUB:
                case IR_ADD_WRAP:
                case IR_SUB_WRAP: add_sub(e, i, kind, type, a, b); break;
                case IR_MUL:
                case IR_MUL_WRAP:
                case IR_MUL_HIGH: multiply(e, i, kind, type, a, b); break;
                case IR_DIV:
                case IR_REM: divide(e, i, kind, type, a, b); break;
                case IR_AND:
                case IR_OR:
                case IR_XOR: bitwise(e, i, kind, type, a, b); break;
                case IR_SHL:
                case IR_SHR: shift(e, i, kind, type, a, b); break;
                case IR_NEG:
                case IR_NEG_WRAP:
                case IR_NOT:
                case IR_BIT_NOT: unary(e, i, kind, type, a); break;
                case IR_TRAILING_ZEROS:
                case IR_LEADING_ZEROS:
                case IR_COUNT_ONES: count_bits(e, i, kind, type, a); break;
                case IR_EQ:
                case IR_NE:
                case IR_LT:
                case IR_LE:
                case IR_GT:
                case IR_GE: compare(e, i, kind, type, a, b); break;
                case IR_ADD_OVERFLOWS:
                case IR_SUB_OVERFLOWS:
                case IR_MUL_OVERFLOWS:
                    test_overflow(e, i, kind, type, a, b);
                    break;
                case IR_CONVERT: convert(e, i, type, a); break;
                case IR_NARROW: narrow(e, i, type, a); break;
                case IR_TRUNCATE: truncate_to(e, i, type, a); break;
                case IR_BITCAST: bitcast_to(e, i, type, a); break;
                case IR_CHECK_ALIGN: check_align(e, i, type, a, b); break;
                case IR_CHECK_NIL: check_nil(e, i, type, a); break;
                case IR_CALL:
                case IR_CALL_C:
                    call(e, i, type, a, b, n, kind == IR_CALL ? FIRST_INT_ARG : 0);
                    break;
                case IR_DROP: release(e, a); break;
                case IR_ASSERT: check_assert(e, a); break;
                case IR_ASSUME: release(e, a); break;
                case IR_RET:
                    if (e->info[type] & INFO_PAIR) {
                        return_pair(e, a, b);
                    } else if (e->info[type] & INFO_MEMORY) {
                        return_struct(e, type, a);
                    } else if (type != TYPE_ID_VOID) {
                        move_into(e, 0, a, is_float(e->info[type]));
                    }
                    if (i + 1 < count) {
                        add_fixup(e, &e->epilogue);
                        put(e, OP_B);
                    }
                    break;
                case IR_BLOCK:
                case IR_LOOP:
                case IR_IF: {
                    e->open = grow(
                        e->open,
                        &e->open_cap,
                        e->nopen + 1,
                        sizeof *e->open
                    );
                    if (kind == IR_IF && (a & IR_CONST)) {
                        assert(type == TYPE_ID_NONE);
                        if (constant_bits(e, a)) {
                            e->open[e->nopen++] = (Open){ .target = i,
                                                          .kind = IR_IF,
                                                          .dead_else = true };
                            break;
                        }
                        i = arm_end(e, i);
                        if (ir->kinds[first + i] == IR_ELSE) {
                            e->open[e->nopen++] = (Open){ .target = i, .kind = IR_BLOCK };
                        }
                        break;
                    }

                    if (kind == IR_IF && e->fused != a) {
                        u32 r = fetch(e, a, X16);
                        release(e, a);
                        spill_all(e);
                        Open o = { .target = i, .kind = kind, .result = NO_REG };
                        add_fixup(e, &o.skip);
                        put(e, OP_CBZ | r);
                        e->open[e->nopen++] = o;
                        break;
                    }

                    spill_all(e);
                    if (kind == IR_LOOP) {
                        align_loop(e);
                    }
                    Open o = { .target = i, .start = here(e), .kind = kind, .result = NO_REG };
                    if (kind == IR_IF) {
                        add_fixup(e, &o.skip);
                        put(e, OP_B_COND | (u32)(e->fused_cond ^ 1));
                        e->fused = NO_VALUE;
                    }
                    e->open[e->nopen++] = o;
                    break;
                }
                case IR_ELSE: {
                    Open *o = &e->open[e->nopen - 1];
                    if (o->dead_else) {
                        e->nopen--;
                        i = arm_end(e, i);
                        break;
                    }
                    if (type != TYPE_ID_NONE) {
                        hand_over(e, o, type, a);
                    }
                    add_fixup(e, &o->ends);
                    put(e, OP_B);
                    patch(e, o->skip, here(e));
                    o->skip = 0;
                    break;
                }
                case IR_END: {
                    Open o = e->open[--e->nopen];
                    if (type != TYPE_ID_NONE) {
                        bool fl = is_float(e->info[type]);
                        if (o.kind == IR_IF) {
                            move_into(e, o.result, a, fl);
                        } else if (o.result == NO_REG) {
                            o.result = (u8)take_reg(e, fl);
                            *(fl ? &e->free_float : &e->free_int) |= 1u << o.result;
                        }
                        u32 *free_mask = fl ? &e->free_float : &e->free_int;
                        assert(*free_mask & (1u << o.result));
                        *free_mask &= ~(1u << o.result);
                        u32 h = (fl ? 32 : 0) + o.result;
                        e->holders[h] = i;
                        e->regs[i] = (u8)h;
                        e->vinfo[i] = e->info[type];
                    }
                    patch(e, o.skip, here(e));
                    patch(e, o.ends, here(e));
                    if (o.kind == IR_SWITCH) {
                        finish_switch(e, &o);
                    }
                    break;
                }
                case IR_EXIT: {
                    Open *o = find_open(e, a);
                    if (type != TYPE_ID_NONE) {
                        hand_over(e, o, type, b);
                    }
                    bool falls = i + 1 < count && ir->kinds[first + i + 1] == IR_END &&
                                 o == &e->open[e->nopen - 1];
                    if (!falls) {
                        add_fixup(e, &o->ends);
                        put(e, OP_B);
                    }
                    break;
                }
                case IR_REPEAT: {
                    Open *o = find_open(e, a);
                    put(e, OP_B | ((o->start - here(e)) & 0x03FFFFFFu));
                    break;
                }
                case IR_NOP: break;
                case IR_SWITCH: open_switch(e, i, type, a, b); break;
                case IR_CASE: start_arm(e, i); break;
                default: assert(false && "unknown IR instruction");
            }
        }
        if (i < count && !e->error) {
            u32 at = first + i;
            if (!is_vector(e->info[ir->types[at]])) {
                e->error = UNSUPPORTED_TYPE;
                break;
            }
            lanes(e, i, ir->kinds[at], ir->types[at], ir->as[at], ir->bs[at], n);
            i++;
        }
    }

    if (fn->count == 0 || ir->kinds[fn->first + fn->count - 1] != IR_RET) {
        add_fixup(e, &e->traps[TRAP_UNREACHABLE]);
        put(e, OP_B);
    }

    u32 saves[PROLOGUE_WORDS];
    u32 nsaves = callee_words(saves, false, e->saved_int, e->saved_float);
    u32 frame = (16 * nsaves + e->locals + e->spill_max + 15) & ~15u;
    bool record = fn->calls || frame || e->wide_record;
    assert(frame || !(e->divisions | e->products));
    bool wide = e->context_saved || e->wide_record;
    patch(e, e->epilogue, here(e));
    e->at += callee_words(e->at, true, e->saved_int, e->saved_float);
    if (record) {
        if (frame) {
            put(e, OP_RESET_SP);
        }
        put(e, wide ? OP_POP_CONTEXT_FRAME : OP_POP_FRAME);
    }
    put(e, OP_RET);
    if (e->divisions) {
        patch(e, e->divisions, here(e));
        emit_divmod(e);
    }
    if (e->products) {
        patch(e, e->products, here(e));
        emit_signed_multiply(e);
    }

    static_assert(NTRAPS <= 32, "every trap kind needs a bit in `kinds`");
    u32 kinds = 0;
    for (u32 k = 1; k < NTRAPS; k++) {
        kinds |= (u32)(e->traps[k] != 0) << k;
    }
    for (; kinds; kinds &= kinds - 1) {
        u32 k = (u32)__builtin_ctz(kinds);
        patch(e, e->traps[k], here(e));
        put(e, OP_BRK | k << 5);
    }

    if (frame >= FRAME_LIMIT) {
        e->error = "a function's frame is larger than 16 MB";
        return 0;
    }
    u32 probe = 0;
    if (frame >= PROBE_INTERVAL) {
        probe = here(e);
        mov_imm(e, X16, frame / PROBE_INTERVAL, SF);
        put(e, OP_SUB_SP_HIGH | 1 << 10);
        put(e, OP_STR_ZR_SP);
        put(e, OP_SUBS_IMM | SF | 1 << 10 | X16 << 5 | X16);
        put(e, OP_B_COND | ((u32)-3 & 0x7FFFFu) << 5 | COND_NE);
        if (frame % PROBE_INTERVAL) {
            put(e, OP_SUB_SP | (frame % PROBE_INTERVAL) << 10);
            put(e, OP_STR_ZR_SP);
        }
        put(e, OP_B);
    }

    u32 prologue[PROLOGUE_WORDS];
    u32 k = 0;
    if (record) {
        prologue[k++] = wide ? OP_PUSH_CONTEXT_FRAME : OP_PUSH_FRAME;
        prologue[k++] = OP_SET_FRAME;
    }
    if (e->context_saved) {
        prologue[k++] = OP_SAVE_CONTEXT;
    }
    u32 jump = k;
    if (probe) {
        prologue[k++] = OP_B;
    } else if (frame) {
        prologue[k++] = OP_SUB_SP | (frame & 0xFFF) << 10;
        if (frame >> 12) {
            prologue[k++] = OP_SUB_SP_HIGH | (frame >> 12) << 10;
        }
    }
    memcpy(prologue + k, saves, nsaves * sizeof *saves);
    k += nsaves;
    assert(k <= PROLOGUE_WORDS);
    u32 entry = PROLOGUE_WORDS - k;
    memcpy(code + entry, prologue, k * sizeof *prologue);
    if (probe) {
        u32 from = entry + jump;
        u32 back = here(e) - 1;
        code[from] = OP_B | ((probe - from) & 0x03FFFFFFu);
        code[back] = OP_B | ((from + 1 - back) & 0x03FFFFFFu);
    }
    return entry;
}

static u32
place_param(const Emit *e, ArgCursor *at, u32 type, u32 *regs)
{
    u8 info = e->info[type];
    if (info & INFO_MEMORY) {
        *regs = check_struct_regs(e->c, type);
        return place_struct(e, at, *regs);
    }
    u32 size = 1u << (info & INFO_SCALE);
    *regs = 1 | (is_float(info) ? REGS_FLOAT : 0);
    return check_place_arg(at, *regs, size, size, e->place);
}

static const u32 unit_loads[4] = { OP_LDRB, OP_LDRH, OP_LDRW, OP_LDRX };
static const u32 unit_stores[4] = { OP_STRB, OP_STRH, OP_STRW, OP_STRX };

static NOINLINE void
store_stack_args(Emit *e, const u32 *params, ArgCursor *in, ArgCursor *out)
{
    for (u32 j = 1; j <= params[0]; j++) {
        u32 type = params[j];
        u8 info = e->info[type];
        u32 regs;
        u32 from = place_param(e, in, type, &regs);
        u32 to = place_param(e, out, type, &regs);
        if (!(to & PLACE_STACK)) {
            assert(!(from & PLACE_STACK) && to == from + !(regs & REGS_FLOAT));
            continue;
        }

        u32 units = 1;
        u32 scale = info & INFO_SCALE;
        if (info & INFO_MEMORY) {
            units = regs & REGS_COUNT;
            scale = (regs & REGS_FLOAT) && !(regs & REGS_DOUBLE) ? 2 : 3;
        }
        to &= ~PLACE_STACK;
        for (u32 u = 0; u < units; u++) {
            u32 step = u << scale;
            u32 r = X16;
            if (from & PLACE_STACK) {
                u32 offset = 16 + (from & ~PLACE_STACK) + step;
                put(e, unit_loads[scale] | (offset >> scale) << 10 | X29 << 5 | X16);
            } else {
                assert(!(regs & REGS_FLOAT));
                r = from + u;
            }
            put(e, unit_stores[scale] | ((to + step) >> scale) << 10 | SP << 5 | r);
        }
    }
}

static NOINLINE void
emit_adapter(Emit *e, u32 decl, const Native *n)
{
    const u32 *params = e->c->lists + e->c->types.arg0[e->c->decls.types[decl]];
    ArgCursor in = { 0, 0, 0 };
    ArgCursor out = { FIRST_INT_ARG, 0, 0 };
    for (u32 j = 1; j <= params[0]; j++) {
        u32 regs;
        place_param(e, &in, params[j], &regs);
        place_param(e, &out, params[j], &regs);
    }
    if (in.stack > ADAPTER_STACK_LIMIT || out.stack > ADAPTER_STACK_LIMIT) {
        e->error = "a callback for C needs over 4064 bytes of stack arguments";
        return;
    }

    u32 frame = (out.stack + 15) & ~15u;
    if (frame) {
        put(e, OP_PUSH_FRAME);
        put(e, OP_SET_FRAME);
        put(e, OP_SUB_SP | frame << 10);
        ArgCursor from = { 0, 0, 0 };
        ArgCursor to = { FIRST_INT_ARG, 0, 0 };
        store_stack_args(e, params, &from, &to);
    }
    for (u32 r = in.ints < 7 ? in.ints : 7; r-- > 0;) {
        mov(e, SF, r + 1, r);
    }

    u64 context = (u64)(uintptr_t)(n->entries + e->c->decls.count);
    adrp(e, 0, context);
    put(e, OP_LDRX | (u32)((context & 0xFFF) >> 3) << 10);
    u64 entry = (u64)(uintptr_t)(n->entries + decl);
    adrp(e, X16, entry);
    put(e, OP_LDR_X16_X16 | (u32)((entry & 0xFFF) >> 3) << 10);
    if (!frame) {
        put(e, OP_BR | X16 << 5);
        return;
    }
    put(e, OP_BLR_X16);
    put(e, OP_RESET_SP);
    put(e, OP_POP_FRAME);
    put(e, OP_RET);
}

static void
jit_write(bool enable)
{
#if defined(__APPLE__) && defined(__aarch64__)
    pthread_jit_write_protect_np(enable ? 0 : 1);
#else
    (void)enable;
#endif
}

bool
arm64_compile(Native *n, const Checker *c, Convention convention)
{
    *n = (Native){ 0 };
    const Ir *ir = &c->ir;
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    size_t words = 0;
    for (u32 f = 0; f < ir->nfuncs; f++) {
        const IrFunc *fn = &ir->funcs[f];
        words += WORDS_PER_FUNCTION + (size_t)WORDS_PER_INSTRUCTION * fn->count +
                 (size_t)WORDS_PER_ARGUMENT * fn->nextra;
    }
    for (u32 k = 0; k < ir->ncallbacks; k++) {
        u32 type = c->decls.types[ir->callbacks[k]];
        u32 nparams = c->lists[c->types.arg0[type]];
        words += WORDS_PER_ADAPTER + (size_t)WORDS_PER_ADAPTER_PARAM * nparams;
    }
    size_t slots = (size_t)c->decls.count + 1;
    size_t table = (slots * sizeof(u64) + page - 1) & ~(page - 1);
    size_t code = table + (((size_t)ir->ndata + page - 1) & ~(page - 1));
    size_t size = code + ((words * sizeof(u32) + page - 1) & ~(page - 1));
    if (size == code) {
        size += page;
    }

#if defined(__aarch64__)
    int prot = PROT_READ | PROT_WRITE | PROT_EXEC;
#else
    int prot = PROT_READ | PROT_WRITE;
#endif
    int flags = MAP_PRIVATE | MAP_ANON;
#if defined(__APPLE__) && defined(__aarch64__)
    flags |= MAP_JIT;
#endif
    void *memory = mmap(NULL, size, prot, flags, -1, 0);
    if (memory == MAP_FAILED) {
        n->error = "can't map memory for machine code";
        return false;
    }

    n->memory = memory;
    n->size = size;
    n->entries = memory;
    n->nentries = c->decls.count;
    u32 *start = (u32 *)(n->memory + code);
    n->code = start;

    if (ir->globals_size) {
        size_t globals_size = ((size_t)ir->globals_size + page - 1) & ~(page - 1);
        void *globals = mmap(NULL, globals_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (globals == MAP_FAILED) {
            n->error = "can't map memory for globals";
            return false;
        }
        n->globals = globals;
        n->globals_size = globals_size;
        if (ir->nglobals) {
            memcpy(n->globals, ir->globals, ir->nglobals);
            ir_relocate(n->globals, ir->global_relocs, n->memory + table);
        }
    }

    u8 *info = malloc(c->types.count);
    for (u32 t = 0; t < c->types.count; t++) {
        info[t] = type_info(c, t);
    }

    jit_write(true);
    if (ir->ndata) {
        memcpy(n->memory + table, ir->data, ir->ndata);
        ir_relocate(n->memory + table, ir->data_relocs, n->memory + table);
    }
    Emit e = { .c = c,
               .ir = &ir->code,
               .fn = &ir->funcs[0],
               .info = info,
               .data = n->memory + table,
               .globals = n->globals,
               .code = start,
               .at = start,
               .regs = malloc(16 * sizeof *e.regs),
               .vinfo = malloc(16 * sizeof *e.vinfo),
               .spills = malloc(16 * sizeof *e.spills),
               .values_cap = 16,
               .slot_regs = malloc(16 * sizeof *e.slot_regs),
               .slot_regs_cap = 16,
               .aliases = malloc(16 * sizeof *e.aliases),
               .alias_at = malloc(16 * sizeof *e.alias_at),
               .spill_free = malloc(16 * sizeof *e.spill_free),
               .spill_free_cap = 16,
               .slot_offsets = malloc(16 * sizeof *e.slot_offsets),
               .slot_offsets_cap = 16,
               .retyped = malloc(16 * sizeof *e.retyped),
               .retyped_cap = 16,
               .place = convention == CONVENTION_APPLE ? PLACE_APPLE : 0,
               .open = malloc(16 * sizeof *e.open),
               .open_cap = 16,
               .fixups = malloc(16 * sizeof *e.fixups),
               .fixups_cap = 16,
               .branches = malloc(16 * sizeof *e.branches),
               .branches_cap = 16,
               .arm_at = malloc(16 * sizeof *e.arm_at),
               .arms_cap = 16,
               .ranges = malloc(16 * sizeof *e.ranges),
               .ranges_cap = 16,
               .homes = malloc(16 * sizeof *e.homes),
               .homes_cap = 16 };
    u64 *adapters = malloc(ir->ncallbacks * sizeof *adapters);
    for (u32 k = 0; k < ir->ncallbacks && !e.error; k++) {
        u32 decl = ir->callbacks[k];
        if (c->decls.bodies[decl] == NO_BODY) {
            continue;
        }
        n->words = (n->words + 3) & ~(size_t)3;
        e.code = e.at = n->code + n->words;
        emit_adapter(&e, decl, n);
        adapters[k] = (u64)(uintptr_t)e.code;
        n->words += here(&e);
        assert(n->words <= words);
    }
    e.adapters = adapters;
    for (u32 f = 0; f < ir->nfuncs && !e.error; f++) {
        const IrFunc *fn = &ir->funcs[f];
        n->words = (n->words + 3) & ~(size_t)3;
        u32 *at = n->code + n->words;
        u32 entry = emit_function(&e, fn, at, n);
        n->entries[fn->decl] = (u64)(uintptr_t)(at + entry);
        n->words += here(&e);
        assert(n->words <= words);
    }
    jit_write(false);

#if defined(__APPLE__) && defined(__aarch64__)
    sys_icache_invalidate(n->code, n->words * sizeof(u32));
#elif defined(__aarch64__)
    __builtin___clear_cache((char *)n->code, (char *)(n->code + n->words));
#endif

    free(info);
    free(e.regs);
    free(e.vinfo);
    free(e.spills);
    free(e.spill_free);
    free(e.homes);
    free(e.slot_offsets);
    free(e.slot_regs);
    free(e.retyped);
    free(e.aliases);
    free(e.alias_at);
    free(e.open);
    free(e.fixups);
    free(e.branches);
    free(e.arm_at);
    free(e.ranges);
    if (e.process) {
        dlclose(e.process);
    }
    n->exports = adapters;
    n->nexports = ir->nexports;
    n->error = e.error;
    return e.error == NULL;
}

void *
native_entry(const Native *n, u32 decl)
{
    return (void *)(uintptr_t)n->entries[decl];
}

void *
native_export(const Native *n, u32 i)
{
    assert(i < n->nexports);
    return (void *)(uintptr_t)n->exports[i];
}

void
native_replace(Native *n, u32 decl, void *entry)
{
    jit_write(true);
    n->entries[decl] = (u64)(uintptr_t)entry;
    jit_write(false);
}

void
native_set_context(Native *n, void *context)
{
    jit_write(true);
    n->entries[n->nentries] = (u64)(uintptr_t)context;
    jit_write(false);
}

void
native_free(Native *n)
{
    if (n->memory) {
        munmap(n->memory, n->size);
    }
    if (n->globals) {
        munmap(n->globals, n->globals_size);
    }
    free(n->exports);
    *n = (Native){ 0 };
}
