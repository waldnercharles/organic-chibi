#include "program.h"

#include "code.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
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
};

typedef struct
{
    char name[NAME_LEN];
    u8 type;
    bool assignable;
} Var;

typedef struct
{
    u8 ret;
    u8 nparams;
    u8 params[MAX_PARAMS];
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
    u32 loops;
    u32 max_loops;
    u32 indent;
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

static Var *
pick_var(Gen *g, u8 t, bool assignable)
{
    u32 n = 0;
    for (u32 i = 0; i < g->nvars; i++) {
        n += g->vars[i].type == t && (!assignable || g->vars[i].assignable);
    }
    if (n == 0) {
        return NULL;
    }
    u32 k = (u32)random_below(g->rng, (int)n);
    for (u32 i = 0; i < g->nvars; i++) {
        if (g->vars[i].type == t && (!assignable || g->vars[i].assignable) && k-- == 0) {
            return &g->vars[i];
        }
    }
    return NULL;
}

static Var *
pick_numeric_var(Gen *g)
{
    u32 n = 0;
    for (u32 i = 0; i < g->nvars; i++) {
        n += g->vars[i].type != T_BOOL;
    }
    assert(n > 0);
    u32 k = (u32)random_below(g->rng, (int)n);
    for (u32 i = 0; i < g->nvars; i++) {
        if (g->vars[i].type != T_BOOL && k-- == 0) {
            return &g->vars[i];
        }
    }
    return NULL;
}

static Var *
declare(Gen *g, char prefix, u8 t, bool assignable)
{
    assert(g->nvars < MAX_VARS);
    Var *v = &g->vars[g->nvars++];
    snprintf(v->name, NAME_LEN, "%c%u", prefix, g->names++);
    v->type = t;
    v->assignable = assignable;
    return v;
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

static u32
callable(const Gen *g, u8 t)
{
    u32 n = 0;
    for (u32 k = 0; k < g->nhelpers; k++) {
        n += g->helpers[k].level < g->level &&
             (t == NTYPES || g->helpers[k].ret == t);
    }
    return n;
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
        if (h->level < g->level && (t == NTYPES || h->ret == t) && pick-- == 0) {
            emit(g, "h%u(", k);
            for (u32 p = 0; p < h->nparams; p++) {
                if (p) {
                    emit(g, ", ");
                }
                expr(g, h->params[p], depth - 1);
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
expr(Gen *g, u8 t, int depth)
{
    if (depth <= 0 || chance(g, 25)) {
        if (chance(g, 30)) {
            literal(g, t, false);
            return true;
        }
        nonliteral(g, t, 0);
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

static void block(Gen *g, int depth, int count);

static void
statement(Gen *g, int depth)
{
    int pick = random_below(g->rng, 20);
    if (pick < 5) {
        u8 t = random_type(g, false);
        line(g);
        emit(g, "v%u: %s = ", g->names, type_names[t]);
        expr(g, t, 3);
        declare(g, 'v', t, true);
        return;
    }
    if (pick < 10) {
        u8 t = random_type(g, false);
        Var *v = pick_var(g, t, true);
        if (!v) {
            return;
        }
        line(g);
        if (t == T_BOOL || chance(g, 40)) {
            emit(g, "%s = ", v->name);
            expr(g, t, 3);
            return;
        }
        if (is_float(t)) {
            static const char *const ops[] = { "+=", "-=", "*=", "/=" };
            emit(g, "%s %s ", v->name, ops[random_below(g->rng, 4)]);
            expr(g, t, 2);
            return;
        }
        static const char *const ops[] = { "+=",  "-=",  "*=", "+%=", "-%=",
                                           "*%=", "/=",  "%=", "&=",  "|=",
                                           "^=",  "<<=", ">>=" };
        int op = random_below(g->rng, 13);
        emit(g, "%s %s ", v->name, ops[op]);
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
        u32 saved = g->nvars;
        if (chance(g, 50)) {
            Var *i = declare(g, 'n', T_INT, false);
            emit(g, "for %s in 0..%d {", i->name, random_below(g->rng, 4));
        } else {
            u8 t = random_int_type(g);
            Var *from = pick_var(g, t, false);
            if (!from) {
                Var *i = declare(g, 'n', T_INT, false);
                emit(g, "for %s in 1..=%d {", i->name, random_below(g->rng, 3));
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
    if (pick < 19 && g->loops > 0) {
        line(g);
        emit(g, "if ");
        expr(g, T_BOOL, 2);
        emit(g, " {");
        g->indent++;
        line(g);
        emit(g, chance(g, 60) ? "break" : "continue");
        g->indent--;
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
        expr(g, g->ret, 3);
        g->indent--;
        line(g);
        emit(g, "}");
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
function(Gen *g, int count)
{
    g->indent = 1;
    for (int k = 0; k < count; k++) {
        statement(g, 2);
    }
    line(g);
    emit(g, "return ");
    expr(g, g->ret, 4);
    emit(g, "\n}\n");
    g->indent = 0;
}

size_t
random_typed_program(u64 *rng, char *buf)
{
    Gen g = { .rng = rng, .out = buf };
    for (u32 k = 0; k < MAX_HELPERS; k++) {
        Helper *h = &g.helpers[k];
        h->level = (u8)(k / 2);
        h->ret = random_type(&g, false);
        h->nparams = (u8)(1 + random_below(rng, MAX_PARAMS));
        g.nvars = 0;
        g.names = 0;
        g.level = h->level;
        g.ret = h->ret;
        g.max_loops = 1;
        emit(&g, "fn h%u(", k);
        for (u32 p = 0; p < h->nparams; p++) {
            h->params[p] = random_type(&g, p == 0);
            Var *v = declare(&g, 'p', h->params[p], false);
            emit(&g, "%s%s: %s", p ? ", " : "", v->name, type_names[h->params[p]]);
        }
        emit(&g, ") -> %s {", type_names[h->ret]);
        g.nhelpers = k;
        function(&g, 2 + random_below(rng, 4));
    }
    g.nhelpers = MAX_HELPERS;

    for (u32 k = 0; k < PROGRAM_ENTRIES; k++) {
        g.nvars = 0;
        g.names = 0;
        g.level = MAX_HELPERS;
        g.ret = T_INT;
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
            emit(&g, "v%u: %s = ", g.names, type_names[t]);
            expr(&g, t, 1);
            declare(&g, 'v', t, true);
        }
        for (int n = 0; n < 3; n++) {
            u8 t = random_type(&g, false);
            line(&g);
            emit(&g, "v%u: %s = ", g.names, type_names[t]);
            nonliteral(&g, t, 1);
            declare(&g, 'v', t, true);
        }
        function(&g, 3 + random_below(rng, 6));
    }
    return g.len;
}
