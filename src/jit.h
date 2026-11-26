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

bool jit_compile(Native *n, const Checker *c, Convention convention);

#endif
