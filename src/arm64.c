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
    u32 fused;
    u8 fused_cond;
    u32 tested;
    const char *error;
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

static u8
type_info(const Checker *c, u32 type)
{
    u8 kind = c->types.kinds[type];
    if (kind == TYPE_ENUM) {
        u32 backing = (u32)c->types.arg1[type];
        return backing ? type_info(c, backing) : INFO_UNSUPPORTED;
    }
    if (kind == TYPE_POINTER) {
        return 3;
    }
    if (kind == TYPE_SLICE) {
        return 3 | INFO_PAIR;
    }
    if (kind == TYPE_ARRAY || kind == TYPE_STRUCT) {
        return INFO_MEMORY;
    }
    u32 bits = c->types.arg0[type];
    u8 scale = bits == 64 ? 3 : bits == 32 ? 2 : bits == 16 ? 1 : 0;
    bool integer = kind == TYPE_INT;
    bool supported = kind == TYPE_NONE || kind == TYPE_VOID || kind == TYPE_BOOL ||
                     integer || kind == TYPE_UINTPTR || kind == TYPE_FLOAT;
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
    if ((word & 0xFC000000u) == OP_B) {
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
        if (t & INFO_MEMORY) {
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

static void
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
    if (kind == IR_MUL_WRAP || is_small(t)) {
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
    u32 rb = fetch(e, b, X17);
    release(e, a);
    release(e, b);
    u32 d = result(e, i, t);
    put(e, OP_SUBS_IMM | width(count_info) | bits_of(t) << 10 | rb << 5 | ZR);
    trap_if(e, COND_HS, TRAP_SHIFT);
    u32 op = kind == IR_SHL ? OP_LSLV : s ? OP_ASRV : OP_LSRV;
    put(e, op | sf | rb << 16 | ra << 5 | d);
    if (kind == IR_SHL && is_small(t)) {
        normalize(e, t, d, d);
    }
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

    u8 cond = compare_cond(e, kind, type);
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

static void
convert(Emit *e, u32 i, u32 to, u32 a)
{
    u8 tt = e->info[to];
    u8 from = operand_info(e, a);
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

static double
power_of_two(u32 k)
{
    return k == 64 ? 0x1p64 : (double)(1ull << k);
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
        u32 fp = precision(from);
        u32 mantissa = bits_of(from) == 64 ? 53 : 24;
        float_bound(e, V29, from, power_of_two(to_signed ? to_bits - 1 : to_bits));
        put(e, OP_FCMP | fp | V29 << 16 | ra << 5);
        trap_if(e, COND_PL, TRAP_RANGE);
        if (!to_signed) {
            float_bound(e, V29, from, -1.0);
            put(e, OP_FCMP | fp | V29 << 16 | ra << 5);
            trap_if(e, COND_LE, TRAP_RANGE);
        } else if (to_bits - 1 < mantissa) {
            float_bound(e, V29, from, -power_of_two(to_bits - 1) - 1.0);
            put(e, OP_FCMP | fp | V29 << 16 | ra << 5);
            trap_if(e, COND_LE, TRAP_RANGE);
        } else {
            float_bound(e, V29, from, -power_of_two(to_bits - 1));
            put(e, OP_FCMP | fp | V29 << 16 | ra << 5);
            trap_if(e, COND_LT, TRAP_RANGE);
        }
        u32 d = result(e, i, tt);
        put(e, (to_signed ? OP_FCVTZS : OP_FCVTZU) | width(tt) | fp | ra << 5 | d);
        return;
    }

    u32 ra = fetch(e, a, X16);
    release(e, a);
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
    u32 ra = fetch(e, a, X16);
    release(e, a);
    u32 d = result(e, i, tt);
    if (is_small(tt)) {
        put(e, extend_op(tt) | ra << 5 | d);
    } else {
        put(e, OP_ORR | width(tt) | ra << 16 | ZR << 5 | d);
    }
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

static bool
consumed_next(const Emit *e, u32 v)
{
    if (v + 1 >= e->fn->count) {
        return false;
    }
    u8 kind = e->ir->kinds[e->fn->first + v + 1];
    return kind == IR_STORE || kind == IR_RET || kind == IR_CALL || kind == IR_CALL_C;
}

static u32
offset_address(Emit *e, u32 i, u8 info, u32 a, u64 offset)
{
    if (offset == 0 && !(a & IR_CONST) && e->regs[a] != NO_REG &&
        !consumed_next(e, i)) {
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
                assert(args[j] == i || !(operand_info(e, args[j]) & INFO_MEMORY));
                same += args[j] == i || !is_float(operand_info(e, args[j]));
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
            pass_struct(e, args[j], &at);
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
point_result(Emit *e, u32 type, u32 slot)
{
    if (by_pointer(e, type)) {
        sp_address(e, X8, e->slot_offsets[slot]);
    }
}

static NOINLINE void
receive_result(Emit *e, u32 type, u32 slot)
{
    if (!by_pointer(e, type)) {
        move_struct(e, false, type, 0, SP, e->slot_offsets[slot]);
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
    char symbol[256];
    if (name.len >= sizeof symbol) {
        e->error = "the name of an `extern fn` is longer than 255 bytes";
        return false;
    }
    memcpy(symbol, name.text, name.len);
    symbol[name.len] = 0;

    if (!e->process) {
        e->process = dlopen(NULL, RTLD_LAZY);
    }
    void *address = dlsym(e->process, symbol);
    if (!address) {
        e->error = "an `extern fn` names a C function that is not loaded";
        return false;
    }
    n->entries[decl] = (u64)(uintptr_t)address;
    return true;
}

static void
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
        point_result(e, type, args[args[0] + 1]);
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

    if (t & INFO_PAIR) {
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
    } else if (t & INFO_MEMORY) {
        receive_result(e, type, args[args[0] + 1]);
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
                                       "arrays and slices so far";

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
skip_struct(const Emit *e, u32 type, u32 *used)
{
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
                skip_struct(e, slots[p].type, used);
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
        if (info & INFO_UNSUPPORTED) {
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

    for (u32 align = 8; memory && align > 0; align /= 2) {
        offset = (offset + align - 1) & ~(align - 1);
        for (u32 s = 0; s < fn->nslots; s++) {
            u32 type = slots[s].type;
            if (!(e->info[type] & INFO_MEMORY) || !slots[s].weight ||
                check_type_align(e->c, type) != align) {
                continue;
            }
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
receive_param(Emit *e, u32 p, ArgCursor *at)
{
    const IrSlot *slot = e->ir->slots + e->fn->first_slot + p;
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

static u32
emit_function(Emit *e, const IrFunc *fn, u32 *code, const Native *n)
{
    const IrCode *ir = e->ir;
    e->fn = fn;
    e->code = code;
    e->nopen = 0;
    e->nfixups = 1;
    e->nspill_free = 0;
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

    for (u32 i = 0; i < fn->count && !e->error; i++) {
        u32 at = fn->first + i;
        u8 kind = ir->kinds[at];
        u32 type = ir->types[at];
        u32 a = ir->as[at];
        u32 b = ir->bs[at];
        if (e->info[type] & INFO_UNSUPPORTED) {
            e->error = UNSUPPORTED_TYPE;
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
            case IR_MUL_WRAP: multiply(e, i, kind, type, a, b); break;
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
            case IR_EQ:
            case IR_NE:
            case IR_LT:
            case IR_LE:
            case IR_GT:
            case IR_GE: compare(e, i, kind, type, a, b); break;
            case IR_CONVERT: convert(e, i, type, a); break;
            case IR_NARROW: narrow(e, i, type, a); break;
            case IR_TRUNCATE: truncate_to(e, i, type, a); break;
            case IR_CALL:
            case IR_CALL_C:
                call(e, i, type, a, b, n, kind == IR_CALL ? FIRST_INT_ARG : 0);
                break;
            case IR_DROP: release(e, a); break;
            case IR_RET:
                if (e->info[type] & INFO_PAIR) {
                    return_pair(e, a, b);
                } else if (e->info[type] & INFO_MEMORY) {
                    return_struct(e, type, a);
                } else if (type != TYPE_ID_VOID) {
                    move_into(e, 0, a, is_float(e->info[type]));
                }
                if (i + 1 < fn->count) {
                    add_fixup(e, &e->epilogue);
                    put(e, OP_B);
                }
                break;
            case IR_BLOCK:
            case IR_LOOP:
            case IR_IF: {
                e->open = grow(e->open, &e->open_cap, e->nopen + 1, sizeof *e->open);
                if (kind == IR_IF && (a & IR_CONST)) {
                    assert(type == TYPE_ID_NONE);
                    if (constant_bits(e, a)) {
                        e->open[e->nopen++] = (Open){ .target = i,
                                                      .kind = IR_IF,
                                                      .dead_else = true };
                        break;
                    }
                    i = arm_end(e, i);
                    if (ir->kinds[fn->first + i] == IR_ELSE) {
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
                bool falls = i + 1 < fn->count &&
                             ir->kinds[fn->first + i + 1] == IR_END &&
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

    if (fn->count == 0 || ir->kinds[fn->first + fn->count - 1] != IR_RET) {
        add_fixup(e, &e->traps[TRAP_UNREACHABLE]);
        put(e, OP_B);
    }

    u32 saves[PROLOGUE_WORDS];
    u32 nsaves = callee_words(saves, false, e->saved_int, e->saved_float);
    u32 frame = (16 * nsaves + e->locals + e->spill_max + 15) & ~15u;
    bool record = fn->calls || frame || e->wide_record;
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

    for (u32 k = 1; k < NTRAPS; k++) {
        if (e->traps[k]) {
            patch(e, e->traps[k], here(e));
            put(e, OP_BRK | k << 5);
        }
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
    size_t table = ((size_t)c->decls.count * sizeof(u64) + page - 1) & ~(page - 1);
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
    n->code = (u32 *)(n->memory + code);

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
        }
    }

    u8 *info = malloc(c->types.count);
    for (u32 t = 0; t < c->types.count; t++) {
        info[t] = type_info(c, t);
    }

    jit_write(true);
    if (ir->ndata) {
        memcpy(n->memory + table, ir->data, ir->ndata);
    }
    Emit e = { .c = c,
               .ir = &ir->code,
               .info = info,
               .data = n->memory + table,
               .globals = n->globals,
               .place = convention == CONVENTION_APPLE ? PLACE_APPLE : 0 };
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
    n->error = e.error;
    return e.error == NULL;
}

void *
native_entry(const Native *n, u32 decl)
{
    return (void *)(uintptr_t)n->entries[decl];
}

void
native_replace(Native *n, u32 decl, void *entry)
{
    jit_write(true);
    n->entries[decl] = (u64)(uintptr_t)entry;
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
    *n = (Native){ 0 };
}
