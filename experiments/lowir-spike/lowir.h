#ifndef CHIBI_LOWIR_H
#define CHIBI_LOWIR_H

#include "../../src/check.h"

typedef struct CppgmLowir CppgmLowir;

enum
{
    LOWIR_SHARED = 1,
    LOWIR_WRAP = 2,
};

void lowir_package(CppgmLowir *m, const Checker *c, const char *prefix, u32 flags, const char *suffix);

#endif
