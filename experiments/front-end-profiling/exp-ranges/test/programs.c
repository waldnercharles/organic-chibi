#include "programs.h"

#include "../src/lexer.h"

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

// clang-format off
static const char *const PIECES[] = {
    "if", "else", "for", "while", "continue", "offsetof", "fn", "return", "true", "as",
    "x", "_", "name", "Name_2", "abcdefg", "abcdefgh", "abcdefghx",
    "abcdefghijklmno", "abcdefghijklmnop", "abcdefghijklmnopxy", "a_name_longer_than_sixteen_bytes",
    "0", "7", "42", "3.14", "1.", "1e5", "1E9", "2e+8", "6e-3", "1e",
    "\"\"", "\"text\"", "\"\\\" \\\\ \\/ \\b \\f \\n \\r \\t\"", "\"\\u00e9\\uABCD\"", "\"caf\xc3\xa9\"",
    "\"// not a comment\"", "// comment\n", "//\n", "// \"quote\" \\ backslash\n",
    "<<", ">>", "&&", "||", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "->", "=>", "==",
    "!=", "<=", ">=", "..", "::", ":=", "+", "-", "*", "/", "%", "=", "!", "@", "$", "?", "&",
    "|", "^", "~", "<", ">", ":", ";", ".", ",", "{", "}", "(", ")", "[", "]", "#", "'", "\\",
    "`", "\x7f", "\xc3\xa9",
    " ", " ", " ", " ", "  ", "        ", "\t", "\n", "\n", "\r\n",
};
// clang-format on

const Pieces all_pieces = { PIECES, sizeof PIECES / sizeof PIECES[0] };

size_t
random_program(u64 *rng, char *buf, Pieces pieces, int count)
{
    size_t len = 0;
    for (int i = 0; i < count; i++) {
        char name[16];
        const char *piece = pieces.items[random_below(rng, pieces.count)];
        if (random_below(rng, 8) == 0) {
            snprintf(name, sizeof name, "v%d", random_below(rng, 100000));
            piece = name;
        }

        size_t n = strlen(piece);
        assert(n <= PIECE_MAX_LEN);
        memcpy(buf + len, piece, n);
        len += n;
    }

    return len;
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
add_random_name(u64 *rng, Vocabulary *v, u32 start, u32 *n, int len, char first)
{
    static const char rest[] = "abcdefghijklmnopqrstuvwxyz_";

    char *s = v->text + (size_t)*n * CODE_NAME_MAX_LEN;
    s[0] = (char)(first + random_below(rng, 26));
    for (int i = 1; i < len; i++) {
        s[i] = rest[random_below(rng, sizeof rest - 1)];
    }

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

static void
put_text(Writer *w, const char *s)
{
    put(w, s, strlen(s));
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
    static const char rest[] = "abcdefghijklmnopqrstuvwxyz_";

    while (w->nlocals < n) {
        char *s = w->locals[w->nlocals];
        int len = pick_weighted(w->rng, name_lengths, CODE_NAME_MAX_LEN + 1);
        s[0] = (char)('a' + random_below(w->rng, 26));
        for (int i = 1; i < len; i++) {
            s[i] = rest[random_below(w->rng, sizeof rest - 1)];
        }
        if (!is_keyword(s, len)) {
            w->local_lens[w->nlocals++] = (u8)len;
        }
    }
}

static void
put_type(Writer *w)
{
    put_word(w, 0);
}

static void
put_int(Writer *w)
{
    static const char *const common[] = { "0", "1",  "2",  "3",  "4",
                                          "8", "16", "64", "256" };
    static const u8 weights[] = { 41, 24, 8, 3, 3, 3, 3, 2, 1 };

    if (chance(w->rng, 85)) {
        put_text(w, common[pick_weighted(w->rng, weights, sizeof weights)]);
        return;
    }

    char digits[16];
    int n = snprintf(digits, sizeof digits, "%d", random_below(w->rng, 10000));
    put(w, digits, (size_t)n);
}

static void
put_float(Writer *w)
{
    char digits[16];
    int n = snprintf(digits, sizeof digits, "%d.%d", random_below(w->rng, 100), random_below(w->rng, 100));
    put(w, digits, (size_t)n);
}

static void
put_string(Writer *w)
{
    static const char *const escapes[] = { "\\n", "\\t", "\\\"", "\\\\" };
    static const u8 weights[] = { 10, 2, 2, 1 };

    put(w, "\"", 1);
    int words = chance(w->rng, 8) ? 8 + random_below(w->rng, 16)
                                  : 1 + random_below(w->rng, 4);
    for (int i = 0; i < words; i++) {
        if (i > 0) {
            put_text(w, chance(w->rng, 10) ? "\\n" : " ");
        }
        put_name(w);
    }

    if (chance(w->rng, 16)) {
        put_text(w, escapes[pick_weighted(w->rng, weights, sizeof weights)]);
    }
    put(w, "\"", 1);
}

static void
put_line_break(Writer *w, int extra)
{
    put(w, "\n", 1);
    for (int i = 0; i < w->indent + extra; i++) {
        put(w, "    ", 4);
    }
}

static void put_expr(Writer *w, int depth);

static void
put_fn_literal(Writer *w, int depth)
{
    put(w, "fn(", 3);
    put_name(w);
    if (chance(w->rng, 40)) {
        put(w, ": ", 2);
        put_type(w);
    }
    put(w, ") => ", 5);
    put_expr(w, depth + 1);
}

static void
put_args(Writer *w, int depth)
{
    static const u8 counts[] = { 2, 40, 25, 23, 6, 3, 1 };

    put(w, "(", 1);
    int n = pick_weighted(w->rng, counts, sizeof counts);
    bool split = depth == 0 && n >= 2 && chance(w->rng, 4);
    for (int i = 0; i < n; i++) {
        if (split) {
            put_line_break(w, 1);
        } else if (i > 0) {
            put(w, ", ", 2);
        }

        if (chance(w->rng, 6)) {
            put(w, ".", 1);
            put_name(w);
            put(w, " = ", 3);
        }

        if (depth < EXPR_DEPTH && chance(w->rng, 3)) {
            put_fn_literal(w, depth);
        } else {
            put_expr(w, depth + 1);
        }

        if (split) {
            put(w, ",", 1);
        }
    }

    if (split) {
        put_line_break(w, 0);
    }
    put(w, ")", 1);
}

static void
put_postfix(Writer *w, int depth)
{
    put_name(w);
    while (w->operands > 0) {
        int r = random_below(w->rng, 100);
        if (r < 22) {
            put(w, ".", 1);
            put_name(w);
        } else if (r < 28 && depth < EXPR_DEPTH) {
            put(w, "[", 1);
            put_expr(w, depth + 1);
            put(w, "]", 1);
        } else if (r < 36 && depth < EXPR_DEPTH) {
            put_args(w, depth);
        } else if (r < 37) {
            put(w, ".*", 2);
        } else {
            return;
        }
        w->operands--;
    }
}

static void
put_operand(Writer *w, int depth)
{
    static const char unary[] = "---!!!&&~";
    static const char *const atoms[] = { "true", "false", "nil" };

    w->operands--;
    if (w->operands <= 0) {
        put_name(w);
        return;
    }

    int r = random_below(w->rng, 100);
    if (r < 9) {
        put(w, &unary[r], 1);
        put_postfix(w, depth);
    } else if (r < 12 && depth < EXPR_DEPTH) {
        put(w, "(", 1);
        put_expr(w, depth + 1);
        put(w, ")", 1);
    } else if (r < 30) {
        put_int(w);
    } else if (r < 31) {
        if (chance(w->rng, 40)) {
            put_float(w);
        } else {
            put_int(w);
        }
    } else if (r < 42) {
        put_string(w);
    } else if (r < 44) {
        put_text(w, atoms[random_below(w->rng, 3)]);
    } else {
        put_postfix(w, depth);
    }
}

static const char *const binary_ops[] = {
    " + ", " - ",   " * ",  " / ",  " % ",  " << ", " >> ", " & ", " | ",
    " ^ ", " and ", " or ", " == ", " != ", " < ",  " <= ", " > ", " >= ",
};
static const u8 binary_weights[] = { 24, 14, 10, 4, 2, 3, 3, 6, 4,
                                     1,  5,  2,  8, 5, 6, 2, 4, 2 };

enum
{
    FIRST_LOGICAL = 10,
    FIRST_COMPARE = 12,
    NBINARY = sizeof binary_weights,
};

static void
put_expr(Writer *w, int depth)
{
    static const u8 counts[2][5] = { { 0, 45, 30, 15, 10 }, { 0, 75, 20, 5, 0 } };

    int n = pick_weighted(w->rng, counts[depth > 0], sizeof counts[0]);
    bool compared = false;
    put_operand(w, depth);
    for (int i = 1; i < n; i++) {
        int op = pick_weighted(w->rng, binary_weights, NBINARY);
        if (op >= FIRST_COMPARE && compared) {
            op = 0;
        }
        compared = op >= FIRST_COMPARE || (compared && op < FIRST_LOGICAL);

        put_text(w, binary_ops[op]);
        put_operand(w, depth);
    }
}

static void
put_comparison(Writer *w)
{
    put_operand(w, 1);
    if (chance(w->rng, 20)) {
        put_text(w, binary_ops[pick_weighted(w->rng, binary_weights, FIRST_LOGICAL)]);
        put_operand(w, 1);
    }

    int op = FIRST_COMPARE +
             pick_weighted(w->rng, binary_weights + FIRST_COMPARE, NBINARY - FIRST_COMPARE);
    put_text(w, binary_ops[op]);
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
        put(w, "!", 1);
        put_postfix(w, 1);
    } else {
        put_comparison(w);
        if (chance(w->rng, 20)) {
            const char *op = chance(w->rng, 70) ? "and " : "or ";
            if (chance(w->rng, 10)) {
                put_line_break(w, 1);
            } else {
                put(w, " ", 1);
            }
            put_text(w, op);
            put_comparison(w);
        }
    }
}

static void
put_indent(Writer *w)
{
    for (int i = 0; i < w->indent; i++) {
        put(w, "    ", 4);
    }
}

static void
put_target(Writer *w)
{
    put_name(w);

    int r = random_below(w->rng, 100);
    if (r < 30) {
        put(w, ".", 1);
        put_name(w);
    } else if (r < 45) {
        put(w, "[", 1);
        put_expr(w, 1);
        put(w, "]", 1);
    } else if (r < 48) {
        put(w, ".*", 2);
    }
}

static void
put_decl(Writer *w)
{
    put_name(w);

    int r = random_below(w->rng, 100);
    if (r < 60) {
        put(w, " := ", 4);
        put_expr(w, 0);
    } else if (r < 85) {
        put(w, ": ", 2);
        put_type(w);
        put(w, " = ", 3);
        put_expr(w, 0);
    } else {
        put(w, ": ", 2);
        put_type(w);
    }
}

static void
put_call(Writer *w)
{
    put_name(w);
    if (chance(w->rng, 30)) {
        put(w, ".", 1);
        put_name(w);
    }
    put_args(w, 0);
}

static void
put_assign(Writer *w)
{
    put_target(w);
    put(w, " = ", 3);
    put_expr(w, 0);
}

static void
put_update(Writer *w)
{
    static const char *const ops[] = { " += ",  " -= ",  " |= ", " &= ",
                                       " <<= ", " >>= ", " *= " };
    static const u8 weights[] = { 10, 4, 3, 2, 1, 1, 1 };

    put_target(w);
    put_text(w, ops[pick_weighted(w->rng, weights, sizeof weights)]);
    if (chance(w->rng, 50)) {
        put(w, "1", 1);
    } else {
        put_expr(w, 0);
    }
}

static void
put_return(Writer *w)
{
    put(w, "return", 6);
    if (chance(w->rng, 88)) {
        put(w, " ", 1);
        put_expr(w, 0);
    }
}

static void
put_comment(Writer *w)
{
    put(w, "//", 2);
    int words = 3 + random_below(w->rng, 9);
    for (int i = 0; i < words; i++) {
        put(w, " ", 1);
        put_name(w);
    }
}

static void
put_const(Writer *w)
{
    put(w, "const ", 6);
    put_name(w);
    if (chance(w->rng, 70)) {
        put(w, " := ", 4);
    } else {
        put(w, ": ", 2);
        put_type(w);
        put(w, " = ", 3);
    }
    put_expr(w, 0);
}

static void put_statement(Writer *w);

static void
put_body(Writer *w, int count)
{
    w->indent++;
    for (int i = 0; i < count && w->len < w->size; i++) {
        if (i > 0 && chance(w->rng, w->indent == 1 ? 2 : 0)) {
            put(w, "\n", 1);
        }
        put_statement(w);
    }
    w->indent--;

    put_indent(w);
    put(w, "}", 1);
}

static void
put_else(Writer *w, const char *rest)
{
    if (chance(w->rng, 10)) {
        put_line_break(w, 0);
    } else {
        put(w, " ", 1);
    }
    put_text(w, rest);
}

static void
put_if(Writer *w)
{
    put(w, "if ", 3);
    put_condition(w);
    put(w, " {\n", 3);
    put_body(w, 1 + random_below(w->rng, 4));

    if (!chance(w->rng, 12)) {
        return;
    }

    while (w->len < w->size && chance(w->rng, 30)) {
        put_else(w, "else if ");
        put_condition(w);
        put(w, " {\n", 3);
        put_body(w, 1 + random_below(w->rng, 4));
    }
    put_else(w, "else {\n");
    put_body(w, 1 + random_below(w->rng, 4));
}

static void
put_while(Writer *w)
{
    put(w, "while ", 6);
    put_condition(w);
    put(w, " {\n", 3);

    w->loops++;
    put_body(w, 1 + random_below(w->rng, 5));
    w->loops--;
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
    NSTATEMENTS,
};

static const u8 statement_weights[NSTATEMENTS] = { 19, 13, 10, 4, 8, 2,
                                                   1,  3,  8,  4, 2 };
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

    w->operands = STATEMENT_OPERANDS;
    put_indent(w);
    switch (pick_weighted(w->rng, weights, NSTATEMENTS)) {
        case STATEMENT_DECL: put_decl(w); break;
        case STATEMENT_CALL: put_call(w); break;
        case STATEMENT_ASSIGN: put_assign(w); break;
        case STATEMENT_UPDATE: put_update(w); break;
        case STATEMENT_RETURN: put_return(w); break;
        case STATEMENT_JUMP:
            put_text(w, chance(w->rng, 60) ? "break" : "continue");
            break;
        case STATEMENT_DEFER:
            put(w, "defer ", 6);
            put_call(w);
            break;
        case STATEMENT_COMMENT: put_comment(w); break;
        case STATEMENT_IF: put_if(w); break;
        case STATEMENT_WHILE: put_while(w); break;
        case STATEMENT_CONST: put_const(w); break;
    }

    if (chance(w->rng, 4)) {
        put(w, " ", 1);
        put_comment(w);
    }
    put(w, "\n", 1);
}

static void
put_globals(Writer *w)
{
    int n = 1 + random_below(w->rng, 4);
    for (int i = 0; i < n && w->len < w->size; i++) {
        w->operands = STATEMENT_OPERANDS;
        if (chance(w->rng, 30)) {
            put_const(w);
        } else {
            put_decl(w);
        }
        put(w, "\n", 1);
    }
}

static void
put_params(Writer *w)
{
    static const u8 counts[] = { 15, 35, 28, 14, 6, 2 };

    put(w, "(", 1);
    int n = pick_weighted(w->rng, counts, sizeof counts);
    for (int i = 0; i < n; i++) {
        if (i > 0) {
            put(w, ", ", 2);
        }
        put_name(w);

        int r = random_below(w->rng, 100);
        if (r < 85) {
            put(w, ": ", 2);
            put_type(w);
            if (chance(w->rng, 5)) {
                put(w, " = ", 3);
                put_int(w);
            }
        } else if (r < 90) {
            put(w, " := ", 4);
            put_int(w);
        }
    }
    put(w, ")", 1);
}

static void
put_function(Writer *w)
{
    if (chance(w->rng, 25)) {
        put_comment(w);
        put(w, "\n", 1);
    }

    put(w, "fn ", 3);
    put_name(w);
    put_params(w);
    if (chance(w->rng, 70)) {
        put(w, " -> ", 4);
        put_type(w);
    }

    if (chance(w->rng, 4)) {
        w->operands = STATEMENT_OPERANDS;
        put(w, " => ", 4);
        put_expr(w, 0);
        put(w, "\n", 1);
        return;
    }

    put(w, " {\n", 3);
    put_body(w, 6 + random_below(w->rng, 20));
    put(w, "\n", 1);
}

size_t
random_code(u64 *rng, char *buf, const Vocabulary *v, size_t size)
{
    Writer w = { .rng = rng, .v = v, .buf = buf, .size = size };
    int nlocals = 4 + (int)(size / 1000);
    make_locals(&w, nlocals < LOCAL_NAMES_MAX ? nlocals : LOCAL_NAMES_MAX);

    while (w.len < size) {
        if (chance(rng, 15)) {
            put_globals(&w);
        } else {
            put_function(&w);
        }
        put(&w, "\n", 1);
    }

    return w.len;
}
