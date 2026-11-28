static void
trap_if(Emit *e, u8 cond, u8 kind)
{
    add_fixup(&e->as, &e->traps[kind]);
    put(&e->as, OP_B_COND | cond);
}

static void
trap_unless_zero(Emit *e, u32 reg, u32 sf, u8 kind)
{
    add_fixup(&e->as, &e->traps[kind]);
    put(&e->as, OP_CBNZ | sf | reg);
}

static void
trap_if_zero(Emit *e, u32 reg, u32 sf, u8 kind)
{
    add_fixup(&e->as, &e->traps[kind]);
    put(&e->as, OP_CBZ | sf | reg);
}

static NOINLINE void
data_address(Emit *e, u32 i, u32 type, u32 offset)
{
    u32 d = result(e, i, e->info[type]);
    u64 to = (u64)(uintptr_t)(e->data + offset);

    adrp(&e->as, d, to);
    if (to & 0xFFF) {
        put(&e->as, SF | OP_ADD_IMM | (u32)(to & 0xFFF) << 10 | d << 5 | d);
    }
}

static NOINLINE void
global_address(Emit *e, u32 i, u32 type, u32 offset)
{
    u32 d = result(e, i, e->info[type]);
    mov_imm(&e->as, d, (u64)(uintptr_t)(e->globals + offset), SF);
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
        put(&e->as, extend_op(t) | n << 5 | d);
    } else {
        mov(&e->as, width(t), d, n);
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
        put(&e->as, extend_op(info) | r << 5 | d);
    } else if (bits_of(info) == 32) {
        put(&e->as, OP_ORR | r << 16 | ZR << 5 | d);
    } else {
        mov(&e->as, SF, d, r);
    }
}

static void
check_small(Emit *e, u8 t, u32 d)
{
    put(&e->as,
        OP_SUBS_EXT | d << 16 | extension(bits_of(t), is_signed(t)) << 13 | d << 5 | ZR);
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
        put(&e->as, (sub ? OP_FSUB : OP_FADD) | precision(t) | rb << 16 | ra << 5 | d);
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
    put(&e->as, op | sf | operand | ra << 5 | d);
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
        put(&e->as, OP_FMUL | precision(t) | rb << 16 | ra << 5 | d);
        return;
    }

    u32 ra = fetch(e, a, X16), rb = fetch(e, b, X17);
    release(e, a);
    release(e, b);
    u32 d = result(e, i, t);
    u32 sf = width(t);
    bool s = is_signed(t);
    if (kind == IR_MUL_WRAP || is_small(t)) {
        put(&e->as, OP_MUL | sf | rb << 16 | ra << 5 | d);
        if (kind == IR_MUL_WRAP) {
            normalize(e, t, d, d);
        } else {
            check_small(e, t, d);
        }
    } else if (!sf) {
        put(&e->as, (s ? OP_SMULL : OP_UMULL) | rb << 16 | ra << 5 | X8);
        put(&e->as, 0xEB20001Fu | (s ? 0xC000u : 0x4000u) | X8 << 16 | X8 << 5);
        trap_if(e, COND_NE, TRAP_OVERFLOW);
        mov(&e->as, 0, d, X8);
    } else {
        put(&e->as, (s ? OP_SMULH : OP_UMULH) | rb << 16 | ra << 5 | X8);
        put(&e->as, OP_MUL | SF | rb << 16 | ra << 5 | d);
        if (s) {
            put(&e->as, 0xEB80FC1Fu | d << 16 | X8 << 5);
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
shift_imm(Emit *e, u32 op, u32 sf, u32 d, u32 n, u32 count)
{
    put(&e->as, op | sf | (sf ? 0x00408000u : 0) | count << 16 | n << 5 | d);
}

static void
lsl_imm(Emit *e, u32 d, u32 n, u32 count)
{
    put(&e->as,
        OP_LSL_IMM | ((64 - count) & 63) << 16 | (63 - count) << 10 | n << 5 | d);
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
        put(&e->as, OP_ANDS_IMM | sf | field | ra << 5 | ZR);
        e->tested = i + 1;
        return;
    }

    u32 ra = fetch(e, a, X16);
    release(e, a);
    u32 d = result(e, i, t);
    if (pow2 && k == 0) {
        mov(&e->as, sf, d, rem ? ZR : ra);
        return;
    }
    if (pow2 && !s) {
        if (rem) {
            [[maybe_unused]] bool mask = logical_imm(divisor - 1, sf, &field);
            assert(mask);
            put(&e->as, OP_AND_IMM | sf | field | ra << 5 | d);
        } else {
            shift_imm(e, OP_LSR_IMM, sf, d, ra, k);
        }
        return;
    }
    if (pow2) {
        [[maybe_unused]] bool mask = logical_imm(divisor - 1, sf, &field);
        assert(mask);
        if (rem) {
            put(&e->as, OP_SUBS | sf | ra << 16 | ZR << 5 | X8);
            put(&e->as, OP_AND_IMM | sf | field | ra << 5 | X17);
            put(&e->as, OP_AND_IMM | sf | field | X8 << 5 | X8);
            put(&e->as, OP_CSNEG | sf | X8 << 16 | (u32)COND_MI << 12 | X17 << 5 | d);
            return;
        }
        if (k == 1) {
            put(&e->as,
                OP_ADD | sf | SHIFT_LSR | ra << 16 | (w - 1) << 10 | ra << 5 | X8);
        } else {
            shift_imm(e, OP_ASR_IMM, sf, X8, ra, w - 1);
            put(&e->as,
                OP_ADD | sf | SHIFT_LSR | X8 << 16 | (w - k) << 10 | ra << 5 | X8);
        }
        shift_imm(e, OP_ASR_IMM, sf, d, X8, k);
        return;
    }

    if (s) {
        int64_t magic;
        u32 shift;
        signed_magic((int64_t)divisor, w, &magic, &shift);
        mov_imm(&e->as, X17, (u64)magic, sf);
        if (sf) {
            put(&e->as, OP_SMULH | X17 << 16 | ra << 5 | X8);
        } else {
            put(&e->as, OP_SMULL | X17 << 16 | ra << 5 | X8);
            shift_imm(e, OP_ASR_IMM, SF, X8, X8, 32);
        }
        if ((int64_t)divisor > 0 && magic < 0) {
            put(&e->as, OP_ADD | sf | ra << 16 | X8 << 5 | X8);
        } else if ((int64_t)divisor < 0 && magic > 0) {
            put(&e->as, OP_SUB | sf | ra << 16 | X8 << 5 | X8);
        }
        if (shift) {
            shift_imm(e, OP_ASR_IMM, sf, X8, X8, shift);
        }
        u32 q = rem ? X8 : d;
        put(&e->as, OP_ADD | sf | SHIFT_LSR | X8 << 16 | (w - 1) << 10 | X8 << 5 | q);
    } else {
        u64 magic;
        bool add;
        u32 shift;
        unsigned_magic(divisor, &magic, &add, &shift);
        mov_imm(&e->as, X17, magic, SF);
        put(&e->as, OP_UMULH | X17 << 16 | ra << 5 | X8);
        u32 q = rem ? X8 : d;
        if (add) {
            put(&e->as, OP_SUB | SF | X8 << 16 | ra << 5 | X17);
            put(&e->as, OP_ADD | SF | SHIFT_LSR | X17 << 16 | 1u << 10 | X8 << 5 | X8);
            shift_imm(e, OP_LSR_IMM, SF, q, X8, shift - 1);
        } else if (shift) {
            shift_imm(e, OP_LSR_IMM, SF, q, X8, shift);
        } else {
            mov(&e->as, SF, q, X8);
        }
    }
    if (rem) {
        mov_imm(&e->as, X17, divisor, sf);
        put(&e->as, OP_MSUB | sf | X17 << 16 | ra << 10 | X8 << 5 | d);
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
        put(&e->as, OP_FDIV | precision(t) | rb << 16 | ra << 5 | d);
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
        put(&e->as, OP_ADDS_IMM | sf | 1 << 10 | rb << 5 | ZR);
        u32 skip = here(&e->as);
        put(&e->as, OP_B_COND | COND_NE);
        mov_imm(&e->as, X8, sf ? 0x8000000000000000ull : 0x80000000ull, sf);
        put(&e->as, OP_SUBS | sf | X8 << 16 | ra << 5 | ZR);
        trap_if(e, COND_EQ, TRAP_OVERFLOW);
        e->as.code[skip] |= ((here(&e->as) - skip) & 0x7FFFFu) << 5;
    }

    u32 op = s ? OP_SDIV : OP_UDIV;
    if (kind == IR_DIV) {
        put(&e->as, op | sf | rb << 16 | ra << 5 | d);
        if (s && !full) {
            check_small(e, t, d);
        }
    } else {
        put(&e->as, op | sf | rb << 16 | ra << 5 | X8);
        put(&e->as, OP_MSUB | sf | rb << 16 | ra << 10 | X8 << 5 | d);
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
        put(&e->as,
            (imm ? OP_ANDS_IMM | field : OP_ANDS | rb << 16) | sf | ra << 5 | ZR);
        e->tested = i + 1;
        return;
    }
    u32 d = result(e, i, t);
    if (imm) {
        u32 op = kind == IR_AND ? OP_AND_IMM
               : kind == IR_OR  ? OP_ORR_IMM
                                : OP_EOR_IMM;
        put(&e->as, op | sf | field | ra << 5 | d);
    } else {
        u32 op = kind == IR_AND ? OP_AND : kind == IR_OR ? OP_ORR : OP_EOR;
        put(&e->as, op | sf | rb << 16 | ra << 5 | d);
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
        put(&e->as, op | (home >> 3) << 15 | hi << 10 | SP << 5 | lo);
        return;
    }
    sp_address(&e->as, X8, home);
    put(&e->as, op | hi << 10 | X8 << 5 | lo);
}

static void
fetch_wide(Emit *e, u32 x, u32 lo, u32 hi)
{
    if (x & IR_CONST) {
        mov_imm(&e->as, lo, constant_bits(e, x), SF);
        mov_imm(&e->as, hi, constant_high(e, x), SF);
        return;
    }
    u32 home = e->spills[x];
    if (home == NO_VALUE) {
        put(&e->as, OP_FMOV_TO_X | V29 << 5 | lo);
        put(&e->as, OP_UMOV_D | 1u << 20 | V29 << 5 | hi);
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
    put(&e->as, OP_SUBS_IMM | SF | bits << 10 | d << 5 | ZR);
    trap_if(e, COND_HS, TRAP_SHIFT);
}

static void
shift_by(Emit *e, u8 kind, u8 t, u32 d, u32 n, u32 count)
{
    u32 op = kind == IR_SHL ? OP_LSLV : is_signed(t) ? OP_ASRV : OP_LSRV;
    put(&e->as, op | width(t) | count << 16 | n << 5 | d);
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
            put(&e->as,
                base | ((bits - count) & (bits - 1)) << 16 |
                    (bits - 1 - count) << 10 | ra << 5 | d);
        } else {
            u32 op = s ? (sf ? 0x93400000u : 0x13000000u) : base;
            put(&e->as, op | count << 16 | (bits - 1) << 10 | ra << 5 | d);
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
    put(&e->as, OP_SUBS_IMM | width(count_info) | bits_of(t) << 10 | rb << 5 | ZR);
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
    put(&e->as, OP_CSET | (u32)(cond ^ 1) << 12 | d);
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
        put(&e->as, OP_FCMP | precision(t) | rb << 16 | ra << 5);
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
            put(&e->as, (flip ? OP_ADDS_IMM : OP_SUBS_IMM) | sf | field | ra << 5 | ZR);
        } else {
            put(&e->as, OP_SUBS | sf | rb << 16 | ra << 5 | ZR);
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
        put(&e->as, OP_FNEG | precision(t) | ra << 5 | d);
        return;
    }

    u32 ra = fetch(e, a, X16);
    release(e, a);
    u32 d = result(e, i, t);
    u32 sf = width(t);
    bool small = is_small(t);
    switch (kind) {
        case IR_NOT: put(&e->as, OP_EOR_ONE | ra << 5 | d); break;
        case IR_BIT_NOT:
            put(&e->as, OP_ORN | sf | ra << 16 | ZR << 5 | d);
            if (small && !is_signed(t)) {
                normalize(e, t, d, d);
            }
            break;
        case IR_NEG_WRAP:
            put(&e->as, OP_SUB | sf | ra << 16 | ZR << 5 | d);
            if (small) {
                normalize(e, t, d, d);
            }
            break;
        case IR_NEG:
            if (!is_signed(t)) {
                trap_unless_zero(e, ra, sf, TRAP_OVERFLOW);
                mov(&e->as, sf, d, ZR);
            } else if (small) {
                put(&e->as, OP_SUB | ra << 16 | ZR << 5 | d);
                check_small(e, t, d);
            } else {
                put(&e->as, OP_SUBS | sf | ra << 16 | ZR << 5 | d);
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
            put(&e->as, OP_RBIT | SF | X16 << 5 | X8);
            put(&e->as, OP_CLZ | SF | X8 << 5 | X8);
            put(&e->as, OP_RBIT | SF | X17 << 5 | d);
            put(&e->as, OP_CLZ | SF | d << 5 | d);
            put(&e->as, OP_ADD_IMM | SF | 64u << 10 | d << 5 | d);
            put(&e->as, OP_SUBS_IMM | SF | X16 << 5 | ZR);
            put(&e->as, OP_CSEL | SF | d << 16 | (u32)COND_NE << 12 | X8 << 5 | d);
            break;
        case IR_LEADING_ZEROS:
            put(&e->as, OP_CLZ | SF | X17 << 5 | X8);
            put(&e->as, OP_CLZ | SF | X16 << 5 | d);
            put(&e->as, OP_ADD_IMM | SF | 64u << 10 | d << 5 | d);
            put(&e->as, OP_SUBS_IMM | SF | X17 << 5 | ZR);
            put(&e->as, OP_CSEL | SF | d << 16 | (u32)COND_NE << 12 | X8 << 5 | d);
            break;
        case IR_COUNT_ONES:
            put(&e->as, OP_FMOV_FROM_X | X16 << 5 | V31);
            put(&e->as, OP_INS_D1 | X17 << 5 | V31);
            put(&e->as, OP_CNT_8B | 1u << 30 | V31 << 5 | V31);
            put(&e->as, OP_ADDV_8B | 1u << 30 | V31 << 5 | V31);
            put(&e->as, OP_FMOV_TO_W | V31 << 5 | d);
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
        put(&e->as, (bits == 8 ? OP_UXTB : OP_UXTH) | ra << 5 | d);
        ra = d;
    }
    switch (kind) {
        case IR_TRAILING_ZEROS:
            if (bits < 32) {
                put(&e->as, OP_ORR_IMM | (32 - bits) << 16 | ra << 5 | d);
                ra = d;
            }
            put(&e->as, OP_RBIT | sf | ra << 5 | d);
            put(&e->as, OP_CLZ | sf | d << 5 | d);
            break;
        case IR_LEADING_ZEROS:
            put(&e->as, OP_CLZ | sf | ra << 5 | d);
            if (bits < 32) {
                put(&e->as, OP_SUB_IMM | (32 - bits) << 10 | d << 5 | d);
            }
            break;
        case IR_COUNT_ONES:
            put(&e->as, (sf ? OP_FMOV_FROM_X : OP_FMOV_FROM_W) | ra << 5 | V31);
            put(&e->as, OP_CNT_8B | V31 << 5 | V31);
            put(&e->as, OP_ADDV_8B | V31 << 5 | V31);
            put(&e->as, OP_FMOV_TO_W | V31 << 5 | d);
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
        put(&e->as, (s ? OP_SMULL : OP_UMULL) | rb << 16 | ra << 5 | X16);
        put(&e->as, OP_SUBS_EXT | SF | X16 << 16 | extend | X16 << 5 | ZR);
    } else if (kind == IR_MUL_OVERFLOWS && s) {
        put(&e->as, OP_SMULH | rb << 16 | ra << 5 | X8);
        put(&e->as, OP_MUL | SF | rb << 16 | ra << 5 | X16);
        put(&e->as, OP_SUBS | SF | SHIFT_ASR | X16 << 16 | 63u << 10 | X8 << 5 | ZR);
    } else if (kind == IR_MUL_OVERFLOWS) {
        put(&e->as, OP_UMULH | rb << 16 | ra << 5 | X16);
        put(&e->as, OP_SUBS_IMM | SF | X16 << 5 | ZR);
    } else if (is_small(t)) {
        u32 op = imm ? (negative ? OP_SUB_IMM : OP_ADD_IMM) : (sub ? OP_SUB : OP_ADD);
        put(&e->as, op | operand | ra << 5 | X16);
        put(&e->as, OP_SUBS_EXT | X16 << 16 | extend | X16 << 5 | ZR);
    } else {
        u32 op = imm ? (negative ? OP_SUBS_IMM : OP_ADDS_IMM)
                     : (sub ? OP_SUBS : OP_ADDS);
        put(&e->as, op | sf | operand | ra << 5 | ZR);
        cond = s ? COND_VS : sub ? COND_LO : COND_HS;
    }
    flag_result(e, i, cond);
}

static void
negate_if(Emit *e, u32 lo, u32 hi, u32 mask)
{
    put(&e->as, OP_EOR | SF | mask << 16 | lo << 5 | lo);
    put(&e->as, OP_EOR | SF | mask << 16 | hi << 5 | hi);
    put(&e->as, OP_SUBS | SF | mask << 16 | lo << 5 | lo);
    put(&e->as, OP_SBC | SF | mask << 16 | hi << 5 | hi);
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
    u32 big = here(&e->as);
    put(&e->as, OP_CBNZ | SF | X17);
    put(&e->as, OP_UCVTF | SF | precision(tt) | X16 << 5 | d);
    u32 done = here(&e->as);
    put(&e->as, OP_B);
    patch_one(&e->as, big, here(&e->as));

    put(&e->as, OP_CLZ | SF | X17 << 5 | X8);
    put(&e->as, OP_LSLV | SF | X8 << 16 | X17 << 5 | X17);
    put(&e->as, OP_ORN | SF | X8 << 16 | ZR << 5 | t0);
    shift_imm(e, OP_LSR_IMM, SF, t1, X16, 1);
    put(&e->as, OP_LSRV | SF | t0 << 16 | t1 << 5 | t1);
    put(&e->as, OP_ORR | SF | t1 << 16 | X17 << 5 | X17);
    put(&e->as, OP_LSLV | SF | X8 << 16 | X16 << 5 | X16);
    put(&e->as, OP_SUBS_IMM | SF | X16 << 5 | ZR);
    put(&e->as, OP_CSET | (u32)(COND_NE ^ 1) << 12 | t1);
    put(&e->as, OP_ORR | SF | t1 << 16 | X17 << 5 | X17);
    put(&e->as, OP_UCVTF | SF | precision(tt) | X17 << 5 | d);

    mov_imm(&e->as, t0, dbl ? 1087 : 191, 0);
    put(&e->as, OP_SUB | X8 << 16 | t0 << 5 | t0);
    lsl_imm(e, t0, t0, dbl ? 52 : 23);
    put(&e->as, (dbl ? OP_FMOV_FROM_X : OP_FMOV_FROM_W) | t0 << 5 | V31);
    put(&e->as, OP_FMUL | precision(tt) | V31 << 16 | d << 5 | d);
    patch_one(&e->as, done, here(&e->as));

    if (s) {
        put(&e->as, OP_SUBS_IMM | SF | sign << 5 | ZR);
        put(&e->as, OP_FNEG | precision(tt) | d << 5 | V31);
        put(&e->as,
            OP_FCSEL | precision(tt) | d << 16 | (u32)COND_NE << 12 | V31 << 5 | d);
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
            put(&e->as,
                is_signed(from) ? OP_SXTW | ra << 5 | d
                                : OP_ORR | ra << 16 | ZR << 5 | d);
        } else {
            mov(&e->as, width(tt), d, ra);
        }
    } else if (!float_from) {
        u32 ra = fetch(e, a, X16);
        release(e, a);
        u32 d = result(e, i, tt);
        u32 op = is_signed(from) ? OP_SCVTF : OP_UCVTF;
        put(&e->as, op | width(from) | precision(tt) | ra << 5 | d);
    } else {
        assert(float_to);
        u32 ra = fetch(e, a, V30);
        release(e, a);
        u32 d = result(e, i, tt);
        if (bits_of(from) == bits_of(tt)) {
            fmov(&e->as, d, ra);
        } else {
            put(&e->as,
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
        float_imm(&e->as, reg, bits, true);
    } else {
        float f = (float)value;
        u32 bits;
        memcpy(&bits, &f, sizeof bits);
        float_imm(&e->as, reg, bits, false);
    }
}

static ALWAYS_INLINE void
check_float_range(Emit *e, u32 ra, u8 from, u32 to_bits, bool to_signed)
{
    u32 fp = precision(from);
    bool dbl = bits_of(from) == 64;
    u32 mantissa = dbl ? 53 : 24;
    float_imm(&e->as, V29, power_bits(from, (int32_t)(to_signed ? to_bits - 1 : to_bits)), dbl);
    put(&e->as, OP_FCMP | fp | V29 << 16 | ra << 5);
    trap_if(e, COND_PL, TRAP_RANGE);
    if (!to_signed) {
        float_bound(e, V29, from, -1.0);
        put(&e->as, OP_FCMP | fp | V29 << 16 | ra << 5);
        trap_if(e, COND_LE, TRAP_RANGE);
    } else if (to_bits - 1 < mantissa) {
        float_bound(e, V29, from, -(double)(1ull << (to_bits - 1)) - 1.0);
        put(&e->as, OP_FCMP | fp | V29 << 16 | ra << 5);
        trap_if(e, COND_LE, TRAP_RANGE);
    } else {
        u64 sign = dbl ? 1ull << 63 : 1ull << 31;
        float_imm(&e->as, V29, sign | power_bits(from, (int32_t)to_bits - 1), dbl);
        put(&e->as, OP_FCMP | fp | V29 << 16 | ra << 5);
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
        put(&e->as, OP_SUBS_IMM | sf | field | reg << 5 | ZR);
        return;
    }
    u64 negated = sf ? 0 - v : (0 - v) & 0xFFFFFFFFu;
    if (may_negate && imm12(negated, &field)) {
        put(&e->as, OP_ADDS_IMM | sf | field | reg << 5 | ZR);
        return;
    }
    u32 temp = reg == X17 ? X8 : X17;
    mov_imm(&e->as, temp, v, sf);
    put(&e->as, OP_SUBS | sf | temp << 16 | reg << 5 | ZR);
}

static void
subtract_imm(Emit *e, u32 d, u32 n, u64 v, u32 sf)
{
    if (!sf) {
        v &= 0xFFFFFFFFu;
    }
    u32 field;
    if (v == 0) {
        put(&e->as, OP_ORR | sf | n << 16 | ZR << 5 | d);
    } else if (imm12(v, &field)) {
        put(&e->as, OP_SUB_IMM | sf | field | n << 5 | d);
    } else if (imm12(sf ? 0 - v : (0 - v) & 0xFFFFFFFFu, &field)) {
        put(&e->as, OP_ADD_IMM | sf | field | n << 5 | d);
    } else {
        mov_imm(&e->as, X8, v, sf);
        put(&e->as, OP_SUB | sf | X8 << 16 | n << 5 | d);
    }
}

static void
branch_to(Emit *e, u32 op, u32 target, u8 trap)
{
    if (target == NO_VALUE) {
        add_fixup(&e->as, &e->traps[trap]);
    } else {
        e->branches = grow(
            e->branches,
            &e->branches_cap,
            e->nbranches + 1,
            sizeof *e->branches
        );
        e->branches[e->nbranches++] = (Branch){ here(&e->as), target };
    }
    put(&e->as, op);
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
    add_fixup(&e->as, &left);
    put(&e->as, OP_B_COND | (is_signed(t) ? COND_LT : COND_LO));
    search(e, r, t, ranges + mid, n - mid, fallback, trap);
    patch(&e->as, left, here(&e->as));
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
        patch_one(&e->as, e->branches[k].at, here(&e->as));
    }
    e->nbranches = first;
}

static NOINLINE u8
narrow_wide(Emit *e, u32 a, u8 from, bool to_signed)
{
    fetch_wide(e, a, X16, X17);
    if (is_signed(from) && to_signed) {
        put(&e->as, OP_SUBS | SF | SHIFT_ASR | X16 << 16 | 63u << 10 | X17 << 5 | ZR);
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
        put(&e->as,
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
            put(&e->as, OP_SUBS_IMM | sf | ra << 5 | ZR);
            trap_if(e, COND_LT, TRAP_RANGE);
        } else {
            put(&e->as,
                OP_SUBS_EXT | sf | ra << 16 |
                    extension(to_bits, to_signed) << 13 | ra << 5 | ZR);
            trap_if(e, COND_NE, TRAP_RANGE);
        }
    } else if (to_bits - to_signed < from_bits) {
        u32 field = 0;
        [[maybe_unused]] bool
            mask = logical_imm(~((1ull << (to_bits - to_signed)) - 1), sf, &field);
        assert(mask);
        put(&e->as, OP_ANDS_IMM | sf | field | ra << 5 | ZR);
        trap_if(e, COND_NE, TRAP_RANGE);
    }
    u32 d = result(e, i, tt);
    if (width(tt) && !sf && is_signed(from) && to_signed) {
        put(&e->as, OP_SXTW | ra << 5 | d);
    } else if (width(tt) && !sf) {
        put(&e->as, OP_ORR | ra << 16 | ZR << 5 | d);
    } else {
        mov(&e->as, width(tt), d, ra);
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
        put(&e->as, extend_op(tt) | ra << 5 | d);
    } else {
        put(&e->as, OP_ORR | width(tt) | ra << 16 | ZR << 5 | d);
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
    put(&e->as, op | ra << 5 | d);
}

static NOINLINE void
check_align(Emit *e, u32 i, u32 type, u32 a, u32 align)
{
    u32 field = 0;
    [[maybe_unused]] bool mask = logical_imm(align - 1, SF, &field);
    assert(mask);

    u32 ra = fetch(e, a, X16);
    put(&e->as, OP_ANDS_IMM | SF | field | ra << 5 | ZR);
    trap_if(e, COND_NE, TRAP_ALIGN);
    if (can_take_over(e, i, a)) {
        take_over(e, a, i, e->info[type]);
        return;
    }
    release(e, a);
    mov(&e->as, SF, result(e, i, e->info[type]), ra);
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
    mov(&e->as, SF, result(e, i, e->info[type]), ra);
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
        mov(&e->as, SF, d, n);
    } else if (imm12(offset, &field)) {
        put(&e->as, OP_ADD_IMM | SF | field | n << 5 | d);
    } else {
        mov_imm(&e->as, X17, offset, SF);
        put(&e->as, OP_ADD | SF | X17 << 16 | n << 5 | d);
    }
}

static u32
fetch_index(Emit *e, u32 x, u32 scratch)
{
    u8 t = operand_info(e, x);
    u32 r = fetch(e, x, scratch);
    if (bits_of(t) < 64) {
        put(&e->as,
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
        mov(&e->as, SF, d, p);
    } else if ((size & (size - 1)) == 0) {
        u32 shift = (u32)__builtin_ctzll(size);
        put(&e->as, OP_ADD | SF | r << 16 | shift << 10 | p << 5 | d);
    } else {
        mov_imm(&e->as, X8, size, SF);
        put(&e->as, OP_MADD | SF | X8 << 16 | p << 10 | r << 5 | d);
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
        put(&e->as, OP_SUBS | SF | n << 16 | r << 5 | ZR);
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
    mov(&e->as, SF, d, r);
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
        put(&e->as, OP_SUBS | SF | q << 16 | r << 5 | ZR);
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
            mov_imm(&e->as, d, constant_bits(e, hi) - k, SF);
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
        mov_imm(&e->as, X16, constant_bits(e, hi), SF);
    }
    put(&e->as, OP_SUB | SF | r << 16 | q << 5 | d1);
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
        put(&e->as, OP_STP_X | (k / 8) << 15 | ZR << 10 | p << 5 | ZR);
    }
    if (size & 8) {
        put(&e->as, OP_STRX | (k / 8) << 10 | p << 5 | ZR);
        k += 8;
    }
    if (size & 4) {
        put(&e->as, OP_STRW | (k / 4) << 10 | p << 5 | ZR);
        k += 4;
    }
    if (size & 2) {
        put(&e->as, OP_STRH | (k / 2) << 10 | p << 5 | ZR);
        k += 2;
    }
    if (size & 1) {
        put(&e->as, OP_STRB | k << 10 | p << 5 | ZR);
    }
}

static void
zero_memory(Emit *e, u32 type, u32 a)
{
    u64 size = check_type_size(e->c, type);
    u32 p = fetch(e, a, X16);
    release(e, a);
    if (size > UNROLLED_BYTES) {
        mov(&e->as, SF, X16, p);
        p = X16;
        mov_imm(&e->as, X17, size / 32, SF);
        put(&e->as, OP_STP_X | 2 << 15 | ZR << 10 | X16 << 5 | ZR);
        put(&e->as, OP_STP_X_POST | 4 << 15 | ZR << 10 | X16 << 5 | ZR);
        put(&e->as, OP_SUBS_IMM | SF | 1 << 10 | X17 << 5 | X17);
        put(&e->as, OP_B_COND | ((u32)-3 & 0x7FFFFu) << 5 | COND_NE);
        size %= 32;
    }
    store_zeros(e, p, size);
}

static void
copy_bytes(Emit *e, u32 d, u32 s, u64 size)
{
    u32 k = 0;
    for (; size >= 32; size -= 32, k += 32) {
        put(&e->as, OP_LDP_Q | (k / 16) << 15 | V31 << 10 | s << 5 | V30);
        put(&e->as, OP_STP_Q | (k / 16) << 15 | V31 << 10 | d << 5 | V30);
    }
    if (size & 16) {
        put(&e->as, OP_LDR_Q | (k / 16) << 10 | s << 5 | V30);
        put(&e->as, OP_STR_Q | (k / 16) << 10 | d << 5 | V30);
        k += 16;
    }
    if (size & 8) {
        put(&e->as, OP_LDRX | (k / 8) << 10 | s << 5 | X8);
        put(&e->as, OP_STRX | (k / 8) << 10 | d << 5 | X8);
        k += 8;
    }
    if (size & 4) {
        put(&e->as, OP_LDRW | (k / 4) << 10 | s << 5 | X8);
        put(&e->as, OP_STRW | (k / 4) << 10 | d << 5 | X8);
        k += 4;
    }
    if (size & 2) {
        put(&e->as, OP_LDRH | (k / 2) << 10 | s << 5 | X8);
        put(&e->as, OP_STRH | (k / 2) << 10 | d << 5 | X8);
        k += 2;
    }
    if (size & 1) {
        put(&e->as, OP_LDRB | k << 10 | s << 5 | X8);
        put(&e->as, OP_STRB | k << 10 | d << 5 | X8);
    }
}

static void
copy_between(Emit *e, u32 d, u32 s, u64 size)
{
    assert(s != X16);
    if (size > UNROLLED_BYTES) {
        mov(&e->as, SF, X16, d);
        mov(&e->as, SF, X17, s);
        d = X16;
        s = X17;
        mov_imm(&e->as, X8, size / 32, SF);
        put(&e->as, OP_LDP_Q_POST | 2 << 15 | V31 << 10 | X17 << 5 | V30);
        put(&e->as, OP_STP_Q_POST | 2 << 15 | V31 << 10 | X16 << 5 | V30);
        put(&e->as, OP_SUBS_IMM | SF | 1 << 10 | X8 << 5 | X8);
        put(&e->as, OP_B_COND | ((u32)-3 & 0x7FFFFu) << 5 | COND_NE);
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
            float_imm(&e->as, d, constant_bits(e, x), bits_of(t) == 64);
        } else {
            mov_imm(&e->as, d, constant_bits(e, x), width(t));
        }
        return;
    }
    if (e->regs[x] != NO_REG) {
        if (fl) {
            fmov(&e->as, d, e->regs[x] & 31u);
        } else {
            mov(&e->as, SF, d, e->regs[x] & 31u);
        }
    } else {
        memory(&e->as, fl ? OP_LDRD : OP_LDRX, 3, d, e->spills[x], fl ? X8 : d);
    }
    release(e, x);
}

static void
piece(Emit *e, u32 op, u32 scale, u32 rt, u32 base, u32 offset)
{
    if (base == SP) {
        memory(&e->as, op, scale, rt, offset, X16);
        return;
    }
    put(&e->as, op | (offset >> scale) << 10 | base << 5 | rt);
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
        put(&e->as, OP_ORR | SF | X17 << 16 | (done * 8) << 10 | d << 5 | d);
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
                put(&e->as,
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
        put(&e->as,
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
        mov(&e->as, SF, d, s);
        return;
    }

    u64 size = check_type_size(e->c, type);
    sp_address(&e->as, d, e->copies_at + e->copied);
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
        sp_address(&e->as, X16, offset);
        copy_bytes(e, X16, s, check_type_size(e->c, type));
        return;
    }

    u32 root = e->ir->bs[e->fn->first + v];
    if (root == DATA_ROOT ||
        (root < DATA_ROOT &&
         e->ir->slots[e->fn->first_slot + root].weight != WEIGHT_ADDRESSED)) {
        memory(&e->as, OP_STRX, 3, s, offset, X16);
        return;
    }

    u64 size = check_type_size(e->c, type);
    u32 copy = e->copies_at + e->copied;
    sp_address(&e->as, X16, copy);
    copy_between(e, X16, s, size);
    e->copied += (u32)((size + 7) & ~(u64)7);
    assert(e->copied <= e->fn->copies);
    sp_address(&e->as, X17, copy);
    memory(&e->as, OP_STRX, 3, X17, offset, X16);
}

static void
lane_constant(Emit *e, u32 d, u8 t, u64 bits)
{
    u32 scale = t & INFO_SCALE;
    u64 ones = scale == 3 ? ~0ull : (1ull << (8u << scale)) - 1;
    u64 lane = bits & ones;
    if (lane == 0 || lane == ones) {
        u32 imm = lane ? 0xFF : 0;
        put(&e->as, OP_MOVI_64 | (imm >> 5) << 16 | (imm & 31) << 5 | d);
        return;
    }
    if (lane < 256 && scale < 3) {
        static const u32 movi[] = { OP_MOVI_8, OP_MOVI_16, OP_MOVI_32 };
        put(&e->as, movi[scale] | (u32)(lane >> 5) << 16 | (u32)(lane & 31) << 5 | d);
        return;
    }
    mov_imm(&e->as, X16, lane, scale == 3 ? SF : 0);
    put(&e->as, OP_DUP | (1u << scale) << 16 | X16 << 5 | d);
}

static void
wide_constant(Emit *e, u32 d, u64 lo, u64 hi)
{
    if ((lo == 0 || lo == ~0ull) && hi == lo) {
        u32 imm = lo ? 0xFF : 0;
        put(&e->as, OP_MOVI_64 | (imm >> 5) << 16 | (imm & 31) << 5 | d);
        return;
    }
    mov_imm(&e->as, X16, lo, SF);
    put(&e->as, OP_FMOV_FROM_X | X16 << 5 | d);
    if (hi) {
        mov_imm(&e->as, X16, hi, SF);
        put(&e->as, OP_INS_D1 | X16 << 5 | d);
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
    memory(&e->as, OP_LDR_Q, 4, scratch, home, X8);
    release_home(e, home);
    return scratch;
}

static void
move_lanes(Emit *e, u32 d, u32 x)
{
    u32 r = fetch_lanes(e, x, d);
    if (r != d) {
        put(&e->as, OP_VORR | r << 16 | r << 5 | d);
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
    memory(&e->as, OP_STR_Q, 4, r, place & ~PLACE_STACK, X16);
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
    memory(&e->as, store_op(info, r), scale, r, place & ~PLACE_STACK, X16);
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
        sp_address(&e->as, X8, e->slot_offsets[args[args[0] + 1]]);
    }
}

static NOINLINE void
receive_result(Emit *e, u32 type, const u32 *args)
{
    if (is_vector(e->info[type])) {
        put(&e->as, OP_VORR | 0 << 16 | 0 << 5 | V29);
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
        e->as.error = global
                        ? "the name of an `extern` global is longer than 255 "
                          "bytes"
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
        e->as
            .error = global
                       ? "an `extern` global names a C variable that isn't "
                         "loaded"
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
        adrp(&e->as, d, to);
        if (to & 0xFFF) {
            put(&e->as, SF | OP_ADD_IMM | (u32)(to & 0xFFF) << 10 | d << 5 | d);
        }
        return;
    }
    if (!n->entries[decl] && !bind_c(e, n, decl)) {
        return;
    }

    u32 d = result(e, i, e->info[type]);
    u64 slot = (u64)(uintptr_t)(n->entries + decl);
    adrp(&e->as, d, slot);
    put(&e->as, OP_LDRX | (u32)((slot & 0xFFF) >> 3) << 10 | d << 5 | d);
}

static NOINLINE void
c_global_address(Emit *e, u32 i, u32 type, u32 decl, const Native *n)
{
    if (!n->entries[decl] && !bind_c(e, n, decl)) {
        return;
    }

    u32 d = result(e, i, e->info[type]);
    u64 slot = (u64)(uintptr_t)(n->entries + decl);
    adrp(&e->as, d, slot);
    put(&e->as, OP_LDRX | (u32)((slot & 0xFFF) >> 3) << 10 | d << 5 | d);
}

static NOINLINE void
pair_result(Emit *e, u32 i, u8 t, u8 second)
{
    if (is_float(t)) {
        fmov(&e->as, alloc(e, i, t), 0);
        mov(&e->as, SF, alloc(e, i + 1, second), 1);
        return;
    }
    u32 d0 = half_result(e, i, 0, t);
    u32 d1 = half_result(e, i, 1, second);
    assert(d1 != 0);
    if (d0 == 1) {
        mov(&e->as, SF, d1, 1);
        mov(&e->as, SF, d0, 0);
    } else {
        mov(&e->as, SF, d0, 0);
        mov(&e->as, SF, d1, 1);
    }
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
        put(&e->as, OP_LOAD_CONTEXT);
        e->context_saved = true;
    }
    e->called = true;

    u64 entry = (u64)(uintptr_t)(n->entries + decl);
    adrp(&e->as, X16, entry);
    put(&e->as, OP_LDR_X16_X16 | (u32)((entry & 0xFFF) >> 3) << 10);
    put(&e->as, OP_BLR_X16);

    if (t & INFO_MEMORY) {
        receive_result(e, type, args);
    } else if (t & INFO_PAIR) {
        pair_result(e, i, t, e->info[TYPE_ID_I64]);
    } else if (check_is_flag_optional(e->c, type)) {
        pair_result(e, i, t, e->info[TYPE_ID_BOOL]);
    } else if (check_is_error_pair(e->c, type)) {
        pair_result(e, i, t, e->info[TYPE_ID_ERROR]);
    } else if (type != TYPE_ID_VOID) {
        bool fl = is_float(t);
        u32 d = result(e, i, e->info[type]);
        if (fl) {
            fmov(&e->as, d, 0);
        } else if (ints == 0) {
            take_from_c(e, t, d, 0);
        } else {
            mov(&e->as, SF, d, 0);
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
    put(&e->as, OP_LDP_X | d1 << 10 | p << 5 | d0);
}

static NOINLINE void
load_flagged(Emit *e, u32 i, u32 p, u32 type)
{
    u8 t = e->info[type];
    u32 d0 = half_result(e, i, 0, t);
    u32 d1 = half_result(e, i, 1, e->info[TYPE_ID_BOOL]);
    u32 size = (u32)check_type_size(e->c, e->c->types.arg0[type]);
    u32 flag = OP_LDRB | size << 10 | p << 5 | d1;
    if (d1 == p) {
        put(&e->as, load_op(t) | p << 5 | d0);
        put(&e->as, flag);
        return;
    }
    put(&e->as, flag);
    put(&e->as, load_op(t) | p << 5 | d0);
}

static NOINLINE void
store_flagged(Emit *e, u32 type, u32 a, u32 b)
{
    const u32 *x = e->ir->extra + e->fn->first_extra + b;
    u8 t = e->info[type];
    u32 p = fetch(e, a, X16);
    u32 r0 = (x[0] & IR_CONST) && constant_bits(e, x[0]) == 0
               ? ZR
               : fetch(e, x[0], is_float(t) ? V30 : X17);
    u32 r1 = (x[1] & IR_CONST) && constant_bits(e, x[1]) == 0 ? ZR
                                                              : fetch(e, x[1], X8);
    u32 size = (u32)check_type_size(e->c, e->c->types.arg0[type]);
    put(&e->as, store_op(t, r0) | p << 5 | r0);
    put(&e->as, OP_STRB | size << 10 | p << 5 | r1);
    release(e, a);
    release(e, x[0]);
    release(e, x[1]);
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
    put(&e->as, OP_STP_X | r1 << 10 | p << 5 | r0);
    release(e, a);
    release(e, x[0]);
    release(e, x[1]);
}

static void
move_pair(Emit *e, u32 d0, u32 d1, u32 a, u32 b)
{
    if (in_register(e, a, d1) && in_register(e, b, d0)) {
        mov(&e->as, SF, X16, d0);
        mov(&e->as, SF, d0, d1);
        mov(&e->as, SF, d1, X16);
        release(e, a);
        release(e, b);
    } else if (in_register(e, b, d0)) {
        move_into(e, d1, b, false);
        move_into(e, d0, a, false);
    } else {
        move_into(e, d0, a, false);
        move_into(e, d1, b, false);
    }
}

static NOINLINE void
move_halves(Emit *e, u32 type, u32 d0, u32 d1, u32 a, u32 b)
{
    if (!is_float(e->info[type])) {
        move_pair(e, d0, d1, a, b);
        return;
    }
    move_into(e, d0, a, true);
    move_into(e, d1, b, false);
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
        put(&e->as, OP_ADR | 4u << 5 | X16);
        put(&e->as, OP_LDRSW_SCALED | X17 << 16 | X16 << 5 | X17);
        put(&e->as, OP_ADD | SF | X17 << 16 | X16 << 5 | X16);
        put(&e->as, OP_BR | X16 << 5);
        o.table = here(&e->as);
        for (u64 k = 0; k < span; k++) {
            put(&e->as, 0);
        }
        if (count < span && fallback == NO_VALUE) {
            o.stub = here(&e->as);
            branch_to(e, OP_B, NO_VALUE, TRAP_UNREACHABLE);
        }
    } else {
        search(e, r, t, e->ranges, n, fallback, TRAP_UNREACHABLE);
    }

    e->arm_at = grow(e->arm_at, &e->arms_cap, e->narms + 1, sizeof *e->arm_at);
    e->arm_at[e->narms++] = here(&e->as);
    e->open = grow(e->open, &e->open_cap, e->nopen + 1, sizeof *e->open);
    e->open[e->nopen++] = o;
}

static void
finish_switch(Emit *e, const Open *o)
{
    const u32 *arms = e->arm_at + o->first_arm;
    for (u32 k = o->first_branch; k < e->nbranches; k++) {
        patch_one(&e->as, e->branches[k].at, arms[e->branches[k].target]);
    }
    if (o->table != NO_VALUE) {
        const u32 *x = e->ir->extra + e->fn->first_extra +
                       e->ir->bs[e->fn->first + o->target];
        u32 fallback = x[1] == NO_VALUE ? o->stub : arms[x[1]];
        for (u32 k = 0; k < o->span; k++) {
            e->as.code[o->table + k] = (fallback - o->table) * 4;
        }
        for (u32 k = 0; k < x[0]; k++) {
            const u32 *w = x + 2 + 5 * k;
            u32 lo = (u32)((w[0] | (u64)w[1] << 32) - o->min);
            u32 hi = (u32)((w[2] | (u64)w[3] << 32) - o->min);
            u32 offset = (arms[w[4]] - o->table) * 4;
            for (u32 v = lo; v <= hi; v++) {
                e->as.code[o->table + v] = offset;
            }
        }
    }
    e->nbranches = o->first_branch;
    e->narms = o->first_arm;
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

static u32
outgoing_bytes(const Emit *e)
{
    return e->fn->stack_args;
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
        put(&e->as, OP_ADD_IMM | SF | offset << 10 | X29 << 5 | d);
        return;
    }
    mov_imm(&e->as, d, offset, SF);
    put(&e->as, OP_ADD | SF | d << 16 | X29 << 5 | d);
}

static NOINLINE void
receive_vector(Emit *e, u32 p, ArgCursor *at)
{
    bool used = e->slots[p].weight != 0;
    if (at->floats < 8) {
        if (used) {
            memory(&e->as, OP_STR_Q, 4, at->floats, e->slot_offsets[p], X16);
        }
        at->floats++;
        return;
    }

    u32 place = check_place_arg(at, 1 | REGS_FLOAT, 16, 16, e->place);
    assert(place & PLACE_STACK);
    e->wide_record = true;
    if (used) {
        incoming_address(e, X16, place & ~PLACE_STACK);
        put(&e->as, OP_LDR_Q | X16 << 5 | V30);
        memory(&e->as, OP_STR_Q, 4, V30, e->slot_offsets[p], X16);
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
        sp_address(&e->as, X16, e->slot_offsets[p]);
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
        put(&e->as, load_op(info) | (offset >> scale) << 10 | X29 << 5 | r);
    } else {
        incoming_address(e, X16, offset - 32);
        put(&e->as, load_op(info) | X16 << 5 | r);
    }
    if (h == NO_REG) {
        store_typed(&e->as, e->info[e->slots[p].type], r, e->slot_offsets[p]);
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
    memory(&e->as, OP_LDRX, 3, d, e->slot_offsets[p], d);
}

static NOINLINE void
return_struct(Emit *e, u32 type, u32 a)
{
    if (e->result_at != NO_VALUE) {
        u32 s = fetch(e, a, X17);
        release(e, a);
        memory(&e->as, OP_LDRX, 3, X16, e->result_at, X16);
        copy_between(e, X16, s, check_type_size(e->c, type));
        return;
    }
    u32 p = fetch(e, a, X16);
    if (p < 8) {
        mov(&e->as, SF, X16, p);
        p = X16;
    }
    move_struct(e, true, type, 0, p, 0);
    release(e, a);
}

static bool
frames_lanes(const Emit *e, u32 v)
{
    if (v + 1 >= e->fn->count) {
        return false;
    }
    u32 at = e->fn->first + v + 1;
    u8 kind = e->ir->kinds[at];
    return (kind == IR_LOAD_AT || kind == IR_STORE_AT) && e->ir->as[at] == v &&
           is_vector(e->info[e->ir->types[at]]);
}

static bool
reads_lanes_next(const Emit *e, u32 v)
{
    u32 at = e->fn->first + v + 1;
    u32 end = e->fn->first + e->fn->count;
    while (at < end && (e->ir->kinds[at] == IR_ADDR || e->ir->kinds[at] == IR_FIELD ||
                        e->ir->kinds[at] == IR_INDEX)) {
        at++;
    }
    if (at >= end) {
        return false;
    }
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
carve_home(Emit *e, u32 bytes)
{
    u32 offset = (e->locals + e->spill_top + 15) & ~15u;
    e->spill_top = offset + bytes - e->locals;
    if (e->spill_top > e->spill_max) {
        e->spill_max = e->spill_top;
    }
    return offset;
}

static u32
new_home(Emit *e)
{
    if (e->nhomes) {
        return e->homes[--e->nhomes];
    }
    return carve_home(e, 16);
}

static void
keep_lanes(Emit *e, u32 v)
{
    if (reads_lanes_next(e, v)) {
        e->spills[v] = NO_VALUE;
        return;
    }
    u32 home = new_home(e);
    memory(&e->as, OP_STR_Q, 4, V29, home, X8);
    e->spills[v] = home;
}

static u32
lane_shift(Emit *e, u8 kind, u8 t, u32 b)
{
    u32 scale = t & INFO_SCALE;
    u32 bits = 8u << scale;
    if (b & IR_CONST) {
        u32 count = (u32)constant_bits(e, b);
        if (kind == IR_SHL || count == 0) {
            return OP_SHL_IMM | (bits + count) << 16;
        }
        return (is_signed(t) ? OP_SSHR_IMM : OP_USHR_IMM) | (2 * bits - count) << 16;
    }

    u8 count_info = operand_info(e, b);
    u32 rb = X17;
    if (UNLIKELY(count_info & INFO_UNSUPPORTED)) {
        check_wide_count(e, b, bits, X17);
    } else {
        rb = fetch(e, b, X17);
        put(&e->as, OP_SUBS_IMM | width(count_info) | bits << 10 | rb << 5 | ZR);
        trap_if(e, COND_HS, TRAP_SHIFT);
        release(e, b);
    }
    if (kind == IR_SHR) {
        put(&e->as, OP_SUB | rb << 16 | ZR << 5 | X16);
        rb = X16;
    }
    put(&e->as, OP_DUP | (1u << scale) << 16 | rb << 5 | V31);
    return (is_signed(t) ? OP_SSHL : OP_USHL) | scale << 22 | V31 << 16;
}

static void
shift_lanes(Emit *e, u8 kind, u8 t, u32 a, u32 b)
{
    u32 op = lane_shift(e, kind, t, b);
    u32 r = fetch_lanes(e, a, V30);
    put(&e->as, op | r << 5 | V29);
}

static void
gather_bits(Emit *e, u32 scale, u32 r, u32 d)
{
    u32 bits = 8u << scale;
    put(&e->as, OP_USHR_IMM | (bits + 1) << 16 | r << 5 | V29);
    for (u32 n = 1, w = bits; w < 64; n *= 2, w *= 2) {
        put(&e->as, OP_USRA | (3 * w + n) << 16 | V29 << 5 | V29);
    }
    put(&e->as, OP_UMOV_B | V29 << 5 | X16);
    put(&e->as, OP_UMOV_B | 8u << 17 | V29 << 5 | X17);
    put(&e->as, OP_ORR | X17 << 16 | (8u >> scale) << 10 | X16 << 5 | d);
}

static void
gather_top_bits(Emit *e, u32 i, u8 t, u32 a)
{
    u32 scale = t & INFO_SCALE;
    u32 d = result(e, i, e->info[scale ? TYPE_ID_U8 : TYPE_ID_U16]);
    gather_bits(e, scale, fetch_lanes(e, a, V30), d);
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
    put(&e->as, OP_UMOV_D | ra << 5 | X16);
    put(&e->as, OP_UMOV_D | rb << 5 | X17);
    put(&e->as, OP_MUL | SF | X17 << 16 | X16 << 5 | X16);
    put(&e->as, OP_UMOV_D | 1u << 20 | ra << 5 | X17);
    put(&e->as, OP_UMOV_D | 1u << 20 | rb << 5 | X8);
    put(&e->as, OP_MUL | SF | X8 << 16 | X17 << 5 | X17);
    put(&e->as, OP_FMOV_FROM_X | X16 << 5 | V29);
    put(&e->as, OP_INS_D1 | X17 << 5 | V29);
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
        memory(&e->as, OP_STR_Q, 4, V29, home, X8);
    }
    return home;
}

static u32
call_routine(Emit *e, u32 a, u32 b, u32 *calls, u32 mode)
{
    u32 ha = wide_home(e, a);
    u32 hb = wide_home(e, b);
    u32 hr = new_home(e);

    sp_address(&e->as, X16, ha);
    sp_address(&e->as, X17, hb);
    sp_address(&e->as, X8, hr);
    if (mode) {
        put(&e->as, OP_ADD_IMM | SF | mode << 10 | X8 << 5 | X8);
    }
    add_fixup(&e->as, calls);
    put(&e->as, OP_BL);

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
    put(&e->as, (sub ? OP_SUBS : OP_ADDS) | SF | X8 << 16 | X16 << 5 | X16);
    put(&e->as, carry | SF | b1 << 16 | X17 << 5 | X17);
    if (checked) {
        trap_if(e, is_signed(t) ? COND_VS : (sub ? COND_LO : COND_HS), TRAP_OVERFLOW);
    }
    e->free_int |= 1u << b1;
    keep_wide(e, i, t, X16, X17);
}

static void
unsigned_product(Emit *e, u32 b0, u32 b1, u32 *overflow)
{
    put(&e->as, OP_SUBS_IMM | SF | X17 << 5 | ZR);
    put(&e->as, OP_CCMP_IMM | SF | (u32)COND_NE << 12 | b1 << 5 | 4);
    add_fixup(&e->as, overflow);
    put(&e->as, OP_B_COND | COND_NE);
    put(&e->as, OP_UMULH | b0 << 16 | X17 << 5 | X8);
    add_fixup(&e->as, overflow);
    put(&e->as, OP_CBNZ | SF | X8);
    put(&e->as, OP_UMULH | b1 << 16 | X16 << 5 | X8);
    add_fixup(&e->as, overflow);
    put(&e->as, OP_CBNZ | SF | X8);
    put(&e->as, OP_MUL | SF | b0 << 16 | X17 << 5 | X8);
    put(&e->as, OP_MADD | SF | b1 << 16 | X8 << 10 | X16 << 5 | X8);
    put(&e->as, OP_UMULH | b0 << 16 | X16 << 5 | X17);
    put(&e->as, OP_ADDS | SF | X8 << 16 | X17 << 5 | X17);
    add_fixup(&e->as, overflow);
    put(&e->as, OP_B_COND | COND_HS);
    put(&e->as, OP_MUL | SF | b0 << 16 | X16 << 5 | X16);
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
        put(&e->as, OP_MUL | SF | b1 << 16 | X16 << 5 | b1);
        put(&e->as, OP_MADD | SF | b0 << 16 | b1 << 10 | X17 << 5 | X17);
        put(&e->as, OP_UMULH | b0 << 16 | X16 << 5 | b1);
        put(&e->as, OP_ADD | SF | b1 << 16 | X17 << 5 | X17);
        put(&e->as, OP_MUL | SF | b0 << 16 | X16 << 5 | X16);
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
            put(&e->as, OP_EXTR | X16 << 16 | (64 - n) << 10 | X17 << 5 | X17);
            lsl_imm(e, X16, X16, n);
        } else if (n >= 64) {
            shift_imm(e, shr, SF, X16, X17, n - 64);
            if (s) {
                shift_imm(e, OP_ASR_IMM, SF, X17, X17, 63);
            } else {
                hi = ZR;
            }
        } else if (n > 0) {
            put(&e->as, OP_EXTR | X16 << 16 | n << 10 | X17 << 5 | X16);
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
        put(&e->as, OP_SUBS_IMM | width(count_info) | 128u << 10 | n << 5 | ZR);
        trap_if(e, COND_HS, TRAP_SHIFT);
        release(e, b);
    }
    fetch_wide(e, a, X16, X17);

    u32 field = logical_field(64, 0);
    put(&e->as, OP_ORN | SF | n << 16 | ZR << 5 | X8);
    if (kind == IR_SHL) {
        shift_imm(e, OP_LSR_IMM, SF, t1, X16, 1);
        put(&e->as, OP_LSRV | SF | X8 << 16 | t1 << 5 | t1);
        put(&e->as, OP_LSLV | SF | n << 16 | X17 << 5 | X8);
        put(&e->as, OP_ORR | SF | X8 << 16 | t1 << 5 | t1);
        put(&e->as, OP_LSLV | SF | n << 16 | X16 << 5 | X8);
        put(&e->as, OP_ANDS_IMM | field | n << 5 | ZR);
        put(&e->as, OP_CSEL | SF | t1 << 16 | (u32)COND_NE << 12 | X8 << 5 | X17);
        put(&e->as, OP_CSEL | SF | X8 << 16 | (u32)COND_NE << 12 | ZR << 5 | X16);
    } else {
        lsl_imm(e, t1, X17, 1);
        put(&e->as, OP_LSLV | SF | X8 << 16 | t1 << 5 | t1);
        put(&e->as, OP_LSRV | SF | n << 16 | X16 << 5 | X8);
        put(&e->as, OP_ORR | SF | X8 << 16 | t1 << 5 | t1);
        put(&e->as, (s ? OP_ASRV : OP_LSRV) | SF | n << 16 | X17 << 5 | X8);
        put(&e->as, OP_ANDS_IMM | field | n << 5 | ZR);
        put(&e->as, OP_CSEL | SF | t1 << 16 | (u32)COND_NE << 12 | X8 << 5 | X16);
        u32 fill = ZR;
        if (s) {
            shift_imm(e, OP_ASR_IMM, SF, t1, X17, 63);
            fill = t1;
        }
        put(&e->as, OP_CSEL | SF | X8 << 16 | (u32)COND_NE << 12 | fill << 5 | X17);
    }
    e->free_int |= 1u << c | 1u << t1;
    keep_wide(e, i, t, X16, X17);
}

static void
negate_wide(Emit *e, u32 i, u8 kind, u8 t, u32 a)
{
    fetch_wide(e, a, X16, X17);
    if (kind == IR_NEG && !is_signed(t)) {
        put(&e->as, OP_ORR | SF | X17 << 16 | X16 << 5 | X8);
        trap_unless_zero(e, X8, SF, TRAP_OVERFLOW);
        keep_wide(e, i, t, ZR, ZR);
        return;
    }
    put(&e->as, OP_SUBS | SF | X16 << 16 | ZR << 5 | X16);
    put(&e->as, (kind == IR_NEG ? OP_SBCS : OP_SBC) | SF | X17 << 16 | ZR << 5 | X17);
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
        put(&e->as, OP_SUBS | SF | X8 << 16 | X16 << 5 | ZR);
        put(&e->as, OP_CCMP | SF | b1 << 16 | (u32)COND_EQ << 12 | X17 << 5);
        cond = kind == IR_EQ ? COND_EQ : COND_NE;
    } else {
        bool swapped = kind == IR_GT || kind == IR_LE;
        u32 l0 = swapped ? X8 : X16, l1 = swapped ? b1 : X17;
        u32 r0 = swapped ? X16 : X8, r1 = swapped ? X17 : b1;
        put(&e->as, OP_SUBS | SF | r0 << 16 | l0 << 5 | ZR);
        put(&e->as, OP_SBCS | SF | r1 << 16 | l1 << 5 | ZR);
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
        put(&e->as, OP_MOVZ | 1u << 5 | d);
        unsigned_product(e, b0, b1, &done);
        put(&e->as, OP_MOVZ | d);
        patch(&e->as, done, here(&e->as));
        e->free_int |= 1u << b0 | 1u << b1;
        return;
    }

    bool sub = kind == IR_SUB_OVERFLOWS;
    u32 b1 = take_reg(e, false);
    fetch_wide(e, a, X16, X17);
    fetch_wide(e, b, X8, b1);
    put(&e->as, (sub ? OP_SUBS : OP_ADDS) | SF | X8 << 16 | X16 << 5 | ZR);
    put(&e->as, (sub ? OP_SBCS : OP_ADCS) | SF | b1 << 16 | X17 << 5 | ZR);
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
        put(&e->as, OP_FCMP_ZERO | fp | ra << 5);
        put(&e->as, OP_FABS | fp | ra << 5 | V30);
        src = V30;
    }
    float_imm(&e->as, V29, power_bits(from, -64), dbl);
    put(&e->as, OP_FMUL | fp | V29 << 16 | src << 5 | V31);
    put(&e->as, OP_FCVTZU | SF | fp | V31 << 5 | X17);
    put(&e->as, OP_UCVTF | SF | fp | X17 << 5 | V31);
    float_imm(&e->as, V29, power_bits(from, 64), dbl);
    put(&e->as, OP_FMUL | fp | V29 << 16 | V31 << 5 | V31);
    put(&e->as, OP_FSUB | fp | V31 << 16 | src << 5 | V31);
    put(&e->as, OP_FCVTZU | SF | fp | V31 << 5 | X16);
    if (s) {
        put(&e->as, OP_CSINV | SF | ZR << 16 | (u32)(COND_MI ^ 1) << 12 | ZR << 5 | X8);
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
        put(&e->as, OP_SUBS_IMM | SF | X17 << 5 | ZR);
        trap_if(e, COND_LT, TRAP_RANGE);
        keep_wide(e, i, t, X16, X17);
        return;
    }

    u32 ra = fetch(e, a, X16);
    release(e, a);
    if (kind == IR_NARROW) {
        put(&e->as, OP_SUBS_IMM | width(from) | ra << 5 | ZR);
        trap_if(e, COND_LT, TRAP_RANGE);
    }
    if (width(from)) {
        mov(&e->as, SF, X16, ra);
    } else {
        put(&e->as,
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

static u32
new_home64(Emit *e)
{
    if (e->nhomes64) {
        return e->homes64[--e->nhomes64];
    }
    return carve_home(e, 64);
}

static void
release_quarters(Emit *e, u32 x)
{
    if (x & IR_CONST) {
        return;
    }
    e->homes64 = grow(e->homes64, &e->homes64_cap, e->nhomes64 + 1, sizeof *e->homes64);
    e->homes64[e->nhomes64++] = e->spills[x];
}

static void
constant_quarter(Emit *e, u32 x, u32 r)
{
    if (x & IR_CONST) {
        lane_constant(e, r, operand_info(e, x), constant_bits(e, x));
    }
}

static u32
quarter(Emit *e, u32 x, u32 q, u32 r)
{
    if (!(x & IR_CONST)) {
        memory(&e->as, OP_LDR_Q, 4, r, e->spills[x] + 16 * q, X8);
    }
    return r;
}

static void
put_quarter(Emit *e, u32 r, u32 home, u32 q)
{
    memory(&e->as, OP_STR_Q, 4, r, home + 16 * q, X8);
}

static u32
transfer_home(Emit *e)
{
    if (e->transfer64 == NO_VALUE) {
        e->transfer64 = carve_home(e, 64);
    }
    return e->transfer64;
}

static void
copy_quarters(Emit *e, u32 home, u32 x)
{
    constant_quarter(e, x, V30);
    for (u32 q = 0; q < 4; q++) {
        put_quarter(e, quarter(e, x, q, V30), home, q);
    }
    release_quarters(e, x);
}

static void
top_bits64(Emit *e, u32 i, u8 t, u32 type, u32 a)
{
    u32 scale = t & INFO_SCALE;
    u32 d = result(e, i, e->info[check_top_bits_type(e->c, type)]);
    constant_quarter(e, a, V30);
    for (u32 q = 0; q < 4; q++) {
        gather_bits(e, scale, quarter(e, a, q, V30), q ? X16 : d);
        if (q) {
            put(&e->as,
                OP_ORR | SF | X16 << 16 | (q * (16u >> scale)) << 10 | d << 5 | d);
        }
    }
    release_quarters(e, a);
}

static void
store_at64(Emit *e, u32 a, u32 b)
{
    constant_quarter(e, b, V30);
    if (a == e->framed) {
        for (u32 q = 0; q < 4; q++) {
            put_quarter(e, quarter(e, b, q, V30), e->framed_offset, q);
        }
        e->framed = NO_VALUE;
    } else {
        u32 p = fetch(e, a, X16);
        for (u32 q = 0; q < 4; q++) {
            put(&e->as, OP_STR_Q | q << 10 | p << 5 | quarter(e, b, q, V30));
        }
        release(e, a);
    }
    release_quarters(e, b);
}

static void
load_at64(Emit *e, u32 a, u32 home)
{
    if (a == e->framed) {
        for (u32 q = 0; q < 4; q++) {
            memory(&e->as, OP_LDR_Q, 4, V29, e->framed_offset + 16 * q, X8);
            put_quarter(e, V29, home, q);
        }
        e->framed = NO_VALUE;
        return;
    }
    u32 p = fetch(e, a, X16);
    release(e, a);
    for (u32 q = 0; q < 4; q++) {
        put(&e->as, OP_LDR_Q | q << 10 | p << 5 | V29);
        put_quarter(e, V29, home, q);
    }
}

static void
unary_quarters(Emit *e, u32 op, u32 a, u32 home)
{
    constant_quarter(e, a, V30);
    for (u32 q = 0; q < 4; q++) {
        put(&e->as, op | quarter(e, a, q, V30) << 5 | V29);
        put_quarter(e, V29, home, q);
    }
    release_quarters(e, a);
}

static void
lookup64(Emit *e, u32 a, u32 b, u32 home)
{
    u32 r = fetch_lanes(e, a, V31);
    if (r != V31) {
        put(&e->as, OP_VORR | r << 16 | r << 5 | V31);
    }
    constant_quarter(e, b, V30);
    for (u32 q = 0; q < 4; q++) {
        put(&e->as, OP_TBL | quarter(e, b, q, V30) << 16 | V31 << 5 | V29);
        put_quarter(e, V29, home, q);
    }
    release_quarters(e, b);
}

static void
binary_quarters(Emit *e, u8 kind, u8 t, u32 a, u32 b, u32 home)
{
    assert(a != b || (a & IR_CONST));
    constant_quarter(e, a, V30);
    constant_quarter(e, b, V31);
    for (u32 q = 0; q < 4; q++) {
        u32 ra = quarter(e, a, q, V30);
        u32 rb = quarter(e, b, q, V31);
        if (kind == IR_MUL_WRAP && (t & INFO_SCALE) == 3) {
            multiply_wide_lanes(e, ra, rb);
        } else {
            if (kind == IR_LT || kind == IR_LE) {
                swap(&ra, &rb);
            }
            put(&e->as, lane_op(kind, t) | rb << 16 | ra << 5 | V29);
            if (kind == IR_NE) {
                put(&e->as, OP_VNOT | V29 << 5 | V29);
            }
        }
        put_quarter(e, V29, home, q);
    }
    release_quarters(e, a);
    release_quarters(e, b);
}

static NOINLINE void
lanes64(Emit *e, u32 i, u8 kind, u32 type, u32 a, u32 b)
{
    u8 t = e->info[type];
    switch (kind) {
        case IR_TOP_BITS: top_bits64(e, i, t, type, a); return;
        case IR_STORE_AT: store_at64(e, a, b); return;
        case IR_ELSE: {
            Open *o = &e->open[e->nopen - 1];
            copy_quarters(e, transfer_home(e), a);
            add_fixup(&e->as, &o->ends);
            put(&e->as, OP_B);
            patch(&e->as, o->skip, here(&e->as));
            o->skip = 0;
            return;
        }
        case IR_EXIT: {
            Open *o = find_open(e, a);
            copy_quarters(e, transfer_home(e), b);
            bool falls = i + 1 < e->fn->count &&
                         e->ir->kinds[e->fn->first + i + 1] == IR_END &&
                         o == &e->open[e->nopen - 1];
            if (!falls) {
                add_fixup(&e->as, &o->ends);
                put(&e->as, OP_B);
            }
            return;
        }
        case IR_BITCAST:
            assert(!(a & IR_CONST));
            e->spills[i] = e->spills[a];
            e->vinfo[i] = t;
            return;
        default: break;
    }

    u32 home = new_home64(e);
    switch (kind) {
        case IR_LOAD_AT: load_at64(e, a, home); break;
        case IR_SPLAT: {
            u32 r = fetch(e, a, X16);
            release(e, a);
            put(&e->as, OP_DUP | (1u << (t & INFO_SCALE)) << 16 | r << 5 | V29);
            for (u32 q = 0; q < 4; q++) {
                put_quarter(e, V29, home, q);
            }
            break;
        }
        case IR_SHL:
        case IR_SHR:
            unary_quarters(e, lane_shift(e, kind, t, b), a, home);
            break;
        case IR_NEG_WRAP:
            unary_quarters(e, OP_VNEG | (u32)(t & INFO_SCALE) << 22, a, home);
            break;
        case IR_BIT_NOT: unary_quarters(e, OP_VNOT, a, home); break;
        case IR_LOOKUP: lookup64(e, a, b, home); break;
        case IR_END: {
            Open o = e->open[--e->nopen];
            assert(o.kind == IR_BLOCK || o.kind == IR_IF);
            if (o.kind == IR_IF) {
                copy_quarters(e, transfer_home(e), a);
            }
            patch(&e->as, o.ends, here(&e->as));
            for (u32 q = 0; q < 4; q++) {
                memory(&e->as, OP_LDR_Q, 4, V29, transfer_home(e) + 16 * q, X8);
                put_quarter(e, V29, home, q);
            }
            break;
        }
        default: binary_quarters(e, kind, t, a, b, home); break;
    }
    e->spills[i] = home;
    e->vinfo[i] = t;
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
    if (is_vector64(t) && kind != IR_IF) {
        lanes64(e, i, kind, type, a, b);
        return;
    }
    switch (kind) {
        case IR_LOAD:
            memory(&e->as, OP_LDR_Q, 4, V29, e->slot_offsets[a], X8);
            break;
        case IR_LOAD_AT: {
            if (a == e->framed) {
                memory(&e->as, OP_LDR_Q, 4, V29, e->framed_offset, X8);
                e->framed = NO_VALUE;
                break;
            }
            u32 p = fetch(e, a, X16);
            release(e, a);
            put(&e->as, OP_LDR_Q | p << 5 | V29);
            break;
        }
        case IR_SPLAT: {
            u32 r = fetch(e, a, X16);
            release(e, a);
            put(&e->as, OP_DUP | (1u << (t & INFO_SCALE)) << 16 | r << 5 | V29);
            break;
        }
        case IR_SHL:
        case IR_SHR: shift_lanes(e, kind, t, a, b); break;
        case IR_NEG_WRAP: {
            u32 r = fetch_lanes(e, a, V30);
            put(&e->as, OP_VNEG | (u32)(t & INFO_SCALE) << 22 | r << 5 | V29);
            break;
        }
        case IR_BIT_NOT: {
            u32 r = fetch_lanes(e, a, V30);
            put(&e->as, OP_VNOT | r << 5 | V29);
            break;
        }
        case IR_STORE: {
            u32 r = fetch_lanes(e, b, V30);
            memory(&e->as, OP_STR_Q, 4, r, e->slot_offsets[a], X8);
            return;
        }
        case IR_STORE_AT: {
            u32 r = fetch_lanes(e, b, V30);
            if (a == e->framed) {
                memory(&e->as, OP_STR_Q, 4, r, e->framed_offset, X8);
                e->framed = NO_VALUE;
                return;
            }
            u32 p = fetch(e, a, X16);
            put(&e->as, OP_STR_Q | p << 5 | r);
            release(e, a);
            return;
        }
        case IR_TOP_BITS: gather_top_bits(e, i, t, a); return;
        case IR_BITCAST: move_lanes(e, V29, a); break;
        case IR_CALL: call(e, i, type, a, b, n, FIRST_INT_ARG); break;
        case IR_RET:
            move_lanes(e, 0, a);
            if (i + 1 < e->fn->count) {
                add_fixup(&e->as, &e->epilogue);
                put(&e->as, OP_B);
            }
            return;
        case IR_IF: {
            e->open = grow(e->open, &e->open_cap, e->nopen + 1, sizeof *e->open);
            Open o = { .target = i, .kind = IR_IF, .result = NO_REG };
            if (e->fused == a) {
                spill_all(e);
                add_fixup(&e->as, &o.skip);
                put(&e->as, OP_B_COND | (u32)(e->fused_cond ^ 1));
                e->fused = NO_VALUE;
            } else {
                u32 r = fetch(e, a, X16);
                release(e, a);
                spill_all(e);
                add_fixup(&e->as, &o.skip);
                put(&e->as, OP_CBZ | r);
            }
            e->open[e->nopen++] = o;
            return;
        }
        case IR_ELSE: {
            Open *o = &e->open[e->nopen - 1];
            move_lanes(e, V29, a);
            add_fixup(&e->as, &o->ends);
            put(&e->as, OP_B);
            patch(&e->as, o->skip, here(&e->as));
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
                add_fixup(&e->as, &o->ends);
                put(&e->as, OP_B);
            }
            return;
        }
        case IR_END: {
            Open o = e->open[--e->nopen];
            assert(o.kind == IR_BLOCK || o.kind == IR_IF);
            if (o.kind == IR_IF) {
                move_lanes(e, V29, a);
            }
            patch(&e->as, o.ends, here(&e->as));
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
            put(&e->as, lane_op(kind, t) | rb << 16 | ra << 5 | V29);
            if (kind == IR_NE) {
                put(&e->as, OP_VNOT | V29 << 5 | V29);
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
        put(&e->as, OP_STP_X_PRE | 0x7Eu << 15 | (r + 1) << 10 | SP << 5 | r);
    }
}

static void
restore_routine_regs(Emit *e)
{
    for (u32 k = sizeof routine_saves; k-- > 0;) {
        u32 r = routine_saves[k];
        put(&e->as, OP_LDP_X_POST | 2u << 15 | (r + 1) << 10 | SP << 5 | r);
    }
}

static void
patch_test(Emit *e, u32 at)
{
    e->as.code[at] |= ((here(&e->as) - at) & 0x3FFFu) << 5;
}

static NOINLINE void
emit_divmod(Emit *e)
{
    save_routine_regs(e);
    put(&e->as, OP_LDP_X | 1u << 10 | X16 << 5 | 0);
    put(&e->as, OP_LDP_X | 3u << 10 | X17 << 5 | 2);
    put(&e->as, OP_ORR | SF | 3u << 16 | 2u << 5 | 9);
    trap_if_zero(e, 9, SF, TRAP_DIVIDE);

    u32 to_unsigned = here(&e->as);
    put(&e->as, OP_TBZ | 1u << 19 | X8);
    put(&e->as, OP_EOR | SF | 3u << 16 | 1u << 5 | 10);
    put(&e->as, OP_ANDS_IMM | SF | logical_field(1, SF) | X8 << 5 | ZR);
    put(&e->as, OP_CSEL | SF | 10u << 16 | (u32)COND_NE << 12 | 1u << 5 | 10);
    shift_imm(e, OP_ASR_IMM, SF, 9, 1, 63);
    negate_if(e, 0, 1, 9);
    shift_imm(e, OP_ASR_IMM, SF, 9, 3, 63);
    negate_if(e, 2, 3, 9);
    patch_test(e, to_unsigned);

    put(&e->as, OP_ORR | SF | 3u << 16 | 1u << 5 | 9);
    u32 to_slow = here(&e->as);
    put(&e->as, OP_CBNZ | SF | 9);
    put(&e->as, OP_UDIV | SF | 2u << 16 | 0u << 5 | 9);
    put(&e->as, OP_MSUB | SF | 2u << 16 | 0u << 10 | 9u << 5 | 4);
    mov(&e->as, SF, 0, 9);
    mov(&e->as, SF, 5, ZR);
    u32 to_select = here(&e->as);
    put(&e->as, OP_B);
    patch_one(&e->as, to_slow, here(&e->as));

    mov(&e->as, SF, 4, ZR);
    mov(&e->as, SF, 5, ZR);
    put(&e->as, OP_MOVZ | 128u << 5 | 9);
    u32 loop = here(&e->as);
    put(&e->as, OP_EXTR | 4u << 16 | 63u << 10 | 5u << 5 | 5);
    put(&e->as, OP_EXTR | 1u << 16 | 63u << 10 | 4u << 5 | 4);
    put(&e->as, OP_EXTR | 0u << 16 | 63u << 10 | 1u << 5 | 1);
    lsl_imm(e, 0, 0, 1);
    put(&e->as, OP_SUBS | SF | 2u << 16 | 4u << 5 | 6);
    put(&e->as, OP_SBCS | SF | 3u << 16 | 5u << 5 | 7);
    put(&e->as, OP_CSEL | SF | 4u << 16 | (u32)COND_HS << 12 | 6u << 5 | 4);
    put(&e->as, OP_CSEL | SF | 5u << 16 | (u32)COND_HS << 12 | 7u << 5 | 5);
    put(&e->as, OP_CSINC | SF | 0u << 16 | (u32)COND_LO << 12 | 0u << 5 | 0);
    put(&e->as, OP_SUBS_IMM | SF | 1u << 10 | 9u << 5 | 9);
    put(&e->as, OP_B_COND | ((loop - here(&e->as)) & 0x7FFFFu) << 5 | COND_NE);
    patch_one(&e->as, to_select, here(&e->as));

    put(&e->as, OP_ANDS_IMM | SF | logical_field(1, SF) | X8 << 5 | ZR);
    put(&e->as, OP_CSEL | SF | 0u << 16 | (u32)COND_NE << 12 | 4u << 5 | 0);
    put(&e->as, OP_CSEL | SF | 1u << 16 | (u32)COND_NE << 12 | 5u << 5 | 1);
    u32 to_store = here(&e->as);
    put(&e->as, OP_TBZ | 1u << 19 | X8);
    shift_imm(e, OP_ASR_IMM, SF, 10, 10, 63);
    u32 to_negate = here(&e->as);
    put(&e->as, OP_TBNZ | X8);
    put(&e->as, OP_BICS | SF | 10u << 16 | 1u << 5 | ZR);
    trap_if(e, COND_MI, TRAP_OVERFLOW);
    patch_test(e, to_negate);
    negate_if(e, 0, 1, 10);
    patch_test(e, to_store);

    put(&e->as, OP_AND_IMM | SF | logical_field(~3ull, SF) | X8 << 5 | X8);
    put(&e->as, OP_STP_X | 1u << 10 | X8 << 5 | 0);
    restore_routine_regs(e);
    put(&e->as, OP_RET);
}

static NOINLINE void
emit_signed_multiply(Emit *e)
{
    save_routine_regs(e);
    put(&e->as, OP_LDP_X | 1u << 10 | X16 << 5 | 0);
    put(&e->as, OP_LDP_X | 3u << 10 | X17 << 5 | 2);
    put(&e->as, OP_EOR | SF | 3u << 16 | 1u << 5 | 10);
    shift_imm(e, OP_ASR_IMM, SF, 10, 10, 63);
    shift_imm(e, OP_ASR_IMM, SF, 9, 1, 63);
    negate_if(e, 0, 1, 9);
    shift_imm(e, OP_ASR_IMM, SF, 9, 3, 63);
    negate_if(e, 2, 3, 9);

    put(&e->as, OP_SUBS_IMM | SF | 1u << 5 | ZR);
    put(&e->as, OP_CCMP_IMM | SF | (u32)COND_NE << 12 | 3u << 5 | 4);
    put(&e->as, OP_CSET | (u32)(COND_NE ^ 1) << 12 | 6);
    put(&e->as, OP_UMULH | 2u << 16 | 1u << 5 | 4);
    put(&e->as, OP_UMULH | 3u << 16 | 0u << 5 | 5);
    put(&e->as, OP_ORR | SF | 5u << 16 | 4u << 5 | 4);
    put(&e->as, OP_SUBS_IMM | SF | 4u << 5 | ZR);
    put(&e->as, OP_CSINC | 6u << 16 | (u32)COND_EQ << 12 | 6u << 5 | 6);
    put(&e->as, OP_MUL | SF | 2u << 16 | 1u << 5 | 4);
    put(&e->as, OP_MADD | SF | 3u << 16 | 4u << 10 | 0u << 5 | 4);
    put(&e->as, OP_UMULH | 2u << 16 | 0u << 5 | 5);
    put(&e->as, OP_ADDS | SF | 4u << 16 | 5u << 5 | 5);
    put(&e->as, OP_CSINC | 6u << 16 | (u32)COND_LO << 12 | 6u << 5 | 6);
    put(&e->as, OP_MUL | SF | 2u << 16 | 0u << 5 | 4);

    mov_imm(&e->as, 9, 0x7FFFFFFFFFFFFFFFull, SF);
    put(&e->as, OP_SUB | SF | 10u << 16 | 9u << 5 | 9);
    put(&e->as, OP_ORN | SF | 10u << 16 | ZR << 5 | 7);
    put(&e->as, OP_SUBS | SF | 4u << 16 | 7u << 5 | ZR);
    put(&e->as, OP_SBCS | SF | 5u << 16 | 9u << 5 | ZR);
    put(&e->as, OP_CSINC | 6u << 16 | (u32)COND_HS << 12 | 6u << 5 | 6);
    negate_if(e, 4, 5, 10);
    put(&e->as, OP_STP_X | 5u << 10 | X8 << 5 | 4);
    put(&e->as, OP_SUBS_IMM | 6u << 5 | ZR);
    restore_routine_regs(e);
    put(&e->as, OP_RET);
}

static void
begin_function(Emit *e, u32 *code)
{
    const IrFunc *fn = e->fn;
    memset(code, 0, PROLOGUE_WORDS * sizeof *code);
    e->as.at = code + PROLOGUE_WORDS;

    const IrSlot *slots = e->slots;
    if (e->result_at != NO_VALUE) {
        memory(&e->as, OP_STRX, 3, X8, e->result_at, X16);
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
                fmov(&e->as, h & 31u, r);
            } else {
                take_from_c(e, info, h, r);
            }
        } else if (slots[p].weight) {
            store_typed(&e->as, e->info[type], r, e->slot_offsets[p]);
        }
    }
}

static void
store_local(Emit *e, u32 type, u32 slot, u32 b)
{
    u32 r;
    if ((b & IR_CONST) && constant_bits(e, b) == 0) {
        r = ZR;
    } else {
        r = fetch(e, b, is_float(e->info[type]) ? V30 : X16);
        release(e, b);
    }
    store_typed(&e->as, e->info[type], r, e->slot_offsets[slot]);
}

static void
load_at(Emit *e, u32 i, u32 type, u32 a)
{
    u8 t = e->info[type];
    if (t & INFO_MEMORY) {
        hold_struct(e, i, a);
        return;
    }
    u32 p = fetch(e, a, X16);
    release(e, a);
    if (t & INFO_PAIR) {
        load_pair(e, i, p, t);
        return;
    }
    if (check_is_flag_optional(e->c, type)) {
        load_flagged(e, i, p, type);
        return;
    }
    u32 d = result(e, i, t);
    put(&e->as, load_op(t) | p << 5 | d);
}

static void
store_at(Emit *e, u32 type, u32 a, u32 b)
{
    u8 t = e->info[type];
    u32 p = fetch(e, a, X16);
    u32 r = (b & IR_CONST) && constant_bits(e, b) == 0
              ? ZR
              : fetch(e, b, is_float(t) ? V30 : X17);
    put(&e->as, store_op(t, r) | p << 5 | r);
    release(e, a);
    release(e, b);
}

static void
return_value(Emit *e, u32 type, u32 a, u32 b)
{
    if (e->info[type] & INFO_PAIR) {
        move_pair(e, 0, 1, a, b);
    } else if (check_is_flag_optional(e->c, type) || check_is_error_pair(e->c, type)) {
        move_halves(e, type, 0, 1, a, b);
    } else if (e->info[type] & INFO_MEMORY) {
        return_struct(e, type, a);
    } else if (type != TYPE_ID_VOID) {
        move_into(e, 0, a, is_float(e->info[type]));
    }
}

static NOINLINE void
describe_frame(Emit *e, u32 entry)
{
    const IrFunc *fn = e->fn;
    u32 saves[PROLOGUE_WORDS];
    u32 nsaves = callee_words(saves, false, e->saved_int, e->saved_float);
    u32 frame = (16 * nsaves + e->locals + e->spill_max + 15) & ~15u;
    bool record = fn->calls || frame || e->wide_record;
    if (!record) {
        return;
    }
    u32 rec = e->context_saved || e->wide_record ? 32 : 16;
    u32 words = PROLOGUE_WORDS - entry;
    u32 popped = e->popped;

    Cfa f = start_frame(e, entry);
    frame_advance(&f, entry + 1);
    frame_cfa_offset(&f, rec);
    frame_saved(&f, X29, rec);
    frame_saved(&f, DWARF_RETURN, rec - 8);
    frame_advance(&f, entry + 2);
    frame_cfa_register(&f, X29);
    frame_advance(&f, entry + words);
    u32 below = rec;
    for (u32 r = 19; r < 29; r += 2) {
        if (e->saved_int & (3u << r)) {
            below += 16;
            frame_saved(&f, r, below);
            frame_saved(&f, r + 1, below - 8);
        }
    }
    for (u32 r = 8; r < 16; r += 2) {
        if (e->saved_float & (3u << r)) {
            below += 16;
            frame_saved(&f, DWARF_D0 + r, below);
            frame_saved(&f, DWARF_D0 + r + 1, below - 8);
        }
    }
    frame_advance(&f, popped);
    frame_remember(&f);
    frame_cfa(&f, DWARF_SP, 0);
    frame_restore(&f, X29);
    frame_restore(&f, DWARF_RETURN);
    if (e->stubs < here(&e->as)) {
        frame_advance(&f, e->stubs);
        frame_recall(&f);
    }
    end_frame(e, f);
}

static NOINLINE void
place_trap_sites(Emit *e)
{
    for (u32 k = 0; k < e->nsites; k++) {
        patch(&e->as, e->sites[k].fixups, here(&e->as));
        mark_row(e, e->sites[k].token);
        put(&e->as, OP_BRK | (u32)e->sites[k].kind << 5);
    }
}

static u32
finish_function(Emit *e, u32 *code)
{
    const IrFunc *fn = e->fn;
    u32 saves[PROLOGUE_WORDS];
    u32 nsaves = callee_words(saves, false, e->saved_int, e->saved_float);
    u32 frame = (16 * nsaves + e->locals + e->spill_max + 15) & ~15u;
    bool record = fn->calls || frame || e->wide_record;
    assert(frame || !(e->divisions | e->products));
    bool wide = e->context_saved || e->wide_record;
    patch(&e->as, e->epilogue, here(&e->as));
    e->as.at += callee_words(e->as.at, true, e->saved_int, e->saved_float);
    if (record) {
        if (frame) {
            put(&e->as, OP_RESET_SP);
        }
        put(&e->as, wide ? OP_POP_CONTEXT_FRAME : OP_POP_FRAME);
    }
    e->popped = here(&e->as);
    put(&e->as, OP_RET);
    e->stubs = here(&e->as);
    if (UNLIKELY(e->symfile != NULL)) {
        open_stubs(e);
    }
    if (e->divisions) {
        patch(&e->as, e->divisions, here(&e->as));
        emit_divmod(e);
    }
    if (e->products) {
        patch(&e->as, e->products, here(&e->as));
        emit_signed_multiply(e);
    }

    static_assert(NTRAPS <= 32, "every trap kind needs a bit in `kinds`");
    u32 kinds = 0;
    for (u32 k = 1; k < NTRAPS; k++) {
        kinds |= (u32)(e->traps[k] != 0) << k;
    }
    for (; kinds; kinds &= kinds - 1) {
        u32 k = (u32)__builtin_ctz(kinds);
        patch(&e->as, e->traps[k], here(&e->as));
        put(&e->as, OP_BRK | k << 5);
    }
    if (UNLIKELY(e->symfile != NULL)) {
        place_trap_sites(e);
    }

    if (frame >= FRAME_LIMIT) {
        e->as.error = "a function's frame is larger than 16 MB";
        return 0;
    }
    u32 probe = 0;
    if (frame >= PROBE_INTERVAL) {
        probe = here(&e->as);
        mov_imm(&e->as, X16, frame / PROBE_INTERVAL, SF);
        put(&e->as, OP_SUB_SP_HIGH | 1 << 10);
        put(&e->as, OP_STR_ZR_SP);
        put(&e->as, OP_SUBS_IMM | SF | 1 << 10 | X16 << 5 | X16);
        put(&e->as, OP_B_COND | ((u32)-3 & 0x7FFFFu) << 5 | COND_NE);
        if (frame % PROBE_INTERVAL) {
            put(&e->as, OP_SUB_SP | (frame % PROBE_INTERVAL) << 10);
            put(&e->as, OP_STR_ZR_SP);
        }
        put(&e->as, OP_B);
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
        u32 back = here(&e->as) - 1;
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
                put(&e->as,
                    unit_loads[scale] | (offset >> scale) << 10 | X29 << 5 | X16);
            } else {
                assert(!(regs & REGS_FLOAT));
                r = from + u;
            }
            put(&e->as,
                unit_stores[scale] | ((to + step) >> scale) << 10 | SP << 5 | r);
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
        e->as.error = "a callback for C needs over 4064 bytes of stack "
                      "arguments";
        return;
    }

    u32 frame = (out.stack + 15) & ~15u;
    if (frame) {
        put(&e->as, OP_PUSH_FRAME);
        put(&e->as, OP_SET_FRAME);
        put(&e->as, OP_SUB_SP | frame << 10);
        ArgCursor from = { 0, 0, 0 };
        ArgCursor to = { FIRST_INT_ARG, 0, 0 };
        store_stack_args(e, params, &from, &to);
    }
    for (u32 r = in.ints < 7 ? in.ints : 7; r-- > 0;) {
        mov(&e->as, SF, r + 1, r);
    }

    u64 context = (u64)(uintptr_t)(n->entries + e->c->decls.count);
    adrp(&e->as, 0, context);
    put(&e->as, OP_LDRX | (u32)((context & 0xFFF) >> 3) << 10);
    u64 entry = (u64)(uintptr_t)(n->entries + decl);
    adrp(&e->as, X16, entry);
    put(&e->as, OP_LDR_X16_X16 | (u32)((entry & 0xFFF) >> 3) << 10);
    if (!frame) {
        put(&e->as, OP_BR | X16 << 5);
        return;
    }
    put(&e->as, OP_BLR_X16);
    put(&e->as, OP_RESET_SP);
    put(&e->as, OP_POP_FRAME);
    u32 popped = here(&e->as);
    put(&e->as, OP_RET);
    if (e->symfile) {
        Cfa f = start_frame(e, 0);
        frame_advance(&f, 1);
        frame_cfa_offset(&f, 16);
        frame_saved(&f, X29, 16);
        frame_saved(&f, DWARF_RETURN, 8);
        frame_advance(&f, 2);
        frame_cfa_register(&f, X29);
        frame_advance(&f, popped);
        frame_cfa(&f, DWARF_SP, 0);
        frame_restore(&f, X29);
        frame_restore(&f, DWARF_RETURN);
        end_frame(e, f);
    }
}
