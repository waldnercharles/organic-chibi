#define _DEFAULT_SOURCE

#include "arm64.h"

#include <assert.h>
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
    WORDS_PER_FUNCTION = 64,
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
    OP_EOR_ONE = 0x52000000u,
    OP_B = 0x14000000u,
    OP_B_COND = 0x54000000u,
    OP_CBZ = 0x34000000u,
    OP_CBNZ = 0x35000000u,
    OP_BLR_X16 = 0xD63F0200u,
    OP_RET = 0xD65F03C0u,
    OP_BRK = 0xD4200000u,
    OP_NOP = 0xD503201Fu,
    OP_ADRP = 0x90000000u,
    OP_LDR_X16_X16 = 0xF9400210u,
    OP_PUSH_FRAME = 0xA9BF7BFDu,
    OP_SET_FRAME = 0x910003FDu,
    OP_RESET_SP = 0x910003BFu,
    OP_POP_FRAME = 0xA8C17BFDu,
    OP_SUB_SP = 0xD10003FFu,
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
};

typedef struct
{
    u32 at;
    u32 next;
} Fixup;

typedef struct
{
    u32 target;
    u32 start;
    u32 skip;
    u32 ends;
    u8 kind;
    u8 result;
} Open;

typedef struct
{
    const Checker *c;
    const IrCode *ir;
    const IrFunc *fn;
    const u8 *info;
    u32 *code;
    u32 *at;
    u8 *regs;
    u8 *vinfo;
    u32 *spills;
    u32 values_cap;
    u32 holders[64];
    u32 free_int;
    u32 free_float;
    u32 *spill_free;
    u32 nspill_free;
    u32 spill_free_cap;
    u32 spill_top;
    u32 spill_max;
    u32 locals;
    u32 *slot_offsets;
    u32 slot_offsets_cap;
    Open *open;
    u32 nopen;
    u32 open_cap;
    Fixup *fixups;
    u32 nfixups;
    u32 fixups_cap;
    u32 traps[NTRAPS];
    u32 fused;
    u8 fused_cond;
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
    u32 bits = c->types.arg0[type];
    u8 scale = bits == 64 ? 3 : bits == 32 ? 2 : bits == 16 ? 1 : 0;
    bool integer = kind == TYPE_INT;
    return (u8)(scale | (kind == TYPE_FLOAT ? INFO_FLOAT : 0) |
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
patch(Emit *e, u32 list, u32 target)
{
    for (u32 f = list; f; f = e->fixups[f].next) {
        u32 at = e->fixups[f].at;
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
memory(Emit *e, u32 op, u32 scale, u32 rt, u32 offset, u32 address)
{
    if ((offset & ((1u << scale) - 1)) == 0 && (offset >> scale) < 4096) {
        put(e, op | (offset >> scale) << 10 | SP << 5 | rt);
        return;
    }
    if (offset >= (1u << 24)) {
        e->error = "a function's frame is larger than 16 MB";
        return;
    }
    put(e, OP_ADD_FROM_SP | SF | (offset & 0xFFF) << 10 | address);
    if (offset >> 12) {
        put(e, OP_ADD_HIGH | (offset >> 12) << 10 | address << 5 | address);
    }
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
spill_reg(Emit *e, u32 holder_index)
{
    bool fl = holder_index >= 32;
    u32 reg = holder_index & 31;
    u32 v = e->holders[holder_index];
    u32 offset = new_spill(e);
    if (fl) {
        memory(e, OP_STRD, 3, reg, offset, X8);
        e->free_float |= 1u << reg;
    } else {
        memory(e, OP_STRX, 3, reg, offset, X8);
        e->free_int |= 1u << reg;
    }
    e->holders[holder_index] = NO_VALUE;
    e->regs[v] = NO_REG;
    e->spills[v] = offset;
}

static void
spill_all(Emit *e)
{
    for (u32 used = ~e->free_int & INT_POOL; used; used &= used - 1) {
        u32 h = (u32)__builtin_ctz(used);
        if (e->holders[h] != NO_VALUE) {
            spill_reg(e, h);
        }
    }
    for (u32 used = ~e->free_float & FLOAT_POOL; used; used &= used - 1) {
        u32 h = 32 + (u32)__builtin_ctz(used);
        if (e->holders[h] != NO_VALUE) {
            spill_reg(e, h);
        }
    }
}

static u32
take_reg(Emit *e, bool fl)
{
    u32 *free_mask = fl ? &e->free_float : &e->free_int;
    if (*free_mask == 0) {
        u32 oldest = NO_VALUE, at = 0;
        for (u32 h = fl ? 32 : 0, end = h + 32; h < end; h++) {
            if (e->holders[h] < oldest) {
                oldest = e->holders[h];
                at = h;
            }
        }
        if (oldest == NO_VALUE) {
            e->error = "an expression nests `and` and `or` too deeply";
            return fl ? V29 : X8;
        }
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
    e->holders[h] = NO_VALUE;
    if (h >= 32) {
        e->free_float |= 1u << (h - 32);
    } else {
        e->free_int |= 1u << h;
    }
    e->regs[x] = NO_REG;
}

static bool
small_constant(const Emit *e, u32 x)
{
    return (x & IR_CONST) && !is_float(operand_info(e, x)) &&
           constant_bits(e, x) < 4096;
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

static void
check_small(Emit *e, u8 t, u32 d)
{
    put(e, extend_op(t) | d << 5 | X8);
    put(e, OP_SUBS | d << 16 | X8 << 5 | ZR);
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
        u32 d = alloc(e, i, t);
        put(e, (sub ? OP_FSUB : OP_FADD) | precision(t) | rb << 16 | ra << 5 | d);
        return;
    }

    bool checked = kind == IR_ADD || kind == IR_SUB;
    bool small = is_small(t);
    bool imm = small_constant(e, b);
    u32 sf = width(t);
    u32 ra = fetch(e, a, X16);
    u32 rb = imm ? 0 : fetch(e, b, X17);
    release(e, a);
    release(e, b);
    u32 d = alloc(e, i, t);
    bool flags = checked && !small;
    u32 op = imm ? (sub ? (flags ? OP_SUBS_IMM : OP_SUB_IMM)
                        : (flags ? OP_ADDS_IMM : OP_ADD_IMM))
                 : (sub ? (flags ? OP_SUBS : OP_SUB) : (flags ? OP_ADDS : OP_ADD));
    u32 operand = imm ? (u32)constant_bits(e, b) << 10 : rb << 16;
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
        u32 d = alloc(e, i, t);
        put(e, OP_FMUL | precision(t) | rb << 16 | ra << 5 | d);
        return;
    }

    u32 ra = fetch(e, a, X16), rb = fetch(e, b, X17);
    release(e, a);
    release(e, b);
    u32 d = alloc(e, i, t);
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

static void
divide(Emit *e, u32 i, u8 kind, u32 type, u32 a, u32 b)
{
    u8 t = e->info[type];
    if (is_float(t)) {
        u32 ra = fetch(e, a, V30), rb = fetch(e, b, V31);
        release(e, a);
        release(e, b);
        u32 d = alloc(e, i, t);
        put(e, OP_FDIV | precision(t) | rb << 16 | ra << 5 | d);
        return;
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
    u32 d = alloc(e, i, t);
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
    u32 ra = fetch(e, a, X16), rb = fetch(e, b, X17);
    release(e, a);
    release(e, b);
    u32 d = alloc(e, i, t);
    u32 op = kind == IR_AND ? OP_AND : kind == IR_OR ? OP_ORR : OP_EOR;
    put(e, op | width(t) | rb << 16 | ra << 5 | d);
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
        u32 d = alloc(e, i, t);
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
    u32 d = alloc(e, i, t);
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
    if (is_float(t)) {
        u32 ra = fetch(e, a, V30), rb = fetch(e, b, V31);
        release(e, a);
        release(e, b);
        put(e, OP_FCMP | precision(t) | rb << 16 | ra << 5);
    } else {
        bool imm = small_constant(e, b);
        u32 ra = fetch(e, a, X16);
        u32 rb = imm ? 0 : fetch(e, b, X17);
        release(e, a);
        release(e, b);
        u32 sf = width(t);
        if (imm) {
            put(e, OP_SUBS_IMM | sf | (u32)constant_bits(e, b) << 10 | ra << 5 | ZR);
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
    u32 d = alloc(e, i, e->info[TYPE_ID_BOOL]);
    put(e, OP_CSET | (u32)(cond ^ 1) << 12 | d);
}

static void
unary(Emit *e, u32 i, u8 kind, u32 type, u32 a)
{
    u8 t = e->info[type];
    if (is_float(t)) {
        u32 ra = fetch(e, a, V30);
        release(e, a);
        u32 d = alloc(e, i, t);
        put(e, OP_FNEG | precision(t) | ra << 5 | d);
        return;
    }

    u32 ra = fetch(e, a, X16);
    release(e, a);
    u32 d = alloc(e, i, t);
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
        u32 d = alloc(e, i, tt);
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
        u32 d = alloc(e, i, tt);
        u32 op = is_signed(from) ? OP_SCVTF : OP_UCVTF;
        put(e, op | width(from) | precision(tt) | ra << 5 | d);
    } else {
        assert(float_to);
        u32 ra = fetch(e, a, V30);
        release(e, a);
        u32 d = alloc(e, i, tt);
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
        u32 d = alloc(e, i, tt);
        put(e, (to_signed ? OP_FCVTZS : OP_FCVTZU) | width(tt) | fp | ra << 5 | d);
        return;
    }

    u32 ra = fetch(e, a, X16);
    release(e, a);
    u32 sf = width(from);
    u32 from_bits = bits_of(from);
    bool from_signed = is_signed(from);
    if (from_signed) {
        int64_t low = to_signed ? -(int64_t)(1ull << (to_bits - 1)) : 0;
        int64_t from_low = from_bits == 64 ? INT64_MIN
                                           : -(int64_t)(1ull << (from_bits - 1));
        if (low > from_low) {
            if (low == 0) {
                put(e, OP_SUBS_IMM | sf | ra << 5 | ZR);
            } else {
                mov_imm(e, X8, (u64)low, sf);
                put(e, OP_SUBS | sf | X8 << 16 | ra << 5 | ZR);
            }
            trap_if(e, COND_LT, TRAP_RANGE);
        }
    }
    u64 high = to_signed ? (1ull << (to_bits - 1)) - 1
                         : (to_bits == 64 ? ~0ull : (1ull << to_bits) - 1);
    u64 from_high = from_signed
                      ? (1ull << (from_bits - 1)) - 1
                      : (from_bits == 64 ? ~0ull : (1ull << from_bits) - 1);
    if (high < from_high) {
        if (high < 4096) {
            put(e, OP_SUBS_IMM | sf | (u32)high << 10 | ra << 5 | ZR);
        } else {
            mov_imm(e, X8, high, sf);
            put(e, OP_SUBS | sf | X8 << 16 | ra << 5 | ZR);
        }
        trap_if(e, from_signed ? COND_GT : COND_HI, TRAP_RANGE);
    }
    u32 d = alloc(e, i, tt);
    mov(e, width(tt), d, ra);
}

static void
truncate_to(Emit *e, u32 i, u32 to, u32 a)
{
    u8 tt = e->info[to];
    u32 ra = fetch(e, a, X16);
    release(e, a);
    u32 d = alloc(e, i, tt);
    if (is_small(tt)) {
        put(e, extend_op(tt) | ra << 5 | d);
    } else {
        put(e, OP_ORR | width(tt) | ra << 16 | ZR << 5 | d);
    }
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
call(Emit *e, u32 i, u32 type, u32 decl, u32 b, const Native *n)
{
    u8 t = e->info[type];
    const u32 *args = e->ir->extra + e->fn->first_extra + b;
    u32 ints = 0, floats = 0;
    for (u32 j = 1; j <= args[0]; j++) {
        bool fl = is_float(operand_info(e, args[j]));
        u32 d = fl ? floats++ : ints++;
        if (d >= 8) {
            e->error = "a call passes more than 8 integer or 8 float arguments";
            return;
        }
        move_into(e, d, args[j], fl);
    }
    spill_all(e);

    u64 pc = (u64)(uintptr_t)(e->code + here(e));
    u64 entry = (u64)(uintptr_t)(n->entries + decl);
    int64_t pages = (int64_t)((entry & ~0xFFFull) - (pc & ~0xFFFull)) >> 12;
    put(e, OP_ADRP | ((u32)pages & 3) << 29 | (((u32)pages >> 2) & 0x7FFFFu) << 5 | X16);
    put(e, OP_LDR_X16_X16 | (u32)((entry & 0xFFF) >> 3) << 10);
    put(e, OP_BLR_X16);

    if (type != TYPE_ID_VOID) {
        bool fl = is_float(t);
        u32 d = alloc(e, i, e->info[type]);
        if (fl) {
            fmov(e, d, 0);
        } else {
            mov(e, SF, d, 0);
        }
    }
}

static void
epilogue(Emit *e)
{
    put(e, OP_RESET_SP);
    put(e, OP_POP_FRAME);
    put(e, OP_RET);
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

static void
lay_out_frame(Emit *e)
{
    const IrFunc *fn = e->fn;
    const IrSlot *slots = e->ir->slots + fn->first_slot;
    e->slot_offsets = grow(
        e->slot_offsets,
        &e->slot_offsets_cap,
        fn->nslots,
        sizeof *e->slot_offsets
    );
    u32 counts[4] = { 0 };
    for (u32 s = 0; s < fn->nslots; s++) {
        counts[e->info[slots[s].type] & INFO_SCALE]++;
    }
    u32 bases[4];
    u32 offset = 0;
    for (u32 k = 0; k < 4; k++) {
        offset = (offset + (1u << k) - 1) & ~((1u << k) - 1);
        bases[k] = offset;
        offset += counts[k] << k;
    }
    for (u32 s = 0; s < fn->nslots; s++) {
        u32 k = e->info[slots[s].type] & INFO_SCALE;
        e->slot_offsets[s] = bases[k];
        bases[k] += 1u << k;
    }
    e->locals = (offset + 7) & ~7u;
}

static void
emit_function(Emit *e, const IrFunc *fn, u32 *code, const Native *n)
{
    const IrCode *ir = e->ir;
    e->fn = fn;
    e->code = code;
    e->at = code;
    e->nopen = 0;
    e->nfixups = 1;
    e->nspill_free = 0;
    e->spill_top = 0;
    e->spill_max = 0;
    e->free_int = INT_POOL;
    e->free_float = FLOAT_POOL;
    e->fused = NO_VALUE;
    memset(e->traps, 0, sizeof e->traps);
    memset(e->holders, 0xFF, sizeof e->holders);
    if (fn->count > e->values_cap) {
        e->values_cap = fn->count * 2;
        e->regs = realloc(e->regs, e->values_cap * sizeof *e->regs);
        e->spills = realloc(e->spills, e->values_cap * sizeof *e->spills);
        e->vinfo = realloc(e->vinfo, e->values_cap * sizeof *e->vinfo);
    }
    memset(e->regs, NO_REG, fn->count);
    lay_out_frame(e);

    put(e, OP_PUSH_FRAME);
    put(e, OP_SET_FRAME);
    put(e, OP_NOP);
    put(e, OP_NOP);

    u32 ints = 0, floats = 0;
    for (u32 p = 0; p < fn->nparams; p++) {
        u32 type = slot_type(e, p);
        bool fl = is_float(e->info[type]);
        u32 r = fl ? floats++ : ints++;
        if (r >= 8) {
            e->error = "a function has more than 8 integer or 8 float "
                       "parameters";
            return;
        }
        store_typed(e, type, r, e->slot_offsets[p]);
    }

    for (u32 i = 0; i < fn->count && !e->error; i++) {
        u32 at = fn->first + i;
        u8 kind = ir->kinds[at];
        u32 type = ir->types[at];
        u32 a = ir->as[at];
        u32 b = ir->bs[at];
        switch (kind) {
            case IR_LOAD: {
                u32 st = slot_type(e, a);
                u32 d = alloc(e, i, e->info[st]);
                load_typed(e, st, d, e->slot_offsets[a]);
                break;
            }
            case IR_STORE: {
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
            case IR_CALL: call(e, i, type, a, b, n); break;
            case IR_DROP: release(e, a); break;
            case IR_RET:
                if (type != TYPE_ID_VOID) {
                    move_into(e, 0, a, is_float(e->info[type]));
                }
                epilogue(e);
                break;
            case IR_BLOCK:
            case IR_LOOP:
            case IR_IF: {
                e->open = grow(e->open, &e->open_cap, e->nopen + 1, sizeof *e->open);
                Open o = { .target = i, .start = here(e), .kind = kind, .result = NO_REG };
                if (kind == IR_IF) {
                    bool typed = type != TYPE_ID_NONE;
                    if (a & IR_CONST) {
                        if (constant_bits(e, a) == 0) {
                            add_fixup(e, &o.skip);
                            put(e, OP_B);
                        }
                    } else if (e->fused == a) {
                        if (typed) {
                            spill_all(e);
                        }
                        add_fixup(e, &o.skip);
                        put(e, OP_B_COND | (u32)(e->fused_cond ^ 1));
                        e->fused = NO_VALUE;
                    } else {
                        u32 r = fetch(e, a, X16);
                        release(e, a);
                        if (typed) {
                            spill_all(e);
                        }
                        add_fixup(e, &o.skip);
                        put(e, OP_CBZ | r);
                    }
                    if (typed) {
                        o.result = (u8)take_reg(e, is_float(e->info[type]));
                    }
                }
                e->open[e->nopen++] = o;
                break;
            }
            case IR_ELSE: {
                Open *o = &e->open[e->nopen - 1];
                if (type != TYPE_ID_NONE) {
                    move_into(e, o->result, a, is_float(e->info[type]));
                }
                add_fixup(e, &o->ends);
                put(e, OP_B);
                patch(e, o->skip, here(e));
                o->skip = 0;
                break;
            }
            case IR_END: {
                Open o = e->open[--e->nopen];
                if (o.kind == IR_IF && type != TYPE_ID_NONE) {
                    bool fl = is_float(e->info[type]);
                    move_into(e, o.result, a, fl);
                    u32 h = (fl ? 32 : 0) + o.result;
                    e->holders[h] = i;
                    e->regs[i] = (u8)h;
                    e->vinfo[i] = e->info[type];
                }
                patch(e, o.skip, here(e));
                patch(e, o.ends, here(e));
                break;
            }
            case IR_EXIT: {
                Open *o = find_open(e, a);
                add_fixup(e, &o->ends);
                put(e, OP_B);
                break;
            }
            case IR_REPEAT: {
                Open *o = find_open(e, a);
                put(e, OP_B | ((o->start - here(e)) & 0x03FFFFFFu));
                break;
            }
            default: assert(false && "unknown IR instruction");
        }
    }

    if (fn->count == 0 || ir->kinds[fn->first + fn->count - 1] != IR_RET) {
        add_fixup(e, &e->traps[TRAP_UNREACHABLE]);
        put(e, OP_B);
    }
    for (u32 k = 1; k < NTRAPS; k++) {
        if (e->traps[k]) {
            patch(e, e->traps[k], here(e));
            put(e, OP_BRK | k << 5);
        }
    }

    u32 frame = (e->locals + e->spill_max + 15) & ~15u;
    if (frame >= (1u << 24)) {
        e->error = "a function's frame is larger than 16 MB";
    } else if (frame) {
        code[2] = OP_SUB_SP | (frame & 0xFFF) << 10;
        if (frame >> 12) {
            code[3] = OP_SUB_SP_HIGH | (frame >> 12) << 10;
        }
    }
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
arm64_compile(Native *n, const Checker *c)
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
    size_t size = table + ((words * sizeof(u32) + page - 1) & ~(page - 1));
    if (size == table) {
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
    n->code = (u32 *)(n->memory + table);

    u8 *info = malloc(c->types.count);
    for (u32 t = 0; t < c->types.count; t++) {
        info[t] = type_info(c, t);
    }

    jit_write(true);
    Emit e = { .c = c, .ir = &ir->code, .info = info };
    for (u32 f = 0; f < ir->nfuncs && !e.error; f++) {
        const IrFunc *fn = &ir->funcs[f];
        u32 *at = n->code + n->words;
        n->entries[fn->decl] = (u64)(uintptr_t)at;
        emit_function(&e, fn, at, n);
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
    free(e.open);
    free(e.fixups);
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
    *n = (Native){ 0 };
}
