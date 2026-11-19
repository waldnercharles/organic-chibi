#include "program.h"

#include "code.h"

#include <assert.h>
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
    ANY = 0xFFFF,
    NO_ENUM = 0xFFFFFFFFu,
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
    u8 inner;
    u16 length;
} Var;

typedef struct
{
    u32 var;
    u16 length;
} Bound;

typedef struct
{
    u8 ret;
    u8 nparams;
    u8 params[MAX_PARAMS];
    bool pointers[MAX_PARAMS];
    u16 lengths[MAX_PARAMS];
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
    Label result;
    u32 loops;
    u32 max_loops;
    u32 indent;
    u32 loop_labels[MAX_LABELS];
    u32 block_labels[MAX_LABELS];
    u32 nblock_labels;
    Label values[MAX_LABELS];
    u32 nvalues;
    Enum enums[MAX_ENUMS];
    u32 nenums;
    u8 switch_type;
    u32 nspans;
    Bound bounds[MAX_LABELS];
    u32 nbounds;
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
           !v->length && (!assignable || v->assignable);
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
        n += g->vars[i].type != T_BOOL && !g->vars[i].pointer && !g->vars[i].length;
    }
    assert(n > 0);
    u32 k = (u32)random_below(g->rng, (int)n);
    for (u32 i = 0; i < g->nvars; i++) {
        if (g->vars[i].type != T_BOOL && !g->vars[i].pointer &&
            !g->vars[i].length && k-- == 0) {
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
    v->inner = 0;
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
    return v->length && (t == NTYPES || v->type == t) &&
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
conversion(Gen *g, u8 t, int depth)
{
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
can_call(const Gen *g, const Helper *h, u8 t)
{
    if (h->level >= g->level || (t != NTYPES && h->ret != t)) {
        return false;
    }
    for (u32 p = 0; p < h->nparams; p++) {
        u8 u = h->params[p];
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
        n += can_call(g, &g->helpers[k], t);
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

static bool
call_helper(Gen *g, u8 t, int depth)
{
    u32 n = callable(g, t);
    if (n == 0) {
        return false;
    }
    u32 pick = (u32)random_below(g->rng, (int)n);
    for (u32 k = 0; k < g->nhelpers; k++) {
        const Helper *h = &g->helpers[k];
        if (can_call(g, h, t) && pick-- == 0) {
            emit(g, "h%u(", k);
            for (u32 p = 0; p < h->nparams; p++) {
                if (p) {
                    emit(g, ", ");
                }
                if (h->pointers[p]) {
                    pointer_argument(g, h->params[p], h->lengths[p]);
                } else {
                    typed_expr(g, h->params[p], depth - 1);
                }
            }
            emit(g, ")");
            return true;
        }
    }
    return false;
}

static void
nonliteral(Gen *g, u8 t, int depth)
{
    if (depth > 0 && chance(g, 40)) {
        int form = random_below(g->rng, 4);
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
        emit(g, "%s.*", q->name);
        return;
    }
    Var *a = chance(g, 20) ? pick_array(g, t, ANY, ANY, false) : NULL;
    if (a) {
        element(g, a, 0);
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
        if (chance(g, 25)) {
            emit(g, "return ");
            break_value(g, &g->result, 2);
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
    emit(g, "%s.*", q->name);
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
literal_items(Gen *g, u8 t, u16 length, u16 inner)
{
    emit(g, "[");
    for (u16 k = 0; k < length; k++) {
        if (k) {
            emit(g, ", ");
        }
        if (!inner) {
            typed_expr(g, t, chance(g, 20) ? 2 : 1);
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
array_loop(Gen *g, int depth)
{
    Var *a = pick_array(g, NTYPES, ANY, ANY, false);
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
    if (inner && chance(g, 40)) {
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
    r->length = length;
    r->inner = (u8)inner;
}

static void
array_fill(Gen *g, const Var *a)
{
    u8 t = a->type;
    u32 n = g->names++;
    emit(g, "for n%u in 0..", n);
    array_base(g, a);
    emit(g, ".len {");
    g->indent++;
    line(g);
    array_base(g, a);
    emit(g, "[n%u] = ", n);
    if (t == T_BOOL) {
        emit(g, "(((n%u ^ ", n);
        nonliteral(g, T_INT, 0);
        emit(g, ") & 1) == 0)");
    } else {
        emit(g, "%s(((n%u ^ ", type_names[t], n);
        nonliteral(g, T_INT, 0);
        u64 mask = is_float(t)               ? 65535
                 : max_of(t) < max_of(T_INT) ? max_of(t)
                                             : max_of(T_INT);
        emit(g, ") & %llu))", (unsigned long long)mask);
    }
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
        array_loop(g, depth);
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

static void
statement(Gen *g, int depth)
{
    if (chance(g, 12)) {
        pointer_statement(g);
        return;
    }
    if (chance(g, 15)) {
        array_statement(g, depth);
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
    if (pick < 19 && (g->loops > 0 || g->nblock_labels > 0)) {
        line(g);
        emit(g, "if ");
        expr(g, T_BOOL, 2);
        emit(g, " {");
        g->indent++;
        line(g);
        bool is_break = chance(g, 60);
        u32 labeled = 0;
        for (u32 k = 0; k < g->loops; k++) {
            labeled += g->loop_labels[k] != 0;
        }
        if (g->nblock_labels > 0 && (g->loops == 0 || chance(g, 25))) {
            emit(g, "break d%u", g->block_labels[random_below(g->rng, (int)g->nblock_labels)]);
        } else if (labeled > 0 && chance(g, 50)) {
            u32 pick_label = (u32)random_below(g->rng, (int)labeled);
            for (u32 k = 0; k < g->loops; k++) {
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
    if (pick < 20) {
        line(g);
        emit(g, "if ");
        expr(g, T_BOOL, 2);
        emit(g, " {");
        g->indent++;
        line(g);
        emit(g, "return ");
        break_value(g, &g->result, 3);
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
    g->indent++;
    for (int k = 0; k < count; k++) {
        statement(g, depth);
    }
    g->indent--;
    g->nvars = saved;
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

static bool
digest_arrays(Gen *g)
{
    u32 n = 0;
    for (u32 i = 0; i < g->nvars; i++) {
        n += g->vars[i].length && !g->vars[i].pointer;
    }
    if (n == 0 || !chance(g, 50)) {
        return false;
    }

    u32 sum = g->names++;
    line(g);
    emit(g, "s%u: int = 0", sum);
    for (u32 i = 0; i < g->nvars; i++) {
        if (g->vars[i].length && !g->vars[i].pointer) {
            digest(g, sum, &g->vars[i]);
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
        last_value(g, &g->result, 4);
    }
    emit(g, "\n}\n");
    g->indent = 0;
}

size_t
random_typed_program(u64 *rng, char *buf)
{
    Gen g = { .rng = rng, .out = buf };
    enum_declarations(&g);
    for (u32 k = 0; k < MAX_HELPERS; k++) {
        Helper *h = &g.helpers[k];
        h->level = (u8)(k / 2);
        h->ret = random_type(&g, false);
        h->nparams = (u8)(1 + random_below(rng, MAX_PARAMS));
        g.nvars = 0;
        g.names = 0;
        g.level = h->level;
        g.ret = h->ret;
        g.result = (Label){ .type = h->ret, .typed = chance(&g, 50) };
        g.max_loops = 1;
        emit(&g, "fn h%u(", k);
        for (u32 p = 0; p < h->nparams; p++) {
            h->params[p] = random_type(&g, p == 0);
            h->pointers[p] = p > 0 && chance(&g, 30);
            h->lengths[p] = h->pointers[p] && chance(&g, 40)
                              ? (u16)(1 + random_below(rng, 6))
                              : 0;
            Var *v = declare(&g, 'p', h->params[p], false);
            v->pointer = h->pointers[p];
            v->length = h->lengths[p];
            emit(&g, "%s%s: %s", p ? ", " : "", v->name, h->pointers[p] ? "*" : "");
            if (h->lengths[p]) {
                emit(&g, "[%u]", h->lengths[p]);
            }
            emit(&g, "%s", type_names[h->params[p]]);
        }
        emit(&g, g.result.typed ? ") -> %s {" : ") {", type_names[h->ret]);
        g.nhelpers = k;
        function(&g, 2 + random_below(rng, 4), false);
    }
    g.nhelpers = MAX_HELPERS;

    for (u32 k = 0; k < PROGRAM_ENTRIES; k++) {
        g.nvars = 0;
        g.names = 0;
        g.level = MAX_HELPERS;
        g.ret = T_INT;
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
        function(&g, 3 + random_below(rng, 6), true);
    }
    return g.len;
}
