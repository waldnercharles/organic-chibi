#define _DEFAULT_SOURCE

#include "../src/parser.h"
#include "../vendor/pico_unit/pico_unit.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    char *src;
    Tokens tokens;
    Nodes nodes;
    Parser parser;
    int count;
} Parse;

static Parse
parse(Arena *arena, const char *text)
{
    size_t len = strlen(text);
    Parse r = { .src = arena_alloc(arena, len + LEXER_SOURCE_PADDING, 16) };
    memset(r.src, 0, len + LEXER_SOURCE_PADDING);
    memcpy(r.src, text, len);

    Lexer lx;
    lexer_init(&lx, arena, len);
    r.tokens = lexer_alloc_tokens(arena, len);
    int ntokens = lexer_tokenize(&lx, r.src, r.tokens);
    assert(ntokens > 0);

    r.nodes = parser_alloc_nodes(arena, ntokens);
    r.count = parser_parse(&r.parser, r.tokens, ntokens, r.nodes);
    return r;
}

static bool
parses_to(const char *text, const char *want)
{
    Arena arena = { 0 };
    Parse r = parse(&arena, text);

    char *got = NULL;
    size_t got_len = 0;
    FILE *out = open_memstream(&got, &got_len);
    if (r.count > 0) {
        parser_dump(out, &arena, r.src, r.tokens, r.nodes, r.count);
    } else {
        fprintf(out, "error at byte %u: %s\n", r.parser.error_pos, r.parser.error);
    }
    fclose(out);

    size_t want_len = strlen(want);
    bool same = got_len == want_len + 1 && memcmp(got, want, want_len) == 0;
    if (!same) {
        fprintf(stderr, "parse \"%s\"\n  got:  %s  want: %s\n", text, got, want);
    }

    free(got);
    arena_free(&arena);
    return same;
}

static bool
fails_with(const char *text, const char *error, u32 pos)
{
    Arena arena = { 0 };
    Parse r = parse(&arena, text);

    bool same = r.count == 0 && strcmp(r.parser.error, error) == 0 &&
                r.parser.error_pos == pos;
    if (!same) {
        fprintf(
            stderr,
            "parse \"%s\"\n  got:  %s at byte %u\n  want: %s at byte %u\n",
            text,
            r.count == 0 ? r.parser.error : "no error",
            r.parser.error_pos,
            error,
            pos
        );
    }

    arena_free(&arena);
    return same;
}

// clang-format off
static const struct
{
    const char *src;
    const char *want;
} TREES[] = {
    { "a", "a" },
    { "42", "42" },
    { "1.5", "1.5" },
    { "\"hi\"", "\"hi\"" },
    { "true", "true" },
    { "false", "false" },
    { "nil", "nil" },
    { "a + b", "(+ a b)" },
    { "a + b + c", "(+ (+ a b) c)" },
    { "a - b - c", "(- (- a b) c)" },
    { "a + b * c", "(+ a (* b c))" },
    { "a * b + c", "(+ (* a b) c)" },
    { "(a + b) * c", "(* (+ a b) c)" },
    { "a * (b + c)", "(* a (+ b c))" },
    { "((a))", "a" },
    { "-a", "(- a)" },
    { "- -a", "(- (- a))" },
    { "-a * b", "(* (- a) b)" },
    { "a - -b", "(- a (- b))" },
    { "!a && b", "(&& (! a) b)" },
    { "~a & b", "(& (~ a) b)" },
    { "a % b / c", "(/ (% a b) c)" },
    { "a << b + c", "(+ (<< a b) c)" },
    { "a & b == c", "(== (& a b) c)" },
    { "a | b ^ c", "(^ (| a b) c)" },
    { "a >> b <= c", "(<= (>> a b) c)" },
    { "a == b && c != d", "(&& (== a b) (!= c d))" },
    { "a || b && c", "(|| a (&& b c))" },
    { "a && b || c", "(|| (&& a b) c)" },
    { "a < b || c >= d && e", "(|| (< a b) (&& (>= c d) e))" },
    { "(a < b) == c", "(== (< a b) c)" },
    { "a < (b < c)", "(< a (< b c))" },
    { "a +\n  b", "(+ a b)" },
};

static const struct
{
    const char *src;
    const char *error;
    u32 pos;
} ERRORS[] = {
    { "", "expected an expression", 0 },
    { "a +", "expected an expression", 3 },
    { "a + * b", "expected an expression", 4 },
    { "* a", "expected an expression", 0 },
    { "()", "expected an expression", 1 },
    { "(a", "expected `)`", 2 },
    { "a)", "expected an operator or the end of the input", 1 },
    { "a b", "expected an operator or the end of the input", 2 },
    { "a < b < c", "comparisons cannot be chained; add parentheses", 6 },
    { "a == b != c", "comparisons cannot be chained; add parentheses", 7 },
    { "a < b + c > d", "comparisons cannot be chained; add parentheses", 10 },
};
// clang-format on

TEST_CASE(trees)
{
    for (size_t i = 0; i < sizeof TREES / sizeof TREES[0]; i++) {
        REQUIRE(parses_to(TREES[i].src, TREES[i].want));
    }
    return true;
}

TEST_CASE(errors)
{
    for (size_t i = 0; i < sizeof ERRORS / sizeof ERRORS[0]; i++) {
        REQUIRE(fails_with(ERRORS[i].src, ERRORS[i].error, ERRORS[i].pos));
    }
    return true;
}

static char *
nested(const char *open, int depth, const char *close)
{
    size_t open_len = strlen(open), close_len = strlen(close);
    char *s = malloc((open_len + close_len) * (size_t)depth + 2);
    char *p = s;
    for (int i = 0; i < depth; i++, p += open_len) {
        memcpy(p, open, open_len);
    }
    *p++ = 'x';
    for (int i = 0; i < depth; i++, p += close_len) {
        memcpy(p, close, close_len);
    }
    *p = '\0';
    return s;
}

static bool
depth_ok(const char *open, const char *close)
{
    char *deepest = nested(open, PARSER_MAX_DEPTH, close);
    char *too_deep = nested(open, PARSER_MAX_DEPTH + 1, close);

    Arena arena = { 0 };
    bool ok = parse(&arena, deepest).count > 0;
    ok = ok &&
         fails_with(too_deep, "expression nested too deeply", PARSER_MAX_DEPTH * (u32)strlen(open));

    arena_free(&arena);
    free(deepest);
    free(too_deep);
    return ok;
}

TEST_CASE(depth_limit)
{
    REQUIRE(depth_ok("(", ")"));
    REQUIRE(depth_ok("-", ""));
    REQUIRE(depth_ok("!", ""));
    return true;
}

TEST_SUITE(parser_tests)
{
    RUN_TEST_CASE(trees);
    RUN_TEST_CASE(errors);
    RUN_TEST_CASE(depth_limit);
}
