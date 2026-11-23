#define _DEFAULT_SOURCE

#include "../src/arena.h"
#include "../vendor/pico_unit/pico_unit.h"
#include "random/code.h"
#include "ref_lexer.h"
#include "samples.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
print_mismatch(const char *label, const char *src, int n, RefToken fast, RefToken ref)
{
    fprintf(stderr, "%s: token #%d\n", label, n);
    fprintf(
        stderr,
        "  fast: %-8s start=%u len=%u id=%u starts_line=%d\n",
        lexer_kind_name(fast.kind),
        fast.start,
        fast.length,
        fast.id,
        fast.starts_line
    );
    fprintf(
        stderr,
        "  ref:  %-8s start=%u len=%u id=%u starts_line=%d\n",
        lexer_kind_name(ref.kind),
        ref.start,
        ref.length,
        ref.id,
        ref.starts_line
    );

    u32 from = ref.start > 20 ? ref.start - 20 : 0;
    fprintf(stderr, "  context: \"");
    for (u32 i = from; i < ref.start + ref.length + 20 && src[i] != '\0'; i++) {
        u8 ch = (u8)src[i];
        if (ch == '\n') {
            fprintf(stderr, "\\n");
        } else if (ch < 0x20 || ch >= 0x7f) {
            fprintf(stderr, "\\x%02x", ch);
        } else {
            fputc(ch, stderr);
        }
    }
    fprintf(stderr, "\"\n");
}

static void
print_error_mismatch(const char *label, const Lexer *lx, int nfast, const RefLexer *rl)
{
    fprintf(stderr, "%s: errors differ\n", label);
    if (nfast == 0) {
        fprintf(stderr, "  fast: \"%s\" at byte %u\n", lx->error, lx->error_pos);
    } else {
        fprintf(stderr, "  fast: no error\n");
    }
    if (rl->error != NULL) {
        fprintf(stderr, "  ref:  \"%s\" at byte %d\n", rl->error, rl->error_pos);
    } else {
        fprintf(stderr, "  ref:  no error\n");
    }
}

static Tokens
tokens_new(size_t len)
{
    size_t n = lexer_max_tokens(len);
    return (Tokens){
        malloc(n),
        malloc(n * sizeof(u32)),
        malloc(n * sizeof(u32)),
        malloc(lexer_line_words(len) * sizeof(u64)),
    };
}

static void
tokens_free(Tokens t)
{
    free(t.kinds);
    free(t.starts);
    free(t.ids);
    free(t.line_starts);
}

static RefToken
fast_token(const char *src, Tokens t, int n)
{
    TokenKind kind = t.kinds[n];
    bool named = kind == TK_IDENT || (kind >= TK_IF && kind < TK_IF + NKEYWORDS);
    return (RefToken){
        kind,
        t.starts[n],
        lexer_token_length(src, kind, t.starts[n]),
        named ? t.ids[n] : 0,
        lexer_token_starts_line(t, n),
    };
}

static bool
matches_reference(const char *label, const char *src, size_t len, Lexer *lx, RefLexer *rl)
{
    Tokens fast = tokens_new(len);
    int nfast = lexer_tokenize(lx, src, fast);

    RefToken *ref = malloc((len + 1) * sizeof *ref);
    int nref = 0;
    rl->error = NULL;
    for (int pos = 0;;) {
        RefToken b = ref_next(rl, src, pos);
        if (rl->error != NULL) {
            break;
        }
        ref[nref++] = b;
        if (b.kind == TK_EOF) {
            break;
        }
        pos = (int)(b.start + b.length);
    }

    if (nfast > 0 && rl->error == NULL) {
        for (int n = 0; n < nref; n++) {
            RefToken a = n < nfast
                           ? fast_token(src, fast, n)
                           : (RefToken){ TK_OP, UINT32_MAX, UINT32_MAX, 0, false };
            RefToken b = ref[n];
            if (a.kind != b.kind || a.start != b.start || a.length != b.length ||
                a.id != b.id || a.starts_line != b.starts_line) {
                print_mismatch(label, src, n, a, b);
                free(ref);
                tokens_free(fast);
                return false;
            }
        }
    }

    free(ref);
    tokens_free(fast);

    bool fast_failed = nfast == 0, ref_failed = rl->error != NULL;
    bool same = fast_failed == ref_failed &&
                (!ref_failed || (lx->error_pos == (u32)rl->error_pos &&
                                 strcmp(lx->error, rl->error) == 0));
    if (!same) {
        print_error_mismatch(label, lx, nfast, rl);
    }
    return same;
}

#define LONG_TEXT                                                              \
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrst" \
    "uvwxyz"

// clang-format off
static const char *EDGE_CASES[] = {
    "",
    "x",
    "abcdefgh",                // exactly 8 bytes
    "abcdefghi",               // 9 bytes
    "continue offsetof",
    "very_long_name|x",
    "header_document}",
    "abcdefgh_ijklmn{",        // { lands at byte 15 of a 16-byte window
    "abcdefgh`ijklmno~p",      // ` and ~ are in 0x60..0x7e, like letters
    "a_b_c_d_e_f_g_h_i_j\x7f",
    "name\xc3\xa9suffix",
    "1 1.5 1e5 1e+5 1e-5 1. 1.. 1..2 1ex 1e+ 12.5e3",
    "1 1.5 1e5 1e+5 1e-5 1. 1.. 1..2 12.5e3 0 0.5 0e5 2.x 1._5 a1_0",
    "0x1F 0xff_ff 0xABCDEF0123456789 0b1010 0b1_0 0o755 0o7_7 1_000 1_00_000 1_000.5 3.141_592 1e1_0 0..<1_0",
    "0x", "0xG", "0b2", "0o8", "0b102", "0xFG", "0o78", "1_", "1__0", "0x_1", "1.5_", "1_.5", "1e5_",
    "017", "0_1", "x := 007", "0X1F", "0B1", "0O7", "12ab", "1e5x", "0x1F.5", "0b1.5",
    "007 " LONG_TEXT LONG_TEXT,
    "x " LONG_TEXT " 1_0 0x_F " LONG_TEXT,
    "\"\" \"a\" \"esc\\\"aped\" \"\\u00e9\" \"\\\\\" \"tab\\t\"",
    "a // comment\nb //\n//\nc",
    "<<= >>= &&= ||= ... ::= -> => != == <= >= :: :=",
    "# ' \\ ` @ $ ? ~",
    "   \t\r\n\n    x                                  y",
    "x \"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"y z",
    "x //ccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc\ny==z",
    "a abcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghij b",
    "1234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890abc",
    "=== ::= <<= >>> &&& ||| ... ->> =>= !== :::: ==== x===y",
    "<<==> >>=> <<<= >>>= <<=== a<<=b a>>=b<<=c p.* p.*=1 p.*==q ..* .** a.*.b 1.*2",
    "0..<n 1..=10 a..b ...< ..<= ..== ..<< .. < ..= x..<y.z 1.5..<2 0..<..=..",
    "\"abc\"def 12.5.6 1e5e5 12..34 a.b.c",
    "/=== /==> a/==b //=== x/=y ==/= 1/=2",
    "a +% b -% c *% d +%= e -%= f *%= g -%x a+%b a-%b a*%b",
    "+%% -%- *%* %+% +%=%= *%== -%> +%. %%= ++% --% **%= 1+%2 1-%=2",
    "undefined undefinedx xundefined undefine",
    "p!! p!!.x a!! + b x!!!! x!!=y x!!==y !!x !! ! !x !=!! a ?? b a??b x ?\?= y x?\?=y",
    "?\?== ??? ???? ?\?\?= ?= ?=? ?== ?!! !?? ?. ?: ?* ?[ ?T ??T !!!= ?\?!= !!?",
    LONG_TEXT "+x 9 \"s\" a " LONG_TEXT,
    LONG_TEXT LONG_TEXT "+x 9 \"s\" a " LONG_TEXT LONG_TEXT,
    "\"abcdefghijklmnop\\\"qrstuvwxyz\" x",
    "// comment with \"quote\" and \\ backslash\ny",
    "\"str // not a comment\" z // real \"comment\"\nw",
    "a\"b\"c \\ \"d\" \\\"e\"",
    "x //",
    "//\n\"\"//\"\n\"//\"",
    "\"a\\\\\\\"b\\\\\" x \"\\u00e9\\uABCD\" y",
    "\"\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\" z",
    "\"" LONG_TEXT "\ny\"",
    "\"" LONG_TEXT,
    "\"" LONG_TEXT "\\q\"",
    "\"" LONG_TEXT "\\u12G4\"",
    LONG_TEXT "\x01",
    "\"" LONG_TEXT "\x01\t\" z",
    "\"" LONG_TEXT "\\\"\\\\\\u00e9\" w // c \"x\n v",
    "// " LONG_TEXT "\"\n\"iii\n",
    "\"// " LONG_TEXT "\" k // \"\n\"\\q\"",
    "// " LONG_TEXT,
    "( [ ) ]",
    "{{{ ))) ]]]",
    "([{}]) {[()]}",
    "{ ( } )",
    "f(a[1], {b}) )",
    "((((((((((((((((((((((((((((((((((((((((x))))))))))))))))))))))))))))))))))))))))",
    "a\nb c\n\nd",
    "a // c\nb\n// only\n\n  c\r\n d",
    "\"s\"\nx 1.5\ny \"t\" z",
    "x \\ y\n// a comment that ends the file",
    "x \\ y\n// " LONG_TEXT "\n z",
};
// clang-format on

static bool
edge_case_matches(size_t i, int offset)
{
    size_t text_len = strlen(EDGE_CASES[i]);
    size_t len = (size_t)offset + text_len;
    char *src = calloc(len + LEXER_SOURCE_PADDING, 1);
    memset(src, ' ', (size_t)offset);
    memcpy(src + offset, EDGE_CASES[i], text_len);

    char label[48];
    snprintf(label, sizeof label, "edge case #%zu at offset %d", i, offset);

    Arena arena = { 0 };
    Lexer lx;
    lexer_init(&lx, &arena, len);
    RefLexer *rl = ref_lexer_new();

    bool same = matches_reference(label, src, len, &lx, rl);

    ref_lexer_free(rl);
    arena_free(&arena);
    free(src);
    return same;
}

TEST_CASE(edge_cases)
{
    for (size_t i = 0; i < sizeof EDGE_CASES / sizeof EDGE_CASES[0]; i++) {
        for (int offset = 0; offset < 128; offset++) {
            REQUIRE(edge_case_matches(i, offset));
        }
    }
    return true;
}

static const struct
{
    const char *src;
    const char *error;
    u32 pos;
} NUMBER_ERRORS[] = {
    { "0x", "expected a hex digit", 2 },
    { "0xG", "expected a hex digit", 2 },
    { "0b2", "expected a binary digit", 2 },
    { "0o8", "expected an octal digit", 2 },
    { "0b102", "invalid digit in binary number", 4 },
    { "0xFG", "invalid digit in hex number", 3 },
    { "0o78", "invalid digit in octal number", 3 },
    { "1_", "`_` must sit between two digits", 1 },
    { "1__0", "`_` must sit between two digits", 1 },
    { "0x_1", "`_` must sit between two digits", 2 },
    { "1.5_", "`_` must sit between two digits", 3 },
    { "017", "leading zeros are not allowed; use `0o` for octal", 0 },
    { "x := 007", "leading zeros are not allowed; use `0o` for octal", 5 },
    { "0_1", "leading zeros are not allowed; use `0o` for octal", 0 },
    { "0X1F", "use a lowercase prefix like `0x`", 1 },
    { "12ab", "a name cannot follow a number", 2 },
    { "1e5x", "a name cannot follow a number", 3 },
};

static bool
number_fails_with(const char *text, const char *error, u32 pos)
{
    size_t len = strlen(text);
    char *src = calloc(len + LEXER_SOURCE_PADDING, 1);
    memcpy(src, text, len);

    Arena arena = { 0 };
    Lexer lx;
    lexer_init(&lx, &arena, len);
    Tokens t = tokens_new(len);
    int n = lexer_tokenize(&lx, src, t);

    bool same = n == 0 && strcmp(lx.error, error) == 0 && lx.error_pos == pos;
    if (!same) {
        fprintf(
            stderr,
            "lex \"%s\"\n  got:  %s at byte %u\n  want: %s at byte %u\n",
            text,
            n == 0 ? lx.error : "no error",
            lx.error_pos,
            error,
            pos
        );
    }

    tokens_free(t);
    arena_free(&arena);
    free(src);
    return same;
}

TEST_CASE(number_errors)
{
    for (size_t i = 0; i < sizeof NUMBER_ERRORS / sizeof NUMBER_ERRORS[0]; i++) {
        REQUIRE(number_fails_with(
            NUMBER_ERRORS[i].src,
            NUMBER_ERRORS[i].error,
            NUMBER_ERRORS[i].pos
        ));
    }
    return true;
}

TEST_CASE(builtin_names)
{
    const char *text = "void error tail count";
    size_t len = strlen(text);
    char *src = calloc(len + LEXER_SOURCE_PADDING, 1);
    memcpy(src, text, len);

    Arena arena = { 0 };
    Lexer lx;
    lexer_init(&lx, &arena, len);
    Tokens t = tokens_new(len);
    int n = lexer_tokenize(&lx, src, t);

    bool same = n == 5 && t.kinds[0] == TK_IDENT && t.kinds[1] == TK_IDENT &&
                t.kinds[2] == TK_IDENT && t.ids[0] == NAME_VOID &&
                t.ids[1] == NAME_ERROR && t.ids[2] == NAME_TAIL &&
                t.ids[3] == NFIXED_NAMES + 1;

    tokens_free(t);
    arena_free(&arena);
    free(src);
    REQUIRE(same);
    return true;
}

TEST_CASE(name_hashes)
{
    const char *text = "x Name_2 abcdefghijklmnop abcdefghijklmnopxy "
                       "a_name_longer_than_sixteen_bytes";
    size_t len = strlen(text);
    char *src = calloc(len + LEXER_SOURCE_PADDING, 1);
    memcpy(src, text, len);

    Arena arena = { 0 };
    Lexer lx;
    lexer_init(&lx, &arena, len);
    Tokens t = tokens_new(len);
    int n = lexer_tokenize(&lx, src, t);

    bool same = n == 6 && lx.names.count == NFIXED_NAMES + 6;
    for (u32 id = 1; same && id < lx.names.count; id++) {
        Name name = names_get(&lx.names, id);
        u8 buf[16] = { 0 };
        memcpy(buf, name.text, name.len < 16 ? name.len : 16);

        u64 k0, k1;
        memcpy(&k0, buf, 8);
        memcpy(&k1, buf + 8, 8);

        const NameSlot *slot = &lx.names.slots[name.slot];
        same = slot->id == id &&
               slot->hash == names_hash(name.text, name.len, k0, k1);
    }

    tokens_free(t);
    arena_free(&arena);
    free(src);
    REQUIRE(same);
    return true;
}

#define PIECE_MAX_LEN 64
#define KEYWORD_PIECE(name, text) text,

// clang-format off
static const char *const PIECES[] = {
    CHIBI_TOKENS(CHIBI_SKIP, KEYWORD_PIECE, CHIBI_SKIP, CHIBI_SKIP, CHIBI_SKIP)
    "x", "_", "name", "Name_2", "abcdefg", "abcdefgh", "abcdefghx",
    "abcdefghijklmno", "abcdefghijklmnop", "abcdefghijklmnopxy", "a_name_longer_than_sixteen_bytes",
    "0", "7", "42", "3.14", "1.", "1e5", "1E9", "2e+8", "6e-3", "1e",
    "0x1F", "0xff_ff", "0b1010", "0o755", "1_000", "1_000.5", "3.141_592", "1e1_0", "07", "1_", "0xG",
    "\"\"", "\"text\"", "\"\\\" \\\\ \\/ \\b \\f \\n \\r \\t\"", "\"\\u00e9\\uABCD\"", "\"caf\xc3\xa9\"",
    "\"// not a comment\"", "// comment\n", "//\n", "// \"quote\" \\ backslash\n",
    "<<", ">>", "&&", "||", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "->", "=>", "==",
    "!=", "<=", ">=", "..", "..<", "..=", "::", ":=", "+", "-", "*", "/", "%", "=", "!", "@", "$", "?", "&",
    "+%", "-%", "*%", "+%=", "-%=", "*%=", "!!", "??", "?\?=",
    "|", "^", "~", "<", ">", ":", ";", ".", ",", "{", "}", "(", ")", "[", "]", "#", "'", "\\",
    "`", "\x7f", "\xc3\xa9",
    " ", " ", " ", " ", "  ", "        ", "\t", "\n", "\n", "\r\n",
};
// clang-format on

static size_t
random_program(u64 *rng, char *buf, int count)
{
    size_t len = 0;
    for (int i = 0; i < count; i++) {
        char name[16];
        const char *piece = PIECES[random_below(rng, sizeof PIECES / sizeof PIECES[0])];
        if (random_below(rng, 8) == 0) {
            snprintf(name, sizeof name, "v%d", random_below(rng, 100000));
            piece = name;
        }

        size_t n = strlen(piece);
        assert(n <= PIECE_MAX_LEN);
        memcpy(buf + len, piece, n);
        len += n;

        bool number = piece[0] >= '0' && piece[0] <= '9';
        if (number && random_below(rng, 20) != 0) {
            buf[len++] = ' ';
        }
    }

    return len;
}

enum
{
    MAX_PIECES = 400,
    MAX_LONG_PIECES = 4000,
};

TEST_CASE(random_programs)
{
    enum
    {
        NPROGRAMS = 5000
    };

    char **programs = calloc(NPROGRAMS, sizeof *programs);

    Arena arena = { 0 };
    Lexer lx;
    lexer_init(&lx, &arena, 0);
    RefLexer *rl = ref_lexer_new();

    u64 rng = 1;
    bool same = true;
    size_t cap = MAX_LONG_PIECES * PIECE_MAX_LEN;
    char *buf = malloc(cap);

    for (int i = 0; i < NPROGRAMS && same; i++) {
        int count = random_below(&rng, 50) == 0 ? random_below(&rng, MAX_LONG_PIECES)
                                                : random_below(&rng, MAX_PIECES);
        size_t len = random_program(&rng, buf, count);
        programs[i] = calloc(len + LEXER_SOURCE_PADDING, 1);
        memcpy(programs[i], buf, len);

        char label[32];
        snprintf(label, sizeof label, "random program #%d", i);
        same = matches_reference(label, programs[i], len, &lx, rl);
        if (lx.error != NULL) {
            lexer_init(&lx, &arena, 0);
            ref_lexer_free(rl);
            rl = ref_lexer_new();
        }
    }

    free(buf);
    ref_lexer_free(rl);
    arena_free(&arena);
    for (int i = 0; i < NPROGRAMS; i++) {
        free(programs[i]);
    }
    free(programs);

    REQUIRE(same);
    return true;
}

static void
print_first_difference(const char *got, size_t got_len, const char *want, size_t want_len)
{
    size_t i = 0;
    while (i < got_len && i < want_len && got[i] == want[i]) {
        i++;
    }

    size_t line_start = i;
    while (line_start > 0 && want[line_start - 1] != '\n') {
        line_start--;
    }

    int line = 1;
    for (size_t k = 0; k < line_start; k++) {
        line += want[k] == '\n';
    }

    fprintf(
        stderr,
        "test/hello.txt line %d\n  got:  %.*s\n  want: %.*s\n",
        line,
        (int)strcspn(got + line_start, "\n"),
        got + line_start,
        (int)strcspn(want + line_start, "\n"),
        want + line_start
    );
}

TEST_CASE(golden_output)
{
    Sample src = sample_read("test/hello.chibi");
    Sample want = sample_read("test/hello.txt");

    Arena arena = { 0 };
    Lexer lx;
    lexer_init(&lx, &arena, src.len);
    Tokens tokens = tokens_new(src.len);
    int n = lexer_tokenize(&lx, src.text, tokens);

    char *got;
    size_t got_len;
    FILE *out = open_memstream(&got, &got_len);
    lexer_dump(out, src.text, tokens, n - 1);
    fclose(out);

    bool same = got_len == want.len && memcmp(got, want.text, got_len) == 0;
    if (!same) {
        print_first_difference(got, got_len, want.text, want.len);
    }

    free(got);
    tokens_free(tokens);
    arena_free(&arena);
    sample_free(src);
    sample_free(want);

    REQUIRE(same);
    return true;
}

TEST_SUITE(lexer_tests)
{
    RUN_TEST_CASE(edge_cases);
    RUN_TEST_CASE(number_errors);
    RUN_TEST_CASE(builtin_names);
    RUN_TEST_CASE(name_hashes);
    RUN_TEST_CASE(random_programs);
    RUN_TEST_CASE(golden_output);
}
