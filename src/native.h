#ifndef CHIBI_NATIVE_H
#define CHIBI_NATIVE_H

#include "check.h"

typedef struct
{
    u8 *memory;
    size_t size;
    u64 *entries;
    u32 nentries;
    u64 *exports;
    u32 nexports;
    u8 *data;
    u8 *code;
    size_t code_size;
    u8 *globals;
    size_t globals_size;
    const char *error;
} Native;

bool native_begin(Native *n, const Checker *c, size_t code_size, bool executable);
void native_end(Native *n, size_t code_size);
void *native_entry(const Native *n, u32 decl);
void *native_export(const Native *n, u32 i);
void native_replace(Native *n, u32 decl, void *entry);
void native_set_context(Native *n, void *context);
void native_free(Native *n);

#endif
