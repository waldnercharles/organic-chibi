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
    SECTION_STR,
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
    DW_ATE_boolean = 0x02,
    DW_ATE_float = 0x04,
    DW_ATE_signed = 0x05,
    DW_ATE_unsigned = 0x07,
    DW_ATE_unsigned_char = 0x08,
    DW_OP_deref = 0x06,
    DW_OP_reg0 = 0x50,
    DW_OP_breg0 = 0x70,
    DW_OP_regx = 0x90,
    DW_OP_bregx = 0x92,
    DW_OP_piece = 0x93,
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
    ABBREV_PARAM = 3,
    ABBREV_VARIABLE = 4,
    ABBREV_PARAM_UNPLACED = 5,
    ABBREV_VARIABLE_UNPLACED = 6,
    ABBREV_BASE = 7,
    ABBREV_POINTER = 8,
    ABBREV_VOID_POINTER = 9,
    ABBREV_STRUCT = 10,
    ABBREV_ANONYMOUS_STRUCT = 11,
    ABBREV_MEMBER = 12,
    ABBREV_ARRAY = 13,
    ABBREV_VECTOR = 14,
    ABBREV_SUBRANGE = 15,
    ABBREV_ENUM = 16,
    ABBREV_ENUMERATOR = 17,
    ABBREV_UNSIGNED_ENUMERATOR = 18,
    ABBREV_TYPEDEF = 19,
    VAR_PARAM = 1,
    VAR_PAIR = 2,
    POINTER_KEY = 0x80000000u,
    BASE_KEY = 0x40000000u,
    TYPE_KEY = BASE_KEY - 1,
    TEXT_BYTE = TYPE_ID_NONE,
    DIE_QUEUED = 1,
    HOME_BYTES = 32,
    BYTES_PER_VAR = 48,
    BYTES_PER_TYPE = 32,
    BYTES_PER_MEMBER = 32,
    ADAPTER_SUFFIX_BYTES = 8,
    BYTES_PER_SYMBOL = 160,
    BYTES_PER_ROW = 13,
};

static const char *const section_names[NSECTIONS] = {
    "",           ".text",       ".debug_abbrev", ".debug_info", ".debug_str", ".debug_line",
    ".debug_frame", ".symtab",   ".strtab",       ".shstrtab",
};

static const u8 abbrevs[] = {
    ABBREV_UNIT,     0x11, 1, 0x25, 0x08, 0x13, 0x05, 0x03, 0x08, 0x1B, 0x08,
    0x11,            0x01, 0x12, 0x07, 0x10, 0x17, 0,    0,
    ABBREV_FUNCTION, 0x2E, 1, 0x03, 0x08, 0x3A, 0x0F, 0x3B, 0x0F, 0x11, 0x01,
    0x12,            0x06, 0x40, 0x18, 0x3F, 0x19, 0,    0,
    ABBREV_PARAM,    0x05, 0, 0x03, 0x0E, 0x49, 0x13, 0x02, 0x18, 0, 0,
    ABBREV_VARIABLE, 0x34, 0, 0x03, 0x0E, 0x49, 0x13, 0x02, 0x18, 0, 0,
    ABBREV_PARAM_UNPLACED, 0x05, 0, 0x03, 0x0E, 0x49, 0x13, 0, 0,
    ABBREV_VARIABLE_UNPLACED, 0x34, 0, 0x03, 0x0E, 0x49, 0x13, 0, 0,
    ABBREV_BASE,     0x24, 0, 0x03, 0x08, 0x3E, 0x0B, 0x0B, 0x0B, 0, 0,
    ABBREV_POINTER,  0x0F, 0, 0x49, 0x13, 0x0B, 0x0B, 0, 0,
    ABBREV_VOID_POINTER, 0x0F, 0, 0x0B, 0x0B, 0, 0,
    ABBREV_STRUCT,   0x13, 1, 0x03, 0x08, 0x0B, 0x0F, 0, 0,
    ABBREV_ANONYMOUS_STRUCT, 0x13, 1, 0x0B, 0x0F, 0, 0,
    ABBREV_MEMBER,   0x0D, 0, 0x03, 0x08, 0x49, 0x13, 0x38, 0x0F, 0, 0,
    ABBREV_ARRAY,    0x01, 1, 0x49, 0x13, 0, 0,
    ABBREV_VECTOR,   0x01, 1, 0x49, 0x13, 0x87, 0x42, 0x19, 0, 0,
    ABBREV_SUBRANGE, 0x21, 0, 0x37, 0x0F, 0, 0,
    ABBREV_ENUM,     0x04, 1, 0x03, 0x08, 0x49, 0x13, 0x0B, 0x0B, 0, 0,
    ABBREV_ENUMERATOR, 0x28, 0, 0x03, 0x08, 0x1C, 0x0D, 0, 0,
    ABBREV_UNSIGNED_ENUMERATOR, 0x28, 0, 0x03, 0x08, 0x1C, 0x0F, 0, 0,
    ABBREV_TYPEDEF,  0x16, 0, 0x03, 0x08, 0x49, 0x13, 0, 0,
    0,
};

#define FIXED_TYPE_NAME(id, kind, arg0, arg1, text) text,
static const char *const fixed_names[NFIXED_TYPES] = { "", CHIBI_FIXED_TYPES(FIXED_TYPE_NAME) };

static const u8 opcode_lengths[OPCODE_BASE - 1] = { 0, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 1 };

typedef struct
{
    u8 *at;
    size_t count;
} Bytes;

typedef struct
{
    u32 *at;
    Bytes text;
} Strings;

typedef struct
{
    u32 *dies;
    u32 *pointer_dies;
    u32 *base_dies;
    u32 *queue;
    u32 nqueue;
    u32 *refs;
    u32 nrefs;
    u32 refs_cap;
} TypeDies;


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
            u32 row_token = rows[r].token;
            u32 l = row_token == NO_LINE ? 0 : check_line(c, &starts[file], file, row_token);
            if (any && l == line) {
                continue;
            }
            line_row(b, &at, &line, (rows[r].offset - sym->low) / align, l);
            any = true;
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

static u32 *
die_of(TypeDies *d, u32 key)
{
    u32 type = key & TYPE_KEY;
    return key & POINTER_KEY ? &d->pointer_dies[type]
         : key & BASE_KEY    ? &d->base_dies[type]
                             : &d->dies[type];
}

static void
put_type_ref(Bytes *b, TypeDies *d, u32 key)
{
    u32 *die = die_of(d, key);
    if (*die == 0) {
        *die = DIE_QUEUED;
        d->queue[d->nqueue] = key;
        d->nqueue += 1;
    }
    if (d->nrefs + 2 > d->refs_cap) {
        d->refs_cap = (d->nrefs + 2) * 2;
        d->refs = realloc(d->refs, d->refs_cap * sizeof *d->refs);
    }
    d->refs[d->nrefs] = (u32)b->count;
    d->refs[d->nrefs + 1] = key;
    d->nrefs += 2;
    bytes_u32(b, 0);
}

static void
put_breg(Bytes *x, u32 reg, int64_t offset)
{
    if (reg < 32) {
        bytes_u8(x, DW_OP_breg0 + reg);
    } else {
        bytes_u8(x, DW_OP_bregx);
        bytes_uleb(x, reg);
    }
    bytes_sleb(x, offset);
}

static void
put_home(Bytes *x, u32 home, u32 sp)
{
    bool by_pointer = home & HOME_BY_POINTER;
    u32 v = home & HOME_VALUE;
    if ((home & HOME_KIND) == HOME_STACK) {
        put_breg(x, sp, v);
        if (by_pointer) {
            bytes_u8(x, DW_OP_deref);
        }
    } else if (by_pointer) {
        put_breg(x, v, 0);
    } else if (v < 32) {
        bytes_u8(x, DW_OP_reg0 + v);
    } else {
        bytes_u8(x, DW_OP_regx);
        bytes_uleb(x, v);
    }
}

static u32
var_home(Bytes *x, const Checker *c, u32 type, const u32 *homes, bool pair, u32 sp)
{
    if (!pair) {
        if (homes[0] & HOME_KIND) {
            put_home(x, homes[0], sp);
        }
        return (u32)x->count;
    }

    bool placed = ((homes[0] | homes[1]) & HOME_KIND) != 0;
    if (c->types.kinds[type] != TYPE_SLICE || !placed) {
        return 0;
    }
    for (u32 k = 0; k < 2; k++) {
        if (homes[k] & HOME_KIND) {
            put_home(x, homes[k], sp);
        }
        bytes_u8(x, DW_OP_piece);
        bytes_uleb(x, 8);
    }
    return (u32)x->count;
}

static void
put_string(Bytes *b, const Checker *c, Strings *st, u32 name)
{
    if (st->at[name] == 0) {
        Name n = names_get(c->names, name);
        st->at[name] = (u32)st->text.count;
        bytes_text(&st->text, n.text, n.len);
    }
    bytes_u32(b, st->at[name]);
}

static void
write_var(Bytes *b, const Checker *c, TypeDies *d, Strings *st, const IrSlot *slot,
          const u32 *homes, u32 flags, u32 sp)
{
    u8 expr[HOME_BYTES] = { 0 };
    Bytes x = { expr, 0 };
    u32 n = var_home(&x, c, slot->type, homes, flags & VAR_PAIR, sp);

    u32 abbrev = flags & VAR_PARAM ? ABBREV_PARAM : ABBREV_VARIABLE;
    bytes_u8(b, n ? abbrev : abbrev + 2);
    put_string(b, c, st, slot->name);
    put_type_ref(b, d, slot->type);
    if (n) {
        bytes_u8(b, n);
        memcpy(b->at + b->count, expr, HOME_BYTES);
        b->count += n;
    }
}

static void
write_vars(Bytes *b, const Symfile *s, const Checker *c, TypeDies *d, Strings *st, u32 decl,
           u32 sp)
{
    const IrFunc *fn = &c->ir.funcs[c->decls.funcs[decl] - 1];
    const IrSlot *slots = c->ir.code.slots + fn->first_slot;
    const u32 *homes = s->homes + fn->first_slot;
    for (u32 k = 0; k < fn->nslots; k++) {
        u32 name = slots[k].name;
        u32 node = slots[k].node;
        bool second = k > 0 && name == slots[k - 1].name && node == slots[k - 1].node;
        if (name == 0 || second) {
            continue;
        }

        bool pair = k + 1 < fn->nslots && slots[k + 1].name == name && slots[k + 1].node == node;
        u32 flags = (k < fn->nparams ? VAR_PARAM : 0) | (pair ? VAR_PAIR : 0);
        write_var(b, c, d, st, &slots[k], homes + k, flags, sp);
    }
}

static void
write_base(Bytes *b, const char *name, u32 encoding, u32 size)
{
    bytes_u8(b, ABBREV_BASE);
    bytes_text(b, name, strlen(name));
    bytes_u8(b, encoding);
    bytes_u8(b, size);
}

static const char *
base_name(const Checker *c, u32 type)
{
    return c->types.kinds[type] == TYPE_ERROR ? "error" : fixed_names[type];
}

static void
write_typedef(Bytes *b, const Checker *c, TypeDies *d, u32 type)
{
    const char *name = base_name(c, type);
    bytes_u8(b, ABBREV_TYPEDEF);
    bytes_text(b, name, strlen(name));
    put_type_ref(b, d, BASE_KEY | type);
}

static void
write_named_base(Bytes *b, const Checker *c, u32 type)
{
    const Types *t = &c->types;
    u8 kind = t->kinds[type];
    u32 encoding = kind == TYPE_FLOAT                  ? DW_ATE_float
                 : kind == TYPE_INT && t->arg1[type]   ? DW_ATE_signed
                                                       : DW_ATE_unsigned;
    write_base(b, base_name(c, type), encoding, (u32)check_type_size(c, type));
}

static void
write_pointer(Bytes *b, TypeDies *d, u32 to)
{
    if (to == TYPE_ID_VOID) {
        bytes_u8(b, ABBREV_VOID_POINTER);
        bytes_u8(b, 8);
        return;
    }
    bytes_u8(b, ABBREV_POINTER);
    put_type_ref(b, d, to);
    bytes_u8(b, 8);
}

static void
write_member(Bytes *b, TypeDies *d, Name name, u32 type, u64 offset)
{
    bytes_u8(b, ABBREV_MEMBER);
    bytes_text(b, name.text, name.len);
    put_type_ref(b, d, type);
    bytes_uleb(b, offset);
}

static void
write_slice(Bytes *b, TypeDies *d, u32 type, u32 elem)
{
    if (type == TYPE_ID_STRING) {
        bytes_u8(b, ABBREV_STRUCT);
        bytes_text(b, "string", 6);
    } else {
        bytes_u8(b, ABBREV_ANONYMOUS_STRUCT);
    }
    bytes_uleb(b, 16);
    u32 to = type == TYPE_ID_STRING ? TEXT_BYTE : elem;
    write_member(b, d, (Name){ "ptr", 3, 0 }, POINTER_KEY | to, 0);
    write_member(b, d, (Name){ "len", 3, 0 }, TYPE_ID_I64, 8);
    bytes_u8(b, 0);
}

static void
write_array(Bytes *b, TypeDies *d, u32 abbrev, u32 elem, u64 count)
{
    bytes_u8(b, abbrev);
    put_type_ref(b, d, elem);
    bytes_u8(b, ABBREV_SUBRANGE);
    bytes_uleb(b, count);
    bytes_u8(b, 0);
}

static size_t
member_names(const Checker *c, u32 decl)
{
    u32 first = c->decls.members[decl];
    u32 n = c->decls.member_counts[decl];
    size_t bytes = names_get(c->names, c->decls.names[decl]).len + BYTES_PER_TYPE;
    for (u32 i = 0; i < n; i++) {
        bytes += names_get(c->names, c->members[first + i].name).len + BYTES_PER_MEMBER;
    }
    return bytes;
}

static void
write_struct(Bytes *b, const Checker *c, TypeDies *d, u32 decl, u64 size)
{
    u32 first = c->decls.members[decl];
    u32 n = c->decls.member_counts[decl];
    bool tuple = c->decls.flags[decl] & DECL_TUPLE;

    if (tuple) {
        bytes_u8(b, ABBREV_ANONYMOUS_STRUCT);
    } else {
        Name name = names_get(c->names, c->decls.names[decl]);
        bytes_u8(b, ABBREV_STRUCT);
        bytes_text(b, name.text, name.len);
    }
    bytes_uleb(b, size);
    for (u32 i = 0; i < n; i++) {
        const Member *m = &c->members[first + i];
        char digits[12];
        Name field = names_get(c->names, m->name);
        if (tuple) {
            field = (Name){ digits, (u32)snprintf(digits, sizeof digits, "%u", i), 0 };
        }
        write_member(b, d, field, m->type, check_member_offset(c, decl, first + i));
    }
    bytes_u8(b, 0);
}

static void
write_enum(Bytes *b, const Checker *c, TypeDies *d, u32 decl, u32 type)
{
    const Types *t = &c->types;
    u32 under = (u32)t->arg1[type];
    bool sign = t->kinds[under] == TYPE_INT && t->arg1[under];
    u32 first = c->decls.members[decl];
    u32 n = c->decls.member_counts[decl];
    Name name = names_get(c->names, c->decls.names[decl]);

    bytes_u8(b, ABBREV_ENUM);
    bytes_text(b, name.text, name.len);
    put_type_ref(b, d, under);
    bytes_u8(b, (u32)check_type_size(c, type));
    for (u32 i = 0; i < n; i++) {
        const Member *m = &c->members[first + i];
        if (m->value >= MEMBER_NONE) {
            continue;
        }
        Name item = names_get(c->names, m->name);
        u64 bits = exact_bits(&c->exacts, m->value);
        bytes_u8(b, sign ? ABBREV_ENUMERATOR : ABBREV_UNSIGNED_ENUMERATOR);
        bytes_text(b, item.text, item.len);
        if (sign) {
            bytes_sleb(b, (int64_t)bits);
        } else {
            bytes_uleb(b, bits);
        }
    }
    bytes_u8(b, 0);
}

static size_t
type_bytes(const Checker *c)
{
    const Types *t = &c->types;
    size_t bytes = 0;
    for (u32 type = 0; type < t->count; type++) {
        u8 kind = t->kinds[type];
        bytes += 3 * BYTES_PER_TYPE;
        if (kind == TYPE_STRUCT || kind == TYPE_ENUM) {
            bytes += member_names(c, t->arg0[type]);
        } else if (kind == TYPE_SLICE) {
            bytes += 2 * BYTES_PER_MEMBER;
        }
    }
    return bytes;
}

static size_t
names_bytes(const Names *names)
{
    size_t bytes = 1;
    for (u32 k = 0; k < names->count; k++) {
        bytes += names->names[k].len + 1;
    }
    return bytes;
}

static void
write_type(Bytes *b, const Checker *c, TypeDies *d, u32 key, size_t unit)
{
    const Types *t = &c->types;
    u32 type = key & TYPE_KEY;
    *die_of(d, key) = (u32)(b->count - unit);
    if (key & POINTER_KEY) {
        write_pointer(b, d, type);
        return;
    }
    if (key & BASE_KEY) {
        write_named_base(b, c, type);
        return;
    }

    u32 arg0 = t->arg0[type];
    if (type == TEXT_BYTE) {
        write_base(b, "char", DW_ATE_unsigned_char, 1);
        return;
    }
    if (type == TYPE_ID_CSTRING) {
        write_pointer(b, d, TEXT_BYTE);
        return;
    }
    switch (t->kinds[type]) {
        case TYPE_BOOL: write_base(b, "bool", DW_ATE_boolean, 1); return;
        case TYPE_INT:
        case TYPE_FLOAT:
        case TYPE_UINTPTR:
        case TYPE_ERROR:
            assert(type < NFIXED_TYPES || t->kinds[type] == TYPE_ERROR);
            write_typedef(b, c, d, type);
            return;
        case TYPE_POINTER:
        case TYPE_MANY: write_pointer(b, d, arg0); return;
        case TYPE_FN: write_pointer(b, d, TYPE_ID_VOID); return;
        case TYPE_OPTIONAL: {
            u8 inner = t->kinds[arg0];
            if (inner == TYPE_POINTER || inner == TYPE_MANY || inner == TYPE_FN) {
                write_pointer(b, d, inner == TYPE_FN ? TYPE_ID_VOID : t->arg0[arg0]);
                return;
            }
            break;
        }
        case TYPE_SLICE: write_slice(b, d, type, arg0); return;
        case TYPE_ARRAY: write_array(b, d, ABBREV_ARRAY, arg0, t->arg1[type]); return;
        case TYPE_VECTOR: write_array(b, d, ABBREV_VECTOR, arg0, t->arg1[type]); return;
        case TYPE_STRUCT: write_struct(b, c, d, arg0, t->arg1[type]); return;
        case TYPE_ENUM: write_enum(b, c, d, arg0, type); return;
        default: break;
    }
    write_array(b, d, ABBREV_ARRAY, TYPE_ID_U8, check_type_size(c, type));
}

static void
write_info(Bytes *b, const Symfile *s, const Checker *c, const SymfileTarget *t, Strings *st,
         const u8 *code, size_t code_size, const u32 *decl_lines, const char *cwd)
{
    u32 unit_file = 0;
    for (u32 f = c->nfiles; f-- > 0;) {
        if (c->files[f].entry) {
            unit_file = f;
        }
    }

    u32 ntypes = c->types.count;
    TypeDies d = { calloc(ntypes, sizeof *d.dies),
                   calloc(ntypes, sizeof *d.pointer_dies),
                   calloc(ntypes, sizeof *d.base_dies),
                   malloc(3 * (size_t)ntypes * sizeof *d.queue),
                   0,
                   NULL,
                   0,
                   0 };

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
        Name name = names_get(c->names, c->decls.names[sym->decl]);
        bytes_u8(b, ABBREV_FUNCTION);
        bytes_name(b, name, sym->flags);
        bytes_uleb(b, c->decls.files[sym->decl] + 1);
        bytes_uleb(b, decl_lines[k]);
        bytes_u64(b, (u64)(uintptr_t)(code + sym->low));
        bytes_u32(b, sym->high - sym->low);
        bytes_u8(b, 1);
        bytes_u8(b, DW_OP_call_frame_cfa);
        if (!(sym->flags & SYMBOL_ADAPTER)) {
            write_vars(b, s, c, &d, st, sym->decl, t->stack_column);
        }
        bytes_u8(b, 0);
    }
    for (u32 q = 0; q < d.nqueue; q++) {
        write_type(b, c, &d, d.queue[q], start);
    }
    bytes_u8(b, 0);

    for (u32 r = 0; r < d.nrefs; r += 2) {
        bytes_patch32(b, d.refs[r], *die_of(&d, d.refs[r + 1]));
    }
    free(d.dies);
    free(d.pointer_dies);
    free(d.base_dies);
    free(d.queue);
    free(d.refs);
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
                 (size_t)NSECTIONS * SECTION_HEADER_BYTES +
                 (size_t)c->ir.code.nslots * BYTES_PER_VAR + type_bytes(c) + names_bytes(c->names);
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

    Strings st = { calloc(c->names->count, sizeof *st.at), { malloc(names_bytes(c->names)), 1 } };
    st.text.at[0] = 0;
    offsets[SECTION_INFO] = b.count;
    write_info(&b, s, c, t, &st, code, code_size, decl_lines, cwd);
    sizes[SECTION_INFO] = b.count - offsets[SECTION_INFO];

    offsets[SECTION_STR] = b.count;
    memcpy(b.at + b.count, st.text.at, st.text.count);
    b.count += st.text.count;
    sizes[SECTION_STR] = st.text.count;
    free(st.at);
    free(st.text.at);

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
    free(s->homes);
    *s = (Symfile){ 0 };
}
