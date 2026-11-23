#include "program.h"

#include "code.h"

#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum
{
    T_I8,
    T_I16,
    T_I32,
    T_INT,
    T_U8,
    T_U16,
    T_U32,
    T_U64,
    T_F32,
    T_F64,
    T_BOOL,
    NTYPES,
} Type;

static const char *const type_names[NTYPES] = { "i8",  "i16", "i32", "int",
                                                "u8",  "u16", "u32", "u64",
                                                "f32", "f64", "bool" };
static const u8 type_bits[NTYPES] = { 8, 16, 32, 64, 8, 16, 32, 64, 32, 64, 8 };

enum
{
    MAX_VARS = 128,
    MAX_HELPERS = 6,
    MAX_PARAMS = 3,
    NAME_LEN = 8,
    MAX_LABELS = 16,
    MAX_ARMS = 6,
    MAX_ENUMS = 2,
    MAX_MEMBERS = 8,
    MAX_LITERAL = 8,
    MAX_ITERATED = 100,
    MAX_RECORDS = 3,
    MAX_FIELDS = 5,
    MAX_RECORD_VARS = 32,
    MAX_TABLES = 8,
    PATH_LEN = 64,
    ANY = 0xFFFF,
    NO_ENUM = 0xFFFFFFFFu,
    NO_RECORD = 0xFF,
};

typedef struct
{
    int64_t lo;
    int64_t hi;
} Span;

typedef struct
{
    int64_t values[MAX_MEMBERS];
    u8 count;
    u8 backing;
} Enum;

typedef struct
{
    u32 name;
    u8 type;
    bool given;
    bool narrowed;
    bool typed;
} Label;

typedef struct
{
    char name[NAME_LEN];
    u8 type;
    bool assignable;
    bool pointer;
    bool slice;
    bool escapes;
    u8 inner;
    u8 limit;
    u16 length;
} Var;

typedef struct
{
    u32 var;
    u16 length;
} Bound;

typedef struct
{
    u8 type;
    u8 record;
    u8 length;
    bool given;
    bool slice;
} Field;

typedef struct
{
    Field fields[MAX_FIELDS];
    u8 count;
    u8 align;
    u8 leaf;
    u8 vregs;
    u32 size;
} Record;

typedef struct
{
    char name[NAME_LEN];
    u8 record;
    u8 length;
    u8 limit;
    bool pointer;
    bool assignable;
} RecordVar;

typedef struct
{
    char name[NAME_LEN];
    u8 type;
    u8 inner;
    u8 limit;
    bool slice;
    u16 length;
} Table;

typedef struct
{
    u8 ret;
    bool ret_slice;
    u8 ret_record;
    u16 ret_length;
    u8 nparams;
    u8 params[MAX_PARAMS];
    bool pointers[MAX_PARAMS];
    bool slices[MAX_PARAMS];
    u16 lengths[MAX_PARAMS];
    u8 records[MAX_PARAMS];
    u8 level;
} Helper;

typedef struct
{
    u64 *rng;
    char *out;
    size_t len;
    Var vars[MAX_VARS];
    u32 nvars;
    u32 names;
    Helper helpers[MAX_HELPERS];
    u32 nhelpers;
    u32 level;
    u8 ret;
    bool ret_slice;
    u8 ret_record;
    u16 ret_length;
    u32 nesting;
    Label result;
    u32 loops;
    u32 max_loops;
    u32 indent;
    u32 loop_labels[MAX_LABELS];
    u32 block_labels[MAX_LABELS];
    u32 nblock_labels;
    u32 loops_outside;
    u32 blocks_outside;
    bool in_defer;
    Label values[MAX_LABELS];
    u32 nvalues;
    Enum enums[MAX_ENUMS];
    u32 nenums;
    u8 switch_type;
    u32 nspans;
    Bound bounds[MAX_LABELS];
    u32 nbounds;
    Record records[MAX_RECORDS];
    u32 nrecords;
    RecordVar rvars[MAX_RECORD_VARS];
    u32 nrvars;
    Table tables[MAX_TABLES];
    u32 ntables;
    u32 in_path;
} Gen;

static bool
is_int(u8 t)
{
    return t <= T_U64;
}

static bool
is_signed(u8 t)
{
    return t <= T_INT;
}

static bool
is_float(u8 t)
{
    return t == T_F32 || t == T_F64;
}

static bool
chance(Gen *g, int percent)
{
    return random_below(g->rng, 100) < percent;
}

static void
emit(Gen *g, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(g->out + g->len, PROGRAM_SIZE - g->len, fmt, ap);
    va_end(ap);
    g->len += (size_t)n;
    assert(g->len < PROGRAM_SIZE - 256);
}

static void
line(Gen *g)
{
    emit(g, "\n%*s", (int)(4 * g->indent), "");
}

static u8
random_type(Gen *g, bool numeric)
{
    return (u8)random_below(g->rng, numeric ? T_BOOL : NTYPES);
}

static u8
random_int_type(Gen *g)
{
    return (u8)random_below(g->rng, T_U64 + 1);
}

static bool
matches(const Var *v, u8 t, bool assignable, bool pointer)
{
    return (t == NTYPES || v->type == t) && v->pointer == pointer &&
           !v->slice && !v->length && (!assignable || v->assignable);
}

static u32
count_vars(const Gen *g, u8 t, bool assignable, bool pointer)
{
    u32 n = 0;
    for (u32 i = 0; i < g->nvars; i++) {
        n += matches(&g->vars[i], t, assignable, pointer);
    }
    return n;
}

static Var *
pick_matching(Gen *g, u8 t, bool assignable, bool pointer)
{
    u32 n = count_vars(g, t, assignable, pointer);
    if (n == 0) {
        return NULL;
    }
    u32 k = (u32)random_below(g->rng, (int)n);
    for (u32 i = 0; i < g->nvars; i++) {
        if (matches(&g->vars[i], t, assignable, pointer) && k-- == 0) {
            return &g->vars[i];
        }
    }
    return NULL;
}

static Var *
pick_var(Gen *g, u8 t, bool assignable)
{
    return pick_matching(g, t, assignable, false);
}

static Var *
pick_pointer(Gen *g, u8 t)
{
    return pick_matching(g, t, false, true);
}

static Var *
pick_numeric_var(Gen *g)
{
    u32 n = 0;
    for (u32 i = 0; i < g->nvars; i++) {
        n += g->vars[i].type != T_BOOL && !g->vars[i].pointer &&
             !g->vars[i].slice && !g->vars[i].length;
    }
    assert(n > 0);
    u32 k = (u32)random_below(g->rng, (int)n);
    for (u32 i = 0; i < g->nvars; i++) {
        if (g->vars[i].type != T_BOOL && !g->vars[i].pointer &&
            !g->vars[i].slice && !g->vars[i].length && k-- == 0) {
            return &g->vars[i];
        }
    }
    return NULL;
}

static Var *
declare_as(Gen *g, char prefix, u32 number, u8 t, bool assignable)
{
    assert(g->nvars < MAX_VARS);
    Var *v = &g->vars[g->nvars++];
    snprintf(v->name, NAME_LEN, "%c%u", prefix, number);
    v->type = t;
    v->assignable = assignable;
    v->pointer = false;
    v->slice = false;
    v->escapes = false;
    v->inner = 0;
    v->limit = 0;
    v->length = 0;
    return v;
}

static Var *
declare(Gen *g, char prefix, u8 t, bool assignable)
{
    return declare_as(g, prefix, g->names++, t, assignable);
}

static bool
holds(const Var *v, u8 t, u16 length, u16 inner, bool writable)
{
    return v->length && !v->slice && (t == NTYPES || v->type == t) &&
           (length == ANY || v->length == length) &&
           (inner == ANY || v->inner == inner) &&
           (!writable || v->assignable || v->pointer);
}

static u32
count_arrays(const Gen *g, u8 t, u16 length, u16 inner, bool writable)
{
    u32 n = 0;
    for (u32 i = 0; i < g->nvars; i++) {
        n += holds(&g->vars[i], t, length, inner, writable);
    }
    return n;
}

static Var *
pick_array(Gen *g, u8 t, u16 length, u16 inner, bool writable)
{
    u32 n = count_arrays(g, t, length, inner, writable);
    if (n == 0) {
        return NULL;
    }
    u32 k = (u32)random_below(g->rng, (int)n);
    for (u32 i = 0; i < g->nvars; i++) {
        if (holds(&g->vars[i], t, length, inner, writable) && k-- == 0) {
            return &g->vars[i];
        }
    }
    return NULL;
}

static bool
can_shape(const Gen *g, u8 t, u16 length, u16 inner, bool writable)
{
    return count_arrays(g, t, length, inner, writable) > 0 ||
           (inner == 0 && count_arrays(g, t, ANY, length, writable) > 0);
}

static int
sliceable(const Var *v, u8 t, u16 inner)
{
    bool whole = v->slice || (v->length && (v->assignable || v->pointer));
    if (v->type != t || !whole) {
        return -1;
    }
    if (v->inner == inner) {
        return v->length;
    }
    return inner == 0 && v->length ? v->inner : -1;
}

static bool
is_source(const Var *v, bool escaping, u8 t, u16 inner, u16 length)
{
    return (!escaping || v->escapes) && sliceable(v, t, inner) >= (int)length;
}

static u32
count_sources(const Gen *g, u32 limit, bool escaping, u8 t, u16 inner, u16 length)
{
    u32 n = 0;
    for (u32 i = 0; i < limit; i++) {
        n += is_source(&g->vars[i], escaping, t, inner, length);
    }
    return n;
}

static Var *
pick_source(Gen *g, u32 limit, bool escaping, u8 t, u16 inner, u16 length)
{
    u32 n = count_sources(g, limit, escaping, t, inner, length);
    if (n == 0) {
        return NULL;
    }
    u32 k = (u32)random_below(g->rng, (int)n);
    for (u32 i = 0; i < limit; i++) {
        if (is_source(&g->vars[i], escaping, t, inner, length) && k-- == 0) {
            return &g->vars[i];
        }
    }
    return NULL;
}

static bool
viewable(const Var *v, u16 max_length)
{
    return sliceable(v, v->type, v->inner) >= 0 && v->length <= max_length;
}

static Var *
pick_view(Gen *g, u16 max_length)
{
    u32 n = 0;
    for (u32 i = 0; i < g->nvars; i++) {
        n += viewable(&g->vars[i], max_length);
    }
    if (n == 0) {
        return NULL;
    }
    u32 k = (u32)random_below(g->rng, (int)n);
    for (u32 i = 0; i < g->nvars; i++) {
        if (viewable(&g->vars[i], max_length) && k-- == 0) {
            return &g->vars[i];
        }
    }
    return NULL;
}

static bool
is_view(const Var *v, u8 t, u16 length)
{
    return v->slice && (t == NTYPES || v->type == t) && v->length >= length;
}

static Var *
pick_slice(Gen *g, u8 t, u16 length)
{
    u32 n = 0;
    for (u32 i = 0; i < g->nvars; i++) {
        n += is_view(&g->vars[i], t, length);
    }
    if (n == 0) {
        return NULL;
    }
    u32 k = (u32)random_below(g->rng, (int)n);
    for (u32 i = 0; i < g->nvars; i++) {
        if (is_view(&g->vars[i], t, length) && k-- == 0) {
            return &g->vars[i];
        }
    }
    return NULL;
}

static bool
is_table(const Table *z, u8 t, u16 inner, u32 limit)
{
    return (t == NTYPES || z->type == t) && z->inner >= inner && z->limit <= limit;
}

static Table *
pick_table(Gen *g, u8 t, u16 inner, u32 limit)
{
    u32 n = 0;
    for (u32 i = 0; i < g->ntables; i++) {
        n += is_table(&g->tables[i], t, inner, limit);
    }
    if (n == 0) {
        return NULL;
    }
    u32 k = (u32)random_below(g->rng, (int)n);
    for (u32 i = 0; i < g->ntables; i++) {
        if (is_table(&g->tables[i], t, inner, limit) && k-- == 0) {
            return &g->tables[i];
        }
    }
    return NULL;
}

static const Bound *
pick_bound(Gen *g, u16 length)
{
    u32 n = 0;
    for (u32 k = 0; k < g->nbounds; k++) {
        n += g->bounds[k].length <= length;
    }
    if (n == 0) {
        return NULL;
    }
    u32 pick = (u32)random_below(g->rng, (int)n);
    for (u32 k = 0; k < g->nbounds; k++) {
        if (g->bounds[k].length <= length && pick-- == 0) {
            return &g->bounds[k];
        }
    }
    return NULL;
}

static void
print_int(Gen *g, bool negative, u64 magnitude)
{
    if (negative) {
        emit(g, "(-%llu)", (unsigned long long)magnitude);
    } else {
        emit(g, "%llu", (unsigned long long)magnitude);
    }
}

static void
int_literal(Gen *g, u8 t, bool nonzero)
{
    u32 bits = type_bits[t];
    bool s = is_signed(t);
    u64 top = s ? 1ull << (bits - 1) : (bits == 64 ? ~0ull : (1ull << bits) - 1);
    for (;;) {
        bool negative = false;
        u64 m = 0;
        int pick = random_below(g->rng, 10);
        if (pick < 6) {
            m = (u64)random_below(g->rng, 10);
            negative = s && chance(g, 30);
        } else if (pick < 8) {
            static const u64 edges[] = { 1,  2,  3,  7,   8,   15,  16, 31,
                                         32, 63, 64, 127, 128, 255, 256 };
            m = edges[random_below(g->rng, sizeof edges / sizeof edges[0])];
            negative = s && chance(g, 40);
        } else if (pick < 9) {
            m = s ? top - (u64)random_below(g->rng, 2)
                  : top - (u64)random_below(g->rng, 2);
            negative = s && chance(g, 50);
        } else {
            m = (u64)random_below(g->rng, 1 << 20) * 977;
            negative = s && chance(g, 50);
        }
        bool ok = negative ? m <= top : m <= (s ? top - 1 : top);
        if (ok && (!nonzero || m != 0)) {
            print_int(g, negative, m);
            return;
        }
    }
}

static void
literal(Gen *g, u8 t, bool nonzero)
{
    if (t == T_BOOL) {
        emit(g, chance(g, 50) ? "true" : "false");
    } else if (is_float(t)) {
        static const char *const floats[] = { "0.0",
                                              "1.0",
                                              "1.5",
                                              "0.25",
                                              "2.0",
                                              "10.5",
                                              "(-0.5)",
                                              "(-3.25)",
                                              "100000.0",
                                              "0.1",
                                              "3.0",
                                              "(-1.0)",
                                              "1000000000000.0" };
        emit(g, "%s", floats[random_below(g->rng, sizeof floats / sizeof floats[0])]);
    } else {
        int_literal(g, t, nonzero);
    }
}

static bool expr(Gen *g, u8 t, int depth);
static void typed_expr(Gen *g, u8 t, int depth);
static void nonliteral(Gen *g, u8 t, int depth);

static bool
widens(u8 from, u8 to)
{
    if (is_signed(from) == is_signed(to)) {
        return type_bits[from] <= type_bits[to];
    }
    return !is_signed(from) && type_bits[from] < type_bits[to];
}

static u64
max_of(u8 t)
{
    u32 bits = type_bits[t] - is_signed(t);
    return bits == 64 ? ~0ull : (1ull << bits) - 1;
}

static void
convert_from(Gen *g, u8 t, u8 u, const char *operand)
{
    if (is_int(t) && is_int(u) && !widens(u, t) && chance(g, 95)) {
        u64 mask = max_of(t) < max_of(u) ? max_of(t) : max_of(u);
        emit(g, "%s((%s & %llu))", type_names[t], operand, (unsigned long long)mask);
        return;
    }
    emit(g, "%s(%s)", type_names[t], operand);
}

static void
bitcast_value(Gen *g, u8 t, int depth)
{
    static const u8 partners[NTYPES] = {
        [T_I8] = T_U8,   [T_I16] = T_U16, [T_I32] = T_U32, [T_INT] = T_U64,
        [T_U8] = T_I8,   [T_U16] = T_I16, [T_U32] = T_I32, [T_U64] = T_INT,
        [T_F32] = T_U32, [T_F64] = T_U64,
    };
    if (is_float(t)) {
        u8 u = chance(g, 50) ? partners[t] : t == T_F32 ? T_I32 : T_INT;
        emit(g, "bitcast(%s, (", type_names[t]);
        nonliteral(g, u, depth - 1);
        emit(g, " & %s))", t == T_F32 ? "1073741823" : "4611686018427387903");
        return;
    }
    if (type_bits[t] >= 32 && chance(g, 30)) {
        emit(g, "bitcast(%s, %s(", type_names[t], type_bits[t] == 32 ? "f32" : "f64");
        nonliteral(g, random_int_type(g), depth - 1);
        emit(g, "))");
        return;
    }
    emit(g, "bitcast(%s, ", type_names[t]);
    nonliteral(g, partners[t], depth - 1);
    emit(g, ")");
}

static void
conversion(Gen *g, u8 t, int depth)
{
    if (t != T_BOOL && chance(g, 15)) {
        bitcast_value(g, t, depth);
        return;
    }
    u8 u = random_type(g, true);
    if (is_float(u) && is_int(t) && !chance(g, 8)) {
        u = random_int_type(g);
    }
    size_t start = g->len;
    nonliteral(g, u, depth - 1);
    char operand[PROGRAM_SIZE / 64];
    size_t n = g->len - start;
    assert(n < sizeof operand);
    memcpy(operand, g->out + start, n);
    operand[n] = 0;
    g->len = start;
    convert_from(g, t, u, operand);
}

static u64
mask_below(u16 length)
{
    u64 m = 1;
    while (m * 2 <= length) {
        m *= 2;
    }
    return m - 1;
}

static void
subscript(Gen *g, u16 length, int depth)
{
    emit(g, "[");
    const Bound *b = pick_bound(g, length);
    int r = random_below(g->rng, 100);
    if (r < 25) {
        u16 tail = length < 8 ? length : 8;
        emit(
            g,
            "%d",
            chance(g, 50) ? random_below(g->rng, length)
                          : length - 1 - random_below(g->rng, tail)
        );
    } else if (r < 40 && b) {
        emit(g, "%s", g->vars[b->var].name);
    } else if (r < 70) {
        u8 u = (u8)(T_U8 + random_below(g->rng, 4));
        while (max_of(u) < length) {
            u++;
        }
        emit(g, "(");
        nonliteral(g, u, depth);
        emit(g, " %% %u)", length);
    } else if (r < 98) {
        u8 u = random_int_type(g);
        u64 mask = mask_below(length) < max_of(u) ? mask_below(length) : max_of(u);
        emit(g, "(");
        nonliteral(g, u, depth);
        emit(g, " & %llu)", (unsigned long long)mask);
    } else {
        nonliteral(g, random_int_type(g), depth);
    }
    emit(g, "]");
}

static void
array_base(Gen *g, const Var *a)
{
    emit(g, a->pointer ? "%s.*" : "%s", a->name);
}

static void
element(Gen *g, const Var *a, int depth)
{
    array_base(g, a);
    subscript(g, a->length, depth);
    if (a->inner) {
        subscript(g, a->inner, depth);
    }
}

static void
table_row(Gen *g, const Table *z, int depth)
{
    emit(g, "%s", z->name);
    subscript(g, z->length, depth);
}

static void
shaped(Gen *g, u8 t, u16 length, u16 inner, bool writable)
{
    Var *a = pick_array(g, t, length, inner, writable);
    Var *rows = inner ? NULL : pick_array(g, t, ANY, length, writable);
    assert(a || rows);
    if (a && (!rows || chance(g, 60))) {
        array_base(g, a);
        return;
    }
    array_base(g, rows);
    subscript(g, rows->length, 0);
}

static bool
has_leaf(const Gen *g, u8 r, u8 t, bool slice)
{
    const Record *rec = &g->records[r];
    for (u8 k = 0; k < rec->count; k++) {
        const Field *f = &rec->fields[k];
        if (f->record != NO_RECORD ? has_leaf(g, f->record, t, slice)
                                   : f->type == t && f->slice == slice) {
            return true;
        }
    }
    return false;
}

static bool
leads_to(const Gen *g, const Field *f, u8 t, bool slice)
{
    return f->record != NO_RECORD ? has_leaf(g, f->record, t, slice)
                                  : f->type == t && f->slice == slice;
}

static void
leaf_path(Gen *g, u8 r, u8 t, bool slice)
{
    const Record *rec = &g->records[r];
    u32 n = 0;
    for (u8 k = 0; k < rec->count; k++) {
        n += leads_to(g, &rec->fields[k], t, slice);
    }
    u32 pick = (u32)random_below(g->rng, (int)n);
    for (u8 k = 0; k < rec->count; k++) {
        const Field *f = &rec->fields[k];
        if (!leads_to(g, f, t, slice) || pick-- != 0) {
            continue;
        }
        emit(g, ".f%u", k);
        if (f->length) {
            subscript(g, f->length, 0);
        }
        if (f->record != NO_RECORD) {
            leaf_path(g, f->record, t, slice);
        }
        return;
    }
}

static bool
reads_leaf(const Gen *g, const RecordVar *v, u8 t, bool writable, bool slice)
{
    return has_leaf(g, v->record, t, slice) &&
           (!writable || v->assignable || v->pointer);
}

static RecordVar *
pick_record_var(Gen *g, u8 t, bool writable, bool slice)
{
    u32 n = 0;
    for (u32 i = 0; i < g->nrvars; i++) {
        n += reads_leaf(g, &g->rvars[i], t, writable, slice);
    }
    if (n == 0) {
        return NULL;
    }
    u32 k = (u32)random_below(g->rng, (int)n);
    for (u32 i = 0; i < g->nrvars; i++) {
        if (reads_leaf(g, &g->rvars[i], t, writable, slice) && k-- == 0) {
            return &g->rvars[i];
        }
    }
    return NULL;
}

static void
record_leaf(Gen *g, const RecordVar *v, u8 t, bool slice)
{
    g->in_path++;
    emit(g, "%s", v->name);
    if (v->length) {
        subscript(g, v->length, 0);
    }
    leaf_path(g, v->record, t, slice);
    g->in_path--;
}

static RecordVar *
pick_slice_leaf(Gen *g, bool writable, u8 *t)
{
    for (int k = 0; k < 4; k++) {
        *t = random_type(g, false);
        RecordVar *v = pick_record_var(g, *t, writable, true);
        if (v) {
            return v;
        }
    }
    return NULL;
}

static bool
has_record(const Gen *g, u8 outer, u8 r)
{
    const Record *rec = &g->records[outer];
    for (u8 k = 0; k < rec->count; k++) {
        u8 inner = rec->fields[k].record;
        if (inner != NO_RECORD && (inner == r || has_record(g, inner, r))) {
            return true;
        }
    }
    return false;
}

static bool
holds_slices(const Gen *g, u8 r)
{
    const Record *rec = &g->records[r];
    for (u8 k = 0; k < rec->count; k++) {
        const Field *f = &rec->fields[k];
        if (f->slice || (f->record != NO_RECORD && holds_slices(g, f->record))) {
            return true;
        }
    }
    return false;
}

static bool
reaches(const Gen *g, const Field *f, u8 r)
{
    return f->record != NO_RECORD && (f->record == r || has_record(g, f->record, r));
}

static void
record_path(Gen *g, u8 outer, u8 r)
{
    const Record *rec = &g->records[outer];
    u32 n = 0;
    for (u8 k = 0; k < rec->count; k++) {
        n += reaches(g, &rec->fields[k], r);
    }
    u32 pick = (u32)random_below(g->rng, (int)n);
    for (u8 k = 0; k < rec->count; k++) {
        const Field *f = &rec->fields[k];
        if (!reaches(g, f, r) || pick-- != 0) {
            continue;
        }
        emit(g, ".f%u", k);
        if (f->length) {
            subscript(g, f->length, 0);
        }
        if (f->record != r || (has_record(g, f->record, r) && chance(g, 50))) {
            record_path(g, f->record, r);
        }
        return;
    }
}

static bool
holds_record(const Gen *g, const RecordVar *v, u8 r, bool writable, u32 limit)
{
    return (v->record == r || has_record(g, v->record, r)) &&
           (!writable || v->assignable || v->pointer) &&
           (v->limit <= limit || !holds_slices(g, r));
}

static u32
count_record_places(const Gen *g, u8 r, bool writable, u32 limit)
{
    u32 n = 0;
    for (u32 i = 0; i < g->nrvars; i++) {
        n += holds_record(g, &g->rvars[i], r, writable, limit);
    }
    return n;
}

static void
record_place_in(Gen *g, const RecordVar *v, u8 r)
{
    g->in_path++;
    emit(g, "%s", v->name);
    if (v->length) {
        subscript(g, v->length, 0);
    }
    if (v->record != r) {
        record_path(g, v->record, r);
    } else if (v->pointer) {
        emit(g, ".*");
    }
    g->in_path--;
}

static void
record_place(Gen *g, u8 r, bool writable, u32 limit)
{
    u32 n = count_record_places(g, r, writable, limit);
    assert(n > 0);
    u32 k = (u32)random_below(g->rng, (int)n);
    for (u32 i = 0; i < g->nrvars; i++) {
        if (holds_record(g, &g->rvars[i], r, writable, limit) && k-- == 0) {
            record_place_in(g, &g->rvars[i], r);
            return;
        }
    }
}

static u8
index_type(Gen *g, u64 limit)
{
    u8 u = (u8)(T_U8 + random_below(g->rng, 4));
    while (max_of(u) < limit) {
        u++;
    }
    return u;
}

static u16
slice_base(Gen *g, const Var *v, u16 inner)
{
    array_base(g, v);
    if (v->inner == inner) {
        return v->length;
    }
    subscript(g, v->length, 0);
    return v->inner;
}

static void
dyn_slice(Gen *g, const Var *v, u16 inner)
{
    u16 n = slice_base(g, v, inner);
    u16 h = n / 2;
    u8 u = index_type(g, n + 1u);
    emit(g, "[(");
    nonliteral(g, u, 0);
    emit(g, " %% %u)..", h + 1u);

    bool inclusive = n && chance(g, 50);
    emit(g, inclusive ? "=(%u + (" : "(%u + (", h);
    nonliteral(g, u, 0);
    emit(g, " %% %u))]", n - h + !inclusive);
}

static void
literal_items(Gen *g, u8 t, u16 length, u16 inner)
{
    emit(g, "[");
    for (u16 k = 0; k < length; k++) {
        if (k) {
            emit(g, ", ");
        }
        if (!inner) {
            typed_expr(g, t, g->nesting ? 0 : chance(g, 20) ? 2 : 1);
        } else if (chance(g, 25) && can_shape(g, t, inner, 0, false)) {
            shaped(g, t, inner, 0, false);
        } else {
            if (chance(g, 30)) {
                emit(g, "%s", type_names[t]);
            }
            emit(g, ".");
            literal_items(g, t, inner, 0);
        }
    }
    emit(g, "]");
}

static bool slice_value(Gen *g, u32 limit, bool escaping, u8 t, u16 inner, u16 length, bool convert);
static void record_value(Gen *g, u32 limit, u8 r);
static void record_literal(Gen *g, u32 limit, u8 r);
static int pick_record_call(Gen *g, u8 r, u8 t);
static void record_call(Gen *g, u32 k);

static void
slice_argument(Gen *g, u8 t, u16 length, bool keep)
{
    if (!keep && length &&
        (count_sources(g, g->nvars, false, t, 0, length) == 0 || chance(g, 20))) {
        if (chance(g, 30)) {
            emit(g, "%s", type_names[t]);
        }
        emit(g, ".");
        g->nesting++;
        literal_items(g, t, length, 0);
        g->nesting--;
        return;
    }
    slice_value(g, g->nvars, false, t, 0, length, true);
}

static bool
can_call(const Gen *g, const Helper *h, u8 t, bool keep)
{
    if (h->level >= g->level || (t != NTYPES && (h->ret != t || h->ret_slice ||
                                                 h->ret_record != NO_RECORD))) {
        return false;
    }
    for (u32 p = 0; p < h->nparams; p++) {
        u8 u = h->params[p];
        if (h->slices[p]) {
            if ((keep || !h->lengths[p]) &&
                count_sources(g, g->nvars, false, u, 0, h->lengths[p]) == 0) {
                return false;
            }
            continue;
        }
        if (h->records[p] != NO_RECORD) {
            if (h->pointers[p] &&
                count_record_places(g, h->records[p], true, MAX_VARS) == 0) {
                return false;
            }
            continue;
        }
        if (h->lengths[p] && !can_shape(g, u, h->lengths[p], 0, true)) {
            return false;
        }
        if (h->pointers[p] && !h->lengths[p] && count_vars(g, u, true, false) == 0 &&
            count_vars(g, u, false, true) == 0 &&
            count_arrays(g, u, ANY, ANY, true) == 0) {
            return false;
        }
    }
    return true;
}

static u32
callable(const Gen *g, u8 t)
{
    u32 n = 0;
    for (u32 k = 0; k < g->nhelpers; k++) {
        n += can_call(g, &g->helpers[k], t, false);
    }
    return n;
}

static void
pointer_argument(Gen *g, u8 t, u16 length)
{
    if (length) {
        emit(g, "&");
        shaped(g, t, length, 0, true);
        return;
    }
    Var *q = pick_pointer(g, t);
    Var *v = pick_var(g, t, true);
    Var *a = pick_array(g, t, ANY, ANY, true);
    if (a && ((!q && !v) || chance(g, 30))) {
        emit(g, "&");
        element(g, a, 0);
    } else if (q && (!v || chance(g, 50))) {
        emit(g, "%s", q->name);
    } else {
        emit(g, "&%s", v->name);
    }
}

static void
call_args(Gen *g, u32 k, int depth, bool keep)
{
    const Helper *h = &g->helpers[k];
    emit(g, "h%u(", k);
    u8 shared = NO_RECORD;
    size_t shared_at = 0;
    size_t shared_len = 0;
    for (u32 p = 0; p < h->nparams; p++) {
        if (p) {
            emit(g, ", ");
        }
        u8 r = h->records[p];
        if (r != NO_RECORD && h->pointers[p]) {
            int call = chance(g, 15) ? pick_record_call(g, r, NTYPES) : -1;
            emit(g, "&");
            if (call >= 0) {
                record_call(g, (u32)call);
            } else {
                shared = r;
                shared_at = g->len;
                record_place(g, r, true, MAX_VARS);
                shared_len = g->len - shared_at;
            }
        } else if (r != NO_RECORD && r == shared && chance(g, 60)) {
            emit(g, "%.*s", (int)shared_len, g->out + shared_at);
        } else if (r != NO_RECORD &&
                   count_record_places(g, r, false, g->nvars) && chance(g, 50)) {
            record_place(g, r, false, g->nvars);
        } else if (r != NO_RECORD) {
            g->nesting++;
            record_value(g, g->nvars, r);
            g->nesting--;
        } else if (h->slices[p]) {
            slice_argument(g, h->params[p], h->lengths[p], keep);
        } else if (h->pointers[p]) {
            pointer_argument(g, h->params[p], h->lengths[p]);
        } else {
            typed_expr(g, h->params[p], depth - 1);
        }
    }
    emit(g, ")");
}

static bool
call_helper(Gen *g, u8 t, int depth)
{
    u32 n = callable(g, t);
    if (n == 0) {
        return false;
    }
    u32 pick = (u32)random_below(g->rng, (int)n);
    for (u32 k = 0; k < g->nhelpers; k++) {
        if (can_call(g, &g->helpers[k], t, false) && pick-- == 0) {
            call_args(g, k, depth, false);
            return true;
        }
    }
    return false;
}

static bool
returns_view(const Gen *g, const Helper *h, u8 t, u16 length)
{
    return h->ret_slice && h->ret == t && h->ret_length >= length &&
           can_call(g, h, NTYPES, true);
}

static int
pick_returning(Gen *g, u8 t, u16 length)
{
    u32 n = 0;
    for (u32 k = 0; k < g->nhelpers; k++) {
        n += returns_view(g, &g->helpers[k], t, length);
    }
    if (n == 0) {
        return -1;
    }
    u32 pick = (u32)random_below(g->rng, (int)n);
    for (u32 k = 0; k < g->nhelpers; k++) {
        if (returns_view(g, &g->helpers[k], t, length) && pick-- == 0) {
            return (int)k;
        }
    }
    return -1;
}

static bool
returns_record(const Gen *g, const Helper *h, u8 r, u8 t)
{
    if (h->ret_record == NO_RECORD || !can_call(g, h, NTYPES, false)) {
        return false;
    }
    return r != NO_RECORD ? h->ret_record == r : has_leaf(g, h->ret_record, t, false);
}

static int
pick_record_call(Gen *g, u8 r, u8 t)
{
    if (g->nesting >= 2) {
        return -1;
    }
    u32 n = 0;
    for (u32 k = 0; k < g->nhelpers; k++) {
        n += returns_record(g, &g->helpers[k], r, t);
    }
    if (n == 0) {
        return -1;
    }
    u32 pick = (u32)random_below(g->rng, (int)n);
    for (u32 k = 0; k < g->nhelpers; k++) {
        if (returns_record(g, &g->helpers[k], r, t) && pick-- == 0) {
            return (int)k;
        }
    }
    return -1;
}

static void
record_call(Gen *g, u32 k)
{
    g->nesting++;
    call_args(g, k, 2, false);
    g->nesting--;
}

static bool
call_leaf(Gen *g, u8 t)
{
    int k = pick_record_call(g, NO_RECORD, t);
    if (k < 0) {
        return false;
    }
    record_call(g, (u32)k);
    g->in_path++;
    leaf_path(g, g->helpers[k].ret_record, t, false);
    g->in_path--;
    return true;
}

static bool
slice_value(Gen *g, u32 limit, bool escaping, u8 t, u16 inner, u16 length, bool convert)
{
    u32 sources = count_sources(g, limit, escaping, t, inner, length);
    int k = limit == g->nvars && !escaping && !inner && g->nesting < 2 &&
                    (!sources || chance(g, 15))
              ? pick_returning(g, t, length)
              : -1;
    bool view = true;
    bool escapes = false;
    u16 n;
    if (k >= 0) {
        g->nesting++;
        call_args(g, (u32)k, 2, true);
        g->nesting--;
        n = g->helpers[k].ret_length;
    } else {
        Var *v = pick_source(g, limit, escaping, t, inner, length);
        assert(v);
        view = v->slice && v->inner == inner;
        escapes = v->escapes;
        n = slice_base(g, v, inner);
    }
    if (n == length && chance(g, view ? 25 : convert ? 40 : 0)) {
        return escapes;
    }

    u16 room = (u16)(n - length);
    int form = random_below(g->rng, 10);
    if (form < 2) {
        int k = random_below(g->rng, room + 1);
        emit(g, "[%d..%d]", k, k + length);
    } else if (form < 3 && length) {
        int k = random_below(g->rng, room + 1);
        emit(g, "[%d..=%d]", k, k + length - 1);
    } else if (form < 4) {
        emit(g, "[%u..]", room);
    } else if (form < 5 && length) {
        emit(g, "[..=%d]", length - 1);
    } else if (form < 6) {
        emit(g, "[..%u]", length);
    } else {
        u32 m = room + 1u + chance(g, 3);
        emit(g, "[(");
        nonliteral(g, index_type(g, m), 0);
        emit(g, " %% %u)..][..%u]", m, length);
    }
    return escapes;
}

static void
nonliteral(Gen *g, u8 t, int depth)
{
    if (depth > 0 && chance(g, 40)) {
        int form = random_below(g->rng, 4);
        if (form == 0 && !g->in_path && chance(g, 30) && call_leaf(g, t)) {
            return;
        }
        if (t == T_BOOL) {
            u8 u = random_type(g, false);
            static const char *const ops[] = { "<", "<=", ">", ">=", "==", "!=" };
            const char *op = u == T_BOOL ? ops[4 + random_below(g->rng, 2)]
                                         : ops[random_below(g->rng, 6)];
            emit(g, "(");
            nonliteral(g, u, depth - 1);
            emit(g, " %s ", op);
            expr(g, u, depth - 1);
            emit(g, ")");
            return;
        }
        if (form == 0 && call_helper(g, t, depth)) {
            return;
        }
        if (form == 1) {
            conversion(g, t, depth);
            return;
        }
        emit(g, "(");
        nonliteral(g, t, depth - 1);
        static const char *const int_ops[] = { "+",  "-", "*", "+%", "-%",
                                               "*%", "&", "|", "^" };
        static const char *const float_ops[] = { "+", "-", "*", "/" };
        emit(
            g,
            " %s ",
            is_float(t) ? float_ops[random_below(g->rng, 4)]
                        : int_ops[random_below(g->rng, 9)]
        );
        expr(g, t, depth - 1);
        emit(g, ")");
        return;
    }

    Var *q = chance(g, 25) ? pick_pointer(g, t) : NULL;
    if (q) {
        if (chance(g, 15)) {
            emit(g, "bitcast(*%s, bitcast(%s, %s)).*", type_names[t], chance(g, 50) ? "uintptr" : "*u8", q->name);
        } else {
            emit(g, "%s.*", q->name);
        }
        return;
    }
    Var *a = chance(g, 20) ? pick_array(g, t, ANY, ANY, false) : NULL;
    if (a) {
        element(g, a, 0);
        return;
    }
    Var *s = chance(g, 15) ? pick_slice(g, t, 1) : NULL;
    if (s) {
        element(g, s, 0);
        return;
    }
    Table *z = g->ntables && chance(g, 10) ? pick_table(g, t, 1, MAX_VARS) : NULL;
    if (z) {
        table_row(g, z, 0);
        subscript(g, z->inner, 0);
        return;
    }
    Var *w = t == T_INT && chance(g, 6) ? pick_view(g, ANY) : NULL;
    if (w && w->slice && chance(g, 50)) {
        emit(g, "%s.len", w->name);
        return;
    }
    if (w) {
        dyn_slice(g, w, w->inner && w->length && chance(g, 40) ? 0 : w->inner);
        emit(g, ".len");
        return;
    }
    Table *rows = t == T_INT && g->ntables && chance(g, 6)
                    ? pick_table(g, NTYPES, 0, MAX_VARS)
                    : NULL;
    if (rows && (!rows->slice || chance(g, 60))) {
        table_row(g, rows, 0);
        emit(g, ".len");
        return;
    }
    if (rows) {
        emit(g, "%s.len", rows->name);
        return;
    }
    RecordVar *o = !g->in_path && chance(g, 20) ? pick_record_var(g, t, false, false)
                                                : NULL;
    if (o) {
        record_leaf(g, o, t, false);
        return;
    }
    u8 u;
    RecordVar *l = t == T_INT && !g->in_path && g->nrvars && chance(g, 8)
                     ? pick_slice_leaf(g, false, &u)
                     : NULL;
    if (l) {
        record_leaf(g, l, u, true);
        emit(g, ".len");
        return;
    }
    Var *v = pick_var(g, t, false);
    if (v) {
        emit(g, "%s", v->name);
        return;
    }
    if (t == T_BOOL) {
        Var *n = pick_numeric_var(g);
        emit(g, "(%s < ", n->name);
        literal(g, n->type, false);
        emit(g, ")");
        return;
    }
    Var *n = pick_numeric_var(g);
    for (int k = 0; k < 3 && is_int(t) && is_float(n->type) && !chance(g, 8); k++) {
        n = pick_numeric_var(g);
    }
    convert_from(g, t, n->type, n->name);
}

static bool
widens_to(u8 from, u8 to)
{
    if (from == to || from == T_BOOL || to == T_BOOL ||
        (is_float(from) && !is_float(to))) {
        return false;
    }
    if (is_float(to)) {
        u32 mantissa = to == T_F32 ? 24 : 53;
        return is_float(from) ? type_bits[from] < type_bits[to]
                              : type_bits[from] - is_signed(from) <= mantissa;
    }
    return widens(from, to);
}

static void value_block(Gen *g, u8 t, int depth, bool typed);

static void
plain_value(Gen *g, u8 u, int depth)
{
    Var *v = pick_var(g, u, false);
    if (v && chance(g, 60)) {
        emit(g, "%s", v->name);
    } else {
        conversion(g, u, depth);
    }
}

static void
plain_block(Gen *g, u8 u, int depth)
{
    u32 name = g->names++;
    emit(g, "(b%u: { if ", name);
    expr(g, T_BOOL, 1);
    emit(g, " { break b%u ", name);
    plain_value(g, u, depth - 1);
    emit(g, " }; break b%u ", name);
    plain_value(g, u, depth - 1);
    emit(g, " })");
}

static void
break_value(Gen *g, Label *to, int depth)
{
    if (g->nvalues < MAX_LABELS && depth > 1 && chance(g, 8)) {
        value_block(g, to->type, depth, to->typed);
        to->given = true;
        return;
    }

    u8 narrower[NTYPES];
    u32 n = 0;
    for (u8 u = 0; u < NTYPES; u++) {
        if (widens_to(u, to->type)) {
            narrower[n++] = u;
        }
    }
    if (n == 0 || !chance(g, 30)) {
        to->given |= !expr(g, to->type, depth);
        return;
    }

    u8 u = narrower[random_below(g->rng, (int)n)];
    to->narrowed = true;
    if (depth > 1 && chance(g, 20)) {
        plain_block(g, u, depth);
        return;
    }
    Var *v = pick_var(g, u, false);
    if (v) {
        emit(g, "%s", v->name);
    } else {
        conversion(g, u, depth);
    }
}

static void
last_value(Gen *g, Label *to, int depth)
{
    bool defaults = to->type == T_INT || to->type == T_F32 || to->type == T_BOOL;
    if (to->typed && chance(g, 30)) {
        literal(g, to->type, false);
    } else if (to->typed || to->given) {
        break_value(g, to, depth);
    } else if (defaults && !to->narrowed && chance(g, 30)) {
        literal(g, to->type, false);
    } else {
        nonliteral(g, to->type, depth);
    }
}

static void
value_block(Gen *g, u8 t, int depth, bool typed)
{
    u32 own = g->nvalues;
    g->values[g->nvalues++] = (Label){ g->names++, t, false, false, typed };
    emit(g, "(b%u: { ", g->values[own].name);
    for (int k = random_below(g->rng, 3); k > 0; k--) {
        emit(g, "if ");
        expr(g, T_BOOL, depth - 1);
        Label *to = &g->values[chance(g, 70) ? own : (u32)random_below(g->rng, (int)own + 1)];
        emit(g, " { break b%u ", to->name);
        break_value(g, to, depth - 1);
        emit(g, " }; ");
    }
    emit(g, "break b%u ", g->values[own].name);
    last_value(g, &g->values[own], depth - 1);
    emit(g, " })");
    g->nvalues = own;
}

static void block(Gen *g, int depth, int count);

static bool
fits_type(u8 t, int64_t v)
{
    u32 bits = type_bits[t];
    if (is_signed(t)) {
        return bits == 64 || (v >= -(1ll << (bits - 1)) && v < (1ll << (bits - 1)));
    }
    return v >= 0 && (bits >= 63 || v < (1ll << bits));
}

static void
print_value(Gen *g, int64_t v)
{
    print_int(g, v < 0, v < 0 ? 0 - (u64)v : (u64)v);
}

static int
compare_values(const void *a, const void *b)
{
    int64_t x = *(const int64_t *)a, y = *(const int64_t *)b;
    return (x > y) - (x < y);
}

static u32
int_spans(Gen *g, u8 u, Span *out, u32 max)
{
    static const int64_t edges[] = { 127,   128,   255,  256,   -128,  -129,
                                     32767, 65535, 1000, -1000, 100000 };
    int64_t values[2 * MAX_ARMS * 3];
    u32 n = 0;
    for (u32 k = 0; k < 2 * max; k++) {
        int64_t v = chance(g, 70)
                      ? random_below(g->rng, 41) - 20
                      : edges[random_below(g->rng, (int)(sizeof edges / sizeof edges[0]))];
        if (fits_type(u, v)) {
            values[n++] = v;
        }
    }
    qsort(values, n, sizeof *values, compare_values);
    u32 count = 0;
    for (u32 k = 0; k < n && count < max; k++) {
        if (k > 0 && values[k] == values[k - 1]) {
            continue;
        }
        Span s = { values[k], values[k] };
        if (k + 1 < n && values[k + 1] > values[k] && chance(g, 25)) {
            s.hi = values[++k];
        }
        out[count++] = s;
    }
    return count;
}

static u32
full_spans(Gen *g, u8 u, Span *out)
{
    int64_t lo = u == T_I8 ? -128 : 0, hi = u == T_I8 ? 127 : 255;
    u32 count = 0;
    int64_t from = lo;
    for (u32 k = 0; k < 3 && from < hi; k++) {
        int64_t to = from + random_below(g->rng, (int)(hi - from));
        out[count++] = (Span){ from, to };
        from = to + 1;
    }
    if (from <= hi) {
        out[count++] = (Span){ from, hi };
    }
    return count;
}

static void
print_span(Gen *g, u8 u, Span s)
{
    print_value(g, s.lo);
    if (s.hi == s.lo) {
        return;
    }
    if (s.hi < INT64_MAX && fits_type(u, s.hi + 1) && chance(g, 30)) {
        emit(g, "..");
        print_value(g, s.hi + 1);
    } else {
        emit(g, "..=");
        print_value(g, s.hi);
    }
}

static void
print_member(Gen *g, u32 e, u32 m, bool literal_ok)
{
    if (literal_ok && chance(g, 60)) {
        emit(g, ".m%u", m);
    } else {
        emit(g, "E%u.m%u", e, m);
    }
}

static bool
enum_value(Gen *g, u32 e, int depth)
{
    const Enum *en = &g->enums[e];
    int r = random_below(g->rng, 100);
    if (r < 35 || depth <= 0) {
        emit(g, "E%u.m%u", e, random_below(g->rng, en->count));
        return true;
    }
    if (r < 55) {
        emit(g, "E%u(", e);
        print_value(g, en->values[random_below(g->rng, en->count)]);
        emit(g, ")");
        return true;
    }
    if (r < 92) {
        u8 u = random_int_type(g);
        emit(g, "(switch (");
        nonliteral(g, u, depth - 1);
        emit(g, " & 3) { ");
        for (u32 k = 0; k < 3; k++) {
            emit(g, "%u => ", k);
            print_member(g, e, (u32)random_below(g->rng, en->count), true);
            emit(g, ", ");
        }
        emit(g, "_ => E%u.m%u })", e, random_below(g->rng, en->count));
        return false;
    }
    emit(g, "E%u(", e);
    nonliteral(g, random_int_type(g), depth - 1);
    emit(g, ")");
    return false;
}

static bool
enum_int(Gen *g, u8 t, int depth)
{
    u32 e = (u32)random_below(g->rng, (int)g->nenums);
    const Enum *en = &g->enums[e];
    bool fit = true;
    for (u32 k = 0; k < en->count; k++) {
        fit &= fits_type(t, en->values[k]);
    }
    size_t start = g->len;
    bool constant = enum_value(g, e, depth - 1);
    char operand[PROGRAM_SIZE / 64];
    size_t n = g->len - start;
    assert(n < sizeof operand);
    memcpy(operand, g->out + start, n);
    operand[n] = 0;
    g->len = start;
    if (t == T_INT || ((fit || !constant) && chance(g, 20))) {
        emit(g, "%s(%s)", type_names[t], operand);
        return constant;
    }
    char wide[PROGRAM_SIZE / 64 + 8];
    snprintf(wide, sizeof wide, "int(%s)", operand);
    if (constant) {
        u64 mask = max_of(t) < max_of(T_INT) ? max_of(t) : max_of(T_INT);
        emit(g, "%s((%s & %llu))", type_names[t], wide, (unsigned long long)mask);
    } else {
        convert_from(g, t, T_INT, wide);
    }
    return constant;
}

static void
enum_compare(Gen *g, int depth)
{
    u32 e = (u32)random_below(g->rng, (int)g->nenums);
    emit(g, "(");
    enum_value(g, e, depth - 1);
    emit(g, chance(g, 50) ? " == " : " != ");
    print_member(g, e, (u32)random_below(g->rng, g->enums[e].count), true);
    emit(g, ")");
}

static u32
scrutinee(Gen *g, int depth, Span *spans, bool *exhaustive)
{
    if (g->nenums && chance(g, 35)) {
        u32 e = (u32)random_below(g->rng, (int)g->nenums);
        enum_value(g, e, depth);
        *exhaustive = chance(g, 50);
        u32 count = g->enums[e].count;
        g->nspans = *exhaustive ? count : 1 + (u32)random_below(g->rng, (int)count);
        return e;
    }
    *exhaustive = chance(g, 30);
    u8 u = *exhaustive ? (chance(g, 50) ? T_I8 : T_U8) : random_int_type(g);
    if (*exhaustive && chance(g, 30)) {
        u = T_BOOL;
    }
    if (u == T_BOOL) {
        expr(g, T_BOOL, depth);
        g->switch_type = u;
        g->nspans = 2;
        spans[0] = (Span){ 0, 0 };
        spans[1] = (Span){ 1, 1 };
        return NO_ENUM;
    }
    nonliteral(g, u, depth);
    g->switch_type = u;
    g->nspans = *exhaustive
                  ? full_spans(g, u, spans)
                  : int_spans(g, u, spans, 1 + (u32)random_below(g->rng, MAX_ARMS));
    return NO_ENUM;
}

static u32
arm_labels(Gen *g, u32 e, const Span *spans, u32 *next, bool last)
{
    u32 n = g->nspans;
    u32 take = last ? n - *next : 1 + (u32)random_below(g->rng, 2);
    if (take > n - *next) {
        take = n - *next;
    }
    for (u32 k = 0; k < take; k++) {
        emit(g, k ? ", " : "");
        u32 at = *next + k;
        if (e != NO_ENUM) {
            print_member(g, e, at, true);
        } else if (g->switch_type == T_BOOL) {
            emit(g, spans[at].lo ? "true" : "false");
        } else {
            print_span(g, g->switch_type, spans[at]);
        }
    }
    *next += take;
    return take;
}

static void
switch_value(Gen *g, u8 t, int depth, bool typed)
{
    Label own = { .type = t, .typed = typed };
    Span spans[MAX_ARMS];
    bool exhaustive;
    emit(g, "(switch ");
    u32 e = scrutinee(g, depth - 1, spans, &exhaustive);
    u8 type = g->switch_type;
    u32 nspans = g->nspans;
    emit(g, " { ");
    u32 n = nspans;
    u32 next = 0;
    while (next < n) {
        g->switch_type = type;
        g->nspans = nspans;
        bool last = exhaustive && next + 2 >= n;
        arm_labels(g, e, spans, &next, last);
        emit(g, " => ");
        if (exhaustive && next == n) {
            last_value(g, &own, depth - 1);
        } else {
            break_value(g, &own, depth - 1);
            emit(g, ", ");
        }
    }
    if (!exhaustive) {
        emit(g, "_ => ");
        last_value(g, &own, depth - 1);
    }
    emit(g, " })");
}

static void
record_result(Gen *g)
{
    u8 r = g->ret_record;
    if (g->result.typed) {
        record_value(g, g->nvars, r);
        return;
    }
    int k = chance(g, 20) ? pick_record_call(g, r, NTYPES) : -1;
    if (k >= 0) {
        record_call(g, (u32)k);
    } else if (count_record_places(g, r, false, g->nvars) && chance(g, 30)) {
        record_place(g, r, false, g->nvars);
    } else {
        emit(g, "R%u.", r);
        record_literal(g, g->nvars, r);
    }
}

static void
return_value(Gen *g, int depth)
{
    emit(g, "return ");
    if (g->ret_record != NO_RECORD) {
        record_result(g);
    } else if (g->ret_slice) {
        slice_value(g, g->nvars, true, g->ret, 0, g->ret_length, g->result.typed);
    } else {
        break_value(g, &g->result, depth);
    }
}

static void
switch_statement(Gen *g, int depth)
{
    line(g);
    u32 label = 0;
    if (g->nblock_labels < MAX_LABELS && chance(g, 25)) {
        label = g->names++;
        emit(g, "d%u: ", label);
    }
    Span spans[MAX_ARMS];
    bool exhaustive;
    emit(g, "switch ");
    u32 e = scrutinee(g, 2, spans, &exhaustive);
    u8 type = g->switch_type;
    u32 nspans = g->nspans;
    emit(g, " {");
    if (label) {
        g->block_labels[g->nblock_labels++] = label;
    }
    g->indent++;
    u32 n = nspans;
    u32 next = 0;
    bool any = false;
    while (next < n || (!exhaustive && !any)) {
        g->switch_type = type;
        g->nspans = nspans;
        line(g);
        if (next < n) {
            arm_labels(g, e, spans, &next, exhaustive && next + 2 >= n);
        } else {
            emit(g, "_");
            any = true;
        }
        if (!exhaustive && next == n && !any && chance(g, 40)) {
            any = true;
            emit(g, ", _");
        }
        emit(g, " => ");
        if (!g->in_defer && chance(g, 25)) {
            return_value(g, 2);
        } else {
            emit(g, "{");
            g->indent++;
            block(g, depth - 1, 1 + random_below(g->rng, 2));
            g->indent--;
            line(g);
            emit(g, "}");
        }
    }
    g->indent--;
    if (label) {
        g->nblock_labels--;
    }
    line(g);
    emit(g, "}");
}

static void
enum_declarations(Gen *g)
{
    static const u8 backings[] = { NTYPES, NTYPES, T_U8,  T_I8,
                                   T_U16,  T_I32,  T_INT, T_U64 };
    g->nenums = MAX_ENUMS;
    for (u32 e = 0; e < MAX_ENUMS; e++) {
        Enum *en = &g->enums[e];
        en->backing = backings[random_below(g->rng, (int)sizeof backings)];
        bool negative = en->backing == NTYPES || is_signed(en->backing);
        int64_t v = negative && chance(g, 30) ? -(int64_t)random_below(g->rng, 130)
                                              : random_below(g->rng, 4);
        u32 want = 1 + (u32)random_below(g->rng, MAX_MEMBERS);
        emit(
            g,
            "enum E%u%s%s {",
            e,
            en->backing == NTYPES ? "" : ": ",
            en->backing == NTYPES ? "" : type_names[en->backing]
        );
        en->count = 0;
        bool automatic = v == 0;
        while (en->count < want) {
            u8 check = en->backing == NTYPES ? T_I16 : en->backing;
            if (!fits_type(check, v)) {
                break;
            }
            emit(g, "%s m%u", en->count ? "," : "", en->count);
            if (!automatic) {
                emit(g, " = ");
                print_value(g, v);
            }
            en->values[en->count++] = v;
            automatic = chance(g, 60);
            v += automatic ? 1 : 2 + random_below(g->rng, 60);
        }
        if (en->count == 0) {
            emit(g, " m0 = 0");
            en->values[en->count++] = 0;
        }
        emit(g, " }\n");
    }
}

static void
lay_out_record(Gen *g, Record *rec)
{
    u32 size = 0;
    u32 align = 1;
    u8 leaf = NTYPES;
    for (u8 k = 0; k < rec->count; k++) {
        const Field *f = &rec->fields[k];
        u32 bytes = type_bits[f->type] / 8u;
        u32 a = bytes;
        u8 l = is_float(f->type) ? f->type : NTYPES;
        if (f->slice) {
            bytes = 16;
            a = 8;
            l = NTYPES;
        } else if (f->record != NO_RECORD) {
            const Record *in = &g->records[f->record];
            bytes = in->size;
            a = in->align;
            l = in->leaf;
        }
        size = (size + a - 1) / a * a + bytes * (f->length ? f->length : 1u);
        align = a > align ? a : align;
        leaf = k == 0 || l == leaf ? l : NTYPES;
    }
    rec->size = (size + align - 1) / align * align;
    rec->align = (u8)align;
    rec->leaf = leaf;

    u32 piece = leaf == NTYPES ? 0 : type_bits[leaf] / 8u;
    rec->vregs = piece && rec->size <= 4 * piece ? (u8)(rec->size / piece) : 0;
}

static void
record_declarations(Gen *g)
{
    g->nrecords = chance(g, 60) ? 1 + (u32)random_below(g->rng, MAX_RECORDS) : 0;
    for (u32 r = 0; r < g->nrecords; r++) {
        Record *rec = &g->records[r];
        rec->count = (u8)(1 + random_below(g->rng, MAX_FIELDS));
        u8 uniform = chance(g, 15) ? (chance(g, 50) ? T_F32 : T_F64) : NTYPES;
        emit(g, "struct R%u {", r);
        for (u8 k = 0; k < rec->count; k++) {
            Field *f = &rec->fields[k];
            int form = random_below(g->rng, 100);
            f->type = uniform != NTYPES ? uniform : random_type(g, false);
            f->record = r && form >= 70 ? (u8)random_below(g->rng, (int)r) : NO_RECORD;
            if (uniform != NTYPES && f->record != NO_RECORD &&
                g->records[f->record].leaf != uniform) {
                f->record = NO_RECORD;
            }
            f->length = (form >= 55 && form < 70) || form >= 90
                          ? (u8)(1 + random_below(g->rng, 4))
                          : 0;
            f->slice = uniform == NTYPES && form >= 35 && form < 55;
            f->given = f->record == NO_RECORD && !f->length && !f->slice &&
                       chance(g, 35);
            emit(g, "%s f%u: ", k ? "," : "", k);
            if (f->length) {
                emit(g, "[%u]", f->length);
            } else if (f->slice) {
                emit(g, "[]");
            }
            if (f->record != NO_RECORD) {
                emit(g, "R%u", f->record);
            } else {
                emit(g, "%s", type_names[f->type]);
            }
            if (f->given) {
                emit(g, " = ");
                literal(g, f->type, false);
            }
        }
        emit(g, " }\n");
        lay_out_record(g, rec);
    }
}

static void
typed_expr(Gen *g, u8 t, int depth)
{
    if (g->nvalues < MAX_LABELS && depth > 1 && chance(g, 8)) {
        value_block(g, t, depth, true);
    } else if (depth > 1 && chance(g, 5)) {
        switch_value(g, t, depth, true);
    } else {
        expr(g, t, depth);
    }
}

static bool
expr(Gen *g, u8 t, int depth)
{
    if (depth <= 0 || chance(g, 25)) {
        if (chance(g, 30)) {
            Var *a = t == T_INT && chance(g, 20)
                       ? pick_array(g, NTYPES, ANY, ANY, false)
                       : NULL;
            if (a) {
                array_base(g, a);
                if (a->inner && chance(g, 30)) {
                    subscript(g, a->length, 0);
                }
                emit(g, ".len");
                return true;
            }
            literal(g, t, false);
            return true;
        }
        nonliteral(g, t, 0);
        return false;
    }

    if (g->nvalues < MAX_LABELS && depth > 1 && chance(g, 6)) {
        value_block(g, t, depth, false);
        return false;
    }
    if (depth > 1 && chance(g, 4)) {
        switch_value(g, t, depth, false);
        return false;
    }
    if (is_int(t) && g->nenums && chance(g, 5)) {
        return enum_int(g, t, depth);
    }
    if (t == T_BOOL && g->nenums && chance(g, 6)) {
        enum_compare(g, depth);
        return false;
    }

    if (t == T_BOOL) {
        int form = random_below(g->rng, 7);
        if (form == 6) {
            u8 u = random_int_type(g);
            emit(g, "((");
            nonliteral(g, u, depth - 1);
            emit(g, chance(g, 50) ? " & " : " %% ");
            literal(g, u, true);
            emit(g, chance(g, 50) ? ") == 0)" : ") != 0)");
        } else if (form < 3) {
            u8 u = random_type(g, false);
            static const char *const ops[] = { "<", "<=", ">", ">=", "==", "!=" };
            const char *op = u == T_BOOL ? ops[4 + random_below(g->rng, 2)]
                                         : ops[random_below(g->rng, 6)];
            emit(g, "(");
            bool constant = expr(g, u, depth - 1);
            emit(g, " %s ", op);
            if (constant) {
                nonliteral(g, u, depth - 1);
            } else {
                expr(g, u, depth - 1);
            }
            emit(g, ")");
        } else if (form < 5) {
            emit(g, "(");
            expr(g, T_BOOL, depth - 1);
            emit(g, form == 3 ? " and " : " or ");
            expr(g, T_BOOL, depth - 1);
            emit(g, ")");
        } else {
            emit(g, "!");
            nonliteral(g, T_BOOL, depth - 1);
        }
        return false;
    }

    int form = random_below(g->rng, 12);
    if (form < 6) {
        static const char *const int_ops[] = { "+",  "-",  "*",  "/",
                                               "%",  "+%", "-%", "*%",
                                               "+%", "-%", "*%", "&",
                                               "|",  "^",  "&",  "^" };
        static const char *const float_ops[] = { "+", "-", "*", "/" };
        const char *op = is_float(t) ? float_ops[random_below(g->rng, 4)]
                                     : int_ops[random_below(g->rng, 16)];
        bool divides = is_int(t) && (op[0] == '/' || op[0] == '%');
        emit(g, "(");
        bool constant = expr(g, t, depth - 1);
        emit(g, " %s ", op);
        if (constant || (divides && chance(g, 50))) {
            nonliteral(g, t, depth - 1);
        } else if (divides) {
            literal(g, t, true);
        } else {
            expr(g, t, depth - 1);
        }
        emit(g, ")");
        return false;
    }
    if (form < 7 && is_int(t)) {
        emit(g, "(");
        nonliteral(g, t, depth - 1);
        emit(g, chance(g, 50) ? " << " : " >> ");
        if (chance(g, 50)) {
            emit(g, "%d", random_below(g->rng, type_bits[t]));
        } else {
            u8 u = random_int_type(g);
            emit(g, "(");
            nonliteral(g, u, depth - 1);
            emit(g, " & %u)", chance(g, 90) ? type_bits[t] - 1 : 127u);
        }
        emit(g, ")");
        return false;
    }
    if (form < 9) {
        static const char *const int_unary[] = { "-", "-%", "~" };
        int k = is_signed(t) ? random_below(g->rng, 3) : 1 + random_below(g->rng, 2);
        emit(g, "%s", is_float(t) ? "-" : int_unary[chance(g, 3) ? 0 : k]);
        nonliteral(g, t, depth - 1);
        return false;
    }
    if (form < 11) {
        conversion(g, t, depth);
        return false;
    }
    if (!call_helper(g, t, depth)) {
        nonliteral(g, t, depth - 1);
    }
    return false;
}

static void
loop_label(Gen *g)
{
    g->loop_labels[g->loops] = 0;
    if (chance(g, 40)) {
        g->loop_labels[g->loops] = g->names++;
        emit(g, "l%u: ", g->loop_labels[g->loops]);
    }
}

static void
assignment(Gen *g, u8 t)
{
    if (t == T_BOOL || chance(g, 40)) {
        emit(g, " = ");
        expr(g, t, 3);
        return;
    }
    if (is_float(t)) {
        static const char *const ops[] = { "+=", "-=", "*=", "/=" };
        emit(g, " %s ", ops[random_below(g->rng, 4)]);
        expr(g, t, 2);
        return;
    }
    static const char *const ops[] = { "+=",  "-=",  "*=", "+%=", "-%=",
                                       "*%=", "/=",  "%=", "&=",  "|=",
                                       "^=",  "<<=", ">>=" };
    int op = random_below(g->rng, 13);
    emit(g, " %s ", ops[op]);
    if (op >= 11) {
        emit(g, "(");
        nonliteral(g, random_int_type(g), 1);
        emit(g, " & %u)", type_bits[t] - 1);
    } else if (op == 6 || op == 7) {
        if (chance(g, 50)) {
            literal(g, t, true);
        } else {
            nonliteral(g, t, 2);
        }
    } else {
        expr(g, t, 2);
    }
}

static void
pointer_statement(Gen *g)
{
    Var *q = pick_pointer(g, NTYPES);
    if (!q || chance(g, 25)) {
        Var *v = pick_var(g, NTYPES, true);
        if (!v || g->nvars == MAX_VARS) {
            return;
        }
        line(g);
        u32 n = g->names++;
        emit(g, "q%u := &%s", n, v->name);
        declare_as(g, 'q', n, v->type, false)->pointer = true;
        return;
    }
    line(g);
    if (chance(g, 15)) {
        emit(g, "bitcast(*%s, bitcast(uintptr, %s)).*", type_names[q->type], q->name);
    } else {
        emit(g, "%s.*", q->name);
    }
    assignment(g, q->type);
}

static void
print_shape(Gen *g, u8 t, u16 length, u16 inner)
{
    emit(g, "[%u]", length);
    if (inner) {
        emit(g, "[%u]", inner);
    }
    emit(g, "%s", type_names[t]);
}

static void
typed_literal(Gen *g, u8 t, u16 length, u16 inner)
{
    if (inner) {
        emit(g, "[%u]", inner);
    }
    emit(g, "%s.", type_names[t]);
    literal_items(g, t, length, inner);
}

static u16
random_length(Gen *g)
{
    static const u16 long_lengths[] = { 9, 16, 33, 100, 700, 4100 };
    if (chance(g, 10)) {
        return long_lengths[random_below(g->rng, sizeof long_lengths / sizeof long_lengths[0])];
    }
    return (u16)(1 + random_below(g->rng, 6));
}

static void
helper_shape(Gen *g, u8 *t, u16 *length)
{
    u32 n = 0;
    for (u32 k = 0; k < g->nhelpers; k++) {
        const Helper *h = &g->helpers[k];
        for (u32 p = 0; p < h->nparams; p++) {
            n += h->level < g->level && h->lengths[p];
        }
    }
    if (n == 0) {
        return;
    }
    u32 pick = (u32)random_below(g->rng, (int)n);
    for (u32 k = 0; k < g->nhelpers; k++) {
        const Helper *h = &g->helpers[k];
        for (u32 p = 0; p < h->nparams; p++) {
            if (h->level < g->level && h->lengths[p] && pick-- == 0) {
                *t = h->params[p];
                *length = h->lengths[p];
                return;
            }
        }
    }
}

static void
field_type(Gen *g, u8 *t)
{
    u32 n = 0;
    for (u32 r = 0; r < g->nrecords; r++) {
        for (u8 k = 0; k < g->records[r].count; k++) {
            n += g->records[r].fields[k].slice;
        }
    }
    if (n == 0) {
        return;
    }
    u32 pick = (u32)random_below(g->rng, (int)n);
    for (u32 r = 0; r < g->nrecords; r++) {
        for (u8 k = 0; k < g->records[r].count; k++) {
            const Field *f = &g->records[r].fields[k];
            if (f->slice && pick-- == 0) {
                *t = f->type;
                return;
            }
        }
    }
}

static void
declare_array(Gen *g)
{
    if (g->nvars == MAX_VARS) {
        return;
    }

    u8 t = random_type(g, false);
    u16 length = random_length(g);
    u16 inner = 0;
    Var *like = chance(g, 40) ? pick_array(g, NTYPES, ANY, ANY, false) : NULL;
    if (like) {
        t = like->type;
        length = like->length;
        inner = like->inner;
    } else if (chance(g, 15)) {
        length = (u16)(1 + random_below(g->rng, 4));
        inner = (u16)(1 + random_below(g->rng, 4));
    } else if (chance(g, 40)) {
        helper_shape(g, &t, &length);
    } else if (chance(g, 50)) {
        field_type(g, &t);
    }

    line(g);
    u32 n = g->names++;
    bool source = can_shape(g, t, length, inner, false);
    bool small = length <= MAX_LITERAL;
    int form = random_below(g->rng, 10);
    if (form < 3 || (!source && !small)) {
        emit(g, "a%u: ", n);
        print_shape(g, t, length, inner);
    } else if (form < 6 && small) {
        emit(g, "a%u: ", n);
        print_shape(g, t, length, inner);
        emit(g, " = .");
        literal_items(g, t, length, inner);
    } else {
        if (chance(g, 50)) {
            emit(g, "a%u := ", n);
        } else {
            emit(g, "a%u: ", n);
            print_shape(g, t, length, inner);
            emit(g, " = ");
        }
        if (source && (!small || chance(g, 50))) {
            shaped(g, t, length, inner, false);
        } else {
            typed_literal(g, t, length, inner);
        }
    }
    Var *v = declare_as(g, 'a', n, t, true);
    v->length = length;
    v->inner = (u8)inner;
}

static void
array_value(Gen *g, u8 t, u16 length, u16 inner)
{
    int form = random_below(g->rng, 10);
    if (length > MAX_LITERAL || form < 4) {
        shaped(g, t, length, inner, false);
    } else if (form < 7) {
        emit(g, ".");
        literal_items(g, t, length, inner);
    } else {
        typed_literal(g, t, length, inner);
    }
}

static void
array_loop(Gen *g, const Var *a, int depth)
{
    if (g->nvars + 2 > MAX_VARS || a->length > MAX_ITERATED) {
        return;
    }

    u8 t = a->type;
    u16 length = a->length;
    u16 inner = a->inner;
    line(g);
    loop_label(g);
    u32 x = g->names++;
    u32 i = g->names++;
    bool indexed = chance(g, 50);
    bool discard = indexed && chance(g, 20);
    emit(g, discard ? "for _" : "for x%u", x);
    if (indexed) {
        emit(g, ", j%u", i);
    }
    emit(g, " in ");
    array_base(g, a);
    if (inner && length && chance(g, 40)) {
        subscript(g, length, 0);
        length = inner;
        inner = 0;
    }
    emit(g, " {");

    u32 saved = g->nvars;
    if (!discard) {
        declare_as(g, 'x', x, t, false)->length = inner;
    }
    if (indexed) {
        g->bounds[g->nbounds++] = (Bound){ g->nvars, length };
        declare_as(g, 'j', i, T_INT, false);
    }
    g->loops++;
    block(g, depth - 1, 1 + random_below(g->rng, 3));
    g->loops--;
    g->nbounds -= indexed;
    g->nvars = saved;
    line(g);
    emit(g, "}");
}

static void
array_pointer(Gen *g, const Var *a)
{
    line(g);
    u32 n = g->names++;
    if (chance(g, 50)) {
        emit(g, "q%u := &", n);
        element(g, a, 1);
        declare_as(g, 'q', n, a->type, false)->pointer = true;
        return;
    }

    u16 length = a->length;
    u16 inner = a->inner;
    emit(g, "r%u := &", n);
    array_base(g, a);
    if (inner && chance(g, 40)) {
        subscript(g, length, 1);
        length = inner;
        inner = 0;
    }
    Var *r = declare_as(g, 'r', n, a->type, false);
    r->pointer = true;
    r->escapes = a->escapes;
    r->length = length;
    r->inner = (u8)inner;
}

static void
fill_value(Gen *g, u8 t, u32 n)
{
    if (t == T_BOOL) {
        emit(g, "(((n%u ^ ", n);
        nonliteral(g, T_INT, 0);
        emit(g, ") & 1) == 0)");
        return;
    }
    emit(g, "%s(((n%u ^ ", type_names[t], n);
    nonliteral(g, T_INT, 0);
    u64 mask = is_float(t)               ? 65535
             : max_of(t) < max_of(T_INT) ? max_of(t)
                                         : max_of(T_INT);
    emit(g, ") & %llu))", (unsigned long long)mask);
}

static void
array_fill(Gen *g, const Var *a)
{
    u32 n = g->names++;
    emit(g, "for n%u in 0..", n);
    array_base(g, a);
    emit(g, ".len {");
    g->indent++;
    line(g);
    array_base(g, a);
    emit(g, "[n%u] = ", n);
    fill_value(g, a->type, n);
    g->indent--;
    line(g);
    emit(g, "}");
}

static void
array_statement(Gen *g, int depth)
{
    Var *a = pick_array(g, NTYPES, ANY, ANY, true);
    int pick = random_below(g->rng, 12);
    if (!a || pick < 3) {
        declare_array(g);
        return;
    }
    if (pick < 5 && depth > 0 && g->loops < g->max_loops) {
        array_loop(g, pick_array(g, NTYPES, ANY, ANY, false), depth);
        return;
    }
    if (pick < 6 && g->nvars < MAX_VARS) {
        array_pointer(g, a);
        return;
    }

    line(g);
    if (pick == 9 && !a->inner) {
        array_fill(g, a);
        return;
    }
    if (pick < 10) {
        element(g, a, 1);
        assignment(g, a->type);
        return;
    }
    u16 length = a->length;
    u16 inner = a->inner;
    array_base(g, a);
    if (inner && chance(g, 50)) {
        subscript(g, length, 1);
        length = inner;
        inner = 0;
    }
    emit(g, " = ");
    array_value(g, a->type, length, inner);
}

static Var *
view_shape(Gen *g, u16 max_length, u8 *t, u16 *inner, u16 *length)
{
    Var *v = pick_view(g, max_length);
    if (!v) {
        return NULL;
    }
    *t = v->type;
    *inner = v->inner && v->length && chance(g, 40) ? 0 : v->inner;
    *length = (u16)random_below(g->rng, sliceable(v, *t, *inner) + 1);
    return v;
}

static bool
call_shape(Gen *g, u8 *t, u16 *inner, u16 *length)
{
    if (g->nesting || !chance(g, 30)) {
        return false;
    }
    u32 n = 0;
    for (u32 k = 0; k < g->nhelpers; k++) {
        const Helper *h = &g->helpers[k];
        n += h->ret_slice && can_call(g, h, NTYPES, true);
    }
    if (n == 0) {
        return false;
    }
    u32 pick = (u32)random_below(g->rng, (int)n);
    for (u32 k = 0; k < g->nhelpers; k++) {
        const Helper *h = &g->helpers[k];
        if (h->ret_slice && can_call(g, h, NTYPES, true) && pick-- == 0) {
            *t = h->ret;
            *inner = 0;
            *length = (u16)random_below(g->rng, h->ret_length + 1);
            return true;
        }
    }
    return false;
}

static void
declare_slice(Gen *g)
{
    u8 t;
    u16 inner;
    u16 length;
    if (g->nvars == MAX_VARS || (!call_shape(g, &t, &inner, &length) &&
                                 !view_shape(g, ANY, &t, &inner, &length))) {
        return;
    }

    line(g);
    u32 n = g->names++;
    bool typed = chance(g, 50);
    if (typed) {
        emit(g, "s%u: []", n);
        if (inner) {
            emit(g, "[%u]", inner);
        }
        emit(g, "%s = ", type_names[t]);
    } else {
        emit(g, "s%u := ", n);
    }
    bool escapes = slice_value(g, g->nvars, false, t, inner, length, typed);
    Var *s = declare_as(g, 's', n, t, true);
    s->slice = true;
    s->escapes = escapes;
    s->inner = (u8)inner;
    s->length = length;
}

static void
shrink_loop(Gen *g, int depth)
{
    u8 t;
    u16 inner;
    u16 length;
    if (g->nvars == MAX_VARS || !view_shape(g, MAX_ITERATED, &t, &inner, &length)) {
        return;
    }

    u32 w = g->names++;
    u32 x = g->names++;
    line(g);
    emit(g, "w%u := ", w);
    slice_value(g, g->nvars, false, t, inner, length, false);
    line(g);
    loop_label(g);
    emit(g, "while w%u.len > 0 {", w);
    g->indent++;
    line(g);
    emit(g, "x%u := w%u[0]", x, w);
    line(g);
    emit(g, "w%u = w%u[1..]", w, w);
    g->indent--;

    u32 saved = g->nvars;
    declare_as(g, 'x', x, t, false)->length = inner;
    g->loops++;
    block(g, depth - 1, 1 + random_below(g->rng, 3));
    g->loops--;
    g->nvars = saved;
    line(g);
    emit(g, "}");
}

static void
dyn_loop(Gen *g, int depth)
{
    u8 t;
    u16 inner;
    u16 length;
    bool call = call_shape(g, &t, &inner, &length);
    Var *v = call ? NULL : pick_view(g, MAX_ITERATED);
    if ((!call && !v) || g->nvars + 2 > MAX_VARS) {
        return;
    }

    if (v) {
        t = v->type;
        inner = v->inner && v->length && chance(g, 40) ? 0 : v->inner;
    }
    u32 x = g->names++;
    u32 i = g->names++;
    bool indexed = chance(g, 30);
    line(g);
    loop_label(g);
    emit(g, "for x%u", x);
    if (indexed) {
        emit(g, ", j%u", i);
    }
    emit(g, " in ");
    if (call) {
        slice_value(g, g->nvars, false, t, 0, length, false);
    } else {
        dyn_slice(g, v, inner);
    }
    emit(g, " {");

    u32 saved = g->nvars;
    declare_as(g, 'x', x, t, false)->length = inner;
    if (indexed) {
        declare_as(g, 'j', i, T_INT, false);
    }
    g->loops++;
    block(g, depth - 1, 1 + random_below(g->rng, 3));
    g->loops--;
    g->nvars = saved;
    line(g);
    emit(g, "}");
}

static Var *
pick_slice_var(Gen *g, bool pointer)
{
    u32 n = 0;
    for (u32 i = 0; i < g->nvars; i++) {
        const Var *v = &g->vars[i];
        n += v->slice && v->pointer == pointer && (pointer || v->assignable);
    }
    if (n == 0) {
        return NULL;
    }
    u32 k = (u32)random_below(g->rng, (int)n);
    for (u32 i = 0; i < g->nvars; i++) {
        Var *v = &g->vars[i];
        if (v->slice && v->pointer == pointer && (pointer || v->assignable) &&
            k-- == 0) {
            return v;
        }
    }
    return NULL;
}

static void
slice_pointer(Gen *g)
{
    Var *q = pick_slice_var(g, true);
    if (!q || chance(g, 40)) {
        Var *s = pick_slice_var(g, false);
        if (!s || g->nvars == MAX_VARS) {
            return;
        }
        line(g);
        u32 n = g->names++;
        emit(g, "q%u := &%s", n, s->name);
        Var *p = declare_as(g, 'q', n, s->type, false);
        p->pointer = true;
        p->slice = true;
        p->escapes = s->escapes;
        p->inner = s->inner;
        p->length = s->length;
        p->limit = (u8)(s - g->vars + 1);
        return;
    }
    if (!count_sources(g, q->limit, q->escapes, q->type, q->inner, q->length)) {
        return;
    }
    line(g);
    emit(g, "%s.* = ", q->name);
    slice_value(g, q->limit, q->escapes, q->type, q->inner, q->length, true);
}

static void
slice_statement(Gen *g, int depth)
{
    Var *s = pick_slice(g, NTYPES, 0);
    int pick = random_below(g->rng, 14);
    if (pick >= 12) {
        slice_pointer(g);
        return;
    }
    bool loop = depth > 0 && g->loops < g->max_loops;
    if (!s || pick < 3) {
        declare_slice(g);
        return;
    }
    if (pick < 5 && loop) {
        array_loop(g, s, depth);
        return;
    }
    if (pick < 6 && loop) {
        shrink_loop(g, depth);
        return;
    }
    if (pick < 7 && loop) {
        dyn_loop(g, depth);
        return;
    }
    if (pick < 8 && s->assignable) {
        line(g);
        emit(g, "%s = ", s->name);
        u32 limit = (u32)(s - g->vars) + 1;
        slice_value(g, limit, s->escapes, s->type, s->inner, s->length, true);
        return;
    }
    if (pick < 9 && !s->inner) {
        line(g);
        array_fill(g, s);
        return;
    }
    if (s->length) {
        line(g);
        element(g, s, 1);
        assignment(g, s->type);
    }
}

static void
record_value(Gen *g, u32 limit, u8 r)
{
    int k = chance(g, 20) ? pick_record_call(g, r, NTYPES) : -1;
    if (k >= 0) {
        record_call(g, (u32)k);
        return;
    }
    if (count_record_places(g, r, false, limit) && chance(g, 30)) {
        record_place(g, r, false, limit);
        return;
    }
    if (chance(g, 30)) {
        emit(g, "R%u", r);
    }
    emit(g, ".");
    record_literal(g, limit, r);
}

static void
record_items(Gen *g, u32 limit, u8 r, u8 length)
{
    emit(g, "[");
    for (u8 k = 0; k < length; k++) {
        emit(g, k ? ", " : "");
        record_value(g, limit, r);
    }
    emit(g, "]");
}

static void
any_slice(Gen *g, u32 limit, u8 t)
{
    Var *v = pick_source(g, limit, false, t, 0, 0);
    u16 length = (u16)random_below(g->rng, sliceable(v, t, 0) + 1);
    slice_value(g, limit, false, t, 0, length, true);
}

static bool
fills(const Gen *g, u32 limit, const Field *f)
{
    return !f->slice || count_sources(g, limit, false, f->type, 0, 0) > 0;
}

static void
field_value(Gen *g, u32 limit, const Field *f)
{
    if (f->slice) {
        any_slice(g, limit, f->type);
    } else if (f->record == NO_RECORD && !f->length) {
        expr(g, f->type, g->nesting ? 0 : 1);
    } else if (f->record == NO_RECORD) {
        if (chance(g, 30)) {
            emit(g, "%s", type_names[f->type]);
        }
        emit(g, ".");
        literal_items(g, f->type, f->length, 0);
    } else if (f->length) {
        if (chance(g, 30)) {
            emit(g, "R%u", f->record);
        }
        emit(g, ".");
        record_items(g, limit, f->record, f->length);
    } else {
        record_value(g, limit, f->record);
    }
}

static void
record_literal(Gen *g, u32 limit, u8 r)
{
    const Record *rec = &g->records[r];
    bool all = true;
    for (u8 k = 0; k < rec->count; k++) {
        all = all && fills(g, limit, &rec->fields[k]);
    }
    if (all && chance(g, 50)) {
        emit(g, "{ ");
        for (u8 k = 0; k < rec->count; k++) {
            emit(g, k ? ", " : "");
            field_value(g, limit, &rec->fields[k]);
        }
        emit(g, " }");
        return;
    }

    u8 order[MAX_FIELDS];
    for (u8 k = 0; k < rec->count; k++) {
        order[k] = k;
    }
    for (u8 k = rec->count; k > 1; k--) {
        u8 j = (u8)random_below(g->rng, k);
        u8 swap = order[k - 1];
        order[k - 1] = order[j];
        order[j] = swap;
    }
    u8 n = (u8)random_below(g->rng, rec->count + 1);
    u8 kept = 0;
    for (u8 k = 0; k < n; k++) {
        if (fills(g, limit, &rec->fields[order[k]])) {
            order[kept++] = order[k];
        }
    }
    if (kept == 0) {
        emit(g, "{}");
        return;
    }
    emit(g, "{ ");
    for (u8 k = 0; k < kept; k++) {
        emit(g, k ? ", .f%u = " : ".f%u = ", order[k]);
        field_value(g, limit, &rec->fields[order[k]]);
    }
    emit(g, " }");
}

static RecordVar *
add_record_var(Gen *g, char prefix, u32 n, u8 r)
{
    assert(g->nrvars < MAX_RECORD_VARS);
    RecordVar *v = &g->rvars[g->nrvars++];
    snprintf(v->name, NAME_LEN, "%c%u", prefix, n);
    v->record = r;
    v->length = 0;
    v->limit = (u8)g->nvars;
    v->pointer = false;
    v->assignable = true;
    return v;
}

static void
backing_array(Gen *g, u8 t)
{
    u16 length = (u16)(1 + random_below(g->rng, 6));
    u32 n = g->names++;
    line(g);
    if (chance(g, 30)) {
        emit(g, "a%u: [%u]%s", n, length, type_names[t]);
    } else {
        emit(g, "a%u := ", n);
        typed_literal(g, t, length, 0);
    }
    declare_as(g, 'a', n, t, true)->length = length;
}

static void
declare_record(Gen *g)
{
    if (g->nrecords == 0 || g->nrvars == MAX_RECORD_VARS) {
        return;
    }

    u8 r = (u8)random_below(g->rng, (int)g->nrecords);
    const Record *rec = &g->records[r];
    for (u8 k = 0; k < rec->count && g->nvars < MAX_VARS; k++) {
        const Field *f = &rec->fields[k];
        if (fills(g, g->nvars, f) || chance(g, 30)) {
            continue;
        }
        backing_array(g, f->type);
    }
    u8 length = chance(g, 25) ? (u8)(1 + random_below(g->rng, 4)) : 0;
    u32 n = g->names++;
    int form = random_below(g->rng, 10);
    int k = !length && form == 7 ? pick_record_call(g, r, NTYPES) : -1;
    line(g);
    u32 limit = g->nvars;
    if (length && form < 4) {
        emit(g, "o%u: [%u]R%u", n, length, r);
    } else if (length && form < 7) {
        emit(g, "o%u: [%u]R%u = .", n, length, r);
        record_items(g, limit, r, length);
    } else if (length) {
        emit(g, "o%u := R%u.", n, r);
        record_items(g, limit, r, length);
    } else if (form < 3) {
        emit(g, "o%u: R%u", n, r);
    } else if (form < 6) {
        emit(g, "o%u: R%u = ", n, r);
        record_value(g, limit, r);
    } else if (form < 7 && count_record_places(g, r, false, limit)) {
        emit(g, "o%u := ", n);
        record_place(g, r, false, limit);
    } else if (k >= 0) {
        emit(g, "o%u := ", n);
        record_call(g, (u32)k);
    } else {
        emit(g, "o%u := R%u.", n, r);
        record_literal(g, limit, r);
    }
    add_record_var(g, 'o', n, r)->length = length;
}

static u8
held_record(Gen *g, const RecordVar *v)
{
    u32 n = 0;
    for (u8 r = 0; r < g->nrecords; r++) {
        n += r == v->record || has_record(g, v->record, r);
    }
    u32 pick = (u32)random_below(g->rng, (int)n);
    for (u8 r = 0; r < g->nrecords; r++) {
        if ((r == v->record || has_record(g, v->record, r)) && pick-- == 0) {
            return r;
        }
    }
    return v->record;
}

static RecordVar *
pick_writable_record(Gen *g)
{
    u32 n = 0;
    for (u32 i = 0; i < g->nrvars; i++) {
        n += g->rvars[i].assignable || g->rvars[i].pointer;
    }
    if (n == 0) {
        return NULL;
    }
    u32 k = (u32)random_below(g->rng, (int)n);
    for (u32 i = 0; i < g->nrvars; i++) {
        if ((g->rvars[i].assignable || g->rvars[i].pointer) && k-- == 0) {
            return &g->rvars[i];
        }
    }
    return NULL;
}

static void
record_loop(Gen *g, int depth)
{
    u32 n = 0;
    for (u32 i = 0; i < g->nrvars; i++) {
        n += g->rvars[i].length != 0;
    }
    if (n == 0 || g->nrvars == MAX_RECORD_VARS) {
        return;
    }
    u32 k = (u32)random_below(g->rng, (int)n);
    const RecordVar *a = NULL;
    for (u32 i = 0; i < g->nrvars; i++) {
        if (g->rvars[i].length && k-- == 0) {
            a = &g->rvars[i];
        }
    }

    line(g);
    loop_label(g);
    u32 x = g->names++;
    emit(g, "for x%u in %s {", x, a->name);
    u32 saved = g->nrvars;
    RecordVar *v = add_record_var(g, 'x', x, a->record);
    v->limit = a->limit;
    v->assignable = false;
    g->loops++;
    block(g, depth - 1, 1 + random_below(g->rng, 3));
    g->loops--;
    g->nrvars = saved;
    line(g);
    emit(g, "}");
}

static void
leaf_fill(Gen *g, const RecordVar *v, u8 t)
{
    u32 n = g->names++;
    emit(g, "for n%u in 0..", n);
    size_t start = g->len;
    record_leaf(g, v, t, true);
    int length = (int)(g->len - start);
    emit(g, ".len {");
    g->indent++;
    line(g);
    emit(g, "%.*s[n%u] = ", length, g->out + start, n);
    fill_value(g, t, n);
    g->indent--;
    line(g);
    emit(g, "}");
}

static bool
stores_slice(const Gen *g, const RecordVar *v, u8 t)
{
    return reads_leaf(g, v, t, true, true) &&
           count_sources(g, v->limit, false, t, 0, 0) > 0;
}

static RecordVar *
pick_stored_leaf(Gen *g, u8 *t)
{
    u32 n = 0;
    for (u32 i = 0; i < g->nrvars; i++) {
        for (u8 u = 0; u < NTYPES; u++) {
            n += stores_slice(g, &g->rvars[i], u);
        }
    }
    if (n == 0) {
        return NULL;
    }
    u32 k = (u32)random_below(g->rng, (int)n);
    for (u32 i = 0; i < g->nrvars; i++) {
        for (u8 u = 0; u < NTYPES; u++) {
            if (stores_slice(g, &g->rvars[i], u) && k-- == 0) {
                *t = u;
                return &g->rvars[i];
            }
        }
    }
    return NULL;
}

static bool
record_slice(Gen *g, int depth)
{
    u8 t;
    int form = random_below(g->rng, 4);
    RecordVar *o = form == 0 ? pick_stored_leaf(g, &t) : NULL;
    if (o) {
        line(g);
        record_leaf(g, o, t, true);
        emit(g, " = ");
        any_slice(g, o->limit, t);
        return true;
    }
    RecordVar *v = pick_slice_leaf(g, false, &t);
    if (!v) {
        return false;
    }
    bool writable = v->assignable || v->pointer;
    if (form == 1 && writable) {
        line(g);
        leaf_fill(g, v, t);
        return true;
    }
    if (form < 3 && depth > 0 && g->loops < g->max_loops && g->nvars < MAX_VARS) {
        u32 x = g->names++;
        line(g);
        loop_label(g);
        emit(g, "for x%u in ", x);
        record_leaf(g, v, t, true);
        emit(g, " {");
        u32 saved = g->nvars;
        declare_as(g, 'x', x, t, false);
        g->loops++;
        block(g, depth - 1, 1 + random_below(g->rng, 3));
        g->loops--;
        g->nvars = saved;
        line(g);
        emit(g, "}");
        return true;
    }
    if (g->nvars < MAX_VARS) {
        u32 n = g->names++;
        line(g);
        emit(g, "w%u := ", n);
        record_leaf(g, v, t, true);
        declare_as(g, 'w', n, t, true)->slice = true;
    }
    return true;
}

static void
record_statement(Gen *g, int depth)
{
    RecordVar *v = pick_writable_record(g);
    int pick = random_below(g->rng, 12);
    if (!v || pick < 3) {
        declare_record(g);
        return;
    }
    if (pick < 4 && depth > 0 && g->loops < g->max_loops) {
        record_loop(g, depth);
        return;
    }
    if (pick < 5 && g->nrvars < MAX_RECORD_VARS) {
        u8 r = held_record(g, v);
        u32 n = g->names++;
        line(g);
        emit(g, "q%u := &", n);
        record_place_in(g, v, r);
        RecordVar *q = add_record_var(g, 'q', n, r);
        q->limit = v->limit;
        q->pointer = true;
        q->assignable = false;
        return;
    }
    if (pick < 7) {
        u8 r = held_record(g, v);
        line(g);
        record_place_in(g, v, r);
        emit(g, " = ");
        record_value(g, v->limit, r);
        return;
    }
    if (pick < 11 && record_slice(g, depth)) {
        return;
    }

    u8 t = random_type(g, false);
    for (int k = 0; k < 4 && !has_leaf(g, v->record, t, false); k++) {
        t = random_type(g, false);
    }
    if (!has_leaf(g, v->record, t, false)) {
        return;
    }
    line(g);
    record_leaf(g, v, t, false);
    assignment(g, t);
}

static Table *
add_table(Gen *g, u32 n, u8 t, u16 length)
{
    assert(g->ntables < MAX_TABLES);
    Table *z = &g->tables[g->ntables++];
    snprintf(z->name, NAME_LEN, "z%u", n);
    z->type = t;
    z->inner = 0;
    z->limit = (u8)g->nvars;
    z->slice = false;
    z->length = length;
    return z;
}

static void
table_view(Gen *g, const Table *from)
{
    u32 n = g->names++;
    u16 length = from->length;
    int form = random_below(g->rng, 4);
    line(g);
    if (form == 0 && !from->slice) {
        emit(g, "z%u: [][]%s = %s", n, type_names[from->type], from->name);
    } else if (form < 2) {
        u16 k = (u16)random_below(g->rng, length);
        emit(g, "z%u := %s[%u..]", n, from->name, k);
        length -= k;
    } else if (form < 3) {
        u16 k = (u16)(1 + random_below(g->rng, length));
        emit(g, "z%u := %s[..%u]", n, from->name, k);
        length = k;
    } else {
        u16 k = (u16)random_below(g->rng, length);
        u16 m = (u16)(1 + random_below(g->rng, length - k));
        emit(g, "z%u := %s[%u..%u]", n, from->name, k, k + m);
        length = m;
    }
    Table *z = add_table(g, n, from->type, length);
    z->inner = from->inner;
    z->limit = from->limit;
    z->slice = true;
}

static void
declare_table(Gen *g)
{
    if (g->ntables == MAX_TABLES) {
        return;
    }
    Table *from = chance(g, 50) ? pick_table(g, NTYPES, 0, MAX_VARS) : NULL;
    if (from) {
        table_view(g, from);
        return;
    }

    Var *v = chance(g, 85) ? pick_view(g, ANY) : NULL;
    u8 t = v ? v->type : random_type(g, false);
    u16 length = (u16)(1 + random_below(g->rng, 4));
    u16 inner = (u16)random_below(g->rng, 3);
    while (inner && !count_sources(g, g->nvars, false, t, 0, inner)) {
        inner--;
    }
    u32 n = g->names++;
    int form = random_below(g->rng, 4);
    line(g);
    if (form == 0 || !count_sources(g, g->nvars, false, t, 0, inner)) {
        emit(g, "z%u: [%u][]%s", n, length, type_names[t]);
        inner = 0;
    } else {
        if (form == 1) {
            emit(g, "z%u: [%u][]%s = .[", n, length, type_names[t]);
        } else if (form == 2) {
            emit(g, "z%u := []%s.[", n, type_names[t]);
        } else {
            emit(g, "z%u: [%u][]%s = []%s.[", n, length, type_names[t], type_names[t]);
        }
        for (u16 k = 0; k < length; k++) {
            emit(g, k ? ", " : "");
            slice_value(g, g->nvars, false, t, 0, inner, true);
        }
        emit(g, "]");
    }
    add_table(g, n, t, length)->inner = (u8)inner;
}

static void
table_loop(Gen *g, const Table *z, int depth)
{
    u32 y = g->names++;
    u32 i = g->names++;
    bool row = chance(g, 40);
    bool indexed = chance(g, 30);
    line(g);
    loop_label(g);
    emit(g, "for %c%u", row ? 'x' : 'y', y);
    if (indexed) {
        emit(g, ", j%u", i);
    }
    emit(g, " in ");
    if (row) {
        table_row(g, z, 0);
    } else {
        emit(g, "%s", z->name);
    }
    emit(g, " {");

    u32 saved = g->nvars;
    u32 bounds = g->nbounds;
    Var *v = declare_as(g, row ? 'x' : 'y', y, z->type, false);
    v->slice = !row;
    v->length = row ? 0 : z->inner;
    if (indexed && !row) {
        g->bounds[g->nbounds++] = (Bound){ g->nvars, z->length };
    }
    if (indexed) {
        declare_as(g, 'j', i, T_INT, false);
    }
    g->loops++;
    block(g, depth - 1, 1 + random_below(g->rng, 3));
    g->loops--;
    g->nbounds = bounds;
    g->nvars = saved;
    line(g);
    emit(g, "}");
}

static void
table_statement(Gen *g, int depth)
{
    Table *z = pick_table(g, NTYPES, 0, MAX_VARS);
    int pick = random_below(g->rng, 12);
    if (!z || pick < 2) {
        declare_table(g);
        return;
    }
    if (pick < 4 && depth > 0 && g->loops < g->max_loops && g->nvars + 2 <= MAX_VARS) {
        table_loop(g, z, depth);
        return;
    }
    if (pick < 6 && g->nvars < MAX_VARS) {
        u32 n = g->names++;
        bool pointer = chance(g, 50);
        line(g);
        emit(g, pointer ? "q%u := &" : "w%u := ", n);
        table_row(g, z, 1);
        Var *w = declare_as(g, pointer ? 'q' : 'w', n, z->type, !pointer);
        w->pointer = pointer;
        w->slice = true;
        w->length = z->inner;
        w->limit = z->limit;
        return;
    }
    if (pick < 8 && z->inner) {
        line(g);
        table_row(g, z, 1);
        subscript(g, z->inner, 1);
        assignment(g, z->type);
        return;
    }
    Table *from = pick_table(g, z->type, z->inner, z->limit);
    if (pick < 9 && from && !z->slice && !from->slice && from->length == z->length) {
        line(g);
        emit(g, "%s = %s", z->name, from->name);
        return;
    }
    if (pick < 10 && from) {
        line(g);
        table_row(g, z, 1);
        emit(g, " = ");
        table_row(g, from, 1);
        return;
    }
    if (count_sources(g, z->limit, false, z->type, 0, z->inner)) {
        line(g);
        table_row(g, z, 1);
        emit(g, " = ");
        slice_value(g, z->limit, false, z->type, 0, z->inner, true);
    }
}

static void
defer_statement(Gen *g, int depth)
{
    u32 loops = g->loops_outside;
    u32 blocks = g->blocks_outside;
    bool in_defer = g->in_defer;
    g->loops_outside = g->loops;
    g->blocks_outside = g->nblock_labels;
    g->in_defer = true;

    line(g);
    u8 t = random_type(g, false);
    Var *v = chance(g, 50) ? pick_var(g, t, true) : NULL;
    if (v) {
        emit(g, "defer %s", v->name);
        assignment(g, v->type);
    } else {
        emit(g, "defer {");
        block(g, depth - 1, 1 + random_below(g->rng, 3));
        line(g);
        emit(g, "}");
    }

    g->loops_outside = loops;
    g->blocks_outside = blocks;
    g->in_defer = in_defer;
}

static void
statement(Gen *g, int depth)
{
    if (depth > 0 && chance(g, 5)) {
        defer_statement(g, depth);
        return;
    }
    if (g->nrecords && chance(g, 12)) {
        record_statement(g, depth);
        return;
    }
    if (chance(g, 12)) {
        pointer_statement(g);
        return;
    }
    if (chance(g, 15)) {
        array_statement(g, depth);
        return;
    }
    if (chance(g, 10)) {
        slice_statement(g, depth);
        return;
    }
    if (chance(g, 6)) {
        table_statement(g, depth);
        return;
    }
    int pick = random_below(g->rng, 22);
    if (pick < 5) {
        u8 t = random_type(g, false);
        line(g);
        u32 n = g->names++;
        emit(g, "v%u: %s = ", n, type_names[t]);
        typed_expr(g, t, 3);
        declare_as(g, 'v', n, t, true);
        return;
    }
    if (pick < 10) {
        u8 t = random_type(g, false);
        Var *v = pick_var(g, t, true);
        if (!v) {
            return;
        }
        line(g);
        emit(g, "%s", v->name);
        assignment(g, v->type);
        return;
    }
    if (pick < 13 && depth > 0) {
        line(g);
        emit(g, "if ");
        expr(g, T_BOOL, 3);
        emit(g, " {");
        block(g, depth - 1, 1 + random_below(g->rng, 3));
        line(g);
        emit(g, "}");
        if (chance(g, 50)) {
            emit(g, " else {");
            block(g, depth - 1, 1 + random_below(g->rng, 3));
            line(g);
            emit(g, "}");
        }
        return;
    }
    if (pick < 15 && depth > 0 && g->loops < g->max_loops) {
        line(g);
        Var *k = declare(g, 'k', T_INT, false);
        char name[NAME_LEN];
        memcpy(name, k->name, NAME_LEN);
        emit(g, "%s: int = 0", name);
        line(g);
        loop_label(g);
        emit(g, "while %s < %d", name, 1 + random_below(g->rng, 3));
        if (chance(g, 30)) {
            emit(g, " and ");
            expr(g, T_BOOL, 2);
        }
        emit(g, " {");
        g->indent++;
        line(g);
        emit(g, "%s += 1", name);
        g->indent--;
        g->loops++;
        block(g, depth - 1, 1 + random_below(g->rng, 3));
        g->loops--;
        line(g);
        emit(g, "}");
        return;
    }
    if (pick < 17 && depth > 0 && g->loops < g->max_loops) {
        line(g);
        loop_label(g);
        u32 saved = g->nvars;
        u32 bounds = g->nbounds;
        Var *a = chance(g, 30) ? pick_array(g, NTYPES, ANY, ANY, false) : NULL;
        if (a && a->length <= MAX_ITERATED) {
            u16 length = a->length;
            Var *i = declare(g, 'n', T_INT, false);
            emit(g, "for %s in 0..", i->name);
            array_base(g, a);
            emit(g, ".len {");
            g->bounds[g->nbounds++] = (Bound){ g->nvars - 1, length };
        } else if (chance(g, 50)) {
            Var *i = declare(g, 'n', T_INT, false);
            int end = random_below(g->rng, 4);
            emit(g, "for %s in 0..%d {", i->name, end);
            g->bounds[g->nbounds++] = (Bound){ g->nvars - 1, (u16)end };
        } else {
            u8 t = random_int_type(g);
            Var *from = pick_var(g, t, false);
            if (!from) {
                Var *i = declare(g, 'n', T_INT, false);
                int end = random_below(g->rng, 3);
                emit(g, "for %s in 1..=%d {", i->name, end);
                g->bounds[g->nbounds++] = (Bound){ g->nvars - 1, (u16)(end + 1) };
            } else {
                char start[NAME_LEN];
                memcpy(start, from->name, NAME_LEN);
                Var *i = declare(g, 'n', t, false);
                emit(
                    g,
                    "for %s in %s..%s(%s +%% %d) {",
                    i->name,
                    start,
                    chance(g, 50) ? "=" : "",
                    start,
                    random_below(g->rng, 3)
                );
            }
        }
        g->loops++;
        block(g, depth - 1, 1 + random_below(g->rng, 3));
        g->loops--;
        g->nvars = saved;
        g->nbounds = bounds;
        line(g);
        emit(g, "}");
        return;
    }
    if (pick < 18) {
        if (callable(g, NTYPES)) {
            line(g);
            emit(g, "_ = ");
            call_helper(g, NTYPES, 3);
        }
        return;
    }
    u32 loops = g->loops - g->loops_outside;
    u32 blocks = g->nblock_labels - g->blocks_outside;
    if (pick < 19 && (loops > 0 || blocks > 0)) {
        line(g);
        emit(g, "if ");
        expr(g, T_BOOL, 2);
        emit(g, " {");
        g->indent++;
        line(g);
        bool is_break = chance(g, 60);
        u32 labeled = 0;
        for (u32 k = g->loops_outside; k < g->loops; k++) {
            labeled += g->loop_labels[k] != 0;
        }
        if (blocks > 0 && (loops == 0 || chance(g, 25))) {
            emit(g, "break d%u", g->block_labels[g->blocks_outside + (u32)random_below(g->rng, (int)blocks)]);
        } else if (labeled > 0 && chance(g, 50)) {
            u32 pick_label = (u32)random_below(g->rng, (int)labeled);
            for (u32 k = g->loops_outside; k < g->loops; k++) {
                if (g->loop_labels[k] && pick_label-- == 0) {
                    emit(g, "%s l%u", is_break ? "break" : "continue", g->loop_labels[k]);
                    break;
                }
            }
        } else {
            emit(g, is_break ? "break" : "continue");
        }
        g->indent--;
        line(g);
        emit(g, "}");
        return;
    }
    if (pick < 20 && depth > 0 && g->nblock_labels < MAX_LABELS && chance(g, 40)) {
        line(g);
        u32 name = g->names++;
        emit(g, "d%u: {", name);
        g->block_labels[g->nblock_labels++] = name;
        block(g, depth - 1, 1 + random_below(g->rng, 3));
        g->nblock_labels--;
        line(g);
        emit(g, "}");
        return;
    }
    if (pick < 20 && !g->in_defer) {
        line(g);
        emit(g, "if ");
        expr(g, T_BOOL, 2);
        emit(g, " {");
        g->indent++;
        line(g);
        return_value(g, 3);
        g->indent--;
        line(g);
        emit(g, "}");
        return;
    }
    if (depth > 0) {
        switch_statement(g, depth);
    }
}

static void
block(Gen *g, int depth, int count)
{
    u32 saved = g->nvars;
    u32 records = g->nrvars;
    u32 tables = g->ntables;
    g->indent++;
    for (int k = 0; k < count; k++) {
        statement(g, depth);
    }
    g->indent--;
    g->nvars = saved;
    g->nrvars = records;
    g->ntables = tables;
}

static void
digest(Gen *g, u32 sum, const Var *a)
{
    u32 row = g->names++;
    u32 x = g->names++;
    line(g);
    if (a->inner) {
        emit(g, "for y%u in %s { for x%u in y%u { ", row, a->name, x, row);
    } else {
        emit(g, "for x%u in %s { ", x, a->name);
    }

    u8 t = a->type;
    if (t == T_BOOL) {
        emit(g, "if x%u { s%u +%%= 1 }; s%u *%%= 31", x, sum, sum);
    } else if (is_float(t)) {
        emit(g, "if x%u < 0.0 { s%u +%%= 1 }; if x%u > 0.0 { s%u +%%= 2 }; s%u *%%= 31", x, sum, x, sum, sum);
    } else if (t == T_U64) {
        emit(g, "s%u = s%u *%% 31 +%% int((x%u & %llu))", sum, sum, x, (unsigned long long)max_of(T_INT));
    } else {
        emit(g, "s%u = s%u *%% 31 +%% int(x%u)", sum, sum, x);
    }
    emit(g, a->inner ? " } }" : " }");
}

static void
digest_leaf(Gen *g, u32 sum, const char *x, u8 t)
{
    line(g);
    if (t == T_BOOL) {
        emit(g, "if %s { s%u +%%= 1 }; s%u *%%= 31", x, sum, sum);
    } else if (is_float(t)) {
        emit(g, "if %s < 0.0 { s%u +%%= 1 }; if %s > 0.0 { s%u +%%= 2 }; s%u *%%= 31", x, sum, x, sum, sum);
    } else if (t == T_U64) {
        emit(g, "s%u = s%u *%% 31 +%% int((%s & %llu))", sum, sum, x, (unsigned long long)max_of(T_INT));
    } else {
        emit(g, "s%u = s%u *%% 31 +%% int(%s)", sum, sum, x);
    }
}

static void
digest_record(Gen *g, u32 sum, char *path, int len, u8 r)
{
    const Record *rec = &g->records[r];
    for (u8 k = 0; k < rec->count; k++) {
        const Field *f = &rec->fields[k];
        int n = len + snprintf(path + len, PATH_LEN - (size_t)len, ".f%u", k);
        assert(n < PATH_LEN);
        if (!f->length && f->record != NO_RECORD) {
            digest_record(g, sum, path, n, f->record);
        } else if (!f->length && !f->slice) {
            digest_leaf(g, sum, path, f->type);
        } else {
            if (f->slice) {
                line(g);
                emit(g, "s%u = s%u *%% 31 +%% %s.len", sum, sum, path);
            }
            u32 x = g->names++;
            line(g);
            emit(g, "for x%u in %s {", x, path);
            g->indent++;
            char element[PATH_LEN];
            int m = snprintf(element, sizeof element, "x%u", x);
            if (f->record != NO_RECORD) {
                digest_record(g, sum, element, m, f->record);
            } else {
                digest_leaf(g, sum, element, f->type);
            }
            g->indent--;
            line(g);
            emit(g, "}");
        }
        path[len] = 0;
    }
}

static void
digest_records(Gen *g, u32 sum)
{
    for (u32 i = 0; i < g->nrvars; i++) {
        const RecordVar *v = &g->rvars[i];
        if (v->pointer) {
            continue;
        }
        char path[PATH_LEN];
        if (!v->length) {
            int n = snprintf(path, sizeof path, "%s", v->name);
            digest_record(g, sum, path, n, v->record);
            continue;
        }
        u32 x = g->names++;
        line(g);
        emit(g, "for x%u in %s {", x, v->name);
        g->indent++;
        int n = snprintf(path, sizeof path, "x%u", x);
        digest_record(g, sum, path, n, v->record);
        g->indent--;
        line(g);
        emit(g, "}");
    }
}

static void
digest_table(Gen *g, u32 sum, const Table *z)
{
    u32 y = g->names++;
    u32 x = g->names++;
    line(g);
    emit(g, "for y%u in %s {", y, z->name);
    g->indent++;
    line(g);
    emit(g, "s%u = s%u *%% 31 +%% y%u.len", sum, sum, y);
    line(g);
    emit(g, "for x%u in y%u {", x, y);
    g->indent++;
    char element[NAME_LEN];
    snprintf(element, sizeof element, "x%u", x);
    digest_leaf(g, sum, element, z->type);
    g->indent--;
    line(g);
    emit(g, "}");
    g->indent--;
    line(g);
    emit(g, "}");
}

static bool
digest_arrays(Gen *g)
{
    u32 n = 0;
    for (u32 i = 0; i < g->nvars; i++) {
        n += g->vars[i].length && !g->vars[i].pointer && !g->vars[i].slice;
    }
    for (u32 i = 0; i < g->nrvars; i++) {
        n += !g->rvars[i].pointer;
    }
    for (u32 i = 0; i < g->ntables; i++) {
        n += !g->tables[i].slice;
    }
    if (n == 0 || !chance(g, 50)) {
        return false;
    }

    u32 sum = g->names++;
    line(g);
    emit(g, "s%u: int = 0", sum);
    for (u32 i = 0; i < g->nvars; i++) {
        if (g->vars[i].length && !g->vars[i].pointer && !g->vars[i].slice) {
            digest(g, sum, &g->vars[i]);
        }
    }
    digest_records(g, sum);
    for (u32 i = 0; i < g->ntables; i++) {
        if (!g->tables[i].slice) {
            digest_table(g, sum, &g->tables[i]);
        }
    }
    u32 result = g->names++;
    line(g);
    emit(g, "t%u: int = ", result);
    last_value(g, &g->result, 4);
    line(g);
    emit(g, "return s%u +%% t%u", sum, result);
    return true;
}

static void
function(Gen *g, int count, bool entry)
{
    g->indent = 1;
    for (int k = 0; k < count; k++) {
        statement(g, 2);
    }
    if (!entry || !digest_arrays(g)) {
        line(g);
        emit(g, "return ");
        if (g->ret_record != NO_RECORD) {
            record_result(g);
        } else if (g->ret_slice) {
            slice_value(g, g->nvars, true, g->ret, 0, g->ret_length, g->result.typed);
        } else {
            last_value(g, &g->result, 4);
        }
    }
    emit(g, "\n}\n");
    g->indent = 0;
}

size_t
random_typed_program(u64 *rng, char *buf)
{
    Gen g = { .rng = rng, .out = buf };
    enum_declarations(&g);
    record_declarations(&g);
    for (u32 k = 0; k < MAX_HELPERS; k++) {
        Helper *h = &g.helpers[k];
        h->level = (u8)(k / 2);
        h->ret = random_type(&g, false);
        h->ret_slice = chance(&g, 20);
        h->ret_record = NO_RECORD;
        u8 returned = g.nrecords ? (u8)random_below(rng, (int)g.nrecords) : 0;
        if (g.nrecords && !h->ret_slice && !holds_slices(&g, returned) &&
            chance(&g, 30)) {
            h->ret_record = returned;
        }
        h->ret_length = (u16)random_below(rng, 5);
        h->nparams = (u8)(1 + random_below(rng, MAX_PARAMS));
        if (h->ret_slice && h->nparams < 2) {
            h->nparams = 2;
        }
        g.nvars = 0;
        g.nrvars = 0;
        g.ntables = 0;
        g.names = 0;
        g.level = h->level;
        g.ret = h->ret;
        g.ret_slice = h->ret_slice;
        g.ret_record = h->ret_record;
        g.ret_length = h->ret_length;
        g.result = (Label){ .type = h->ret, .typed = chance(&g, 50) };
        g.max_loops = 1;
        emit(&g, "fn h%u(", k);
        u32 floats = 0;
        u8 pointed = NO_RECORD;
        for (u32 p = 0; p < h->nparams; p++) {
            bool source = h->ret_slice && p == 1;
            h->records[p] = NO_RECORD;
            bool alias = pointed != NO_RECORD && chance(&g, 70);
            u8 r = alias      ? pointed
                 : g.nrecords ? (u8)random_below(rng, (int)g.nrecords)
                              : 0;
            const Record *pr = &g.records[r];
            bool value = floats + pr->vregs <= 8 && (alias || chance(&g, 50));
            if (!source && p > 0 && g.nrecords && chance(&g, alias ? 80 : 20) &&
                (value || !holds_slices(&g, r))) {
                pointed = value ? pointed : r;
                h->records[p] = r;
                h->params[p] = T_INT;
                h->pointers[p] = !value;
                h->slices[p] = false;
                h->lengths[p] = 0;
                floats += value ? pr->vregs : 0;
                u32 n = g.names++;
                RecordVar *v = add_record_var(&g, 'p', n, h->records[p]);
                v->pointer = !value;
                v->assignable = false;
                v->limit = 0;
                emit(&g, ", %s: %sR%u", v->name, value ? "" : "*", h->records[p]);
                continue;
            }
            h->params[p] = source ? h->ret : random_type(&g, p == 0);
            h->pointers[p] = !source && p > 0 && chance(&g, 30);
            h->slices[p] = source || (p > 0 && !h->pointers[p] && chance(&g, 25));
            h->lengths[p] = h->pointers[p] && chance(&g, 40)
                              ? (u16)(1 + random_below(rng, 6))
                          : source ? (u16)(h->ret_length + random_below(rng, 3))
                          : h->slices[p] ? (u16)random_below(rng, 7)
                                         : 0;
            floats += is_float(h->params[p]) && !h->pointers[p] && !h->slices[p];
            Var *v = declare(&g, 'p', h->params[p], false);
            v->pointer = h->pointers[p];
            v->slice = h->slices[p];
            v->escapes = h->pointers[p] || h->slices[p];
            v->length = h->lengths[p];
            emit(&g, "%s%s: %s", p ? ", " : "", v->name, h->pointers[p] ? "*" : "");
            if (h->slices[p]) {
                emit(&g, "[]");
            } else if (h->lengths[p]) {
                emit(&g, "[%u]", h->lengths[p]);
            }
            emit(&g, "%s", type_names[h->params[p]]);
        }
        if (!g.result.typed) {
            emit(&g, ") {");
        } else if (h->ret_record != NO_RECORD) {
            emit(&g, ") -> R%u {", h->ret_record);
        } else {
            emit(&g, h->ret_slice ? ") -> []%s {" : ") -> %s {", type_names[h->ret]);
        }
        g.nhelpers = k;
        function(&g, 2 + random_below(rng, 4), false);
    }
    g.nhelpers = MAX_HELPERS;

    for (u32 k = 0; k < PROGRAM_ENTRIES; k++) {
        g.nvars = 0;
        g.nrvars = 0;
        g.ntables = 0;
        g.names = 0;
        g.level = MAX_HELPERS;
        g.ret = T_INT;
        g.ret_slice = false;
        g.ret_record = NO_RECORD;
        g.result = (Label){ .type = T_INT, .typed = true };
        g.max_loops = 2;
        emit(&g, "fn e%u(a: int, b: int, c: int, d: f64) -> int {", k);
        declare(&g, 'a', T_INT, false);
        memcpy(g.vars[g.nvars - 1].name, "a", 2);
        declare(&g, 'b', T_INT, false);
        memcpy(g.vars[g.nvars - 1].name, "b", 2);
        declare(&g, 'c', T_INT, false);
        memcpy(g.vars[g.nvars - 1].name, "c", 2);
        declare(&g, 'd', T_F64, false);
        memcpy(g.vars[g.nvars - 1].name, "d", 2);
        g.indent = 1;
        int floats = chance(&g, 20) ? 9 + random_below(rng, 4) : 0;
        for (int n = 0; n < floats; n++) {
            u8 t = chance(&g, 50) ? T_F64 : T_F32;
            line(&g);
            u32 name = g.names++;
            emit(&g, "v%u: %s = ", name, type_names[t]);
            expr(&g, t, 1);
            declare_as(&g, 'v', name, t, true);
        }
        for (int n = 0; n < 3; n++) {
            u8 t = random_type(&g, false);
            line(&g);
            u32 name = g.names++;
            emit(&g, "v%u: %s = ", name, type_names[t]);
            nonliteral(&g, t, 1);
            declare_as(&g, 'v', name, t, true);
        }
        if (chance(&g, 50)) {
            declare_array(&g);
        }
        if (chance(&g, 40)) {
            declare_slice(&g);
        }
        if (chance(&g, 50)) {
            declare_record(&g);
        }
        function(&g, 3 + random_below(rng, 6), true);
    }
    return g.len;
}

int64_t
random_argument(u64 *rng)
{
    static const int64_t edges[] = {
        0,         1,          -1,        127,       128,     -128,   -129,
        255,       256,        32767,     -32768,    65535,   65536,  INT32_MAX,
        INT32_MIN, UINT32_MAX, INT64_MAX, INT64_MIN, 1000003, -999983
    };
    int pick = random_below(rng, 10);
    if (pick < 6) {
        return random_below(rng, 41) - 20;
    }
    if (pick < 8) {
        return random_below(rng, 2001) - 1000;
    }
    return edges[random_below(rng, sizeof edges / sizeof edges[0])];
}

double
random_float_argument(u64 *rng)
{
    static const double edges[] = { 0.0,   -0.0, 0.5,   -2.75, 1e18,
                                    -1e19, 3e9,  1e300, NAN,   INFINITY };
    if (random_below(rng, 4) == 0) {
        return edges[random_below(rng, sizeof edges / sizeof edges[0])];
    }
    return (random_below(rng, 4001) - 2000) / 8.0;
}
