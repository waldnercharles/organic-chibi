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

#define KEYWORD_TEXT(name, text) text,

static const char *const keywords[] = {
    CHIBI_TOKENS(CHIBI_SKIP, KEYWORD_TEXT, CHIBI_SKIP, CHIBI_SKIP)
};

#define KEYWORD_INDEX(name, text) KEYWORD_INDEX_##name,
enum
{
    CHIBI_TOKENS(CHIBI_SKIP, KEYWORD_INDEX, CHIBI_SKIP, CHIBI_SKIP)
};

#define KEYWORD_CHECK(name, text)                                              \
    static_assert(name == TK_IF + KEYWORD_INDEX_##name, "keywords must be consecutive kinds starting at TK_IF");
CHIBI_TOKENS(CHIBI_SKIP, KEYWORD_CHECK, CHIBI_SKIP, CHIBI_SKIP)

#define SOURCE_BYTES_PER_NAME 256

void
lexer_init(Lexer *lx, Arena *arena, size_t total_source_bytes)
{
    size_t expected = total_source_bytes / SOURCE_BYTES_PER_NAME;
    if (expected > UINT32_MAX / 4) {
        expected = UINT32_MAX / 4;
    }

    *lx = (Lexer){ 0 };
    names_init(&lx->names, arena, (u32)expected);
    for (u32 i = 0; i < NKEYWORDS; i++) {
        [[maybe_unused]] u32 id = names_intern(&lx->names, keywords[i], (u32)strlen(keywords[i]));
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
        BEFORE_EQ = 0x0F,   // + - * / % & | ^ = ! < > :  (+= == != <= ...)
        BEFORE_GT = 0x30,   // - =                        (-> =>)
        BEFORE_SAME = 0xC8, // | & . : < >                (|| && .. :: << >>)
    };

    // Lookup by their low and high bits.
    const u8x16 lo_sets = { 0, 0x01, 0,    0,    0,    0x01, 0x41, 0,
                            0, 0,    0x83, 0x01, 0x8A, 0x33, 0xE6, 0x01 };
    const u8x16 hi_sets = { 0, 0, 0x51, 0xA2, 0, 0x04, 0, 0x08,
                            0, 0, 0,    0,    0, 0,    0, 0 };
    u8x16 sets_of_v = simd_lookup16(lo_sets, v & 15) & simd_lookup16(hi_sets, v >> 4);

    u8x16 sets_allowed = (simd_eq(next, '=') & BEFORE_EQ) |
                         (simd_eq(next, '>') & BEFORE_GT) |
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
    u64 backslash;
    u64 comment;
    u64 line_end;
} BlockMasks;

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
        .backslash = MASK64(simd_eq, v, '\\'),
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

static const u8 escape_code[256] = { ['"'] = 1, ['\\'] = 1, ['/'] = 1,
                                     ['b'] = 1, ['f'] = 1,  ['n'] = 1,
                                     ['r'] = 1, ['t'] = 1,  ['u'] = 2 };

static inline bool
escapes_valid(const char *p, u64 escaped)
{
    for (; escaped; escaped &= escaped - 1) {
        const char *e = p + __builtin_ctzll(escaped);
        bool hex4 = byte_is_hex(e[1]) && byte_is_hex(e[2]) &&
                    byte_is_hex(e[3]) && byte_is_hex(e[4]);

        u8 code = escape_code[(u8)*e];
        if (code == 0 || (code == 2 && !hex4)) {
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
resolve_literals(BlockMasks m, LiteralState in, const char *p, u32 from)
{
    u64 active = ~0ULL << from;
    u64 quote = m.quote & active, comment_starts = m.comment & active;
    u64 backslash = m.backslash & active;

    // This assumes every backslash is in a string.
    u64 escaped = 0, escape_carry = in.escaped;
    if (UNLIKELY(backslash | escape_carry)) {
        escaped = escaped_bytes(backslash, &escape_carry);
        quote &= ~escaped;
    }

    u64 comment = 0;
    if (UNLIKELY(in.in_comment)) {
        u64 ends = m.line_end & active;
        comment = ((ends & -ends) - 1) & active;
        quote &= ~comment;
        comment_starts &= ~comment;
    }

    u64 inside;
    for (;;) {
        inside = prefix_xor(quote) ^ -in.in_string;
        u64 c = comment_starts & ~inside;
        if (LIKELY(c == 0)) {
            break;
        }
        c &= -c;
        u64 ends = m.line_end & -c;
        u64 span = (ends & -ends) - c;
        comment |= span;
        quote &= ~span;
        comment_starts &= ~span;
    }

    u64 body = inside << 1 | in.in_string;
    bool fallback = ((backslash & ~comment & ~body) | (m.line_end & body)) != 0 ||
                    ((escaped & body) && !escapes_valid(p, escaped & body));
    u64 keep = -(u64)!fallback;
    return (Literals){
        .quiet = (body | comment) & keep,
        .fallback = fallback,
        .in = in,
        .out = { inside >> 63 & keep, comment >> 63 & keep, escape_carry & keep },
    };
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

static NOINLINE u64
pairs_from_left(u64 candidates)
{
    u64 taken = 0;
    while (candidates) {
        u64 b = candidates & -candidates;
        taken |= b;
        candidates &= ~(b | (b << 1));
    }

    return taken;
}

static inline u64
token_starts(Carry *carry, BlockMasks m, u64 quiet)
{
    u64 continues_name = m.ident & ((m.ident << 1) | carry->in_name);
    carry->in_name = m.ident >> 63;
    u64 starts = ~m.ws & ~continues_name & ~carry->pair_second & ~quiet;

    u64 firsts = m.pairs & starts;
    if (UNLIKELY(firsts & (firsts << 1))) {
        firsts = pairs_from_left(firsts);
    }
    carry->pair_second = firsts >> 63;

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
    CHIBI_TOKENS(CHIBI_SKIP, CHIBI_SKIP, CHIBI_SKIP, KIND2_OP1)                                                 \
    CHIBI_TOKENS(CHIBI_SKIP, CHIBI_SKIP, KIND2_OP2, CHIBI_SKIP)                                                 \
    PAIR('/', '/') = SLOW
// clang-format on

static const u8 kind2[2][256][256] = {
    [0] = { KIND2_ENTRIES },
    [1] = { KIND2_ENTRIES, FIRST('0', '9') = SLOW, FIRST('"', '"') = SLOW },
};

#define KIND_LEN_OP2(name, c0, c1) [name] = 2,
static const u8 kind_len[SLOW] = {
    [0 ... SLOW - 1] = 1,
    [TK_IDENT] = 0,
    [TK_INT] = 0,
    [TK_STRING] = 0,
    [TK_EOF] = 0,
    CHIBI_TOKENS(CHIBI_SKIP, CHIBI_SKIP, KIND_LEN_OP2, CHIBI_SKIP)
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

static inline const char *
number_end(const char *p, bool *is_float)
{
    *is_float = false;
    while (byte_is_digit(*p)) {
        p++;
    }

    if (p[0] == '.' && byte_is_digit(p[1])) {
        *is_float = true;
        p++;
        while (byte_is_digit(*p)) {
            p++;
        }
    }

    const char e = p[0], sign = p[1];
    if ((e == 'e' || e == 'E') &&
        (byte_is_digit(sign) || ((sign == '+' || sign == '-') && byte_is_digit(p[2])))) {
        *is_float = true;
        p += 2;
        while (byte_is_digit(*p)) {
            p++;
        }
    }

    return p;
}

static inline int
escape_len(Lexer *lx, const char *src, const char *q)
{
    char esc = q[1];
    if (esc == '"' || esc == '\\' || esc == '/' || esc == 'b' || esc == 'f' ||
        esc == 'n' || esc == 'r' || esc == 't') {
        return 2;
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
closing_quote(Lexer *lx, const char *src, const char *open)
{
    const char *q = open + 1;
    for (;;) {
        u8x16 v = simd_load(q);
        u8x16 special = simd_eq(v, '"') | simd_eq(v, '\\') | simd_eq(v, '\n') |
                        simd_eq(v, 0);
        u64 hit = simd_mask16(special);
        if (!hit) {
            q += 16;
            continue;
        }
        q += simd_first16(hit);

        char ch = *q;
        if (ch == '\0' || ch == '\n') {
            fail(lx, (u32)(q - src), "unterminated string");
            return NULL;
        }
        if (ch == '"') {
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
        bool is_float;
        const char *end = number_end(p, &is_float);
        out.kinds[ntok] = is_float ? TK_FLOAT : TK_INT;
        out.starts[ntok] = at;
        *emitted = ONE_TOKEN;
        return (u32)(end - src);
    }

    if (c == '"') {
        const char *close = closing_quote(lx, src, p);
        if (close == NULL) {
            *emitted = STOP;
            return at;
        }
        out.kinds[ntok] = TK_STRING;
        out.starts[ntok] = at;
        *emitted = ONE_TOKEN;
        return (u32)(close + 1 - src);
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

    const char *close = closing_quote(lx, src, src + base - 1 - in.escaped);
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

static inline bool
byte_is_ident(u8 c)
{
    return byte_is_digit(c) || (u8)((c | 0x20) - 'a') < 26 || c == '_';
}

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
            fail(lx, at, "unexpected character");
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

        u64 starts = token_starts(&carry, cur, lit.quiet);
        if (UNLIKELY(resume >= base)) {
            starts = starts_from(src, starts, base, resume) & ~lit.quiet;
        }

        t.line_starts[base / BLOCK_BYTES] = first_on_line(&carry, cur.line_end, starts);

        BlockMasks next = block_masks((const u8 *)src + base + BLOCK_BYTES);
        Literals next_lit = resolve_literals(next, lit.out, src + base + BLOCK_BYTES, 0);
        bool slow = lit.fallback | has_unusual_number(cur, starts);

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
    u32 name_tokens[BLOCKS_PER_FLUSH * MAX_NAMES_PER_BLOCK + 1];
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

        u64 starts = token_starts(&carry, cur, lit.quiet);
        if (UNLIKELY(resume >= base)) {
            starts = starts_from(src, starts, base, resume) & ~lit.quiet;
        }

        // PERF: Built before the token loop so the CPU works on them during the
        // loop instead of after the loop's exit, which usually mispredicts.
        BlockMasks next = block_masks((const u8 *)src + base + BLOCK_BYTES);
        Literals next_lit = resolve_literals(next, lit.out, src + base + BLOCK_BYTES, 0);
        const u8 *kinds = (const u8 *)kind2[lit.fallback | has_unusual_number(cur, starts)];

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

            t.kinds[ntok] = (u8)kind;
            t.starts[ntok] = at;
            t.ids[ntok] = (u32)__builtin_ctzll(name_last >> pos) + 1;
            name_tokens[nnames] = (u32)ntok;
            nnames += kind == TK_IDENT;
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
#define KIND_NAME_OP2(name, c0, c1) [name] = { c0, c1 },
#define KIND_NAME_OP1(name, c) [name] = { c },
static const char kind_names[NKINDS][12] = {
    CHIBI_TOKENS(KIND_NAME, KIND_NAME, KIND_NAME_OP2, KIND_NAME_OP1)
};

const char *
lexer_kind_name(TokenKind k)
{
    assert((u32)k < NKINDS);
    return kind_names[k];
}

void
print_error_at(const char *src, u32 pos, const char *msg)
{
    LineCol lc = advance_line_col(src, (LineCol){ 1, 1 }, 0, pos);
    fprintf(stderr, "chibi:%d:%d: %s\n", lc.line, lc.col, msg);
}

void
lexer_print_error(const Lexer *lx, const char *src)
{
    print_error_at(src, lx->error_pos, lx->error);
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

static bool
kind_has_id(TokenKind kind)
{
    return kind == TK_IDENT || (kind >= TK_IF && kind < TK_IF + NKEYWORDS);
}

static const char *
string_end(const char *p)
{
    const char *q = p + 1;
    while (*q != '"') {
        q += *q == '\\' ? (q[1] == 'u' ? 6 : 2) : 1;
    }
    return q + 1;
}

u32
lexer_token_length(const char *src, TokenKind kind, u32 start)
{
    const char *p = src + start;

    if (kind_has_id(kind)) {
        return ident_run_len(p);
    }

    if (kind == TK_INT || kind == TK_FLOAT) {
        bool is_float;
        return (u32)(number_end(p, &is_float) - p);
    }

    if (kind == TK_STRING) {
        return (u32)(string_end(p) - p);
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
        if (kind_has_id(kind)) {
            fprintf(out, " id=%u", tokens.ids[i]);
        }
        fputc('\n', out);
    }
}
