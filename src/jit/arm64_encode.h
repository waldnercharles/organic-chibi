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

static void
put(Assembler *as, u32 word)
{
    *as->at++ = word;
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

static void
patch_one(Assembler *as, u32 at, u32 target)
{
    int32_t delta = (int32_t)target - (int32_t)at;
    u32 word = as->code[at];
    if ((word & 0x7C000000u) == OP_B) {
        as->code[at] = word | ((u32)delta & 0x03FFFFFFu);
    } else {
        if (delta < -(1 << 18) || delta >= (1 << 18)) {
            as->error = "a function is too large for a conditional branch";
        }
        as->code[at] = word | ((u32)delta & 0x7FFFFu) << 5;
    }
}

static void
mov_imm(Assembler *as, u32 reg, u64 value, u32 sf)
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
            put(as, (inverted ? OP_MOVN : OP_MOVZ) | sf | i << 21 | imm << 5 | reg);
            first = false;
        } else {
            put(as, OP_MOVK | sf | i << 21 | h << 5 | reg);
        }
    }
    if (first) {
        put(as, (inverted ? OP_MOVN : OP_MOVZ) | sf | reg);
    }
}

static void
mov(Assembler *as, u32 sf, u32 d, u32 m)
{
    if (d != m) {
        put(as, OP_ORR | sf | m << 16 | ZR << 5 | d);
    }
}

static void
fmov(Assembler *as, u32 d, u32 n)
{
    if (d != n) {
        put(as, OP_FMOV | DOUBLE | n << 5 | d);
    }
}

static void
float_imm(Assembler *as, u32 reg, u64 bits, bool is_double)
{
    if (bits == 0) {
        put(as, (is_double ? OP_FMOV_FROM_X : OP_FMOV_FROM_W) | ZR << 5 | reg);
        return;
    }
    mov_imm(as, X8, bits, is_double ? SF : 0);
    put(as, (is_double ? OP_FMOV_FROM_X : OP_FMOV_FROM_W) | X8 << 5 | reg);
}

static void
adrp(Assembler *as, u32 d, u64 to)
{
    u64 pc = (u64)(uintptr_t)(as->code + here(as));
    int64_t pages = (int64_t)((to & ~0xFFFull) - (pc & ~0xFFFull)) >> 12;
    put(as, OP_ADRP | ((u32)pages & 3) << 29 | (((u32)pages >> 2) & 0x7FFFFu) << 5 | d);
}

static void
sp_address(Assembler *as, u32 d, u32 offset)
{
    if (offset >= (1u << 24)) {
        as->error = "a function's frame is larger than 16 MB";
        return;
    }
    put(as, OP_ADD_FROM_SP | (offset & 0xFFF) << 10 | d);
    if (offset >> 12) {
        put(as, OP_ADD_HIGH | (offset >> 12) << 10 | d << 5 | d);
    }
}

static void
memory(Assembler *as, u32 op, u32 scale, u32 rt, u32 offset, u32 address)
{
    if ((offset & ((1u << scale) - 1)) == 0 && (offset >> scale) < 4096) {
        put(as, op | (offset >> scale) << 10 | SP << 5 | rt);
        return;
    }
    sp_address(as, address, offset);
    put(as, op | address << 5 | rt);
}

static void
load_typed(Assembler *as, u8 t, u32 rt, u32 offset)
{
    if (is_float(t)) {
        bool d = bits_of(t) == 64;
        memory(as, d ? OP_LDRD : OP_LDRS, d ? 3 : 2, rt, offset, X8);
        return;
    }
    bool s = is_signed(t);
    switch (bits_of(t)) {
        case 8: memory(as, s ? OP_LDRSB : OP_LDRB, 0, rt, offset, rt); break;
        case 16: memory(as, s ? OP_LDRSH : OP_LDRH, 1, rt, offset, rt); break;
        case 32: memory(as, OP_LDRW, 2, rt, offset, rt); break;
        default: memory(as, OP_LDRX, 3, rt, offset, rt); break;
    }
}

static void
store_typed(Assembler *as, u8 t, u32 rt, u32 offset)
{
    if (is_float(t) && rt != ZR) {
        bool d = bits_of(t) == 64;
        memory(as, d ? OP_STRD : OP_STRS, d ? 3 : 2, rt, offset, X8);
        return;
    }
    switch (bits_of(t)) {
        case 8: memory(as, OP_STRB, 0, rt, offset, X8); break;
        case 16: memory(as, OP_STRH, 1, rt, offset, X8); break;
        case 32: memory(as, OP_STRW, 2, rt, offset, X8); break;
        default: memory(as, OP_STRX, 3, rt, offset, X8); break;
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

static void
store_spill(Assembler *as, u32 h, u32 offset)
{
    memory(as, h >= 32 ? OP_STRD : OP_STRX, 3, h & 31, offset, X8);
}

static void
load_spill(Assembler *as, u32 reg, u32 offset, bool fl)
{
    memory(as, fl ? OP_LDRD : OP_LDRX, 3, reg, offset, fl ? X8 : reg);
}

static void
load_immediate(Assembler *as, u32 reg, u64 bits, u8 info)
{
    if (is_float(info)) {
        float_imm(as, reg, bits, bits_of(info) == 64);
    } else {
        mov_imm(as, reg, bits, width(info));
    }
}

static void
move_reg(Assembler *as, u32 d, u32 m, bool fl)
{
    if (fl) {
        fmov(as, d, m);
    } else {
        mov(as, SF, d, m);
    }
}

static void
jump_later(Assembler *as, u32 *list)
{
    add_fixup(as, list);
    put(as, OP_B);
}

static void
jump_back(Assembler *as, u32 start)
{
    put(as, OP_B | ((start - here(as)) & 0x03FFFFFFu));
}

static void
skip_if_zero(Assembler *as, u32 *list, u32 reg)
{
    add_fixup(as, list);
    put(as, OP_CBZ | reg);
}

static void
skip_unless_flags(Assembler *as, u32 *list, u8 cond)
{
    add_fixup(as, list);
    put(as, OP_B_COND | (u32)(cond ^ 1));
}

static void
align_loop(Assembler *as)
{
    put(as, OP_NOP);
    while ((uintptr_t)as->at & 15) {
        put(as, OP_NOP);
    }
}
