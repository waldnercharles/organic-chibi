#define _DEFAULT_SOURCE

#include "symfile.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum
{
    SECTION_NULL,
    SECTION_TEXT,
    SECTION_ABBREV,
    SECTION_INFO,
    SECTION_LINE,
    SECTION_FRAME,
    SECTION_SYMTAB,
    SECTION_STRTAB,
    SECTION_SHSTRTAB,
    NSECTIONS,
};

enum
{
    ELF_HEADER_BYTES = 64,
    SECTION_HEADER_BYTES = 64,
    SYMBOL_BYTES = 24,
    ET_REL = 1,
    SHT_PROGBITS = 1,
    SHT_SYMTAB = 2,
    SHT_STRTAB = 3,
    SHT_NOBITS = 8,
    SHF_ALLOC = 2,
    SHF_EXECINSTR = 4,
    STT_FUNC = 2,
    STB_GLOBAL = 1,
    DW_LANG_C99 = 0x0C,
    DW_OP_call_frame_cfa = 0x9C,
    DW_LNS_advance_pc = 2,
    DW_LNS_advance_line = 3,
    DW_LNS_set_file = 4,
    DW_LNE_end_sequence = 1,
    DW_LNE_set_address = 2,
    LINE_BIAS = 5,
    LINE_RANGE = 14,
    OPCODE_BASE = 13,
    ABBREV_UNIT = 1,
    ABBREV_FUNCTION = 2,
    ADAPTER_SUFFIX_BYTES = 8,
    BYTES_PER_SYMBOL = 160,
    BYTES_PER_ROW = 13,
};

static const char *const section_names[NSECTIONS] = {
    "",           ".text",       ".debug_abbrev", ".debug_info", ".debug_line",
    ".debug_frame", ".symtab",   ".strtab",       ".shstrtab",
};

static const u8 abbrevs[] = {
    ABBREV_UNIT,     0x11, 1, 0x25, 0x08, 0x13, 0x05, 0x03, 0x08, 0x1B, 0x08,
    0x11,            0x01, 0x12, 0x07, 0x10, 0x17, 0,    0,
    ABBREV_FUNCTION, 0x2E, 0, 0x03, 0x08, 0x3A, 0x0F, 0x3B, 0x0F, 0x11, 0x01,
    0x12,            0x06, 0x40, 0x18, 0x3F, 0x19, 0,    0,
    0,
};

static const u8 opcode_lengths[OPCODE_BASE - 1] = { 0, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 1 };

typedef struct
{
    u8 *at;
    size_t count;
} Bytes;

static void
bytes_u8(Bytes *b, u32 v)
{
    b->at[b->count] = (u8)v;
    b->count += 1;
}

static void
bytes_u16(Bytes *b, u32 v)
{
    u16 x = (u16)v;
    memcpy(b->at + b->count, &x, sizeof x);
    b->count += sizeof x;
}

static void
bytes_u32(Bytes *b, u32 v)
{
    memcpy(b->at + b->count, &v, sizeof v);
    b->count += sizeof v;
}

static void
bytes_u64(Bytes *b, u64 v)
{
    memcpy(b->at + b->count, &v, sizeof v);
    b->count += sizeof v;
}

static void
bytes_uleb(Bytes *b, u64 v)
{
    while (v >= 0x80) {
        bytes_u8(b, (u32)(v & 0x7F) | 0x80);
        v >>= 7;
    }
    bytes_u8(b, (u32)v);
}

static void
bytes_sleb(Bytes *b, int64_t v)
{
    while (v < -64 || v > 63) {
        bytes_u8(b, (u32)(v & 0x7F) | 0x80);
        v >>= 7;
    }
    bytes_u8(b, (u32)(v & 0x7F));
}

static void
bytes_text(Bytes *b, const char *s, size_t len)
{
    memcpy(b->at + b->count, s, len);
    b->count += len;
    bytes_u8(b, 0);
}

static void
bytes_name(Bytes *b, Name n, u32 flags)
{
    memcpy(b->at + b->count, n.text, n.len);
    b->count += n.len;
    if (flags & SYMBOL_ADAPTER) {
        memcpy(b->at + b->count, ".adapter", ADAPTER_SUFFIX_BYTES);
        b->count += ADAPTER_SUFFIX_BYTES;
    }
    bytes_u8(b, 0);
}

static void
bytes_align(Bytes *b, size_t n)
{
    while (b->count % n) {
        bytes_u8(b, 0);
    }
}

static void
bytes_patch32(Bytes *b, size_t at, size_t v)
{
    u32 x = (u32)v;
    memcpy(b->at + at, &x, sizeof x);
}

static void
line_row(Bytes *b, u32 *at, u32 *line, u32 to, u32 to_line)
{
    u32 step = to - *at;
    int64_t delta = (int64_t)to_line - (int64_t)*line;
    if (delta + LINE_BIAS < 0 || delta + LINE_BIAS >= LINE_RANGE) {
        bytes_u8(b, DW_LNS_advance_line);
        bytes_sleb(b, delta);
        delta = 0;
    }
    u64 op = (u64)(delta + LINE_BIAS) + (u64)LINE_RANGE * step + OPCODE_BASE;
    if (op > 255) {
        bytes_u8(b, DW_LNS_advance_pc);
        bytes_uleb(b, step);
        op = (u64)(delta + LINE_BIAS) + OPCODE_BASE;
    }
    bytes_u8(b, (u32)op);
    *at = to;
    *line = to_line;
}

static void
write_lines(Bytes *b, const Symfile *s, const Checker *c, const SymfileTarget *t, const u8 *code,
          u32 *decl_lines)
{
    size_t start = b->count;
    bytes_u32(b, 0);
    bytes_u16(b, 4);
    size_t header = b->count;
    bytes_u32(b, 0);
    bytes_u8(b, t->code_align);
    bytes_u8(b, 1);
    bytes_u8(b, 1);
    bytes_u8(b, 256 - LINE_BIAS);
    bytes_u8(b, LINE_RANGE);
    bytes_u8(b, OPCODE_BASE);
    memcpy(b->at + b->count, opcode_lengths, sizeof opcode_lengths);
    b->count += sizeof opcode_lengths;
    bytes_u8(b, 0);
    for (u32 f = 0; f < c->nfiles; f++) {
        bytes_text(b, c->files[f].path, strlen(c->files[f].path));
        bytes_u8(b, 0);
        bytes_u8(b, 0);
        bytes_u8(b, 0);
    }
    bytes_u8(b, 0);
    bytes_patch32(b, header, b->count - header - 4);

    LineStarts *starts = calloc(c->nfiles, sizeof *starts);
    u32 align = t->code_align;
    for (u32 k = 0; k < s->nsymbols; k++) {
        const Symbol *sym = &s->symbols[k];
        u32 decl = sym->decl;
        u32 file = c->decls.files[decl];
        u32 token = c->files[file].nodes.tokens[c->decls.roots[decl]];
        u32 decl_line = check_line(c, &starts[file], file, token);
        decl_lines[k] = decl_line;

        bytes_u8(b, 0);
        bytes_u8(b, 9);
        bytes_u8(b, DW_LNE_set_address);
        bytes_u64(b, (u64)(uintptr_t)(code + sym->low));
        if (file) {
            bytes_u8(b, DW_LNS_set_file);
            bytes_uleb(b, file + 1);
        }

        u32 at = 0;
        u32 line = 1;
        bool any = false;
        const SymbolRow *rows = s->rows + sym->first_row;
        if (sym->nrows == 0 || rows[0].offset > sym->low) {
            line_row(b, &at, &line, 0, decl_line);
            any = true;
        }
        for (u32 r = 0; r < sym->nrows; r++) {
            u32 l = check_line(c, &starts[file], file, rows[r].token);
            if (any && l == line) {
                continue;
            }
            line_row(b, &at, &line, (rows[r].offset - sym->low) / align, l);
            any = true;
        }
        u32 stubs = (sym->stubs - sym->low) / align;
        if (sym->stubs < sym->high && stubs > at) {
            line_row(b, &at, &line, stubs, 0);
        }
        u32 end = (sym->high - sym->low) / align;
        if (end > at) {
            bytes_u8(b, DW_LNS_advance_pc);
            bytes_uleb(b, end - at);
        }
        bytes_u8(b, 0);
        bytes_u8(b, 1);
        bytes_u8(b, DW_LNE_end_sequence);
    }
    for (u32 f = 0; f < c->nfiles; f++) {
        check_free_lines(&starts[f]);
    }
    free(starts);
    bytes_patch32(b, start, b->count - start - 4);
}

static void
write_info(Bytes *b, const Symfile *s, const Checker *c, const u8 *code, size_t code_size,
         const u32 *decl_lines, const char *cwd)
{
    u32 unit_file = 0;
    for (u32 f = c->nfiles; f-- > 0;) {
        if (c->files[f].entry) {
            unit_file = f;
        }
    }

    size_t start = b->count;
    bytes_u32(b, 0);
    bytes_u16(b, 4);
    bytes_u32(b, 0);
    bytes_u8(b, 8);
    bytes_u8(b, ABBREV_UNIT);
    bytes_text(b, "chibi", 5);
    bytes_u16(b, DW_LANG_C99);
    bytes_text(b, c->files[unit_file].path, strlen(c->files[unit_file].path));
    bytes_text(b, cwd, strlen(cwd));
    bytes_u64(b, (u64)(uintptr_t)code);
    bytes_u64(b, code_size);
    bytes_u32(b, 0);
    for (u32 k = 0; k < s->nsymbols; k++) {
        const Symbol *sym = &s->symbols[k];
        bytes_u8(b, ABBREV_FUNCTION);
        bytes_name(b, names_get(c->names, c->decls.names[sym->decl]), sym->flags);
        bytes_uleb(b, c->decls.files[sym->decl] + 1);
        bytes_uleb(b, decl_lines[k]);
        bytes_u64(b, (u64)(uintptr_t)(code + sym->low));
        bytes_u32(b, sym->high - sym->low);
        bytes_u8(b, 1);
        bytes_u8(b, DW_OP_call_frame_cfa);
    }
    bytes_u8(b, 0);
    bytes_patch32(b, start, b->count - start - 4);
}

static void
write_frames(Bytes *b, const Symfile *s, const SymfileTarget *t, const u8 *code)
{
    size_t cie = b->count;
    bytes_u32(b, 0);
    bytes_u32(b, 0xFFFFFFFFu);
    bytes_u8(b, 1);
    bytes_u8(b, 0);
    bytes_uleb(b, t->code_align);
    bytes_sleb(b, -8);
    bytes_u8(b, t->return_column);
    bytes_u8(b, DW_CFA_def_cfa);
    bytes_uleb(b, t->stack_column);
    bytes_uleb(b, t->entry_cfa);
    if (t->entry_cfa) {
        bytes_u8(b, DW_CFA_offset | t->return_column);
        bytes_uleb(b, t->entry_cfa / 8u);
    }
    bytes_align(b, 8);
    bytes_patch32(b, cie, b->count - cie - 4);

    for (u32 k = 0; k < s->nsymbols; k++) {
        const Symbol *sym = &s->symbols[k];
        size_t fde = b->count;
        bytes_u32(b, 0);
        bytes_u32(b, 0);
        bytes_u64(b, (u64)(uintptr_t)(code + sym->low));
        bytes_u64(b, sym->high - sym->low);
        memcpy(b->at + b->count, s->frames + sym->first_frame, sym->nframe);
        b->count += sym->nframe;
        bytes_align(b, 8);
        bytes_patch32(b, fde, b->count - fde - 4);
    }
}

static void
write_section(Bytes *b, u32 name, u32 type, u64 flags, u64 addr, u64 offset, u64 size, u32 link,
            u32 info, u64 align, u64 entsize)
{
    bytes_u32(b, name);
    bytes_u32(b, type);
    bytes_u64(b, flags);
    bytes_u64(b, addr);
    bytes_u64(b, offset);
    bytes_u64(b, size);
    bytes_u32(b, link);
    bytes_u32(b, info);
    bytes_u64(b, align);
    bytes_u64(b, entsize);
}

u8 *
symfile_write(const Symfile *s, const Checker *c, const SymfileTarget *t, const u8 *code,
              size_t code_size, size_t *size)
{
    char cwd[4096];
    if (!getcwd(cwd, sizeof cwd)) {
        cwd[0] = 0;
    }

    size_t names = 0;
    for (u32 k = 0; k < s->nsymbols; k++) {
        names += names_get(c->names, c->decls.names[s->symbols[k].decl]).len + ADAPTER_SUFFIX_BYTES + 1;
    }
    size_t paths = strlen(cwd) + 1;
    for (u32 f = 0; f < c->nfiles; f++) {
        paths += strlen(c->files[f].path) + 4;
    }
    size_t cap = 1024 + 2 * names + 2 * paths + (size_t)s->nsymbols * BYTES_PER_SYMBOL +
                 (size_t)s->nrows * BYTES_PER_ROW + s->nframes +
                 (size_t)NSECTIONS * SECTION_HEADER_BYTES;
    u8 *out = malloc(cap);
    u32 *decl_lines = malloc((size_t)s->nsymbols * sizeof *decl_lines + 1);
    Bytes b = { out, ELF_HEADER_BYTES };
    size_t offsets[NSECTIONS];
    size_t sizes[NSECTIONS];
    memset(offsets, 0, sizeof offsets);
    memset(sizes, 0, sizeof sizes);

    offsets[SECTION_ABBREV] = b.count;
    memcpy(b.at + b.count, abbrevs, sizeof abbrevs);
    b.count += sizeof abbrevs;
    sizes[SECTION_ABBREV] = b.count - offsets[SECTION_ABBREV];

    offsets[SECTION_LINE] = b.count;
    write_lines(&b, s, c, t, code, decl_lines);
    sizes[SECTION_LINE] = b.count - offsets[SECTION_LINE];

    offsets[SECTION_INFO] = b.count;
    write_info(&b, s, c, code, code_size, decl_lines, cwd);
    sizes[SECTION_INFO] = b.count - offsets[SECTION_INFO];

    bytes_align(&b, 8);
    offsets[SECTION_FRAME] = b.count;
    write_frames(&b, s, t, code);
    sizes[SECTION_FRAME] = b.count - offsets[SECTION_FRAME];

    offsets[SECTION_SYMTAB] = b.count;
    memset(b.at + b.count, 0, SYMBOL_BYTES);
    b.count += SYMBOL_BYTES;
    u32 name_at = 1;
    for (u32 k = 0; k < s->nsymbols; k++) {
        const Symbol *sym = &s->symbols[k];
        bytes_u32(&b, name_at);
        bytes_u8(&b, STB_GLOBAL << 4 | STT_FUNC);
        bytes_u8(&b, 0);
        bytes_u16(&b, SECTION_TEXT);
        bytes_u64(&b, sym->low);
        bytes_u64(&b, sym->high - sym->low);
        name_at += names_get(c->names, c->decls.names[sym->decl]).len + 1;
        if (sym->flags & SYMBOL_ADAPTER) {
            name_at += ADAPTER_SUFFIX_BYTES;
        }
    }
    sizes[SECTION_SYMTAB] = b.count - offsets[SECTION_SYMTAB];

    offsets[SECTION_STRTAB] = b.count;
    bytes_u8(&b, 0);
    for (u32 k = 0; k < s->nsymbols; k++) {
        const Symbol *sym = &s->symbols[k];
        bytes_name(&b, names_get(c->names, c->decls.names[sym->decl]), sym->flags);
    }
    sizes[SECTION_STRTAB] = b.count - offsets[SECTION_STRTAB];

    offsets[SECTION_SHSTRTAB] = b.count;
    u32 name_offsets[NSECTIONS];
    for (u32 k = 0; k < NSECTIONS; k++) {
        name_offsets[k] = (u32)(b.count - offsets[SECTION_SHSTRTAB]);
        bytes_text(&b, section_names[k], strlen(section_names[k]));
    }
    sizes[SECTION_SHSTRTAB] = b.count - offsets[SECTION_SHSTRTAB];

    bytes_align(&b, 8);
    size_t headers = b.count;
    memset(b.at + b.count, 0, SECTION_HEADER_BYTES);
    b.count += SECTION_HEADER_BYTES;
    write_section(&b, name_offsets[SECTION_TEXT], SHT_NOBITS, SHF_ALLOC | SHF_EXECINSTR,
                (u64)(uintptr_t)code, ELF_HEADER_BYTES, code_size, 0, 0, 16, 0);
    for (u32 k = SECTION_ABBREV; k <= SECTION_FRAME; k++) {
        write_section(&b, name_offsets[k], SHT_PROGBITS, 0, 0, offsets[k], sizes[k], 0, 0, 1, 0);
    }
    write_section(&b, name_offsets[SECTION_SYMTAB], SHT_SYMTAB, 0, 0, offsets[SECTION_SYMTAB],
                sizes[SECTION_SYMTAB], SECTION_STRTAB, 1, 8, SYMBOL_BYTES);
    for (u32 k = SECTION_STRTAB; k <= SECTION_SHSTRTAB; k++) {
        write_section(&b, name_offsets[k], SHT_STRTAB, 0, 0, offsets[k], sizes[k], 0, 0, 1, 0);
    }
    assert(b.count <= cap);

    Bytes h = { out, 0 };
    bytes_u32(&h, 0x464C457Fu);
    bytes_u8(&h, 2);
    bytes_u8(&h, 1);
    bytes_u8(&h, 1);
    memset(h.at + h.count, 0, 9);
    h.count += 9;
    bytes_u16(&h, ET_REL);
    bytes_u16(&h, t->machine);
    bytes_u32(&h, 1);
    bytes_u64(&h, 0);
    bytes_u64(&h, 0);
    bytes_u64(&h, headers);
    bytes_u32(&h, 0);
    bytes_u16(&h, ELF_HEADER_BYTES);
    bytes_u16(&h, 0);
    bytes_u16(&h, 0);
    bytes_u16(&h, SECTION_HEADER_BYTES);
    bytes_u16(&h, NSECTIONS);
    bytes_u16(&h, SECTION_SHSTRTAB);
    assert(h.count == ELF_HEADER_BYTES);

    free(decl_lines);
    *size = b.count;
    return out;
}

bool
symfile_write_perf_map(const Symfile *s, const Checker *c, const u8 *code)
{
    char path[64];
    snprintf(path, sizeof path, "/tmp/perf-%d.map", (int)getpid());
    FILE *f = fopen(path, "a");
    if (!f) {
        return false;
    }
    for (u32 k = 0; k < s->nsymbols; k++) {
        const Symbol *sym = &s->symbols[k];
        Name n = names_get(c->names, c->decls.names[sym->decl]);
        fprintf(f, "%llx %x %.*s%s\n", (unsigned long long)(uintptr_t)(code + sym->low),
                sym->high - sym->low, (int)n.len, n.text,
                sym->flags & SYMBOL_ADAPTER ? ".adapter" : "");
    }
    return fclose(f) == 0;
}

void
symfile_free(Symfile *s)
{
    free(s->symbols);
    free(s->rows);
    free(s->frames);
    *s = (Symfile){ 0 };
}
