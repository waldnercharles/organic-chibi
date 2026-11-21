#ifndef CHIBI_ARM64_H
#define CHIBI_ARM64_H

#include "check.h"

typedef enum
{
    TRAP_OVERFLOW = 1,
    TRAP_DIVIDE,
    TRAP_SHIFT,
    TRAP_RANGE,
    TRAP_UNREACHABLE,
    TRAP_BOUNDS,
    NTRAPS,
} TrapKind;

typedef struct
{
    u8 *memory;
    size_t size;
    u64 *entries;
    u32 nentries;
    u32 *code;
    size_t words;
    u8 *globals;
    size_t globals_size;
    const char *error;
} Native;

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

bool arm64_compile(Native *n, const Checker *c, Convention convention);
void *native_entry(const Native *n, u32 decl);
void native_replace(Native *n, u32 decl, void *entry);
void native_set_context(Native *n, void *context);
void native_free(Native *n);

#endif
