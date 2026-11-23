#include "lexer.h"

#include "simd.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct
{
    int line, col;
} LineCol;

static LineCol
advance_line_col(const char *src, LineCol lc, u32 from, u32 to)
{
    for (u32 i = from; i < to; i++) {
        if (src[i] == '\n') {
            lc.line++;
            lc.col = 1;
        } else {
            lc.col++;
        }
    }

    return lc;
}

static void
fail(Lexer *lx, u32 pos, const char *msg)
{
    lx->error = msg;
    lx->error_pos = pos;
}

#define FIXED_NAME_TEXT(name, text) text,

// clang-format off
static const char *const fixed_names[] = {
    CHIBI_TOKENS(CHIBI_SKIP, FIXED_NAME_TEXT, CHIBI_SKIP, CHIBI_SKIP, CHIBI_SKIP)
    CHIBI_BUILTIN_NAMES(FIXED_NAME_TEXT, FIXED_NAME_TEXT)
};
// clang-format on

#define KEYWORD_INDEX(name, text) KEYWORD_INDEX_##name,
enum
{
    CHIBI_TOKENS(CHIBI_SKIP, KEYWORD_INDEX, CHIBI_SKIP, CHIBI_SKIP, CHIBI_SKIP)
};

#define KEYWORD_CHECK(name, text)                                              \
    static_assert(name == TK_IF + KEYWORD_INDEX_##name, "keywords must be consecutive kinds starting at TK_IF");
CHIBI_TOKENS(CHIBI_SKIP, KEYWORD_CHECK, CHIBI_SKIP, CHIBI_SKIP, CHIBI_SKIP)

#define TYPE_NAME_CHECK(name, text)                                            \
    static_assert(name <= NAME_LAST_KEYWORD + NBUILTIN_TYPES, "builtin type names must come before other fixed names");
CHIBI_BUILTIN_NAMES(TYPE_NAME_CHECK, CHIBI_SKIP)

#define SOURCE_BYTES_PER_NAME 256

void
lexer_init(Lexer *lx, Arena *arena, size_t total_source_bytes)
{
    size_t expected = total_source_bytes / SOURCE_BYTES_PER_NAME;
    if (expected > UINT32_MAX / 4) {
        expected = UINT32_MAX / 4;
    }

    Names names;
    names_init(&names, arena, (u32)expected);
    *lx = (Lexer){ .names = names };
    for (u32 i = 0; i < NFIXED_NAMES; i++) {
        const char *name = fixed_names[i];
        [[maybe_unused]] u32 id = names_intern(&lx->names, name, (u32)strlen(name));
        assert(id == i + 1);
    }
}

static inline u8x16
is_ident(u8x16 v)
{
    u8x16 letter = simd_in_range(v | 0x20, 'a', 'z');
    u8x16 digit = simd_in_range(v, '0', '9');

    return letter | digit | simd_eq(v, '_');
}

static inline u8x16
is_ws(u8x16 v)
{
    const u8x16 ws_char = { ' ', 0,    0,    0, 0, 0,    0, 0,
                            0,   '\t', '\n', 0, 0, '\r', 0, 0 };
    return simd_eq(simd_lookup16(ws_char, v & 15), v);
}

static inline u8x16
pair_firsts(u8x16 v, u8x16 next)
{
    enum
    {
        BEFORE_EQ = 0x0F, // + - * / % & | ^ = ! < > : ?  (+= == != <= ...)
        BEFORE_STAR = 0x10,
        BEFORE_GT = 0x20,   // - =                          (-> =>)
        BEFORE_SAME = 0x4A, // | & . : < = > ! ?            (|| .. !! ?? ...)
        BEFORE_PERCENT = 0x80,
    };

    // Lookup by their low and high bits.
    const u8x16 lo_sets = { 0, 0x41, 0,    0,    0,    0x01, 0x41, 0,
                            0, 0,    0x83, 0x81, 0x0A, 0xA3, 0x56, 0x03 };
    const u8x16 hi_sets = { 0, 0, 0xF1, 0x22, 0, 0x04, 0, 0x08,
                            0, 0, 0,    0,    0, 0,    0, 0 };
    u8x16 sets_of_v = simd_lookup16(lo_sets, v & 15) & simd_lookup16(hi_sets, v >> 4);

    // clang-format off
    const u8x16 lo_allowed = { 0, 0, 0,           0, 0, BEFORE_PERCENT, 0,         0,
                               0, 0, BEFORE_STAR, 0, 0, BEFORE_EQ,      BEFORE_GT, 0 };
    const u8x16 hi_allowed = { 0, 0, BEFORE_STAR | BEFORE_PERCENT, BEFORE_EQ | BEFORE_GT,
                               0, 0, 0,                            0,
                               0, 0, 0,                            0,
                               0, 0, 0,                            0 };
    // clang-format on
    u8x16 sets_allowed = (simd_lookup16(lo_allowed, next & 15) &
                          simd_lookup16(hi_allowed, next >> 4)) |
                         (simd_eq(next, v) & BEFORE_SAME);

    return simd_any_bits(sets_of_v, sets_allowed);
}

#define BLOCK_BYTES 64

typedef struct
{
    u64 ident;
    u64 ws;
    u64 pairs;
    u64 digit;
    u64 dot;
    u64 quote;
    u64 rare;
    u64 comment;
    u64 line_end;
} BlockMasks;

static inline u8x16
is_rare(u8x16 v)
{
    const u8x16 rare_char = { 0, 0, 0, 0, 0,    0, 0, '\'',
                              0, 0, 0, 0, '\\', 0, 0, 0 };
    return simd_eq(simd_lookup16(rare_char, v & 15), v);
}

static inline u8x16
is_comment_start(u8x16 v, u8x16 next)
{
    return simd_eq(v, '/') & simd_eq(next, '/');
}

static inline u8x16
is_line_end(u8x16 v)
{
    return simd_eq(v, '\n') | simd_eq(v, 0);
}

#define MASK64(f, v, ...)                                                      \
    simd_mask64(                                                               \
        f((v).v[0] __VA_OPT__(, ) __VA_ARGS__),                                \
        f((v).v[1] __VA_OPT__(, ) __VA_ARGS__),                                \
        f((v).v[2] __VA_OPT__(, ) __VA_ARGS__),                                \
        f((v).v[3] __VA_OPT__(, ) __VA_ARGS__)                                 \
    )

#define MASK64_PAIR(f, v, n)                                                   \
    simd_mask64(                                                               \
        f((v).v[0], (n).v[0]),                                                 \
        f((v).v[1], (n).v[1]),                                                 \
        f((v).v[2], (n).v[2]),                                                 \
        f((v).v[3], (n).v[3])                                                  \
    )

static inline ALWAYS_INLINE BlockMasks
block_masks(const u8 *p)
{
    u8x64 v = simd_load64(p);
    u8x64 next = simd_next64(v, p);

    return (BlockMasks){
        .ident = MASK64(is_ident, v),
        .ws = MASK64(is_ws, v),
        .pairs = MASK64_PAIR(pair_firsts, v, next),
        .digit = MASK64(simd_in_range, v, '0', '9'),
        .dot = MASK64(simd_eq, v, '.'),
        .quote = MASK64(simd_eq, v, '"'),
        .rare = MASK64(is_rare, v),
        .comment = MASK64_PAIR(is_comment_start, v, next),
        .line_end = MASK64(is_line_end, v),
    };
}

static inline u64
prefix_xor(u64 x)
{
    x ^= x << 1;
    x ^= x << 2;
    x ^= x << 4;
    x ^= x << 8;
    x ^= x << 16;
    x ^= x << 32;
    return x;
}

static inline u64
escaped_bytes(u64 backslash, u64 *carry)
{
    const u64 odd = 0xAAAAAAAAAAAAAAAAULL;

    u64 potential = backslash & ~*carry;
    u64 code = (((potential << 1) | odd) - potential) ^ odd;
    u64 escaped = code ^ (backslash | *carry);
    *carry = (code & backslash) >> 63;

    return escaped;
}

static inline bool
byte_is_digit(u8 c)
{
    return (u8)(c - '0') <= 9;
}

static inline bool
byte_is_hex(u8 c)
{
    return byte_is_digit(c) || (u8)((c | 0x20) - 'a') < 6;
}

static inline bool
byte_is_ident(u8 c)
{
    return byte_is_digit(c) || (u8)((c | 0x20) - 'a') < 26 || c == '_';
}

enum
{
    ESCAPE_BYTE = 1,
    ESCAPE_ZERO,
    ESCAPE_HEX2,
    ESCAPE_HEX4,
};

static const u8 escape_code[256] = {
    ['"'] = ESCAPE_BYTE, ['\''] = ESCAPE_BYTE, ['\\'] = ESCAPE_BYTE,
    ['/'] = ESCAPE_BYTE, ['b'] = ESCAPE_BYTE,  ['f'] = ESCAPE_BYTE,
    ['n'] = ESCAPE_BYTE, ['r'] = ESCAPE_BYTE,  ['t'] = ESCAPE_BYTE,
    ['0'] = ESCAPE_ZERO, ['x'] = ESCAPE_HEX2,  ['u'] = ESCAPE_HEX4,
};

static inline bool
escapes_valid(const char *p, u64 escaped)
{
    for (; escaped; escaped &= escaped - 1) {
        const char *e = p + __builtin_ctzll(escaped);
        bool hex2 = byte_is_hex(e[1]) && byte_is_hex(e[2]);
        bool hex4 = hex2 && byte_is_hex(e[3]) && byte_is_hex(e[4]);

        u8 code = escape_code[(u8)*e];
        bool valid = code == ESCAPE_BYTE ||
                     (code == ESCAPE_ZERO && !byte_is_digit(e[1])) ||
                     (code == ESCAPE_HEX2 && hex2) || (code == ESCAPE_HEX4 && hex4);
        if (!valid) {
            return false;
        }
    }

    return true;
}

typedef struct
{
    u64 in_string;
    u64 in_comment;
    u64 escaped;
} LiteralState;

typedef struct
{
    u64 quiet;
    bool fallback;
    LiteralState in, out;
} Literals;

static inline ALWAYS_INLINE Literals
literals_in(BlockMasks m, LiteralState in, const char *p, u64 active, u64 backslash, u64 squote)
{
    u64 quote = m.quote & active, comment_starts = m.comment & active;

    // This assumes every backslash is in a string or a character literal.
    u64 escaped = 0, escape_carry = in.escaped;
    if (backslash | escape_carry) {
        escaped = escaped_bytes(backslash, &escape_carry);
        quote &= ~escaped;
        squote &= ~escaped;
    }

    u64 comment = 0;
    if (UNLIKELY(in.in_comment)) {
        u64 ends = m.line_end & active;
        comment = ((ends & -ends) - 1) & active;
        quote &= ~comment;
        squote &= ~comment;
        comment_starts &= ~comment;
    }

    u64 inside, chars = 0, unclosed = 0;
    for (;;) {
        inside = prefix_xor(quote) ^ -in.in_string;
        u64 c = (comment_starts | squote) & ~inside;
        if (LIKELY(c == 0)) {
            break;
        }
        c &= -c;

        u64 span;
        if (c & squote) {
            u64 later = squote & -(c << 1);
            u64 close = later & -later;
            span = (close << 1) - c;
            chars |= span & ~c;
            unclosed |= close == 0 ? c : 0;
        } else {
            u64 ends = m.line_end & -c;
            span = (ends & -ends) - c;
            comment |= span;
        }
        quote &= ~span;
        squote &= ~span;
        comment_starts &= ~span;
    }

    u64 body = inside << 1 | in.in_string | chars;
    bool fallback = (unclosed | (backslash & ~comment & ~body) |
                     (m.line_end & body)) != 0 ||
                    ((escaped & body) && !escapes_valid(p, escaped & body));
    u64 keep = -(u64)!fallback;
    return (Literals){
        .quiet = (body | comment) & keep,
        .fallback = fallback,
        .in = in,
        .out = { inside >> 63 & keep, comment >> 63 & keep, escape_carry & keep },
    };
}

static NOINLINE Literals
rare_literals(BlockMasks m, LiteralState in, const char *p, u64 active)
{
    u8x64 v = simd_load64((const u8 *)p);
    u64 backslash = MASK64(simd_eq, v, '\\') & active;
    u64 squote = MASK64(simd_eq, v, '\'') & active;
    return literals_in(m, in, p, active, backslash, squote);
}

static inline ALWAYS_INLINE Literals
resolve_literals(BlockMasks m, LiteralState in, const char *p, u32 from)
{
    u64 active = ~0ULL << from;
    if (UNLIKELY((m.rare & active) | in.escaped)) {
        return rare_literals(m, in, p, active);
    }

    return literals_in(m, (LiteralState){ in.in_string, in.in_comment, 0 }, p, active, 0, 0);
}

static inline Literals
literals_after(BlockMasks m, const char *p, u64 r)
{
    if (r >= BLOCK_BYTES) {
        return (Literals){ 0 };
    }

    return resolve_literals(m, (LiteralState){ 0 }, p, (u32)r);
}

typedef struct
{
    u64 in_name;
    u64 pair_second;
    u64 line_break;
} Carry;

static inline bool
is_triple(const char *p)
{
    bool shift_assign = (p[0] == '<' || p[0] == '>') && p[1] == p[0] && p[2] == '=';
    bool range = p[0] == '.' && p[1] == '.' && p[2] == '=';
    bool wrap_assign = (p[0] == '+' || p[0] == '-' || p[0] == '*') &&
                       p[1] == '%' && p[2] == '=';
    bool or_else_assign = p[0] == '?' && p[1] == '?' && p[2] == '=';
    return shift_assign | range | wrap_assign | or_else_assign;
}

static NOINLINE u64
operators_from_left(const char *p, u64 candidates, u64 *triples)
{
    u64 taken = 0;
    while (candidates) {
        u64 b = candidates & -candidates;
        taken |= b;
        if (is_triple(p + __builtin_ctzll(b))) {
            *triples |= b;
            candidates &= ~(b | (b << 1) | (b << 2));
        } else {
            candidates &= ~(b | (b << 1));
        }
    }

    return taken;
}

static inline u64
token_starts(Carry *carry, BlockMasks m, u64 quiet, const char *p, u64 *triples)
{
    u64 continues_name = m.ident & ((m.ident << 1) | carry->in_name);
    carry->in_name = m.ident >> 63;
    u64 starts = ~m.ws & ~continues_name & ~carry->pair_second & ~quiet;

    u64 firsts = m.pairs & starts;
    *triples = 0;
    if (UNLIKELY(firsts & ((firsts >> 1) | (m.dot & (m.dot >> 1))))) {
        firsts = operators_from_left(p, firsts, triples);
    } else if (UNLIKELY(firsts >> 63) && is_triple(p + 63)) {
        *triples = 1ULL << 63;
    }
    carry->pair_second = (firsts >> 63) | (*triples >> 62);

    return starts & ~(firsts << 1);
}

static inline u64
first_on_line(Carry *carry, u64 line_end, u64 starts)
{
    u64 sum;
    bool over = __builtin_add_overflow(~starts, line_end, &sum);
    over |= __builtin_add_overflow(sum, carry->line_break, &sum);
    carry->line_break = over;

    return sum & starts;
}

static inline bool
has_unusual_number(BlockMasks m, u64 starts)
{
    u64 sum;
    bool crosses = __builtin_add_overflow(m.digit, starts & m.digit, &sum);
    return crosses || (sum & ~m.digit & (m.ident | m.dot)) != 0;
}

static inline bool
byte_is_ws(u8 c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static inline u64
restart_at(u64 starts, u32 r, u8 byte)
{
    starts &= ~0ULL << r;
    return starts | (u64)!byte_is_ws(byte) << r;
}

static inline u64
starts_from(const char *src, u64 starts, u64 base, u32 end)
{
    u64 r = end - base;
    if (r >= BLOCK_BYTES) {
        return 0;
    }
    return restart_at(starts, (u32)r, (u8)src[end]);
}

#define SLOW 0x80
static_assert(TK_OP < SLOW, "token kinds must fit in 7 bits of kind2[]");

static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "kind2[] is indexed by a little-endian load");

#define FIRST(lo, hi) [0 ... 255][(lo)...(hi)]
#define PAIR(c0, c1) [c1][c0]
#define KIND2_OP1(name, c) FIRST(c, c) = name,
#define KIND2_OP2(name, c0, c1) PAIR(c0, c1) = name,
#define KIND2_SLOW_OP3(name, c0, c1, c2) PAIR(c0, c1) = SLOW,

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverride-init"
// clang-format off
#define KIND2_ENTRIES                                                                                           \
    FIRST(0x00, 0x20) = SLOW,                                                                                   \
    FIRST(0x21, 0xFF) = TK_OP,                                                                                  \
    FIRST('0', '9') = TK_INT,                                                                                   \
    FIRST('a', 'z') = TK_IDENT,                                                                                 \
    FIRST('A', 'Z') = TK_IDENT,                                                                                 \
    FIRST('_', '_') = TK_IDENT,                                                                                 \
    FIRST('"', '"') = TK_STRING,                                                                                \
    FIRST('\'', '\'') = TK_CHAR,                                                                                \
    CHIBI_TOKENS(CHIBI_SKIP, CHIBI_SKIP, CHIBI_SKIP, CHIBI_SKIP, KIND2_OP1)                                     \
    CHIBI_TOKENS(CHIBI_SKIP, CHIBI_SKIP, CHIBI_SKIP, KIND2_OP2, CHIBI_SKIP)                                     \
    PAIR('/', '/') = SLOW,                                                                                      \
    ['0' ... '9']['0'] = SLOW
// clang-format on

static const u8 kind2[2][256][256] = {
    [0] = { KIND2_ENTRIES },
    [1] = { KIND2_ENTRIES,
            FIRST('0', '9') = SLOW,
            FIRST('"', '"') = SLOW,
            FIRST('\'', '\'') = SLOW,
            CHIBI_TOKENS(CHIBI_SKIP, CHIBI_SKIP, KIND2_SLOW_OP3, CHIBI_SKIP, CHIBI_SKIP) },
};

#define KIND_LEN_OP3(name, c0, c1, c2) [name] = 3,
#define KIND_LEN_OP2(name, c0, c1) [name] = 2,
static const u8 kind_len[SLOW] = {
    [0 ... SLOW - 1] = 1,
    [TK_IDENT] = 0,
    [TK_INT] = 0,
    [TK_STRING] = 0,
    [TK_CHAR] = 0,
    [TK_EOF] = 0,
    CHIBI_TOKENS(CHIBI_SKIP, CHIBI_SKIP, KIND_LEN_OP3, KIND_LEN_OP2, CHIBI_SKIP)
};
#pragma GCC diagnostic pop

static inline u32
kind2_index(const char *p)
{
    u16 two;
    memcpy(&two, p, 2);
    return two;
}

enum
{
    NO_TOKEN,
    ONE_TOKEN,
    STOP
};

typedef struct
{
    const char *end;
    const char *error;
    TokenKind kind;
} Number;

static inline bool
byte_in_base(u8 c, u32 base)
{
    return base == 16 ? byte_is_hex(c) : (u8)(c - '0') < base;
}

static inline const char *
digits_end(const char *p, u32 base, const char **error)
{
    for (;;) {
        while (byte_in_base((u8)*p, base)) {
            p++;
        }
        if (*p != '_') {
            return p;
        }
        if (!byte_in_base((u8)p[1], base)) {
            *error = "`_` must sit between two digits";
            return p;
        }
        p++;
    }
}

static Number
scan_number(const char *p)
{
    static const char *const expected[17] = {
        [2] = "expected a binary digit",
        [8] = "expected an octal digit",
        [16] = "expected a hex digit",
    };
    static const char *const invalid[17] = {
        [2] = "invalid digit in binary number",
        [8] = "invalid digit in octal number",
        [10] = "a name cannot follow a number",
        [16] = "invalid digit in hex number",
    };

    if (p[0] == '0' && (p[1] == 'X' || p[1] == 'B' || p[1] == 'O')) {
        return (Number){ p + 1, "use a lowercase prefix like `0x`", TK_INT };
    }

    const char *error = NULL;
    TokenKind kind = TK_INT;
    u32 base = 10;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'b' || p[1] == 'o')) {
        base = p[1] == 'x' ? 16 : p[1] == 'b' ? 2 : 8;
        p += 2;
        if (!byte_in_base((u8)*p, base)) {
            return (Number){ p,
                             *p == '_' ? "`_` must sit between two digits"
                                       : expected[base],
                             kind };
        }
        p = digits_end(p, base, &error);
    } else {
        if (p[0] == '0' &&
            (byte_is_digit((u8)p[1]) || (p[1] == '_' && byte_is_digit((u8)p[2])))) {
            return (Number){ p, "leading zeros are not allowed; use `0o` for octal", kind };
        }

        p = digits_end(p, 10, &error);
        if (error == NULL && p[0] == '.' && byte_is_digit((u8)p[1])) {
            kind = TK_FLOAT;
            p = digits_end(p + 1, 10, &error);
        }
        if (error == NULL && (p[0] == 'e' || p[0] == 'E')) {
            const char *digits = p + 1 + (p[1] == '+' || p[1] == '-');
            if (byte_is_digit((u8)*digits)) {
                kind = TK_FLOAT;
                p = digits_end(digits, 10, &error);
            }
        }
    }

    if (error == NULL && byte_is_ident((u8)*p)) {
        error = invalid[base];
    }
    return (Number){ p, error, kind };
}

static inline int
escape_len(Lexer *lx, const char *src, const char *q)
{
    char esc = q[1];
    if (esc == '"' || esc == '\'' || esc == '\\' || esc == '/' || esc == 'b' ||
        esc == 'f' || esc == 'n' || esc == 'r' || esc == 't') {
        return 2;
    }

    if (esc == '0') {
        if (byte_is_digit((u8)q[2])) {
            fail(lx, (u32)(q - src), "a digit can't follow `\\0`; use `\\x00`");
            return 0;
        }
        return 2;
    }

    if (esc == 'x') {
        if (!byte_is_hex((u8)q[2]) || !byte_is_hex((u8)q[3])) {
            fail(lx, (u32)(q - src), "invalid hex escape");
            return 0;
        }
        return 4;
    }

    if (esc == 'u') {
        for (int i = 2; i < 6; i++) {
            if (!byte_is_hex((u8)q[i])) {
                fail(lx, (u32)(q - src), "invalid unicode hex escape");
                return 0;
            }
        }
        return 6;
    }

    fail(lx, (u32)(q - src), "unknown escape sequence");
    return 0;
}

static inline const char *
closing_quote(Lexer *lx, const char *src, const char *open, u8 quote)
{
    const char *q = open + 1;
    for (;;) {
        u8x16 v = simd_load(q);
        u8x16 special = simd_eq(v, quote) | simd_eq(v, '\\') |
                        simd_eq(v, '\n') | simd_eq(v, 0);
        u64 hit = simd_mask16(special);
        if (!hit) {
            q += 16;
            continue;
        }
        q += simd_first16(hit);

        u8 ch = (u8)*q;
        if (ch == '\0' || ch == '\n') {
            fail(lx, (u32)(q - src), quote == '"' ? "unterminated string" : "unterminated character literal");
            return NULL;
        }
        if (ch == quote) {
            return q;
        }

        int n = escape_len(lx, src, q);
        if (n == 0) {
            return NULL;
        }
        q += n;
    }
}

static inline const char *
line_end(const char *p)
{
    for (;;) {
        u8x16 v = simd_load(p);
        u64 end = simd_mask16(simd_eq(v, '\n') | simd_eq(v, 0));
        if (end) {
            return p + simd_first16(end);
        }
        p += 16;
    }
}

static inline ALWAYS_INLINE u32
slow_token(Lexer *lx, const char *src, u32 at, Tokens out, int ntok, int *emitted)
{
    const char *p = src + at;
    u8 c = (u8)*p;

    if (byte_is_digit(c)) {
        Number n = scan_number(p);
        if (n.error != NULL) {
            fail(lx, (u32)(n.end - src), n.error);
            *emitted = STOP;
            return at;
        }
        out.kinds[ntok] = n.kind;
        out.starts[ntok] = at;
        *emitted = ONE_TOKEN;
        return (u32)(n.end - src);
    }

    if (c == '"' || c == '\'') {
        const char *close = closing_quote(lx, src, p, c);
        if (close == NULL) {
            *emitted = STOP;
            return at;
        }
        out.kinds[ntok] = c == '"' ? TK_STRING : TK_CHAR;
        out.starts[ntok] = at;
        *emitted = ONE_TOKEN;
        return (u32)(close + 1 - src);
    }

    if (c == '<' || c == '>') {
        bool assign = p[2] == '=';
        TokenKind shift = c == '<' ? TK_SHL : TK_SHR;
        out.kinds[ntok] = assign ? (c == '<' ? TK_SHLEQ : TK_SHREQ) : shift;
        out.starts[ntok] = at;
        *emitted = ONE_TOKEN;
        return at + 2 + assign;
    }

    if (c == '.') {
        bool inclusive = p[2] == '=';
        out.kinds[ntok] = inclusive ? TK_RANGE_INCLUSIVE : TK_RANGE;
        out.starts[ntok] = at;
        *emitted = ONE_TOKEN;
        return at + 2 + inclusive;
    }

    if ((c == '+' || c == '-' || c == '*') && p[1] == '%') {
        bool assign = p[2] == '=';
        TokenKind wrap = c == '+' ? TK_ADD_WRAP
                       : c == '-' ? TK_SUB_WRAP
                                  : TK_MUL_WRAP;
        TokenKind wrap_assign = c == '+' ? TK_ADD_WRAP_EQ
                              : c == '-' ? TK_SUB_WRAP_EQ
                                         : TK_MUL_WRAP_EQ;
        out.kinds[ntok] = assign ? wrap_assign : wrap;
        out.starts[ntok] = at;
        *emitted = ONE_TOKEN;
        return at + 2 + assign;
    }

    if (c == '?') {
        bool assign = p[2] == '=';
        out.kinds[ntok] = assign ? TK_OR_ELSE_EQ : TK_OR_ELSE;
        out.starts[ntok] = at;
        *emitted = ONE_TOKEN;
        return at + 2 + assign;
    }

    // Only "//" gets here. '/' and "/=" take the fast path.
    if (c == '/') {
        *emitted = NO_TOKEN;
        return (u32)(line_end(p + 2) - src);
    }

    if (c != 0) {
        fail(lx, at, "unexpected character");
    }
    *emitted = STOP;
    return at;
}

static NOINLINE u32
finish_literal(Lexer *lx, const char *src, u64 base, LiteralState in)
{
    if (in.in_comment) {
        return (u32)(line_end(src + base) - src);
    }

    const char *close = closing_quote(lx, src, src + base - 1 - in.escaped, '"');
    if (close == NULL) {
        return UINT32_MAX;
    }

    return (u32)(close + 1 - src);
}

#define LOW_BYTES(n) ((n) >= 8 ? ~0ULL : (1ULL << (8 * ((n) & 7))) - 1)
#define KEY_MASK(n) { LOW_BYTES(n), (n) <= 8 ? 0 : LOW_BYTES((n) - 8) }
static const u64 key_mask[17][2] = {
    KEY_MASK(0),  KEY_MASK(1),  KEY_MASK(2),  KEY_MASK(3),  KEY_MASK(4),
    KEY_MASK(5),  KEY_MASK(6),  KEY_MASK(7),  KEY_MASK(8),  KEY_MASK(9),
    KEY_MASK(10), KEY_MASK(11), KEY_MASK(12), KEY_MASK(13), KEY_MASK(14),
    KEY_MASK(15), KEY_MASK(16),
};

static NOINLINE u32
intern_cold(Names *names, const char *s, u32 len, u64 k0, u64 k1)
{
    return names_intern_hashed(names, s, len, k0, k1, names_hash(s, len, k0, k1));
}

static NOINLINE u32
ident_run_len(const char *p)
{
    const char *s = p;
    for (;;) {
        u64 stop = simd_mask16(~is_ident(simd_load(p)));
        if (stop) {
            return (u32)(p - s) + simd_first16(stop);
        }
        p += 16;
    }
}

#define BLOCKS_PER_FLUSH 64
#define MAX_NAMES_PER_BLOCK (BLOCK_BYTES / 2)

#ifdef CHIBI_LEXER_BULK

static NOINLINE void
intern_names(
    Names *names,
    const char *src,
    Tokens out,
    const u32 *name_tokens,
    int n,
    const u64 *run_ends,
    u32 window_base
)
{
    Tokens t = out; // PERF: Forces `out` into a register
    NameSlot *slots = names->slots;
    u32 mask = names->mask;

    for (int i = 0; i < n; i++) {
        u32 k = name_tokens[i];
        u32 start = t.starts[k];
        const char *s = src + start;

        u32 offset = start - window_base;
        u32 block = offset / BLOCK_BYTES, pos = offset % BLOCK_BYTES;
        u64 here = run_ends[block] >> pos, after = run_ends[block + 1];
        u32 len = here ? (u32)__builtin_ctzll(here) + 1
                       : BLOCK_BYTES - pos +
                             (u32)__builtin_ctzll(after | 1ULL << 63) + 1;
        if (UNLIKELY(here == 0 && after == 0)) {
            len = ident_run_len(s);
        }

        u64 w0, w1;
        memcpy(&w0, s, 8);
        memcpy(&w1, s + 8, 8);
        const u64 *km = key_mask[len < 16 ? len : 16];
        u64 k0 = w0 & km[0], k1 = w1 & km[1];

        const NameSlot *slot = &slots[(u32)names_hash_short(k0, k1) & mask];
        u32 id;
        if (LIKELY(((slot->k0 ^ k0) | (slot->k1 ^ k1) | (slot->len ^ len)) == 0 && len <= 16)) {
            id = slot->id;
        } else {
            id = intern_cold(names, s, len, k0, k1);
            slots = names->slots; // intern_cold may have grown the table
            mask = names->mask;
        }

        t.kinds[k] = id <= NKEYWORDS ? (u8)(TK_IF + (id - 1)) : TK_IDENT;
        t.ids[k] = id;
    }
}

#define BIT_POS(b, i)                                                          \
    ((u32)((__builtin_popcount((b) & 0x01) <= (i)) +                           \
           (__builtin_popcount((b) & 0x03) <= (i)) +                           \
           (__builtin_popcount((b) & 0x07) <= (i)) +                           \
           (__builtin_popcount((b) & 0x0F) <= (i)) +                           \
           (__builtin_popcount((b) & 0x1F) <= (i)) +                           \
           (__builtin_popcount((b) & 0x3F) <= (i)) +                           \
           (__builtin_popcount((b) & 0x7F) <= (i)) +                           \
           (__builtin_popcount((b) & 0xFF) <= (i))))
#define BIT_ROW(b)                                                             \
    { BIT_POS(b, 0), BIT_POS(b, 1), BIT_POS(b, 2), BIT_POS(b, 3),              \
      BIT_POS(b, 4), BIT_POS(b, 5), BIT_POS(b, 6), BIT_POS(b, 7) }
#define BIT_ROW4(b)                                                            \
    BIT_ROW(b), BIT_ROW((b) + 1), BIT_ROW((b) + 2), BIT_ROW((b) + 3)
#define BIT_ROW16(b)                                                           \
    BIT_ROW4(b), BIT_ROW4((b) + 4), BIT_ROW4((b) + 8), BIT_ROW4((b) + 12)
#define BIT_ROW64(b)                                                           \
    BIT_ROW16(b), BIT_ROW16((b) + 16), BIT_ROW16((b) + 32), BIT_ROW16((b) + 48)
static const u32 bit_positions[256][8] = { BIT_ROW64(0),
                                           BIT_ROW64(64),
                                           BIT_ROW64(128),
                                           BIT_ROW64(192) };

static inline u32
write_starts(u32 *dst, u64 starts, u32 base)
{
    u64 c = starts - ((starts >> 1) & 0x5555555555555555ULL);
    c = (c & 0x3333333333333333ULL) + ((c >> 2) & 0x3333333333333333ULL);
    c = (c + (c >> 4)) & 0x0F0F0F0F0F0F0F0FULL;
    u64 ends = c * 0x0101010101010101ULL;

    u32 n = 0;
    for (u32 k = 0; k < 8; k++) {
        const u32 *row = bit_positions[(starts >> (8 * k)) & 0xFF];
        u32x4 lo, hi;
        memcpy(&lo, row, sizeof lo);
        memcpy(&hi, row + 4, sizeof hi);

        u32 offset = base + 8 * k;
        lo += offset;
        hi += offset;
        memcpy(dst + n, &lo, sizeof lo);
        memcpy(dst + n + 4, &hi, sizeof hi);
        n = (u32)(ends >> (8 * k)) & 0xFF;
    }

    return n;
}

static NOINLINE int
read_kinds(Lexer *lx, const char *src, Tokens out, int from, int to, u32 *name_tokens, int nnames)
{
    Tokens t = out;
    const u8 *kinds = (const u8 *)kind2[0];
    for (int i = from; i < to; i++) {
        u32 at = t.starts[i];
        u32 kind = kinds[kind2_index(src + at)];
        if (UNLIKELY(kind & SLOW)) {
            Number n = byte_is_digit((u8)src[at])
                         ? scan_number(src + at)
                         : (Number){ src + at, "unexpected character", TK_OP };
            assert(n.error != NULL);
            fail(lx, (u32)(n.end - src), n.error);
            return -1;
        }

        t.kinds[i] = (u8)kind;
        name_tokens[nnames] = (u32)i;
        nnames += kind == TK_IDENT;
    }

    return nnames;
}

int
lexer_tokenize(Lexer *lx, const char *src, Tokens out)
{
    lx->error = NULL;

    Tokens t = out; // PERF: Forces `out` into a register

    // + 1 because the loop writes the next slot before it knows the token's kind.
    u32 name_tokens[BLOCKS_PER_FLUSH * MAX_NAMES_PER_BLOCK + 1];
    u64 run_ends[BLOCKS_PER_FLUSH + 1];
    int nnames = 0;
    int ntok = 0;
    int kinds_from = 0;
    Carry carry = { .line_break = 1 };
    u32 resume = 0;

    BlockMasks cur = block_masks((const u8 *)src);
    Literals lit = resolve_literals(cur, (LiteralState){ 0 }, src, 0);
    for (u64 base = 0;; base += BLOCK_BYTES) {
        if (UNLIKELY(resume >= base)) {
            lit = literals_after(cur, src + base, resume - base);
        } else if (UNLIKELY(lit.fallback && (lit.in.in_string | lit.in.in_comment))) {
            nnames = read_kinds(lx, src, t, kinds_from, ntok, name_tokens, nnames);
            if (nnames < 0) {
                return 0;
            }
            kinds_from = ntok;
            resume = finish_literal(lx, src, base, lit.in);
            if (resume == UINT32_MAX) {
                return 0;
            }
            lit = literals_after(cur, src + base, resume - base);
        }

        u64 triples;
        u64 starts = token_starts(&carry, cur, lit.quiet, src + base, &triples);
        if (UNLIKELY(resume >= base)) {
            starts = starts_from(src, starts, base, resume) & ~lit.quiet;
        }

        t.line_starts[base / BLOCK_BYTES] = first_on_line(&carry, cur.line_end, starts);

        BlockMasks next = block_masks((const u8 *)src + base + BLOCK_BYTES);
        Literals next_lit = resolve_literals(next, lit.out, src + base + BLOCK_BYTES, 0);
        bool slow = lit.fallback | has_unusual_number(cur, starts) | (triples != 0);

        u32 block = (u32)(base / BLOCK_BYTES) % BLOCKS_PER_FLUSH;
        u64 after_next = byte_is_ident((u8)src[base + 2 * BLOCK_BYTES]);
        run_ends[block] = cur.ident & ~((cur.ident >> 1) | next.ident << 63);
        run_ends[block + 1] = next.ident & ~((next.ident >> 1) | after_next << 63);

        if (LIKELY(!slow && !(cur.line_end & ~cur.ws))) {
            ntok += (int)write_starts(t.starts + ntok, starts, (u32)base);
        } else {
            nnames = read_kinds(lx, src, t, kinds_from, ntok, name_tokens, nnames);
            if (nnames < 0) {
                return 0;
            }

            const u8 *kinds = (const u8 *)kind2[slow];
            while (starts) {
                u32 pos = (u32)__builtin_ctzll(starts);
                starts &= starts - 1;
                u32 at = base + pos;

                u32 kind = kinds[kind2_index(src + at)];
                if (UNLIKELY(kind & SLOW)) {
                    int emitted;
                    u32 after = slow_token(lx, src, at, t, ntok, &emitted);
                    if (emitted == STOP) {
                        if (lx->error != NULL) {
                            return 0;
                        }

                        intern_names(&lx->names, src, t, name_tokens, nnames, run_ends, (u32)base - block * BLOCK_BYTES);

                        t.kinds[ntok] = TK_EOF;
                        t.starts[ntok] = at;
                        t.ids[ntok] = 0;
                        t.line_starts[at / BLOCK_BYTES] |= 1ULL << (at % BLOCK_BYTES);
                        return ntok + 1;
                    }
                    ntok += emitted;
                    resume = after;
                    starts = starts_from(src, starts, base, resume);
                    continue;
                }

                t.kinds[ntok] = (u8)kind;
                t.starts[ntok] = at;
                name_tokens[nnames] = (u32)ntok;
                nnames += kind == TK_IDENT;
                ntok++;
            }
            kinds_from = ntok;
        }

        if (block == BLOCKS_PER_FLUSH - 1) {
            nnames = read_kinds(lx, src, t, kinds_from, ntok, name_tokens, nnames);
            if (nnames < 0) {
                return 0;
            }
            kinds_from = ntok;
            intern_names(&lx->names, src, t, name_tokens, nnames, run_ends, (u32)base - block * BLOCK_BYTES);
            nnames = 0;
        }

        cur = next;
        lit = next_lit;
    }
}

#else

static inline u64
past_runs(u64 run, u64 first)
{
    return (run + (first & run)) & ~run;
}

static NOINLINE void
intern_names(Names *names, const char *src, Tokens out, const u32 *name_tokens, int n)
{
    Tokens t = out; // PERF: Forces `out` into a register
    NameSlot *slots = names->slots;
    u32 mask = names->mask;

    for (int i = 0; i < n; i++) {
        u32 k = name_tokens[i];
        const char *s = src + t.starts[k];
        u32 len = t.ids[k];

        u64 w0, w1;
        memcpy(&w0, s, 8);
        memcpy(&w1, s + 8, 8);
        const u64 *km = key_mask[len < 16 ? len : 16];
        u64 k0 = w0 & km[0], k1 = w1 & km[1];

        const NameSlot *slot = &slots[(u32)names_hash_short(k0, k1) & mask];
        u32 id;
        if (LIKELY(((slot->k0 ^ k0) | (slot->k1 ^ k1) | (slot->len ^ len)) == 0 && len <= 16)) {
            id = slot->id;
        } else {
            id = intern_cold(names, s, len, k0, k1);
            slots = names->slots; // intern_cold may have grown the table
            mask = names->mask;
        }

        t.kinds[k] = id <= NKEYWORDS ? (u8)(TK_IF + (id - 1)) : TK_IDENT;
        t.ids[k] = id;
    }
}

static inline u32
trailing_zeros(u64 x, u32 if_zero)
{
    return x ? (u32)__builtin_ctzll(x) : if_zero;
}

static inline void
extend_last_name(const char *src, Tokens out, int last, u64 next_ident, u64 block_end)
{
    u32 rest = trailing_zeros(~next_ident, 64);
    if (UNLIKELY(rest == 64)) {
        rest = ident_run_len(src + block_end);
    }

    u32 length = out.ids[last];
    bool continues = out.kinds[last] == TK_IDENT &&
                     out.starts[last] + length == block_end;
    out.ids[last] = length + (continues ? rest : 0);
}

int
lexer_tokenize(Lexer *lx, const char *src, Tokens out)
{
    lx->error = NULL;

    Tokens t = out; // PERF: Forces `out` into a register

    // + 1 because the loop writes the next slot before it knows the token's kind.
    u32 names_buffer[BLOCKS_PER_FLUSH * MAX_NAMES_PER_BLOCK + 1];
    u32 *name_tokens = names_buffer;
    int nnames = 0;
    int ntok = 0;
    Carry carry = { .line_break = 1 };
    u32 resume = 0;

    BlockMasks cur = block_masks((const u8 *)src);
    Literals lit = resolve_literals(cur, (LiteralState){ 0 }, src, 0);
    for (u64 base = 0;; base += BLOCK_BYTES) {
        if (UNLIKELY(resume >= base)) {
            lit = literals_after(cur, src + base, resume - base);
        } else if (UNLIKELY(lit.fallback && (lit.in.in_string | lit.in.in_comment))) {
            resume = finish_literal(lx, src, base, lit.in);
            if (resume == UINT32_MAX) {
                return 0;
            }
            lit = literals_after(cur, src + base, resume - base);
        }

        u64 triples;
        u64 starts = token_starts(&carry, cur, lit.quiet, src + base, &triples);
        if (UNLIKELY(resume >= base)) {
            starts = starts_from(src, starts, base, resume) & ~lit.quiet;
        }

        // PERF: Built before the token loop so the CPU works on them during the
        // loop instead of after the loop's exit, which usually mispredicts.
        BlockMasks next = block_masks((const u8 *)src + base + BLOCK_BYTES);
        Literals next_lit = resolve_literals(next, lit.out, src + base + BLOCK_BYTES, 0);
        bool slow = lit.fallback | has_unusual_number(cur, starts) | (triples != 0);
        const u8 *kinds = (const u8 *)kind2[slow];

        u64 name_last = (past_runs(cur.ident, starts) >> 1) | 1ULL << 63;
        t.line_starts[base / BLOCK_BYTES] = first_on_line(&carry, cur.line_end, starts);

        while (starts) {
            u32 pos = (u32)__builtin_ctzll(starts);
            starts &= starts - 1;
            u32 at = base + pos;

            u32 kind = kinds[kind2_index(src + at)];
            if (UNLIKELY(kind & SLOW)) {
                int emitted;
                u32 after = slow_token(lx, src, at, t, ntok, &emitted);
                if (emitted == STOP) {
                    if (lx->error != NULL) {
                        return 0;
                    }

                    intern_names(&lx->names, src, t, name_tokens, nnames);

                    t.kinds[ntok] = TK_EOF;
                    t.starts[ntok] = at;
                    t.ids[ntok] = 0;
                    t.line_starts[at / BLOCK_BYTES] |= 1ULL << (at % BLOCK_BYTES);
                    return ntok + 1;
                }
                ntok += emitted;
                resume = after;
                starts = starts_from(src, starts, base, resume);
                continue;
            }

            name_tokens[nnames] = (u32)ntok;
            nnames += kind == TK_IDENT;
            t.kinds[ntok] = (u8)kind;
            t.starts[ntok] = at;
            t.ids[ntok] = (u32)__builtin_ctzll(name_last >> pos) + 1;
            ntok++;
        }

        if (LIKELY(ntok > 0)) {
            extend_last_name(src, t, ntok - 1, next.ident, base + BLOCK_BYTES);
        }

        if ((base / BLOCK_BYTES) % BLOCKS_PER_FLUSH == BLOCKS_PER_FLUSH - 1) {
            intern_names(&lx->names, src, t, name_tokens, nnames);
            nnames = 0;
        }

        cur = next;
        lit = next_lit;
    }
}

#endif

#define KIND_NAME(name, text) [name] = text,
#define KIND_NAME_OP3(name, c0, c1, c2) [name] = { c0, c1, c2 },
#define KIND_NAME_OP2(name, c0, c1) [name] = { c0, c1 },
#define KIND_NAME_OP1(name, c) [name] = { c },
static const char kind_names[NKINDS][12] = {
    CHIBI_TOKENS(KIND_NAME, KIND_NAME, KIND_NAME_OP3, KIND_NAME_OP2, KIND_NAME_OP1)
};

const char *
lexer_kind_name(TokenKind k)
{
    assert((u32)k < NKINDS);
    return kind_names[k];
}

void
print_error_at(FILE *out, const char *path, const char *src, u32 pos, const char *msg)
{
    LineCol lc = advance_line_col(src, (LineCol){ 1, 1 }, 0, pos);
    fprintf(out, "%s:%d:%d: %s\n", path, lc.line, lc.col, msg);
}

void
lexer_print_error(FILE *out, const Lexer *lx, const char *path, const char *src)
{
    print_error_at(out, path, src, lx->error_pos, lx->error);
}

Tokens
lexer_alloc_tokens(Arena *arena, size_t len)
{
    size_t n = lexer_max_tokens(len);
    return (Tokens){
        .kinds = arena_alloc(arena, n, 1),
        .starts = arena_alloc(arena, n * sizeof(u32), alignof(u32)),
        .ids = arena_alloc(arena, n * sizeof(u32), alignof(u32)),
        .line_starts = arena_alloc(arena, lexer_line_words(len) * sizeof(u64), alignof(u64)),
    };
}

static const char *
quoted_end(const char *p)
{
    const char *q = p + 1;
    while (*q != *p) {
        q += *q == '\\' ? (q[1] == 'u' ? 6 : 2) : 1;
    }
    return q + 1;
}

u32
lexer_token_length(const char *src, TokenKind kind, u32 start)
{
    const char *p = src + start;

    if (lexer_kind_has_id(kind)) {
        return ident_run_len(p);
    }

    if (kind == TK_INT || kind == TK_FLOAT) {
        return (u32)(scan_number(p).end - p);
    }

    if (kind == TK_STRING || kind == TK_CHAR) {
        return (u32)(quoted_end(p) - p);
    }

    return kind_len[kind];
}

void
lexer_dump(FILE *out, const char *src, Tokens tokens, int n)
{
    LineCol lc = { 1, 1 };
    u32 at = 0;
    for (int i = 0; i < n; i++) {
        TokenKind kind = tokens.kinds[i];
        u32 start = tokens.starts[i];

        lc = advance_line_col(src, lc, at, start);
        at = start;

        fprintf(
            out,
            "%-10s @ %d:%d \"%.*s\"",
            lexer_kind_name(kind),
            lc.line,
            lc.col,
            (int)lexer_token_length(src, kind, start),
            src + start
        );
        if (lexer_kind_has_id(kind)) {
            fprintf(out, " id=%u", tokens.ids[i]);
        }
        fputc('\n', out);
    }
}
