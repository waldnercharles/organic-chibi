enum
{
    X8 = 8,
    X16 = 16,
    X17 = 17,
    SCRATCH = X16,
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
    PROLOGUE_WORDS = 13,
    PROBE_INTERVAL = 4096,
    FRAME_LIMIT = 1u << 24,
    UNROLLED_BYTES = 64,
    CODE_PER_FUNCTION = 96,
    CODE_PER_INSTRUCTION = 32,
    CODE_PER_ARGUMENT = 8,
    CODE_PER_ADAPTER = 24,
    CODE_PER_ADAPTER_PARAM = 8,
    FUNCTION_ALIGN = 4,
    ADAPTER_STACK_LIMIT = 4064,
    ALIAS_ARGUMENTS = 1,
    ELF_MACHINE = 183,
    DWARF_RETURN = 30,
    DWARF_SP = 31,
    DWARF_D0 = 64,
    ENTRY_CFA = 0,
};

typedef u32 Code;

static u32
dwarf_register(u32 r)
{
    return r < 32 ? r : DWARF_D0 + r - 32;
}

#if defined(__aarch64__)
#define HOST_RUNS_CODE true
#else
#define HOST_RUNS_CODE false
#endif

static const char UNSUPPORTED_TYPE[] = "the arm64 backend supports only "
                                       "integers, floats, bool, pointers, "
                                       "arrays, slices and vectors so far";
