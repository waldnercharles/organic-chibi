static const char NO_FLOATS[] = "the x86-64 backend doesn't support floats yet";
static const char NO_MEMORY[] = "the x86-64 backend doesn't support arrays, "
                                "structs or slices yet";
static const char
    NO_C_CALLS[] = "the x86-64 backend doesn't support calls into C yet";
static const char
    NO_DIVISION[] = "the x86-64 backend doesn't support division, "
                    "shifts or bit counts yet";
static const char NO_PAIRS[] = "the x86-64 backend doesn't support optionals, "
                               "errors or tuples yet";

static void
refuse(Emit *e, const char *why)
{
    if (!e->as.error) {
        e->as.error = why;
    }
}

static void
trap_if(Emit *e, u8 cond, u8 kind)
{
    jump_if(&e->as, &e->traps[kind], cond);
}

static void
trap_if_zero(Emit *e, u32 reg, u32 w, u8 kind)
{
    test_rr(&e->as, w, reg, reg);
    trap_if(e, CC_E, kind);
}

static void
trap_unless_zero(Emit *e, u32 reg, u32 w, u8 kind)
{
    test_rr(&e->as, w, reg, reg);
    trap_if(e, CC_NE, kind);
}

static NOINLINE void
data_address(Emit *e, u32 i, u32 type, u32 offset)
{
    (void)i;
    (void)type;
    (void)offset;
    refuse(e, NO_MEMORY);
}

static NOINLINE void
global_address(Emit *e, u32 i, u32 type, u32 offset)
{
    u32 d = result(e, i, e->info[type]);
    mov_imm(&e->as, d, (u64)(uintptr_t)(e->globals + offset), 1);
}

static void
swap(u32 *a, u32 *b)
{
    u32 t = *a;
    *a = *b;
    *b = t;
}

static bool
imm_operand(const Emit *e, u32 x, u32 w, int32_t *imm)
{
    if (!(x & IR_CONST)) {
        return false;
    }
    u64 v = constant_bits(e, x);
    if (!w) {
        *imm = (int32_t)(u32)v;
        return true;
    }
    if (!fits32((int64_t)v)) {
        return false;
    }
    *imm = (int32_t)v;
    return true;
}

static void
normalize(Emit *e, u8 t, u32 d, u32 n)
{
    if (is_small(t)) {
        extend(&e->as, bits_of(t), is_signed(t), d, n);
    } else {
        mov_rr(&e->as, width(t), d, n);
    }
}

static void
check_small(Emit *e, u8 t, u32 d)
{
    if (is_signed(t)) {
        extend(&e->as, bits_of(t), true, SCRATCH, d);
        alu_rr(&e->as, ALU_CMP, 0, d, SCRATCH);
        trap_if(e, CC_NE, TRAP_OVERFLOW);
        return;
    }
    alu_imm(&e->as, ALU_CMP, 0, d, bits_of(t) == 8 ? 0xFF : 0xFFFF);
    trap_if(e, CC_A, TRAP_OVERFLOW);
}

static void
binary(Emit *e, u32 alu, u32 w, u32 d, u32 ra, u32 rb, const int32_t *imm)
{
    if (imm) {
        mov_rr(&e->as, w, d, ra);
        alu_imm(&e->as, alu, w, d, *imm);
        return;
    }
    if (d == rb && d != ra) {
        if (alu != ALU_SUB) {
            alu_rr(&e->as, alu, w, d, ra);
            return;
        }
        mov_rr(&e->as, w, SCRATCH, ra);
        alu_rr(&e->as, alu, w, SCRATCH, rb);
        mov_rr(&e->as, w, d, SCRATCH);
        return;
    }
    mov_rr(&e->as, w, d, ra);
    alu_rr(&e->as, alu, w, d, rb);
}

static void
add_sub(Emit *e, u32 i, u8 kind, u32 type, u32 a, u32 b)
{
    u8 t = e->info[type];
    if (is_float(t)) {
        refuse(e, NO_FLOATS);
        return;
    }

    bool sub = kind == IR_SUB || kind == IR_SUB_WRAP;
    bool checked = kind == IR_ADD || kind == IR_SUB;
    bool small = is_small(t);
    if (!sub && (a & IR_CONST) && !(b & IR_CONST)) {
        swap(&a, &b);
    }
    u32 w = width(t);
    int32_t imm = 0;
    bool is_imm = imm_operand(e, b, w, &imm);
    u32 ra = fetch(e, a, SCRATCH);
    u32 rb = is_imm ? 0 : fetch(e, b, SCRATCH2);
    release(e, a);
    release(e, b);
    u32 d = result(e, i, t);
    binary(e, sub ? ALU_SUB : ALU_ADD, w, d, ra, rb, is_imm ? &imm : NULL);
    if (checked && !small) {
        trap_if(e, is_signed(t) ? CC_O : CC_B, TRAP_OVERFLOW);
    } else if (small && checked) {
        check_small(e, t, d);
    } else if (small) {
        normalize(e, t, d, d);
    }
}

static void
product(Emit *e, u32 w, u32 d, u32 ra, u32 rb, const int32_t *imm)
{
    if (imm) {
        imul_imm(&e->as, w, d, ra, *imm);
    } else if (d == ra) {
        imul_rr(&e->as, w, d, rb);
    } else if (d == rb) {
        imul_rr(&e->as, w, d, ra);
    } else {
        mov_rr(&e->as, w, d, ra);
        imul_rr(&e->as, w, d, rb);
    }
}

static void
free_reg(Emit *e, u32 reg, u32 avoid)
{
    u32 v = e->holders[reg];
    if (v == NO_VALUE) {
        return;
    }
    u32 spare = e->free_int & ~avoid;
    if (!spare) {
        spill_reg(e, reg);
        return;
    }
    u32 to = (u32)__builtin_ctz(spare);
    mov_rr(&e->as, 1, to, reg);
    e->free_int = (e->free_int & ~(1u << to)) | 1u << reg;
    e->holders[to] = v;
    e->holders[reg] = NO_VALUE;
    e->regs[v] = (u8)to;
}

static void
wide_product(Emit *e, u32 a, u32 b)
{
    u32 avoid = 1u << RAX | 1u << RDX;
    free_reg(e, RAX, avoid);
    free_reg(e, RDX, avoid);
    u32 ra = fetch(e, a, SCRATCH);
    u32 rb = fetch(e, b, SCRATCH2);
    mov_rr(&e->as, 1, RAX, ra);
    unary_r(&e->as, UNARY_MUL, 1, rb);
    release(e, a);
    release(e, b);
}

static void
check_high_half(Emit *e, u32 d)
{
    mov_rr(&e->as, 1, SCRATCH, d);
    shift_imm(&e->as, SHIFT_SHR, 1, SCRATCH, 32);
}

static void
multiply(Emit *e, u32 i, u8 kind, u32 type, u32 a, u32 b)
{
    u8 t = e->info[type];
    if (is_float(t)) {
        refuse(e, NO_FLOATS);
        return;
    }

    bool wrap = kind == IR_MUL_WRAP;
    bool small = is_small(t);
    bool s = is_signed(t);
    u32 w = width(t);
    if (!wrap && !small && !s && w) {
        wide_product(e, a, b);
        trap_if(e, CC_O, TRAP_OVERFLOW);
        mov_rr(&e->as, 1, result(e, i, t), RAX);
        return;
    }
    if ((a & IR_CONST) && !(b & IR_CONST)) {
        swap(&a, &b);
    }
    u32 op_w = !wrap && !small && !s ? 1 : w;
    int32_t imm = 0;
    bool is_imm = imm_operand(e, b, op_w, &imm);
    u32 ra = fetch(e, a, SCRATCH);
    u32 rb = is_imm ? 0 : fetch(e, b, SCRATCH2);
    release(e, a);
    release(e, b);
    u32 d = result(e, i, t);
    product(e, op_w, d, ra, rb, is_imm ? &imm : NULL);
    if (wrap) {
        if (small) {
            normalize(e, t, d, d);
        }
    } else if (small) {
        check_small(e, t, d);
    } else if (s) {
        trap_if(e, CC_O, TRAP_OVERFLOW);
    } else {
        check_high_half(e, d);
        trap_if(e, CC_NE, TRAP_OVERFLOW);
    }
}

static NOINLINE void
divide(Emit *e, u32 i, u8 kind, u32 type, u32 a, u32 b)
{
    (void)i;
    (void)kind;
    (void)type;
    (void)a;
    (void)b;
    refuse(e, NO_DIVISION);
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
bitwise(Emit *e, u32 i, u8 kind, u32 type, u32 a, u32 b)
{
    u8 t = e->info[type];
    u32 w = width(t);
    if ((a & IR_CONST) && !(b & IR_CONST)) {
        swap(&a, &b);
    }
    int32_t imm = 0;
    bool is_imm = imm_operand(e, b, w, &imm);
    u32 ra = fetch(e, a, SCRATCH);
    u32 rb = is_imm ? 0 : fetch(e, b, SCRATCH2);
    release(e, a);
    release(e, b);
    if (kind == IR_AND && tests_zero(e, i)) {
        if (is_imm) {
            test_imm(&e->as, w, ra, imm);
        } else {
            test_rr(&e->as, w, ra, rb);
        }
        e->tested = i + 1;
        return;
    }
    u32 d = result(e, i, t);
    u32 alu = kind == IR_AND ? ALU_AND : kind == IR_OR ? ALU_OR : ALU_XOR;
    binary(e, alu, w, d, ra, rb, is_imm ? &imm : NULL);
}

static NOINLINE void
shift(Emit *e, u32 i, u8 kind, u32 type, u32 a, u32 b)
{
    (void)i;
    (void)kind;
    (void)type;
    (void)a;
    (void)b;
    refuse(e, NO_DIVISION);
}

static u8
compare_cond(const Emit *e, u8 kind, u32 type)
{
    u8 t = e->info[type];
    if (kind == IR_EQ) {
        return CC_E;
    }
    if (kind == IR_NE) {
        return CC_NE;
    }
    if (is_signed(t)) {
        static const u8 conds[] = { CC_L, CC_LE, CC_G, CC_GE };
        return conds[kind - IR_LT];
    }
    static const u8 conds[] = { CC_B, CC_BE, CC_A, CC_AE };
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
    setcc(&e->as, cond, d);
}

static void
compare(Emit *e, u32 i, u8 kind, u32 type, u32 a, u32 b)
{
    u8 t = e->info[type];
    if (e->tested == i) {
        e->tested = NO_VALUE;
    } else if (is_float(t)) {
        refuse(e, NO_FLOATS);
        return;
    } else {
        if ((a & IR_CONST) && !(b & IR_CONST)) {
            swap(&a, &b);
            static const u8 mirrored[] = { IR_GT, IR_GE, IR_LT, IR_LE };
            kind = kind >= IR_LT ? mirrored[kind - IR_LT] : kind;
        }
        u32 w = width(t);
        int32_t imm = 0;
        bool is_imm = imm_operand(e, b, w, &imm);
        u32 ra = fetch(e, a, SCRATCH);
        u32 rb = is_imm ? 0 : fetch(e, b, SCRATCH2);
        release(e, a);
        release(e, b);
        if (is_imm) {
            alu_imm(&e->as, ALU_CMP, w, ra, imm);
        } else {
            alu_rr(&e->as, ALU_CMP, w, ra, rb);
        }
    }

    flag_result(e, i, compare_cond(e, kind, type));
}

static void
unary(Emit *e, u32 i, u8 kind, u32 type, u32 a)
{
    u8 t = e->info[type];
    if (is_float(t)) {
        refuse(e, NO_FLOATS);
        return;
    }

    u32 ra = fetch(e, a, SCRATCH);
    release(e, a);
    u32 d = result(e, i, t);
    u32 w = width(t);
    bool small = is_small(t);
    switch (kind) {
        case IR_NOT:
            mov_rr(&e->as, 0, d, ra);
            alu_imm(&e->as, ALU_XOR, 0, d, 1);
            break;
        case IR_BIT_NOT:
            mov_rr(&e->as, w, d, ra);
            unary_r(&e->as, UNARY_NOT, w, d);
            if (small && !is_signed(t)) {
                normalize(e, t, d, d);
            }
            break;
        case IR_NEG_WRAP:
            mov_rr(&e->as, w, d, ra);
            unary_r(&e->as, UNARY_NEG, w, d);
            if (small) {
                normalize(e, t, d, d);
            }
            break;
        case IR_NEG:
            if (!is_signed(t)) {
                trap_unless_zero(e, ra, w, TRAP_OVERFLOW);
                mov_imm(&e->as, d, 0, 0);
            } else if (small) {
                mov_rr(&e->as, 0, d, ra);
                unary_r(&e->as, UNARY_NEG, 0, d);
                check_small(e, t, d);
            } else {
                mov_rr(&e->as, w, d, ra);
                unary_r(&e->as, UNARY_NEG, w, d);
                trap_if(e, CC_O, TRAP_OVERFLOW);
            }
            break;
        default: assert(false && "not a unary instruction");
    }
}

static NOINLINE void
count_bits(Emit *e, u32 i, u8 kind, u32 type, u32 a)
{
    (void)i;
    (void)kind;
    (void)type;
    (void)a;
    refuse(e, NO_DIVISION);
}

static void
test_overflow(Emit *e, u32 i, u8 kind, u32 type, u32 a, u32 b)
{
    u8 t = e->info[type];
    u32 w = width(t);
    bool s = is_signed(t);
    bool small = is_small(t);
    bool mul = kind == IR_MUL_OVERFLOWS;
    bool sub = kind == IR_SUB_OVERFLOWS;
    if (mul && !small && !s && w) {
        wide_product(e, a, b);
        flag_result(e, i, CC_O);
        return;
    }
    if (!sub && (a & IR_CONST) && !(b & IR_CONST)) {
        swap(&a, &b);
    }
    u32 op_w = mul && !small && !s ? 1 : w;
    int32_t imm = 0;
    bool is_imm = imm_operand(e, b, op_w, &imm);
    u32 ra = fetch(e, a, SCRATCH);
    u32 rb = is_imm ? 0 : fetch(e, b, SCRATCH2);
    release(e, a);
    release(e, b);

    if (mul) {
        product(e, op_w, SCRATCH, ra, rb, is_imm ? &imm : NULL);
    } else {
        binary(e, sub ? ALU_SUB : ALU_ADD, w, SCRATCH, ra, rb, is_imm ? &imm : NULL);
    }
    u8 cond = s ? CC_O : CC_B;
    if (small) {
        if (s) {
            extend(&e->as, bits_of(t), true, SCRATCH2, SCRATCH);
            alu_rr(&e->as, ALU_CMP, 0, SCRATCH, SCRATCH2);
            cond = CC_NE;
        } else {
            alu_imm(&e->as, ALU_CMP, 0, SCRATCH, bits_of(t) == 8 ? 0xFF : 0xFFFF);
            cond = CC_A;
        }
    } else if (mul && !s) {
        shift_imm(&e->as, SHIFT_SHR, 1, SCRATCH, 32);
        cond = CC_NE;
    }
    flag_result(e, i, cond);
}

static void
move_narrower(Emit *e, u8 tt, u8 from, u32 d, u32 ra)
{
    if (!width(tt) && width(from)) {
        extend(&e->as, 32, false, d, ra);
    } else {
        mov_rr(&e->as, width(tt), d, ra);
    }
}

static void
convert(Emit *e, u32 i, u32 to, u32 a)
{
    u8 tt = e->info[to];
    u8 from = operand_info(e, a);
    if (is_float(from) || is_float(tt)) {
        refuse(e, NO_FLOATS);
        return;
    }
    u32 ra = fetch(e, a, SCRATCH);
    release(e, a);
    u32 d = result(e, i, tt);
    if (width(tt) && !width(from)) {
        extend(&e->as, 32, is_signed(from), d, ra);
    } else {
        move_narrower(e, tt, from, d, ra);
    }
}

enum
{
    JUMP = 0xFF,
};

static void
compare_value(Emit *e, u32 reg, u64 v, u32 w)
{
    int64_t sv = w ? (int64_t)v : (int32_t)(u32)v;
    if (fits32(sv)) {
        alu_imm(&e->as, ALU_CMP, w, reg, (int32_t)sv);
        return;
    }
    mov_imm(&e->as, RAX, v, 1);
    alu_rr(&e->as, ALU_CMP, 1, reg, RAX);
}

static void
subtract_value(Emit *e, u32 d, u32 n, u64 v, u32 w)
{
    mov_rr(&e->as, w, d, n);
    int64_t sv = w ? (int64_t)v : (int32_t)(u32)v;
    if (fits32(sv)) {
        alu_imm(&e->as, ALU_SUB, w, d, (int32_t)sv);
        return;
    }
    mov_imm(&e->as, RAX, v, 1);
    alu_rr(&e->as, ALU_SUB, 1, d, RAX);
}

static void
branch_to(Emit *e, u8 cond, u32 target, u8 trap)
{
    if (cond == JUMP) {
        put(&e->as, OP_JMP32);
    } else {
        opcode(&e->as, OP_JCC32 | cond);
    }
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
    put32(&e->as, 0);
}

static void
test_range(Emit *e, u32 r, u8 t, Range range, u8 trap)
{
    u32 w = width(t);
    if (range.lo == range.hi) {
        compare_value(e, r, range.lo, w);
        branch_to(e, CC_E, range.target, trap);
        return;
    }
    subtract_value(e, SCRATCH2, r, range.lo, w);
    compare_value(e, SCRATCH2, range.hi - range.lo, w);
    branch_to(e, CC_BE, range.target, trap);
}

static void
search(Emit *e, u32 r, u8 t, const Range *ranges, u32 n, u32 fallback, u8 trap)
{
    if (n <= 3) {
        for (u32 k = 0; k < n; k++) {
            test_range(e, r, t, ranges[k], trap);
        }
        branch_to(e, JUMP, fallback, trap);
        return;
    }
    u32 mid = n / 2;
    compare_value(e, r, ranges[mid].lo, width(t));
    u32 left = 0;
    jump_if(&e->as, &left, is_signed(t) ? CC_L : CC_B);
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
        const u32 *v = runs + 1 + 4 * k;
        e->ranges[k] = (Range){ v[0] | (u64)v[1] << 32, v[2] | (u64)v[3] << 32, 0 };
    }
    u32 first = e->nbranches;
    search(e, d, t, e->ranges, n, NO_VALUE, TRAP_RANGE);
    for (u32 k = first; k < e->nbranches; k++) {
        patch_one(&e->as, e->branches[k].at, here(&e->as));
    }
    e->nbranches = first;
}

static void
narrow(Emit *e, u32 i, u32 to, u32 a)
{
    u8 tt = e->info[to];
    u8 from = operand_info(e, a);
    if (is_float(from)) {
        refuse(e, NO_FLOATS);
        return;
    }
    u32 to_bits = bits_of(tt);
    bool to_signed = is_signed(tt);
    u32 ra = fetch(e, a, SCRATCH);
    release(e, a);
    u32 w = width(from);
    u32 from_bits = bits_of(from);
    if (is_signed(from)) {
        if (to_signed && to_bits >= from_bits) {
        } else if (to_bits >= from_bits) {
            test_rr(&e->as, w, ra, ra);
            trap_if(e, CC_S, TRAP_RANGE);
        } else {
            if (w && to_signed) {
                sign_extend_wide(&e->as, to_bits, SCRATCH2, ra);
            } else {
                extend(&e->as, to_bits, to_signed, SCRATCH2, ra);
            }
            alu_rr(&e->as, ALU_CMP, w, ra, SCRATCH2);
            trap_if(e, CC_NE, TRAP_RANGE);
        }
    } else if (to_bits - to_signed < from_bits) {
        mov_rr(&e->as, w, SCRATCH2, ra);
        shift_imm(&e->as, SHIFT_SHR, w, SCRATCH2, to_bits - to_signed);
        trap_if(e, CC_NE, TRAP_RANGE);
    }
    u32 d = result(e, i, tt);
    if (width(tt) && !w) {
        extend(&e->as, 32, is_signed(from) && to_signed, d, ra);
    } else {
        move_narrower(e, tt, from, d, ra);
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
    u8 from = operand_info(e, a);
    if (is_float(tt) || is_float(from)) {
        refuse(e, NO_FLOATS);
        return;
    }
    u32 ra = fetch(e, a, SCRATCH);
    release(e, a);
    u32 d = result(e, i, tt);
    if (is_small(tt)) {
        extend(&e->as, bits_of(tt), is_signed(tt), d, ra);
    } else {
        move_narrower(e, tt, from, d, ra);
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
    if (is_float(from) || is_float(tt)) {
        refuse(e, NO_FLOATS);
        return;
    }
    if (bits_of(tt) == 64 && can_take_over(e, i, a)) {
        take_over(e, a, i, tt);
        return;
    }
    truncate_to(e, i, to, a);
}

static void
keep_or_copy(Emit *e, u32 i, u32 type, u32 a, u32 ra)
{
    if (can_take_over(e, i, a)) {
        take_over(e, a, i, e->info[type]);
        return;
    }
    release(e, a);
    mov_rr(&e->as, 1, result(e, i, e->info[type]), ra);
}

static NOINLINE void
check_align(Emit *e, u32 i, u32 type, u32 a, u32 align)
{
    u32 ra = fetch(e, a, SCRATCH);
    test_imm(&e->as, 1, ra, (int32_t)(align - 1));
    trap_if(e, CC_NE, TRAP_ALIGN);
    keep_or_copy(e, i, type, a, ra);
}

static NOINLINE void
check_nil(Emit *e, u32 i, u32 type, u32 a)
{
    u32 ra = fetch(e, a, SCRATCH);
    trap_if_zero(e, ra, 1, TRAP_NIL);
    keep_or_copy(e, i, type, a, ra);
}

static NOINLINE void
check_assert(Emit *e, u32 a)
{
    u32 ra = fetch(e, a, SCRATCH);
    trap_if_zero(e, ra, 0, TRAP_ASSERT);
    release(e, a);
}

static NOINLINE void
offset_address(Emit *e, u32 i, u8 info, u32 a, u64 offset)
{
    if (!fits32((int64_t)offset)) {
        refuse(e, NO_MEMORY);
        return;
    }
    u32 ra = fetch(e, a, SCRATCH);
    release(e, a);
    u32 d = result(e, i, info);
    if (offset == 0) {
        mov_rr(&e->as, 1, d, ra);
    } else {
        lea(&e->as, d, ra, (int32_t)offset);
    }
}

static NOINLINE void
index_address(Emit *e, u32 i, u32 type, u32 a, u32 b)
{
    (void)i;
    (void)type;
    (void)a;
    (void)b;
    refuse(e, NO_MEMORY);
}

static NOINLINE void
index_len(Emit *e, u32 i, u32 type, u32 a, u32 b)
{
    (void)i;
    (void)type;
    (void)a;
    (void)b;
    refuse(e, NO_MEMORY);
}

static NOINLINE void
slice(Emit *e, u32 i, u8 kind, u32 type, u32 a, u32 b)
{
    (void)i;
    (void)kind;
    (void)type;
    (void)a;
    (void)b;
    refuse(e, NO_MEMORY);
}

static NOINLINE void
zero_memory(Emit *e, u32 type, u32 a)
{
    (void)type;
    (void)a;
    refuse(e, NO_MEMORY);
}

static NOINLINE void
copy_memory(Emit *e, u32 type, u32 a, u32 b)
{
    (void)type;
    (void)a;
    (void)b;
    refuse(e, NO_MEMORY);
}

static void
move_into(Emit *e, u32 d, u32 x, bool fl)
{
    u8 t = operand_info(e, x);
    if (x & IR_CONST) {
        load_immediate(&e->as, d, constant_bits(e, x), t);
        return;
    }
    if (e->regs[x] != NO_REG) {
        move_reg(&e->as, d, e->regs[x] & 31u, fl);
    } else {
        load_spill(&e->as, d, e->spills[x], fl);
    }
    release(e, x);
}

static NOINLINE void
move_halves(Emit *e, u32 type, u32 d0, u32 d1, u32 a, u32 b)
{
    (void)type;
    (void)d0;
    (void)d1;
    (void)a;
    (void)b;
    refuse(e, NO_PAIRS);
}

static NOINLINE void
store_flagged(Emit *e, u32 type, u32 a, u32 b)
{
    (void)type;
    (void)a;
    (void)b;
    refuse(e, NO_PAIRS);
}

static NOINLINE void
store_pair(Emit *e, u32 a, u32 b)
{
    (void)a;
    (void)b;
    refuse(e, NO_PAIRS);
}

static NOINLINE u32
by_pointer(const Emit *e, u32 type)
{
    (void)e;
    (void)type;
    return 1;
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

static NOINLINE void
callback_address(Emit *e, u32 i, u32 type, u32 k, u32 decl, const Native *n)
{
    (void)i;
    (void)type;
    (void)k;
    (void)decl;
    (void)n;
    refuse(e, NO_C_CALLS);
}

static NOINLINE void
c_global_address(Emit *e, u32 i, u32 type, u32 decl, const Native *n)
{
    (void)i;
    (void)type;
    (void)decl;
    (void)n;
    refuse(e, NO_C_CALLS);
}

static bool
plain_int(const Emit *e, u32 type)
{
    u8 t = e->info[type];
    return !(t & (INFO_MEMORY | INFO_PAIR | INFO_UNSUPPORTED)) && !is_float(t) &&
           !check_is_flag_optional(e->c, type) && !check_is_error_pair(e->c, type);
}

static void
pass_args(Emit *e, const u32 *args)
{
    u32 stack = 0;
    for (u32 j = 1; j <= args[0]; j++) {
        if (FIRST_INT_ARG + j - 1 < INT_ARGS) {
            continue;
        }
        u32 r = fetch(e, args[j], SCRATCH);
        release(e, args[j]);
        rm(&e->as, 0, OP_MOV_RM_R, 1, r, RSP, (int32_t)(8 * stack++), false);
    }
    for (u32 j = 1; j <= args[0] && FIRST_INT_ARG + j - 1 < INT_ARGS; j++) {
        move_into(e, INT_ARG_REGS[FIRST_INT_ARG + j - 1], args[j], false);
    }
}

static ALWAYS_INLINE void
call(Emit *e, u32 i, u32 type, u32 decl, u32 b, const Native *n, u32 ints)
{
    if (!ints) {
        refuse(e, NO_C_CALLS);
        return;
    }
    const u32 *args = e->ir->extra + e->fn->first_extra + b;
    if (type != TYPE_ID_VOID && !plain_int(e, type)) {
        refuse(e, NO_PAIRS);
        return;
    }
    for (u32 j = 1; j <= args[0]; j++) {
        u8 t = operand_info(e, args[j]);
        if (t & (INFO_MEMORY | INFO_PAIR) || is_float(t)) {
            refuse(e, is_float(t) ? NO_FLOATS : NO_MEMORY);
            return;
        }
    }

    save_for_call(e, ~e->free_int & INT_POOL, false);
    save_for_call(e, ~e->free_float & FLOAT_POOL, true);
    pass_args(e, args);
    if (e->called || inside_loop(e)) {
        rm(&e->as, 0, OP_MOV_R_RM, 1, RDI, RBP, -8, false);
        e->context_saved = true;
    }
    e->called = true;
    call_through(&e->as, (u64)(uintptr_t)(n->entries + decl));

    if (type != TYPE_ID_VOID) {
        u32 d = result(e, i, e->info[type]);
        mov_rr(&e->as, 1, d, RAX);
    }
}

static void
load_at(Emit *e, u32 i, u32 type, u32 a)
{
    u8 t = e->info[type];
    if (!plain_int(e, type)) {
        refuse(e, is_float(t) ? NO_FLOATS : NO_MEMORY);
        return;
    }
    u32 p = fetch(e, a, SCRATCH);
    release(e, a);
    u32 d = result(e, i, t);
    bool s = is_signed(t);
    switch (bits_of(t)) {
        case 8:
            rm(&e->as, 0, s ? OP_MOVSX8 : OP_MOVZX8, 0, d, p, 0, false);
            break;
        case 16:
            rm(&e->as, 0, s ? OP_MOVSX16 : OP_MOVZX16, 0, d, p, 0, false);
            break;
        case 32: rm(&e->as, 0, OP_MOV_R_RM, 0, d, p, 0, false); break;
        default: rm(&e->as, 0, OP_MOV_R_RM, 1, d, p, 0, false); break;
    }
}

static void
store_at(Emit *e, u32 type, u32 a, u32 b)
{
    u8 t = e->info[type];
    if (!plain_int(e, type)) {
        refuse(e, is_float(t) ? NO_FLOATS : NO_MEMORY);
        return;
    }
    u32 p = fetch(e, a, SCRATCH);
    u32 r = fetch(e, b, SCRATCH2);
    switch (bits_of(t)) {
        case 8: rm(&e->as, 0, OP_MOV_RM_R8, 0, r, p, 0, byte_reg(r)); break;
        case 16: rm(&e->as, PREFIX_16, OP_MOV_RM_R, 0, r, p, 0, false); break;
        case 32: rm(&e->as, 0, OP_MOV_RM_R, 0, r, p, 0, false); break;
        default: rm(&e->as, 0, OP_MOV_RM_R, 1, r, p, 0, false); break;
    }
    release(e, a);
    release(e, b);
}

static NOINLINE void
open_switch(Emit *e, u32 i, u32 type, u32 a, u32 b)
{
    u8 t = e->info[type];
    u32 w = width(t);
    u64 mask = w ? ~0ull : 0xFFFFFFFFull;
    const u32 *x = e->ir->extra + e->fn->first_extra + b;
    u32 n = x[0];
    u32 fallback = x[1];
    u32 r = fetch(e, a, SCRATCH);
    release(e, a);
    spill_all(e);
    mov_rr(&e->as, 1, SCRATCH, r);

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
        const u32 *v = x + 2 + 5 * k;
        Range range = { v[0] | (u64)v[1] << 32, v[2] | (u64)v[3] << 32, v[4] };
        e->ranges[k] = range;
        count += ((range.hi - range.lo) & mask) + 1;
    }

    u64 span = n ? ((e->ranges[n - 1].hi - e->ranges[0].lo) & mask) + 1 : 0;
    if (n >= 4 && span != 0 && span <= 8ull * n && 10 * count >= 4 * span) {
        o.min = e->ranges[0].lo;
        o.span = (u32)span;
        subtract_value(e, SCRATCH2, SCRATCH, o.min, w);
        compare_value(e, SCRATCH2, span - 1, w);
        branch_to(e, CC_A, fallback, TRAP_UNREACHABLE);
        u32 disp = lea_rip(&e->as, SCRATCH);
        load_entry(&e->as, SCRATCH2, SCRATCH, SCRATCH2);
        alu_rr(&e->as, ALU_ADD, 1, SCRATCH, SCRATCH2);
        jump_reg(&e->as, SCRATCH);
        o.table = here(&e->as);
        u32 rel = o.table - (disp + 4);
        memcpy(e->as.code + disp, &rel, 4);
        memset(e->as.at, 0, 4 * span);
        e->as.at += 4 * span;
        if (count < span && fallback == NO_VALUE) {
            o.stub = here(&e->as);
            branch_to(e, JUMP, NO_VALUE, TRAP_UNREACHABLE);
        }
    } else {
        search(e, SCRATCH, t, e->ranges, n, fallback, TRAP_UNREACHABLE);
    }

    e->arm_at = grow(e->arm_at, &e->arms_cap, e->narms + 1, sizeof *e->arm_at);
    e->arm_at[e->narms++] = here(&e->as);
    e->open = grow(e->open, &e->open_cap, e->nopen + 1, sizeof *e->open);
    e->open[e->nopen++] = o;
}

static NOINLINE void
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
        u32 entry = fallback - o->table;
        for (u32 k = 0; k < o->span; k++) {
            memcpy(e->as.code + o->table + 4 * k, &entry, 4);
        }
        for (u32 k = 0; k < x[0]; k++) {
            const u32 *v = x + 2 + 5 * k;
            u32 lo = (u32)((v[0] | (u64)v[1] << 32) - o->min);
            u32 hi = (u32)((v[2] | (u64)v[3] << 32) - o->min);
            entry = arms[v[4]] - o->table;
            for (u32 m = lo; m <= hi; m++) {
                memcpy(e->as.code + o->table + 4 * m, &entry, 4);
            }
        }
    }
    e->nbranches = o->first_branch;
    e->narms = o->first_arm;
}

static u32
outgoing_bytes(const Emit *e)
{
    const IrFunc *fn = e->fn;
    if (fn->calls == 0) {
        return 0;
    }
    u32 most = 0;
    for (u32 i = 0; i < fn->count; i++) {
        u32 at = fn->first + i;
        if (e->ir->kinds[at] != IR_CALL) {
            continue;
        }
        u32 n = e->ir->extra[fn->first_extra + e->ir->bs[at]] + FIRST_INT_ARG;
        if (n > INT_ARGS && n - INT_ARGS > most) {
            most = n - INT_ARGS;
        }
    }
    return 8 * most;
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
    e->slots = slots;

    u32 cheap = fn->calls == 0 ? 1u << RDI : 0;
    u32 wanted = 0;
    for (u32 s = 0; s < fn->nslots; s++) {
        u8 info = e->info[slots[s].type];
        u32 w = slots[s].weight;
        wanted += w != 0 && w != WEIGHT_ADDRESSED && !is_float(info) &&
                  !(info & (INFO_MEMORY | INFO_UNSUPPORTED));
    }
    u32 callee = CALLEE_INT;
    if (wanted > (u32)__builtin_popcount(cheap | callee)) {
        pin_by_weight(e, false, 0, cheap, callee);
    } else {
        for (u32 s = 0; s < fn->nslots && wanted; s++) {
            u8 info = e->info[slots[s].type];
            u32 w = slots[s].weight;
            if (is_float(info) || !w || (info & (INFO_UNSUPPORTED | INFO_MEMORY)) ||
                w == WEIGHT_ADDRESSED) {
                continue;
            }
            wanted--;
            if (cheap) {
                e->slot_regs[s] = (u8)__builtin_ctz(cheap);
                cheap &= cheap - 1;
            } else if (w >= 2) {
                u32 r = (u32)__builtin_ctz(callee);
                callee &= callee - 1;
                e->slot_regs[s] = (u8)r;
                e->saved_int |= 1u << r;
            }
        }
    }
    e->pinned_int = e->saved_int;
    e->pinned_float = 0;
    e->free_callee_int = CALLEE_INT & ~e->saved_int;
    e->free_callee_float = 0;
}

static void
param_address(Emit *e, u32 i, u32 p, u8 info)
{
    (void)i;
    (void)p;
    (void)info;
    refuse(e, NO_MEMORY);
}

static void
begin_function(Emit *e, Code *code)
{
    const IrFunc *fn = e->fn;
    memset(code, OP_INT3, PROLOGUE_BYTES);
    e->as.at = code + PROLOGUE_BYTES;
    if (e->result_at != NO_VALUE) {
        refuse(e, NO_MEMORY);
        return;
    }

    const IrSlot *slots = e->slots;
    for (u32 p = 0; p < fn->nparams; p++) {
        u32 type = slots[p].type;
        u8 info = e->info[type];
        if (!plain_int(e, type)) {
            refuse(e, is_float(info) ? NO_FLOATS : NO_MEMORY);
            return;
        }
        u8 h = e->slot_regs[p];
        if (h == NO_REG && !slots[p].weight) {
            continue;
        }
        u32 k = FIRST_INT_ARG + p;
        u32 r = h != NO_REG ? h : SCRATCH;
        if (k < INT_ARGS) {
            normalize(e, info, r, INT_ARG_REGS[k]);
        } else {
            e->wide_record = true;
            rm(&e->as, 0, OP_MOV_R_RM, 1, r, RBP, (int32_t)(16 + 8 * (k - INT_ARGS)), false);
        }
        if (h == NO_REG) {
            store_typed(&e->as, info, r, e->slot_offsets[p]);
        }
    }
}

static void
store_local(Emit *e, u32 type, u32 slot, u32 b)
{
    u8 t = e->info[type];
    int32_t imm = 0;
    if (!is_float(t) && imm_operand(e, b, width(t), &imm)) {
        store_imm(&e->as, t, imm, e->slot_offsets[slot]);
        return;
    }
    u32 r = fetch(e, b, is_float(t) ? XMM15 : SCRATCH);
    release(e, b);
    store_typed(&e->as, t, r, e->slot_offsets[slot]);
}

static void
return_value(Emit *e, u32 type, u32 a, u32 b)
{
    (void)b;
    if (type == TYPE_ID_VOID) {
        return;
    }
    if (!plain_int(e, type)) {
        refuse(e, is_float(e->info[type]) ? NO_FLOATS : NO_PAIRS);
        return;
    }
    move_into(e, RAX, a, false);
}

static void
lanes(Emit *e, u32 i, u8 kind, u32 type, u32 a, u32 b, const Native *n)
{
    (void)i;
    (void)kind;
    (void)type;
    (void)a;
    (void)b;
    (void)n;
    refuse(e, NO_PAIRS);
}

static u32
frames_lanes(const Emit *e, u32 v)
{
    (void)e;
    (void)v;
    return false;
}

static NOINLINE void
describe_frame(Emit *e, u32 entry)
{
    const IrFunc *fn = e->fn;
    u32 nsaves = (u32)__builtin_popcount(e->saved_int);
    u32 pushes = nsaves + e->context_saved;
    bool record = fn->calls || e->locals + e->spill_max || pushes || e->wide_record;
    if (!record) {
        return;
    }
    u32 pushed = 4 + e->context_saved;
    for (u32 r = 0; r < 16; r++) {
        pushed += (e->saved_int >> r & 1) * (1 + (r >= 8));
    }

    Cfa f = start_frame(e, entry);
    frame_advance(&f, entry + 1);
    frame_cfa_offset(&f, 16);
    frame_saved(&f, DWARF_RBP, 16);
    frame_advance(&f, entry + 4);
    frame_cfa_register(&f, DWARF_RBP);
    frame_advance(&f, entry + pushed);
    u32 below = 16 + 8 * e->context_saved;
    for (u32 r = 0; r < 16; r++) {
        if (e->saved_int >> r & 1) {
            below += 8;
            frame_saved(&f, DWARF_REGS[r], below);
        }
    }
    frame_advance(&f, e->popped);
    frame_remember(&f);
    frame_cfa(&f, DWARF_SP, 8);
    frame_restore(&f, DWARF_RBP);
    if (e->stubs < here(&e->as)) {
        frame_advance(&f, e->stubs);
        frame_recall(&f);
    }
    end_frame(e, f);
}

static u32
finish_function(Emit *e, Code *code)
{
    const IrFunc *fn = e->fn;
    u32 nsaves = (u32)__builtin_popcount(e->saved_int);
    u32 pushes = nsaves + e->context_saved;
    u32 frame = e->locals + e->spill_max;
    frame += (16 - (frame + 8 * pushes) % 16) % 16;
    bool record = fn->calls || frame || pushes || e->wide_record;

    patch(&e->as, e->epilogue, here(&e->as));
    if (nsaves && frame) {
        lea(&e->as, RSP, RBP, -(int32_t)(8 * pushes));
    }
    for (u32 r = 16; r-- > 0;) {
        if (e->saved_int >> r & 1) {
            pop_reg(&e->as, r);
        }
    }
    if (record) {
        put(&e->as, OP_LEAVE);
    }
    e->popped = here(&e->as);
    put(&e->as, OP_RET);
    e->stubs = here(&e->as);

    static_assert(NTRAPS <= 32, "every trap kind needs a bit in `kinds`");
    u32 kinds = 0;
    for (u32 k = 1; k < NTRAPS; k++) {
        kinds |= (u32)(e->traps[k] != 0) << k;
    }
    for (; kinds; kinds &= kinds - 1) {
        u32 k = (u32)__builtin_ctz(kinds);
        patch(&e->as, e->traps[k], here(&e->as));
        trap_kind(&e->as, k);
    }

    if (frame >= FRAME_LIMIT) {
        e->as.error = "a function's frame is larger than 16 MB";
        return 0;
    }
    u32 probe = 0;
    u32 back = 0;
    if (frame >= PROBE_INTERVAL) {
        probe = here(&e->as);
        mov_imm(&e->as, SCRATCH, frame / PROBE_INTERVAL, 0);
        u32 loop = here(&e->as);
        sub_rsp(&e->as, PROBE_INTERVAL);
        touch_stack(&e->as);
        rr(&e->as, 0, OP_GROUP5, 0, 1, SCRATCH, false);
        opcode(&e->as, OP_JCC32 | CC_NE);
        put32(&e->as, loop - (here(&e->as) + 4));
        if (frame % PROBE_INTERVAL) {
            sub_rsp(&e->as, frame % PROBE_INTERVAL);
            touch_stack(&e->as);
        }
        put(&e->as, OP_JMP32);
        back = here(&e->as);
        put32(&e->as, 0);
    }

    Code prologue[PROLOGUE_BYTES];
    Assembler pro = { .code = prologue, .at = prologue };
    if (record) {
        push_reg(&pro, RBP);
        mov_rr(&pro, 1, RBP, RSP);
    }
    if (e->context_saved) {
        push_reg(&pro, RDI);
    }
    for (u32 r = 0; r < 16; r++) {
        if (e->saved_int >> r & 1) {
            push_reg(&pro, r);
        }
    }
    u32 jump = 0;
    if (probe) {
        put(&pro, OP_JMP32);
        jump = here(&pro);
        put32(&pro, 0);
    } else if (frame) {
        sub_rsp(&pro, frame);
    }
    u32 k = here(&pro);
    assert(k <= PROLOGUE_BYTES);
    u32 entry = PROLOGUE_BYTES - k;
    memcpy(code + entry, prologue, k);
    if (probe) {
        u32 from = entry + jump;
        u32 rel = probe - (from + 4);
        memcpy(code + from, &rel, 4);
        rel = PROLOGUE_BYTES - (back + 4);
        memcpy(code + back, &rel, 4);
    }
    return entry;
}

static NOINLINE void
emit_adapter(Emit *e, u32 decl, const Native *n)
{
    (void)decl;
    (void)n;
    refuse(e, NO_C_CALLS);
}
