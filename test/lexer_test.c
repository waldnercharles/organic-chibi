#define _DEFAULT_SOURCE

#include "../src/arena.h"
#include "../vendor/pico_unit/pico_unit.h"
#include "programs.h"
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
            RefToken a = n < nfast ? fast_token(src, fast, n)
                                   : (RefToken){ TK_OP, UINT32_MAX, UINT32_MAX, 0, false };
            RefToken b = ref[n];
            if (a.kind != b.kind || a.start != b.start ||
                a.length != b.length || a.id != b.id || a.starts_line != b.starts_line) {
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
    "\"abc\"def 12.5.6 1e5e5 12..34 a.b.c",
    "/=== /==> a/==b //=== x/=y ==/= 1/=2",
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
        size_t len = random_program(&rng, buf, all_pieces, count);
        programs[i] = calloc(len + LEXER_SOURCE_PADDING, 1);
        memcpy(programs[i], buf, len);

        char label[32];
        snprintf(label, sizeof label, "random program #%d", i);
        same = matches_reference(label, programs[i], len, &lx, rl);
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
    RUN_TEST_CASE(random_programs);
    RUN_TEST_CASE(golden_output);
}
