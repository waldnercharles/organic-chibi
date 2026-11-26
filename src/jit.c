#define _DEFAULT_SOURCE

#include "arm64.h"

#include <assert.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>

enum
{
    NO_REG = 0xFF,
    NO_VALUE = 0xFFFFFFFFu,
    ALIAS = 64,
};

#include "jit/arm64_target.h"

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
    INFO_VECTOR64 = INFO_VECTOR | INFO_SMALL,
};

typedef struct
{
    u32 at;
    u32 next;
} Fixup;

typedef struct
{
    Code *code;
    Code *at;
    Fixup *fixups;
    u32 nfixups;
    u32 fixups_cap;
    const char *error;
} Assembler;

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
    u8 result2;
    bool dead_else;
} Open;

typedef struct
{
    Assembler as;
    const Checker *c;
    const IrCode *ir;
    const IrFunc *fn;
    const u8 *info;
    const u8 *data;
    u8 *globals;
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
    u32 framed;
    u32 framed_offset;
    u32 tested;
    const u64 *adapters;
    u32 *homes;
    u32 nhomes;
    u32 homes_cap;
    u32 *homes64;
    u32 nhomes64;
    u32 homes64_cap;
    u32 transfer64;
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

static u32
here(const Assembler *as)
{
    return (u32)(as->at - as->code);
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

static bool
is_vector64(u8 info)
{
    return (info & INFO_VECTOR64) == INFO_VECTOR64;
}

static u8
type_info(const Checker *c, u32 type)
{
    u8 kind = c->types.kinds[type];
    if (kind == TYPE_ENUM) {
        u32 backing = (u32)c->types.arg1[type];
        return backing ? type_info(c, backing) : INFO_UNSUPPORTED;
    }
    if (check_is_flag_optional(c, type)) {
        return type_info(c, c->types.arg0[type]);
    }
    if (kind == TYPE_ERROR_UNION) {
        return type_info(c, check_error_union_first(c, type));
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
        return (check_type_size(c, type) == 64 ? INFO_VECTOR64 : INFO_VECTOR) |
               (type_info(c, c->types.arg0[type]) & (INFO_SCALE | INFO_SIGNED));
    }
    u32 bits = c->types.arg0[type];
    u8 scale = bits == 64 ? 3 : bits == 32 ? 2 : bits == 16 ? 1 : 0;
    bool integer = kind == TYPE_INT || kind == TYPE_ERROR;
    if (integer && bits == 128) {
        return INFO_WIDE | (c->types.arg1[type] ? INFO_SIGNED : 0);
    }
    bool supported = kind == TYPE_NONE || kind == TYPE_VOID ||
                     kind == TYPE_BOOL || (integer && bits <= 64) ||
                     kind == TYPE_UINTPTR || kind == TYPE_FLOAT;
    return (u8)(scale | (kind == TYPE_FLOAT ? INFO_FLOAT : 0) |
                (supported ? 0 : INFO_UNSUPPORTED) |
                (kind == TYPE_INT && c->types.arg1[type] ? INFO_SIGNED : 0) |
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
add_fixup(Assembler *as, u32 *list)
{
    as->fixups = grow(as->fixups, &as->fixups_cap, as->nfixups + 1, sizeof *as->fixups);
    as->fixups[as->nfixups] = (Fixup){ here(as), *list };
    *list = as->nfixups++;
}

static u32
slot_type(const Emit *e, u32 slot)
{
    return e->ir->slots[e->fn->first_slot + slot].type;
}

static const char UNSUPPORTED_TYPE[] = "the arm64 backend supports only "
                                       "integers, floats, bool, pointers, "
                                       "arrays, slices and vectors so far";

#include "jit/arm64_encode.h"

static void
patch(Assembler *as, u32 list, u32 target)
{
    for (u32 f = list; f; f = as->fixups[f].next) {
        patch_one(as, as->fixups[f].at, target);
    }
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
    store_spill(&e->as, h, offset);
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
        store_spill(&e->as, h, offset);
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

static u32
fetch(Emit *e, u32 x, u32 scratch)
{
    if (x & IR_CONST) {
        load_immediate(&e->as, scratch, constant_bits(e, x), operand_info(e, x));
        return scratch;
    }
    u8 h = e->regs[x];
    if (h != NO_REG) {
        return h & 31;
    }
    load_spill(&e->as, scratch, e->spills[x], is_float(e->vinfo[x]));
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
        move_reg(&e->as, to, reg, fl);
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
    move_reg(&e->as, r, h & 31, h >= 32);
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

static bool
is_pair(const Emit *e, u32 type)
{
    return (e->info[type] & INFO_PAIR) || check_is_flag_optional(e->c, type) ||
           check_is_error_pair(e->c, type);
}

static u8
result_reg(Emit *e, u32 v, bool fl)
{
    if (!(v & IR_CONST) && in_pool(e->regs[v])) {
        return e->regs[v] & 31u;
    }
    return (u8)take_reg(e, fl);
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

static u32
frame_align(const Checker *c, u32 type)
{
    bool lanes = c->types.kinds[type] == TYPE_ARRAY &&
                 c->types.kinds[c->types.arg0[type]] == TYPE_INT &&
                 (check_type_size(c, type) == 16 || check_type_size(c, type) == 64);
    return lanes ? 16 : check_type_align(c, type);
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

#include "jit/arm64_select.h"

static void
start_arm(Emit *e, u32 i)
{
    Open *o = &e->open[e->nopen - 1];
    u8 before = e->ir->kinds[e->fn->first + i - 1];
    if (before != IR_RET && before != IR_EXIT && before != IR_REPEAT) {
        jump_later(&e->as, &o->ends);
    }
    e->arm_at = grow(e->arm_at, &e->arms_cap, e->narms + 1, sizeof *e->arm_at);
    e->arm_at[e->narms++] = here(&e->as);
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

static NOINLINE void
hand_over_pair(Emit *e, Open *o, u32 type, u32 a, u32 b)
{
    bool fl = is_float(e->info[type]);
    o->result = result_reg(e, a, fl);
    o->result2 = result_reg(e, b, false);
    assert(fl || o->result != o->result2);

    move_halves(e, type, o->result, o->result2, a, b);
    *(fl ? &e->free_float : &e->free_int) |= 1u << o->result;
    e->free_int |= 1u << o->result2;
}

static NOINLINE void
end_pair(Emit *e, const Open *o, u32 i, u32 type, u32 a, u32 b)
{
    move_halves(e, type, o->result, o->result2, a, b);

    u32 r = o->result2;
    assert(e->free_int & (1u << r));
    e->free_int &= ~(1u << r);
    e->holders[r] = i + 1;
    e->regs[i + 1] = (u8)r;
    e->vinfo[i + 1] = e->info[e->ir->types[e->fn->first + i + 1]];
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
        load_immediate(&e->as, r, constant_bits(e, b), t);
    } else if (e->regs[b] == ALIAS + h) {
        release(e, b);
    } else {
        move_into(e, r, b, fl);
    }
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
            e->as.error = UNSUPPORTED_TYPE;
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
                frame_align(e->c, type) != align) {
                continue;
            }
            offset = (offset + align - 1) & ~(align - 1);
            u64 size = check_type_size(e->c, type);
            if (size >= FRAME_LIMIT - offset) {
                e->as.error = "a function's frame is larger than 16 MB";
                return;
            }
            e->slot_offsets[s] = offset;
            offset += (u32)size;
            memory--;
        }
    }

    offset = (offset + 7) & ~7u;
    if (fn->copies >= FRAME_LIMIT - offset) {
        e->as.error = "a function's frame is larger than 16 MB";
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
emit_function(Emit *e, const IrFunc *fn, Code *code, const Native *n)
{
    const IrCode *ir = e->ir;
    e->fn = fn;
    e->as.code = code;
    e->nopen = 0;
    e->as.nfixups = 1;
    e->nspill_free = 0;
    e->nhomes = 0;
    e->nhomes64 = 0;
    e->transfer64 = NO_VALUE;
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
    e->framed = NO_VALUE;
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
    begin_function(e, code);

    u32 i = 0;
    u32 count = fn->count;
    u32 first = fn->first;
    while (i < count && !e->as.error) {
        for (; i < count && !e->as.error; i++) {
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
                    load_typed(&e->as, e->info[st], d, e->slot_offsets[a]);
                    break;
                }
                case IR_STORE: {
                    u8 h = e->slot_regs[a];
                    if (h != NO_REG) {
                        store_pinned(e, h, type, b);
                        break;
                    }
                    store_local(e, type, a, b);
                    break;
                }
                case IR_ADDR:
                    if (a < 32 ? e->by_ref >> a & 1
                               : a < fn->nparams &&
                                     e->slots[a].type != slot_type(e, a)) {
                        param_address(e, i, a, e->info[type]);
                        break;
                    }
                    if (frames_lanes(e, i)) {
                        e->framed = i;
                        e->framed_offset = e->slot_offsets[a];
                        break;
                    }
                    sp_address(&e->as, result(e, i, e->info[type]), e->slot_offsets[a]);
                    break;
                case IR_DATA: data_address(e, i, type, a); break;
                case IR_GLOBAL: global_address(e, i, type, a); break;
                case IR_C_GLOBAL: c_global_address(e, i, type, b, n); break;
                case IR_CALLBACK: callback_address(e, i, type, a, b, n); break;
                case IR_LOAD_AT: load_at(e, i, type, a); break;
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
                case IR_STORE_AT: store_at(e, type, a, b); break;
                case IR_STORE_PAIR:
                    if (check_is_flag_optional(e->c, type)) {
                        store_flagged(e, type, a, b);
                        break;
                    }
                    store_pair(e, a, b);
                    break;
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
                    return_value(e, type, a, b);
                    if (i + 1 < count) {
                        jump_later(&e->as, &e->epilogue);
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
                        u32 r = fetch(e, a, SCRATCH);
                        release(e, a);
                        spill_all(e);
                        Open o = { .target = i, .kind = kind, .result = NO_REG };
                        skip_if_zero(&e->as, &o.skip, r);
                        e->open[e->nopen++] = o;
                        break;
                    }

                    spill_all(e);
                    if (kind == IR_LOOP) {
                        align_loop(&e->as);
                    }
                    Open o = { .target = i,
                               .start = here(&e->as),
                               .kind = kind,
                               .result = NO_REG };
                    if (kind == IR_IF) {
                        skip_unless_flags(&e->as, &o.skip, e->fused_cond);
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
                    if (type != TYPE_ID_NONE && is_pair(e, type)) {
                        hand_over_pair(e, o, type, a, b);
                    } else if (type != TYPE_ID_NONE) {
                        hand_over(e, o, type, a);
                    }
                    jump_later(&e->as, &o->ends);
                    patch(&e->as, o->skip, here(&e->as));
                    o->skip = 0;
                    break;
                }
                case IR_END: {
                    Open o = e->open[--e->nopen];
                    if (type != TYPE_ID_NONE) {
                        bool fl = is_float(e->info[type]);
                        if (o.kind == IR_IF && is_pair(e, type)) {
                            end_pair(e, &o, i, type, a, b);
                        } else if (o.kind == IR_IF) {
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
                    patch(&e->as, o.skip, here(&e->as));
                    patch(&e->as, o.ends, here(&e->as));
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
                        jump_later(&e->as, &o->ends);
                    }
                    break;
                }
                case IR_REPEAT: {
                    jump_back(&e->as, find_open(e, a)->start);
                    break;
                }
                case IR_NOP: break;
                case IR_SWITCH: open_switch(e, i, type, a, b); break;
                case IR_CASE: start_arm(e, i); break;
                default: assert(false && "unknown IR instruction");
            }
        }
        if (i < count && !e->as.error) {
            u32 at = first + i;
            if (!is_vector(e->info[ir->types[at]])) {
                e->as.error = UNSUPPORTED_TYPE;
                break;
            }
            lanes(e, i, ir->kinds[at], ir->types[at], ir->as[at], ir->bs[at], n);
            i++;
        }
    }

    if (fn->count == 0 || ir->kinds[fn->first + fn->count - 1] != IR_RET) {
        jump_later(&e->as, &e->traps[TRAP_UNREACHABLE]);
    }
    return finish_function(e, code);
}

bool
arm64_compile(Native *n, const Checker *c, Convention convention)
{
    const Ir *ir = &c->ir;
    size_t units = 0;
    for (u32 f = 0; f < ir->nfuncs; f++) {
        const IrFunc *fn = &ir->funcs[f];
        units += CODE_PER_FUNCTION + (size_t)CODE_PER_INSTRUCTION * fn->count +
                 (size_t)CODE_PER_ARGUMENT * fn->nextra;
    }
    for (u32 k = 0; k < ir->ncallbacks; k++) {
        u32 type = c->decls.types[ir->callbacks[k]];
        u32 nparams = c->lists[c->types.arg0[type]];
        units += CODE_PER_ADAPTER + (size_t)CODE_PER_ADAPTER_PARAM * nparams;
    }
    if (!native_begin(n, c, units * sizeof(Code), HOST_RUNS_CODE)) {
        return false;
    }
    Code *start = (Code *)n->code;
    size_t used = 0;

    u8 *info = malloc(c->types.count);
    for (u32 t = 0; t < c->types.count; t++) {
        info[t] = type_info(c, t);
    }

    Emit e = { .as = { .code = start,
                       .at = start,
                       .fixups = malloc(16 * sizeof *e.as.fixups),
                       .fixups_cap = 16 },
               .c = c,
               .ir = &ir->code,
               .fn = &ir->funcs[0],
               .info = info,
               .data = n->data,
               .globals = n->globals,
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
               .slots = ir->code.slots,
               .retyped = malloc(16 * sizeof *e.retyped),
               .retyped_cap = 16,
               .place = convention == CONVENTION_APPLE ? PLACE_APPLE : 0,
               .open = malloc(16 * sizeof *e.open),
               .open_cap = 16,
               .branches = malloc(16 * sizeof *e.branches),
               .branches_cap = 16,
               .arm_at = malloc(16 * sizeof *e.arm_at),
               .arms_cap = 16,
               .ranges = malloc(16 * sizeof *e.ranges),
               .ranges_cap = 16,
               .homes = malloc(16 * sizeof *e.homes),
               .homes_cap = 16,
               .homes64 = malloc(16 * sizeof *e.homes64),
               .homes64_cap = 16 };
    u64 *adapters = malloc(ir->ncallbacks * sizeof *adapters);
    for (u32 k = 0; k < ir->ncallbacks && !e.as.error; k++) {
        u32 decl = ir->callbacks[k];
        if (c->decls.bodies[decl] == NO_BODY) {
            continue;
        }
        used = (used + FUNCTION_ALIGN - 1) & ~(size_t)(FUNCTION_ALIGN - 1);
        e.as.code = e.as.at = start + used;
        emit_adapter(&e, decl, n);
        adapters[k] = (u64)(uintptr_t)e.as.code;
        used += here(&e.as);
        assert(used <= units);
    }
    e.adapters = adapters;
    for (u32 f = 0; f < ir->nfuncs && !e.as.error; f++) {
        const IrFunc *fn = &ir->funcs[f];
        used = (used + FUNCTION_ALIGN - 1) & ~(size_t)(FUNCTION_ALIGN - 1);
        Code *at = start + used;
        u32 entry = emit_function(&e, fn, at, n);
        n->entries[fn->decl] = (u64)(uintptr_t)(at + entry);
        used += here(&e.as);
        assert(used <= units);
    }
    native_end(n, used * sizeof(Code));

    free(info);
    free(e.regs);
    free(e.vinfo);
    free(e.spills);
    free(e.spill_free);
    free(e.homes);
    free(e.homes64);
    free(e.slot_offsets);
    free(e.slot_regs);
    free(e.retyped);
    free(e.aliases);
    free(e.alias_at);
    free(e.open);
    free(e.as.fixups);
    free(e.branches);
    free(e.arm_at);
    free(e.ranges);
    if (e.process) {
        dlclose(e.process);
    }
    n->exports = adapters;
    n->nexports = ir->nexports;
    n->error = e.as.error;
    return e.as.error == NULL;
}
