enum
{
    RAX = 0,
    RCX = 1,
    RDX = 2,
    RBX = 3,
    RSP = 4,
    RBP = 5,
    RSI = 6,
    RDI = 7,
    R8 = 8,
    R9 = 9,
    R10 = 10,
    R11 = 11,
    R12 = 12,
    R13 = 13,
    R14 = 14,
    R15 = 15,
    SCRATCH = R11,
    SCRATCH2 = R10,
    XMM14 = 14,
    XMM15 = 15,
    INT_POOL = 0x00000347u,
    FLOAT_POOL = 0x00003FFFu,
    CALLEE_INT = 0x0000F008u,
    CALLEE_FLOAT = 0,
    ARGUMENTS = 0x000003C6u,
    INT_ARGS = 6,
    FLOAT_ARGS = 8,
    ALIAS_ARGUMENTS = 0,
    PROLOGUE_BYTES = 48,
    PROBE_INTERVAL = 4096,
    FRAME_LIMIT = 1u << 24,
    UNROLLED_BYTES = 64,
    CODE_PER_FUNCTION = 512,
    CODE_PER_INSTRUCTION = 160,
    CODE_PER_ARGUMENT = 48,
    CODE_PER_ADAPTER = 128,
    CODE_PER_ADAPTER_PARAM = 48,
    FUNCTION_ALIGN = 16,
    ELF_MACHINE = 62,
    DWARF_RETURN = 16,
    DWARF_SP = 7,
    DWARF_RBP = 6,
    DWARF_XMM0 = 17,
    ENTRY_CFA = 8,
};

static const u8 DWARF_REGS[16] = { 0, 2, 1, 3, 7, 6, 4, 5, 8, 9, 10, 11, 12, 13, 14, 15 };

static const u8 INT_ARG_REGS[INT_ARGS] = { RDI, RSI, RDX, RCX, R8, R9 };

typedef u8 Code;

static u32
dwarf_register(u32 r)
{
    return r < 32 ? DWARF_REGS[r] : DWARF_XMM0 + r - 32;
}

#if defined(__x86_64__)
#define HOST_RUNS_CODE true
#else
#define HOST_RUNS_CODE false
#endif

static const char UNSUPPORTED_TYPE[] = "the x86-64 backend supports only "
                                       "integers, bool and pointers so far";
