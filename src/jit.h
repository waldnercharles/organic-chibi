#ifndef CHIBI_JIT_H
#define CHIBI_JIT_H

#include "native.h"

typedef enum
{
    CONVENTION_AAPCS64,
    CONVENTION_APPLE,
#if defined(__APPLE__)
    CONVENTION_HOST = CONVENTION_APPLE,
#else
    CONVENTION_HOST = CONVENTION_AAPCS64,
#endif
} Convention;

enum
{
    JIT_DEBUGGER = 1,
    JIT_PERF_MAP = 2,
};

bool jit_compile(Native *n, const Checker *c, Convention convention, u32 flags);

#endif
