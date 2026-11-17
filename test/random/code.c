#include "code.h"

#include "../../src/lexer.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static u64
splitmix64(u64 *state)
{
    u64 z = (*state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

int
random_below(u64 *rng, int n)
{
    return (int)(splitmix64(rng) % (u64)n);
}

#define KEYWORD_TEXT(name, text) text,
static const char *const keywords[] = {
    CHIBI_TOKENS(CHIBI_SKIP, KEYWORD_TEXT, CHIBI_SKIP, CHIBI_SKIP, CHIBI_SKIP)
};

static const char *const builtin_types[] = { "int",  "u8",  "u32", "u64",
                                             "bool", "f64", "i64", "u16" };

static const u8 name_lengths[CODE_NAME_MAX_LEN + 1] = {
    0, 10, 6, 14, 10, 11, 11, 7, 7, 6, 5, 4, 3, 2, 1, 1, 1, 1, 1, 1, 1,
};

enum
{
    VOCABULARY_NAMES = 800,
    LOCAL_NAMES_MAX = 64,
    LOCAL_PERCENT = 45,
    VOCABULARY_TYPES = 200,
    ZIPF_SCALE = 1 << 20,
    STATEMENT_OPERANDS = 12,
    EXPR_DEPTH = 3,
    BLOCK_DEPTH = 6,
};

static int
pick_weighted(u64 *rng, const u8 *weights, int n)
{
    int sum = 0;
    for (int i = 0; i < n; i++) {
        sum += weights[i];
    }

    int r = random_below(rng, sum);
    int i = 0;
    while (r >= weights[i]) {
        r -= weights[i];
        i++;
    }

    return i;
}

static bool
chance(u64 *rng, int percent)
{
    return random_below(rng, 100) < percent;
}

static bool
is_keyword(const char *s, int len)
{
    for (size_t i = 0; i < sizeof keywords / sizeof keywords[0]; i++) {
        if (strlen(keywords[i]) == (size_t)len &&
            memcmp(keywords[i], s, (size_t)len) == 0) {
            return true;
        }
    }

    return false;
}

static bool
is_new_name(const Vocabulary *v, u32 from, u32 to, const char *s, int len)
{
    if (is_keyword(s, len)) {
        return false;
    }

    for (u32 i = from; i < to; i++) {
        const char *other = v->text + (size_t)i * CODE_NAME_MAX_LEN;
        if (v->lens[i] == len && memcmp(other, s, (size_t)len) == 0) {
            return false;
        }
    }

    return true;
}

static void
spell_name(u64 *rng, char *s, int len, char first)
{
    static const char rest[] = "abcdefghijklmnopqrstuvwxyz_";

    s[0] = (char)(first + random_below(rng, 26));
    for (int i = 1; i < len; i++) {
        s[i] = rest[random_below(rng, sizeof rest - 1)];
    }
}

static void
add_random_name(u64 *rng, Vocabulary *v, u32 start, u32 *n, int len, char first)
{
    char *s = v->text + (size_t)*n * CODE_NAME_MAX_LEN;
    spell_name(rng, s, len, first);

    if (is_new_name(v, start, *n, s, len)) {
        v->lens[(*n)++] = (u8)len;
    }
}

void
vocabulary_init(u64 *rng, Vocabulary *v, Arena *arena)
{
    u32 total = VOCABULARY_TYPES + VOCABULARY_NAMES + 26;
    v->text = arena_alloc(arena, (size_t)total * CODE_NAME_MAX_LEN, 1);
    v->lens = arena_alloc(arena, total, 1);
    v->weights = arena_alloc(arena, total * sizeof(u32), alignof(u32));

    u32 n = 0;
    v->pools[0] = 0;
    for (size_t i = 0; i < sizeof builtin_types / sizeof builtin_types[0]; i++) {
        size_t len = strlen(builtin_types[i]);
        memcpy(v->text + (size_t)n * CODE_NAME_MAX_LEN, builtin_types[i], len);
        v->lens[n++] = (u8)len;
    }
    while (n < VOCABULARY_TYPES) {
        add_random_name(rng, v, 0, &n, 3 + random_below(rng, 10), 'A');
    }

    int weight_sum = 0;
    for (int len = 1; len <= CODE_NAME_MAX_LEN; len++) {
        weight_sum += name_lengths[len];
    }

    for (int len = 1; len <= CODE_NAME_MAX_LEN; len++) {
        u64 space = 26;
        for (int i = 1; i < len && space < VOCABULARY_NAMES; i++) {
            space *= 27;
        }

        u64 want = (u64)VOCABULARY_NAMES * name_lengths[len] / (u64)weight_sum;
        u64 most = len == 1 ? space : space / 2;
        want = want < most ? want : most;

        u32 start = n;
        v->pools[len] = start;
        while (n - start < want) {
            add_random_name(rng, v, start, &n, len, 'a');
        }
    }
    v->pools[CODE_NAME_MAX_LEN + 1] = n;

    for (int pool = 0; pool <= CODE_NAME_MAX_LEN; pool++) {
        u32 sum = 0;
        for (u32 i = v->pools[pool]; i < v->pools[pool + 1]; i++) {
            sum += ZIPF_SCALE / (i - v->pools[pool] + 1);
            v->weights[i] = sum;
        }
    }
}

static u32
pick(u64 *rng, const Vocabulary *v, int pool)
{
    u32 lo = v->pools[pool];
    u32 hi = v->pools[pool + 1] - 1;
    u32 r = (u32)random_below(rng, (int)v->weights[hi]);
    while (lo < hi) {
        u32 mid = lo + (hi - lo) / 2;
        if (v->weights[mid] > r) {
            hi = mid;
        } else {
            lo = mid + 1;
        }
    }

    return lo;
}

typedef struct
{
    u64 *rng;
    const Vocabulary *v;
    char *buf;
    size_t len;
    size_t size;
    int indent;
    int loops;
    int operands;
    int nlocals;
    u8 local_lens[LOCAL_NAMES_MAX];
    char locals[LOCAL_NAMES_MAX][CODE_NAME_MAX_LEN];
} Writer;

static void
put(Writer *w, const char *s, size_t n)
{
    assert(w->len + n <= w->size + CODE_SLACK);
    memcpy(w->buf + w->len, s, n);
    w->len += n;
}

#define PUT(w, s) put(w, "" s, sizeof(s) - 1)

static void
put_text(Writer *w, const char *s)
{
    put(w, s, strlen(s));
}

static void
put_weighted(Writer *w, const char *const *items, const u8 *weights, int n)
{
    put_text(w, items[pick_weighted(w->rng, weights, n)]);
}

static void
put_word(Writer *w, int pool)
{
    u32 i = pick(w->rng, w->v, pool);
    put(w, w->v->text + (size_t)i * CODE_NAME_MAX_LEN, w->v->lens[i]);
}

static void
put_name(Writer *w)
{
    if (chance(w->rng, LOCAL_PERCENT)) {
        int i = random_below(w->rng, 1 + random_below(w->rng, w->nlocals));
        put(w, w->locals[i], w->local_lens[i]);
        return;
    }
    put_word(w, pick_weighted(w->rng, name_lengths, CODE_NAME_MAX_LEN + 1));
}

static void
make_locals(Writer *w, int n)
{
    while (w->nlocals < n) {
        char *s = w->locals[w->nlocals];
        int len = pick_weighted(w->rng, name_lengths, CODE_NAME_MAX_LEN + 1);
        spell_name(w->rng, s, len, 'a');
        if (!is_keyword(s, len)) {
            w->local_lens[w->nlocals++] = (u8)len;
        }
    }
}

static void
put_int(Writer *w)
{
    static const char *const common[] = { "0", "1",  "2",  "3",  "4",
                                          "8", "16", "64", "256" };
    static const u8 weights[] = { 41, 24, 8, 3, 3, 3, 3, 2, 1 };

    static const char hex[] = "0123456789ABCDEF";

    int r = random_below(w->rng, 100);
    if (r < 85) {
        put_weighted(w, common, weights, sizeof weights);
        return;
    }

    char digits[32];
    int n = 0;
    if (r < 88) {
        int count = 1 + random_below(w->rng, 8);
        bool grouped = count > 4 && chance(w->rng, 30);
        n = snprintf(digits, sizeof digits, "0x");
        for (int i = 0; i < count; i++) {
            if (grouped && i > 0 && (count - i) % 4 == 0) {
                digits[n++] = '_';
            }
            digits[n++] = hex[random_below(w->rng, 16)];
        }
    } else if (r < 89) {
        int count = 4 + random_below(w->rng, 5);
        bool grouped = count == 8 && chance(w->rng, 50);
        n = snprintf(digits, sizeof digits, "0b");
        for (int i = 0; i < count; i++) {
            if (grouped && i == 4) {
                digits[n++] = '_';
            }
            digits[n++] = (char)('0' + random_below(w->rng, 2));
        }
    } else if (r < 91) {
        int value = 1000 + random_below(w->rng, 999000);
        n = snprintf(digits, sizeof digits, "%d_%03d", value / 1000, value % 1000);
    } else {
        n = snprintf(digits, sizeof digits, "%d", random_below(w->rng, 10000));
    }
    put(w, digits, (size_t)n);
}

static void
put_float(Writer *w)
{
    char digits[16];
    int n = snprintf(digits, sizeof digits, "%d.%d", random_below(w->rng, 100), random_below(w->rng, 100));
    put(w, digits, (size_t)n);
}

static void put_type(Writer *w);

static void
put_fn_type(Writer *w)
{
    PUT(w, "fn(");
    int n = random_below(w->rng, 3);
    for (int i = 0; i < n; i++) {
        if (i > 0) {
            PUT(w, ", ");
        }
        if (chance(w->rng, 30)) {
            put_name(w);
            PUT(w, ": ");
        }
        put_type(w);
    }
    PUT(w, ")");

    if (chance(w->rng, 60)) {
        PUT(w, " -> ");
        put_type(w);
    }
}

static void
put_type(Writer *w)
{
    int r = random_below(w->rng, 100);
    if (r < 3) {
        PUT(w, "?");
        put_type(w);
    } else if (r < 4) {
        if (chance(w->rng, 50)) {
            put_name(w);
        }
        PUT(w, "!");
        put_name(w);
    } else if (r < 15) {
        PUT(w, "*");
        put_type(w);
    } else if (r < 20) {
        PUT(w, "[]");
        put_type(w);
    } else if (r < 24) {
        PUT(w, "[");
        put_int(w);
        if (chance(w->rng, 20)) {
            PUT(w, ", ");
            put_int(w);
        }
        PUT(w, "]");
        put_type(w);
    } else if (r < 25) {
        put_fn_type(w);
    } else if (r < 33) {
        put_name(w);
        PUT(w, ".");
        put_word(w, 0);
    } else {
        put_word(w, 0);
    }
}

static void
put_string(Writer *w)
{
    static const char *const escapes[] = { "\\n", "\\t", "\\\"", "\\\\" };
    static const u8 weights[] = { 10, 2, 2, 1 };

    PUT(w, "\"");
    int words = chance(w->rng, 8) ? 8 + random_below(w->rng, 16)
                                  : 1 + random_below(w->rng, 4);
    for (int i = 0; i < words; i++) {
        if (i > 0) {
            put_text(w, chance(w->rng, 10) ? "\\n" : " ");
        }
        put_name(w);
    }

    if (chance(w->rng, 16)) {
        put_weighted(w, escapes, weights, sizeof weights);
    }
    PUT(w, "\"");
}

static void
put_indent(Writer *w, int extra)
{
    for (int i = 0; i < w->indent + extra; i++) {
        PUT(w, "    ");
    }
}

static void
put_line_break(Writer *w, int extra)
{
    PUT(w, "\n");
    put_indent(w, extra);
}

static void
put_space_or_break(Writer *w, int extra)
{
    if (chance(w->rng, 10)) {
        put_line_break(w, extra);
    } else {
        PUT(w, " ");
    }
}

static void put_expr(Writer *w, int depth);

static void
put_fn_literal(Writer *w, int depth)
{
    PUT(w, "fn(");
    put_name(w);
    if (chance(w->rng, 40)) {
        PUT(w, ": ");
        put_type(w);
    }
    PUT(w, ") => ");
    put_expr(w, depth + 1);
}

static void
put_items(Writer *w, int depth, const char *open, const char *close, int named)
{
    static const u8 counts[] = { 2, 40, 25, 23, 6, 3, 1 };

    put_text(w, open);
    int n = pick_weighted(w->rng, counts, sizeof counts);
    bool split = depth == 0 && n >= 2 && chance(w->rng, 4);
    for (int i = 0; i < n; i++) {
        if (split) {
            put_line_break(w, 1);
        } else if (i > 0) {
            PUT(w, ", ");
        }

        if (chance(w->rng, named)) {
            PUT(w, ".");
            put_name(w);
            PUT(w, " = ");
        }

        if (depth < EXPR_DEPTH && chance(w->rng, 3)) {
            put_fn_literal(w, depth);
        } else {
            put_expr(w, depth + 1);
        }

        if (split) {
            PUT(w, ",");
        }
    }

    if (split) {
        put_line_break(w, 0);
    }
    put_text(w, close);
}

static void
put_literal(Writer *w, int depth)
{
    int r = random_below(w->rng, 100);
    if (r < 35) {
        put_word(w, 0);
        put_items(w, depth, ".{", "}", 40);
    } else if (r < 60) {
        put_items(w, depth, ".{", "}", 40);
    } else if (r < 85) {
        put_items(w, depth, ".[", "]", 0);
    } else {
        if (chance(w->rng, 20)) {
            PUT(w, "*");
        }
        put_word(w, 0);
        put_items(w, depth, ".[", "]", 0);
    }
}

static void
put_postfix(Writer *w, int depth)
{
    put_name(w);
    while (w->operands > 0) {
        int r = random_below(w->rng, 100);
        if (r < 1) {
            PUT(w, "!!");
        } else if (r < 18) {
            PUT(w, ".");
            put_name(w);
        } else if (r < 24 && depth < EXPR_DEPTH) {
            PUT(w, "[");
            int slice = random_below(w->rng, 100);
            if (slice < 4) {
                put_text(w, slice < 1 ? "..=" : "..");
                put_expr(w, depth + 1);
            } else {
                put_expr(w, depth + 1);
                if (slice < 12) {
                    put_text(w, slice < 6 ? "..=" : "..");
                    put_expr(w, depth + 1);
                } else if (slice < 16) {
                    PUT(w, "..");
                } else if (slice < 25) {
                    PUT(w, ", ");
                    put_expr(w, depth + 1);
                }
            }
            PUT(w, "]");
        } else if (r < 36 && depth < EXPR_DEPTH) {
            put_items(w, depth, "(", ")", 6);
        } else if (r < 37) {
            PUT(w, ".*");
        } else {
            return;
        }
        w->operands--;
    }
}

static void
put_operand(Writer *w, int depth)
{
    static const char unary[] = "-!!!&&";
    static const char *const atoms[] = { "true", "false", "nil" };

    w->operands--;
    if (w->operands <= 0) {
        put_name(w);
        return;
    }

    int r = random_below(w->rng, 100);
    if (r < 6) {
        if (r == 0 && chance(w->rng, 5)) {
            PUT(w, "-%");
        } else {
            put(w, &unary[r], 1);
        }
        put_postfix(w, depth);
    } else if (r < 9 && depth < EXPR_DEPTH) {
        if (chance(w->rng, 10)) {
            PUT(w, "bitcast(");
            put_type(w);
            PUT(w, ", ");
        } else {
            PUT(w, "(");
        }
        put_expr(w, depth + 1);
        PUT(w, ")");
    } else if (r < 21) {
        put_int(w);
    } else if (r < 22) {
        if (chance(w->rng, 40)) {
            put_float(w);
        } else {
            put_int(w);
        }
    } else if (r < 31) {
        put_string(w);
    } else if (r < 33) {
        put_text(w, atoms[random_below(w->rng, 3)]);
    } else if (r < 35 && depth < EXPR_DEPTH) {
        put_literal(w, depth);
    } else if (r < 37) {
        PUT(w, ".");
        put_name(w);
    } else {
        put_postfix(w, depth);
    }
}

static const char *const binary_ops[] = {
    " + ",  " - ", " * ",  " / ",  " % ",   " << ", " >> ",
    " & ",  " | ", " ^ ",  " ?? ", " and ", " or ", " == ",
    " != ", " < ", " <= ", " > ",  " >= ",  " is ",
};
static const u8 binary_weights[] = { 14, 6, 4, 3,  1, 1, 1, 3, 2, 1,
                                     1,  9, 7, 10, 5, 6, 2, 3, 2, 1 };
static const char *const wrap_ops[] = { " +% ", " -% ", " *% " };

enum
{
    FIRST_LOGICAL = 11,
    FIRST_COMPARE = 13,
    NBINARY = sizeof binary_weights,
};

static void
put_expr(Writer *w, int depth)
{
    static const u8 counts[2][5] = { { 0, 68, 22, 7, 3 }, { 0, 88, 10, 2, 0 } };

    int n = pick_weighted(w->rng, counts[depth > 0], sizeof counts[0]);
    bool compared = false;
    put_operand(w, depth);
    for (int i = 1; i < n; i++) {
        int op = pick_weighted(w->rng, binary_weights, NBINARY);
        if (op >= FIRST_COMPARE && compared) {
            op = 0;
        }
        compared = op >= FIRST_COMPARE || (compared && op < FIRST_LOGICAL);

        if (op < 3 && chance(w->rng, 3)) {
            put_text(w, wrap_ops[op]);
        } else {
            put_text(w, binary_ops[op]);
        }
        put_operand(w, depth);
    }
}

static void
put_comparison(Writer *w)
{
    put_operand(w, 1);
    if (chance(w->rng, 20)) {
        put_weighted(w, binary_ops, binary_weights, FIRST_LOGICAL);
        put_operand(w, 1);
    }

    put_weighted(w, binary_ops + FIRST_COMPARE, binary_weights + FIRST_COMPARE, NBINARY - FIRST_COMPARE);
    put_operand(w, 1);
}

static void
put_condition(Writer *w)
{
    w->operands = STATEMENT_OPERANDS;

    int r = random_below(w->rng, 100);
    if (r < 15) {
        put_postfix(w, 1);
    } else if (r < 23) {
        PUT(w, "!");
        put_postfix(w, 1);
    } else {
        put_comparison(w);
        if (chance(w->rng, 20)) {
            const char *op = chance(w->rng, 70) ? "and " : "or ";
            put_space_or_break(w, 1);
            put_text(w, op);
            put_comparison(w);
        }
    }
}

static void
put_target(Writer *w)
{
    put_name(w);

    int r = random_below(w->rng, 100);
    if (r < 30) {
        PUT(w, ".");
        put_name(w);
    } else if (r < 45) {
        PUT(w, "[");
        put_expr(w, 1);
        PUT(w, "]");
    } else if (r < 48) {
        PUT(w, ".*");
    }
}

static void put_block(Writer *w, int count);
static void put_switch(Writer *w, bool value);
static void put_call(Writer *w);

static void
put_decl(Writer *w)
{
    put_name(w);

    int r = random_below(w->rng, 100);
    if (r < 60) {
        PUT(w, " := ");
        int value = random_below(w->rng, 100);
        if (value < 2) {
            put_switch(w, true);
        } else if (value < 4) {
            put_name(w);
            PUT(w, ":");
            put_block(w, 1 + random_below(w->rng, 3));
        } else if (value < 7) {
            PUT(w, "try ");
            put_call(w);
        } else if (value < 10) {
            put_call(w);
            PUT(w, " catch ");
            if (chance(w->rng, 40)) {
                put_name(w);
                PUT(w, " => ");
            }
            put_expr(w, 0);
        } else {
            put_expr(w, 0);
        }
    } else if (r < 85) {
        PUT(w, ": ");
        put_type(w);
        PUT(w, " = ");
        if (chance(w->rng, 2)) {
            PUT(w, "undefined");
        } else {
            put_expr(w, 0);
        }
    } else {
        PUT(w, ": ");
        put_type(w);
    }
}

static void
put_call(Writer *w)
{
    put_name(w);
    if (chance(w->rng, 30)) {
        PUT(w, ".");
        put_name(w);
    }
    put_items(w, 0, "(", ")", 6);
}

static void
put_assign(Writer *w)
{
    put_target(w);
    PUT(w, " = ");
    put_expr(w, 0);
}

static void
put_update(Writer *w)
{
    static const char *const ops[] = { " += ", " -= ",  " *= ",  " |= ",
                                       " &= ", " <<= ", " >>= ", " ?\?= " };
    static const u8 weights[] = { 10, 4, 1, 3, 2, 1, 1, 1 };
    static const char *const wrap_assign_ops[] = { " +%= ", " -%= ", " *%= " };

    put_target(w);
    int op = pick_weighted(w->rng, weights, sizeof weights);
    if (op < 3 && chance(w->rng, 3)) {
        put_text(w, wrap_assign_ops[op]);
    } else {
        put_text(w, ops[op]);
    }
    if (chance(w->rng, 50)) {
        PUT(w, "1");
    } else {
        put_expr(w, 0);
    }
}

static void
put_return(Writer *w)
{
    PUT(w, "return");
    if (chance(w->rng, 88)) {
        PUT(w, " ");
        put_expr(w, 0);
    }
}

static void
put_comment(Writer *w)
{
    PUT(w, "//");
    int words = 3 + random_below(w->rng, 9);
    for (int i = 0; i < words; i++) {
        PUT(w, " ");
        put_name(w);
    }
}

static void
put_const(Writer *w)
{
    PUT(w, "const ");
    put_name(w);
    if (chance(w->rng, 70)) {
        PUT(w, " := ");
    } else {
        PUT(w, ": ");
        put_type(w);
        PUT(w, " = ");
    }
    put_expr(w, 0);
}

static void put_statement(Writer *w);

static void
put_block(Writer *w, int count)
{
    PUT(w, " {\n");

    w->indent++;
    for (int i = 0; i < count && w->len < w->size; i++) {
        if (i > 0 && chance(w->rng, w->indent == 1 ? 2 : 0)) {
            PUT(w, "\n");
        }
        put_statement(w);
    }
    w->indent--;

    put_indent(w, 0);
    PUT(w, "}");
}

static void
put_if(Writer *w)
{
    PUT(w, "if ");
    put_condition(w);
    put_block(w, 1 + random_below(w->rng, 4));

    if (!chance(w->rng, 12)) {
        return;
    }

    while (w->len < w->size && chance(w->rng, 30)) {
        put_space_or_break(w, 0);
        PUT(w, "else if ");
        put_condition(w);
        put_block(w, 1 + random_below(w->rng, 4));
    }
    put_space_or_break(w, 0);
    PUT(w, "else");
    put_block(w, 1 + random_below(w->rng, 4));
}

static void
put_label(Writer *w)
{
    if (chance(w->rng, 8)) {
        put_name(w);
        PUT(w, ": ");
    }
}

static void
put_while(Writer *w)
{
    put_label(w);
    PUT(w, "while ");
    put_condition(w);

    w->loops++;
    put_block(w, 1 + random_below(w->rng, 5));
    w->loops--;
}

static void
put_range(Writer *w)
{
    put_operand(w, 1);
    put_text(w, chance(w->rng, 80) ? ".." : "..=");
    put_operand(w, 1);
}

static void
put_for(Writer *w)
{
    put_label(w);
    PUT(w, "for ");
    if (chance(w->rng, 40)) {
        put_name(w);
        PUT(w, " in ");
        put_range(w);
    } else if (chance(w->rng, 25)) {
        if (chance(w->rng, 20)) {
            PUT(w, "_");
        } else {
            put_name(w);
        }
        PUT(w, ", ");
        put_name(w);
        PUT(w, " in ");
        put_postfix(w, 1);
    } else {
        put_name(w);
        PUT(w, " in ");
        put_postfix(w, 1);
    }

    w->loops++;
    put_block(w, 1 + random_below(w->rng, 5));
    w->loops--;
}

static void
put_arm_value(Writer *w)
{
    int r = random_below(w->rng, 100);
    if (r < 45) {
        PUT(w, ".");
        put_name(w);
    } else if (r < 80) {
        put_int(w);
    } else if (r < 90) {
        put_int(w);
        put_text(w, chance(w->rng, 80) ? ".." : "..=");
        put_int(w);
    } else {
        put_string(w);
    }
}

static void
put_arm_body(Writer *w, bool value)
{
    w->operands = STATEMENT_OPERANDS;
    int r = random_below(w->rng, 100);
    if (r < 30) {
        put_block(w, 1 + random_below(w->rng, 3));
        return;
    }

    PUT(w, " ");
    if (r < 60 || (value && r < 80)) {
        put_call(w);
    } else if (r < 80) {
        put_assign(w);
    } else if (r < 90 || w->loops == 0) {
        put_return(w);
    } else {
        PUT(w, "break");
    }
}

static void
put_switch(Writer *w, bool value)
{
    PUT(w, "switch ");
    if (chance(w->rng, 20)) {
        if (chance(w->rng, 25)) {
            PUT(w, "&");
        }
        put_name(w);
        PUT(w, " in ");
    }
    put_postfix(w, 1);
    PUT(w, " {");

    w->indent++;
    int n = 1 + random_below(w->rng, 5);
    for (int i = 0; i < n && w->len < w->size; i++) {
        put_line_break(w, 0);
        int values = chance(w->rng, 25) ? 2 + random_below(w->rng, 2) : 1;
        for (int k = 0; k < values; k++) {
            if (k > 0) {
                PUT(w, ", ");
            }
            put_arm_value(w);
        }
        PUT(w, " =>");
        put_arm_body(w, value);
        if (chance(w->rng, 20)) {
            PUT(w, ",");
        }
    }
    if (chance(w->rng, 60)) {
        put_line_break(w, 0);
        PUT(w, "_ =>");
        put_arm_body(w, value);
    }
    w->indent--;

    put_line_break(w, 0);
    PUT(w, "}");
}

enum
{
    STATEMENT_DECL,
    STATEMENT_CALL,
    STATEMENT_ASSIGN,
    STATEMENT_UPDATE,
    STATEMENT_RETURN,
    STATEMENT_JUMP,
    STATEMENT_DEFER,
    STATEMENT_COMMENT,
    STATEMENT_IF,
    STATEMENT_WHILE,
    STATEMENT_CONST,
    STATEMENT_FOR,
    STATEMENT_SWITCH,
    NSTATEMENTS,
};

static const u8 statement_weights[NSTATEMENTS] = { 19, 13, 10, 4, 8, 2, 1,
                                                   3,  8,  2,  2, 5, 2 };
static const u8 nesting_percent[BLOCK_DEPTH + 1] = { 0, 70, 45, 30, 20, 10, 0 };

static void
put_statement(Writer *w)
{
    u8 weights[NSTATEMENTS];
    memcpy(weights, statement_weights, sizeof weights);
    weights[STATEMENT_JUMP] = w->loops > 0 ? weights[STATEMENT_JUMP] : 0;
    weights[STATEMENT_IF] = (u8)(weights[STATEMENT_IF] *
                                 nesting_percent[w->indent] / 100);
    weights[STATEMENT_WHILE] = (u8)(weights[STATEMENT_WHILE] *
                                    nesting_percent[w->indent] / 100);
    weights[STATEMENT_FOR] = (u8)(weights[STATEMENT_FOR] *
                                  nesting_percent[w->indent] / 100);
    weights[STATEMENT_SWITCH] = (u8)(weights[STATEMENT_SWITCH] *
                                     nesting_percent[w->indent] / 100);

    w->operands = STATEMENT_OPERANDS;
    put_indent(w, 0);
    switch (pick_weighted(w->rng, weights, NSTATEMENTS)) {
        case STATEMENT_DECL: put_decl(w); break;
        case STATEMENT_CALL: put_call(w); break;
        case STATEMENT_ASSIGN:
            if (chance(w->rng, 3)) {
                PUT(w, "_ = ");
                put_call(w);
            } else {
                put_assign(w);
            }
            break;
        case STATEMENT_UPDATE: put_update(w); break;
        case STATEMENT_RETURN: put_return(w); break;
        case STATEMENT_JUMP:
            put_text(w, chance(w->rng, 60) ? "break" : "continue");
            if (chance(w->rng, 15)) {
                PUT(w, " ");
                put_name(w);
                if (chance(w->rng, 20)) {
                    PUT(w, " ");
                    put_expr(w, 0);
                }
            }
            break;
        case STATEMENT_DEFER:
            PUT(w, "defer ");
            if (chance(w->rng, 15)) {
                PUT(w, "catch ");
                if (chance(w->rng, 30)) {
                    put_name(w);
                    PUT(w, " => ");
                }
            }
            put_call(w);
            break;
        case STATEMENT_COMMENT: put_comment(w); break;
        case STATEMENT_IF: put_if(w); break;
        case STATEMENT_WHILE: put_while(w); break;
        case STATEMENT_CONST: put_const(w); break;
        case STATEMENT_FOR: put_for(w); break;
        case STATEMENT_SWITCH: put_switch(w, false); break;
    }

    if (chance(w->rng, 4)) {
        PUT(w, " ");
        put_comment(w);
    }
    PUT(w, "\n");
}

static void
put_union_members(Writer *w, int n, bool tagged)
{
    PUT(w, " {");
    for (int i = 0; i < n; i++) {
        put_text(w, i > 0 ? ", " : " ");
        put_name(w);
        if (!tagged || !chance(w->rng, 25)) {
            PUT(w, ": ");
            put_type(w);
        }
    }
    PUT(w, " }");
}

static void
put_members(Writer *w, int n, bool typed)
{
    bool one_line = n <= 3 && chance(w->rng, 30);
    PUT(w, " {");
    for (int i = 0; i < n; i++) {
        if (one_line) {
            put_text(w, i > 0 ? ", " : " ");
        } else {
            put_line_break(w, 1);
        }

        if (typed && chance(w->rng, 3)) {
            PUT(w, "union");
            put_union_members(w, 1 + random_below(w->rng, 3), false);
            continue;
        }

        put_name(w);
        if (typed) {
            PUT(w, ": ");
            put_type(w);
        }
        if (chance(w->rng, typed ? 10 : 15)) {
            PUT(w, " = ");
            if (typed && chance(w->rng, 5)) {
                PUT(w, "undefined");
            } else {
                put_int(w);
            }
        }
        if (!one_line && chance(w->rng, 10)) {
            PUT(w, ";");
        }
    }

    if (one_line) {
        PUT(w, " }");
    } else {
        PUT(w, "\n}");
    }
}

static void
put_struct(Writer *w)
{
    PUT(w, "struct ");
    put_word(w, 0);
    put_members(w, 1 + random_below(w->rng, 7), true);
}

static void
put_enum(Writer *w)
{
    PUT(w, "enum ");
    put_word(w, 0);
    if (chance(w->rng, 30)) {
        PUT(w, ": u8");
    }
    put_members(w, 2 + random_below(w->rng, 7), false);
}

static void
put_union(Writer *w)
{
    PUT(w, "union ");
    put_word(w, 0);
    bool tagged = chance(w->rng, 60);
    if (tagged) {
        PUT(w, ": u8");
    }
    put_union_members(w, 1 + random_below(w->rng, 5), tagged);
}

static void
put_globals(Writer *w)
{
    int n = 1 + random_below(w->rng, 4);
    for (int i = 0; i < n && w->len < w->size; i++) {
        w->operands = STATEMENT_OPERANDS;
        if (chance(w->rng, 30)) {
            PUT(w, "pub ");
        }

        int r = random_below(w->rng, 100);
        if (r < 25) {
            put_const(w);
        } else if (r < 70) {
            put_decl(w);
        } else if (r < 85) {
            put_struct(w);
        } else if (r < 95) {
            put_enum(w);
        } else {
            put_union(w);
        }
        PUT(w, "\n");
    }
}

static void
put_params(Writer *w)
{
    static const u8 counts[] = { 15, 35, 28, 14, 6, 2 };

    PUT(w, "(");
    int n = pick_weighted(w->rng, counts, sizeof counts);
    for (int i = 0; i < n; i++) {
        if (i > 0) {
            PUT(w, ", ");
        }
        if (chance(w->rng, 3)) {
            PUT(w, "_");
        } else {
            put_name(w);
        }

        int r = random_below(w->rng, 100);
        if (r < 85) {
            PUT(w, ": ");
            put_type(w);
            if (chance(w->rng, 5)) {
                PUT(w, " = ");
                put_int(w);
            }
        } else if (r < 90) {
            PUT(w, " := ");
            put_int(w);
        }
    }
    PUT(w, ")");
}

static void
put_function(Writer *w)
{
    if (chance(w->rng, 25)) {
        put_comment(w);
        PUT(w, "\n");
    }

    if (chance(w->rng, 35)) {
        PUT(w, "pub ");
    }
    if (chance(w->rng, 2)) {
        PUT(w, "export ");
    }
    PUT(w, "fn ");
    put_name(w);
    put_params(w);
    if (chance(w->rng, 70)) {
        PUT(w, " -> ");
        put_type(w);
    }

    if (chance(w->rng, 4)) {
        w->operands = STATEMENT_OPERANDS;
        PUT(w, " => ");
        put_expr(w, 0);
        PUT(w, "\n");
        return;
    }

    put_block(w, 6 + random_below(w->rng, 20));
    PUT(w, "\n");
}

static void
put_imports(Writer *w)
{
    int n = random_below(w->rng, 6);
    for (int i = 0; i < n; i++) {
        if (chance(w->rng, 25)) {
            PUT(w, "const ");
            put_word(w, 0);
            PUT(w, " := ");
        }

        PUT(w, "import \"");
        int parts = 1 + random_below(w->rng, 3);
        for (int k = 0; k < parts; k++) {
            if (k > 0) {
                PUT(w, "/");
            }
            put_name(w);
        }
        if (chance(w->rng, 10)) {
            PUT(w, ".h");
        }
        PUT(w, "\"\n");
    }
    PUT(w, "\n");
}

size_t
random_code(u64 *rng, char *buf, const Vocabulary *v, size_t size)
{
    Writer w = { .rng = rng, .v = v, .buf = buf, .size = size };
    int nlocals = 4 + (int)(size / 1000);
    make_locals(&w, nlocals < LOCAL_NAMES_MAX ? nlocals : LOCAL_NAMES_MAX);

    PUT(&w, "package ");
    put_name(&w);
    PUT(&w, "\n\n");
    put_imports(&w);

    while (w.len < size) {
        if (chance(rng, 15)) {
            put_globals(&w);
        } else {
            put_function(&w);
        }
        PUT(&w, "\n");
    }

    return w.len;
}
