enum
{
    CC_O,
    CC_NO,
    CC_B,
    CC_AE,
    CC_E,
    CC_NE,
    CC_BE,
    CC_A,
    CC_S,
    CC_NS,
    CC_P,
    CC_NP,
    CC_L,
    CC_GE,
    CC_LE,
    CC_G,
};

enum
{
    ALU_ADD,
    ALU_OR,
    ALU_ADC,
    ALU_SBB,
    ALU_AND,
    ALU_SUB,
    ALU_XOR,
    ALU_CMP,
};

enum
{
    SHIFT_SHL = 4,
    SHIFT_SHR = 5,
    SHIFT_SAR = 7,
};

enum
{
    UNARY_NOT = 2,
    UNARY_NEG = 3,
};

enum
{
    OP_ADD_RM_R = 0x01,
    OP_TEST_RM_R = 0x85,
    OP_MOV_RM_R8 = 0x88,
    OP_MOV_RM_R = 0x89,
    OP_MOV_R_RM = 0x8B,
    OP_LEA = 0x8D,
    OP_MOVSXD = 0x63,
    OP_IMUL_IMM8 = 0x6B,
    OP_IMUL_IMM32 = 0x69,
    OP_ALU_IMM8 = 0x83,
    OP_ALU_IMM32 = 0x81,
    OP_SHIFT_IMM = 0xC1,
    OP_MOV_RM_IMM8 = 0xC6,
    OP_MOV_RM_IMM = 0xC7,
    OP_UNARY = 0xF7,
    OP_GROUP5 = 0xFF,
    OP_PUSH = 0x50,
    OP_POP = 0x58,
    OP_MOV_IMM = 0xB8,
    OP_RET = 0xC3,
    OP_LEAVE = 0xC9,
    OP_INT3 = 0xCC,
    OP_JMP8 = 0xEB,
    OP_JMP32 = 0xE9,
    OP_IMUL = 0x0FAF,
    OP_MOVZX8 = 0x0FB6,
    OP_MOVZX16 = 0x0FB7,
    OP_MOVSX8 = 0x0FBE,
    OP_MOVSX16 = 0x0FBF,
    OP_JCC32 = 0x0F80,
    OP_SETCC = 0x0F90,
    OP_CMOVCC = 0x0F40,
    OP_UD1 = 0x0FB9,
    OP_MOVSD_LOAD = 0x0F10,
    OP_MOVSD_STORE = 0x0F11,
    OP_MOVAPS = 0x0F28,
    OP_XORPS = 0x0F57,
    OP_MOVQ_TO_XMM = 0x0F6E,
    PREFIX_16 = 0x66,
    PREFIX_DOUBLE = 0xF2,
    PREFIX_SINGLE = 0xF3,
};

static void
put(Assembler *as, u8 byte)
{
    *as->at++ = byte;
}

static void
put32(Assembler *as, u32 value)
{
    memcpy(as->at, &value, 4);
    as->at += 4;
}

static void
put64(Assembler *as, u64 value)
{
    memcpy(as->at, &value, 8);
    as->at += 8;
}

static u32
width(u8 info)
{
    return (info & INFO_SCALE) == 3;
}

static bool
fits8(int64_t v)
{
    return v >= -128 && v < 128;
}

static bool
fits32(int64_t v)
{
    return v >= INT32_MIN && v <= INT32_MAX;
}

static void
rex(Assembler *as, u32 w, u32 reg, u32 base, bool force)
{
    u32 bits = w << 3 | (reg >> 3) << 2 | base >> 3;
    if (bits || force) {
        put(as, (u8)(0x40 | bits));
    }
}

static void
opcode(Assembler *as, u32 op)
{
    if (op > 0xFF) {
        put(as, (u8)(op >> 8));
    }
    put(as, (u8)op);
}

static bool
byte_reg(u32 reg)
{
    return reg >= 4 && reg < 8;
}

static void
rr(Assembler *as, u32 prefix, u32 op, u32 w, u32 reg, u32 rm, bool force)
{
    if (prefix) {
        put(as, (u8)prefix);
    }
    rex(as, w, reg, rm, force);
    opcode(as, op);
    put(as, (u8)(0xC0 | (reg & 7) << 3 | (rm & 7)));
}

static void
rm(Assembler *as, u32 prefix, u32 op, u32 w, u32 reg, u32 base, int32_t disp, bool force)
{
    if (prefix) {
        put(as, (u8)prefix);
    }
    rex(as, w, reg, base, force);
    opcode(as, op);
    u32 b = base & 7;
    u32 mod = disp == 0 && b != 5 ? 0x00 : fits8(disp) ? 0x40 : 0x80;
    put(as, (u8)(mod | (reg & 7) << 3 | b));
    if (b == 4) {
        put(as, 0x24);
    }
    if (mod == 0x40) {
        put(as, (u8)disp);
    } else if (mod == 0x80) {
        put32(as, (u32)disp);
    }
}

static void
mov_rr(Assembler *as, u32 w, u32 d, u32 s)
{
    if (d != s) {
        rr(as, 0, OP_MOV_RM_R, w, s, d, false);
    }
}

static void
mov_imm(Assembler *as, u32 reg, u64 value, u32 w)
{
    if (!w || value <= 0xFFFFFFFFu) {
        rex(as, 0, 0, reg, false);
        put(as, (u8)(OP_MOV_IMM + (reg & 7)));
        put32(as, (u32)value);
    } else if (fits32((int64_t)value)) {
        rr(as, 0, OP_MOV_RM_IMM, 1, 0, reg, false);
        put32(as, (u32)value);
    } else {
        rex(as, 1, 0, reg, false);
        put(as, (u8)(OP_MOV_IMM + (reg & 7)));
        put64(as, value);
    }
}

static void
alu_rr(Assembler *as, u32 alu, u32 w, u32 d, u32 s)
{
    rr(as, 0, OP_ADD_RM_R | alu << 3, w, s, d, false);
}

static void
alu_imm(Assembler *as, u32 alu, u32 w, u32 d, int32_t imm)
{
    if (fits8(imm)) {
        rr(as, 0, OP_ALU_IMM8, w, alu, d, false);
        put(as, (u8)imm);
    } else if (d == RAX) {
        rex(as, w, 0, 0, false);
        put(as, (u8)(alu << 3 | 5));
        put32(as, (u32)imm);
    } else {
        rr(as, 0, OP_ALU_IMM32, w, alu, d, false);
        put32(as, (u32)imm);
    }
}

static void
test_rr(Assembler *as, u32 w, u32 a, u32 b)
{
    rr(as, 0, OP_TEST_RM_R, w, b, a, false);
}

static void
test_imm(Assembler *as, u32 w, u32 a, int32_t imm)
{
    if (a == RAX) {
        rex(as, w, 0, 0, false);
        put(as, 0xA9);
    } else {
        rr(as, 0, OP_UNARY, w, 0, a, false);
    }
    put32(as, (u32)imm);
}

static void
imul_rr(Assembler *as, u32 w, u32 d, u32 s)
{
    rr(as, 0, OP_IMUL, w, d, s, false);
}

static void
imul_imm(Assembler *as, u32 w, u32 d, u32 s, int32_t imm)
{
    if (fits8(imm)) {
        rr(as, 0, OP_IMUL_IMM8, w, d, s, false);
        put(as, (u8)imm);
    } else {
        rr(as, 0, OP_IMUL_IMM32, w, d, s, false);
        put32(as, (u32)imm);
    }
}

static void
unary_r(Assembler *as, u32 op, u32 w, u32 d)
{
    rr(as, 0, OP_UNARY, w, op, d, false);
}

static void
shift_imm(Assembler *as, u32 op, u32 w, u32 d, u32 count)
{
    rr(as, 0, OP_SHIFT_IMM, w, op, d, false);
    put(as, (u8)count);
}

static void
extend(Assembler *as, u32 bits, bool s, u32 d, u32 n)
{
    if (bits == 8) {
        rr(as, 0, s ? OP_MOVSX8 : OP_MOVZX8, 0, d, n, byte_reg(n));
    } else if (bits == 16) {
        rr(as, 0, s ? OP_MOVSX16 : OP_MOVZX16, 0, d, n, false);
    } else if (s) {
        rr(as, 0, OP_MOVSXD, 1, d, n, false);
    } else {
        mov_rr(as, 0, d, n);
        if (d == n) {
            rr(as, 0, OP_MOV_RM_R, 0, n, d, false);
        }
    }
}

static void
sign_extend_wide(Assembler *as, u32 bits, u32 d, u32 n)
{
    u32 op = bits == 8 ? OP_MOVSX8 : bits == 16 ? OP_MOVSX16 : OP_MOVSXD;
    rr(as, 0, op, 1, d, n, false);
}

static void
setcc(Assembler *as, u8 cond, u32 d)
{
    rr(as, 0, OP_SETCC | cond, 0, 0, d, byte_reg(d));
    rr(as, 0, OP_MOVZX8, 0, d, d, byte_reg(d));
}

static void
push_reg(Assembler *as, u32 reg)
{
    rex(as, 0, 0, reg, false);
    put(as, (u8)(OP_PUSH + (reg & 7)));
}

static void
pop_reg(Assembler *as, u32 reg)
{
    rex(as, 0, 0, reg, false);
    put(as, (u8)(OP_POP + (reg & 7)));
}

static void
lea(Assembler *as, u32 d, u32 base, int32_t disp)
{
    rm(as, 0, OP_LEA, 1, d, base, disp, false);
}

static void
sub_rsp(Assembler *as, u32 bytes)
{
    if (fits8((int32_t)bytes)) {
        rr(as, 0, OP_ALU_IMM8, 1, ALU_SUB, RSP, false);
        put(as, (u8)bytes);
    } else {
        rr(as, 0, OP_ALU_IMM32, 1, ALU_SUB, RSP, false);
        put32(as, bytes);
    }
}

static void
touch_stack(Assembler *as)
{
    rm(as, 0, OP_ALU_IMM8, 1, ALU_OR, RSP, 0, false);
    put(as, 0);
}

static void
call_through(Assembler *as, u64 slot)
{
    int64_t disp = (int64_t)slot - (int64_t)(uintptr_t)(as->at + 6);
    if (fits32(disp)) {
        put(as, (u8)OP_GROUP5);
        put(as, 0x15);
        put32(as, (u32)disp);
        return;
    }
    mov_imm(as, SCRATCH, slot, 1);
    rm(as, 0, OP_GROUP5, 0, 2, SCRATCH, 0, false);
}

static void
trap_kind(Assembler *as, u32 kind)
{
    opcode(as, OP_UD1);
    put(as, 0x40);
    put(as, (u8)kind);
}

static void
load_typed(Assembler *as, u8 t, u32 rt, u32 offset)
{
    if (is_float(t)) {
        u32 prefix = bits_of(t) == 64 ? PREFIX_DOUBLE : PREFIX_SINGLE;
        rm(as, prefix, OP_MOVSD_LOAD, 0, rt, RSP, (int32_t)offset, false);
        return;
    }
    bool s = is_signed(t);
    switch (bits_of(t)) {
        case 8:
            rm(as, 0, s ? OP_MOVSX8 : OP_MOVZX8, 0, rt, RSP, (int32_t)offset, false);
            break;
        case 16:
            rm(as, 0, s ? OP_MOVSX16 : OP_MOVZX16, 0, rt, RSP, (int32_t)offset, false);
            break;
        case 32:
            rm(as, 0, OP_MOV_R_RM, 0, rt, RSP, (int32_t)offset, false);
            break;
        default:
            rm(as, 0, OP_MOV_R_RM, 1, rt, RSP, (int32_t)offset, false);
            break;
    }
}

static void
store_typed(Assembler *as, u8 t, u32 rt, u32 offset)
{
    if (is_float(t)) {
        u32 prefix = bits_of(t) == 64 ? PREFIX_DOUBLE : PREFIX_SINGLE;
        rm(as, prefix, OP_MOVSD_STORE, 0, rt, RSP, (int32_t)offset, false);
        return;
    }
    switch (bits_of(t)) {
        case 8:
            rm(as, 0, OP_MOV_RM_R8, 0, rt, RSP, (int32_t)offset, byte_reg(rt));
            break;
        case 16:
            rm(as, PREFIX_16, OP_MOV_RM_R, 0, rt, RSP, (int32_t)offset, false);
            break;
        case 32:
            rm(as, 0, OP_MOV_RM_R, 0, rt, RSP, (int32_t)offset, false);
            break;
        default:
            rm(as, 0, OP_MOV_RM_R, 1, rt, RSP, (int32_t)offset, false);
            break;
    }
}

static void
store_imm(Assembler *as, u8 t, int32_t imm, u32 offset)
{
    switch (bits_of(t)) {
        case 8:
            rm(as, 0, OP_MOV_RM_IMM8, 0, 0, RSP, (int32_t)offset, false);
            put(as, (u8)imm);
            break;
        case 16:
            rm(as, PREFIX_16, OP_MOV_RM_IMM, 0, 0, RSP, (int32_t)offset, false);
            put(as, (u8)imm);
            put(as, (u8)(imm >> 8));
            break;
        default:
            rm(as, 0, OP_MOV_RM_IMM, bits_of(t) == 64, 0, RSP, (int32_t)offset, false);
            put32(as, (u32)imm);
            break;
    }
}

static void
patch_one(Assembler *as, u32 at, u32 target)
{
    u32 rel = target - (at + 4);
    memcpy(as->code + at, &rel, 4);
}

static void
store_spill(Assembler *as, u32 h, u32 offset)
{
    if (h >= 32) {
        rm(as, PREFIX_DOUBLE, OP_MOVSD_STORE, 0, h & 31, RSP, (int32_t)offset, false);
    } else {
        rm(as, 0, OP_MOV_RM_R, 1, h, RSP, (int32_t)offset, false);
    }
}

static void
load_spill(Assembler *as, u32 reg, u32 offset, bool fl)
{
    rm(as,
       fl ? PREFIX_DOUBLE : 0,
       fl ? OP_MOVSD_LOAD : OP_MOV_R_RM,
       !fl,
       reg,
       RSP,
       (int32_t)offset,
       false);
}

static void
load_immediate(Assembler *as, u32 reg, u64 bits, u8 info)
{
    if (!is_float(info)) {
        mov_imm(as, reg, bits, width(info));
        return;
    }
    if (bits == 0) {
        rr(as, 0, OP_XORPS, 0, reg, reg, false);
        return;
    }
    mov_imm(as, SCRATCH, bits, 1);
    rr(as, PREFIX_16, OP_MOVQ_TO_XMM, 1, reg, SCRATCH, false);
}

static void
move_reg(Assembler *as, u32 d, u32 m, bool fl)
{
    if (d == m) {
        return;
    }
    if (fl) {
        rr(as, 0, OP_MOVAPS, 0, d, m, false);
    } else {
        mov_rr(as, 1, d, m);
    }
}

static void
jump_later(Assembler *as, u32 *list)
{
    put(as, OP_JMP32);
    add_fixup(as, list);
    put32(as, 0);
}

static void
jump_if(Assembler *as, u32 *list, u8 cond)
{
    opcode(as, OP_JCC32 | cond);
    add_fixup(as, list);
    put32(as, 0);
}

static void
jump_back(Assembler *as, u32 start)
{
    int32_t rel = (int32_t)start - (int32_t)(here(as) + 2);
    if (fits8(rel)) {
        put(as, OP_JMP8);
        put(as, (u8)rel);
        return;
    }
    put(as, OP_JMP32);
    put32(as, (u32)((int32_t)start - (int32_t)(here(as) + 4)));
}

static void
skip_if_zero(Assembler *as, u32 *list, u32 reg)
{
    test_rr(as, 0, reg, reg);
    jump_if(as, list, CC_E);
}

static void
skip_unless_flags(Assembler *as, u32 *list, u8 cond)
{
    jump_if(as, list, (u8)(cond ^ 1));
}

static void
align_loop(Assembler *as)
{
    static const u8 nops[9][9] = {
        { 0 },
        { 0x90 },
        { 0x66, 0x90 },
        { 0x0F, 0x1F, 0x00 },
        { 0x0F, 0x1F, 0x40, 0x00 },
        { 0x0F, 0x1F, 0x44, 0x00, 0x00 },
        { 0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00 },
        { 0x0F, 0x1F, 0x80, 0x00, 0x00, 0x00, 0x00 },
        { 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00 },
    };
    u32 pad = (u32)(-(uintptr_t)as->at & 15);
    while (pad) {
        u32 n = pad > 8 ? 8 : pad;
        memcpy(as->at, nops[n], n);
        as->at += n;
        pad -= n;
    }
}

static void
sp_address(Assembler *as, u32 d, u32 offset)
{
    lea(as, d, RSP, (int32_t)offset);
}
