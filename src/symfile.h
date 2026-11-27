#ifndef CHIBI_SYMFILE_H
#define CHIBI_SYMFILE_H

#include "check.h"

enum
{
    DW_CFA_advance_loc = 0x40,
    DW_CFA_advance_loc1 = 0x02,
    DW_CFA_advance_loc2 = 0x03,
    DW_CFA_advance_loc4 = 0x04,
    DW_CFA_offset_extended = 0x05,
    DW_CFA_restore_extended = 0x06,
    DW_CFA_remember_state = 0x0A,
    DW_CFA_restore_state = 0x0B,
    DW_CFA_def_cfa = 0x0C,
    DW_CFA_def_cfa_register = 0x0D,
    DW_CFA_def_cfa_offset = 0x0E,
    DW_CFA_offset = 0x80,
    DW_CFA_restore = 0xC0,
};

enum
{
    SYMBOL_ADAPTER = 1,
    FRAME_BYTES_PER_SYMBOL = 128,
};

typedef struct
{
    u32 decl;
    u32 flags;
    u32 low;
    u32 stubs;
    u32 high;
    u32 first_row;
    u32 nrows;
    u32 first_frame;
    u32 nframe;
} Symbol;

typedef struct
{
    u32 offset;
    u32 token;
} SymbolRow;

typedef struct
{
    u16 machine;
    u8 code_align;
    u8 return_column;
    u8 stack_column;
    u8 entry_cfa;
} SymfileTarget;

typedef struct
{
    Symbol *symbols;
    u32 nsymbols;
    u32 symbols_cap;
    SymbolRow *rows;
    u32 nrows;
    u32 rows_cap;
    u8 *frames;
    u32 nframes;
    u32 frames_cap;
} Symfile;

u8 *symfile_write(const Symfile *s, const Checker *c, const SymfileTarget *t, const u8 *code,
                  size_t code_size, size_t *size);

bool symfile_write_perf_map(const Symfile *s, const Checker *c, const u8 *code);

void symfile_free(Symfile *s);

#endif
