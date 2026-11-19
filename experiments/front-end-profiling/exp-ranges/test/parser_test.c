#define _DEFAULT_SOURCE

#include "../src/parser.h"
#include "../vendor/pico_unit/pico_unit.h"
#include "programs.h"

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
    int ntokens;
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
    r.ntokens = lexer_tokenize(&lx, r.src, r.tokens);
    assert(r.ntokens > 0);

    r.nodes = parser_alloc_nodes(arena, r.ntokens);
    r.count = parser_parse(&r.parser, r.tokens, r.ntokens, r.nodes);
    return r;
}

static bool
tokens_used_at_most_twice(const char *text, Parse r)
{
    int n = r.count >= 0 ? r.count : r.parser.count;
    u8 *uses = calloc((size_t)r.ntokens, 1);
    bool ok = true;
    for (int i = 0; i < n; i++) {
        ok = ok && ++uses[r.nodes.tokens[i]] <= 2;
    }
    free(uses);

    if (!ok) {
        fprintf(stderr, "parse \"%s\"\n  a token is used by more than 2 nodes\n", text);
    }
    return ok;
}

static bool
parses_to(const char *text, const char *want)
{
    Arena arena = { 0 };
    Parse r = parse(&arena, text);

    char *got = NULL;
    size_t got_len = 0;
    FILE *out = open_memstream(&got, &got_len);
    if (r.count >= 0) {
        parser_dump(out, &arena, r.src, r.tokens, r.nodes, r.count);
    } else {
        fprintf(out, "error at byte %u: %s\n", r.parser.error_pos, r.parser.error);
    }
    fclose(out);

    if (got_len > 0 && got[got_len - 1] == '\n') {
        got_len--;
    }

    size_t want_len = strlen(want);
    bool same = got_len == want_len && memcmp(got, want, want_len) == 0 &&
                tokens_used_at_most_twice(text, r);
    if (!same) {
        fprintf(stderr, "parse \"%s\"\n  got:  %.*s\n  want: %s\n", text, (int)got_len, got, want);
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

    bool same = r.count < 0 && strcmp(r.parser.error, error) == 0 &&
                r.parser.error_pos == pos && tokens_used_at_most_twice(text, r);
    if (!same) {
        fprintf(
            stderr,
            "parse \"%s\"\n  got:  %s at byte %u\n  want: %s at byte %u\n",
            text,
            r.count < 0 ? r.parser.error : "no error",
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
    { "!a and b", "(and (! a) b)" },
    { "~a & b", "(& (~ a) b)" },
    { "a % b / c", "(/ (% a b) c)" },
    { "a << b + c", "(+ (<< a b) c)" },
    { "a & b == c", "(== (& a b) c)" },
    { "a | b ^ c", "(^ (| a b) c)" },
    { "a >> b <= c", "(<= (>> a b) c)" },
    { "a == b and c != d", "(and (== a b) (!= c d))" },
    { "a or b and c", "(or a (and b c))" },
    { "a and b or c", "(or (and a b) c)" },
    { "a < b or c >= d and e", "(or (< a b) (and (>= c d) e))" },
    { "(a < b) == c", "(== (< a b) c)" },
    { "a < (b < c)", "(< a (< b c))" },
    { "a +\n  b", "(+ a b)" },
    { "f()", "(call f)" },
    { "f(a)", "(call f a)" },
    { "f(a, b)", "(call f a b)" },
    { "f(a, b,)", "(call f a b)" },
    { "f(\n  a,\n  b,\n)", "(call f a b)" },
    { "f(a + b, c * d)", "(call f (+ a b) (* c d))" },
    { "f(g(x))", "(call f (call g x))" },
    { "f(a)(b)", "(call (call f a) b)" },
    { "f()()", "(call (call f))" },
    { "(f)(x)", "(call f x)" },
    { "a[i]", "(index a i)" },
    { "a[i + 1]", "(index a (+ i 1))" },
    { "a[i][j]", "(index (index a i) j)" },
    { "a[f(i)]", "(index a (call f i))" },
    { "a.b", "(field a b)" },
    { "a.b.c", "(field (field a b) c)" },
    { "a.b(c)[d].e", "(field (index (call (field a b) c) d) e)" },
    { "(a + b).c", "(field (+ a b) c)" },
    { "\"s\".len", "(field \"s\" len)" },
    { "-a.b", "(- (field a b))" },
    { "-f(x)", "(- (call f x))" },
    { "!a[i]", "(! (index a i))" },
    { "a * f(b) + c", "(+ (* a (call f b)) c)" },
    { "&x", "(& x)" },
    { "&a.b", "(& (field a b))" },
    { "p.*", "(deref p)" },
    { "p.*.x", "(field (deref p) x)" },
    { "x.type", "(field x type)" },
    { "", "" },
    { "a\nb", "a\nb" },
    { "a; b", "a\nb" },
    { ";; a;; b;", "a\nb" },
    { "a\n- b", "(- a b)" },
    { "a\n* b", "(* a b)" },
    { "a\nand b", "(and a b)" },
    { "a or\nb", "(or a b)" },
    { "a\n.b", "(field a b)" },
    { "a\n!b", "a\n(! b)" },
    { "f\n(x)", "f\nx" },
    { "f(a\n(b))", "(call f (call a b))" },
    { "a[i\n[j]]", "(index a (index i j))" },
    { "x := 1", "(:= x 1)" },
    { "x: int", "(: x int)" },
    { "x: int = 1", "(: x int 1)" },
    { "x: int\n= 1", "(: x int 1)" },
    { "x: int\ny := 2", "(: x int)\n(:= y 2)" },
    { "a = b", "(= a b)" },
    { "a.b[i] += 1", "(+= (index (field a b) i) 1)" },
    { "a <<= 2", "(<<= a 2)" },
    { "a >>= b & c", "(>>= a (& b c))" },
    { "p.* = q.*", "(= (deref p) (deref q))" },
    { "x = 1\n+ 2", "(= x (+ 1 2))" },
    { "{}", "(block)" },
    { "{ a; b }", "(block a b)" },
    { "{\n  a\n  { b }\n}", "(block a (block b))" },
    { "if a { b }", "(if a (block b))" },
    { "if (a) {}", "(if a (block))" },
    { "if a { b } else { c }", "(if a (block b) (block c))" },
    { "if a {\n} \nelse {\n}", "(if a (block) (block))" },
    { "if a {} else if b {} else {}", "(if a (block) b (block) (block))" },
    { "if a {} else if b {}", "(if a (block) b (block))" },
    { "if a and b or c {}", "(if (or (and a b) c) (block))" },
    { "while a < b { a += 1 }", "(while (< a b) (block (+= a 1)))" },
    { "while a { break; continue }", "(while a (block break continue))" },
    { "return", "return" },
    { "return a + b", "(return (+ a b))" },
    { "return\nx", "return\nx" },
    { "{ return }", "(block return)" },
    { "defer f()", "(defer (call f))" },
    { "defer { a; b }", "(defer (block a b))" },
    { "defer x = 1; y", "(defer (= x 1))\ny" },
    { "fn f() {}", "(fn f (block))" },
    { "fn add(a: int, b: int) -> int { return a + b }",
      "(fn add (: a int) (: b int) (-> int) (block (return (+ a b))))" },
    { "fn f(a, b) {}", "(fn f a b (block))" },
    { "fn f(\n  a,\n  b,\n) {}", "(fn f a b (block))" },
    { "fn f(a: int = 1, b := 2) {}", "(fn f (: a int 1) (:= b 2) (block))" },
    { "fn square(x: int) -> int => x * x", "(fn square (: x int) (-> int) (return (* x x)))" },
    { "fn f()\n{\n}", "(fn f (block))" },
    { "fn f() { fn g() {} }", "(fn f (block (fn g (block))))" },
    { "f := fn(x) => x * 2", "(:= f (fn-literal x (return (* x 2))))" },
    { "g(fn(x: int) -> int { return x })", "(call g (fn-literal (: x int) (-> int) (block (return x))))" },
    { "g(fn() {\n  a\n  (b)\n})", "(call g (fn-literal (block a b)))" },
    { "g(fn() => a\n  (b))", "(call g (fn-literal (return (call a b))))" },
    { "const X := 10", "(const X 10)" },
    { "const X: int = 10", "(const X int 10)" },
    { "f(.a = 1)", "(call f (arg a 1))" },
    { "f(x, .b = 2, .c = y + 1)", "(call f x (arg b 2) (arg c (+ y 1)))" },
    { "f(\n  .a = 1,\n  .b = 2,\n)", "(call f (arg a 1) (arg b 2))" },
    { "f(.a = fn(x) => x)", "(call f (arg a (fn-literal x (return x))))" },
};

static const struct
{
    const char *src;
    const char *error;
    u32 pos;
} ERRORS[] = {
    { "a +", "expected an expression", 3 },
    { "a + * b", "expected an expression", 4 },
    { "* a", "expected an expression", 0 },
    { "()", "expected an expression", 1 },
    { "(a", "expected `)`", 2 },
    { "a)", "expected `;` or a new line", 1 },
    { "a b", "expected `;` or a new line", 2 },
    { "a < b < c", "comparisons cannot be chained; add parentheses", 6 },
    { "a == b != c", "comparisons cannot be chained; add parentheses", 7 },
    { "a < b + c > d", "comparisons cannot be chained; add parentheses", 10 },
    { "f(", "expected an expression", 2 },
    { "f(a", "expected `,` or `)`", 3 },
    { "f(a b)", "expected `,` or `)`", 4 },
    { "f(,)", "expected an expression", 2 },
    { "f(a,,)", "expected an expression", 4 },
    { "f(a))", "expected `;` or a new line", 4 },
    { "a[", "expected an expression", 2 },
    { "a[]", "expected an expression", 2 },
    { "a[i", "expected `]`", 3 },
    { "a[i, j]", "expected `]`", 3 },
    { "a.", "expected a field name after `.`", 2 },
    { "a.(b)", "expected a field name after `.`", 2 },
    { "a.+", "expected a field name after `.`", 2 },
    { "a && b", "use `and` instead of `&&`", 2 },
    { "a || b", "use `or` instead of `||`", 2 },
    { "(a && b)", "use `and` instead of `&&`", 3 },
    { "a = b = c", "assignments cannot be chained", 6 },
    { "a := b := c", "expected `;` or a new line", 7 },
    { "x: 1", "expected a type", 3 },
    { "x :=", "expected an expression", 4 },
    { "a\n[i]", "expected an expression", 2 },
    { "{ a", "expected `}`", 3 },
    { "a }", "unmatched `}`", 2 },
    { "}", "unmatched `}`", 0 },
    { "if a b", "expected `{`", 5 },
    { "if a { b", "expected `}`", 8 },
    { "if a {} b", "expected `;` or a new line", 8 },
    { "if a {} else b", "expected `{`", 13 },
    { "if a {} else if {}", "expected an expression", 16 },
    { "while a", "expected `{`", 7 },
    { "return a b", "expected `;` or a new line", 9 },
    { "else {}", "expected an expression", 0 },
    { "fn f {}", "expected `(`", 5 },
    { "fn f(1) {}", "expected a parameter name", 5 },
    { "fn f(a b) {}", "expected `,` or `)`", 7 },
    { "fn f()", "expected `{` or `=>`", 6 },
    { "fn f() int {}", "expected `{` or `=>`", 7 },
    { "fn f() -> {}", "expected a type", 10 },
    { "fn f() => {}", "expected an expression", 10 },
    { "fn(", "expected a parameter name", 3 },
    { "const X = 1", "expected `:=` or `:`", 8 },
    { "const X: int", "expected `=`", 12 },
    { "const 1 := 2", "expected a name", 6 },
    { "f(.a)", "expected an expression", 2 },
    { "f(.a = )", "expected an expression", 7 },
    { "f(.", "expected an expression", 2 },
};
// clang-format on

#define NODE_KIND_NAME(name, ...) [name] = #name,
static const char *const node_kind_names[NNODES] = {
    CHIBI_NODES(NODE_KIND_NAME, NODE_KIND_NAME, NODE_KIND_NAME, NODE_KIND_NAME, NODE_KIND_NAME, NODE_KIND_NAME, NODE_KIND_NAME)
};

static bool
postorder_is(const char *text, const char *want)
{
    Arena arena = { 0 };
    Parse r = parse(&arena, text);

    char got[512] = "";
    size_t got_len = 0;
    for (int i = 0; i < r.count; i++) {
        const char *name = node_kind_names[r.nodes.kinds[i]] + strlen("NODE_");
        got_len += (size_t)
            snprintf(got + got_len, sizeof got - got_len, "%s%s", i ? " " : "", name);
    }

    bool same = r.count >= 0 && strcmp(got, want) == 0 &&
                tokens_used_at_most_twice(text, r);
    if (!same) {
        fprintf(stderr, "parse \"%s\"\n  got:  %s\n  want: %s\n", text, got, want);
    }

    arena_free(&arena);
    return same;
}

// clang-format off
static const struct
{
    const char *src;
    const char *want;
} POSTORDERS[] = {
    { "a and b", "NAME AND_LEFT NAME AND EXPR_STMT" },
    { "a or b and c", "NAME OR_LEFT NAME AND_LEFT NAME AND OR EXPR_STMT" },
    { "f(x)", "NAME CALL_ARGS NAME CALL EXPR_STMT" },
    { "{ a }", "BLOCK_START NAME EXPR_STMT BLOCK" },
    { "if a {}", "IF_START NAME IF_THEN BLOCK_START BLOCK IF" },
    { "if a {} else if b {} else {}",
      "IF_START NAME IF_THEN BLOCK_START BLOCK ELSE_IF NAME IF_THEN BLOCK_START BLOCK ELSE BLOCK_START BLOCK IF" },
    { "while a {}", "WHILE_START NAME WHILE_BODY BLOCK_START BLOCK WHILE" },
    { "defer f()", "DEFER_START NAME CALL_ARGS CALL EXPR_STMT DEFER" },
    { "x := 1", "INT VAR_INIT" },
    { "x: T", "NAME VAR_TYPE" },
    { "x: T = 1", "NAME VALUE INT VAR_TYPE_INIT" },
    { "a.b = c", "NAME FIELD VALUE NAME ASSIGN" },
    { "fn f(a, b: T = 1) -> R {}",
      "FN_START PARAM NAME VALUE INT PARAM_TYPE_INIT NAME FN_RETURN FN_BODY BLOCK_START BLOCK FN" },
    { "fn(x) => x", "FN_START PARAM FN_BODY NAME RETURN_VALUE FN_LITERAL EXPR_STMT" },
    { "f(.a = 1)", "NAME CALL_ARGS ARG_NAME VALUE INT NAMED_ARG CALL EXPR_STMT" },
    { "const X := 1", "INT CONST_INIT" },
    { "const X: T = 1", "NAME VALUE INT CONST_TYPE_INIT" },
    { "return a", "NAME RETURN_VALUE" },
};
// clang-format on

TEST_CASE(postorders)
{
    for (size_t i = 0; i < sizeof POSTORDERS / sizeof POSTORDERS[0]; i++) {
        REQUIRE(postorder_is(POSTORDERS[i].src, POSTORDERS[i].want));
    }
    return true;
}

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
depth_ok(const char *open, const char *close, u32 at)
{
    char *deepest = nested(open, PARSER_MAX_DEPTH, close);
    char *too_deep = nested(open, PARSER_MAX_DEPTH + 1, close);

    Arena arena = { 0 };
    bool ok = parse(&arena, deepest).count >= 0;
    u32 failing_byte = PARSER_MAX_DEPTH * (u32)strlen(open) + at;
    ok = ok && fails_with(too_deep, "nested too deeply", failing_byte);

    arena_free(&arena);
    free(deepest);
    free(too_deep);
    return ok;
}

TEST_CASE(depth_limit)
{
    REQUIRE(depth_ok("(", ")", 0));
    REQUIRE(depth_ok("-", "", 0));
    REQUIRE(depth_ok("!", "", 0));
    REQUIRE(depth_ok("f(", ")", 1));
    REQUIRE(depth_ok("a[", "]", 1));
    REQUIRE(depth_ok("{", "}", 0));
    REQUIRE(depth_ok("defer ", "", 0));
    REQUIRE(depth_ok("if a {", "}", 5));
    REQUIRE(depth_ok("fn() => ", "", 0));
    return true;
}

TEST_CASE(long_chains)
{
    char *chain = nested("", PARSER_MAX_DEPTH + 1, ".b(c)[d]");

    Arena arena = { 0 };
    bool ok = parse(&arena, chain).count >= 0;

    arena_free(&arena);
    free(chain);
    REQUIRE(ok);
    return true;
}

TEST_CASE(long_else_if)
{
    const char *link = " else if a {}";
    size_t link_len = strlen(link);
    int nlinks = 10 * PARSER_MAX_DEPTH;

    char *chain = malloc(strlen("if a {}") + link_len * (size_t)nlinks + 1);
    char *p = stpcpy(chain, "if a {}");
    for (int i = 0; i < nlinks; i++) {
        p = stpcpy(p, link);
    }

    Arena arena = { 0 };
    bool ok = parse(&arena, chain).count >= 0;

    arena_free(&arena);
    free(chain);
    REQUIRE(ok);
    return true;
}

TEST_CASE(generated_code)
{
    enum
    {
        NPROGRAMS = 200,
        PROGRAM_SIZE = 8192,
    };

    Arena arena = { 0 };
    u64 rng = 1;
    Vocabulary v;
    vocabulary_init(&rng, &v, &arena);
    char *text = arena_alloc(&arena, PROGRAM_SIZE + CODE_SLACK + 1, 1);

    bool ok = true;
    for (int i = 0; i < NPROGRAMS && ok; i++) {
        size_t len = random_code(&rng, text, &v, PROGRAM_SIZE);
        text[len] = '\0';

        ArenaMark mark = arena_mark(&arena);
        Parse r = parse(&arena, text);
        ok = r.count >= 0;
        if (!ok) {
            fprintf(stderr, "generated program #%d\n", i);
            parser_print_error(&r.parser, r.src);
        }
        arena_restore(&arena, mark);
    }

    arena_free(&arena);
    REQUIRE(ok);
    return true;
}

TEST_SUITE(parser_tests)
{
    RUN_TEST_CASE(trees);
    RUN_TEST_CASE(postorders);
    RUN_TEST_CASE(errors);
    RUN_TEST_CASE(depth_limit);
    RUN_TEST_CASE(long_chains);
    RUN_TEST_CASE(long_else_if);
    RUN_TEST_CASE(generated_code);
}
