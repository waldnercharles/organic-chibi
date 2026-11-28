#define _DEFAULT_SOURCE

#include "../src/jit.h"
#include "../vendor/pico_unit/pico_unit.h"
#include "backend_programs.h"
#include "ir_reference.h"
#include "random/code.h"
#include "random/program.h"
#include "vectors.h"

#include <assert.h>
#include <math.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__aarch64__)

typedef struct
{
    Arena arena;
    Lexer lx;
    ParsedFile file;
    Checker c;
    Native n;
} Program;

static bool
check_text(Program *p, const char *text, bool release_fast, bool entry)
{
    *p = (Program){ 0 };
    size_t len = strlen(text);
    lexer_init(&p->lx, &p->arena, len);
    char *src = arena_alloc(&p->arena, len + LEXER_SOURCE_PADDING, 16);
    memset(src, 0, len + LEXER_SOURCE_PADDING);
    memcpy(src, text, len);

    Tokens tokens = lexer_alloc_tokens(&p->arena, len);
    int ntokens = lexer_tokenize(&p->lx, src, tokens);
    Nodes nodes = parser_alloc_nodes(&p->arena, ntokens);
    Parser parser;
    int count = parser_parse(&parser, tokens, ntokens, nodes);
    if (count < 0) {
        fprintf(stderr, "arm64 test: the program does not parse\n%s", text);
        return false;
    }

    p->file = (ParsedFile){ "a",   src,        tokens,
                            nodes, (u32)count, (u32)parser.top_count,
                            entry };
    check_init(&p->c, &p->lx.names, &p->file, 1);
    p->c.release_fast = release_fast;
    check_package(&p->c);
    if (p->c.ndiags) {
        check_print_diagnostics(stderr, &p->c);
        return false;
    }
    return true;
}

static bool
build(Program *p, const char *text)
{
    if (!check_text(p, text, false, false)) {
        return false;
    }
    if (!jit_compile(&p->n, &p->c, CONVENTION_HOST, 0)) {
        fprintf(stderr, "arm64 test: %s\n", p->n.error);
        return false;
    }
    return true;
}

static u32
decl_named(Program *p, const char *name)
{
    u32 before = p->lx.names.count;
    u32 id = names_intern(&p->lx.names, name, (u32)strlen(name));
    u32 decl = id <= before ? p->c.package_names[id] : 0;
    if (!decl) {
        fprintf(stderr, "arm64 test: no function `%s`\n", name);
        abort();
    }
    return decl;
}

static void *
fn(Program *p, const char *name)
{
    return native_entry(&p->n, decl_named(p, name));
}

static uint8_t CONTEXT[16];
static u32 lost_contexts;

static void
see_context(const void *context)
{
    lost_contexts += context != CONTEXT;
}

static void
release_program(Program *p)
{
    native_free(&p->n);
    check_free(&p->c);
    arena_free(&p->arena);
}

static sigjmp_buf trap_jump;
static volatile sig_atomic_t trap_kind;

static void
on_trap(int sig, siginfo_t *info, void *context)
{
    (void)sig;
    (void)info;
    ucontext_t *uc = context;
#ifdef __APPLE__
    u64 pc = uc->uc_mcontext->__ss.__pc;
#else
    u64 pc = uc->uc_mcontext.pc;
#endif
    trap_kind = (sig_atomic_t)((*(const u32 *)(uintptr_t)pc >> 5) & 0xFFFF);
    siglongjmp(trap_jump, 1);
}

static void
catch_traps(void)
{
    struct sigaction sa = { 0 };
    sa.sa_sigaction = on_trap;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTRAP, &sa, NULL);
}

typedef int64_t I2(void *, int64_t, int64_t);

static int
call_i2(I2 *f, int64_t a, int64_t b, int64_t *result)
{
    trap_kind = 0;
    if (sigsetjmp(trap_jump, 1) == 0) {
        *result = f(CONTEXT, a, b);
        return 0;
    }
    return trap_kind;
}

typedef enum
{
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_WRAP_MUL_ADD,
    OP_DIV,
    OP_REM,
    OP_SHL,
    OP_SHR,
    OP_NEG,
} Op;

static int
reference(Op op, int bits, bool is_signed, int64_t a, int64_t b, int64_t *out)
{
    int64_t lo = INT64_MIN, hi = INT64_MAX;
    if (bits < 64) {
        lo = is_signed ? -(int64_t)(1ull << (bits - 1)) : 0;
        hi = is_signed ? (int64_t)((1ull << (bits - 1)) - 1)
                       : (int64_t)((1ull << bits) - 1);
    }
    __int128 r = 0;
    switch (op) {
        case OP_ADD: r = (__int128)a + b; break;
        case OP_SUB: r = (__int128)a - b; break;
        case OP_MUL: r = (__int128)a * b; break;
        case OP_WRAP_MUL_ADD: {
            uint64_t mask = bits == 64 ? ~0ull : (1ull << bits) - 1;
            uint64_t v = (((uint64_t)a * (uint64_t)b) + (uint64_t)a) & mask;
            if (is_signed && bits < 64 && (v >> (bits - 1)) & 1) {
                v |= ~mask;
            }
            *out = (int64_t)v;
            return 0;
        }
        case OP_DIV:
        case OP_REM:
            if (b == 0) {
                return TRAP_DIVIDE;
            }
            if (is_signed && a == lo && b == -1) {
                if (op == OP_DIV) {
                    return TRAP_OVERFLOW;
                }
                *out = 0;
                return 0;
            }
            r = op == OP_DIV ? (__int128)a / b : (__int128)a % b;
            break;
        case OP_SHL:
        case OP_SHR: {
            if ((uint64_t)b >= (uint64_t)bits) {
                return TRAP_SHIFT;
            }
            uint64_t mask = bits == 64 ? ~0ull : (1ull << bits) - 1;
            uint64_t v = (uint64_t)a & mask;
            if (op == OP_SHL) {
                v = (v << b) & mask;
                if (is_signed && bits < 64 && (v >> (bits - 1)) & 1) {
                    v |= ~mask;
                }
                *out = (int64_t)v;
            } else {
                *out = is_signed ? a >> b : (int64_t)(v >> b);
            }
            return 0;
        }
        case OP_NEG: r = -(__int128)a; break;
    }
    if (r < lo || r > hi) {
        return TRAP_OVERFLOW;
    }
    *out = (int64_t)r;
    return 0;
}

static bool
agrees(Program *p, const char *name, Op op, int bits, bool is_signed, int64_t a, int64_t b)
{
    int64_t want = 0, got = 0;
    int want_trap = reference(op, bits, is_signed, a, b, &want);
    int got_trap = call_i2((I2 *)fn(p, name), a, b, &got);
    if (want_trap != got_trap || (!want_trap && want != got)) {
        fprintf(
            stderr,
            "%s(%lld, %lld): want %lld (trap %d), got %lld (trap %d)\n",
            name,
            (long long)a,
            (long long)b,
            (long long)want,
            want_trap,
            (long long)got,
            got_trap
        );
        return false;
    }
    return true;
}

static const int64_t EDGES_64[] = { 0,
                                    1,
                                    -1,
                                    2,
                                    -2,
                                    3,
                                    63,
                                    64,
                                    100,
                                    -100,
                                    INT32_MAX,
                                    INT32_MIN,
                                    (int64_t)INT32_MAX + 1,
                                    (int64_t)INT32_MIN - 1,
                                    UINT32_MAX,
                                    INT64_MAX,
                                    INT64_MIN,
                                    INT64_MAX - 1,
                                    INT64_MIN + 1,
                                    0x123456789ABCDEFll,
                                    -0x123456789ABCDEFll,
                                    3037000499ll,
                                    3037000500ll,
                                    -3037000500ll };

TEST_CASE(small_integers)
{
    Program p;
    REQUIRE(build(&p, ARITHMETIC));
    catch_traps();
    static const struct
    {
        const char *name;
        Op op;
        int bits;
        bool is_signed;
    } ops[] = {
        { "add_i8", OP_ADD, 8, true },
        { "sub_u8", OP_SUB, 8, false },
        { "mul_i8", OP_MUL, 8, true },
        { "wrap_u8", OP_WRAP_MUL_ADD, 8, false },
        { "div_i8", OP_DIV, 8, true },
        { "rem_i8", OP_REM, 8, true },
        { "shl_u8", OP_SHL, 8, false },
        { "shr_i8", OP_SHR, 8, true },
        { "neg_i8", OP_NEG, 8, true },
    };
    bool ok = true;
    for (size_t k = 0; k < sizeof ops / sizeof ops[0] && ok; k++) {
        int64_t lo = ops[k].is_signed ? -128 : 0;
        for (int64_t a = lo; a < lo + 256 && ok; a++) {
            for (int64_t b = lo; b < lo + 256 && ok; b += (a & 7) == 0 ? 1 : 37) {
                int64_t bb = ops[k].op == OP_NEG ? 0 : b;
                ok = agrees(&p, ops[k].name, ops[k].op, ops[k].bits, ops[k].is_signed, a, bb);
            }
        }
    }
    release_program(&p);
    REQUIRE(ok);
    return true;
}

TEST_CASE(wide_integers)
{
    Program p;
    REQUIRE(build(&p, ARITHMETIC));
    catch_traps();
    static const struct
    {
        const char *name;
        Op op;
        int bits;
        bool is_signed;
    } ops[] = {
        { "add_i32", OP_ADD, 32, true }, { "sub_u32", OP_SUB, 32, false },
        { "mul_i32", OP_MUL, 32, true }, { "mul_u32", OP_MUL, 32, false },
        { "div_i32", OP_DIV, 32, true }, { "add_i64", OP_ADD, 64, true },
        { "sub_i64", OP_SUB, 64, true }, { "mul_i64", OP_MUL, 64, true },
        { "div_i64", OP_DIV, 64, true }, { "rem_i64", OP_REM, 64, true },
        { "shl_i64", OP_SHL, 64, true }, { "shr_i64", OP_SHR, 64, true },
        { "neg_i64", OP_NEG, 64, true },
    };
    size_t n = sizeof EDGES_64 / sizeof EDGES_64[0];
    bool ok = true;
    for (size_t k = 0; k < sizeof ops / sizeof ops[0] && ok; k++) {
        int bits = ops[k].bits;
        bool s = ops[k].is_signed;
        int64_t lo = s ? (bits == 64 ? INT64_MIN : -(int64_t)(1ull << (bits - 1))) : 0;
        int64_t hi = bits == 64 ? INT64_MAX
                   : s          ? (int64_t)((1ull << (bits - 1)) - 1)
                                : (int64_t)((1ull << bits) - 1);
        for (size_t i = 0; i < n && ok; i++) {
            for (size_t j = 0; j < n && ok; j++) {
                int64_t a = EDGES_64[i], b = ops[k].op == OP_NEG ? 0 : EDGES_64[j];
                if (a < lo || a > hi || b < lo || b > hi) {
                    continue;
                }
                ok = agrees(&p, ops[k].name, ops[k].op, bits, s, a, b);
            }
        }
    }

    int64_t got = 0;
    ok = ok && call_i2((I2 *)fn(&p, "wrap_i64"), INT64_MAX, 3, &got) == 0 &&
         got == (int64_t)((uint64_t)INT64_MAX * 3 - (uint64_t)INT64_MAX);
    ok = ok && call_i2((I2 *)fn(&p, "to_u8"), 255, 0, &got) == 0 && got == 255;
    ok = ok && call_i2((I2 *)fn(&p, "to_u8"), 256, 0, &got) == TRAP_RANGE;
    ok = ok && call_i2((I2 *)fn(&p, "to_u8"), -1, 0, &got) == TRAP_RANGE;
    ok = ok && call_i2((I2 *)fn(&p, "mask_u8"), 0x1FF, 0, &got) == 0 && got == 255;
    ok = ok && call_i2((I2 *)fn(&p, "mul_u64"), 3, 5, &got) == 0 && got == 15;
    release_program(&p);
    REQUIRE(ok);
    return true;
}

typedef int64_t I1(void *, int64_t);
typedef int64_t I0(void *);
typedef bool B1(void *, int64_t);
typedef bool B3(void *, bool, bool, bool);
typedef int64_t I4(void *, int64_t, int64_t, int64_t, int64_t);
typedef double D4(void *, int64_t, double, int32_t, float);
typedef double D0(void *);
typedef double DFD(void *, float, double);
typedef int64_t ID(void *, double);
typedef int64_t IF(void *, float);

static int64_t
deep_reference(int64_t a, int64_t b, int64_t c, int64_t d)
{
    return a * b +
           (c * d +
            (a * c + (b * d + (a * d + (b * c + (a * a + (b * b + (c * c + d * d))))))));
}

static int
call_id(ID *f, double x, int64_t *result)
{
    trap_kind = 0;
    if (sigsetjmp(trap_jump, 1) == 0) {
        *result = f(CONTEXT, x);
        return 0;
    }
    return trap_kind;
}

static int
call_if(IF *f, float x, int64_t *result)
{
    trap_kind = 0;
    if (sigsetjmp(trap_jump, 1) == 0) {
        *result = f(CONTEXT, x);
        return 0;
    }
    return trap_kind;
}

TEST_CASE(control_flow)
{
    Program p;
    REQUIRE(build(&p, CONTROL));
    catch_traps();
    REQUIRE(((I1 *)fn(&p, "sum"))(CONTEXT, 100) == 4950);
    REQUIRE(((I1 *)fn(&p, "sum"))(CONTEXT, 0) == 0);
    REQUIRE(((I1 *)fn(&p, "collatz"))(CONTEXT, 27) == 111);
    REQUIRE(((I1 *)fn(&p, "fib"))(CONTEXT, 20) == 6765);
    REQUIRE(((B1 *)fn(&p, "is_even"))(CONTEXT, 10));
    REQUIRE(!((B1 *)fn(&p, "is_even"))(CONTEXT, 7));
    REQUIRE(((I0 *)fn(&p, "bytes"))(CONTEXT) == 256);
    REQUIRE(((I1 *)fn(&p, "skips"))(CONTEXT, 10) == 1 + 2 + 4 + 5 + 7 + 8 + 10);

    I2 *logic = (I2 *)fn(&p, "logic");
    I2 *count_flags = (I2 *)fn(&p, "count_flags");
    for (int64_t a = -2; a <= 2; a++) {
        for (int64_t b = -2; b <= 2; b++) {
            REQUIRE(logic(CONTEXT, a, b) == ((a > 0 && b > 0) || a == b));
            bool pp = a > b, q = a < b || a == 0;
            REQUIRE(count_flags(CONTEXT, a, b) == (pp || (q && !pp)) + 10 * a);
        }
    }
    B3 *flags = (B3 *)fn(&p, "flags");
    for (int m = 0; m < 8; m++) {
        bool a = m & 1, b = m & 2, c = m & 4;
        REQUIRE(flags(CONTEXT, a, b, c) == (a && (b || c)));
    }

    I4 *deep = (I4 *)fn(&p, "deep");
    REQUIRE(deep(CONTEXT, 3, -5, 7, 11) == deep_reference(3, -5, 7, 11));
    REQUIRE(deep(CONTEXT, -1, 2, -3, 4) == deep_reference(-1, 2, -3, 4));
    REQUIRE(((I1 *)fn(&p, "live"))(CONTEXT, 4) == 4 + 55 * (4 + 5));

    REQUIRE(((D4 *)fn(&p, "mix"))(CONTEXT, 1, 2.5, 3, 4.5f) == 11.0);
    REQUIRE(((D0 *)fn(&p, "use_mix"))(CONTEXT) == 22.0);
    REQUIRE(((I1 *)fn(&p, "use_scale"))(CONTEXT, 3) == 36);
    REQUIRE(((I0 *)fn(&p, "use_many"))(CONTEXT) == 1 + 4 + 9 + 16 + 25 + 36 + 49);

    DFD *floats = (DFD *)fn(&p, "floats");
    REQUIRE(floats(CONTEXT, 5.0f, 4.0) == 2.5 * 4.0 - 0.25);
    REQUIRE(floats(CONTEXT, 2.0f, 4.0) == -4.0);
    REQUIRE(floats(CONTEXT, 5.0f, NAN) == -NAN || isnan(floats(CONTEXT, 5.0f, NAN)));

    int64_t got = 0;
    REQUIRE(call_id((ID *)fn(&p, "to_int"), -2.75, &got) == 0 && got == -2);
    REQUIRE(call_id((ID *)fn(&p, "to_int"), 9.2e18, &got) == 0 && got == 9200000000000000000ll);
    REQUIRE(call_id((ID *)fn(&p, "to_int"), 9.3e18, &got) == TRAP_RANGE);
    REQUIRE(call_id((ID *)fn(&p, "to_int"), -0x1p63, &got) == 0 && got == INT64_MIN);
    REQUIRE(call_id((ID *)fn(&p, "to_int"), NAN, &got) == TRAP_RANGE);
    REQUIRE(call_id((ID *)fn(&p, "to_int"), INFINITY, &got) == TRAP_RANGE);
    REQUIRE(call_if((IF *)fn(&p, "to_u8f"), 255.9f, &got) == 0 && got == 255);
    REQUIRE(call_if((IF *)fn(&p, "to_u8f"), -0.9f, &got) == 0 && got == 0);
    REQUIRE(call_if((IF *)fn(&p, "to_u8f"), 256.0f, &got) == TRAP_RANGE);
    REQUIRE(call_if((IF *)fn(&p, "to_u8f"), -1.0f, &got) == TRAP_RANGE);
    REQUIRE(call_id((ID *)fn(&p, "to_i32f"), -2147483648.9, &got) == 0 && got == INT32_MIN);
    REQUIRE(call_id((ID *)fn(&p, "to_i32f"), -2147483649.0, &got) == TRAP_RANGE);
    REQUIRE(call_id((ID *)fn(&p, "to_i32f"), 2147483647.9, &got) == 0 && got == INT32_MAX);

    I0 *call_one = (I0 *)fn(&p, "call_one");
    REQUIRE(call_one(CONTEXT) == 11);
    native_replace(&p.n, decl_named(&p, "one"), fn(&p, "two"));
    REQUIRE(call_one(CONTEXT) == 12);

    release_program(&p);
    return true;
}

static bool
rejects(const char *text)
{
    Program p = { 0 };
    size_t len = strlen(text);
    lexer_init(&p.lx, &p.arena, len);
    char *src = arena_alloc(&p.arena, len + LEXER_SOURCE_PADDING, 16);
    memset(src, 0, len + LEXER_SOURCE_PADDING);
    memcpy(src, text, len);
    Tokens tokens = lexer_alloc_tokens(&p.arena, len);
    int ntokens = lexer_tokenize(&p.lx, src, tokens);
    Nodes nodes = parser_alloc_nodes(&p.arena, ntokens);
    Parser parser;
    int count = parser_parse(&parser, tokens, ntokens, nodes);
    p.file = (ParsedFile){ "a",   src,        tokens,
                           nodes, (u32)count, (u32)parser.top_count,
                           true };
    check_init(&p.c, &p.lx.names, &p.file, 1);
    check_package(&p.c);
    bool ok = count >= 0 && p.c.ndiags == 0 && !jit_compile(&p.n, &p.c, CONVENTION_HOST, 0) &&
              p.n.error && strstr(p.n.error, "supports only") != NULL;
    if (!ok) {
        fprintf(stderr, "arm64 test: expected the backend to reject\n%s", text);
    }
    release_program(&p);
    return ok;
}

TEST_CASE(unsupported_types)
{
    REQUIRE(rejects("enum Kind: u8 { circle, none }\nunion Shape: Kind { circle: f32, none }\nfn c(s: Shape) -> int {\n    return 0\n}\n"));
    return true;
}

static const char *
large_frame(char *text, size_t cap)
{
    size_t len = (size_t)snprintf(text, cap, "fn big(a: int, b: int) -> int {\n    total := b\n");
    for (u32 k = 0; k < 600; k++) {
        len += (size_t)snprintf(text + len, cap - len, "    v%u := a + %u\n    total +%%= v%u\n", k, k, k);
    }
    snprintf(text + len, cap - len, "    p := &v599\n    p.* += 1\n    return v599 + total\n}\n");
    return text;
}

TEST_CASE(pointers)
{
    Program p;
    REQUIRE(build(&p, POINTERS));
    int64_t r = 0;
    REQUIRE(((I2 *)fn(&p, "run"))(CONTEXT, 1, 1) == 14);
    REQUIRE(((I2 *)fn(&p, "run"))(CONTEXT, 7, -3) == 18);
    REQUIRE(call_i2((I2 *)fn(&p, "over"), 5, 6, &r) == 0 && r == 11);
    REQUIRE(call_i2((I2 *)fn(&p, "over"), INT64_MAX, 1, &r) == TRAP_OVERFLOW);
    REQUIRE(((I2 *)fn(&p, "reload"))(CONTEXT, 4, 1) == 20);
    REQUIRE(((I2 *)fn(&p, "reload"))(CONTEXT, -2, 0) == -8);
    REQUIRE(((I2 *)fn(&p, "alias"))(CONTEXT, 3, 1) == 24);
    REQUIRE(((I2 *)fn(&p, "alias"))(CONTEXT, -5, 2) == -18);
    REQUIRE(((I2 *)fn(&p, "narrow"))(CONTEXT, 1, 2) == 2001015);
    REQUIRE(((I2 *)fn(&p, "narrow"))(CONTEXT, 300, 70000) == 4464001744);
    REQUIRE(((I2 *)fn(&p, "narrow"))(CONTEXT, 200, 0) == 637);
    REQUIRE(((I2 *)fn(&p, "choose"))(CONTEXT, 1, 2) == 101002);
    REQUIRE(((I2 *)fn(&p, "choose"))(CONTEXT, 5, 3) == 5103);
    REQUIRE(((I2 *)fn(&p, "same"))(CONTEXT, 1, 2) == 11);
    REQUIRE(((I2 *)fn(&p, "same"))(CONTEXT, 4, 4) == 11);
    REQUIRE(((I2 *)fn(&p, "accumulate"))(CONTEXT, 10, 0) == 45);
    REQUIRE(((I2 *)fn(&p, "accumulate"))(CONTEXT, 0, 7) == 7);
    release_program(&p);

    char *text = malloc(64 * 1024);
    REQUIRE(build(&p, large_frame(text, 64 * 1024)));
    free(text);
    REQUIRE(((I2 *)fn(&p, "big"))(CONTEXT, 1, 0) == 180901);
    REQUIRE(((I2 *)fn(&p, "big"))(CONTEXT, -7, 3) == 176096);
    release_program(&p);
    return true;
}

TEST_CASE(arrays)
{
    catch_traps();
    Program p;
    REQUIRE(build(&p, ARRAYS));
    int64_t r = 0;
    REQUIRE(((I2 *)fn(&p, "sum"))(CONTEXT, 1, 2) == 110);
    REQUIRE(((I2 *)fn(&p, "sum"))(CONTEXT, -5, 7) == 109);
    REQUIRE(call_i2((I2 *)fn(&p, "lit"), 0, 0, &r) == 0 && r == 13);
    REQUIRE(call_i2((I2 *)fn(&p, "lit"), 2, 1, &r) == 0 && r == 37);
    REQUIRE(call_i2((I2 *)fn(&p, "lit"), 3, 0, &r) == TRAP_BOUNDS);
    REQUIRE(call_i2((I2 *)fn(&p, "lit"), -1, 0, &r) == TRAP_BOUNDS);
    REQUIRE(call_i2((I2 *)fn(&p, "lit"), 0, 2, &r) == TRAP_BOUNDS);
    REQUIRE(call_i2((I2 *)fn(&p, "via"), 5, 0, &r) == 0 && r == 10007);
    REQUIRE(call_i2((I2 *)fn(&p, "via"), 5, 1, &r) == 0 && r == 15005);
    REQUIRE(call_i2((I2 *)fn(&p, "via"), 5, 2, &r) == 0 && r == 18005);
    REQUIRE(call_i2((I2 *)fn(&p, "via"), 5, 3, &r) == TRAP_BOUNDS);
    REQUIRE(call_i2((I2 *)fn(&p, "via"), 1, 255, &r) == TRAP_BOUNDS);
    REQUIRE(((I2 *)fn(&p, "tails"))(CONTEXT, 1, 2) == 100006498);
    REQUIRE(((I2 *)fn(&p, "tails"))(CONTEXT, 200, -1) == 42997040);
    REQUIRE(((I2 *)fn(&p, "rezero"))(CONTEXT, 7, 3) == 0);
    REQUIRE(((I2 *)fn(&p, "rezero"))(CONTEXT, -1, 255) == 0);
    REQUIRE(((I2 *)fn(&p, "big"))(CONTEXT, 1, 2) == 5003);
    REQUIRE(((I2 *)fn(&p, "big"))(CONTEXT, -3, 9) == 5006);
    REQUIRE(call_i2((I2 *)fn(&p, "odd"), 4, 1, &r) == 0 && r == 103);
    REQUIRE(call_i2((I2 *)fn(&p, "odd"), 0, 0, &r) == 0 && r == 62);
    REQUIRE(call_i2((I2 *)fn(&p, "odd"), 7, 0, &r) == TRAP_BOUNDS);
    REQUIRE(call_i2((I2 *)fn(&p, "odd"), 2, 3, &r) == TRAP_BOUNDS);
    REQUIRE(call_i2((I2 *)fn(&p, "rows"), 0, 0, &r) == 0 && r == 807);
    REQUIRE(call_i2((I2 *)fn(&p, "rows"), 1, 1, &r) == 0 && r == 2607);
    REQUIRE(call_i2((I2 *)fn(&p, "rows"), 2, 0, &r) == TRAP_BOUNDS);
    REQUIRE(call_i2((I2 *)fn(&p, "kinds"), 1, 2, &r) == 0 && r == 112);
    REQUIRE(call_i2((I2 *)fn(&p, "kinds"), 2, 0, &r) == 0 && r == 208);
    REQUIRE(call_i2((I2 *)fn(&p, "kinds"), 0, -1, &r) == TRAP_BOUNDS);
    REQUIRE(((I2 *)fn(&p, "swap_rows"))(CONTEXT, 1, 2) == 21);
    REQUIRE(((I2 *)fn(&p, "swap_rows"))(CONTEXT, 5, -3) == -25);
    release_program(&p);
    return true;
}

typedef struct
{
    uint8_t a;
    int64_t b;
    uint16_t c;
    float d;
    bool e;
    int16_t f[3];
    uint8_t g;
} Mixed;

typedef int64_t MI(void *, Mixed *, int64_t);
typedef int64_t M3(void *, Mixed *);

TEST_CASE(structs)
{
    catch_traps();
    Program p;
    REQUIRE(build(&p, STRUCTS));
    int64_t r = 0;

    Mixed ms[3];
    memset(ms, 0xAB, sizeof ms);
    REQUIRE(((MI *)fn(&p, "fill"))(CONTEXT, &ms[0], 5) == 8);
    REQUIRE(ms[0].a == 5 && ms[0].b == 5000 && ms[0].c == 6 && ms[0].d == 2.5f);
    REQUIRE(ms[0].e && ms[0].f[0] == 5 && ms[0].f[2] == 4 && ms[0].g == 3);
    REQUIRE(((MI *)fn(&p, "fill"))(CONTEXT, &ms[2], -3) == 256);
    REQUIRE(ms[2].a == 253 && ms[2].b == -3000 && ms[2].c == 32766 && ms[2].d == -1.5f);
    REQUIRE(!ms[2].e && ms[2].f[0] == -3 && ms[2].f[2] == -4 && ms[2].g == 3);
    memset(&ms[1], 0, sizeof ms[1]);
    ms[0].f[1] = 0;
    ms[2].f[1] = 0;
    ms[1].d = 2.5f;
    ms[1].f[1] = 9;
    ms[1].e = true;
    ms[1].g = 1;
    REQUIRE(((M3 *)fn(&p, "read"))(CONTEXT, ms) == 1035049);

    REQUIRE(((I2 *)fn(&p, "lines"))(CONTEXT, 3, 4) == 50804154);
    REQUIRE(((I2 *)fn(&p, "lines"))(CONTEXT, -2, 9) == 50809204);
    REQUIRE(((I2 *)fn(&p, "defaults"))(CONTEXT, 11, 200) == 36);
    REQUIRE(((I2 *)fn(&p, "via"))(CONTEXT, 1, 2) == 8008);
    REQUIRE(((I2 *)fn(&p, "via"))(CONTEXT, -50, 6) == -35035);
    REQUIRE(((I2 *)fn(&p, "many"))(CONTEXT, 7, -3) == 99977);
    REQUIRE(((I2 *)fn(&p, "many"))(CONTEXT, 64, 1000) == 110064);
    REQUIRE(call_i2((I2 *)fn(&p, "poke"), 2, -7, &r) == 0 && r == -7);
    REQUIRE(call_i2((I2 *)fn(&p, "poke"), 1, 5, &r) == 0 && r == 0);
    REQUIRE(call_i2((I2 *)fn(&p, "poke"), 3, 5, &r) == TRAP_BOUNDS);
    release_program(&p);
    return true;
}

typedef struct
{
    const void *ptr;
    int64_t len;
} Slice;

typedef int64_t SI(void *, Slice);
typedef int64_t SU8(void *, Slice, uint8_t);
typedef Slice SS(void *, Slice);
typedef float SF(void *, Slice);
typedef int64_t ISIS(void *, int64_t, Slice, int64_t, Slice);
typedef Slice IIS(void *, int64_t, int64_t, Slice);

static bool
agree_on_grid(Program *p, const char *const *names, size_t count)
{
    IrReference ref;
    ir_reference_init(&ref, &p->c);
    bool ok = true;
    for (size_t f = 0; f < count && ok; f++) {
        u32 decl = decl_named(p, names[f]);
        I2 *native = (I2 *)native_entry(&p->n, decl);
        for (int64_t a = -3; a <= 14 && ok; a++) {
            for (int64_t b = -3; b <= 12 && ok; b++) {
                int64_t x = a < 14 ? a : INT64_MIN;
                u64 slots[2] = { (u64)x, (u64)b };
                u64 want = 0;
                int64_t got = 0;
                int want_trap = ir_reference_run(&ref, decl, slots, &want);
                int got_trap = call_i2(native, x, b, &got);
                if (want_trap != got_trap || (!want_trap && (int64_t)want != got)) {
                    fprintf(
                        stderr,
                        "arm64 test: %s(%lld, %lld): reference %lld (trap %d), "
                        "native %lld (trap %d)\n",
                        names[f],
                        (long long)x,
                        (long long)b,
                        (long long)want,
                        want_trap,
                        (long long)got,
                        got_trap
                    );
                    ok = false;
                }
            }
        }
    }
    ir_reference_free(&ref);
    return ok;
}

static const char *const SLICE_FUNCTIONS[] = {
    "kernels", "window", "ends",   "nested", "known", "sizes",  "rows",
    "pointed", "empty",  "swap",   "picks",  "mixer", "scan",   "literal",
    "narrow",  "tails",  "floats", "many",   "chain", "rebase",
};

TEST_CASE(slices)
{
    catch_traps();
    Program p;
    REQUIRE(build(&p, SLICES));
    int64_t r = 0;
    REQUIRE(call_i2((I2 *)fn(&p, "kernels"), 32, 1, &r) == 0 && r == 1079);
    REQUIRE(call_i2((I2 *)fn(&p, "kernels"), 7, 0, &r) == 0 && r == 75);
    REQUIRE(call_i2((I2 *)fn(&p, "window"), 2, 5, &r) == 0 && r == 2903);
    REQUIRE(call_i2((I2 *)fn(&p, "window"), 0, 10, &r) == 0 && r == 28510);
    REQUIRE(call_i2((I2 *)fn(&p, "window"), 10, 10, &r) == 0 && r == 0);
    REQUIRE(call_i2((I2 *)fn(&p, "window"), 4, 3, &r) == TRAP_BOUNDS);
    REQUIRE(call_i2((I2 *)fn(&p, "window"), 0, 11, &r) == TRAP_BOUNDS);
    REQUIRE(call_i2((I2 *)fn(&p, "window"), -1, 2, &r) == TRAP_BOUNDS);
    REQUIRE(call_i2((I2 *)fn(&p, "ends"), 2, 5, &r) == 0 && r == 4080524);
    REQUIRE(call_i2((I2 *)fn(&p, "ends"), 0, 9, &r) == 0 && r == 10100965);
    REQUIRE(call_i2((I2 *)fn(&p, "ends"), 3, 2, &r) == 0 && r == 70203);
    REQUIRE(call_i2((I2 *)fn(&p, "ends"), 0, 10, &r) == TRAP_BOUNDS);
    REQUIRE(call_i2((I2 *)fn(&p, "ends"), 1, INT64_MAX, &r) == TRAP_OVERFLOW);
    REQUIRE(call_i2((I2 *)fn(&p, "nested"), 2, 3, &r) == 0 && r == 3036);
    REQUIRE(call_i2((I2 *)fn(&p, "nested"), 0, 8, &r) == 0 && r == 8108);
    REQUIRE(call_i2((I2 *)fn(&p, "nested"), 8, 0, &r) == 0 && r == 0);
    REQUIRE(call_i2((I2 *)fn(&p, "nested"), 9, 0, &r) == TRAP_BOUNDS);
    REQUIRE(call_i2((I2 *)fn(&p, "nested"), 2, 7, &r) == TRAP_BOUNDS);
    REQUIRE(call_i2((I2 *)fn(&p, "known"), 0, 1, &r) == 0 && r == 4100);
    REQUIRE(call_i2((I2 *)fn(&p, "known"), 1, 2, &r) == 0 && r == 4150);
    REQUIRE(call_i2((I2 *)fn(&p, "known"), 2, 1, &r) == TRAP_BOUNDS);
    REQUIRE(call_i2((I2 *)fn(&p, "known"), -1, 1, &r) == TRAP_BOUNDS);
    REQUIRE(call_i2((I2 *)fn(&p, "sizes"), 0, 0, &r) == 0 && r == 2042);
    REQUIRE(call_i2((I2 *)fn(&p, "sizes"), 2, 1, &r) == 0 && r == 923);
    REQUIRE(call_i2((I2 *)fn(&p, "sizes"), 1, 3, &r) == 0 && r == 3436);
    REQUIRE(call_i2((I2 *)fn(&p, "sizes"), 4, 0, &r) == TRAP_BOUNDS);
    REQUIRE(call_i2((I2 *)fn(&p, "rows"), 1, 2, &r) == 0 && r == 32111);
    REQUIRE(call_i2((I2 *)fn(&p, "rows"), 0, 0, &r) == 0 && r == 2401);
    REQUIRE(call_i2((I2 *)fn(&p, "rows"), 2, 2, &r) == TRAP_BOUNDS);
    REQUIRE(call_i2((I2 *)fn(&p, "pointed"), 1, 0, &r) == 0 && r == 3107);
    REQUIRE(call_i2((I2 *)fn(&p, "pointed"), 0, 5, &r) == 0 && r == 4113);
    REQUIRE(call_i2((I2 *)fn(&p, "pointed"), 4, 0, &r) == TRAP_BOUNDS);
    REQUIRE(call_i2((I2 *)fn(&p, "empty"), 0, 7, &r) == 0 && r == 7);
    REQUIRE(call_i2((I2 *)fn(&p, "empty"), 1, 0, &r) == TRAP_BOUNDS);
    REQUIRE(call_i2((I2 *)fn(&p, "swap"), 2, 0, &r) == 0 && r == 304);
    REQUIRE(call_i2((I2 *)fn(&p, "swap"), 2, 3, &r) == 0 && r == 1802);
    REQUIRE(call_i2((I2 *)fn(&p, "picks"), 2, 1, &r) == 0 && r == 31);
    REQUIRE(call_i2((I2 *)fn(&p, "picks"), 1, 2, &r) == 0 && r == 903);
    REQUIRE(call_i2((I2 *)fn(&p, "picks"), 5, 2, &r) == TRAP_BOUNDS);
    REQUIRE(call_i2((I2 *)fn(&p, "mixer"), 2, 3, &r) == 0 && r == 30239);
    REQUIRE(call_i2((I2 *)fn(&p, "mixer"), 1, 0, &r) == TRAP_BOUNDS);
    REQUIRE(call_i2((I2 *)fn(&p, "scan"), 0, 9, &r) == 0 && r == 34);
    REQUIRE(call_i2((I2 *)fn(&p, "scan"), 2, 100, &r) == 0 && r == 53);
    REQUIRE(call_i2((I2 *)fn(&p, "literal"), 1, 7, &r) == 0 && r == 13);
    REQUIRE(call_i2((I2 *)fn(&p, "narrow"), 1, 2, &r) == 0 && r == 4070);
    REQUIRE(call_i2((I2 *)fn(&p, "narrow"), -1, 0, &r) == TRAP_BOUNDS);
    REQUIRE(call_i2((I2 *)fn(&p, "tails"), 4, 0, &r) == 0 && r == 93);
    REQUIRE(call_i2((I2 *)fn(&p, "tails"), 0, 0, &r) == TRAP_BOUNDS);
    REQUIRE(call_i2((I2 *)fn(&p, "floats"), 0, 4, &r) == 0 && r == 15);
    REQUIRE(call_i2((I2 *)fn(&p, "floats"), 2, 1, &r) == TRAP_BOUNDS);
    REQUIRE(call_i2((I2 *)fn(&p, "many"), 0, 1, &r) == 0 && r == 409348);
    REQUIRE(call_i2((I2 *)fn(&p, "many"), 5, 1, &r) == 0 && r == 404708);
    REQUIRE(call_i2((I2 *)fn(&p, "many"), 6, 1, &r) == TRAP_BOUNDS);
    REQUIRE(call_i2((I2 *)fn(&p, "chain"), 1, 2, &r) == 0 && r == 355);
    REQUIRE(call_i2((I2 *)fn(&p, "chain"), 5, -1, &r) == 0 && r == 452);
    REQUIRE(call_i2((I2 *)fn(&p, "rebase"), 2, 0, &r) == 0 && r == 304);
    REQUIRE(call_i2((I2 *)fn(&p, "rebase"), 3, 1, &r) == 0 && r == 502);
    REQUIRE(call_i2((I2 *)fn(&p, "rebase"), 0, 0, &r) == 0 && r == 106);

    int64_t xs[3] = { 4, 5, 6 };
    float fs[3] = { 1.5f, 2.0f, 4.0f };
    REQUIRE(((SI *)fn(&p, "sum"))(CONTEXT, (Slice){ xs, 3 }) == 15);
    REQUIRE(((SU8 *)fn(&p, "count"))(CONTEXT, (Slice){ "a,b,,c", 6 }, ',') == 3);
    Slice t = ((SS *)fn(&p, "tail"))(CONTEXT, (Slice){ xs, 3 });
    REQUIRE(t.ptr == xs + 1 && t.len == 2);
    REQUIRE(((SF *)fn(&p, "fsum"))(CONTEXT, (Slice){ fs, 3 }) == 7.5f);
    REQUIRE(
        ((ISIS *)fn(&p, "mix"))(CONTEXT, 2, (Slice){ xs, 3 }, 3, (Slice){ "xyz", 2 }) == 20671
    );
    t = ((SS *)fn(&p, "skip2"))(CONTEXT, (Slice){ xs, 3 });
    REQUIRE(t.ptr == xs + 2 && t.len == 1);

    int64_t ws[8] = { 0, 10, 20, 30, 40, 50, 60, 70 };
    int64_t vs[8] = { 0, 10, 12, 14, 16, 16, 0, 0 };
    int64_t us[8] = { 0, 40, 0, 0, 0, 0, 0, 0 };
    IIS *swapped = (IIS *)fn(&p, "swapped");
    IIS *lone = (IIS *)fn(&p, "lone");
    t = swapped(CONTEXT, 0, 0, (Slice){ ws, 8 });
    REQUIRE(t.ptr == ws + 1 && t.len == 7);
    t = swapped(CONTEXT, 0, 0, (Slice){ vs, 8 });
    REQUIRE(t.ptr == vs + 2 && t.len == 6);
    t = lone(CONTEXT, 0, 0, (Slice){ ws, 8 });
    REQUIRE(t.ptr == ws + 1 && t.len == 7);
    t = lone(CONTEXT, 0, 0, (Slice){ us, 8 });
    REQUIRE(t.ptr == us + 2 && t.len == 6);

    REQUIRE(agree_on_grid(&p, SLICE_FUNCTIONS, sizeof SLICE_FUNCTIONS / sizeof SLICE_FUNCTIONS[0])
    );
    release_program(&p);
    return true;
}

typedef struct
{
    Slice data;
    int64_t n;
    uint8_t tag;
} Text;

typedef struct
{
    Slice rows;
    int64_t w;
} Grid;

typedef int64_t TI(void *, Text *);
typedef int64_t GI(void *, Grid *);
typedef Slice GS(void *, Grid *, int64_t);
typedef void GP(void *, Grid *, int64_t, Slice);

static const char *const STORED_SLICE_FUNCTIONS[] = {
    "spans", "table", "deep", "walk", "order", "chain",
};

TEST_CASE(stored_slices)
{
    catch_traps();
    Program p;
    REQUIRE(build(&p, STORED_SLICES));
    int64_t r = 0;
    REQUIRE(call_i2((I2 *)fn(&p, "spans"), 1, 2, &r) == 0 && r == 522207);
    REQUIRE(call_i2((I2 *)fn(&p, "spans"), 6, 0, &r) == 0 && r == 7);
    REQUIRE(call_i2((I2 *)fn(&p, "spans"), 7, 0, &r) == TRAP_BOUNDS);
    REQUIRE(call_i2((I2 *)fn(&p, "spans"), 1, 6, &r) == TRAP_BOUNDS);
    REQUIRE(call_i2((I2 *)fn(&p, "table"), 1, 2, &r) == 0 && r == 10402);
    REQUIRE(call_i2((I2 *)fn(&p, "table"), 4, 3, &r) == 0 && r == 10310);
    REQUIRE(call_i2((I2 *)fn(&p, "table"), 1, 4, &r) == TRAP_BOUNDS);
    REQUIRE(call_i2((I2 *)fn(&p, "table"), 0, -1, &r) == TRAP_BOUNDS);
    REQUIRE(call_i2((I2 *)fn(&p, "deep"), 1, 2, &r) == 0 && r == 314);
    REQUIRE(call_i2((I2 *)fn(&p, "deep"), 0, 0, &r) == 0 && r == 401);
    REQUIRE(call_i2((I2 *)fn(&p, "deep"), 4, 0, &r) == TRAP_BOUNDS);
    REQUIRE(call_i2((I2 *)fn(&p, "deep"), 5, 0, &r) == TRAP_BOUNDS);
    REQUIRE(call_i2((I2 *)fn(&p, "walk"), 2, 7, &r) == 0 && r == 430);
    REQUIRE(call_i2((I2 *)fn(&p, "walk"), 0, 0, &r) == 0 && r == 505);
    REQUIRE(call_i2((I2 *)fn(&p, "walk"), 5, 1, &r) == 0 && r == 123451);
    REQUIRE(call_i2((I2 *)fn(&p, "walk"), -2, 0, &r) == 0 && r == 505);
    REQUIRE(call_i2((I2 *)fn(&p, "walk"), 6, 0, &r) == TRAP_BOUNDS);
    REQUIRE(call_i2((I2 *)fn(&p, "order"), 2, 1, &r) == 0 && r == 5234);
    REQUIRE(call_i2((I2 *)fn(&p, "order"), 0, 4, &r) == 0 && r == 5055);
    REQUIRE(call_i2((I2 *)fn(&p, "order"), 5, 0, &r) == TRAP_BOUNDS);
    REQUIRE(call_i2((I2 *)fn(&p, "order"), 6, 0, &r) == TRAP_BOUNDS);
    REQUIRE(call_i2((I2 *)fn(&p, "chain"), 3, 4, &r) == 0 && r == 11034);
    REQUIRE(call_i2((I2 *)fn(&p, "chain"), -5, 0, &r) == 0 && r == 10950);

    char text[] = "x,y,,z";
    Text t = { { text, 6 }, -1, 0 };
    REQUIRE(((TI *)fn(&p, "measure"))(CONTEXT, &t) == 305);
    REQUIRE(t.n == 3 && t.tag == 'x' && t.data.ptr == text + 1 && t.data.len == 5);

    int64_t r0[2] = { 1, 2 };
    int64_t r1[3] = { 3, 4, 5 };
    Slice rows[2] = { { r0, 2 }, { r1, 3 } };
    Grid g = { { rows, 2 }, 0 };
    REQUIRE(((GI *)fn(&p, "total"))(CONTEXT, &g) == 27 && g.w == 2);
    Slice row = ((GS *)fn(&p, "get"))(CONTEXT, &g, 1);
    REQUIRE(row.ptr == r1 && row.len == 3);
    ((GP *)fn(&p, "put"))(CONTEXT, &g, 0, (Slice){ r1 + 1, 2 });
    REQUIRE(rows[0].ptr == r1 + 1 && rows[0].len == 2);
    REQUIRE(((GI *)fn(&p, "total"))(CONTEXT, &g) == 33 && g.w == 2);

    REQUIRE(agree_on_grid(
        &p,
        STORED_SLICE_FUNCTIONS,
        sizeof STORED_SLICE_FUNCTIONS / sizeof STORED_SLICE_FUNCTIONS[0]
    ));
    release_program(&p);
    return true;
}

typedef struct
{
    uint8_t a;
} CB1;

typedef struct
{
    uint8_t a, b, c;
} CB3;

typedef struct
{
    uint16_t a, b, c;
} CH3;

typedef struct
{
    uint8_t a[7];
} CB7;

typedef struct
{
    int32_t x, y;
} CP;

typedef struct
{
    uint8_t a[11];
} CB11;

typedef struct
{
    int32_t a, b, c;
} CI3;

typedef struct
{
    uint16_t a[7];
} CH7;

typedef struct
{
    uint8_t a[15];
} CB15;

typedef struct
{
    int64_t a;
    double b;
} CLD;

typedef struct
{
    float a;
    int32_t b;
} CFI;

typedef struct
{
    float a;
    double b;
} CFD;

typedef struct
{
    Slice s;
} CS;

typedef struct
{
    float x, y, z;
} CV3;

typedef struct
{
    float v[4];
} CV4;

typedef struct
{
    double x, y, z, w;
} CD4;

typedef struct
{
    float x, y;
} CV2;

typedef struct
{
    CV2 a;
    float z;
} CN3;

typedef struct
{
    double x;
} CD1;

typedef struct
{
    double x, y;
} CD2;

typedef struct
{
    uint8_t a[3];
} CA3;

typedef struct
{
    int64_t a[5];
} CI5;

typedef struct
{
    CP a[2];
} CP2;

typedef struct
{
    uint8_t a[2][3];
} CR23;

typedef double AMIX(void *, int64_t, CA3, float, CD2, CI5, CV4, uint8_t);
typedef int64_t ASPILL(void *, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, CA3, CD2, CI5, CB11);
typedef int64_t AEDGE(void *, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, CB11, int64_t);
typedef double AFEDGE(void *, double, double, double, double, double, double, double, CD2, double);

typedef double MIX(void *, int64_t, CB3, float, CV3, CP, double, CD2, uint8_t);

static CB3
c_eB3(void *context, CB3 s, uint8_t k)
{
    see_context(context);
    return (CB3){ (uint8_t)(s.a + k), (uint8_t)(s.b + k), (uint8_t)(s.c + k) };
}

static CV3
c_eV3(void *context, float k, CV3 s)
{
    see_context(context);
    return (CV3){ s.x + k, s.y + k, s.z + k };
}

static CB15
c_eB15(void *context, CB15 s, CB7 t)
{
    see_context(context);
    for (int i = 0; i < 15; i++) {
        s.a[i] = (uint8_t)(s.a[i] + t.a[i % 7]);
    }
    return s;
}

static CD4
c_eD4(void *context, CD4 a, CP b, double c)
{
    see_context(context);
    return (CD4){ a.w + c, a.z + b.x, a.y + b.y, a.x };
}

static CS
c_eS(void *context, CS s, int64_t n)
{
    see_context(context);
    return (CS){ { (const uint8_t *)s.s.ptr + n, s.s.len - n } };
}

static const char *const PASSED_STRUCT_FUNCTIONS[] = { "late", "chain", "rows", "tails" };

static bool
passes_to_c(Program *p)
{
    native_replace(&p->n, decl_named(p, "eB3"), (void *)c_eB3);
    native_replace(&p->n, decl_named(p, "eV3"), (void *)c_eV3);
    native_replace(&p->n, decl_named(p, "eB15"), (void *)c_eB15);
    native_replace(&p->n, decl_named(p, "eD4"), (void *)c_eD4);
    native_replace(&p->n, decl_named(p, "eS"), (void *)c_eS);
    for (int64_t a = 0; a < 40; a += 7) {
        for (int64_t b = 0; b < 40; b += 5) {
            int64_t t = ((a + 3) & 0xFF) + ((b + 3) & 0xFF) * 256 + 10 * 65536;
            int64_t vx = (int64_t)(((a & 15) + 0.5) * 2);
            int64_t vy = (int64_t)(((b & 15) + 0.5) * 2);
            uint8_t t7[7] = { 1, 2, 3, 4, 5, 6, (uint8_t)(b & 0xFF) };
            int64_t sum = 0;
            for (int i = 0; i < 15; i++) {
                sum = sum * 3 + (uint8_t)(((a + i * 3) & 0xFF) + t7[i % 7]);
            }
            int64_t dw = (int64_t)(((b & 15) + 0.25) * 4);
            int64_t d = dw * 1000000 + (3 + (a & 0xFF)) * 40000 + 3200 + 4;
            int64_t off = (a & 1) + (b & 3);
            if (((I2 *)fn(p, "callB3"))(CONTEXT, a, b) != t ||
                ((I2 *)fn(p, "callV3"))(CONTEXT, a, b) != vx * 10000 + vy * 100 + 7 ||
                ((I2 *)fn(p, "callB15"))(CONTEXT, a, b) != sum ||
                ((I2 *)fn(p, "callD4"))(CONTEXT, a, b) != d ||
                ((I2 *)fn(p, "callS"))(CONTEXT, a, b) != (6 - off) * 100 + 1 + off) {
                fprintf(
                    stderr,
                    "arm64 test: a call to C with structs at (%lld, %lld)\n",
                    (long long)a,
                    (long long)b
                );
                return false;
            }
        }
    }
    return true;
}

TEST_CASE(passed_structs)
{
    Program p;
    REQUIRE(build(&p, PASSED_STRUCTS));

    REQUIRE(((CB1(*)(void *, CB1))fn(&p, "tB1"))(CONTEXT, (CB1){ 0xFF }).a == 0);
    CB3 b3 = ((CB3(*)(void *, CB3))fn(&p, "tB3"))(CONTEXT, (CB3){ 10, 20, 254 });
    REQUIRE(b3.a == 11 && b3.b == 22 && b3.c == 1);
    CH3 h3 = ((CH3(*)(void *, CH3))fn(&p, "tH3"))(CONTEXT, (CH3){ 1000, 0xFFFF, 7 });
    REQUIRE(h3.a == 1001 && h3.b == 1 && h3.c == 10);
    CB7 in7 = { { 1, 2, 3, 4, 5, 6, 0xFF } };
    CB7 b7 = ((CB7(*)(void *, CB7))fn(&p, "tB7"))(CONTEXT, in7);
    for (int i = 0; i < 7; i++) {
        REQUIRE(b7.a[i] == (uint8_t)((i < 6 ? i + 1 : 0xFF) + i + 1));
    }
    CP pi = ((CP(*)(void *, CP))fn(&p, "tP"))(CONTEXT, (CP){ -5, 7 });
    REQUIRE(pi.x == 7 && pi.y == -5);
    CB11 b11;
    for (int i = 0; i < 11; i++) {
        b11.a[i] = (uint8_t)(0xA0 + i);
    }
    b11 = ((CB11(*)(void *, CB11))fn(&p, "tB11"))(CONTEXT, b11);
    for (int i = 0; i < 11; i++) {
        REQUIRE(b11.a[i] == (uint8_t)(0xA0 + 2 * i + 1));
    }
    CI3 i3 = ((CI3(*)(void *, CI3))fn(&p, "tI3"))(CONTEXT, (CI3){ 1, -2, 3 });
    REQUIRE(i3.a == 3 && i3.b == 1 && i3.c == -2);
    CH7 h7;
    for (int i = 0; i < 7; i++) {
        h7.a[i] = (uint16_t)(0xF000 + i * 0x111);
    }
    h7 = ((CH7(*)(void *, CH7))fn(&p, "tH7"))(CONTEXT, h7);
    for (int i = 0; i < 7; i++) {
        REQUIRE(h7.a[i] == (uint16_t)(0xF000 + i * 0x111 + i + 1));
    }
    CB15 b15;
    for (int i = 0; i < 15; i++) {
        b15.a[i] = (uint8_t)(0x30 + 7 * i);
    }
    b15 = ((CB15(*)(void *, CB15))fn(&p, "tB15"))(CONTEXT, b15);
    for (int i = 0; i < 15; i++) {
        REQUIRE(b15.a[i] == (uint8_t)(0x30 + 8 * i + 1));
    }
    CLD ld = ((CLD(*)(void *, CLD))fn(&p, "tLD"))(CONTEXT, (CLD){ -9, 1.25 });
    REQUIRE(ld.a == -8 && ld.b == 2.5);
    CFI fi = ((CFI(*)(void *, CFI))fn(&p, "tFI"))(CONTEXT, (CFI){ 2.0f, 40 });
    REQUIRE(fi.a == 2.5f && fi.b == 39);
    CFD fd = ((CFD(*)(void *, CFD))fn(&p, "tFD"))(CONTEXT, (CFD){ 1.5f, -1.0 });
    REQUIRE(fd.a == 3.0f && fd.b == -0.75);
    static const uint8_t text[] = "hello";
    CS s = ((CS(*)(void *, CS))fn(&p, "tS"))(CONTEXT, (CS){ { text, 5 } });
    REQUIRE(s.s.ptr == text + 1 && s.s.len == 4);
    CV3 v3 = ((CV3(*)(void *, CV3))fn(&p, "tV3"))(CONTEXT, (CV3){ 1, 2, 3 });
    REQUIRE(v3.x == 3 && v3.y == 1 && v3.z == 2);
    CV4 v4 = ((CV4(*)(void *, CV4))fn(&p, "tV4"))(CONTEXT, (CV4){ { 1, 2, 3, 4 } });
    REQUIRE(v4.v[0] == 2 && v4.v[1] == 2 && v4.v[2] == 3 && v4.v[3] == 3);
    CD4 d4 = ((CD4(*)(void *, CD4))fn(&p, "tD4"))(CONTEXT, (CD4){ 1, 2, 3, 4 });
    REQUIRE(d4.x == 4 && d4.y == 3 && d4.z == 2 && d4.w == 1);
    CN3 n3 = ((CN3(*)(void *, CN3))fn(&p, "tN3"))(CONTEXT, (CN3){ { 1, 2 }, 3 });
    REQUIRE(n3.a.x == 3 && n3.a.y == 1 && n3.z == 2);
    REQUIRE(((CD1(*)(void *, CD1))fn(&p, "tD1"))(CONTEXT, (CD1){ 2.5 }).x == -2.5);
    double m = ((MIX *)fn(&p, "mix"))(
        CONTEXT,
        1,
        (CB3){ 2, 3, 4 },
        5,
        (CV3){ 6, 7, 8 },
        (CP){ 9, 10 },
        11,
        (CD2){ 12, 13 },
        14
    );
    REQUIRE(m == 955);
    REQUIRE(((I2 *)fn(&p, "late"))(CONTEXT, 5, 6) == 5);

    REQUIRE(agree_on_grid(
        &p,
        PASSED_STRUCT_FUNCTIONS,
        sizeof PASSED_STRUCT_FUNCTIONS / sizeof PASSED_STRUCT_FUNCTIONS[0]
    ));
    lost_contexts = 0;
    REQUIRE(passes_to_c(&p));
    REQUIRE(lost_contexts == 0);
    release_program(&p);
    return true;
}

static const char *const ARRAY_ARG_FUNCTIONS[] = {
    "ar_small", "ar_large", "ar_copy",  "ar_alias",   "ar_nested",
    "ar_peer",  "ar_deep",  "ar_stack", "ar_floats",  "ar_structs",
    "ar_field", "ar_grid",  "ar_infer", "ar_through", "ar_global",
};

TEST_CASE(passed_arrays)
{
    catch_traps();
    Program p;
    REQUIRE(build(&p, ARRAY_ARGS));

    CA3 a3 = ((CA3(*)(void *, CA3))fn(&p, "ar_rot"))(CONTEXT, (CA3){ { 1, 2, 3 } });
    REQUIRE(a3.a[0] == 2 && a3.a[1] == 3 && a3.a[2] == 1);
    CV4 h = ((CV4(*)(void *, CV4))fn(&p, "ar_halves"))(CONTEXT, (CV4){ { 2, 4, 6, 8 } });
    REQUIRE(h.v[0] == 1 && h.v[1] == 2 && h.v[2] == 3 && h.v[3] == 4);
    CD2 d = ((CD2(*)(void *, CD2))fn(&p, "ar_flip"))(CONTEXT, (CD2){ 1.5, -2 });
    REQUIRE(d.x == -2 && d.y == -1.5);
    CI5 ones = { { 1, 2, 3, 4, 5 } };
    CI5 tens = { { 10, 20, 30, 40, 50 } };
    CI5 sum = ((CI5(*)(void *, CI5, CI5))fn(&p, "ar_add"))(CONTEXT, ones, tens);
    for (int i = 0; i < 5; i++) {
        REQUIRE(sum.a[i] == 11 * (i + 1));
    }
    CB11 b11;
    for (int i = 0; i < 11; i++) {
        b11.a[i] = (uint8_t)(0xF0 + i);
    }
    CB11 w = ((CB11(*)(void *, CB11, uint8_t))fn(&p, "ar_wide"))(CONTEXT, b11, 3);
    for (int i = 0; i < 11; i++) {
        REQUIRE(w.a[i] == (uint8_t)(0xF3 + i));
    }
    CP2 pairs = { { { 1, 2 }, { 3, 4 } } };
    CP2 swapped = ((CP2(*)(void *, CP2))fn(&p, "ar_swap"))(CONTEXT, pairs);
    REQUIRE(swapped.a[0].x == 3 && swapped.a[0].y == 4);
    REQUIRE(swapped.a[1].x == 1 && swapped.a[1].y == 2);
    CR23 grid = { { { 1, 2, 3 }, { 4, 5, 6 } } };
    CR23 rows = ((CR23(*)(void *, CR23))fn(&p, "ar_rows"))(CONTEXT, grid);
    REQUIRE(rows.a[0][0] == 4 && rows.a[0][2] == 6);
    REQUIRE(rows.a[1][0] == 1 && rows.a[1][2] == 3);
    CI5 made = ((CI5(*)(void *))fn(&p, "ar_made"))(CONTEXT);
    REQUIRE(made.a[0] == 1 && made.a[4] == 5);
    double m = ((AMIX *)fn(&p, "ar_mixed"))(
        CONTEXT,
        1,
        (CA3){ { 0, 0, 2 } },
        0.5f,
        (CD2){ 0, 4 },
        (CI5){ { 0, 0, 0, 0, 8 } },
        (CV4){ { 0, 0, 0, 16 } },
        32
    );
    REQUIRE(m == 63.5);
    int64_t spilled = ((ASPILL *)fn(&p, "ar_spill"))(
        CONTEXT,
        1,
        2,
        3,
        4,
        5,
        6,
        7,
        8,
        (CA3){ { 0, 0, 9 } },
        (CD2){ 0, 10 },
        (CI5){ { 0, 0, 0, 0, 11 } },
        b11
    );
    REQUIRE(spilled == 36 + 9 + 10 + 11 + 0xFA);
    int64_t edge = ((AEDGE *)fn(&p, "ar_edge"))(CONTEXT, 1, 2, 3, 4, 5, 6, b11, 7);
    REQUIRE(edge == 21 + 0xFA * 1000 + 700000);
    double fedge = ((AFEDGE *)fn(&p, "ar_fedge"))(CONTEXT, 1, 2, 3, 4, 5, 6, 7, (CD2){ 0, 8 }, 9);
    REQUIRE(fedge == 28 + 800 + 9000);

    REQUIRE(agree_on_grid(
        &p,
        ARRAY_ARG_FUNCTIONS,
        sizeof ARRAY_ARG_FUNCTIONS / sizeof ARRAY_ARG_FUNCTIONS[0]
    ));
    release_program(&p);
    return true;
}

static const char *const LEFT_TO_RIGHT_FUNCTIONS[] = {
    "arrays", "structs", "fields", "named", "zeroed", "branch", "either", "block", "chosen",
    "pass",   "base",    "rows",   "slices", "elems", "inner",  "loops",  "params", "under",
};

static int64_t
left_to_right_want(const char *name, int64_t a, int64_t b)
{
    int64_t x = a & 0xFF;
    int64_t y = b & 0xFF;
    if (strcmp(name, "arrays") == 0) {
        return x * 1000000 + 3 * 100000 + (x + 1000) * 10 + 2;
    }
    if (strcmp(name, "structs") == 0) {
        return x * 10000 + (x + 100) * 10 + 1;
    }
    if (strcmp(name, "fields") == 0) {
        return x * 10000 + (x + 100) * 10 + 1;
    }
    if (strcmp(name, "named") == 0) {
        return x * 1000 + 100 + (x + 100) * 100000;
    }
    if (strcmp(name, "zeroed") == 0) {
        return x * 100000 + (x + 1000) * 10 + 2;
    }
    if (strcmp(name, "branch") == 0) {
        return x * 1000 + (x + (b > 3 ? 100 : 0)) * 10 + (b > 3);
    }
    if (strcmp(name, "either") == 0) {
        int64_t px = x + (b > 5 ? 0 : 100);
        return x * 1000000 + px * 1000 + px * 2 + !(b > 2 && a > 1);
    }
    if (strcmp(name, "block") == 0) {
        return x * 100000 + 9 * 1000 + y;
    }
    if (strcmp(name, "chosen") == 0) {
        int64_t k = b & 3;
        return x * 100000 + (x + (k == 0 ? 100 : 0)) * 100 + (k == 0 ? 1 : k == 1 ? 5 : 6);
    }
    if (strcmp(name, "pass") == 0) {
        return x * 1000000 + (x + 1) * 1000 + x + 100;
    }
    if (strcmp(name, "base") == 0) {
        return 2000 * 100000 + x + 1000;
    }
    if (strcmp(name, "rows") == 0) {
        return 50 * 100000 + x;
    }
    if (strcmp(name, "slices") == 0) {
        return x + 1002;
    }
    if (strcmp(name, "elems") == 0 || strcmp(name, "inner") == 0) {
        return x * 10000 + x + 100;
    }
    if (strcmp(name, "loops") == 0) {
        return x * 1000 + 1 + (x + 200) * 1000000;
    }
    if (strcmp(name, "under") == 0) {
        return x * 10000 + (x + 100) * 10 + 2;
    }
    return x * 1000 + y;
}

TEST_CASE(left_to_right)
{
    Program p;
    REQUIRE(build(&p, LEFT_TO_RIGHT));
    size_t count = sizeof LEFT_TO_RIGHT_FUNCTIONS / sizeof LEFT_TO_RIGHT_FUNCTIONS[0];
    for (size_t f = 0; f < count; f++) {
        for (int64_t a = -3; a < 300; a += 37) {
            for (int64_t b = -3; b < 9; b++) {
                const char *name = LEFT_TO_RIGHT_FUNCTIONS[f];
                int64_t got = ((I2 *)fn(&p, name))(CONTEXT, a, b);
                int64_t want = left_to_right_want(name, a, b);
                if (got != want) {
                    fprintf(
                        stderr,
                        "arm64 test: %s(%lld, %lld) = %lld, want %lld\n",
                        name,
                        (long long)a,
                        (long long)b,
                        (long long)got,
                        (long long)want
                    );
                    return false;
                }
            }
        }
    }
    REQUIRE(agree_on_grid(&p, LEFT_TO_RIGHT_FUNCTIONS, count));
    release_program(&p);
    return true;
}

typedef struct
{
    int64_t a, b, c;
} CW;

typedef struct
{
    int64_t a[3];
    int64_t n;
} CWA;

typedef struct
{
    uint8_t a[17];
} CB17;

typedef struct
{
    float a[5];
} CF5;

typedef struct
{
    double a[5];
} CD5;

typedef struct
{
    int32_t n;
    CD2 d;
    Slice s;
} CM;

typedef struct
{
    int64_t a[9];
} CBig;

typedef struct
{
    int64_t a[40];
} CHuge;

typedef double MIXL(void *, int64_t, CW, float, CF5, CBig, double, CD2, uint8_t);

static CW
c_eW(void *context, const CW *s, int64_t k)
{
    see_context(context);
    return (CW){ s->a + k, s->b * 2, s->c - k };
}

static CBig
c_eBig(void *context, const CBig *s)
{
    see_context(context);
    CBig r;
    for (int i = 0; i < 9; i++) {
        r.a[i] = s->a[i] * 2;
    }
    return r;
}

static int64_t
c_see(void *context, CW *s)
{
    see_context(context);
    s->a = 999;
    return 0;
}

static int64_t
c_seeWA(void *context, CWA *s)
{
    see_context(context);
    s->a[0] = 999;
    return 0;
}

static const char *const LARGE_STRUCT_FUNCTIONS[] = {
    "alias",      "fieldAlias", "sliceAlias", "convertAlias", "derefAlias", "passAlias",
    "results",    "selfAssign", "ptrAssign",  "loopPass",     "bigChain",   "retTwice",
    "hugeLoop",   "hugeAlias",  "bigAlias",   "b17Alias",     "lateL",      "callMix",
    "copyReturn", "copyThree",  "callSpill",
};

static const char *const SEE_FUNCTIONS[] = {
    "seeLocal", "seeAddressed", "seeLater", "seeField", "seeElement",
    "seeLoop",  "seeSliced",    "callEW",   "callEBig",
};

static int64_t
large_want(const char *name, int64_t a, int64_t b)
{
    int64_t x = a & 0xFF;
    int64_t y = b & 0xFF;
    if (strcmp(name, "alias") == 0 || strcmp(name, "derefAlias") == 0 ||
        strcmp(name, "sliceAlias") == 0 || strcmp(name, "convertAlias") == 0) {
        return x * 1000 + 100;
    }
    if (strcmp(name, "fieldAlias") == 0) {
        return x * 1000 + 100 + y * 1000000;
    }
    if (strcmp(name, "passAlias") == 0) {
        return 2 * x * 1000 + 100;
    }
    if (strcmp(name, "results") == 0) {
        return (111 * x + 210) * 100000 + 111 * y + 210 + ((a & 7) + 2) * 1000000000;
    }
    if (strcmp(name, "selfAssign") == 0) {
        return 100 * x + 70 + y;
    }
    if (strcmp(name, "ptrAssign") == 0) {
        return 7 + 10 * x + 100 * y;
    }
    if (strcmp(name, "loopPass") == 0) {
        return 6 * (x + 10 * y) + 8;
    }
    if (strcmp(name, "bigChain") == 0) {
        int64_t total = 0;
        for (int64_t i = 0; i < 9; i++) {
            total = total * 3 + ((a + i) & 0xFF) + 2;
        }
        return total + x * 1000000000;
    }
    if (strcmp(name, "retTwice") == 0) {
        return (a & 0xFFFF) + 10 * (b & 0xFFFF) + 100;
    }
    if (strcmp(name, "hugeLoop") == 0) {
        int64_t total = 0;
        for (int64_t i = 0; i < 40; i++) {
            total = (total * 31 + ((a + (39 - i) * b) & 0xFFF) * 3) & 0xFFFFFFF;
        }
        return total;
    }
    if (strcmp(name, "hugeAlias") == 0) {
        return (x + y) * 1000 + 5;
    }
    if (strcmp(name, "bigAlias") == 0) {
        return (10 * y + x) * 1000 + 7;
    }
    if (strcmp(name, "b17Alias") == 0) {
        return (y * 1000 + x) * 10 + 9;
    }
    if (strcmp(name, "lateL") == 0) {
        return (10 * x + 1) * 1000 + x + 100;
    }
    if (strcmp(name, "callMix") == 0) {
        int64_t n = x + 2 + 6 + 12 + y * 5 + 3 * 6 + (b & 0x7F) * 7;
        return (int64_t)((double)n + 4 + (double)(a & 7) * 9 + 20 + 2.75 + 12 + 26);
    }
    if (strcmp(name, "copyReturn") == 0) {
        return x + 10 * y + 700;
    }
    if (strcmp(name, "copyThree") == 0) {
        return (a & 7) + (b & 7) * 110 + (a & 7) * 1000 + ((a + b) & 7) * 10000 + 900000;
    }
    if (strcmp(name, "callSpill") == 0) {
        int64_t k = a & 7;
        int64_t v[12];
        for (int64_t i = 0; i < 12; i++) {
            v[i] = k + 1 + i;
        }
        int64_t total = k + 210;
        for (int64_t r = 0; r < 3; r++) {
            for (int i = 0; i < 12; i += 2) {
                total += v[i] * v[i + 1];
            }
            total += r;
            for (int i = 0; i < 12; i += 2) {
                v[i] += 1;
            }
        }
        return total + x + 10 * y;
    }
    if (strcmp(name, "seeLocal") == 0 || strcmp(name, "seeField") == 0 ||
        strcmp(name, "seeElement") == 0) {
        return 999;
    }
    if (strcmp(name, "seeLoop") == 0) {
        return 19980 + a;
    }
    if (strcmp(name, "callEW") == 0) {
        return (x + 3 + 20 * y + 200) * 1000 + x + 10 * y + 500;
    }
    if (strcmp(name, "callEBig") == 0) {
        return 200 * x + 2 * (x + 8) + (x + 8) * 1000000;
    }
    return a;
}

typedef int64_t Want(const char *, int64_t, int64_t);

static bool
matches_by_hand(Program *p, const char *const *names, size_t count, Want *rule)
{
    for (size_t f = 0; f < count; f++) {
        for (int64_t a = -3; a < 300; a += 37) {
            for (int64_t b = -3; b < 9; b++) {
                int64_t got = ((I2 *)fn(p, names[f]))(CONTEXT, a, b);
                int64_t want = rule(names[f], a, b);
                if (got != want) {
                    fprintf(
                        stderr,
                        "arm64 test: %s(%lld, %lld) = %lld, want %lld\n",
                        names[f],
                        (long long)a,
                        (long long)b,
                        (long long)got,
                        (long long)want
                    );
                    return false;
                }
            }
        }
    }
    return true;
}

TEST_CASE(large_structs)
{
    Program p;
    REQUIRE(build(&p, LARGE_STRUCTS));

    CW w = ((CW(*)(void *, CW))fn(&p, "tW"))(CONTEXT, (CW){ 1, -2, 3 });
    REQUIRE(w.a == 3 && w.b == 1 && w.c == -2);
    CB17 b17;
    for (int i = 0; i < 17; i++) {
        b17.a[i] = (uint8_t)(0xF0 + i);
    }
    b17 = ((CB17(*)(void *, CB17))fn(&p, "tB17"))(CONTEXT, b17);
    for (int i = 0; i < 17; i++) {
        REQUIRE(b17.a[i] == (uint8_t)(0xF0 + 2 * i + 1));
    }
    CF5 f5 = ((CF5(*)(void *, CF5))fn(&p, "tF5"))(CONTEXT, (CF5){ { 1, 2, 3, 4, 5 } });
    for (int i = 0; i < 5; i++) {
        REQUIRE(f5.a[i] == (float)(2 * (5 - i)));
    }
    CD5 in5 = { { 1.5, -2, 3, 0.25, 8 } };
    CD5 d5 = ((CD5(*)(void *, CD5))fn(&p, "tD5"))(CONTEXT, in5);
    REQUIRE(d5.a[0] == -1.5 && d5.a[1] == 3 && d5.a[2] == -1 && d5.a[3] == 2.75 &&
            d5.a[4] == -4);
    static const uint8_t text[] = "hello";
    CM in = { 7, { 1.5, -2.5 }, { text, 5 } };
    CM m = ((CM(*)(void *, CM))fn(&p, "tM"))(CONTEXT, in);
    REQUIRE(m.n == 8 && m.d.x == -2.5 && m.d.y == 1.5 && m.s.ptr == text + 1 &&
            m.s.len == 4);
    CBig big;
    for (int i = 0; i < 9; i++) {
        big.a[i] = i * 11 - 3;
    }
    big = ((CBig(*)(void *, CBig))fn(&p, "tBig"))(CONTEXT, big);
    for (int i = 0; i < 9; i++) {
        REQUIRE(big.a[i] == (8 - i) * 11 - 2);
    }
    CHuge huge;
    for (int i = 0; i < 40; i++) {
        huge.a[i] = i * i - 7;
    }
    huge = ((CHuge(*)(void *, CHuge))fn(&p, "tHuge"))(CONTEXT, huge);
    for (int i = 0; i < 40; i++) {
        REQUIRE(huge.a[i] == ((39 - i) * (39 - i) - 7) * 3);
    }
    CBig mb = { { 8, 0, 0, 0, 0, 0, 0, 0, 9 } };
    double mix = ((MIXL *)fn(&p, "mixL"))(
        CONTEXT,
        1,
        (CW){ 2, 3, 4 },
        5,
        (CF5){ { 6, 0, 0, 0, 7 } },
        mb,
        10,
        (CD2){ 11, 12 },
        13
    );
    REQUIRE(mix == 777);

    size_t count = sizeof LARGE_STRUCT_FUNCTIONS / sizeof LARGE_STRUCT_FUNCTIONS[0];
    REQUIRE(matches_by_hand(&p, LARGE_STRUCT_FUNCTIONS, count, large_want));
    REQUIRE(agree_on_grid(&p, LARGE_STRUCT_FUNCTIONS, count));

    native_replace(&p.n, decl_named(&p, "eW"), (void *)c_eW);
    native_replace(&p.n, decl_named(&p, "eBig"), (void *)c_eBig);
    native_replace(&p.n, decl_named(&p, "see"), (void *)c_see);
    native_replace(&p.n, decl_named(&p, "seeWA"), (void *)c_seeWA);
    size_t sees = sizeof SEE_FUNCTIONS / sizeof SEE_FUNCTIONS[0];
    lost_contexts = 0;
    REQUIRE(matches_by_hand(&p, SEE_FUNCTIONS, sees, large_want));
    REQUIRE(lost_contexts == 0);
    CW cw = { 5, 6, 7 };
    REQUIRE(((int64_t(*)(void *, CW))fn(&p, "seeParam"))(CONTEXT, cw) == 999 && cw.a == 5);
    REQUIRE(((int64_t(*)(void *, CW *))fn(&p, "seeDeref"))(CONTEXT, &cw) == 5 && cw.a == 5);
    release_program(&p);
    return true;
}

TEST_CASE(constant_conditions)
{
    Program p;
    REQUIRE(build(&p, CONSTANT_CONDITIONS));
    REQUIRE(((I1 *)fn(&p, "t"))(CONTEXT, 5) == 5);
    REQUIRE(((I1 *)fn(&p, "u"))(CONTEXT, 7) == 1007);
    REQUIRE(((I1 *)fn(&p, "v"))(CONTEXT, 4) == 8);
    REQUIRE(((I1 *)fn(&p, "w"))(CONTEXT, 4) == 5);
    release_program(&p);
    return true;
}

static const char *const CONSTANT_TABLE_FUNCTIONS[] = {
    "square",  "byteAt",     "oddAt",     "signedAt", "halfAt",
    "wordAt",  "floatAt",    "singleAt",  "flagAt",   "colorAt",
    "gridAt",  "rowSum",     "allSum",    "pointAt",  "originAt",
    "boxArea", "deepAt",     "passPoint", "passWide", "retPoint",
    "retWide", "copyChange", "arrayCopy", "hugeAt",   "passHuge",
    "farAt",   "scaledAt",   "annotated", "nestedAt", "lengths",
    "emptyAt", "loopTables",
};

static const char *const ALIGNED_CONSTANTS[] = {
    "halves", "words", "floats", "singles", "grid",   "origin",
    "deep",   "huge",  "far",    "after",   "scaled", "nested",
};

static const char *const CONSTANT_TABLE_TRAPS[] = { "trapIndex", "trapRow" };

static int64_t
table_want(const char *name, int64_t a, int64_t b)
{
    static const int64_t bytes[] = { 1, 200, 255, 0, 17, 128, 64, 3 };
    static const int64_t signed_bytes[] = { -128, -1, 0, 1, 127, -50, 50, 7 };
    static const int64_t halves[] = { -30000, -2, 2, 30000 };
    static const int64_t words[] = { 4000000000, 1, 2, 3 };
    static const int64_t floats_by_4[] = { 2, -5, 12, 400 };
    static const int64_t singles_by_4[] = { 6, -10, 1, 32 };
    static const int64_t colors[] = { 100, 200, 300, 200 };
    static const int64_t corner_x[] = { 0, 10, 10, 3 };
    static const int64_t corner_y[] = { 0, 0, 10, -4 };
    static const int64_t deep_a[] = { -1, 2, -3 };
    static const int64_t scaled[] = { 3, 9, -3 };
    static const int64_t after[] = { -7, 70 };

    int64_t i = a & 7;
    int64_t r = a & 3;
    int64_t c = (b & 3) % 3;
    if (strcmp(name, "square") == 0) {
        return i * i + b;
    }
    if (strcmp(name, "byteAt") == 0) {
        return bytes[i] * 10 + b;
    }
    if (strcmp(name, "oddAt") == 0) {
        return 5 + r % 3 + b;
    }
    if (strcmp(name, "signedAt") == 0) {
        return signed_bytes[i] * 10 + b;
    }
    if (strcmp(name, "halfAt") == 0) {
        return halves[r] + b;
    }
    if (strcmp(name, "wordAt") == 0) {
        return words[r] + b;
    }
    if (strcmp(name, "floatAt") == 0) {
        return floats_by_4[r] + b;
    }
    if (strcmp(name, "singleAt") == 0) {
        return singles_by_4[r] + b;
    }
    if (strcmp(name, "flagAt") == 0) {
        return r == 0 || r == 3 ? 1000 + b : b;
    }
    if (strcmp(name, "colorAt") == 0) {
        return colors[r] + b;
    }
    if (strcmp(name, "gridAt") == 0) {
        return (3 * r + c + 1) * 100 + 3 * ((a + 1) & 3) + c + 1;
    }
    if (strcmp(name, "rowSum") == 0) {
        return (9 * r + 6) * 10 + b;
    }
    if (strcmp(name, "allSum") == 0) {
        return 240 + a + b;
    }
    if (strcmp(name, "pointAt") == 0) {
        return corner_x[r] * 100 + corner_y[r] + b;
    }
    if (strcmp(name, "originAt") == 0) {
        return 2996 + a + b;
    }
    if (strcmp(name, "boxArea") == 0) {
        return 1102 + a * b;
    }
    if (strcmp(name, "deepAt") == 0) {
        return 9400 + deep_a[r % 3] * 10 + b;
    }
    if (strcmp(name, "passPoint") == 0) {
        return corner_x[r] + corner_y[r] * b;
    }
    if (strcmp(name, "passWide") == 0) {
        return 123 + a + b;
    }
    if (strcmp(name, "retPoint") == 0) {
        return corner_x[r] * 7 + corner_y[r] + b;
    }
    if (strcmp(name, "retWide") == 0) {
        return 321 + a + b;
    }
    if (strcmp(name, "copyChange") == 0) {
        return a * 100 + b - 4 + 30000 - 4000000;
    }
    if (strcmp(name, "arrayCopy") == 0) {
        return b * 1000 + i * i;
    }
    if (strcmp(name, "hugeAt") == 0) {
        return 95 + after[a & 1] + b;
    }
    if (strcmp(name, "passHuge") == 0) {
        return 5 + 9 * a + b;
    }
    if (strcmp(name, "farAt") == 0) {
        return 77000 + after[a & 1] + b;
    }
    if (strcmp(name, "scaledAt") == 0) {
        return scaled[r % 3] * 10 + b;
    }
    if (strcmp(name, "annotated") == 0) {
        return (7 + r % 3) * 100 + 65 + b;
    }
    if (strcmp(name, "nestedAt") == 0) {
        int64_t x = 1 + 4 * (a & 1) + 2 * (b & 1);
        return 11 * x + 1;
    }
    if (strcmp(name, "lengths") == 0) {
        return 843 + b;
    }
    if (strcmp(name, "emptyAt") == 0) {
        return a - b;
    }
    if (strcmp(name, "loopTables") == 0) {
        int64_t total = 6 * b;
        for (int64_t k = 0; k < 8; k++) {
            total += k * k * bytes[(k + a) & 7];
        }
        return total;
    }
    return a;
}

TEST_CASE(constant_tables)
{
    Program p;
    REQUIRE(build(&p, CONSTANT_TABLES));

    for (size_t k = 0; k < sizeof ALIGNED_CONSTANTS / sizeof ALIGNED_CONSTANTS[0]; k++) {
        u32 decl = decl_named(&p, ALIGNED_CONSTANTS[k]);
        REQUIRE(p.c.decls.values[decl] % check_type_align(&p.c, p.c.decls.types[decl]) == 0);
    }

    size_t count = sizeof CONSTANT_TABLE_FUNCTIONS /
                   sizeof CONSTANT_TABLE_FUNCTIONS[0];
    REQUIRE(matches_by_hand(&p, CONSTANT_TABLE_FUNCTIONS, count, table_want));
    REQUIRE(agree_on_grid(&p, CONSTANT_TABLE_FUNCTIONS, count));
    REQUIRE(agree_on_grid(&p, CONSTANT_TABLE_TRAPS, 2));

    CW w = ((CW(*)(void *))fn(&p, "wideOf"))(CONTEXT);
    REQUIRE(w.a == 1 && w.b == 2 && w.c == 3);

    int64_t r = 0;
    REQUIRE(call_i2((I2 *)fn(&p, "trapIndex"), 7, 1, &r) == 0 && r == 50);
    REQUIRE(call_i2((I2 *)fn(&p, "trapIndex"), 8, 0, &r) == TRAP_BOUNDS);
    REQUIRE(call_i2((I2 *)fn(&p, "trapIndex"), -1, 0, &r) == TRAP_BOUNDS);
    REQUIRE(call_i2((I2 *)fn(&p, "trapRow"), 3, 2, &r) == 0 && r == 12);
    REQUIRE(call_i2((I2 *)fn(&p, "trapRow"), 4, 0, &r) == TRAP_BOUNDS);
    REQUIRE(call_i2((I2 *)fn(&p, "trapRow"), 0, 3, &r) == TRAP_BOUNDS);
    REQUIRE(call_i2((I2 *)fn(&p, "trapRow"), -1, 0, &r) == TRAP_BOUNDS);
    release_program(&p);
    return true;
}

typedef bool B7(void *, bool, bool, bool, bool, bool, bool, bool);

TEST_CASE(deep_logic)
{
    Program p;
    REQUIRE(build(
        &p,
        "fn deep(a: bool, b: bool, c: bool, d: bool, e: bool, f: bool, g: "
        "bool) -> bool {\n"
        "    return a and (b or (c and (d or (e and (f or g)))))\n"
        "}\n"
    ));
    B7 *deep = (B7 *)fn(&p, "deep");
    for (int m = 0; m < 128; m++) {
        bool v[7];
        for (int k = 0; k < 7; k++) {
            v[k] = (m >> k) & 1;
        }
        bool want = v[0] && (v[1] || (v[2] && (v[3] || (v[4] && (v[5] || v[6])))));
        REQUIRE(deep(CONTEXT, v[0], v[1], v[2], v[3], v[4], v[5], v[6]) == want);
    }
    release_program(&p);
    return true;
}

typedef int64_t E4(void *, int64_t, int64_t, int64_t, double);

static int
call_e4(E4 *f, const int64_t *args, double d, int64_t *result)
{
    trap_kind = 0;
    if (sigsetjmp(trap_jump, 1) == 0) {
        *result = f(CONTEXT, args[0], args[1], args[2], d);
        return 0;
    }
    return trap_kind;
}

TEST_CASE(differential)
{
    const char *env = getenv("CHIBI_DIFFERENTIAL");
    const char *seed_text = getenv("CHIBI_DIFFERENTIAL_SEED");
    int programs = env ? atoi(env) : 150;
    catch_traps();
    char *text = malloc(PROGRAM_SIZE);
    u64 rng = seed_text ? strtoull(seed_text, NULL, 10) : 0x5EED;
    for (int k = 0; k < programs; k++) {
        u64 seed = rng;
        size_t len = random_typed_program(&rng, text);
        text[len] = 0;
        Program p;
        if (!build(&p, text)) {
            fprintf(stderr, "differential: seed %llu\n%s", (unsigned long long)seed, text);
            REQUIRE(false);
        }

        Native described;
        REQUIRE(jit_compile(&described, &p.c, CONVENTION_HOST, JIT_DEBUGGER));
        IrReference ref;
        ir_reference_init(&ref, &p.c);
        for (u32 e = 0; e < 2 * PROGRAM_ENTRIES; e++) {
            char name[8];
            snprintf(name, sizeof name, "e%u", e % PROGRAM_ENTRIES);
            u32 decl = decl_named(&p, name);
            E4 *f = (E4 *)native_entry(e < PROGRAM_ENTRIES ? &p.n : &described, decl);
            for (int run = 0; run < 6; run++) {
                int64_t args[3] = { random_argument(&rng),
                                    random_argument(&rng),
                                    random_argument(&rng) };
                double d = random_float_argument(&rng);
                u64 slots[4] = { (u64)args[0], (u64)args[1], (u64)args[2], 0 };
                memcpy(&slots[3], &d, sizeof d);
                u64 want = 0;
                int64_t got = 0;
                int want_trap = ir_reference_run(&ref, decl, slots, &want);
                int got_trap = call_e4(f, args, d, &got);
                if (want_trap != got_trap || (!want_trap && (int64_t)want != got)) {
                    fprintf(
                        stderr,
                        "differential: seed %llu, %s%s(%lld, %lld, %lld, %g): "
                        "reference %lld "
                        "(trap %d), native %lld (trap %d)\n%s",
                        (unsigned long long)seed,
                        name,
                        e < PROGRAM_ENTRIES ? "" : " with debug info",
                        (long long)args[0],
                        (long long)args[1],
                        (long long)args[2],
                        d,
                        (long long)want,
                        want_trap,
                        (long long)got,
                        got_trap,
                        text
                    );
                    ir_reference_free(&ref);
                    native_free(&described);
                    release_program(&p);
                    free(text);
                    REQUIRE(false);
                }
            }
        }
        ir_reference_free(&ref);
        native_free(&described);
        release_program(&p);
    }
    free(text);
    return true;
}

static const int64_t SWITCH_EDGES[] = { INT64_MIN, INT64_MIN + 1, -1000,
                                        -129,      -128,          -7,
                                        1000,      4999,          5000,
                                        70000,     1ll << 40,     INT64_MAX };

TEST_CASE(switches)
{
    Program p;
    REQUIRE(build(&p, SWITCHES));
    catch_traps();
    int64_t got = 0;
    REQUIRE(call_i2((I2 *)fn(&p, "s0"), 2, 0, &got) == 0 && got == 20);
    REQUIRE(call_i2((I2 *)fn(&p, "s1"), 3, 0, &got) == 0 && got == -2);
    REQUIRE(call_i2((I2 *)fn(&p, "s1"), 6, 21, &got) == 0 && got == 42);
    REQUIRE(call_i2((I2 *)fn(&p, "s4"), 2, 9, &got) == 0 && got == 9);
    REQUIRE(call_i2((I2 *)fn(&p, "s5"), 200, 0, &got) == 0 && got == 200);
    REQUIRE(call_i2((I2 *)fn(&p, "s5"), 2, 0, &got) == TRAP_RANGE);
    REQUIRE(call_i2((I2 *)fn(&p, "s6"), 0, 1, &got) == 0 && got == 1);
    REQUIRE(call_i2((I2 *)fn(&p, "s7"), 1, 0, &got) == 0 && got == 3);

    IrReference ref;
    ir_reference_init(&ref, &p.c);
    static const int64_t bs[] = { -1, 0, 5 };
    for (int f = 0; f <= 12; f++) {
        char name[8];
        snprintf(name, sizeof name, "s%d", f);
        u32 decl = decl_named(&p, name);
        I2 *native = (I2 *)native_entry(&p.n, decl);
        for (int k = -300;
             k < 300 + (int)(sizeof SWITCH_EDGES / sizeof SWITCH_EDGES[0]);
             k++) {
            int64_t a = k < 300 ? k : SWITCH_EDGES[k - 300];
            for (int j = 0; j < 3; j++) {
                u64 slots[2] = { (u64)a, (u64)bs[j] };
                u64 want = 0;
                int want_trap = ir_reference_run(&ref, decl, slots, &want);
                int got_trap = call_i2(native, a, bs[j], &got);
                if (want_trap != got_trap || (!want_trap && (int64_t)want != got)) {
                    fprintf(
                        stderr,
                        "switches: %s(%lld, %lld): reference %lld (trap %d), "
                        "native %lld (trap %d)\n",
                        name,
                        (long long)a,
                        (long long)bs[j],
                        (long long)want,
                        want_trap,
                        (long long)got,
                        got_trap
                    );
                    REQUIRE(false);
                }
            }
        }
    }
    ir_reference_free(&ref);
    release_program(&p);
    return true;
}

TEST_CASE(store_while_loaded)
{
    Program p;
    REQUIRE(check_text(
        &p,
        "fn f(a: int) -> int {\n"
        "    x := a\n"
        "    y := x\n"
        "    x = 5\n"
        "    return y * 10 + x\n"
        "}\n",
        false,
        false
    ));
    IrCode *code = &p.c.ir.code;
    const IrFunc *f = &p.c.ir.funcs[0];
    u32 first = f->first;
    while (code->kinds[first] != IR_STORE || code->bs[first] & IR_CONST ||
           code->kinds[first + 1] != IR_STORE || !(code->bs[first + 1] & IR_CONST)) {
        first++;
        REQUIRE(first + 1 < f->first + f->count);
    }
    u32 slot = code->as[first], value = code->bs[first];
    code->as[first] = code->as[first + 1];
    code->bs[first] = code->bs[first + 1];
    code->as[first + 1] = slot;
    code->bs[first + 1] = value;
    REQUIRE(jit_compile(&p.n, &p.c, CONVENTION_HOST, 0));
    REQUIRE(((I1 *)fn(&p, "f"))(CONTEXT, 7) == 75);
    release_program(&p);
    return true;
}

typedef uint64_t U1(void *, uint64_t);

static int
call_u1(U1 *f, uint64_t a, uint64_t *result)
{
    trap_kind = 0;
    if (sigsetjmp(trap_jump, 1) == 0) {
        *result = f(CONTEXT, a);
        return 0;
    }
    return trap_kind;
}

static const char *const DIVIDE_TYPES[] = { "i8", "i16", "i32", "int",
                                            "u8", "u16", "u32", "u64" };
static const int DIVIDE_BITS[] = { 8, 16, 32, 64, 8, 16, 32, 64 };

static const int64_t DIVISORS[] = {
    1,           2,          3,          4,         5,         6,
    7,           8,          9,          10,        11,        12,
    13,          16,         25,         31,        32,        60,
    64,          100,        127,        128,       255,       256,
    641,         1000,       4096,       65535,     65536,     INT32_MAX,
    0x80000000,  UINT32_MAX, 1000000007, 1ll << 62, INT64_MAX, INT64_MIN,
    -1,          -2,         -3,         -4,        -7,        -8,
    -10,         -100,       -128,       -32768,    INT32_MIN, -1000000007,
    -(1ll << 62)
};
static const uint64_t BIG_DIVISORS[] = { 0x8000000000000000ull,
                                         0x8000000000000001ull,
                                         0xFFFFFFFFFFFFFFFFull,
                                         0xFFFFFFFFFFFFFFFEull,
                                         0x9E3779B97F4A7C15ull };

static bool
divisor_fits(int k, int64_t d)
{
    int bits = DIVIDE_BITS[k];
    bool s = k < 4;
    if (bits == 64) {
        return s || d > 0;
    }
    if (s) {
        return d >= -(1ll << (bits - 1)) && d < (1ll << (bits - 1));
    }
    return d > 0 && d < (1ll << bits);
}

static int
divide_reference(int k, bool rem, uint64_t a, uint64_t d, uint64_t *out)
{
    int bits = DIVIDE_BITS[k];
    if (k >= 4) {
        *out = rem ? a % d : a / d;
        return 0;
    }
    int64_t x = (int64_t)a, y = (int64_t)d;
    int64_t lo = bits == 64 ? INT64_MIN : -(1ll << (bits - 1));
    if (x == lo && y == -1) {
        *out = 0;
        return rem ? 0 : TRAP_OVERFLOW;
    }
    *out = (uint64_t)(rem ? x % y : x / y);
    return 0;
}

TEST_CASE(constant_division)
{
    char *text = malloc(1 << 20);
    size_t len = 0;
    u32 count = 0;
    uint64_t divisors[8][64];
    u32 ndivisors[8] = { 0 };
    for (int k = 0; k < 8; k++) {
        for (size_t j = 0; j < sizeof DIVISORS / sizeof DIVISORS[0]; j++) {
            if (divisor_fits(k, DIVISORS[j])) {
                divisors[k][ndivisors[k]++] = (uint64_t)DIVISORS[j];
            }
        }
        if (k == 7) {
            for (size_t j = 0; j < sizeof BIG_DIVISORS / sizeof BIG_DIVISORS[0]; j++) {
                divisors[k][ndivisors[k]++] = BIG_DIVISORS[j];
            }
        }
        for (u32 j = 0; j < ndivisors[k]; j++) {
            for (int rem = 0; rem < 2; rem++) {
                const char *op = rem ? "%" : "/";
                char divisor[32];
                if (k < 4) {
                    snprintf(divisor, sizeof divisor, "(%lld)", (long long)divisors[k][j]);
                } else {
                    snprintf(divisor, sizeof divisor, "%llu", (unsigned long long)divisors[k][j]);
                }
                if (k == 3) {
                    len += (size_t)snprintf(
                        text + len,
                        (1 << 20) - len,
                        "fn q%u(a: int) -> int => a %s %s\n",
                        count++,
                        op,
                        divisor
                    );
                } else if (k == 7) {
                    len += (size_t)snprintf(
                        text + len,
                        (1 << 20) - len,
                        "fn q%u(a: u64) -> u64 => a %s %s\n",
                        count++,
                        op,
                        divisor
                    );
                } else {
                    len += (size_t)snprintf(
                        text + len,
                        (1 << 20) - len,
                        "fn q%u(a: int) -> int => int(%s(a) %s %s)\n",
                        count++,
                        DIVIDE_TYPES[k],
                        op,
                        divisor
                    );
                }
            }
        }
    }

    Program p;
    REQUIRE(build(&p, text));
    catch_traps();
    u64 rng = 7;
    u32 at = 0;
    bool ok = true;
    for (int k = 0; k < 8 && ok; k++) {
        int bits = DIVIDE_BITS[k];
        bool s = k < 4;
        uint64_t mask = bits == 64 ? ~0ull : (1ull << bits) - 1;
        uint64_t lo = s ? (uint64_t)(-(int64_t)(mask >> 1) - 1) : 0;
        uint64_t hi = s ? mask >> 1 : mask;
        for (u32 j = 0; j < ndivisors[k]; j++) {
            for (int rem = 0; rem < 2; rem++) {
                char name[16];
                snprintf(name, sizeof name, "q%u", at++);
                void *f = fn(&p, name);
                uint64_t d = divisors[k][j];
                for (int n = 0; n < 160 && ok; n++) {
                    uint64_t a;
                    switch (n) {
                        case 0: a = lo; break;
                        case 1: a = lo + 1; break;
                        case 2: a = hi; break;
                        case 3: a = hi - 1; break;
                        case 4: a = 0; break;
                        case 5: a = 1; break;
                        case 6: a = s ? (uint64_t)-1 : 2; break;
                        case 7: a = d; break;
                        case 8: a = d - 1; break;
                        case 9: a = d + 1; break;
                        case 10: a = 0 - d; break;
                        case 11: a = 2 * d; break;
                        case 12: a = 2 * d - 1; break;
                        default: {
                            uint64_t r = (uint64_t)random_below(&rng, 1 << 30) << 34 ^
                                         (uint64_t)random_below(&rng, 1 << 30) << 4 ^
                                         (uint64_t)random_below(&rng, 16);
                            a = n % 3 ? r >> random_below(&rng, 64) : r;
                            break;
                        }
                    }
                    a &= mask;
                    if (s && bits < 64 && (a >> (bits - 1)) & 1) {
                        a |= ~mask;
                    }
                    uint64_t want = 0, got = 0;
                    int want_trap = divide_reference(k, rem, a, d, &want);
                    int got_trap;
                    if (k == 7) {
                        got_trap = call_u1((U1 *)f, a, &got);
                    } else {
                        int64_t r = 0;
                        got_trap = call_i2((I2 *)f, (int64_t)a, 0, &r);
                        got = (uint64_t)r;
                    }
                    if (got_trap != want_trap || (!want_trap && got != want)) {
                        fprintf(
                            stderr,
                            "%s %s %llu: want %llu (trap %d), got %llu (trap "
                            "%d)\n",
                            DIVIDE_TYPES[k],
                            rem ? "%" : "/",
                            (unsigned long long)d,
                            (unsigned long long)want,
                            want_trap,
                            (unsigned long long)got,
                            got_trap
                        );
                        fprintf(stderr, "  dividend %lld\n", (long long)a);
                        ok = false;
                    }
                }
            }
        }
    }
    release_program(&p);
    free(text);
    REQUIRE(ok);
    return true;
}

typedef int64_t W1(void *, uint64_t);

TEST_CASE(narrow_parameters)
{
    Program p;
    REQUIRE(build(
        &p,
        "fn t(a: u32) -> int => int(a / 3)\n"
        "fn t_store(a: u32) -> int {\n"
        "    x := a\n"
        "    return int(x % 10)\n"
        "}\n"
        "fn g(x: int) -> int => x\n"
        "fn t_call(a: u32) -> int {\n"
        "    y := g(1)\n"
        "    return int(a / 3) + int(a / 7) + y - 1\n"
        "}\n"
        "fn t8(a: u8) -> int => int(a / 3)\n"
        "fn ti8(a: i8) -> int => int(a / 3)\n"
        "fn tb(a: bool) -> int {\n"
        "    if a {\n"
        "        return 1\n"
        "    }\n"
        "    return 0\n"
        "}\n"
    ));
    REQUIRE(((W1 *)fn(&p, "t"))(CONTEXT, 0x100000009ull) == 3);
    REQUIRE(((W1 *)fn(&p, "t_store"))(CONTEXT, 0x100000009ull) == 9);
    REQUIRE(((W1 *)fn(&p, "t_call"))(CONTEXT, 0x100000009ull) == 4);
    REQUIRE(((W1 *)fn(&p, "t8"))(CONTEXT, 0xFFFFFF0Full) == 5);
    REQUIRE(((W1 *)fn(&p, "ti8"))(CONTEXT, 0x1F4ull) == -4);
    REQUIRE(((W1 *)fn(&p, "tb"))(CONTEXT, 0x100ull) == 0);
    release_program(&p);
    return true;
}

typedef struct
{
    int64_t x, y;
} CPoint;

typedef struct
{
    int64_t a, b, c, d;
} CQuad;

static const char *const GLOBAL_FUNCTIONS[] = {
    "setNarrow",   "floats",      "flagMode",         "moveCorner", "tableAt",
    "tableSet",    "defaults",    "padAt",            "viewSum",    "viaPointer",
    "passBig",     "leftToRight", "leftToRightSmall", "scalarOrder", "localCopy",
    "copyAt",      "loopTable",   "passTable",
};

static const char *const STATEFUL_GLOBALS[] = { "bump", "getCounter", "narrow", "tableTrap" };

static const char *const ALIGNED_GLOBALS[] = {
    "counter", "small",  "byte",   "half", "word",  "wide", "bits", "ratio", "single",
    "flag",    "mode",   "origin", "corner", "grid", "table", "zeros", "raw", "pad",
    "after",   "view",   "big",    "copy", "tail",
};

static int64_t
globals_want(const char *name, int64_t a, int64_t b)
{
    static const int64_t table[] = { 10, 20, 30, 40 };

    if (strcmp(name, "setNarrow") == 0) {
        return -7000 + 255 + 30000 + 7 + ((int64_t)1 << 40) + 1 + b;
    }
    if (strcmp(name, "floats") == 0) {
        return 12 + 4 * a + 6 + b;
    }
    if (strcmp(name, "flagMode") == 0) {
        return b + ((a & 1) == 0 ? 1000 : 0) + (b <= 3 ? 100 : 0);
    }
    if (strcmp(name, "moveCorner") == 0) {
        return (a + 1) * 1000 + b;
    }
    if (strcmp(name, "tableAt") == 0) {
        return table[a & 3] + b;
    }
    if (strcmp(name, "tableSet") == 0 || strcmp(name, "passTable") == 0) {
        return 100 - table[a & 3] + b;
    }
    if (strcmp(name, "defaults") == 0) {
        return 770 + b;
    }
    if (strcmp(name, "padAt") == 0) {
        return 77 + a * 10 + b;
    }
    if (strcmp(name, "viewSum") == 0) {
        return a + 230 + b;
    }
    if (strcmp(name, "viaPointer") == 0) {
        return a * 100 + b;
    }
    if (strcmp(name, "passBig") == 0) {
        return a * 10 + b + 100000;
    }
    if (strcmp(name, "leftToRight") == 0 || strcmp(name, "leftToRightSmall") == 0) {
        return a * 1000 + a + 1;
    }
    if (strcmp(name, "scalarOrder") == 0) {
        return a * 100 + a + 1;
    }
    if (strcmp(name, "localCopy") == 0) {
        return a * 100 + 43 + b * 1000;
    }
    if (strcmp(name, "copyAt") == 0) {
        return a + 310 + b;
    }
    if (strcmp(name, "loopTable") == 0) {
        return 100 + b;
    }
    abort();
}

TEST_CASE(empty_globals)
{
    Program p;
    REQUIRE(build(&p, EMPTY_GLOBALS));

    void *empty = ((void *(*)(void *))fn(&p, "emptyAt"))(CONTEXT);
    void *none = ((void *(*)(void *))fn(&p, "noneAt"))(CONTEXT);
    REQUIRE(empty && none && empty != none);
    release_program(&p);
    return true;
}

TEST_CASE(globals)
{
    Program p;
    REQUIRE(build(&p, GLOBALS));

    const Ir *ir = &p.c.ir;
    for (size_t k = 0; k < sizeof ALIGNED_GLOBALS / sizeof ALIGNED_GLOBALS[0]; k++) {
        u32 decl = decl_named(&p, ALIGNED_GLOBALS[k]);
        u32 type = p.c.decls.types[decl];
        u32 at = p.c.decls.values[decl];
        REQUIRE(at % check_type_align(&p.c, type) == 0);
        REQUIRE(at + check_type_size(&p.c, type) <= ir->globals_size);
    }
    REQUIRE(p.c.decls.values[decl_named(&p, "after")] < ir->nglobals);
    REQUIRE(p.c.decls.values[decl_named(&p, "pad")] >= ir->nglobals);
    REQUIRE(p.c.decls.values[decl_named(&p, "zeros")] >= ir->nglobals);
    REQUIRE(p.c.decls.values[decl_named(&p, "grid")] < ir->nglobals);

    int64_t after = 0;
    memcpy(&after, p.n.globals + p.c.decls.values[decl_named(&p, "after")], sizeof after);
    REQUIRE(after == 77);

    I2 *get = (I2 *)fn(&p, "getCounter");
    I2 *bump = (I2 *)fn(&p, "bump");
    REQUIRE(get(CONTEXT, 0, 0) == 5);
    REQUIRE(((I2 *)fn(&p, "narrow"))(CONTEXT, 0, 0) == 3999994909);
    REQUIRE(bump(CONTEXT, 1, 0) == 60);
    REQUIRE(bump(CONTEXT, 2, 3) == 83);
    REQUIRE(get(CONTEXT, 0, 1) == 9);

    int64_t *counter = ((int64_t * (*)(void *)) fn(&p, "counterAt"))(CONTEXT);
    REQUIRE((u8 *)counter == p.n.globals + p.c.decls.values[decl_named(&p, "counter")]);
    *counter = 42;
    REQUIRE(get(CONTEXT, 0, 0) == 42);

    CPoint *corner = ((CPoint * (*)(void *)) fn(&p, "cornerAt"))(CONTEXT);
    REQUIRE(corner->x == 3 && corner->y == 4);
    corner->x = 11;
    corner->y = 12;
    CPoint c = ((CPoint(*)(void *))fn(&p, "cornerOf"))(CONTEXT);
    REQUIRE(c.x == 11 && c.y == 12);

    int64_t r = 0;
    REQUIRE(call_i2((I2 *)fn(&p, "tableAt"), 0, 0, &r) == 0 && r == 10);
    REQUIRE(call_i2((I2 *)fn(&p, "tableTrap"), 3, 1, &r) == 0 && r == 41);
    REQUIRE(call_i2((I2 *)fn(&p, "tableTrap"), 4, 0, &r) == TRAP_BOUNDS);
    REQUIRE(call_i2((I2 *)fn(&p, "tableTrap"), -1, 0, &r) == TRAP_BOUNDS);

    size_t count = sizeof GLOBAL_FUNCTIONS / sizeof GLOBAL_FUNCTIONS[0];
    REQUIRE(matches_by_hand(&p, GLOBAL_FUNCTIONS, count, globals_want));

    CQuad q = ((CQuad(*)(void *))fn(&p, "bigOf"))(CONTEXT);
    REQUIRE(q.a == 100 && q.c == 0 && q.d == 0);
    release_program(&p);

    REQUIRE(build(&p, GLOBALS));
    REQUIRE(agree_on_grid(&p, GLOBAL_FUNCTIONS, count));
    release_program(&p);

    REQUIRE(build(&p, GLOBALS));
    REQUIRE(agree_on_grid(&p, STATEFUL_GLOBALS, sizeof STATEFUL_GLOBALS / sizeof STATEFUL_GLOBALS[0]));
    release_program(&p);
    return true;
}

typedef int64_t S7(void *, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t);

enum
{
    WORD_RET = 0xD65F03C0u,
    WORD_SAVE_CONTEXT = 0xF9000BA0u,
    WORD_LOAD_CONTEXT = 0xF9400BA0u,
    WORD_PUSH_CONTEXT_FRAME = 0xA9BE7BFDu,
    WORD_POP_CONTEXT_FRAME = 0xA8C27BFDu,
    WORD_MOV_X = 0xAA0003E0u,
    MOV_REGS = 0x001F001Fu,
};

static int64_t
c_h(void *context, int64_t x)
{
    see_context(context);
    return x * 3 + 1;
}

static CW
c_hw(void *context, const CW *w, int64_t k)
{
    see_context(context);
    return (CW){ w->a + k, w->b * k, w->c - k };
}

static int64_t
c_hs(void *context, Slice s, int64_t k)
{
    see_context(context);
    const uint8_t *bytes = s.ptr;
    return s.len * 100 + bytes[s.len - 1] * 10 + k;
}

static double
c_hf(void *context, double x, int64_t k)
{
    see_context(context);
    return x * (double)k + 0.5;
}

static int64_t
c_h7(void *context, int64_t a, int64_t b, int64_t c, int64_t d, int64_t e, CPoint p)
{
    see_context(context);
    return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * p.x + 7 * p.y;
}

static u32
count_words(Program *p, const char *name, u32 word, u32 ignored)
{
    u32 n = 0;
    for (const u32 *at = fn(p, name); *at != WORD_RET; at++) {
        n += (*at & ~ignored) == word;
    }
    return n;
}

static bool
keeps_context(Program *p, const char *name, u32 loads)
{
    u32 saves = loads ? 1 : 0;
    return count_words(p, name, WORD_LOAD_CONTEXT, 0) == loads &&
           count_words(p, name, WORD_SAVE_CONTEXT, 0) == saves &&
           count_words(p, name, WORD_PUSH_CONTEXT_FRAME, 0) == saves &&
           count_words(p, name, WORD_POP_CONTEXT_FRAME, 0) == saves;
}

TEST_CASE(hidden_context)
{
    Program p;
    REQUIRE(build(&p, HIDDEN_CONTEXT));
    native_replace(&p.n, decl_named(&p, "h"), (void *)c_h);
    native_replace(&p.n, decl_named(&p, "hw"), (void *)c_hw);
    native_replace(&p.n, decl_named(&p, "hs"), (void *)c_hs);
    native_replace(&p.n, decl_named(&p, "hf"), (void *)c_hf);
    native_replace(&p.n, decl_named(&p, "h7"), (void *)c_h7);

    REQUIRE(keeps_context(&p, "once", 0));
    REQUIRE(keeps_context(&p, "twice", 1));
    REQUIRE(keeps_context(&p, "nested", 1));
    REQUIRE(keeps_context(&p, "looped", 1));
    REQUIRE(keeps_context(&p, "evens", 1));
    REQUIRE(keeps_context(&p, "arms", 1));
    REQUIRE(keeps_context(&p, "mixed", 3));
    REQUIRE(keeps_context(&p, "seven", 0));
    REQUIRE(count_words(&p, "seven", WORD_MOV_X, MOV_REGS) == 0);

    lost_contexts = 0;
    for (int64_t x = 0; x < 20; x++) {
        int64_t evens = 0;
        for (int64_t i = 0; i < x; i += 2) {
            evens += 3 * i + 1;
        }
        REQUIRE(((I1 *)fn(&p, "once"))(CONTEXT, x) == 3 * x + 2);
        REQUIRE(((I1 *)fn(&p, "twice"))(CONTEXT, x) == 6 * x + 5);
        REQUIRE(((I1 *)fn(&p, "nested"))(CONTEXT, x) == 9 * x + 4);
        REQUIRE(((I1 *)fn(&p, "looped"))(CONTEXT, x) == 3 * x * (x - 1) / 2 + x);
        REQUIRE(((I1 *)fn(&p, "evens"))(CONTEXT, x) == evens);
        REQUIRE(((I1 *)fn(&p, "arms"))(CONTEXT, x) == (x > 0 ? 3 * x + 1 : 2));
        REQUIRE(((I1 *)fn(&p, "arms"))(CONTEXT, -x) == (x > 0 ? 6 * x + 2 : 2));
        REQUIRE(((I1 *)fn(&p, "chain"))(CONTEXT, x) == 9 * x + 7);
        REQUIRE(((I1 *)fn(&p, "mixed"))(CONTEXT, x) == 8 * x + 480);
    }
    REQUIRE(lost_contexts == 0);
    REQUIRE(((S7 *)fn(&p, "seven"))(CONTEXT, 1, 2, 3, 4, 5, 6, -7) == 42);
    release_program(&p);
    return true;
}

#ifdef __APPLE__
#define STDERR_NAME "__stderrp"
#else
#define STDERR_NAME "stderr"
#endif

static const char CALLS_INTO_C
    [] = "struct P { x: int, y: int }\n"
         "struct W { a: int, b: int, c: int }\n"
         "struct Mb { a: ?i32, b: ?u8 }\n"
         "struct Mw { a: ?i32, b: ?u8, c: ?f32 }\n"
         "struct Mf { a: ?f32 }\n"
         "struct Md { a: ?f64 }\n"
         "\n"
         "const table := W.{ 7, 8, 9 }\n"
         "\n"
         "extern fn labs(x: i64) -> i64\n"
         "extern fn memset(p: *[8]u8, c: i32, n: u64) -> *[8]u8\n"
         "extern fn strlen(s: [*]u8) -> u64\n"
         "extern fn atoi(s: cstring) -> i32\n"
         "extern fn chibi_c_eight(a: int, b: int, c: int, d: int, e: int, f: int, g: int, h: "
         "int) -> int\n"
         "extern fn chibi_c_mixed(a: i8, b: f64, c: u16, d: f32, e: i32, f: bool) -> f64\n"
         "extern fn chibi_c_registers(a: i8, b: u16, c: i32, d: bool, e: u32)\n"
         "extern fn chibi_c_u8() -> u8\n"
         "extern fn chibi_c_i8() -> i8\n"
         "extern fn chibi_c_u16() -> u16\n"
         "extern fn chibi_c_i16() -> i16\n"
         "extern fn chibi_c_u32() -> u32\n"
         "extern fn chibi_c_i32() -> i32\n"
         "extern fn chibi_c_bool(x: int) -> bool\n"
         "extern fn chibi_c_point(p: P, k: int) -> P\n"
         "extern fn chibi_c_scribble(w: W) -> int\n"
         "extern fn chibi_c_wide(k: int) -> W\n"
         "extern fn chibi_c_maybes(m: Mb, w: Mw, k: int) -> Mw\n"
         "extern fn chibi_c_halves(f: Mf, d: Md, k: int) -> Md\n"
         "extern fn chibi_c_half(d: Md, k: int) -> Mf\n"
         "pub extern fn chibi_c_tick()\n"
         "\n"
         "extern chibi_c_ticks: int\n"
         "extern chibi_c_seen: [5]u64\n"
         "extern chibi_c_origin: P\n"
         "extern chibi_c_maybe: ?i32\n"
         "extern " STDERR_NAME ": ?*u8\n"
         "\n"
         "fn h(x: int) -> int {\n"
         "    return x\n"
         "}\n"
         "\n"
         "fn after_c(x: int) -> int {\n"
         "    return h(labs(x)) + 1\n"
         "}\n"
         "\n"
         "fn only_c(x: int) -> int {\n"
         "    return labs(x) + labs(x - 10)\n"
         "}\n"
         "\n"
         "fn c_in_loop(n: int) -> int {\n"
         "    s := 0\n"
         "    for i in 0..n {\n"
         "        s += labs(i - 5)\n"
         "    }\n"
         "    return s\n"
         "}\n"
         "\n"
         "fn h_in_loop(n: int) -> int {\n"
         "    s := 0\n"
         "    for i in 0..n {\n"
         "        s += h(labs(i - 3))\n"
         "    }\n"
         "    return s\n"
         "}\n"
         "\n"
         "fn h_then_c(x: int) -> int {\n"
         "    return h(x) + labs(x - 100)\n"
         "}\n"
         "\n"
         "fn eight(x: int) -> int {\n"
         "    return chibi_c_eight(x + 1, x * 2, x - 3, 4, x + 5, 6, x * 7, x + 8)\n"
         "}\n"
         "\n"
         "fn mixed(x: int) -> f64 {\n"
         "    return chibi_c_mixed(i8(x) - 5, f64(x) * 0.5, u16(x) + 300, f32(x) + 1.5, i32(x) * "
         "-7, x > 3)\n"
         "}\n"
         "\n"
         "fn registers(x: int) {\n"
         "    chibi_c_registers(i8(x) - 10, u16(x) * 1000, i32(x) - 100, x > 2, u32(x) * "
         "200000000)\n"
         "}\n"
         "\n"
         "fn ret_u8() -> u8 {\n"
         "    return chibi_c_u8()\n"
         "}\n"
         "\n"
         "fn ret_i8() -> i8 {\n"
         "    return chibi_c_i8()\n"
         "}\n"
         "\n"
         "fn ret_u16() -> u16 {\n"
         "    return chibi_c_u16()\n"
         "}\n"
         "\n"
         "fn ret_i16() -> i16 {\n"
         "    return chibi_c_i16()\n"
         "}\n"
         "\n"
         "fn ret_u32() -> u32 {\n"
         "    return chibi_c_u32()\n"
         "}\n"
         "\n"
         "fn ret_i32() -> i32 {\n"
         "    return chibi_c_i32()\n"
         "}\n"
         "\n"
         "fn ret_bool(x: int) -> bool {\n"
         "    return chibi_c_bool(x)\n"
         "}\n"
         "\n"
         "fn narrow_math() -> int {\n"
         "    return int(chibi_c_u8() / 3) + int(chibi_c_i8() / 3) * 10 + int(chibi_c_u16() / 7) "
         "+ int(chibi_c_i16() / 3) + int(chibi_c_u32() / 5) + int(chibi_c_i32() / 2)\n"
         "}\n"
         "\n"
         "fn truth(x: int) -> int {\n"
         "    if chibi_c_bool(x) {\n"
         "        return 1\n"
         "    }\n"
         "    return 0\n"
         "}\n"
         "\n"
         "fn point(x: int) -> int {\n"
         "    p := chibi_c_point(P.{ x, x + 1 }, 3)\n"
         "    return p.x * 100 + p.y\n"
         "}\n"
         "\n"
         "fn scribble(x: int) -> int {\n"
         "    w := W.{ x, 2, 3 }\n"
         "    s := chibi_c_scribble(w)\n"
         "    return s * 1000 + w.a + w.b + w.c\n"
         "}\n"
         "\n"
         "fn from_table() -> int {\n"
         "    return chibi_c_scribble(table) * 10000 + chibi_c_scribble(table)\n"
         "}\n"
         "\n"
         "fn literal_length(x: int) -> int {\n"
         "    return int(strlen(\"chibi\")) * 100 + int(strlen(\"\")) + x\n"
         "}\n"
         "\n"
         "fn c_strings(x: int) -> int {\n"
         "    digits: [*]u8 = \"4096\"\n"
         "    k: cstring = \"27\"\n"
         "    return int(atoi(k)) * 10000 + int(atoi(cstring(digits))) + x\n"
         "}\n"
         "\n"
         "fn add_ticks(x: int) -> int {\n"
         "    chibi_c_ticks += x\n"
         "    return chibi_c_ticks\n"
         "}\n"
         "\n"
         "fn seen_at(i: int) -> u64 {\n"
         "    return chibi_c_seen[i]\n"
         "}\n"
         "\n"
         "fn move_origin(x: int) -> int {\n"
         "    chibi_c_origin.x += x\n"
         "    p := &chibi_c_origin\n"
         "    p.y = p.x * 2\n"
         "    return chibi_c_origin.y\n"
         "}\n"
         "\n"
         "fn swap_maybe(x: int) -> int {\n"
         "    v := int(chibi_c_maybe ?\? -1)\n"
         "    if x % 3 != 0 {\n"
         "        chibi_c_maybe = i32(x)\n"
         "    } else {\n"
         "        chibi_c_maybe = nil\n"
         "    }\n"
         "    return v\n"
         "}\n"
         "\n"
         "fn maybes(x: int) -> int {\n"
         "    m: Mb\n"
         "    if x % 2 == 0 {\n"
         "        m.a = i32(x)\n"
         "    }\n"
         "    if x % 3 != 0 {\n"
         "        m.b = u8(x & 127)\n"
         "    }\n"
         "    w := Mw.{ .b = 3 }\n"
         "    if x > 2 {\n"
         "        w.c = f32(x)\n"
         "    }\n"
         "    r := chibi_c_maybes(m, w, x)\n"
         "    return int(r.a ?\? -1) * 10000 + int(r.b ?\? 7) * 100 + int((r.c ?\? 9.0) * 2.0)\n"
         "}\n"
         "\n"
         "fn halves(x: int) -> int {\n"
         "    f: Mf\n"
         "    if x % 2 == 0 {\n"
         "        f.a = f32(x) * 0.5\n"
         "    }\n"
         "    d := Md.{ if x > 3 then f64(x) * 0.25 else nil }\n"
         "    r := chibi_c_halves(f, d, x)\n"
         "    h := chibi_c_half(r, x)\n"
         "    h2 := chibi_c_half(r, x + 3)\n"
         "    t := int((r.a ?\? -1.0) * 4.0) * 1000 + int((h.a ?\? -2.0) * 4.0)\n"
         "    return t + (if h.a != h2.a then 1000000000 else 0)\n"
         "}\n"
         "\n"
         "fn error_stream() -> ?*u8 {\n"
         "    return " STDERR_NAME "\n"
         "}\n"
         "\n"
         "fn wide(x: int) -> int {\n"
         "    w := chibi_c_wide(x)\n"
         "    return w.a + w.b * 10 + w.c * 100\n"
         "}\n"
         "\n"
         "fn ticks(n: int) -> int {\n"
         "    for i in 0..n {\n"
         "        chibi_c_tick()\n"
         "    }\n"
         "    return n\n"
         "}\n"
         "\n"
         "fn fill(x: int) -> int {\n"
         "    buf: [8]u8\n"
         "    _ = memset(&buf, i32(x), 8)\n"
         "    s := 0\n"
         "    for b in buf {\n"
         "        s += int(b)\n"
         "    }\n"
         "    return s\n"
         "}\n";

typedef uint64_t R0(void *);
typedef uint64_t R1(void *, int64_t);
typedef void V1(void *, int64_t);
typedef double DI(void *, int64_t);

enum
{
    WORD_MOV_X7 = 0xAA0003E7u,
    MOV_FROM = 0x001F0000u,
};

uint64_t chibi_c_seen[5];
int64_t chibi_c_ticks;
CPoint chibi_c_origin = { 1, 2 };
struct
{
    int32_t value;
    bool present;
} chibi_c_maybe = { 9, true };

int64_t
chibi_c_eight(int64_t a, int64_t b, int64_t c, int64_t d, int64_t e, int64_t f, int64_t g, int64_t h)
{
    return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f + 7 * g + 8 * h;
}

double
chibi_c_mixed(int8_t a, double b, uint16_t c, float d, int32_t e, bool f)
{
    return a + b * 2 + c * 3 + d * 4 + e * 5 + (f ? 1000 : 0);
}

void
chibi_c_registers(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e)
{
    chibi_c_seen[0] = a;
    chibi_c_seen[1] = b;
    chibi_c_seen[2] = c;
    chibi_c_seen[3] = d;
    chibi_c_seen[4] = e;
}

uint64_t
chibi_c_u8(void)
{
    return 0xDEADBEEFCAFE0105u;
}

uint64_t
chibi_c_i8(void)
{
    return 0x123456789ABCDE80u;
}

uint64_t
chibi_c_u16(void)
{
    return 0xFFFFFFFFFFFF8001u;
}

uint64_t
chibi_c_i16(void)
{
    return 0x0000000100008000u;
}

uint64_t
chibi_c_u32(void)
{
    return 0xFFFFFFFF80000007u;
}

uint64_t
chibi_c_i32(void)
{
    return 0x00000001FFFFFFF9u;
}

uint64_t
chibi_c_bool(int64_t x)
{
    return 0xFFFFFFFFFFFFFF00u | (uint64_t)(x & 1);
}

CPoint
chibi_c_point(CPoint p, int64_t k)
{
    return (CPoint){ p.x + k, p.y * k };
}

int64_t
chibi_c_scribble(CW w)
{
    int64_t s = w.a + w.b * 10 + w.c * 100;
    volatile CW *v = &w;
    v->a = 900;
    v->b = 800;
    v->c = 700;
    return s;
}

CW
chibi_c_wide(int64_t k)
{
    return (CW){ k, k + 1, k + 2 };
}

typedef struct
{
    struct
    {
        int32_t value;
        bool present;
    } a;
    struct
    {
        uint8_t value;
        bool present;
    } b;
} CMb;

typedef struct
{
    struct
    {
        int32_t value;
        bool present;
    } a;
    struct
    {
        uint8_t value;
        bool present;
    } b;
    struct
    {
        float value;
        bool present;
    } c;
} CMw;

CMw
chibi_c_maybes(CMb m, CMw w, int64_t k)
{
    CMw r = w;
    r.a.present = m.b.present;
    r.a.value = m.b.present ? m.b.value + (int32_t)k : 0;
    if (m.a.present) {
        r.b.value = (uint8_t)(m.a.value * 2);
        r.b.present = true;
    }
    r.c.value = w.c.present ? w.c.value * 2.0f : 0.0f;
    return r;
}

typedef struct
{
    struct
    {
        float value;
        bool present;
    } a;
} CMf;

typedef struct
{
    struct
    {
        double value;
        bool present;
    } a;
} CMd;

CMd
chibi_c_halves(CMf f, CMd d, int64_t k)
{
    CMd r = { { 0.0, false } };
    if (f.a.present || d.a.present) {
        r.a.value = (f.a.present ? (double)f.a.value : 0.0) + (d.a.present ? d.a.value * 2.0 : 0.0) + (double)k;
        r.a.present = true;
    }
    return r;
}

CMf
chibi_c_half(CMd d, int64_t k)
{
    CMf r = { { (float)k * 0.75f, false } };
    if (d.a.present && k % 3 != 0) {
        r.a.value = (float)(d.a.value * 0.5);
        r.a.present = true;
    }
    return r;
}

void
chibi_c_tick(void)
{
    chibi_c_ticks++;
}

static const char *const C_CALL_LIMITS[][2] = {
    { "extern fn chibi_c_nowhere() -> int\n"
      "fn g() -> int {\n"
      "    return chibi_c_nowhere()\n"
      "}\n",
      "an `extern fn` names a C function that is not loaded" },
    { "extern chibi_c_nothing: int\n"
      "fn g() -> int {\n"
      "    return chibi_c_nothing\n"
      "}\n",
      "an `extern` global names a C variable that isn't loaded" },
};

TEST_CASE(calls_into_c)
{
    Program p;
    REQUIRE(build(&p, CALLS_INTO_C));
    native_replace(&p.n, decl_named(&p, "h"), (void *)c_h);

    REQUIRE(keeps_context(&p, "after_c", 1));
    REQUIRE(keeps_context(&p, "only_c", 0));
    REQUIRE(keeps_context(&p, "c_in_loop", 0));
    REQUIRE(keeps_context(&p, "h_in_loop", 1));
    REQUIRE(keeps_context(&p, "h_then_c", 0));
    REQUIRE(keeps_context(&p, "eight", 0));
    REQUIRE(count_words(&p, "eight", WORD_MOV_X7, MOV_FROM) == 0);

    lost_contexts = 0;
    for (int64_t x = 0; x < 20; x++) {
        int64_t around = 0, spread = 0;
        for (int64_t i = 0; i < x; i++) {
            around += llabs(i - 5);
            spread += 3 * llabs(i - 3) + 1;
        }
        REQUIRE(((I1 *)fn(&p, "after_c"))(CONTEXT, -x) == 3 * x + 2);
        REQUIRE(((I1 *)fn(&p, "only_c"))(CONTEXT, x) == x + llabs(x - 10));
        REQUIRE(((I1 *)fn(&p, "c_in_loop"))(CONTEXT, x) == around);
        REQUIRE(((I1 *)fn(&p, "h_in_loop"))(CONTEXT, x) == spread);
        REQUIRE(((I1 *)fn(&p, "h_then_c"))(CONTEXT, x) == 3 * x + 1 + 100 - x);
        REQUIRE(((I1 *)fn(&p, "eight"))(CONTEXT, x) == 70 * x + 133);
        REQUIRE(((DI *)fn(&p, "mixed"))(CONTEXT, x) == -26.0 * (double)x + 901 + (x > 3 ? 1000 : 0));
        REQUIRE(((I1 *)fn(&p, "truth"))(CONTEXT, x) == (x & 1));
        REQUIRE(((R1 *)fn(&p, "ret_bool"))(CONTEXT, x) == (uint64_t)(x & 1));
        REQUIRE(((I1 *)fn(&p, "point"))(CONTEXT, x) == 103 * x + 303);
        REQUIRE(((I1 *)fn(&p, "scribble"))(CONTEXT, x) == 1001 * x + 320005);
        REQUIRE(((I1 *)fn(&p, "wide"))(CONTEXT, x) == 111 * x + 210);
        REQUIRE(((I1 *)fn(&p, "fill"))(CONTEXT, x) == 8 * x);
        REQUIRE(((I1 *)fn(&p, "literal_length"))(CONTEXT, x) == 500 + x);
        REQUIRE(((I1 *)fn(&p, "c_strings"))(CONTEXT, x) == 274096 + x);

        ((V1 *)fn(&p, "registers"))(CONTEXT, x);
        REQUIRE((uint32_t)chibi_c_seen[0] == (uint32_t)(int32_t)(int8_t)(x - 10));
        REQUIRE((uint32_t)chibi_c_seen[1] == (uint32_t)(x * 1000));
        REQUIRE((uint32_t)chibi_c_seen[2] == (uint32_t)(int32_t)(x - 100));
        REQUIRE((uint32_t)chibi_c_seen[3] == (uint32_t)(x > 2));
        REQUIRE((uint32_t)chibi_c_seen[4] == (uint32_t)(x * 200000000u));

        int64_t before = chibi_c_ticks;
        REQUIRE(((I1 *)fn(&p, "ticks"))(CONTEXT, x) == x);
        REQUIRE(chibi_c_ticks == before + x);

        before = chibi_c_ticks;
        REQUIRE(((I1 *)fn(&p, "add_ticks"))(CONTEXT, x) == before + x);
        REQUIRE(chibi_c_ticks == before + x);
        REQUIRE(((R1 *)fn(&p, "seen_at"))(CONTEXT, x % 5) == chibi_c_seen[x % 5]);

        int64_t origin = chibi_c_origin.x + x;
        REQUIRE(((I1 *)fn(&p, "move_origin"))(CONTEXT, x) == 2 * origin);
        REQUIRE(chibi_c_origin.x == origin && chibi_c_origin.y == 2 * origin);

        int64_t maybe = chibi_c_maybe.present ? chibi_c_maybe.value : -1;
        REQUIRE(((I1 *)fn(&p, "swap_maybe"))(CONTEXT, x) == maybe);
        REQUIRE(chibi_c_maybe.present == (x % 3 != 0));
        REQUIRE(!chibi_c_maybe.present || chibi_c_maybe.value == x);

        int64_t ra = x % 3 != 0 ? (x & 127) + x : -1;
        int64_t rb = x % 2 == 0 ? (uint8_t)(x * 2) : 3;
        int64_t rc = x > 2 ? 4 * x : 18;
        REQUIRE(((I1 *)fn(&p, "maybes"))(CONTEXT, x) == ra * 10000 + rb * 100 + rc);

        bool present = x % 2 == 0 || x > 3;
        double sum = (x % 2 == 0 ? (double)x * 0.5 : 0.0) + (x > 3 ? (double)x * 0.5 : 0.0) + (double)x;
        int64_t whole = present ? (int64_t)(sum * 4.0) : -4;
        int64_t half = present && x % 3 != 0 ? (int64_t)((float)(sum * 0.5) * 4.0f) : -8;
        REQUIRE(((I1 *)fn(&p, "halves"))(CONTEXT, x) == whole * 1000 + half);
    }
    REQUIRE(lost_contexts == 0);
    REQUIRE(((R0 *)fn(&p, "error_stream"))(CONTEXT) == (uint64_t)(uintptr_t)stderr);

    REQUIRE(((R0 *)fn(&p, "ret_u8"))(CONTEXT) == 0x05u);
    REQUIRE(((R0 *)fn(&p, "ret_i8"))(CONTEXT) == 0xFFFFFF80u);
    REQUIRE(((R0 *)fn(&p, "ret_u16"))(CONTEXT) == 0x8001u);
    REQUIRE(((R0 *)fn(&p, "ret_i16"))(CONTEXT) == 0xFFFF8000u);
    REQUIRE(((R0 *)fn(&p, "ret_u32"))(CONTEXT) == 0x80000007u);
    REQUIRE(((R0 *)fn(&p, "ret_i32"))(CONTEXT) == 0xFFFFFFF9u);
    REQUIRE(((I0 *)fn(&p, "narrow_math"))(CONTEXT) == 429490068);
    REQUIRE(((I0 *)fn(&p, "from_table"))(CONTEXT) == 9870987);
    REQUIRE(((I0 *)fn(&p, "from_table"))(CONTEXT) == 9870987);
    release_program(&p);

    for (size_t k = 0; k < sizeof C_CALL_LIMITS / sizeof C_CALL_LIMITS[0]; k++) {
        REQUIRE(check_text(&p, C_CALL_LIMITS[k][0], false, false));
        REQUIRE(!jit_compile(&p.n, &p.c, CONVENTION_HOST, 0) && strcmp(p.n.error, C_CALL_LIMITS[k][1]) == 0);
        release_program(&p);
    }
    return true;
}

static const char STACK_ARGUMENTS[] =
    "struct P { a: int, b: int }\n"
    "struct V3 { x: f32, y: f32, z: f32 }\n"
    "struct W { a: int, b: int, c: int }\n"
    "\n"
    "const table := W.{ 4, 5, 6 }\n"
    "\n"
    "extern fn chibi_c_twelve(a: int, b: int, c: int, d: int, e: int, f: int, g: int, h: i8, i: "
    "u16, j: i32, k: bool, l: int) -> int\n"
    "extern fn chibi_c_structs(a: int, b: int, c: int, d: int, e: int, f: int, g: int, p: P, q: "
    "P, r: int) -> int\n"
    "extern fn chibi_c_floats(a: f64, b: f64, c: f64, d: f64, e: f64, f: f64, s: V3, t: f32, n: "
    "int, u: f64) -> f64\n"
    "extern fn chibi_c_last_w(a: int, b: int, c: int, d: int, e: int, f: int, g: int, h: int, w: "
    "W) -> int\n"
    "extern fn chibi_c_probe(a: int, b: int, c: int, d: int, e: int, f: int, g: int, h: int, i: "
    "i8, j: i16, k: i32, l: i8, m: int, n: u8) -> int\n"
    "extern fn chibi_c_fprobe(a: f64, b: f64, c: f64, d: f64, e: f64, f: f64, g: f64, h: f64, i: "
    "f32, j: f64, k: f32, l: f32) -> int\n"
    "\n"
    "fn id(x: int) -> int {\n"
    "    return x\n"
    "}\n"
    "\n"
    "fn twelve(a: int, b: int, c: int, d: int, e: int, f: int, g: int, h: i8, i: u16, j: i32, k: "
    "bool, l: int) -> int {\n"
    "    s := a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f + 7 * g + 8 * int(h) + 9 * int(i) + 10 * "
    "int(j) + 12 * l\n"
    "    if k {\n"
    "        s += 1000\n"
    "    }\n"
    "    return s\n"
    "}\n"
    "\n"
    "fn kept(a: int, b: int, c: int, d: int, e: int, f: int, g: int, h: i8, i: u16, j: i32, k: "
    "bool, l: int) -> int {\n"
    "    s := id(a) + id(8 * int(h))\n"
    "    s += 9 * int(i) + 10 * int(j) + 12 * l\n"
    "    if k {\n"
    "        s += 1000\n"
    "    }\n"
    "    return s + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f + 7 * g\n"
    "}\n"
    "\n"
    "fn structs(a: int, b: int, c: int, d: int, e: int, f: int, p: P, q: P, r: int) -> int {\n"
    "    return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f + 7 * p.a + 8 * p.b + 9 * q.a + 10 * q.b "
    "+ 11 * r\n"
    "}\n"
    "\n"
    "fn hfa(a: f64, b: f64, c: f64, d: f64, e: f64, f: f64, s: V3, t: f32, n: int, u: f64) -> f64 "
    "{\n"
    "    return a + 2.0 * b + 3.0 * c + 4.0 * d + 5.0 * e + 6.0 * f + 7.0 * f64(s.x) + 8.0 * "
    "f64(s.y) + 9.0 * f64(s.z) + 10.0 * f64(t) + 11.0 * f64(n) + 12.0 * u\n"
    "}\n"
    "\n"
    "fn slice_at_seven(a: int, b: int, c: int, d: int, e: int, f: int, xs: []int, y: int) -> int "
    "{\n"
    "    return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f + 100 * xs[0] + 1000 * xs.len + 10000 * y\n"
    "}\n"
    "\n"
    "fn big_last(a: int, b: int, c: int, d: int, e: int, f: int, g: int, w: W) -> int {\n"
    "    return a + 7 * g + 10 * w.a + 100 * w.b + 1000 * w.c\n"
    "}\n"
    "\n"
    "fn alias_last(q: *W, b: int, c: int, d: int, e: int, f: int, g: int, w: W) -> int {\n"
    "    q.a = 1000\n"
    "    return w.a + b + g\n"
    "}\n"
    "\n"
    "fn eight(a: int, b: int, c: int, d: int, e: int, f: int, g: int, h: int) -> int {\n"
    "    return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f + 7 * g + 8 * h\n"
    "}\n"
    "\n"
    "fn many(a0: int, a1: int, a2: int, a3: int, a4: int, a5: int, a6: int, a7: int, a8: int, a9: "
    "int, a10: int, a11: int, a12: int, a13: int, a14: int, a15: int, a16: int, a17: int, a18: "
    "int, a19: int, a20: int, a21: int, a22: int, a23: int, a24: int, a25: int, a26: int, a27: "
    "int, a28: int, a29: int, a30: int, a31: int, a32: int, w: W) -> int {\n"
    "    return a0 + 2 * a7 + 3 * a31 + 4 * a32 + 5 * w.a + 6 * w.c\n"
    "}\n"
    "\n"
    "fn struct_tail(a: int, b: int, c: int, d: int, e: int, f: int, p: P, q: P) -> int {\n"
    "    return a + 2 * f + 3 * p.a + 4 * p.b + 5 * q.a + 6 * q.b\n"
    "}\n"
    "\n"
    "fn pinned(a: int, b: int, c: int, d: int, e: int, f: int, g: int, h: i8, k: i16) -> int {\n"
    "    s := 0\n"
    "    for i in 0..3 {\n"
    "        s += id(int(h)) + int(k) * i\n"
    "    }\n"
    "    return s + a + 2 * g\n"
    "}\n"
    "\n"
    "fn call_eight(x: int) -> int {\n"
    "    return eight(x, 1, 2, 3, 4, 5, 6, x * 2)\n"
    "}\n"
    "\n"
    "fn call_twelve(x: int) -> int {\n"
    "    w := W.{ x, x + 1, x + 2 }\n"
    "    t := twelve(x, x + 1, x - 2, 3, x * 4, 5, x + 6, i8(x) - 7, u16(x) * 300, i32(x) * -9, x "
    "> 3, x * 11)\n"
    "    k := kept(x, x + 1, x - 2, 3, x * 4, 5, x + 6, i8(x) - 7, u16(x) * 300, i32(x) * -9, x > "
    "3, x * 11)\n"
    "    return t + k + w.a + w.c\n"
    "}\n"
    "\n"
    "fn call_c_twelve(x: int) -> int {\n"
    "    return chibi_c_twelve(x, x + 1, x - 2, 3, x * 4, 5, x + 6, i8(x) - 7, u16(x) * 300, "
    "i32(x) * -9, x > 3, x * 11)\n"
    "}\n"
    "\n"
    "fn call_structs(x: int) -> int {\n"
    "    return structs(x, 1, 2, 3, 4, 5, P.{ x, x + 1 }, P.{ 7, x * 2 }, x - 1)\n"
    "}\n"
    "\n"
    "fn call_c_structs(x: int) -> int {\n"
    "    return chibi_c_structs(x, 1, 2, 3, 4, 5, 6, P.{ x, x + 1 }, P.{ 7, x * 2 }, x - 1)\n"
    "}\n"
    "\n"
    "fn call_hfa(x: int) -> f64 {\n"
    "    return hfa(f64(x), 1.5, 2.0, 3.0, 4.0, 5.0, V3.{ f32(x), 0.5, 0.25 }, f32(x) * 2.0, x + 3, "
    "0.125)\n"
    "}\n"
    "\n"
    "fn call_c_floats(x: int) -> f64 {\n"
    "    return chibi_c_floats(f64(x), 1.5, 2.0, 3.0, 4.0, 5.0, V3.{ f32(x), 0.5, 0.25 }, f32(x) * "
    "2.0, x + 3, 0.125)\n"
    "}\n"
    "\n"
    "fn call_slice(xs: []int, x: int) -> int {\n"
    "    return slice_at_seven(x, 1, 2, 3, 4, 5, xs, x + 7)\n"
    "}\n"
    "\n"
    "fn call_big(x: int) -> int {\n"
    "    w := W.{ x, x + 1, x + 2 }\n"
    "    return big_last(1, 2, 3, 4, 5, 6, x, w) + big_last(1, 2, 3, 4, 5, 6, 7, table)\n"
    "}\n"
    "\n"
    "fn call_alias(x: int) -> int {\n"
    "    w := W.{ x, 2, 3 }\n"
    "    r := alias_last(&w, 1, 2, 3, 4, 5, 6, w)\n"
    "    return r * 10000 + w.a\n"
    "}\n"
    "\n"
    "fn call_c_last_w(x: int) -> int {\n"
    "    w := W.{ x, x + 1, x + 2 }\n"
    "    return chibi_c_last_w(1, 2, 3, 4, 5, 6, 7, x, w) + w.a\n"
    "}\n"
    "\n"
    "fn call_many(x: int) -> int {\n"
    "    return many(x, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, "
    "22, 23, 24, 25, 26, 27, 28, 29, 30, 31, x + 32, W.{ x, 0, x * 2 })\n"
    "}\n"
    "\n"
    "fn call_struct_tail(x: int) -> int {\n"
    "    return struct_tail(x, 1, 2, 3, 4, 5, P.{ x, x + 1 }, P.{ 7, x * 2 })\n"
    "}\n"
    "\n"
    "fn call_pinned(x: int) -> int {\n"
    "    return pinned(x, 1, 2, 3, 4, 5, 6, i8(x) - 7, i16(x) * -100)\n"
    "}\n"
    "\n"
    "fn probe(x: int) -> int {\n"
    "    return chibi_c_probe(1, 2, 3, 4, 5, 6, 7, 8, i8(x) - 3, i16(x) * -100, i32(x) * 100000, "
    "i8(x), x * 1000000000000, u8(x) + 200)\n"
    "}\n"
    "\n"
    "fn fprobe(x: int) -> int {\n"
    "    return chibi_c_fprobe(1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, f32(x) + 0.5, f64(x) * 0.25, "
    "f32(x) - 1.5, 2.75)\n"
    "}\n";

typedef int64_t T12(void *, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, int8_t, uint16_t, int32_t, bool, int64_t);
typedef int64_t T12A(void *, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, uint64_t, uint64_t, uint64_t);
typedef int64_t TS(void *, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, CPoint, CPoint, int64_t);
typedef double TH(void *, double, double, double, double, double, double, CV3, float, int64_t, double);
typedef double THA(void *, double, double, double, double, double, double, double, double, double, double, int64_t, double);
typedef int64_t TL(void *, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, Slice, int64_t);
typedef int64_t TW(void *, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, CW);
typedef int64_t TSI(void *, Slice, int64_t);
typedef int64_t TST(void *, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, CPoint, CPoint);
typedef int64_t TPN(void *, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, int8_t, int16_t);

uint64_t chibi_c_words[13];

static int64_t
twelve_value(int64_t a, int64_t b, int64_t c, int64_t d, int64_t e, int64_t f, int64_t g, int8_t h, uint16_t i, int32_t j, bool k, int64_t l)
{
    return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f + 7 * g + 8 * h + 9 * i + 10 * (int64_t)j + 12 * l + (k ? 1000 : 0);
}

int64_t
chibi_c_twelve(int64_t a, int64_t b, int64_t c, int64_t d, int64_t e, int64_t f, int64_t g, int8_t h, uint16_t i, int32_t j, bool k, int64_t l)
{
    return twelve_value(a, b, c, d, e, f, g, h, i, j, k, l);
}

int64_t
chibi_c_structs(int64_t a, int64_t b, int64_t c, int64_t d, int64_t e, int64_t f, int64_t g, CPoint p, CPoint q, int64_t r)
{
    return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f + 7 * g + 8 * p.x + 9 * p.y + 10 * q.x + 11 * q.y + 12 * r;
}

static double
floats_value(double a, double b, double c, double d, double e, double f, CV3 s, float t, int64_t n, double u)
{
    return a + 2.0 * b + 3.0 * c + 4.0 * d + 5.0 * e + 6.0 * f + 7.0 * s.x + 8.0 * s.y + 9.0 * s.z + 10.0 * t + 11.0 * (double)n + 12.0 * u;
}

double
chibi_c_floats(double a, double b, double c, double d, double e, double f, CV3 s, float t, int64_t n, double u)
{
    return floats_value(a, b, c, d, e, f, s, t, n, u);
}

int64_t
chibi_c_last_w(int64_t a, int64_t b, int64_t c, int64_t d, int64_t e, int64_t f, int64_t g, int64_t h, CW w)
{
    int64_t s = a + b + c + d + e + f + g + 8 * h + 10 * w.a + 100 * w.b + 1000 * w.c;
    volatile CW *v = &w;
    v->a = 900;
    v->b = 800;
    v->c = 700;
    return s;
}

int64_t
chibi_c_probe(int64_t a, int64_t b, int64_t c, int64_t d, int64_t e, int64_t f, int64_t g, int64_t h, uint64_t s0, uint64_t s1, uint64_t s2, uint64_t s3, uint64_t s4, uint64_t s5)
{
    uint64_t s[6] = { s0, s1, s2, s3, s4, s5 };
    memcpy(chibi_c_words, s, sizeof s);
    return a + b + c + d + e + f + g + h;
}

int64_t
chibi_c_fprobe(double a, double b, double c, double d, double e, double f, double g, double h, double s0, double s1, double s2, double s3)
{
    double s[4] = { s0, s1, s2, s3 };
    memcpy(chibi_c_words, s, sizeof s);
    return (int64_t)(a + b + c + d + e + f + g + h);
}

static float
low_float(uint64_t w)
{
    float f;
    uint32_t bits = (uint32_t)w;
    memcpy(&f, &bits, sizeof f);
    return f;
}

static float
high_float(uint64_t w)
{
    return low_float(w >> 32);
}

static double
word_double(uint64_t w)
{
    double d;
    memcpy(&d, &w, sizeof d);
    return d;
}

static uint64_t
float_pair(float lo, float hi)
{
    uint32_t a, b;
    memcpy(&a, &lo, sizeof a);
    memcpy(&b, &hi, sizeof b);
    return a | (uint64_t)b << 32;
}

static int64_t
many_value(int64_t x)
{
    return 22 * x + 235;
}

static bool
chibi_calls_agree(Program *p, int64_t x)
{
    int8_t h = (int8_t)(x - 7);
    uint16_t i = (uint16_t)(x * 300);
    int32_t j = (int32_t)x * -9;
    int64_t t = twelve_value(x, x + 1, x - 2, 3, x * 4, 5, x + 6, h, i, j, x > 3, x * 11);
    int64_t xs[3] = { x * 5, 1, 2 };
    Slice s = { xs, 3 };
    CV3 v = { (float)x, 0.5f, 0.25f };
    double hfa = floats_value((double)x, 1.5, 2.0, 3.0, 4.0, 5.0, v, (float)x * 2.0f, x + 3, 0.125);
    int64_t structs = x + 2 + 6 + 12 + 20 + 30 + 7 * x + 8 * (x + 1) + 63 + 20 * x + 11 * (x - 1);
    int64_t sliced = x + 2 + 6 + 12 + 20 + 30 + 500 * x + 3000 + 10000 * (x + 7);
    int64_t big = 1 + 7 * x + 10 * x + 100 * (x + 1) + 1000 * (x + 2) + 1 + 49 + 40 + 500 + 6000;
    return ((I1 *)fn(p, "call_eight"))(CONTEXT, x) == 17 * x + 112 &&
           ((I1 *)fn(p, "call_twelve"))(CONTEXT, x) == 2 * t + 2 * x + 2 &&
           ((I1 *)fn(p, "call_structs"))(CONTEXT, x) == structs &&
           ((DI *)fn(p, "call_hfa"))(CONTEXT, x) == hfa &&
           ((TSI *)fn(p, "call_slice"))(CONTEXT, s, x) == sliced &&
           ((I1 *)fn(p, "call_big"))(CONTEXT, x) == big &&
           ((I1 *)fn(p, "call_alias"))(CONTEXT, x) == (x + 7) * 10000 + 1000 &&
           ((I1 *)fn(p, "call_many"))(CONTEXT, x) == many_value(x) &&
           ((I1 *)fn(p, "call_struct_tail"))(CONTEXT, x) == 20 * x + 49 &&
           ((I1 *)fn(p, "call_pinned"))(CONTEXT, x) == -296 * x - 9;
}

static bool
probes_agree(Program *p, int64_t x, Convention convention)
{
    bool ok = ((I1 *)fn(p, "probe"))(CONTEXT, x) == 36;
    if (convention == CONVENTION_APPLE) {
        ok &= (int8_t)chibi_c_words[0] == x - 3 && (int16_t)(chibi_c_words[0] >> 16) == x * -100 &&
              (int32_t)(chibi_c_words[0] >> 32) == x * 100000 && (int8_t)chibi_c_words[1] == x &&
              (int64_t)chibi_c_words[2] == x * 1000000000000 && (uint8_t)chibi_c_words[3] == x + 200;
    } else {
        ok &= (int8_t)chibi_c_words[0] == x - 3 && (int16_t)chibi_c_words[1] == x * -100 &&
              (int32_t)chibi_c_words[2] == x * 100000 && (int8_t)chibi_c_words[3] == x &&
              (int64_t)chibi_c_words[4] == x * 1000000000000 && (uint8_t)chibi_c_words[5] == x + 200;
    }

    ok &= ((I1 *)fn(p, "fprobe"))(CONTEXT, x) == 36;
    ok &= low_float(chibi_c_words[0]) == (float)x + 0.5f && word_double(chibi_c_words[1]) == (double)x * 0.25 &&
          low_float(chibi_c_words[2]) == (float)x - 1.5f;
    if (convention == CONVENTION_APPLE) {
        ok &= high_float(chibi_c_words[2]) == 2.75f;
    } else {
        ok &= low_float(chibi_c_words[3]) == 2.75f;
    }
    return ok;
}

TEST_CASE(stack_arguments)
{
    Program p;
    REQUIRE(build(&p, STACK_ARGUMENTS));
    REQUIRE(count_words(&p, "twelve", WORD_PUSH_CONTEXT_FRAME, 0) == 1);
    REQUIRE(count_words(&p, "twelve", WORD_SAVE_CONTEXT, 0) == 0);
    REQUIRE(count_words(&p, "kept", WORD_PUSH_CONTEXT_FRAME, 0) == 1);

    for (int64_t x = 0; x < 20; x++) {
        int8_t h = (int8_t)(x - 7);
        uint16_t i = (uint16_t)(x * 300);
        int32_t j = (int32_t)x * -9;
        int64_t t = twelve_value(x, x + 1, x - 2, 3, x * 4, 5, x + 6, h, i, j, x > 3, x * 11);
        REQUIRE(((T12 *)fn(&p, "twelve"))(CONTEXT, x, x + 1, x - 2, 3, x * 4, 5, x + 6, h, i, j, x > 3, x * 11) == t);
        REQUIRE(((T12 *)fn(&p, "kept"))(CONTEXT, x, x + 1, x - 2, 3, x * 4, 5, x + 6, h, i, j, x > 3, x * 11) == t);
        REQUIRE(((I1 *)fn(&p, "call_c_twelve"))(CONTEXT, x) == t);

        CPoint a = { x, x + 1 }, b = { 7, x * 2 };
        int64_t structs = x + 2 + 6 + 12 + 20 + 30 + 7 * x + 8 * (x + 1) + 63 + 20 * x + 11 * (x - 1);
        REQUIRE(((TS *)fn(&p, "structs"))(CONTEXT, x, 1, 2, 3, 4, 5, a, b, x - 1) == structs);
        REQUIRE(((I1 *)fn(&p, "call_c_structs"))(CONTEXT, x) == x + 112 + 8 * x + 9 * (x + 1) + 70 + 22 * x + 12 * (x - 1));
        REQUIRE(((TST *)fn(&p, "struct_tail"))(CONTEXT, x, 1, 2, 3, 4, 5, a, b) == 20 * x + 49);
        REQUIRE(((TPN *)fn(&p, "pinned"))(CONTEXT, x, 1, 2, 3, 4, 5, 6, (int8_t)(x - 7), (int16_t)(x * -100)) == -296 * x - 9);

        CV3 v = { (float)x, 0.5f, 0.25f };
        double hfa = floats_value((double)x, 1.5, 2.0, 3.0, 4.0, 5.0, v, (float)x * 2.0f, x + 3, 0.125);
        REQUIRE(((TH *)fn(&p, "hfa"))(CONTEXT, (double)x, 1.5, 2.0, 3.0, 4.0, 5.0, v, (float)x * 2.0f, x + 3, 0.125) == hfa);
        REQUIRE(((DI *)fn(&p, "call_c_floats"))(CONTEXT, x) == hfa);

        int64_t xs[3] = { x * 5, 1, 2 };
        Slice s = { xs, 3 };
        int64_t sliced = x + 2 + 6 + 12 + 20 + 30 + 500 * x + 3000 + 10000 * (x + 7);
        REQUIRE(((TL *)fn(&p, "slice_at_seven"))(CONTEXT, x, 1, 2, 3, 4, 5, s, x + 7) == sliced);

        CW w = { x, x + 1, x + 2 };
        REQUIRE(((TW *)fn(&p, "big_last"))(CONTEXT, 1, 2, 3, 4, 5, 6, x, w) == 1 + 7 * x + 10 * x + 100 * (x + 1) + 1000 * (x + 2));
        REQUIRE(((I1 *)fn(&p, "call_c_last_w"))(CONTEXT, x) == 28 + 8 * x + 10 * x + 100 * (x + 1) + 1000 * (x + 2) + x);

        REQUIRE(chibi_calls_agree(&p, x));

        REQUIRE(probes_agree(&p, x, CONVENTION_HOST));
    }
    release_program(&p);

    REQUIRE(check_text(&p, STACK_ARGUMENTS, false, false));
    REQUIRE(jit_compile(&p.n, &p.c, CONVENTION_APPLE, 0));
    for (int64_t x = 0; x < 20; x++) {
        REQUIRE(chibi_calls_agree(&p, x));

        int8_t h = (int8_t)(x - 7);
        uint16_t i = (uint16_t)(x * 300);
        int32_t j = (int32_t)x * -9;
        int64_t t = twelve_value(x, x + 1, x - 2, 3, x * 4, 5, x + 6, h, i, j, x > 3, x * 11);
        uint64_t packed = (uint8_t)h | (uint64_t)i << 16 | (uint64_t)(uint32_t)j << 32;
        REQUIRE(((T12A *)fn(&p, "twelve"))(CONTEXT, x, x + 1, x - 2, 3, x * 4, 5, x + 6, packed, x > 3, (uint64_t)(x * 11)) == t);
        REQUIRE(((T12A *)fn(&p, "kept"))(CONTEXT, x, x + 1, x - 2, 3, x * 4, 5, x + 6, packed, x > 3, (uint64_t)(x * 11)) == t);

        CV3 v = { (float)x, 0.5f, 0.25f };
        double hfa = floats_value((double)x, 1.5, 2.0, 3.0, 4.0, 5.0, v, (float)x * 2.0f, x + 3, 0.125);
        double first = word_double(float_pair(v.x, v.y));
        double second = word_double(float_pair(v.z, (float)x * 2.0f));
        REQUIRE(((THA *)fn(&p, "hfa"))(CONTEXT, (double)x, 1.5, 2.0, 3.0, 4.0, 5.0, -1.0, -2.0, first, second, x + 3, 0.125) == hfa);

        REQUIRE(probes_agree(&p, x, CONVENTION_APPLE));
    }
    release_program(&p);
    return true;
}

static const char VARIADIC_CALLS[] =
    "struct P { a: int, b: int }\n"
    "\n"
    "extern fn snprintf(buf: *[128]u8, n: u64, fmt: *[64]u8, ..) -> i32\n"
    "extern fn chibi_c_va(n: int, ..) -> f64\n"
    "extern fn chibi_c_vprobe(a: int, ..) -> int\n"
    "extern fn chibi_c_vpoint(p: P, n: int, ..) -> int\n"
    "extern fn chibi_c_probe(a: int, b: int, c: int, d: int, e: int, f: int, g: int, h: int, i: "
    "i32, ..) -> int\n"
    "\n"
    "fn format(buf: *[128]u8, fmt: *[64]u8, x: int) -> i32 {\n"
    "    return snprintf(buf, 128, fmt, x, i8(x) - 3, u16(x) + 7, f32(x) * 0.5, f64(x) / 4.0, x > "
    "2, 123456789012, 7, -1, x * 3, x * 5, 2.5)\n"
    "}\n"
    "\n"
    "fn pairs(x: int) -> f64 {\n"
    "    return chibi_c_va(10, x, 0.5, x + 1, f32(x), x * 4, 1.25, x - 5, 2.0, x + 6, 0.75, x * 7, "
    "f64(x), x + 8, 3.5, x * 9, 4.5, x - 10, 0.25, x + 11, 8.0)\n"
    "}\n"
    "\n"
    "fn vprobe(x: int) -> int {\n"
    "    return chibi_c_vprobe(x, x + 1, i8(x) - 3, f32(x) + 0.5, 2.5, u16(x) + 60000, 7)\n"
    "}\n"
    "\n"
    "fn vpoint(x: int) -> int {\n"
    "    return chibi_c_vpoint(P.{ x, x + 1 }, 3, x * 2, x + 5, x - 9)\n"
    "}\n"
    "\n"
    "fn vnamed(x: int) -> int {\n"
    "    return chibi_c_probe(1, 2, 3, 4, 5, 6, 7, 8, i32(x) - 5, 7, i8(x) - 3, x * 1000)\n"
    "}\n";

typedef int32_t TF(void *, char *, const char *, int64_t);

double
chibi_c_va(int64_t n, ...)
{
    va_list ap;
    va_start(ap, n);
    double s = 0;
    for (int64_t i = 0; i < n; i++) {
        int64_t k = va_arg(ap, int64_t);
        double d = va_arg(ap, double);
        s = s * 2.0 + (double)k + d;
    }
    va_end(ap);
    return s;
}

int64_t
chibi_c_vpoint(CPoint p, int64_t n, ...)
{
    va_list ap;
    va_start(ap, n);
    int64_t s = p.x + 2 * p.y;
    for (int64_t i = 0; i < n; i++) {
        s = s * 10 + va_arg(ap, int64_t);
    }
    va_end(ap);
    return s;
}

int64_t
chibi_c_vprobe(int64_t a, int64_t x1, int64_t x2, int64_t x3, int64_t x4, int64_t x5, int64_t x6, int64_t x7, uint64_t s0, uint64_t s1, uint64_t s2, uint64_t s3, uint64_t s4, uint64_t s5)
{
    uint64_t w[13] = { (uint64_t)x1, (uint64_t)x2, (uint64_t)x3, (uint64_t)x4, (uint64_t)x5, (uint64_t)x6, (uint64_t)x7, s0, s1, s2, s3, s4, s5 };
    memcpy(chibi_c_words, w, sizeof w);
    return a;
}

static bool
named_then_variadic(Program *p, int64_t x)
{
    return ((I1 *)fn(p, "vnamed"))(CONTEXT, x) == 36 && (int32_t)chibi_c_words[0] == x - 5 &&
           (int32_t)chibi_c_words[1] == 7 && (int32_t)chibi_c_words[2] == x - 3 &&
           (int64_t)chibi_c_words[3] == x * 1000;
}

static bool
vprobe_agrees(Program *p, int64_t x, Convention convention)
{
    bool ok = ((I1 *)fn(p, "vprobe"))(CONTEXT, x) == x;
    if (convention == CONVENTION_APPLE) {
        return ok && (int64_t)chibi_c_words[7] == x + 1 && (int32_t)chibi_c_words[8] == x - 3 &&
               word_double(chibi_c_words[9]) == (double)((float)x + 0.5f) && word_double(chibi_c_words[10]) == 2.5 &&
               (int32_t)chibi_c_words[11] == x + 60000 && (int32_t)chibi_c_words[12] == 7;
    }
    return ok && (int64_t)chibi_c_words[0] == x + 1 && (int32_t)chibi_c_words[1] == x - 3 &&
           (int32_t)chibi_c_words[2] == x + 60000 && (int32_t)chibi_c_words[3] == 7;
}

TEST_CASE(variadic_calls)
{
    Program p;
    REQUIRE(build(&p, VARIADIC_CALLS));
    char fmt[64] = "%ld %d %d %.2f %.3f %d %ld %d %d %ld %ld %.1f";
    for (int64_t x = 0; x < 20; x++) {
        char buf[128] = { 0 }, want[128];
        int m = snprintf(want, sizeof want, fmt, (long)x, (int)(int8_t)(x - 3), (int)(uint16_t)(x + 7), (double)((float)x * 0.5f), (double)x / 4.0, (int)(x > 2), 123456789012L, 7, -1, (long)(x * 3), (long)(x * 5), 2.5);
        REQUIRE(((TF *)fn(&p, "format"))(CONTEXT, buf, fmt, x) == m);
        REQUIRE(strcmp(buf, want) == 0);

        int64_t k[10] = { x, x + 1, x * 4, x - 5, x + 6, x * 7, x + 8, x * 9, x - 10, x + 11 };
        double d[10] = { 0.5, (double)(float)x, 1.25, 2.0, 0.75, (double)x, 3.5, 4.5, 0.25, 8.0 };
        double s = 0;
        for (int i = 0; i < 10; i++) {
            s = s * 2.0 + (double)k[i] + d[i];
        }
        REQUIRE(((DI *)fn(&p, "pairs"))(CONTEXT, x) == s);

        REQUIRE(vprobe_agrees(&p, x, CONVENTION_HOST));

        int64_t point = ((3 * x + 2) * 10 + 2 * x) * 10 + x + 5;
        REQUIRE(((I1 *)fn(&p, "vpoint"))(CONTEXT, x) == point * 10 + x - 9);
        REQUIRE(named_then_variadic(&p, x));
    }
    release_program(&p);

    REQUIRE(check_text(&p, VARIADIC_CALLS, false, false));
    REQUIRE(jit_compile(&p.n, &p.c, CONVENTION_APPLE, 0));
    for (int64_t x = 0; x < 20; x++) {
        REQUIRE(vprobe_agrees(&p, x, CONVENTION_APPLE));
        REQUIRE(named_then_variadic(&p, x));
    }
    release_program(&p);
    return true;
}

static const char CALLBACKS[] =
    "struct P { a: int, b: int }\n"
    "struct W { a: int, b: int, c: int }\n"
    "struct V3 { x: f32, y: f32, z: f32 }\n"
    "struct V4 { v: [4]f32 }\n"
    "struct Hook { f: extern fn(x: int) -> int, n: int }\n"
    "\n"
    "extern fn qsort(base: *[8]int, n: u64, size: u64, cmp: extern fn(a: *int, b: *int) -> i32)\n"
    "extern fn chibi_c_twice(x: int) -> int\n"
    "extern fn chibi_c_each(f: extern fn(x: int) -> int, n: int) -> int\n"
    "extern fn chibi_c_same(a: extern fn(x: int) -> int, b: extern fn(x: int) -> int) -> bool\n"
    "extern fn chibi_c_note_sp()\n"
    "extern fn chibi_c_swap_hook(h: ?extern fn(x: int) -> int) -> ?extern fn(x: int) -> int\n"
    "extern fn chibi_c_fire_hook(x: int) -> int\n"
    "\n"
    "fn ascending(a: *int, b: *int) -> i32 {\n"
    "    if a.* < b.* {\n"
    "        return -1\n"
    "    }\n"
    "    if a.* > b.* {\n"
    "        return 1\n"
    "    }\n"
    "    return 0\n"
    "}\n"
    "\n"
    "fn descending(a: *int, b: *int) -> i32 {\n"
    "    return ascending(b, a)\n"
    "}\n"
    "\n"
    "fn sort(p: *[8]int, up: bool) {\n"
    "    if up {\n"
    "        qsort(p, 8, 8, ascending)\n"
    "    } else {\n"
    "        qsort(p, 8, 8, descending)\n"
    "    }\n"
    "}\n"
    "\n"
    "fn tap(x: int) -> int {\n"
    "    return x * 3 + 1\n"
    "}\n"
    "\n"
    "fn each(n: int) -> int {\n"
    "    return chibi_c_each(tap, n) * 1000 + chibi_c_each(chibi_c_twice, n)\n"
    "}\n"
    "\n"
    "fn hooked(n: int) -> int {\n"
    "    f: extern fn(x: int) -> int = tap\n"
    "    h := Hook.{ .f = f, .n = n + 1 }\n"
    "    return chibi_c_each(h.f, h.n)\n"
    "}\n"
    "\n"
    "fn same(x: int) -> int {\n"
    "    s := x\n"
    "    if chibi_c_same(tap, tap) {\n"
    "        s += 1\n"
    "    }\n"
    "    if chibi_c_same(tap, chibi_c_twice) {\n"
    "        s += 10\n"
    "    }\n"
    "    f: extern fn(x: int) -> int = tap\n"
    "    if f == get_tap() {\n"
    "        s += 100\n"
    "    }\n"
    "    if f != get_twice() {\n"
    "        s += 1000\n"
    "    }\n"
    "    return s\n"
    "}\n"
    "\n"
    "fn get_tap() -> extern fn(x: int) -> int {\n"
    "    return tap\n"
    "}\n"
    "\n"
    "fn get_twice() -> extern fn(x: int) -> int {\n"
    "    return chibi_c_twice\n"
    "}\n"
    "\n"
    "fn hooks(n: int) -> int {\n"
    "    old := chibi_c_swap_hook(tap)\n"
    "    a := chibi_c_fire_hook(n)\n"
    "    mine := chibi_c_swap_hook(old)\n"
    "    b := chibi_c_fire_hook(n)\n"
    "    c := 0\n"
    "    if mine != nil {\n"
    "        c = chibi_c_each(mine, 2)\n"
    "        if mine == get_tap() {\n"
    "            c += 50\n"
    "        }\n"
    "    }\n"
    "    _ = chibi_c_swap_hook(bitcast(?extern fn(x: int) -> int, int(1)))\n"
    "    back := chibi_c_swap_hook(old)\n"
    "    return a * 1000000 + b * 10000 + c * 100 + bitcast(int, back)\n"
    "}\n"
    "\n"
    "fn seven(a: int, b: i8, c: u16, d: i32, e: u32, f: bool, g: *int) -> int {\n"
    "    s := a + int(b) * 10 + int(c) * 1000 + int(d) * 100000 + int(e) * 10000000 + g.*\n"
    "    if f {\n"
    "        s = -s\n"
    "    }\n"
    "    return s\n"
    "}\n"
    "\n"
    "fn get_seven() -> extern fn(a: int, b: i8, c: u16, d: i32, e: u32, f: bool, g: *int) -> int {\n"
    "    return seven\n"
    "}\n"
    "\n"
    "fn eight(a: int, b: int, c: int, d: int, e: int, f: int, g: int, h: i16) -> int {\n"
    "    chibi_c_note_sp()\n"
    "    return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f + 7 * g + 8 * int(h)\n"
    "}\n"
    "\n"
    "fn get_eight() -> extern fn(a: int, b: int, c: int, d: int, e: int, f: int, g: int, h: i16) -> int {\n"
    "    return eight\n"
    "}\n"
    "\n"
    "fn pair_late(a: int, b: int, c: int, d: int, e: int, f: int, p: P) -> int {\n"
    "    return a + b + c + d + e + f * 10 + p.a * 100 + p.b * 1000\n"
    "}\n"
    "\n"
    "fn get_pair_late() -> extern fn(a: int, b: int, c: int, d: int, e: int, f: int, p: P) -> int {\n"
    "    return pair_late\n"
    "}\n"
    "\n"
    "fn big(w: W, v: V3, k: int) -> f64 {\n"
    "    return f64(w.a + 2 * w.b + 3 * w.c + k) + f64(v.x) + 2.0 * f64(v.y) + 4.0 * f64(v.z)\n"
    "}\n"
    "\n"
    "fn get_big() -> extern fn(w: W, v: V3, k: int) -> f64 {\n"
    "    return big\n"
    "}\n"
    "\n"
    "fn mixed(a: int, b: int, c: int, d: int, e: int, f: int, g: int, p: f64, q: f64, r: f64, s: "
    "f64, t: f64, u: f64, v: f64, w: f64, y: f64, h: int, k: i16, m: i8) -> f64 {\n"
    "    n := a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f + 7 * g + 8 * h + 9 * int(k) + 10 * int(m)\n"
    "    return f64(n) + p + 2.0 * q + 3.0 * r + 4.0 * s + 5.0 * t + 6.0 * u + 7.0 * v + 8.0 * w + "
    "9.0 * y\n"
    "}\n"
    "\n"
    "fn get_mixed() -> extern fn(a: int, b: int, c: int, d: int, e: int, f: int, g: int, p: f64, "
    "q: f64, r: f64, s: f64, t: f64, u: f64, v: f64, w: f64, y: f64, h: int, k: i16, m: i8) -> f64 {\n"
    "    return mixed\n"
    "}\n"
    "\n"
    "fn hfa_late(a: int, b: int, c: int, d: int, e: int, f: int, g: int, h: int, p: f64, q: f64, "
    "r: f64, s: f64, t: f64, u: f64, v: V3) -> f64 {\n"
    "    n := a + b + c + d + e + f + g + 8 * h\n"
    "    return f64(n) + p + q + r + s + t + 6.0 * u + f64(v.x) + 2.0 * f64(v.y) + 4.0 * f64(v.z)\n"
    "}\n"
    "\n"
    "fn get_hfa_late() -> extern fn(a: int, b: int, c: int, d: int, e: int, f: int, g: int, h: "
    "int, p: f64, q: f64, r: f64, s: f64, t: f64, u: f64, v: V3) -> f64 {\n"
    "    return hfa_late\n"
    "}\n"
    "\n"
    "fn hfa4_late(a: f64, b: f64, c: f64, d: f64, e: f64, f: f64, v: V4) -> f64 {\n"
    "    n := a + 2.0 * b + 3.0 * c + 4.0 * d + 5.0 * e + 6.0 * f\n"
    "    return n + f64(v.v[0]) + 10.0 * f64(v.v[1]) + 100.0 * f64(v.v[2]) + 1000.0 * f64(v.v[3])\n"
    "}\n"
    "\n"
    "fn get_hfa4_late() -> extern fn(a: f64, b: f64, c: f64, d: f64, e: f64, f: f64, v: V4) -> f64 {\n"
    "    return hfa4_late\n"
    "}\n"
    "\n"
    "fn pair_result(a: int) -> P {\n"
    "    return P.{ a, a * 2 }\n"
    "}\n"
    "\n"
    "fn get_pair_result() -> extern fn(a: int) -> P {\n"
    "    return pair_result\n"
    "}\n"
    "\n"
    "fn big_small(a: int) -> W {\n"
    "    return W.{ a, a + 1, a + 2 }\n"
    "}\n"
    "\n"
    "fn get_big_small() -> extern fn(a: int) -> W {\n"
    "    return big_small\n"
    "}\n"
    "\n"
    "fn big_result(a: int, b: int, c: int, d: int, e: int, f: int, g: int, h: int) -> W {\n"
    "    return W.{ a + h, b * g, c - f + d * e }\n"
    "}\n"
    "\n"
    "fn get_big_result() -> extern fn(a: int, b: int, c: int, d: int, e: int, f: int, g: int, h: "
    "int) -> W {\n"
    "    return big_result\n"
    "}\n";

typedef int64_t FN1(int64_t);
typedef FN1 *GET1(void *);
typedef void SORT(void *, int64_t *, bool);
typedef int64_t FN7(int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t *);
typedef FN7 *GET7(void *);
typedef int64_t FN8(int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t);
typedef FN8 *GET8(void *);
typedef int64_t FNPL(int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, CPoint);
typedef FNPL *GETPL(void *);
typedef double FNBIG(CW, CV3, int64_t);
typedef FNBIG *GETBIG(void *);
typedef double FNMIX(int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, double, double, double, double, double, double, double, double, double, int64_t, int64_t, int64_t);
typedef FNMIX *GETMIX(void *);
typedef double FNMIXA(int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, double, double, double, double, double, double, double, double, double, int64_t, uint64_t);
typedef FNMIXA *GETMIXA(void *);
typedef double FNHFA(int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, double, double, double, double, double, double, CV3);
typedef FNHFA *GETHFA(void *);
typedef double FNHFA4(double, double, double, double, double, double, CV4);
typedef FNHFA4 *GETHFA4(void *);
typedef CPoint FNPR(int64_t);
typedef FNPR *GETPR(void *);
typedef CW FNW(int64_t);
typedef FNW *GETW(void *);
typedef CW FNW8(int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t);
typedef FNW8 *GETW8(void *);

int64_t
chibi_c_twice(int64_t x)
{
    return 2 * x;
}

int64_t
chibi_c_each(FN1 *f, int64_t n)
{
    int64_t s = 0;
    for (int64_t i = 0; i < n; i++) {
        s = s * 3 + f(i);
    }
    return s;
}

bool
chibi_c_same(FN1 *a, FN1 *b)
{
    return a == b;
}

static FN1 *c_hook;

FN1 *
chibi_c_swap_hook(FN1 *h)
{
    FN1 *old = c_hook;
    c_hook = h;
    return old;
}

int64_t
chibi_c_fire_hook(int64_t x)
{
    return c_hook ? c_hook(x) : -1;
}

static bool sp_misaligned;

void
chibi_c_note_sp(void)
{
    uintptr_t sp;
    __asm__ volatile("mov %0, sp" : "=r"(sp));
    sp_misaligned |= (sp & 15) != 0;
}

static int64_t
fold_each(int64_t n, int64_t mul, int64_t add)
{
    int64_t s = 0;
    for (int64_t i = 0; i < n; i++) {
        s = s * 3 + i * mul + add;
    }
    return s;
}

static uint8_t DEFAULT_CONTEXT[16];
static const void *tap_context;

static int64_t
c_tap(void *context, int64_t x)
{
    tap_context = context;
    return x + 100;
}

static bool
sorts(Program *p, int64_t x)
{
    int64_t xs[8] = { 5, -3, 9, 0, 9, -7, x, 2 - x };
    ((SORT *)fn(p, "sort"))(CONTEXT, xs, true);
    bool ok = true;
    for (int i = 1; i < 8; i++) {
        ok &= xs[i - 1] <= xs[i];
    }
    ((SORT *)fn(p, "sort"))(CONTEXT, xs, false);
    for (int i = 1; i < 8; i++) {
        ok &= xs[i - 1] >= xs[i];
    }
    return ok && xs[0] == (x > 9 ? x : 9);
}

static const uint64_t JUNK = 0x5A5A5A5A5A5A5A5Aull;

static bool
odd_layouts(Program *p, int64_t x)
{
    int64_t g = x * 77;
    int8_t b = (int8_t)(x - 5);
    uint16_t c = (uint16_t)(x * 3000 + 1);
    int32_t d = (int32_t)(x * -100000);
    uint32_t e = (uint32_t)(x * 200000000);
    int64_t s = x + b * 10 + c * 1000LL + d * 100000LL + e * 10000000LL + g;
    FN7 *seven = ((GET7 *)fn(p, "get_seven"))(CONTEXT);
    bool ok = seven(x, (int64_t)(JUNK << 8 | (uint8_t)b), (int64_t)(JUNK << 16 | c), (int64_t)(JUNK << 32 | (uint32_t)d), (int64_t)(JUNK << 32 | e), (int64_t)(JUNK << 8 | (x & 1)), &g) == ((x & 1) ? -s : s);

    int16_t h = (int16_t)(x * -700);
    FN8 *eight = ((GET8 *)fn(p, "get_eight"))(CONTEXT);
    ok &= eight(x, 1, 2, 3, x * 5, 6, 7, (int64_t)(JUNK << 16 | (uint16_t)h)) == x + 2 + 6 + 12 + 25 * x + 36 + 49 + 8 * h;

    FNPL *pair_late = ((GETPL *)fn(p, "get_pair_late"))(CONTEXT);
    CPoint pt = { x * 7, x - 3 };
    ok &= pair_late(1, 2, 3, 4, x, 6, pt) == 10 + x + 60 + 700 * x + 1000 * (x - 3);

    FNBIG *big = ((GETBIG *)fn(p, "get_big"))(CONTEXT);
    CW w = { x, x + 1, x + 2 };
    CV3 v = { (float)x + 0.5f, 0.25f, -1.0f };
    ok &= big(w, v, x * 9) == (double)(x + 2 * (x + 1) + 3 * (x + 2) + 9 * x) + ((double)x + 0.5) + 0.5 - 4.0;

    FNHFA *hfa_late = ((GETHFA *)fn(p, "get_hfa_late"))(CONTEXT);
    double want = (double)(1 + 2 + 3 + 4 + 5 + 6 + x + 8 * (x - 4)) + 0.5 + 1.5 + 2.5 + 3.5 + 4.5 + 6.0 * (double)x + ((double)x + 0.5) + 0.5 - 4.0;
    ok &= hfa_late(1, 2, 3, 4, 5, 6, x, x - 4, 0.5, 1.5, 2.5, 3.5, 4.5, (double)x, v) == want;

    FNHFA4 *hfa4_late = ((GETHFA4 *)fn(p, "get_hfa4_late"))(CONTEXT);
    CV4 v4 = { { 0.5f, (float)x, -2.0f, 0.25f } };
    ok &= hfa4_late(1.0, 2.0, 3.0, 4.0, 5.0, (double)x, v4) == 55.0 + 6.0 * (double)x + 0.5 + 10.0 * (double)x - 200.0 + 250.0;

    FNPR *pair_result = ((GETPR *)fn(p, "get_pair_result"))(CONTEXT);
    CPoint r = pair_result(x - 9);
    ok &= r.x == x - 9 && r.y == 2 * (x - 9);

    FNW *big_small = ((GETW *)fn(p, "get_big_small"))(CONTEXT);
    CW s3 = big_small(x * 4);
    ok &= s3.a == x * 4 && s3.b == x * 4 + 1 && s3.c == x * 4 + 2;

    FNW8 *big_result = ((GETW8 *)fn(p, "get_big_result"))(CONTEXT);
    CW s8 = big_result(x, 2, 3, 4, 5, 6, 7, 8);
    ok &= s8.a == x + 8 && s8.b == 14 && s8.c == 3 - 6 + 20;
    return ok;
}

static double
mixed_value(int64_t x, int16_t k, int8_t m)
{
    int64_t n = 1 + 2 * 2 + 3 * 3 + 4 * 4 + 5 * 5 + 6 * 6 + 7 * x + 8 * 8 + 9 * k + 10 * m;
    return (double)n + 0.5 + 2.0 + 3.0 * 0.25 + 4.0 + 5.0 * 1.5 + 6.0 * (double)x + 7.0 + 8.0 * 0.125 + 9.0 * (double)(x - 3);
}

static bool
mixed_agrees(Program *p, int64_t x, Convention convention)
{
    int16_t k = (int16_t)(x * -300);
    int8_t m = (int8_t)(x - 11);
    if (convention == CONVENTION_APPLE) {
        uint64_t packed = (uint16_t)k | (uint64_t)(uint8_t)m << 16 | JUNK << 24;
        FNMIXA *mixed = ((GETMIXA *)fn(p, "get_mixed"))(CONTEXT);
        return mixed(1, 2, 3, 4, 5, 6, x, 0.5, 1.0, 0.25, 1.0, 1.5, (double)x, 1.0, 0.125, (double)(x - 3), 8, packed) == mixed_value(x, k, m);
    }
    FNMIX *mixed = ((GETMIX *)fn(p, "get_mixed"))(CONTEXT);
    return mixed(1, 2, 3, 4, 5, 6, x, 0.5, 1.0, 0.25, 1.0, 1.5, (double)x, 1.0, 0.125, (double)(x - 3), 8, (int64_t)(JUNK << 16 | (uint16_t)k), (int64_t)(JUNK << 8 | (uint8_t)m)) == mixed_value(x, k, m);
}

TEST_CASE(callbacks)
{
    Program p;
    REQUIRE(build(&p, CALLBACKS));
    native_set_context(&p.n, DEFAULT_CONTEXT);
    for (int64_t x = 0; x < 20; x++) {
        REQUIRE(sorts(&p, x));
        REQUIRE(((I1 *)fn(&p, "each"))(CONTEXT, x % 6) == fold_each(x % 6, 3, 1) * 1000 + fold_each(x % 6, 2, 0));
        REQUIRE(((I1 *)fn(&p, "hooked"))(CONTEXT, x % 5) == fold_each(x % 5 + 1, 3, 1));
        REQUIRE(((I1 *)fn(&p, "same"))(CONTEXT, x) == x + 1101);
        REQUIRE(((I1 *)fn(&p, "hooks"))(CONTEXT, x % 7) == (3 * (x % 7) + 1) * 1000000 - 10000 + 5701);
        REQUIRE(odd_layouts(&p, x));

        REQUIRE(mixed_agrees(&p, x, CONVENTION_HOST));
    }
    REQUIRE(!sp_misaligned);
    REQUIRE(((GET1 *)fn(&p, "get_twice"))(CONTEXT) == chibi_c_twice);
    FN1 *tapped = ((GET1 *)fn(&p, "get_tap"))(CONTEXT);
    REQUIRE(tapped == ((GET1 *)fn(&p, "get_tap"))(CONTEXT));
    REQUIRE(tapped != chibi_c_twice && tapped(5) == 16);

    native_replace(&p.n, decl_named(&p, "tap"), (void *)c_tap);
    tap_context = NULL;
    REQUIRE(tapped(5) == 105 && tap_context == DEFAULT_CONTEXT);
    REQUIRE(((I1 *)fn(&p, "each"))(CONTEXT, 4) == fold_each(4, 1, 100) * 1000 + fold_each(4, 2, 0));
    REQUIRE(((GET1 *)fn(&p, "get_tap"))(CONTEXT) == tapped);
    release_program(&p);

    REQUIRE(check_text(&p, CALLBACKS, false, false));
    REQUIRE(jit_compile(&p.n, &p.c, CONVENTION_APPLE, 0));
    for (int64_t x = 0; x < 20; x++) {
        REQUIRE(sorts(&p, x));
        REQUIRE(odd_layouts(&p, x));

        REQUIRE(mixed_agrees(&p, x, CONVENTION_APPLE));
    }
    REQUIRE(!sp_misaligned);
    release_program(&p);
    return true;
}

TEST_CASE(callback_stack_limit)
{
    static char text[16384];
    int at = snprintf(text, sizeof text, "fn many(");
    for (int i = 0; i < 520; i++) {
        at += snprintf(text + at, sizeof text - (size_t)at, "%sa%d: int", i ? ", " : "", i);
    }
    at += snprintf(text + at, sizeof text - (size_t)at, ") -> int {\n    return a0\n}\nextern fn take(f: extern fn(");
    for (int i = 0; i < 520; i++) {
        at += snprintf(text + at, sizeof text - (size_t)at, "%sint", i ? ", " : "");
    }
    snprintf(text + at, sizeof text - (size_t)at, ") -> int)\nfn give() {\n    take(many)\n}\n");

    Program p;
    REQUIRE(check_text(&p, text, false, false));
    REQUIRE(!jit_compile(&p.n, &p.c, CONVENTION_HOST, 0));
    REQUIRE(strcmp(p.n.error, "a callback for C needs over 4064 bytes of stack arguments") == 0);
    release_program(&p);
    return true;
}

static const char EXPORTS[] =
    "extern fn chibi_c_each(f: extern fn(x: int) -> int, n: int) -> int\n"
    "\n"
    "export fn add3(a: int, b: i32, c: u8) -> int {\n"
    "    return a + int(b) * 10 + int(c) * 1000\n"
    "}\n"
    "\n"
    "export fn nine(a: int, b: int, c: int, d: int, e: int, f: int, g: int, h: int, i: int) -> int {\n"
    "    return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f + 7 * g + 8 * h + 9 * i\n"
    "}\n"
    "\n"
    "export fn bump(p: *int) {\n"
    "    p.* += 1\n"
    "}\n"
    "\n"
    "export fn half(x: f64) -> f64 {\n"
    "    return x * 0.5\n"
    "}\n"
    "\n"
    "export fn step(x: int) -> int {\n"
    "    return 2 * x + 1\n"
    "}\n"
    "\n"
    "fn uses(n: int) -> int {\n"
    "    return chibi_c_each(step, n) * 1000 + step(n)\n"
    "}\n"
    "\n"
    "fn get_step() -> extern fn(x: int) -> int {\n"
    "    return step\n"
    "}\n";

typedef int64_t ADD3(int64_t, int64_t, int64_t);
typedef int64_t NINE(int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t);
typedef void BUMP(int64_t *);
typedef double HALF(double);

static void *
export_named(Program *p, const char *name)
{
    u32 decl = decl_named(p, name);
    for (u32 i = 0; i < p->c.ir.nexports; i++) {
        if (p->c.ir.callbacks[i] == decl) {
            return native_export(&p->n, i);
        }
    }
    fprintf(stderr, "arm64 test: `%s` is not exported\n", name);
    abort();
}

TEST_CASE(exports)
{
    Program p;
    REQUIRE(build(&p, EXPORTS));
    native_set_context(&p.n, DEFAULT_CONTEXT);
    ADD3 *add3 = (ADD3 *)export_named(&p, "add3");
    NINE *nine = (NINE *)export_named(&p, "nine");
    BUMP *bump = (BUMP *)export_named(&p, "bump");
    HALF *half = (HALF *)export_named(&p, "half");
    FN1 *step = (FN1 *)export_named(&p, "step");
    for (int64_t x = 0; x < 20; x++) {
        int32_t b = (int32_t)(x * -1000);
        uint8_t c = (uint8_t)(x * 13);
        REQUIRE(add3(x, (int64_t)(JUNK << 32 | (uint32_t)b), (int64_t)(JUNK << 8 | c)) == x + b * 10LL + c * 1000LL);
        REQUIRE(nine(1, 2, 3, 4, 5, 6, 7, x, 2 * x) == 140 + 8 * x + 18 * x);

        int64_t v = x * 7;
        bump(&v);
        REQUIRE(v == x * 7 + 1);
        REQUIRE(half((double)x) == (double)x * 0.5);
        REQUIRE(step(x) == 2 * x + 1);
        REQUIRE(((I1 *)fn(&p, "uses"))(CONTEXT, x % 6) == fold_each(x % 6, 2, 1) * 1000 + 2 * (x % 6) + 1);
    }
    REQUIRE(((GET1 *)fn(&p, "get_step"))(CONTEXT) == step);

    native_replace(&p.n, decl_named(&p, "step"), (void *)c_tap);
    tap_context = NULL;
    REQUIRE(step(5) == 105 && tap_context == DEFAULT_CONTEXT);
    release_program(&p);
    return true;
}

static const char MANY_POINTERS[] =
    "extern fn chibi_c_text() -> [*]u8\n"
    "extern fn chibi_c_find(s: [*]u8, c: u8) -> [*]u8\n"
    "extern fn chibi_c_count(s: [*]u8) -> int\n"
    "extern fn chibi_c_lengths(n: int, ..) -> int\n"
    "\n"
    "struct P3 { x: i32, y: i32, z: i32 }\n"
    "struct Buf { items: []int, n: int }\n"
    "\n"
    "fn get(p: [*]int, i: int) -> int {\n"
    "    return p[i]\n"
    "}\n"
    "\n"
    "fn put(p: [*]i32, i: i32, v: i32) {\n"
    "    p[i] = v\n"
    "}\n"
    "\n"
    "fn third(p: [*]P3, i: int) -> int {\n"
    "    return int(p[i].y) * 1000 + int((p + 1)[i].z)\n"
    "}\n"
    "\n"
    "fn moved(p: [*]u8, n: u32, m: i8) -> int {\n"
    "    q := p + n\n"
    "    return int(q[m])\n"
    "}\n"
    "\n"
    "fn length(s: [*]u8) -> int {\n"
    "    q := s\n"
    "    n := 0\n"
    "    while q[0] != 0 {\n"
    "        q += 1\n"
    "        n += 1\n"
    "    }\n"
    "    return n\n"
    "}\n"
    "\n"
    "fn after(s: [*]u8, c: u8) -> int {\n"
    "    return length(chibi_c_find(s, c) + 1) * 1000 + chibi_c_count(s)\n"
    "}\n"
    "\n"
    "fn lengths(s: [*]u8) -> int {\n"
    "    return chibi_c_lengths(2, s, s + 2)\n"
    "}\n"
    "\n"
    "fn view(a: int, b: int) -> int {\n"
    "    s := chibi_c_text()[a..b]\n"
    "    return s.len * 1000 + int(s[1])\n"
    "}\n"
    "\n"
    "fn ends(b: int, k: int) -> int {\n"
    "    p := chibi_c_text()\n"
    "    s := p[..b]\n"
    "    t := p[1..=b]\n"
    "    return s.len * 100 + t.len + int(t[k])\n"
    "}\n"
    "\n"
    "fn fixed(p: [*]u8) -> []u8 {\n"
    "    return p[2..5]\n"
    "}\n"
    "\n"
    "fn first(s: []int) -> [*]int {\n"
    "    return s.ptr\n"
    "}\n"
    "\n"
    "fn items(b: *Buf) -> [*]int {\n"
    "    return b.items.ptr + b.n\n"
    "}\n"
    "\n"
    "fn local(k: int) -> int {\n"
    "    a := int.[1, 2, 3, 4]\n"
    "    p: [*]int = &a\n"
    "    q := p + 2\n"
    "    q[k] = 50\n"
    "    return a[2] * 100 + a[3] + p[1] * 10000\n"
    "}\n"
    "\n"
    "fn same(p: [*]int, q: [*]int) -> bool {\n"
    "    return p == q\n"
    "}\n"
    "\n"
    "export fn main(argc: i32, argv: [*][*]u8) -> i32 {\n"
    "    n := 0\n"
    "    for i in 0..argc {\n"
    "        n += length(argv[i])\n"
    "    }\n"
    "    return i32(n) * 100 + argc\n"
    "}\n";

static const char TOP_LEVEL[] =
    "import \"std/os\"\n"
    "\n"
    "fn length(s: cstring) -> int {\n"
    "    p: [*]u8 = s\n"
    "    n := 0\n"
    "    while p[n] != 0 {\n"
    "        n += 1\n"
    "    }\n"
    "    return n\n"
    "}\n"
    "\n"
    "fn count() -> int {\n"
    "    return os.args.len\n"
    "}\n"
    "\n"
    "fn skip() {\n"
    "    os.args = os.args[1..]\n"
    "}\n"
    "\n"
    "fn skipped() -> int {\n"
    "    skip()\n"
    "    return count()\n"
    "}\n"
    "\n"
    "fn pick(a: []cstring, n: int) -> int {\n"
    "    return a.len * 10 + n\n"
    "}\n"
    "\n"
    "total := 0\n"
    "for a in os.args {\n"
    "    total += length(a)\n"
    "}\n"
    "if total == 0 {\n"
    "    return\n"
    "}\n"
    "both := pick(os.args, skipped())\n"
    "if count() == 0 {\n"
    "    return -1\n"
    "}\n"
    "return i32(total) * 1000 + i32(both)\n";

static const char *const MANY_GRID_FUNCTIONS[] = {
    "walk_many", "cut_many", "ends_many", "ptr_many", "same_many",
};

enum
{
    WORD_B_COND = 0x54000000u,
    B_COND_FIELDS = 0x00FFFFEFu,
};

typedef struct
{
    int32_t x, y, z;
} P3;

typedef struct
{
    Slice items;
    int64_t n;
} ManyBuf;

typedef int64_t MGET(void *, const int64_t *, int64_t);
typedef void MPUT(void *, int32_t *, int64_t, int64_t);
typedef int64_t MTHIRD(void *, const P3 *, int64_t);
typedef int64_t MMOVED(void *, const char *, uint64_t, uint64_t);
typedef int64_t MLEN(void *, const char *);
typedef int64_t MAFTER(void *, const char *, uint64_t);
typedef Slice MFIXED(void *, const char *);
typedef const int64_t *MFIRST(void *, Slice);
typedef const int64_t *MITEMS(void *, const ManyBuf *);
typedef bool MSAME(void *, const int64_t *, const int64_t *);
typedef int32_t MAIN(int32_t, char **);

static const char MANY_TEXT[] = "0123456789";

const char *
chibi_c_text(void)
{
    return MANY_TEXT;
}

const char *
chibi_c_find(const char *s, uint8_t c)
{
    return strchr(s, c);
}

int64_t
chibi_c_count(const char *s)
{
    return (int64_t)strlen(s);
}

int64_t
chibi_c_lengths(int64_t n, ...)
{
    va_list ap;
    va_start(ap, n);
    int64_t total = 0;
    for (int64_t i = 0; i < n; i++) {
        total += (int64_t)strlen(va_arg(ap, const char *));
    }
    va_end(ap);
    return total;
}

TEST_CASE(many_pointers)
{
    catch_traps();
    Program p;
    REQUIRE(build(&p, MANY_POINTERS));
    native_set_context(&p.n, DEFAULT_CONTEXT);

    int64_t ints[] = { 10, 20, 30, 40 };
    REQUIRE(((MGET *)fn(&p, "get"))(CONTEXT, ints, 2) == 30);
    REQUIRE(((MGET *)fn(&p, "get"))(CONTEXT, ints + 3, -3) == 10);
    REQUIRE(count_words(&p, "get", WORD_B_COND, B_COND_FIELDS) == 0);

    int32_t words[] = { 1, 2, 3, 4 };
    ((MPUT *)fn(&p, "put"))(
        CONTEXT,
        words + 2,
        (int64_t)(JUNK << 32 | (uint32_t)-1),
        (int64_t)(JUNK << 32 | 77u)
    );
    REQUIRE(words[1] == 77 && words[0] == 1 && words[2] == 3);

    P3 points[] = { { 1, 2, 3 }, { 4, 5, 6 }, { 7, 8, 9 }, { 10, 11, 12 } };
    REQUIRE(((MTHIRD *)fn(&p, "third"))(CONTEXT, points, 1) == 5009);
    REQUIRE(((MTHIRD *)fn(&p, "third"))(CONTEXT, points + 2, -1) == 5009);
    REQUIRE(((MTHIRD *)fn(&p, "third"))(CONTEXT, points, 2) == 8012);

    MMOVED *moved = (MMOVED *)fn(&p, "moved");
    REQUIRE(moved(CONTEXT, MANY_TEXT, JUNK << 32 | 5u, JUNK << 8 | (uint8_t)-2) == '3');
    REQUIRE(moved(CONTEXT, MANY_TEXT, JUNK << 32 | 2u, JUNK << 8 | 7u) == '9');

    REQUIRE(((MLEN *)fn(&p, "length"))(CONTEXT, "chibi") == 5);
    REQUIRE(((MLEN *)fn(&p, "length"))(CONTEXT, "") == 0);
    REQUIRE(((MAFTER *)fn(&p, "after"))(CONTEXT, "usr/lib/x", JUNK << 8 | '/') == 5009);
    REQUIRE(((MLEN *)fn(&p, "lengths"))(CONTEXT, "chibi") == 8);

    int64_t r = 0;
    REQUIRE(call_i2((I2 *)fn(&p, "view"), 2, 5, &r) == 0 && r == 3051);
    REQUIRE(call_i2((I2 *)fn(&p, "view"), 0, 2, &r) == 0 && r == 2049);
    REQUIRE(call_i2((I2 *)fn(&p, "view"), 4, 3, &r) == TRAP_BOUNDS);
    REQUIRE(call_i2((I2 *)fn(&p, "view"), 0, -1, &r) == TRAP_BOUNDS);
    REQUIRE(call_i2((I2 *)fn(&p, "view"), -1, 2, &r) == TRAP_BOUNDS);
    REQUIRE(call_i2((I2 *)fn(&p, "view"), 3, 4, &r) == TRAP_BOUNDS);
    REQUIRE(call_i2((I2 *)fn(&p, "ends"), 5, 0, &r) == 0 && r == 554);
    REQUIRE(call_i2((I2 *)fn(&p, "ends"), 9, 8, &r) == 0 && r == 966);
    REQUIRE(call_i2((I2 *)fn(&p, "ends"), -1, 0, &r) == TRAP_BOUNDS);
    REQUIRE(call_i2((I2 *)fn(&p, "ends"), 3, 3, &r) == TRAP_BOUNDS);
    REQUIRE(call_i2((I2 *)fn(&p, "ends"), INT64_MAX, 0, &r) == TRAP_OVERFLOW);

    Slice fixed = ((MFIXED *)fn(&p, "fixed"))(CONTEXT, MANY_TEXT);
    REQUIRE(fixed.ptr == MANY_TEXT + 2 && fixed.len == 3);
    REQUIRE(((MFIRST *)fn(&p, "first"))(CONTEXT, (Slice){ ints + 1, 2 }) == ints + 1);
    ManyBuf buf = { { ints, 4 }, 3 };
    REQUIRE(((MITEMS *)fn(&p, "items"))(CONTEXT, &buf) == ints + 3);

    I1 *local = (I1 *)fn(&p, "local");
    REQUIRE(local(CONTEXT, 0) == 25004);
    REQUIRE(local(CONTEXT, 1) == 20350);
    REQUIRE(local(CONTEXT, -1) == 500304);
    REQUIRE(local(CONTEXT, -2) == 20304);

    MSAME *same = (MSAME *)fn(&p, "same");
    REQUIRE(same(CONTEXT, ints, ints) && !same(CONTEXT, ints, ints + 1));

    char a0[] = "chibi", a1[] = "run", a2[] = "x";
    char *argv[] = { a0, a1, a2, NULL };
    MAIN *entry = (MAIN *)export_named(&p, "main");
    REQUIRE(entry(3, argv) == 903);
    REQUIRE(entry(1, argv) == 501);
    REQUIRE(entry(0, argv) == 0);
    release_program(&p);

    REQUIRE(build(&p, MANY_GRID));
    REQUIRE(agree_on_grid(&p, MANY_GRID_FUNCTIONS, sizeof MANY_GRID_FUNCTIONS / sizeof MANY_GRID_FUNCTIONS[0]));
    release_program(&p);
    return true;
}

static const char *const BIT_GRID_FUNCTIONS[] = {
    "grid_int", "grid_u64", "grid_u32", "grid_i32",
    "grid_u16", "grid_i16", "grid_u8", "grid_i8",
};

enum
{
    WORD_RBIT_X = 0xDAC00000u,
    WORD_CLZ_X = 0xDAC01000u,
    WORD_CLZ_W = 0x5AC01000u,
    WORD_SUB_W_16 = 0x51004000u,
    WORD_CNT_8B = 0x0E205800u,
    WORD_ADDV_8B = 0x0E31B800u,
    REGISTER_FIELDS = 0x3FFu,
};

TEST_CASE(bit_counts)
{
    catch_traps();
    Program p;
    REQUIRE(build(&p, BIT_COUNTS));

    I1 *u8s = (I1 *)fn(&p, "pack_u8");
    REQUIRE(u8s(CONTEXT, (int64_t)(JUNK << 8)) == 808);
    REQUIRE(u8s(CONTEXT, (int64_t)(JUNK << 8 | 1u)) == 10700);
    REQUIRE(u8s(CONTEXT, (int64_t)(JUNK << 8 | 0x80u)) == 10007);
    REQUIRE(u8s(CONTEXT, (int64_t)(JUNK << 8 | 0xFFu)) == 80000);
    REQUIRE(u8s(CONTEXT, (int64_t)(JUNK << 8 | 0x14u)) == 20302);

    I1 *i8s = (I1 *)fn(&p, "pack_i8");
    REQUIRE(i8s(CONTEXT, (int64_t)(JUNK << 8 | 0x80u)) == 10007);
    REQUIRE(i8s(CONTEXT, (int64_t)(JUNK << 8 | 0xFFu)) == 80000);
    REQUIRE(i8s(CONTEXT, (int64_t)(JUNK << 8)) == 808);
    REQUIRE(i8s(CONTEXT, (int64_t)(JUNK << 8 | 0x40u)) == 10106);
    REQUIRE(i8s(CONTEXT, (int64_t)(JUNK << 8 | 0x14u)) == 20302);

    I1 *u16s = (I1 *)fn(&p, "pack_u16");
    REQUIRE(u16s(CONTEXT, (int64_t)(JUNK << 16)) == 1616);
    REQUIRE(u16s(CONTEXT, (int64_t)(JUNK << 16 | 1u)) == 11500);
    REQUIRE(u16s(CONTEXT, (int64_t)(JUNK << 16 | 0x8000u)) == 10015);
    REQUIRE(u16s(CONTEXT, (int64_t)(JUNK << 16 | 0xFFFFu)) == 160000);
    REQUIRE(u16s(CONTEXT, (int64_t)(JUNK << 16 | 0x0F00u)) == 40408);

    I1 *i16s = (I1 *)fn(&p, "pack_i16");
    REQUIRE(i16s(CONTEXT, (int64_t)(JUNK << 16 | 0x8000u)) == 10015);
    REQUIRE(i16s(CONTEXT, (int64_t)(JUNK << 16 | 0xFFFFu)) == 160000);
    REQUIRE(i16s(CONTEXT, (int64_t)(JUNK << 16 | 0xFFFEu)) == 150001);
    REQUIRE(i16s(CONTEXT, (int64_t)(JUNK << 16)) == 1616);

    I1 *u32s = (I1 *)fn(&p, "pack_u32");
    REQUIRE(u32s(CONTEXT, (int64_t)(JUNK << 32)) == 3232);
    REQUIRE(u32s(CONTEXT, (int64_t)(JUNK << 32 | 1u)) == 13100);
    REQUIRE(u32s(CONTEXT, (int64_t)(JUNK << 32 | 0x80000000u)) == 10031);
    REQUIRE(u32s(CONTEXT, (int64_t)(JUNK << 32 | 0xFFFFFFFFu)) == 320000);
    REQUIRE(u32s(CONTEXT, (int64_t)(JUNK << 32 | 0x00F0F000u)) == 80812);

    I1 *i32s = (I1 *)fn(&p, "pack_i32");
    REQUIRE(i32s(CONTEXT, (int64_t)(JUNK << 32 | 0x80000000u)) == 10031);
    REQUIRE(i32s(CONTEXT, (int64_t)(JUNK << 32 | 0xFFFFFFFFu)) == 320000);
    REQUIRE(i32s(CONTEXT, (int64_t)(JUNK << 32)) == 3232);
    REQUIRE(i32s(CONTEXT, (int64_t)(JUNK << 32 | 0x40000000u)) == 10130);

    I1 *u64s = (I1 *)fn(&p, "pack_u64");
    REQUIRE(u64s(CONTEXT, 0) == 6464);
    REQUIRE(u64s(CONTEXT, 1) == 16300);
    REQUIRE(u64s(CONTEXT, INT64_MIN) == 10063);
    REQUIRE(u64s(CONTEXT, -1) == 640000);
    REQUIRE(u64s(CONTEXT, 0x0000F00000000000) == 41644);

    I1 *ints = (I1 *)fn(&p, "pack_int");
    REQUIRE(ints(CONTEXT, INT64_MIN) == 10063);
    REQUIRE(ints(CONTEXT, -1) == 640000);
    REQUIRE(ints(CONTEXT, 0) == 6464);
    REQUIRE(ints(CONTEXT, 0x00FF000000000000) == 80848);

    REQUIRE(((I1 *)fn(&p, "tz64"))(CONTEXT, INT64_MIN) == 63);
    REQUIRE(((I1 *)fn(&p, "ones64"))(CONTEXT, -1) == 64);
    REQUIRE(((I1 *)fn(&p, "lz16"))(CONTEXT, (int64_t)(JUNK << 16 | 0x00F0u)) == 8);
    REQUIRE(count_words(&p, "tz64", WORD_RBIT_X, REGISTER_FIELDS) == 1);
    REQUIRE(count_words(&p, "tz64", WORD_CLZ_X, REGISTER_FIELDS) == 1);
    REQUIRE(count_words(&p, "tz64", WORD_B_COND, B_COND_FIELDS) == 0);
    REQUIRE(count_words(&p, "ones64", WORD_CNT_8B, REGISTER_FIELDS) == 1);
    REQUIRE(count_words(&p, "ones64", WORD_ADDV_8B, REGISTER_FIELDS) == 1);
    REQUIRE(count_words(&p, "lz16", WORD_CLZ_W, REGISTER_FIELDS) == 1);
    REQUIRE(count_words(&p, "lz16", WORD_SUB_W_16, REGISTER_FIELDS) == 1);
    release_program(&p);

    REQUIRE(build(&p, BIT_GRID));
    REQUIRE(agree_on_grid(&p, BIT_GRID_FUNCTIONS, sizeof BIT_GRID_FUNCTIONS / sizeof BIT_GRID_FUNCTIONS[0]));
    release_program(&p);
    return true;
}

static const char *const OVERFLOW_GRID_FUNCTIONS[] = {
    "ov_int", "ov_u64", "ov_i32", "ov_u32", "ov_i16", "ov_u16", "ov_i8", "ov_u8",
};

typedef bool B2(void *, int64_t, int64_t);
typedef uint32_t U2(void *, int64_t, int64_t);

enum
{
    WORD_ADDS_X_ZR = 0xAB00001Fu,
    WORD_ADDS_X_ZR_1 = 0xB100041Fu,
    WORD_SUBS_X_ZR_1 = 0xF100041Fu,
    WORD_CMP_X_0 = 0xF100001Fu,
    WORD_CMP_ASR_63 = 0xEB80FC1Fu,
    WORD_CMP_UXTW = 0xEB20401Fu,
    WORD_CMP_SXTB = 0x6B20801Fu,
    WORD_SMULH = 0x9B407C00u,
    WORD_UMULH = 0x9BC07C00u,
    WORD_UMULL = 0x9BA07C00u,
    WORD_MUL_X = 0x9B007C00u,
    WORD_CSET_NE = 0x1A9F07E0u,
    WORD_CSET_VS = 0x1A9F77E0u,
    WORD_CSET_HS = 0x1A9F37E0u,
    SOURCE_FIELDS = 0x001F03E0u,
    THREE_REGISTERS = 0x001F03FFu,
    RN_FIELD = 0x000003E0u,
    RD_FIELD = 0x0000001Fu,
    CSET_FIELDS = 0x0000F01Fu,
};

TEST_CASE(overflow_tests)
{
    catch_traps();
    Program p;
    REQUIRE(build(&p, OVERFLOW_TESTS));

    I2 *carry = (I2 *)fn(&p, "carry");
    REQUIRE(carry(CONTEXT, -1, 1) == 1);
    REQUIRE(carry(CONTEXT, -1, 0) == 0);
    REQUIRE(carry(CONTEXT, INT64_MIN, INT64_MIN) == 1);
    REQUIRE(carry(CONTEXT, INT64_MIN, INT64_MAX) == 0);

    B2 *signed_add = (B2 *)fn(&p, "signed_add");
    REQUIRE(signed_add(CONTEXT, INT64_MAX, 1));
    REQUIRE(!signed_add(CONTEXT, INT64_MAX, 0));
    REQUIRE(signed_add(CONTEXT, INT64_MIN, -1));
    REQUIRE(!signed_add(CONTEXT, INT64_MIN, INT64_MAX));
    REQUIRE(signed_add(CONTEXT, -1, INT64_MIN));

    B2 *mul64 = (B2 *)fn(&p, "mul64");
    REQUIRE(mul64(CONTEXT, 3037000500, 3037000500));
    REQUIRE(!mul64(CONTEXT, 3037000499, 3037000499));
    REQUIRE(mul64(CONTEXT, INT64_MIN, -1));
    REQUIRE(mul64(CONTEXT, -1, INT64_MIN));
    REQUIRE(!mul64(CONTEXT, INT64_MIN, 1));
    REQUIRE(mul64(CONTEXT, 4611686018427387904, 2));
    REQUIRE(!mul64(CONTEXT, -4611686018427387904, 2));

    B2 *umul64 = (B2 *)fn(&p, "umul64");
    REQUIRE(umul64(CONTEXT, 4294967296, 4294967296));
    REQUIRE(!umul64(CONTEXT, 4294967295, 4294967297));
    REQUIRE(!umul64(CONTEXT, -1, 1));
    REQUIRE(umul64(CONTEXT, -1, 2));

    B2 *mulu32 = (B2 *)fn(&p, "mulu32");
    REQUIRE(mulu32(CONTEXT, (int64_t)(JUNK << 32 | 65536u), (int64_t)(JUNK << 32 | 65536u)));
    REQUIRE(!mulu32(CONTEXT, (int64_t)(JUNK << 32 | 65535u), (int64_t)(JUNK << 32 | 65537u)));

    B2 *borrow32 = (B2 *)fn(&p, "borrow32");
    REQUIRE(borrow32(CONTEXT, (int64_t)(JUNK << 32 | 5u), (int64_t)(JUNK << 32 | 6u)));
    REQUIRE(!borrow32(CONTEXT, (int64_t)(JUNK << 32 | 6u), (int64_t)(JUNK << 32 | 6u)));
    REQUIRE(!borrow32(CONTEXT, (int64_t)(JUNK << 32 | 0xFFFFFFFFu), (int64_t)(JUNK << 32)));

    B2 *mul16 = (B2 *)fn(&p, "mul16");
    REQUIRE(!mul16(CONTEXT, (int64_t)(JUNK << 16 | 255u), (int64_t)(JUNK << 16 | 257u)));
    REQUIRE(mul16(CONTEXT, (int64_t)(JUNK << 16 | 256u), (int64_t)(JUNK << 16 | 256u)));

    B2 *sub8 = (B2 *)fn(&p, "sub8");
    REQUIRE(sub8(CONTEXT, (int64_t)(JUNK << 8 | 0x80u), 1));
    REQUIRE(!sub8(CONTEXT, (int64_t)(JUNK << 8 | 0x81u), 1));
    REQUIRE(sub8(CONTEXT, 127, (int64_t)(JUNK << 8 | 0xFFu)));
    REQUIRE(sub8(CONTEXT, 0, (int64_t)(JUNK << 8 | 0x80u)));
    REQUIRE(!sub8(CONTEXT, (int64_t)(JUNK << 8 | 0xFFu), (int64_t)(JUNK << 8 | 0x80u)));

    B1 *add_one = (B1 *)fn(&p, "add_one");
    REQUIRE(add_one(CONTEXT, INT64_MAX));
    REQUIRE(!add_one(CONTEXT, INT64_MAX - 1));
    B1 *add_minus_one = (B1 *)fn(&p, "add_minus_one");
    REQUIRE(add_minus_one(CONTEXT, INT64_MIN));
    REQUIRE(!add_minus_one(CONTEXT, INT64_MIN + 1));

    I2 *checked_add = (I2 *)fn(&p, "checked_add");
    REQUIRE(checked_add(CONTEXT, 40, 2) == 42);
    REQUIRE(checked_add(CONTEXT, INT64_MAX, 1) == (INT64_MIN ^ 1));
    REQUIRE(checked_add(CONTEXT, INT64_MIN, -1) == (INT64_MAX ^ 1));
    U2 *wrapped_mul = (U2 *)fn(&p, "wrapped_mul");
    REQUIRE(wrapped_mul(CONTEXT, (int64_t)(JUNK << 32 | 65536u), (int64_t)(JUNK << 32 | 65536u)) == 0);
    REQUIRE(wrapped_mul(CONTEXT, 65535, 65537) == 4294967295u);
    REQUIRE(wrapped_mul(CONTEXT, 0xFFFFFFFF, 0xFFFFFFFF) == 1);
    I2 *from_pair = (I2 *)fn(&p, "from_pair");
    REQUIRE(from_pair(CONTEXT, 100, 30) == 140);
    REQUIRE(from_pair(CONTEXT, (int64_t)(JUNK << 16 | 0x8000u), 1) == 65535);
    REQUIRE(from_pair(CONTEXT, 32767, -1) == -65535);

    REQUIRE(count_words(&p, "carry", WORD_ADDS_X_ZR, SOURCE_FIELDS) == 1);
    REQUIRE(count_words(&p, "carry", WORD_CSET_HS, RD_FIELD) == 1);
    REQUIRE(count_words(&p, "carry", WORD_CSET_NE, CSET_FIELDS) == 1);
    REQUIRE(count_words(&p, "signed_add", WORD_ADDS_X_ZR, SOURCE_FIELDS) == 1);
    REQUIRE(count_words(&p, "signed_add", WORD_CSET_VS, RD_FIELD) == 1);
    REQUIRE(count_words(&p, "mul64", WORD_SMULH, THREE_REGISTERS) == 1);
    REQUIRE(count_words(&p, "mul64", WORD_CMP_ASR_63, SOURCE_FIELDS) == 1);
    REQUIRE(count_words(&p, "mul64", WORD_CSET_NE, RD_FIELD) == 1);
    REQUIRE(count_words(&p, "umul64", WORD_UMULH, THREE_REGISTERS) == 1);
    REQUIRE(count_words(&p, "umul64", WORD_CMP_X_0, RN_FIELD) == 1);
    REQUIRE(count_words(&p, "umul64", WORD_MUL_X, THREE_REGISTERS) == 0);
    REQUIRE(count_words(&p, "mulu32", WORD_UMULL, THREE_REGISTERS) == 1);
    REQUIRE(count_words(&p, "mulu32", WORD_CMP_UXTW, SOURCE_FIELDS) == 1);
    REQUIRE(count_words(&p, "sub8", WORD_CMP_SXTB, SOURCE_FIELDS) == 1);
    REQUIRE(count_words(&p, "add_one", WORD_ADDS_X_ZR_1, RN_FIELD) == 1);
    REQUIRE(count_words(&p, "add_minus_one", WORD_SUBS_X_ZR_1, RN_FIELD) == 1);
    REQUIRE(count_words(&p, "wrapped_mul", WORD_UMULL, THREE_REGISTERS) == 0);
    release_program(&p);

    REQUIRE(build(&p, OVERFLOW_GRID));
    REQUIRE(agree_on_grid(&p, OVERFLOW_GRID_FUNCTIONS, sizeof OVERFLOW_GRID_FUNCTIONS / sizeof OVERFLOW_GRID_FUNCTIONS[0]));
    release_program(&p);
    return true;
}

static const char *const MUL_HIGH_FUNCTIONS[] = { "mh_grid", "mh_hash" };

static u64
high_half(u64 a, u64 b)
{
    return (u64)((unsigned __int128)a * b >> 64);
}

static int64_t
mh_grid_want(int64_t a, int64_t b)
{
    static const u64 v[16] = {
        0, 1, 2, 3, 4294967295u, 4294967296u, 4294967297u, 6148914691236517205u,
        9223372036854775807u, 9223372036854775808u, 9223372036854775809u,
        12297829382473034410u, 18446744073709551613u, 18446744073709551614u,
        18446744073709551615u, 11400714819323198485u,
    };
    u64 x = v[a & 15];
    u64 y = v[b & 15];
    u64 h = high_half(x, y);
    u64 k = high_half(x, 11400714819323198485u);
    u64 m = high_half(3, y);
    u64 c = high_half(UINT64_MAX, UINT64_MAX);
    return (int64_t)(h ^ k * 3 ^ m * 5 ^ c);
}

static int64_t
mh_hash_want(int64_t a, int64_t b)
{
    u64 p = (u64)a ^ 0x2D358DCCAA6C78A5u;
    u64 q = (u64)b ^ 0x8BB84B93962EACC9u;
    return (int64_t)(p * q ^ high_half(p, q));
}

TEST_CASE(mul_high_tests)
{
    catch_traps();
    Program p;
    REQUIRE(build(&p, MUL_HIGH));
    REQUIRE(agree_on_grid(&p, MUL_HIGH_FUNCTIONS, sizeof MUL_HIGH_FUNCTIONS / sizeof MUL_HIGH_FUNCTIONS[0]));

    I2 *grid = (I2 *)fn(&p, "mh_grid");
    I2 *hash = (I2 *)fn(&p, "mh_hash");
    for (int64_t a = -3; a <= 20; a++) {
        for (int64_t b = -3; b <= 20; b++) {
            REQUIRE(grid(CONTEXT, a, b) == mh_grid_want(a, b));
            REQUIRE(hash(CONTEXT, a, b) == mh_hash_want(a, b));
        }
    }
    REQUIRE(hash(CONTEXT, INT64_MIN, INT64_MAX) == mh_hash_want(INT64_MIN, INT64_MAX));
    REQUIRE(hash(CONTEXT, -1, INT64_MIN) == mh_hash_want(-1, INT64_MIN));

    REQUIRE(count_words(&p, "mh_hash", WORD_UMULH, THREE_REGISTERS) == 1);
    release_program(&p);
    return true;
}

static const char *const BITCAST_FUNCTIONS[] = {
    "bc_f32",  "bc_f64",     "bc_from",          "bc_ints",    "bc_pointers", "bc_misaligned",
    "bc_many", "bc_strided", "bc_float_pointer", "bc_special",
};

typedef struct
{
    const char *name;
    int64_t a;
    int64_t b;
    int64_t want;
    int trap;
} CallCase;

static const CallCase BITCAST_CASES[] = {
    { "bc_f32", 7, 2, 1075139925, 0 },
    { "bc_f32", -6, 1, 3233808384, 0 },
    { "bc_f32", 13, -3, 3230313131, 0 },
    { "bc_f64", 5, 1, 4609434218613702656, 0 },
    { "bc_f64", 0, 3, -4609434218613702656, 0 },
    { "bc_f64", -3, 12, -4599582594428829696, 0 },
    { "bc_from", 1, 2, 2717, 0 },
    { "bc_from", 5, 7, 2586, 0 },
    { "bc_from", -3, 11, 2851, 0 },
    { "bc_ints", 0, 0, 15032385920, 0 },
    { "bc_ints", 200, 40000, 15032269472, 0 },
    { "bc_ints", -3, -1, 15032385899, 0 },
    { "bc_pointers", 2, 5, 808, 0 },
    { "bc_pointers", 7, 15, 2323, 0 },
    { "bc_misaligned", 1, 0, 20, 0 },
    { "bc_misaligned", 1, 4, 0, TRAP_ALIGN },
    { "bc_misaligned", 3, 8, 40, 0 },
    { "bc_many", 1, 0, 84281099, 0 },
    { "bc_many", 1, 1, 0, TRAP_ALIGN },
    { "bc_many", 6, 2, 151653138, 0 },
    { "bc_strided", 0, 0, 16909060, 0 },
    { "bc_strided", 1, 4, 151653132, 0 },
    { "bc_strided", 2, 5, 84281096, 0 },
    { "bc_strided", 0, 2, 0, TRAP_ALIGN },
    { "bc_strided", 1, 6, 0, TRAP_ALIGN },
    { "bc_float_pointer", 0, 0, 11, 0 },
    { "bc_float_pointer", 2, 1, 33, 0 },
    { "bc_float_pointer", 3, 3, 44, 0 },
    { "bc_float_pointer", 1, 4, 0, TRAP_ALIGN },
    { "bc_float_pointer", 3, 5, 0, TRAP_ALIGN },
    { "bc_special", 0, 0, 32855, 0 },
    { "bc_special", 5, -7, 32860, 0 },
    { "bc_special", -100, 3, 32755, 0 },
};

TEST_CASE(bitcasts)
{
    catch_traps();
    Program p;
    REQUIRE(build(&p, BITCASTS));
    for (size_t k = 0; k < sizeof BITCAST_CASES / sizeof BITCAST_CASES[0]; k++) {
        CallCase t = BITCAST_CASES[k];
        int64_t got = 0;
        int trap = call_i2((I2 *)fn(&p, t.name), t.a, t.b, &got);
        if (trap != t.trap || (!trap && got != t.want)) {
            fprintf(stderr, "arm64 test: %s(%lld, %lld) gave %lld (trap %d)\n", t.name, (long long)t.a, (long long)t.b, (long long)got, trap);
            REQUIRE(false);
        }
    }
    REQUIRE(agree_on_grid(&p, BITCAST_FUNCTIONS, sizeof BITCAST_FUNCTIONS / sizeof BITCAST_FUNCTIONS[0]));
    release_program(&p);
    return true;
}

static const char *const OPTIONAL_FUNCTIONS[] = {
    "opt_list",          "opt_unwrap",      "opt_or_else",
    "opt_assign",        "opt_compare",     "opt_bitcast",
    "opt_struct",        "opt_chain",       "opt_global",
    "opt_copies",        "opt_high",        "opt_literal",
    "opt_narrow_list",   "opt_narrow_else", "opt_narrow_assign",
    "opt_narrow_loops",  "opt_narrow_or",   "opt_narrow_not",
    "opt_narrow_nested",
};

static const CallCase OPTIONAL_CASES[] = {
    { "opt_list", 0, 5, 0, 0 },
    { "opt_list", 3, 2, 363, 0 },
    { "opt_list", -2, 4, 182, 0 },
    { "opt_list", 5, -3, -29, 0 },
    { "opt_unwrap", 7, 1, 8, 0 },
    { "opt_unwrap", -4, 3, -3, 0 },
    { "opt_unwrap", 7, 0, 0, TRAP_NIL },
    { "opt_unwrap", 2, -5, 0, TRAP_NIL },
    { "opt_or_else", 5, 3, 5000, 0 },
    { "opt_or_else", 3, 5, 5107, 0 },
    { "opt_or_else", 4, 4, 4107, 0 },
    { "opt_or_else", -2, -7, -2000, 0 },
    { "opt_assign", 3, 5, 353, 0 },
    { "opt_assign", 4, 7, 747, 0 },
    { "opt_assign", -1, 2, -81, 0 },
    { "opt_assign", 0, -3, -303, 0 },
    { "opt_compare", 2, 2, 126, 0 },
    { "opt_compare", 2, 3, 62, 0 },
    { "opt_compare", -1, -1, 126, 0 },
    { "opt_bitcast", 6, 1, 62, 0 },
    { "opt_bitcast", -3, 9, -28, 0 },
    { "opt_bitcast", 6, 0, 0, TRAP_NIL },
    { "opt_bitcast", 6, -1, 0, TRAP_NIL },
    { "opt_struct", 5, 9, 5, 0 },
    { "opt_struct", 0, 9, 9, 0 },
    { "opt_struct", -4, -6, -6, 0 },
    { "opt_chain", 3, 9, 3, 0 },
    { "opt_chain", 1, 9, 9, 0 },
    { "opt_chain", 1, 2, 7, 0 },
    { "opt_chain", 5, 1, 5, 0 },
    { "opt_global", 5, 3, 45, 0 },
    { "opt_global", 3, 5, 44, 0 },
    { "opt_global", -2, -2, 39, 0 },
    { "opt_copies", 5, 3, 8, 0 },
    { "opt_copies", 5, 0, 110, 0 },
    { "opt_copies", -7, -2, 86, 0 },
    { "opt_high", 1, 2, 12, 0 },
    { "opt_high", 2, 3, 3, 0 },
    { "opt_high", 3, 5, 15, 0 },
    { "opt_high", 1, 0, 10, 0 },
    { "opt_high", 2, 4, 0, TRAP_NIL },
    { "opt_literal", 5, 9, 500507, 0 },
    { "opt_literal", 5, 0, 510605, 0 },
    { "opt_literal", -1, 2, 25535855, 0 },
    { "opt_literal", 200, 4, 20020007, 0 },
    { "opt_narrow_list", 0, 9, 0, 0 },
    { "opt_narrow_list", 3, 2, -184, 0 },
    { "opt_narrow_list", 3, 52, 52138, 0 },
    { "opt_narrow_list", 2, 70, 60000, 0 },
    { "opt_narrow_list", -1, 45, 375, 0 },
    { "opt_narrow_else", 3, 4, 300343, 0 },
    { "opt_narrow_else", 3, -1, 300032, 0 },
    { "opt_narrow_else", -2, 5, 1, 0 },
    { "opt_narrow_else", 0, 0, 1, 0 },
    { "opt_narrow_assign", 5, 3, 4205, 0 },
    { "opt_narrow_assign", 2, 7, 6895, 0 },
    { "opt_narrow_assign", 4, -1, 804, 0 },
    { "opt_narrow_assign", -3, -3, -600, 0 },
    { "opt_narrow_loops", 2, 1, 120220, 0 },
    { "opt_narrow_loops", 3, 0, 180305, 0 },
    { "opt_narrow_loops", 4, 5, -1000, 0 },
    { "opt_narrow_loops", 7, 6, 5, 0 },
    { "opt_narrow_loops", -1, 3, 19905, 0 },
    { "opt_narrow_or", 5, 3, 502, 0 },
    { "opt_narrow_or", 2, 9, 7, 0 },
    { "opt_narrow_or", 0, 4, 7, 0 },
    { "opt_narrow_or", 6, 0, 7, 0 },
    { "opt_narrow_or", -1, -8, -93, 0 },
    { "opt_narrow_not", 3, 4, 3344, 0 },
    { "opt_narrow_not", 3, 1, 311, 0 },
    { "opt_narrow_not", 3, -1, 41, 0 },
    { "opt_narrow_not", -2, 5, -69, 0 },
    { "opt_narrow_not", 5, 2, 522, 0 },
    { "opt_narrow_nested", 3, 1, 1334, 0 },
    { "opt_narrow_nested", -2, 5, 780, 0 },
    { "opt_narrow_nested", 4, 0, -1, 0 },
    { "opt_narrow_nested", 0, 2, 1000, 0 },
};

TEST_CASE(optionals)
{
    catch_traps();
    Program p;
    REQUIRE(build(&p, OPTIONALS));
    for (size_t k = 0; k < sizeof OPTIONAL_CASES / sizeof OPTIONAL_CASES[0]; k++) {
        CallCase t = OPTIONAL_CASES[k];
        int64_t got = 0;
        int trap = call_i2((I2 *)fn(&p, t.name), t.a, t.b, &got);
        if (trap != t.trap || (!trap && got != t.want)) {
            fprintf(stderr, "arm64 test: %s(%lld, %lld) gave %lld (trap %d)\n", t.name, (long long)t.a, (long long)t.b, (long long)got, trap);
            REQUIRE(false);
        }
    }
    REQUIRE(agree_on_grid(&p, OPTIONAL_FUNCTIONS, sizeof OPTIONAL_FUNCTIONS / sizeof OPTIONAL_FUNCTIONS[0]));
    release_program(&p);
    return true;
}

static const char *const STRING_FUNCTIONS[] = {
    "str_byte",   "str_named",  "str_escape_bytes", "str_nul",
    "str_optional", "str_struct", "str_pointer",
};

static const CallCase STRING_CASES[] = {
    { "str_byte", 0, 0, 10400, 0 },
    { "str_byte", 7, -3, 11897, 0 },
    { "str_byte", -2, 4, 3204, 0 },
    { "str_named", 1, 2, 101108, 0 },
    { "str_named", 5, 1, 44032, 0 },
    { "str_escape_bytes", 0, 0, 113000, 0 },
    { "str_escape_bytes", 1, 2, 92003, 0 },
    { "str_escape_bytes", 7, -3, 10008, 0 },
    { "str_escape_bytes", -2, 4, 159014, 0 },
    { "str_escape_bytes", 13, 3, 128016, 0 },
    { "str_escape_bytes", 5, 1, 8006, 0 },
    { "str_nul", 7, -3, 10, 0 },
    { "str_optional", 7, -3, 103114, 0 },
    { "str_optional", 0, 0, 101108, 0 },
    { "str_struct", 1, 2, 97123, 0 },
    { "str_struct", 7, -3, 101118, 0 },
    { "str_pointer", 13, 3, 120160, 0 },
};

TEST_CASE(strings)
{
    catch_traps();
    Program p;
    REQUIRE(build(&p, STRINGS));
    for (size_t k = 0; k < sizeof STRING_CASES / sizeof STRING_CASES[0]; k++) {
        CallCase t = STRING_CASES[k];
        int64_t got = 0;
        int trap = call_i2((I2 *)fn(&p, t.name), t.a, t.b, &got);
        if (trap != t.trap || (!trap && got != t.want)) {
            fprintf(stderr, "arm64 test: %s(%lld, %lld) gave %lld (trap %d)\n", t.name, (long long)t.a, (long long)t.b, (long long)got, trap);
            REQUIRE(false);
        }
    }
    REQUIRE(agree_on_grid(&p, STRING_FUNCTIONS, sizeof STRING_FUNCTIONS / sizeof STRING_FUNCTIONS[0]));
    release_program(&p);
    return true;
}

static const char *const VALUE_IF_FUNCTIONS[] = {
    "vif_pick",  "vif_small", "vif_side",  "vif_nested", "vif_float", "vif_f32",
    "vif_narrow", "vif_loop", "vif_arg",   "vif_const",  "vif_bool",  "vif_widen",
    "vif_spill", "vif_mixed", "vif_while", "vif_ptr",
};

static const CallCase VALUE_IF_CASES[] = {
    { "vif_pick", 3, 5, 3, 0 },
    { "vif_pick", 7, -2, -2, 0 },
    { "vif_small", 5, 1, 200300, 0 },
    { "vif_small", 2, 2, 6700, 0 },
    { "vif_side", 5, 1, 50500, 0 },
    { "vif_side", 1, 4, 40004, 0 },
    { "vif_nested", -1, 0, 1, 0 },
    { "vif_nested", 0, 0, 2, 0 },
    { "vif_nested", 2, 5, 3, 0 },
    { "vif_nested", 5, 2, 4, 0 },
    { "vif_float", 4, 1, 100, 0 },
    { "vif_float", 3, 0, 25, 0 },
    { "vif_f32", 5, 1, 150, 0 },
    { "vif_f32", 1, 5, 25, 0 },
    { "vif_narrow", 1, 7, 8, 0 },
    { "vif_narrow", 0, 7, -1, 0 },
    { "vif_loop", 2, 0, 10, 0 },
    { "vif_loop", -3, 0, 35, 0 },
    { "vif_arg", 5, 1, 8, 0 },
    { "vif_arg", 1, 5, 9, 0 },
    { "vif_const", 3, 4, 304, 0 },
    { "vif_bool", 5, 1, 1, 0 },
    { "vif_bool", -5, -1, 0, 0 },
    { "vif_bool", 1, 5, 1, 0 },
    { "vif_widen", 70000, 3, 4464, 0 },
    { "vif_widen", 5, -2, -2, 0 },
    { "vif_spill", 9, 3, 16, 0 },
    { "vif_spill", 1, 5, 12, 0 },
    { "vif_mixed", 3, 4, 4, 0 },
    { "vif_mixed", -1, 4, -1, 0 },
    { "vif_mixed", 6, 4, -7, 0 },
    { "vif_while", 3, 9, 3, 0 },
    { "vif_while", -1, 4, 4, 0 },
    { "vif_while", 0, -2, 0, 0 },
    { "vif_ptr", 5, 1, 105001, 0 },
    { "vif_ptr", 1, 5, 1105, 0 },
};

TEST_CASE(value_ifs)
{
    catch_traps();
    Program p;
    REQUIRE(build(&p, VALUE_IFS));
    for (size_t k = 0; k < sizeof VALUE_IF_CASES / sizeof VALUE_IF_CASES[0]; k++) {
        CallCase t = VALUE_IF_CASES[k];
        int64_t got = 0;
        int trap = call_i2((I2 *)fn(&p, t.name), t.a, t.b, &got);
        if (trap != t.trap || (!trap && got != t.want)) {
            fprintf(stderr, "arm64 test: %s(%lld, %lld) gave %lld (trap %d)\n", t.name, (long long)t.a, (long long)t.b, (long long)got, trap);
            REQUIRE(false);
        }
    }
    REQUIRE(agree_on_grid(&p, VALUE_IF_FUNCTIONS, sizeof VALUE_IF_FUNCTIONS / sizeof VALUE_IF_FUNCTIONS[0]));
    release_program(&p);
    return true;
}

static const char *const STRING_TABLE_FUNCTIONS[] = {
    "st_name",   "st_optional", "st_keyword", "st_pair",          "st_grid_byte",
    "st_nested", "st_pointer",  "st_escape",  "st_find",          "st_global",
    "st_global_struct", "st_global_slots", "st_global_box", "st_spare",
};

static const CallCase STRING_TABLE_CASES[] = {
    { "st_name", 1, 2, 111111, 0 },
    { "st_optional", 1, 2, 1101, 0 },
    { "st_optional", 0, 0, 970, 0 },
    { "st_keyword", 1, 2, 10104101, 0 },
    { "st_keyword", 1, 1, 11905119, 0 },
    { "st_keyword", 0, 0, 10502045, 0 },
    { "st_pair", 1, 2, 99102, 0 },
    { "st_grid_byte", 1, 2, 120, 0 },
    { "st_nested", 1, 2, 80134, 0 },
    { "st_pointer", 1, 2, 121002, 0 },
    { "st_escape", 1, 2, 1959, 0 },
    { "st_escape", 0, 1, 432, 0 },
    { "st_find", 1, 2, 33, 0 },
};

static const char *
global_string(Program *p, const char *name, u32 offset)
{
    const char *s;
    memcpy(&s, p->n.globals + p->c.decls.values[decl_named(p, name)] + offset, sizeof s);
    return s;
}

TEST_CASE(string_tables)
{
    catch_traps();
    Program p;
    REQUIRE(build(&p, STRING_TABLES));
    REQUIRE(strcmp(global_string(&p, "st_label", 0), "label") == 0);
    REQUIRE(strcmp(global_string(&p, "st_table", 24), "three") == 0);
    REQUIRE(strcmp(global_string(&p, "st_kw", 16), "al") == 0);
    REQUIRE(agree_on_grid(&p, STRING_TABLE_FUNCTIONS, sizeof STRING_TABLE_FUNCTIONS / sizeof STRING_TABLE_FUNCTIONS[0]));
    for (size_t k = 0; k < sizeof STRING_TABLE_CASES / sizeof STRING_TABLE_CASES[0]; k++) {
        CallCase t = STRING_TABLE_CASES[k];
        int64_t got = 0;
        int trap = call_i2((I2 *)fn(&p, t.name), t.a, t.b, &got);
        if (trap != t.trap || (!trap && got != t.want)) {
            fprintf(stderr, "arm64 test: %s(%lld, %lld) gave %lld (trap %d)\n", t.name, (long long)t.a, (long long)t.b, (long long)got, trap);
            REQUIRE(false);
        }
    }
    release_program(&p);
    return true;
}

static const char *const STRUCT_IF_FUNCTIONS[] = {
    "sif_pick", "sif_literal", "sif_alias", "sif_nested", "sif_field", "sif_big",
    "sif_named", "sif_array", "sif_const", "sif_arg", "sif_return", "sif_loop",
    "sif_chain", "sif_order", "sif_rows",
};

static const CallCase STRUCT_IF_CASES[] = {
    { "sif_pick", 3, 5, 3005, 0 },
    { "sif_pick", 7, -2, -1993, 0 },
    { "sif_literal", 7, -2, 8074, 0 },
    { "sif_literal", 3, 5, 3082, 0 },
    { "sif_alias", 3, 5, 3005, 0 },
    { "sif_alias", -2, -3, 2003, 0 },
    { "sif_nested", -1, 4, 999, 0 },
    { "sif_nested", 0, -2, 1998, 0 },
    { "sif_nested", 3, 5, 3008, 0 },
    { "sif_field", 7, -2, 74, 0 },
    { "sif_field", 3, 5, 53, 0 },
    { "sif_big", 3, 5, 54353, 0 },
    { "sif_big", 6, 1, 11676, 0 },
    { "sif_named", 7, -2, 683, 0 },
    { "sif_named", -1, 4, 94, 0 },
    { "sif_array", 3, 5, 351, 0 },
    { "sif_array", 7, -2, -128, 0 },
    { "sif_const", 7, -2, 4328, 0 },
    { "sif_const", -1, 4, 4320, 0 },
    { "sif_arg", 7, -2, 23, 0 },
    { "sif_arg", 3, 5, 12, 0 },
    { "sif_return", 7, -2, 700, 0 },
    { "sif_return", 3, 5, 5, 0 },
    { "sif_loop", 3, 5, 910, 0 },
    { "sif_loop", -1, 4, -292, 0 },
    { "sif_chain", 7, -2, 698, 0 },
    { "sif_chain", 3, 5, 301, 0 },
    { "sif_chain", 1, 8, 801, 0 },
    { "sif_chain", -1, 4, -96, 0 },
    { "sif_chain", -3, 2, 707, 0 },
    { "sif_chain", 0, -2, -101, 0 },
    { "sif_order", 7, -2, 7078, 0 },
    { "sif_order", 3, 5, 3056, 0 },
    { "sif_rows", 7, -2, 681, 0 },
    { "sif_rows", 5, 5, 789, 0 },
    { "sif_rows", 3, 5, 532, 0 },
};

TEST_CASE(struct_ifs)
{
    catch_traps();
    Program p;
    REQUIRE(build(&p, STRUCT_IFS));
    for (size_t k = 0; k < sizeof STRUCT_IF_CASES / sizeof STRUCT_IF_CASES[0]; k++) {
        CallCase t = STRUCT_IF_CASES[k];
        int64_t got = 0;
        int trap = call_i2((I2 *)fn(&p, t.name), t.a, t.b, &got);
        if (trap != t.trap || (!trap && got != t.want)) {
            fprintf(stderr, "arm64 test: %s(%lld, %lld) gave %lld (trap %d)\n", t.name, (long long)t.a, (long long)t.b, (long long)got, trap);
            REQUIRE(false);
        }
    }
    REQUIRE(agree_on_grid(&p, STRUCT_IF_FUNCTIONS, sizeof STRUCT_IF_FUNCTIONS / sizeof STRUCT_IF_FUNCTIONS[0]));
    release_program(&p);
    return true;
}

static const char *const OPTIONAL_FN_FUNCTIONS[] = {
    "ofn_round",  "ofn_unwrap",  "ofn_check",   "ofn_or_else",
    "ofn_assign", "ofn_compare", "ofn_pass",    "ofn_global",
    "ofn_narrow", "ofn_choose",  "ofn_pointer",
};

static const CallCase OPTIONAL_FN_CASES[] = {
    { "ofn_round", 3, 4, 304, 0 },
    { "ofn_round", 0, 7, 70, 0 },
    { "ofn_round", -2, 5, -195, 0 },
    { "ofn_round", 0, -3, -30, 0 },
    { "ofn_unwrap", 7, 2, 6, 0 },
    { "ofn_unwrap", 3, 3, 0, TRAP_NIL },
    { "ofn_unwrap", -4, 1, -4, 0 },
    { "ofn_check", 3, 4, 11, 0 },
    { "ofn_check", 0, 9, 0, TRAP_NIL },
    { "ofn_check", -2, 5, -11, 0 },
    { "ofn_or_else", 5, 3, 5, 0 },
    { "ofn_or_else", 0, 3, 3, 0 },
    { "ofn_or_else", 0, 0, 77, 0 },
    { "ofn_or_else", -1, 0, -1, 0 },
    { "ofn_assign", 7, 2, 70505, 0 },
    { "ofn_assign", 0, 0, 50512, 0 },
    { "ofn_assign", -1, 4, -9493, 0 },
    { "ofn_compare", 2, 2, 1011, 0 },
    { "ofn_compare", 2, 3, 10, 0 },
    { "ofn_compare", 0, 0, 101, 0 },
    { "ofn_compare", 0, 5, 0, 0 },
    { "ofn_compare", 4, 0, 110, 0 },
    { "ofn_pass", 5, 2, 5, 0 },
    { "ofn_pass", 0, -1, -1, 0 },
    { "ofn_pass", 1, 3, 10, 0 },
    { "ofn_global", 5, 3, 5, 0 },
    { "ofn_global", 3, 5, 41, 0 },
    { "ofn_global", 0, -2, 41, 0 },
    { "ofn_narrow", 0, 2, 3308, 0 },
    { "ofn_narrow", 5, 2, 2211, 0 },
    { "ofn_narrow", -1, -3, 2190, 0 },
    { "ofn_choose", 5, 2, 3, 0 },
    { "ofn_choose", 1, 4, 4, 0 },
    { "ofn_choose", -1, 0, 1000, 0 },
    { "ofn_pointer", 5, 3, 8, 0 },
    { "ofn_pointer", -2, 7, 5, 0 },
};

TEST_CASE(optional_fns)
{
    catch_traps();
    Program p;
    REQUIRE(build(&p, OPTIONAL_FNS));
    for (size_t k = 0; k < sizeof OPTIONAL_FN_CASES / sizeof OPTIONAL_FN_CASES[0]; k++) {
        CallCase t = OPTIONAL_FN_CASES[k];
        int64_t got = 0;
        int trap = call_i2((I2 *)fn(&p, t.name), t.a, t.b, &got);
        if (trap != t.trap || (!trap && got != t.want)) {
            fprintf(stderr, "arm64 test: %s(%lld, %lld) gave %lld (trap %d)\n", t.name, (long long)t.a, (long long)t.b, (long long)got, trap);
            REQUIRE(false);
        }
    }
    REQUIRE(agree_on_grid(&p, OPTIONAL_FN_FUNCTIONS, sizeof OPTIONAL_FN_FUNCTIONS / sizeof OPTIONAL_FN_FUNCTIONS[0]));
    release_program(&p);
    return true;
}

static const char *const REST_ARG_FUNCTIONS[] = {
    "rst_none",    "rst_one",    "rst_many",  "rst_stack", "rst_index", "rst_order",
    "rst_structs", "rst_nested", "rst_widen", "rst_loop",  "rst_copy",  "rst_slices",
    "rst_floats",  "rst_strings", "rst_alias", "rst_arrays", "rst_calls", "rst_void",
};

static const CallCase REST_ARG_CASES[] = {
    { "rst_none", 2, 5, 20, 0 },
    { "rst_none", 0, -3, 0, 0 },
    { "rst_none", 1, 300, 10, 0 },
    { "rst_one", 2, 5, 7, 0 },
    { "rst_one", 0, -3, -3, 0 },
    { "rst_one", 1, 300, 301, 0 },
    { "rst_many", 2, 5, 19, 0 },
    { "rst_many", 0, -3, -7, 0 },
    { "rst_many", 1, 300, 899, 0 },
    { "rst_stack", 2, 5, 40, 0 },
    { "rst_stack", 0, -3, 36, 0 },
    { "rst_stack", 1, 300, 38, 0 },
    { "rst_index", 2, 5, 5, 0 },
    { "rst_index", 0, -3, 10, 0 },
    { "rst_index", 1, 300, 20, 0 },
    { "rst_index", 3, 0, 0, TRAP_BOUNDS },
    { "rst_index", -1, 0, 0, TRAP_BOUNDS },
    { "rst_order", 2, 5, 122308, 0 },
    { "rst_order", 0, -3, 120306, 0 },
    { "rst_order", 1, 300, 121307, 0 },
    { "rst_structs", 2, 5, 215273, 0 },
    { "rst_structs", 0, -3, 7173, 0 },
    { "rst_structs", 1, 300, 413213, 0 },
    { "rst_nested", 2, 5, 312, 0 },
    { "rst_nested", 0, -3, 300, 0 },
    { "rst_nested", 1, 300, 605, 0 },
    { "rst_widen", 2, 5, 2005007009, 0 },
    { "rst_widen", 0, -3, 13007009, 0 },
    { "rst_widen", 1, 300, 1012007009, 0 },
    { "rst_loop", 2, 5, 24, 0 },
    { "rst_loop", 0, -3, -6, 0 },
    { "rst_loop", 1, 300, 906, 0 },
    { "rst_copy", 2, 5, 7100, 0 },
    { "rst_copy", 0, -3, -2900, 0 },
    { "rst_copy", 1, 300, 301100, 0 },
    { "rst_slices", 2, 5, 230, 0 },
    { "rst_slices", 0, -3, 30, 0 },
    { "rst_slices", 1, 300, 140, 0 },
    { "rst_floats", 2, 5, 93, 0 },
    { "rst_floats", 0, -3, 1093, 0 },
    { "rst_floats", 1, 300, 380, 0 },
    { "rst_strings", 2, 5, 65098125, 0 },
    { "rst_strings", 0, -3, 65098118, 0 },
    { "rst_strings", 1, 300, 65098420, 0 },
    { "rst_alias", 2, 5, 77211071, 0 },
    { "rst_alias", 0, -3, 77011051, 0 },
    { "rst_alias", 1, 300, 77111061, 0 },
    { "rst_arrays", 2, 5, 123425, 0 },
    { "rst_arrays", 0, -3, 123405, 0 },
    { "rst_arrays", 1, 300, 123414, 0 },
    { "rst_calls", 2, 5, 223411, 0 },
    { "rst_calls", 0, -3, 25611, 0 },
    { "rst_calls", 1, 300, 123411, 0 },
    { "rst_void", 2, 5, 1225, 0 },
    { "rst_void", 0, -3, 1017, 0 },
    { "rst_void", 1, 300, 1420, 0 },
};

TEST_CASE(rest_args)
{
    catch_traps();
    Program p;
    REQUIRE(build(&p, REST_ARGS));
    for (size_t k = 0; k < sizeof REST_ARG_CASES / sizeof REST_ARG_CASES[0]; k++) {
        CallCase t = REST_ARG_CASES[k];
        int64_t got = 0;
        int trap = call_i2((I2 *)fn(&p, t.name), t.a, t.b, &got);
        if (trap != t.trap || (!trap && got != t.want)) {
            fprintf(stderr, "arm64 test: %s(%lld, %lld) gave %lld (trap %d)\n", t.name, (long long)t.a, (long long)t.b, (long long)got, trap);
            REQUIRE(false);
        }
    }
    REQUIRE(agree_on_grid(&p, REST_ARG_FUNCTIONS, sizeof REST_ARG_FUNCTIONS / sizeof REST_ARG_FUNCTIONS[0]));
    release_program(&p);
    return true;
}

static const char *const INLINE_MARK_FUNCTIONS[] = {
    "inl_step",
    "inl_mix",
    "inl_calls",
    "inl_rare",
    "inl_guard",
};

static const CallCase INLINE_MARK_CASES[] = {
    { "inl_step", 2, 5, 9, 0 },
    { "inl_step", 0, -3, -3, 0 },
    { "inl_step", 1, 300, 302, 0 },
    { "inl_step", 4611686018427387904, 0, 0, TRAP_OVERFLOW },
    { "inl_mix", 2, 5, 130, 0 },
    { "inl_mix", 0, -3, -66, 0 },
    { "inl_mix", 1, 300, 6610, 0 },
    { "inl_calls", 2, 5, 132, 0 },
    { "inl_calls", 0, -3, -65, 0 },
    { "inl_calls", 1, 300, 6611, 0 },
    { "inl_calls", 7, 1, 5132, 0 },
    { "inl_calls", -1, 0, 5030, 0 },
    { "inl_rare", 2, 5, 1995, 0 },
    { "inl_rare", -3, 7, -3007, 0 },
    { "inl_rare", 9223372036854776, 0, 0, TRAP_OVERFLOW },
    { "inl_guard", 5, 2, 4998, 0 },
    { "inl_guard", 2, 5, 9, 0 },
    { "inl_guard", 3, 3, 9, 0 },
    { "inl_guard", -1, -4, -996, 0 },
};

TEST_CASE(inline_marks)
{
    catch_traps();
    Program p;
    REQUIRE(build(&p, INLINE_MARKS));
    for (size_t k = 0; k < sizeof INLINE_MARK_CASES / sizeof INLINE_MARK_CASES[0]; k++) {
        CallCase t = INLINE_MARK_CASES[k];
        int64_t got = 0;
        int trap = call_i2((I2 *)fn(&p, t.name), t.a, t.b, &got);
        if (trap != t.trap || (!trap && got != t.want)) {
            fprintf(stderr, "arm64 test: %s(%lld, %lld) gave %lld (trap %d)\n", t.name, (long long)t.a, (long long)t.b, (long long)got, trap);
            REQUIRE(false);
        }
    }
    REQUIRE(agree_on_grid(&p, INLINE_MARK_FUNCTIONS, sizeof INLINE_MARK_FUNCTIONS / sizeof INLINE_MARK_FUNCTIONS[0]));
    release_program(&p);
    return true;
}

enum
{
    WORD_STR_X_SP = 0xF90003E0u,
    Q_FIELDS = 0x003FFC1Fu,
};

static const char *const ASSERT_FUNCTIONS[] = {
    "as_range",
    "as_effects",
    "as_flag",
    "as_const",
};

static const CallCase ASSERT_CASES[] = {
    { "as_range", 2, 5, 25, 0 },
    { "as_range", 5, 2, 0, TRAP_ASSERT },
    { "as_range", 5, -1, 49, 0 },
    { "as_range", INT64_MIN, 0, 0, TRAP_ASSERT },
    { "as_effects", 1, 2, 203, 0 },
    { "as_effects", 13, 0, 0, TRAP_ASSERT },
    { "as_flag", 4, 5, 0, TRAP_ASSERT },
    { "as_flag", 4, 6, -2, 0 },
    { "as_const", 12, 12, 0, TRAP_ASSERT },
    { "as_const", 12, 11, 7, 0 },
};

TEST_CASE(asserts)
{
    catch_traps();
    Program p;
    REQUIRE(build(&p, ASSERTS));
    for (size_t k = 0; k < sizeof ASSERT_CASES / sizeof ASSERT_CASES[0]; k++) {
        CallCase t = ASSERT_CASES[k];
        int64_t got = 0;
        int trap = call_i2((I2 *)fn(&p, t.name), t.a, t.b, &got);
        if (trap != t.trap || (!trap && got != t.want)) {
            fprintf(stderr, "arm64 test: %s(%lld, %lld) gave %lld (trap %d)\n", t.name, (long long)t.a, (long long)t.b, (long long)got, trap);
            REQUIRE(false);
        }
    }
    REQUIRE(agree_on_grid(&p, ASSERT_FUNCTIONS, sizeof ASSERT_FUNCTIONS / sizeof ASSERT_FUNCTIONS[0]));
    REQUIRE(count_words(&p, "as_range", WORD_STR_X_SP, Q_FIELDS) == 0);
    release_program(&p);
    return true;
}

static const char *const TARGET_FUNCTIONS[] = { "tg_arch", "tg_os", "tg_value" };

static const CallCase TARGET_CASES[] = {
    { "tg_arch", 5, 3, 8, 0 },
    { "tg_os", 2, 10, TARGET_OS_HOST == NAME_MACOS ? 20 : 16, 0 },
    { "tg_value", 2, 1, TARGET_OS_HOST == NAME_MACOS ? 221 : 21, 0 },
};

TEST_CASE(targets)
{
    catch_traps();
    Program p;
    REQUIRE(build(&p, TARGETS));
    for (size_t k = 0; k < sizeof TARGET_CASES / sizeof TARGET_CASES[0]; k++) {
        CallCase t = TARGET_CASES[k];
        int64_t got = 0;
        int trap = call_i2((I2 *)fn(&p, t.name), t.a, t.b, &got);
        if (trap != t.trap || (!trap && got != t.want)) {
            fprintf(stderr, "arm64 test: %s(%lld, %lld) gave %lld (trap %d)\n", t.name, (long long)t.a, (long long)t.b, (long long)got, trap);
            REQUIRE(false);
        }
    }
    REQUIRE(agree_on_grid(&p, TARGET_FUNCTIONS, sizeof TARGET_FUNCTIONS / sizeof TARGET_FUNCTIONS[0]));
    release_program(&p);
    return true;
}

static const char *const RELEASE_FAST_FUNCTIONS[] = {
    "rf_wrap", "rf_shift", "rf_convert", "rf_enum", "rf_div", "rf_float", "rf_assert", "rf_range",
};

static const CallCase RELEASE_FAST_CASES[] = {
    { "rf_wrap", 13, 12, 3074457345618258479, 0 },
    { "rf_wrap", 5, 0, -3074457345618258601, 0 },
    { "rf_shift", 13, 12, 40960, 0 },
    { "rf_shift", -3, -3, -6917529027641106426, 0 },
    { "rf_convert", -1, 7, 4294967301, 0 },
    { "rf_convert", -3, -3, 247, 0 },
    { "rf_enum", -3, -3, 328674, 0 },
    { "rf_enum", INT64_MIN, -1, 764, 0 },
    { "rf_div", 5, 0, 0, TRAP_DIVIDE },
    { "rf_div", INT64_MIN, -1, 0, TRAP_OVERFLOW },
    { "rf_float", -1, 7, -93, 0 },
    { "rf_float", 13, 12, 0, TRAP_RANGE },
    { "rf_assert", 5, 3, 12, 0 },
    { "rf_checked", 5, 3, 8, 0 },
    { "rf_range", 5, 3, 53, 0 },
    { "rf_range", 13, -3, 127, 0 },
};

TEST_CASE(release_fast)
{
    catch_traps();
    Program p;
    REQUIRE(check_text(&p, RELEASE_FAST_GRID, true, false));
    REQUIRE(jit_compile(&p.n, &p.c, CONVENTION_HOST, 0));
    for (size_t k = 0; k < sizeof RELEASE_FAST_CASES / sizeof RELEASE_FAST_CASES[0]; k++) {
        CallCase t = RELEASE_FAST_CASES[k];
        int64_t got = 0;
        int trap = call_i2((I2 *)fn(&p, t.name), t.a, t.b, &got);
        if (trap != t.trap || (!trap && got != t.want)) {
            fprintf(stderr, "arm64 test: %s(%lld, %lld) gave %lld (trap %d)\n", t.name, (long long)t.a, (long long)t.b, (long long)got, trap);
            REQUIRE(false);
        }
    }
    REQUIRE(agree_on_grid(&p, RELEASE_FAST_FUNCTIONS, sizeof RELEASE_FAST_FUNCTIONS / sizeof RELEASE_FAST_FUNCTIONS[0]));
    REQUIRE(count_words(&p, "rf_range", WORD_STR_X_SP, Q_FIELDS) == 0);
    release_program(&p);
    return true;
}

typedef int64_t I3(void *, int64_t, int64_t, int64_t);

static int
call_i3(I3 *f, const int64_t *args, int64_t *result)
{
    trap_kind = 0;
    if (sigsetjmp(trap_jump, 1) == 0) {
        *result = f(CONTEXT, args[0], args[1], args[2]);
        return 0;
    }
    return trap_kind;
}

static bool
vector_runs_agree(const char *text, const VectorRun *runs, size_t n, bool release_fast)
{
    Program p;
    if (!check_text(&p, text, release_fast, false)) {
        return false;
    }
    if (!jit_compile(&p.n, &p.c, CONVENTION_HOST, 0)) {
        fprintf(stderr, "arm64 test: %s\n", p.n.error);
        return false;
    }
    bool ok = true;
    for (size_t i = 0; ok && i < n; i++) {
        const VectorRun *r = &runs[i];
        int64_t got = 0;
        int trap = call_i3((I3 *)fn(&p, r->name), r->args, &got);
        if (trap != r->trap || (!trap && got != r->want)) {
            fprintf(
                stderr,
                "arm64 test: %s(%lld, %lld, %lld) gave %lld (trap %d), want "
                "%lld (trap %d)\n",
                r->name,
                (long long)r->args[0],
                (long long)r->args[1],
                (long long)r->args[2],
                (long long)got,
                trap,
                (long long)r->want,
                r->trap
            );
            ok = false;
        }
    }
    release_program(&p);
    return ok;
}

static int
call_i4(I4 *f, const int64_t *args, int64_t *result)
{
    trap_kind = 0;
    if (sigsetjmp(trap_jump, 1) == 0) {
        *result = f(CONTEXT, args[0], args[1], args[2], args[3]);
        return 0;
    }
    return trap_kind;
}

static bool
vector_grid_agrees(Program *p, const GridFunction *g)
{
    I4 *f = (I4 *)fn(p, g->name);
    u64 mask = g->bits == 64 ? ~0ull : (1ull << g->bits) - 1;
    int64_t step = g->lanes * g->bits / 128;
    for (int64_t a = 0; a < 16; a++) {
        for (int64_t b = a % step; b < 16; b += step) {
            for (int64_t op = 0; op < 20; op++) {
                int64_t args[4] = { a, b, op, (a * 3 + b + op) & (g->lanes - 1) };
                int64_t got = 0;
                int trap = call_i4(f, args, &got);
                u64 want = g->oracle(a, b, op, args[3]);
                if (trap || (((u64)got ^ want) & mask)) {
                    fprintf(
                        stderr,
                        "arm64 test: %s(%lld, %lld, %lld, %lld) gave %llu "
                        "(trap %d), want %llu\n",
                        g->name,
                        (long long)a,
                        (long long)b,
                        (long long)op,
                        (long long)args[3],
                        (unsigned long long)((u64)got & mask),
                        trap,
                        (unsigned long long)(want & mask)
                    );
                    return false;
                }
            }
        }
    }
    return true;
}

static const char VECTOR_SHAPES[] =
    "import \"std/mem\"\n"
    "import \"std/simd\"\n"
    "fn bytes_mask(p: [*]u8) -> int {\n"
    "    return simd.top_bits(mem.unaligned_load([16]u8, p))\n"
    "}\n"
    "fn mask(a: u8) -> u8 {\n"
    "    v: [16]u8 = a\n"
    "    m := simd.le((v | 0x20) -% 97, 25)\n"
    "    return m[3]\n"
    "}\n"
    "fn shifted(a: u8, b: i8) -> int {\n"
    "    x: [16]u8 = a\n"
    "    y: [16]i8 = b\n"
    "    s := (x << 2) >> 3\n"
    "    t := y >> 1\n"
    "    return int(s[0]) * 1000 + int(t[0])\n"
    "}\n"
    "fn constants(a: u16, b: u32) -> int {\n"
    "    x: [8]u16 = a\n"
    "    y: [4]u32 = b\n"
    "    p := x +% 5\n"
    "    q := y ^ 200\n"
    "    r := y +% 0x12345\n"
    "    z := x & 0\n"
    "    o := x | 65535\n"
    "    return int(p[1]) + int(q[2]) * 10 + int(r[3]) * 100 + int(z[0]) + int(o[1]) * 1000\n"
    "}\n"
    "fn product(a: int, k: int) -> int {\n"
    "    x: [2]i64 = a\n"
    "    y := x *% x\n"
    "    return y[k]\n"
    "}\n"
    "fn odd_home(a: int, b: u8) -> int {\n"
    "    n := a * 3\n"
    "    r := n + p: {\n"
    "        v: [16]u8 = b\n"
    "        w := (v ^ 1) +% (v | 2)\n"
    "        break p int(w[0])\n"
    "    }\n"
    "    return r\n"
    "}\n"
    "fn far(a: u8) -> int {\n"
    "    big: [70000]u8\n"
    "    big[69999] = a\n"
    "    v: [16]u8 = a\n"
    "    w := (v ^ 1) +% (v | 2)\n"
    "    return int(w[3]) + int(big[69999])\n"
    "}\n"
    "fn mixed_slots(a: u8) -> int {\n"
    "    s := u8.[1, 2, 3]\n"
    "    v: [16]u8 = a\n"
    "    t := i16.[4, 5]\n"
    "    w := v +% s[2]\n"
    "    return int(w[1]) + int(t[1])\n"
    "}\n"
    "fn odd_slot(a: u8, k: int) -> int {\n"
    "    n := int(a)\n"
    "    p := &n\n"
    "    v: [16]u8 = a\n"
    "    w := v +% u8(p.*)\n"
    "    return int(w[k])\n"
    "}\n"
    "fn bump(x: u8) -> u8 {\n"
    "    t: [16]u8 = x\n"
    "    u := (t ^ 255) +% 1\n"
    "    return u[0]\n"
    "}\n"
    "fn across(a: u8) -> int {\n"
    "    v: [16]u8 = a\n"
    "    w := v +% bump(a)\n"
    "    return int(w[0])\n"
    "}\n"
    "fn accumulate(n: int) -> int {\n"
    "    acc: [4]u32\n"
    "    step := u32.[1, 2, 3, 4]\n"
    "    for k in 0..n {\n"
    "        acc = acc +% step *% u32(k)\n"
    "    }\n"
    "    return int(acc[0]) + int(acc[3]) * 1000\n"
    "}\n"
    "fn dropped(a: u8) -> int {\n"
    "    v: [16]u8 = a\n"
    "    _ = v +% 1\n"
    "    w := v ^ 3\n"
    "    return int(w[0])\n"
    "}\n"
    "fn tree(x: u8, y: u8) -> int {\n"
    "    a: [16]u8 = x\n"
    "    b: [16]u8 = y\n"
    "    c := a +% 1\n"
    "    d := b -% 1\n"
    "    r := ((a | b) ^ (c & d)) +% ((a ^ c) | (b & d))\n"
    "    return int(r[7])\n"
    "}\n"
    "fn slot_numbers(a: u8, b: u8, c: u8, d: u8) -> int {\n"
    "    v: [16]u8 = a +% 1\n"
    "    w: [16]u8\n"
    "    x := v ^ w\n"
    "    return int(x[0])\n"
    "}\n"
    "fn address_numbers(a: u8, b: u8, c: u8, d: u8) -> int {\n"
    "    v: [16]u8 = a\n"
    "    s: [3]u8\n"
    "    w := v +% s[2]\n"
    "    return int(w[0])\n"
    "}\n"
    "fn spilled(a: int, b: u8) -> int {\n"
    "    v: [16]u8 = b\n"
    "    w := v +% u8(a * 2 + (a * 3 + (a * 5 + (a * 7 + (a * 11 + (a * 13 + (a * 17 + (a * 19 + a * 23))))))))\n"
    "    return int(w[8]) * 1000 + int(w[12])\n"
    "}\n"
    "fn halves_mask(p: [*]u8) -> int {\n"
    "    return simd.top_bits(mem.unaligned_load([8]i16, p))\n"
    "}\n"
    "fn words_mask(p: [*]u8) -> int {\n"
    "    return simd.top_bits(simd.gt(mem.unaligned_load([4]u32, p), 0x80000000))\n"
    "}\n"
    "fn longs_mask(p: [*]u8) -> int {\n"
    "    return simd.top_bits(mem.unaligned_load([2]i64, p))\n"
    "}\n"
    "fn lookup_store(p: [*]u8, q: [*]u8) {\n"
    "    t := mem.unaligned_load([16]u8, p)\n"
    "    mem.unaligned_store(q + 1, simd.lookup(t, mem.unaligned_load([16]u8, q)))\n"
    "}\n"
    "fn copy16(p: [*]u8, q: [*]u8) {\n"
    "    mem.unaligned_store(q, mem.unaligned_load([16]u8, p))\n"
    "}\n"
    "fn fill(q: [*]u8, a: u8) {\n"
    "    v: [16]u8 = a\n"
    "    mem.unaligned_store(q, v)\n"
    "    mem.unaligned_store(q + 16, v)\n"
    "    mem.unaligned_store(q + 32, v)\n"
    "    mem.unaligned_store(q + 48, v)\n"
    "    mem.unaligned_store(q + 64, v)\n"
    "    mem.unaligned_store(q + 80, v)\n"
    "    mem.unaligned_store(q + 96, v)\n"
    "    mem.unaligned_store(q + 112, v)\n"
    "    mem.unaligned_store(q + 128, v)\n"
    "}\n"
    "fn one_row(a: u8, k: int) -> u8 {\n"
    "    v: [16]u8 = a\n"
    "    m := [16]u8.[v +% 1]\n"
    "    return m[0][k]\n"
    "}\n";

typedef int64_t VMASK(void *, const uint8_t *);
typedef void VLOOK(void *, const uint8_t *, uint8_t *);
typedef void VFILL(void *, uint8_t *, int64_t);

typedef struct
{
    uint8_t a[16];
} V16;
typedef V16 VADD(void *, V16, V16);
typedef int64_t VSPREAD(void *, V16, double, V16, V16, V16, V16, V16, V16, V16, V16, int64_t, V16);
typedef int64_t VMIX(
    void *,
    int64_t,
    int64_t,
    int64_t,
    int64_t,
    int64_t,
    int64_t,
    int64_t,
    int64_t,
    V16,
    V16,
    V16,
    V16,
    V16,
    V16,
    V16,
    V16,
    V16,
    int8_t
);
typedef double VLEAF(void *, V16, double, V16, double);
typedef V16 VRSUM(void *, V16, int64_t);
typedef struct
{
    int64_t a, b;
} CPair;
typedef int64_t VPAIR(void *, CPair, V16, V16);
typedef int64_t VEIGHT(void *, double, double, double, double, double, double, double, double, V16);

enum
{
    WORD_STR_Q_SP = 0x3D8003E0u,
    WORD_LDR_Q_SP = 0x3DC003E0u,
    WORD_STR_Q_X8 = 0x3D800100u,
    WORD_LDR_Q_X8 = 0x3DC00100u,
    WORD_DUP_16B = 0x4E010C00u,
    WORD_DUP_4S_X16 = 0x4E040E00u,
    WORD_MOVI_16B_32 = 0x4F01E400u,
    WORD_MOVI_16B_97 = 0x4F03E420u,
    WORD_MOVI_16B_25 = 0x4F00E720u,
    WORD_MOVI_8H_5 = 0x4F0084A0u,
    WORD_MOVI_4S_200 = 0x4F060500u,
    WORD_MOVI_2D_ZERO = 0x6F00E400u,
    WORD_MOVI_2D_ONES = 0x6F07E7E0u,
    WORD_CMHS_16B = 0x6E203C00u,
    WORD_SHL_16B_2 = 0x4F0A5400u,
    WORD_USHR_16B_3 = 0x6F0D0400u,
    WORD_SSHR_16B_1 = 0x4F0F0400u,
    WORD_UMOV_D = 0x4E083C00u,
    UMOV_FIELDS = 0x001003FFu,
    WORD_INS_D1 = 0x4E181C00u,
    WORD_FMOV_D_X = 0x9E670000u,
    THREE_VECTORS = 0x001F03FFu,
    WORD_SUB_SP_128 = 0xD10203FFu,
    WORD_TBL = 0x4E000000u,
    WORD_USHR_16B_7 = 0x6F090400u,
    WORD_USHR_2D_63 = 0x6F410400u,
    WORD_USRA = 0x6F001400u,
    USRA_FIELDS = 0x007F03FFu,
    WORD_USRA_8H_7 = 0x6F191400u,
    WORD_USRA_4S_14 = 0x6F321400u,
    WORD_USRA_4S_15 = 0x6F311400u,
    WORD_USRA_2D_28 = 0x6F641400u,
    WORD_USRA_2D_30 = 0x6F621400u,
    WORD_USRA_2D_31 = 0x6F611400u,
    WORD_UMOV_B0 = 0x0E013C00u,
    WORD_UMOV_B8 = 0x0E113C00u,
    WORD_ORR_LSL_8 = 0x2A002000u,
    WORD_ORR_LSL_4 = 0x2A001000u,
    WORD_ORR_LSL_2 = 0x2A000800u,
    WORD_ORR_LSL_1 = 0x2A000400u,
    B_OFFSET = 0x03FFFFFFu,
};

static bool
vector_calls_agree(void)
{
    Program p;
    if (!build(&p, VECTOR_RUNS)) {
        return false;
    }
    V16 v[10];
    for (int k = 0; k < 10; k++) {
        for (int lane = 0; lane < 16; lane++) {
            v[k].a[lane] = (uint8_t)(lane * 3 + k * 17);
        }
    }

    bool ok = true;
    V16 sum = ((VADD *)fn(&p, "vadd"))(CONTEXT, v[0], v[1]);
    V16 rsum = ((VRSUM *)fn(&p, "rsum"))(CONTEXT, v[2], 4);
    for (int lane = 0; lane < 16; lane++) {
        ok &= sum.a[lane] == (uint8_t)(v[0].a[lane] + v[1].a[lane]);
        ok &= rsum.a[lane] == (uint8_t)(5 * v[2].a[lane] + 10);
    }

    VSPREAD *spread = (VSPREAD *)fn(&p, "spread");
    for (int64_t n = 0; n < 16; n += 5) {
        uint8_t want = 0;
        for (int k = 0; k < 10; k++) {
            want += (uint8_t)((k + 1) * v[k].a[n]);
        }
        ok &= spread(CONTEXT, v[0], 77.0, v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], n, v[9]) ==
              want + 77;
    }

    int64_t want = v[8].a[1] * 100000 - 300000000;
    uint8_t s0 = 0;
    for (int k = 0; k < 8; k++) {
        want += (k + 1) * (k * 11 + 12);
        s0 += (uint8_t)((k + 1) * v[k].a[0]);
    }
    want += s0 * 100;
    VMIX *mix = (VMIX *)fn(&p, "mix");
    int64_t mixed = mix(
        CONTEXT,
        12,
        23,
        34,
        45,
        56,
        67,
        78,
        89,
        v[0],
        v[1],
        v[2],
        v[3],
        v[4],
        v[5],
        v[6],
        v[7],
        v[8],
        -3
    );
    ok &= mixed == want;

    ok &= ((VLEAF *)fn(&p, "leaf"))(CONTEXT, v[3], 1.25, v[5], 2.5) ==
          5.0 + v[3].a[0] + v[5].a[1];
    ok &= ((VPAIR *)fn(&p, "with_pair"))(CONTEXT, (CPair){ 4, -9 }, v[6], v[7]) ==
          4000 - 90 + (uint8_t)(v[6].a[3] + v[7].a[3]);
    ok &= ((VEIGHT *)fn(&p, "eight_floats"))(CONTEXT, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.5, v[4]) ==
          208 + v[4].a[5] * 1000;

    release_program(&p);
    return ok;
}

TEST_CASE(vectors)
{
    catch_traps();
    REQUIRE(vector_runs_agree(VECTOR_BODY, VECTOR_BODY_RESULTS, NVECTOR_BODY_RESULTS, false));
    REQUIRE(vector_runs_agree(VECTOR_RUNS, VECTOR_RESULTS, NVECTOR_RESULTS, false));
    REQUIRE(vector_runs_agree(VECTOR_WIDE_RUNS, VECTOR_WIDE_RESULTS, NVECTOR_WIDE_RESULTS, false));
    REQUIRE(vector_calls_agree());

    Program p;
    REQUIRE(build(&p, VECTOR_GRID));
    for (size_t f = 0; f < NVECTOR_GRID_FUNCTIONS; f++) {
        REQUIRE(vector_grid_agrees(&p, &VECTOR_GRID_FUNCTIONS[f]));
    }
    release_program(&p);

    REQUIRE(build(&p, VECTOR_WIDE_GRID));
    for (size_t f = 0; f < NVECTOR_WIDE_GRID_FUNCTIONS; f++) {
        REQUIRE(vector_grid_agrees(&p, &VECTOR_WIDE_GRID_FUNCTIONS[f]));
    }
    release_program(&p);

    REQUIRE(build(&p, VECTOR_SHAPES));
    I1 *mask = (I1 *)fn(&p, "mask");
    REQUIRE(mask(CONTEXT, 65) == 255);
    REQUIRE(mask(CONTEXT, 122) == 255);
    REQUIRE(mask(CONTEXT, 64) == 0);
    REQUIRE(mask(CONTEXT, 123) == 0);
    REQUIRE(count_words(&p, "mask", WORD_DUP_16B, REGISTER_FIELDS) == 1);
    REQUIRE(count_words(&p, "mask", WORD_STR_Q_SP, Q_FIELDS) == 2);
    REQUIRE(count_words(&p, "mask", WORD_LDR_Q_SP, Q_FIELDS) == 1);
    REQUIRE(count_words(&p, "mask", WORD_MOVI_16B_32, RD_FIELD) == 1);
    REQUIRE(count_words(&p, "mask", WORD_MOVI_16B_97, RD_FIELD) == 1);
    REQUIRE(count_words(&p, "mask", WORD_MOVI_16B_25, RD_FIELD) == 1);
    REQUIRE(count_words(&p, "mask", WORD_CMHS_16B, THREE_VECTORS) == 1);

    I2 *shifted = (I2 *)fn(&p, "shifted");
    REQUIRE(shifted(CONTEXT, 0xFF, -100) == 31 * 1000 - 50);
    REQUIRE(shifted(CONTEXT, 0x22, 99) == 17 * 1000 + 49);
    REQUIRE(count_words(&p, "shifted", WORD_SHL_16B_2, REGISTER_FIELDS) == 1);
    REQUIRE(count_words(&p, "shifted", WORD_USHR_16B_3, REGISTER_FIELDS) == 1);
    REQUIRE(count_words(&p, "shifted", WORD_SSHR_16B_1, REGISTER_FIELDS) == 1);

    I2 *constants = (I2 *)fn(&p, "constants");
    REQUIRE(
        constants(CONTEXT, 65534, 0xFFFFFF00) ==
        3 + 4294967240ll * 10 + 74309ll * 100 + 65535000
    );
    REQUIRE(constants(CONTEXT, 7, 1) == 12 + 201 * 10 + 74566ll * 100 + 65535000);
    REQUIRE(count_words(&p, "constants", WORD_MOVI_8H_5, RD_FIELD) == 1);
    REQUIRE(count_words(&p, "constants", WORD_MOVI_4S_200, RD_FIELD) == 1);
    REQUIRE(count_words(&p, "constants", WORD_DUP_4S_X16, RD_FIELD) == 1);
    REQUIRE(count_words(&p, "constants", WORD_MOVI_2D_ZERO, RD_FIELD) == 1);
    REQUIRE(count_words(&p, "constants", WORD_MOVI_2D_ONES, RD_FIELD) == 1);

    I2 *product = (I2 *)fn(&p, "product");
    REQUIRE(product(CONTEXT, 3, 0) == 9);
    REQUIRE(product(CONTEXT, -5, 1) == 25);
    REQUIRE(product(CONTEXT, 0x100000001, 1) == 0x200000001);
    REQUIRE(count_words(&p, "product", WORD_UMOV_D, UMOV_FIELDS) == 4);
    REQUIRE(count_words(&p, "product", WORD_INS_D1, REGISTER_FIELDS) == 1);
    REQUIRE(count_words(&p, "product", WORD_FMOV_D_X, REGISTER_FIELDS) == 1);

    REQUIRE(((I2 *)fn(&p, "odd_home"))(CONTEXT, 2, 5) == 17);
    REQUIRE(((I2 *)fn(&p, "odd_home"))(CONTEXT, -1, 255) == 250);
    REQUIRE(count_words(&p, "odd_home", WORD_STR_Q_X8, RD_FIELD) == 0);
    REQUIRE(count_words(&p, "odd_home", WORD_LDR_Q_X8, RD_FIELD) == 0);
    REQUIRE(((I1 *)fn(&p, "far"))(CONTEXT, 5) == 16);
    REQUIRE(count_words(&p, "far", WORD_STR_Q_X8, RD_FIELD) >= 1);
    REQUIRE(((I1 *)fn(&p, "mixed_slots"))(CONTEXT, 5) == 13);
    REQUIRE(count_words(&p, "mixed_slots", WORD_STR_Q_X8, RD_FIELD) == 0);
    REQUIRE(count_words(&p, "mixed_slots", WORD_LDR_Q_X8, RD_FIELD) == 0);
    REQUIRE(((I2 *)fn(&p, "odd_slot"))(CONTEXT, 5, 3) == 10);
    REQUIRE(((I2 *)fn(&p, "odd_slot"))(CONTEXT, 200, 15) == 144);
    REQUIRE(count_words(&p, "odd_slot", WORD_STR_Q_X8, RD_FIELD) == 0);
    REQUIRE(count_words(&p, "odd_slot", WORD_LDR_Q_X8, RD_FIELD) == 0);

    REQUIRE(((I1 *)fn(&p, "across"))(CONTEXT, 5) == 0);
    REQUIRE(((I1 *)fn(&p, "across"))(CONTEXT, 200) == 0);
    REQUIRE(((I1 *)fn(&p, "accumulate"))(CONTEXT, 5) == 40010);
    REQUIRE(((I1 *)fn(&p, "accumulate"))(CONTEXT, 0) == 0);
    REQUIRE(((I1 *)fn(&p, "dropped"))(CONTEXT, 5) == 6);
    for (int64_t x = 0; x < 256; x += 15) {
        for (int64_t y = 0; y < 256; y += 17) {
            uint8_t c = (uint8_t)(x + 1), d = (uint8_t)(y - 1);
            uint8_t want = (uint8_t)(((x | y) ^ (c & d)) + ((x ^ c) | (y & d)));
            REQUIRE(((I2 *)fn(&p, "tree"))(CONTEXT, x, y) == want);
        }
    }
    REQUIRE(count_words(&p, "tree", WORD_STR_Q_SP, Q_FIELDS) == 12);
    REQUIRE(count_words(&p, "tree", WORD_LDR_Q_SP, Q_FIELDS) == 17);
    REQUIRE(count_words(&p, "tree", WORD_SUB_SP_128, 0) == 1);

    REQUIRE(((I4 *)fn(&p, "slot_numbers"))(CONTEXT, 3, 0, 0, 0) == 4);
    REQUIRE(((I4 *)fn(&p, "address_numbers"))(CONTEXT, 7, 0, 0, 0) == 7);
    REQUIRE(((I2 *)fn(&p, "spilled"))(CONTEXT, 1, 250) == 94094);

    uint8_t raw[17] = { 0,    0x80, 0x01, 0xFF, 0x7F, 0x00, 0x90, 0x10, 0x40,
                        0x08, 0x88, 0x00, 0x00, 0x00, 0x80, 0x7F, 0xFF };
    REQUIRE(((VMASK *)fn(&p, "bytes_mask"))(CONTEXT, raw + 1) == 41509);
    REQUIRE(((VMASK *)fn(&p, "halves_mask"))(CONTEXT, raw + 1) == 212);
    REQUIRE(((VMASK *)fn(&p, "words_mask"))(CONTEXT, raw + 1) == 8);
    REQUIRE(((VMASK *)fn(&p, "longs_mask"))(CONTEXT, raw + 1) == 2);
    REQUIRE(count_words(&p, "bytes_mask", WORD_USHR_16B_7, REGISTER_FIELDS) == 1);
    REQUIRE(count_words(&p, "bytes_mask", WORD_USRA_8H_7, REGISTER_FIELDS) == 1);
    REQUIRE(count_words(&p, "bytes_mask", WORD_USRA_4S_14, REGISTER_FIELDS) == 1);
    REQUIRE(count_words(&p, "bytes_mask", WORD_USRA_2D_28, REGISTER_FIELDS) == 1);
    REQUIRE(count_words(&p, "bytes_mask", WORD_UMOV_B0, REGISTER_FIELDS) == 1);
    REQUIRE(count_words(&p, "bytes_mask", WORD_UMOV_B8, REGISTER_FIELDS) == 1);
    REQUIRE(count_words(&p, "bytes_mask", WORD_ORR_LSL_8, THREE_VECTORS) == 1);
    REQUIRE(count_words(&p, "bytes_mask", WORD_STR_Q_SP, Q_FIELDS) == 0);
    REQUIRE(count_words(&p, "halves_mask", WORD_USRA_4S_15, REGISTER_FIELDS) == 1);
    REQUIRE(count_words(&p, "halves_mask", WORD_USRA_2D_30, REGISTER_FIELDS) == 1);
    REQUIRE(count_words(&p, "halves_mask", WORD_ORR_LSL_4, THREE_VECTORS) == 1);
    REQUIRE(count_words(&p, "words_mask", WORD_USRA, USRA_FIELDS) == 1);
    REQUIRE(count_words(&p, "words_mask", WORD_USRA_2D_31, REGISTER_FIELDS) == 1);
    REQUIRE(count_words(&p, "words_mask", WORD_ORR_LSL_2, THREE_VECTORS) == 1);
    REQUIRE(count_words(&p, "words_mask", WORD_STR_Q_SP, Q_FIELDS) == 0);
    REQUIRE(count_words(&p, "longs_mask", WORD_USHR_2D_63, REGISTER_FIELDS) == 1);
    REQUIRE(count_words(&p, "longs_mask", WORD_USRA, USRA_FIELDS) == 0);
    REQUIRE(count_words(&p, "longs_mask", WORD_ORR_LSL_1, THREE_VECTORS) == 1);

    uint8_t table[16] = { 9, 8, 7,   6,   5,   4,   3,   2,
                          1, 0, 200, 201, 202, 203, 204, 205 };
    uint8_t q[18] = { 3, 15, 16, 0,  255, 10, 9, 128,  1,
                      2, 14, 13, 17, 12,  11, 4, 0x55, 0x66 };
    static const uint8_t looked[16] = { 6, 205, 0,   9,   0, 200, 0,   0,
                                        8, 7,   204, 203, 0, 202, 201, 5 };
    ((VLOOK *)fn(&p, "lookup_store"))(CONTEXT, table, q);
    REQUIRE(q[0] == 3 && memcmp(q + 1, looked, sizeof looked) == 0 && q[17] == 0x66);
    REQUIRE(count_words(&p, "lookup_store", WORD_TBL, THREE_VECTORS) == 1);

    uint8_t bytes[146];
    for (int k = 0; k < 146; k++) {
        bytes[k] = (uint8_t)(k * 7);
    }
    ((VLOOK *)fn(&p, "copy16"))(CONTEXT, bytes + 3, bytes + 20);
    REQUIRE(memcmp(bytes + 20, (uint8_t[]){ 21, 28, 35, 42, 49, 56, 63, 70 }, 8) == 0);
    REQUIRE(bytes[19] == 133 && bytes[35] == 126 && bytes[36] == 252);
    REQUIRE(count_words(&p, "copy16", WORD_STR_Q_SP, Q_FIELDS) == 0);
    ((VFILL *)fn(&p, "fill"))(CONTEXT, bytes + 1, 9);
    REQUIRE(bytes[0] == 0 && bytes[1] == 9 && bytes[144] == 9 && bytes[145] == 247);
    REQUIRE(count_words(&p, "fill", WORD_STR_X_SP, Q_FIELDS) == 0);

    REQUIRE(((I2 *)fn(&p, "one_row"))(CONTEXT, 5, 3) == 6);
    REQUIRE(((I2 *)fn(&p, "one_row"))(CONTEXT, 255, 15) == 0);
    REQUIRE(count_words(&p, "one_row", WORD_STR_Q_SP, Q_FIELDS) == 1);
    REQUIRE(count_words(&p, "one_row", WORD_LDR_Q_SP, Q_FIELDS) == 1);
    release_program(&p);
    return true;
}

static const char INT128_SHAPES[] =
    "struct One { x: u128 }\n"
    "\n"
    "fn wide(a: int) -> u128 {\n"
    "    return u128(bitcast(u64, a))\n"
    "}\n"
    "\n"
    "fn fold(x: u128) -> int {\n"
    "    return bitcast(int, u64(x >> 64)) +% bitcast(int, u64(x & 0xFFFF_FFFF_FFFF_FFFF))\n"
    "}\n"
    "\n"
    "fn leaf_divide(a: int, b: int) -> int {\n"
    "    k := a *% 3 +% b\n"
    "    q := (i128(a) << 64) / i128(b)\n"
    "    r := i128(a) % i128(b | 1)\n"
    "    return int(q >> 64) +% int(r) *% 1000 +% a *% 7 +% b +% k *% 100\n"
    "}\n"
    "\n"
    "fn leaf_locals(a: int, b: int) -> int {\n"
    "    c := a +% 1\n"
    "    d := b +% 2\n"
    "    e := a *% b\n"
    "    f := a -% b\n"
    "    g := a ^ b\n"
    "    h := a | b\n"
    "    q := int((i128(a) << 64) / i128(b | 1) >> 64)\n"
    "    return q +% c *% 3 +% d *% 5 +% e *% 7 +% f *% 11 +% g *% 13 +% h *% 17 +% a *% 19 +% b *% 23\n"
    "}\n"
    "\n"
    "fn crowded(a: int, b: int) -> int {\n"
    "    return (a +% 1) +% ((b +% 2) *% 3 +% ((a ^ b) *% 5 +% ((a | 3) *% 7 +% ((b & 12) *% 11 +% ((a -% b) *% 13 +% "
    "((a +% 9) *% 17 +% ((b -% 4) *% 19 +% int(i128(a) * i128(b) / i128(b | 1)))))))))\n"
    "}\n"
    "\n"
    "fn with_calls(a: int, b: int) -> int {\n"
    "    x := a *% 5\n"
    "    y := b *% 7\n"
    "    q := wide(a) * wide(b) / wide(b | 1)\n"
    "    s := i128(a) * i128(x) % i128(y | 1)\n"
    "    return fold(q) +% int(s) +% x +% y\n"
    "}\n"
    "\n"
    "fn ten(a: u128, b: u128, c: u128, d: u128, e: u128, f: u128, g: u128, h: u128, i: u128, j: u128) -> u128 {\n"
    "    return a +% b *% 3 +% c *% 5 +% d *% 7 +% e *% 11 +% f *% 13 +% g *% 17 +% h *% 19 +% i *% 23 +% j *% 29\n"
    "}\n"
    "\n"
    "fn call_ten(a: int, b: int) -> int {\n"
    "    x := wide(a)\n"
    "    y := wide(b)\n"
    "    return fold(ten(x, y, x << 64, y << 64, x +% 1, y +% 2, x << 32, y << 96, x *% 3, y << 120))\n"
    "}\n"
    "\n"
    "fn floats_wide(a: f64, b: f64, c: f64, d: f64, e: f64, f: f64, g: f64, h: f64, x: u128) -> int {\n"
    "    return int(a + b + c + d + e + f + g + h) +% fold(x)\n"
    "}\n"
    "\n"
    "fn call_floats_wide(a: int, b: int) -> int {\n"
    "    return floats_wide(1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, f64(a), wide(b) << 64)\n"
    "}\n"
    "\n"
    "fn one(s: One, k: int) -> One {\n"
    "    return One.{ .x = s.x +% wide(k) }\n"
    "}\n"
    "\n"
    "fn call_one(a: int, b: int) -> int {\n"
    "    s := one(One.{ .x = wide(a) << 64 }, b)\n"
    "    return fold(s.x)\n"
    "}\n"
    "\n"
    "fn vector_pick(a: int, b: int) -> int {\n"
    "    v: [16]u8 = u8(a & 255)\n"
    "    v[15] = 7\n"
    "    w: [16]u8 = u8(b & 255)\n"
    "    p := if a < b then v +% w else v -% w\n"
    "    return int(p[3]) + int(p[15]) * 1000\n"
    "}\n"
    "\n"
    "fn mixed_leaf(x: u128, k: int, y: u128, j: int) -> int {\n"
    "    return int(u64(x >> 64) & 0xFFFF) +% k *% 3 +% int(u64(y & 0xFFFF)) *% 5 +% j *% 7\n"
    "}\n"
    "\n"
    "fn call_mixed(a: int, b: int) -> int {\n"
    "    return mixed_leaf((wide(a) << 64) | 5, a, wide(b), b)\n"
    "}\n"
    "\n"
    "fn wide_pick(a: int, b: int) -> int {\n"
    "    c := a < b\n"
    "    x := if c then wide(a) << 64 else wide(b) *% 3\n"
    "    y := if wide(a) < wide(b) then x else x >> 1\n"
    "    return fold(y)\n"
    "}\n"
    "\n"
    "fn churn(a: int, b: int) -> int {\n"
    "    x := f64(a) * 1e20 + f64(b)\n"
    "    y := f64(i128(f64(i128(f64(i128(f64(i128(f64(i128(f64(i128(x))))))))))))\n"
    "    u := f32(u128(f64(u128(f32(u128(y * y / 1e8))))))\n"
    "    return int(i128(y) >> 40) +% int(u128(u) >> 70)\n"
    "}\n"
    "\n"
    "fn far_wide(a: int, b: int) -> int {\n"
    "    buf: [5000]u8\n"
    "    buf[b & 7] = u8(a & 255)\n"
    "    x := (wide(a) << 64) | wide(b)\n"
    "    y := x *% x +% (x >> 3)\n"
    "    q := i128(a) * i128(b) / i128(b | 1)\n"
    "    return fold(y) +% int(q) +% int(buf[b & 7])\n"
    "}\n"
    "\n"
    "fn wide_switch(a: int, b: int) -> int {\n"
    "    x := switch a & 7 {\n"
    "        0 => wide(a) << 64\n"
    "        1 => wide(b) *% 5\n"
    "        2 => wide(a) +% wide(b)\n"
    "        3 => wide(b) >> 1\n"
    "        _ => u128(7)\n"
    "    }\n"
    "    return fold(x)\n"
    "}\n"
    "\n"
    "fn lanes_switch(a: int, b: int) -> int {\n"
    "    v: [4]u32 = u32(a & 0xFFFF)\n"
    "    w: [4]u32 = u32(b & 0xFFFF)\n"
    "    p := switch b & 3 {\n"
    "        0 => v +% w\n"
    "        1 => v -% w\n"
    "        _ => w\n"
    "    }\n"
    "    return int(p[2])\n"
    "}\n";

static const char *const INT128_GRID_FUNCTIONS[] = {
    "wide_mul", "signed_mul", "divide", "divide_unsigned", "shifts", "counts", "floats",
    "values", "calls", "compares", "bit_ops", "overflows", "tables", "unaligned",
    "shift_counts", "narrows", "signs", "big_floats", "edge_constants", "blocks", "big_arrays",
};

static const char *const INT128_SHAPE_FUNCTIONS[] = {
    "leaf_divide", "leaf_locals", "crowded", "with_calls", "call_ten", "call_floats_wide", "call_one",
    "vector_pick", "call_mixed", "wide_pick", "churn", "far_wide", "wide_switch", "lanes_switch",
};

static const CallCase INT128_CASES[] = {
    { "leaf_divide", 100, 7, 33421, 0 },
    { "leaf_divide", -100, 7, -32008, 0 },
    { "leaf_divide", 5, 0, 0, TRAP_DIVIDE },
    { "leaf_divide", INT64_MIN, -1, 0, TRAP_OVERFLOW },
    { "leaf_divide", INT64_MIN, 2, 4611686018427386106, 0 },
    { "crowded", 13, 12, 849, 0 },
    { "crowded", -3, 5, 30, 0 },
    { "crowded", INT64_MIN, -3, 195, 0 },
    { "with_calls", 13, 12, 241, 0 },
    { "with_calls", -3, -2, -27, 0 },
    { "with_calls", INT64_MIN, 7, 50, 0 },
    { "call_ten", 13, 12, 6629300579929687447, 0 },
    { "call_ten", -1, -3, -6269010926112866326, 0 },
    { "call_floats_wide", 5, -9, 24, 0 },
    { "call_one", INT64_MAX, 1, INT64_MIN, 0 },
    { "wide_pick", -3, 5, -2, 0 },
    { "wide_pick", 5, -3, -7, 0 },
    { "vector_pick", 200, 100, 163100, 0 },
    { "vector_pick", 3, 1, 6002, 0 },
    { "vector_pick", 1, 3, 10004, 0 },
    { "call_mixed", 7, 9, 136, 0 },
    { "call_mixed", -1, 2, 65556, 0 },
    { "wide_switch", 8, 5, 8, 0 },
    { "wide_switch", 9, 5, 25, 0 },
    { "wide_switch", -6, 5, -1, 0 },
    { "wide_switch", 3, -1, 9223372036854775807, 0 },
    { "wide_switch", 4, 0, 7, 0 },
    { "wide_switch", -9, 2, 7, 0 },
    { "lanes_switch", 10, 4, 14, 0 },
    { "lanes_switch", 10, 5, 5, 0 },
    { "lanes_switch", 3, 5, 4294967294, 0 },
    { "lanes_switch", 10, 6, 6, 0 },
    { "lanes_switch", -1, 7, 7, 0 },
    { "lanes_switch", 70000, 4, 4468, 0 },
};

enum
{
    WORD_BL = 0x94000000u,
};

TEST_CASE(int128)
{
    catch_traps();
    REQUIRE(vector_runs_agree(INT128_PROGRAM, INT128_RUNS, NINT128_RUNS, false));
    REQUIRE(vector_runs_agree(INT128_RELEASE_FAST, INT128_RELEASE_FAST_RUNS, NINT128_RELEASE_FAST_RUNS, true));
    REQUIRE(vector_runs_agree(INT128_CHAIN, INT128_CHAIN_RUNS, 2, false));
    REQUIRE(vector_runs_agree(INT128_CHAIN_SHIFTED, INT128_CHAIN_SHIFTED_RUNS, 2, false));

    Program p;
    REQUIRE(build(&p, INT128_GRID));
    REQUIRE(agree_on_grid(&p, INT128_GRID_FUNCTIONS, sizeof INT128_GRID_FUNCTIONS / sizeof INT128_GRID_FUNCTIONS[0]));
    release_program(&p);

    REQUIRE(build(&p, INT128_SHAPES));
    for (size_t k = 0; k < sizeof INT128_CASES / sizeof INT128_CASES[0]; k++) {
        CallCase t = INT128_CASES[k];
        int64_t got = 0;
        int trap = call_i2((I2 *)fn(&p, t.name), t.a, t.b, &got);
        if (trap != t.trap || (!trap && got != t.want)) {
            fprintf(stderr, "arm64 test: %s(%lld, %lld) gave %lld (trap %d)\n", t.name, (long long)t.a, (long long)t.b, (long long)got, trap);
            REQUIRE(false);
        }
    }
    REQUIRE(agree_on_grid(&p, INT128_SHAPE_FUNCTIONS, sizeof INT128_SHAPE_FUNCTIONS / sizeof INT128_SHAPE_FUNCTIONS[0]));
    REQUIRE(count_words(&p, "leaf_divide", WORD_BL, B_OFFSET) == 2);
    REQUIRE(count_words(&p, "leaf_locals", WORD_BL, B_OFFSET) == 1);
    REQUIRE(count_words(&p, "crowded", WORD_BL, B_OFFSET) == 2);
    REQUIRE(count_words(&p, "mixed_leaf", WORD_MOV_X | 1u << 16, RD_FIELD) == 0);
    REQUIRE(count_words(&p, "mixed_leaf", WORD_MOV_X | 2u << 16, RD_FIELD) == 0);
    release_program(&p);
    return true;
}

static const char *const STRING_VIEW_FUNCTIONS[] = {
    "sv_index", "sv_slice", "sv_bytes", "sv_loop", "sv_word", "sv_pass", "sv_cstring",
};

static const CallCase STRING_VIEW_CASES[] = {
    { "sv_index", 0, 5, 10417, 0 },
    { "sv_index", 11, -2, 10010, 0 },
    { "sv_index", 12, 0, 0, TRAP_BOUNDS },
    { "sv_index", -1, 0, 0, TRAP_BOUNDS },
    { "sv_slice", 2, 3, 6399, 0 },
    { "sv_slice", 7, 1, 1204, 0 },
    { "sv_slice", 6, 3, 0, TRAP_BOUNDS },
    { "sv_slice", 3, 4, 0, TRAP_BOUNDS },
    { "sv_bytes", 65, 0, 65148, 0 },
    { "sv_bytes", 200, 1, 72149, 0 },
    { "sv_loop", 0, 2, 109, 0 },
    { "sv_loop", 1, -1, 778, 0 },
    { "sv_loop", 13, 0, 6044614, 0 },
    { "sv_loop", 8, 1, 239250227378, 0 },
    { "sv_word", 0, 3, 51123, 0 },
    { "sv_word", 1, -4, 52117, 0 },
    { "sv_pass", 5, 1, 3104, 0 },
    { "sv_pass", 2, 0, 1097, 0 },
    { "sv_pass", 3, 7, 2105, 0 },
    { "sv_cstring", 1, 0, 120110, 0 },
    { "sv_cstring", 6, 9, 116102, 0 },
    { "sv_cstring", 3, -1, 110, 0 },
};

TEST_CASE(string_views)
{
    catch_traps();
    Program p;
    REQUIRE(build(&p, STRING_VIEWS));
    REQUIRE(strcmp(global_string(&p, "sv_table", 32), "hello, world") == 0);
    REQUIRE(strcmp(global_string(&p, "sv_words", 24), "yz") == 0);

    int64_t n = 0;
    memcpy(&n, p.n.globals + p.c.decls.values[decl_named(&p, "sv_table")] + 40, sizeof n);
    REQUIRE(n == 12);

    for (size_t k = 0; k < sizeof STRING_VIEW_CASES / sizeof STRING_VIEW_CASES[0]; k++) {
        CallCase t = STRING_VIEW_CASES[k];
        int64_t got = 0;
        int trap = call_i2((I2 *)fn(&p, t.name), t.a, t.b, &got);
        if (trap != t.trap || (!trap && got != t.want)) {
            fprintf(stderr, "arm64 test: %s(%lld, %lld) gave %lld (trap %d)\n", t.name, (long long)t.a, (long long)t.b, (long long)got, trap);
            REQUIRE(false);
        }
    }
    REQUIRE(agree_on_grid(&p, STRING_VIEW_FUNCTIONS, sizeof STRING_VIEW_FUNCTIONS / sizeof STRING_VIEW_FUNCTIONS[0]));
    release_program(&p);
    return true;
}

static const char *const TUPLE_FUNCTIONS[] = {
    "tp_div", "tp_swap", "tp_mixed", "tp_globals", "tp_early", "tp_held", "tp_node", "tp_list", "tp_optional", "tp_span", "tp_late", "tp_elems",
};

static const CallCase TUPLE_CASES[] = {
    { "tp_div", 47, 5, 9002, 0 },
    { "tp_div", -13, 4, -3001, 0 },
    { "tp_div", 7, 0, 0, TRAP_DIVIDE },
    { "tp_div", INT64_MIN, -1, 0, TRAP_OVERFLOW },
    { "tp_swap", 3, 4, 70431, 0 },
    { "tp_swap", -2, 9, 70881, 0 },
    { "tp_mixed", 300, 7, 7088, 0 },
    { "tp_mixed", -1, -3, -2490, 0 },
    { "tp_globals", 1, 2, 102985, 0 },
    { "tp_globals", 6, -3, 597752, 0 },
    { "tp_early", 5, 8, 523, 0 },
    { "tp_early", -3, 0, -293, 0 },
    { "tp_held", 2, 31, 15035, 0 },
    { "tp_held", 0, -1, 15011, 0 },
    { "tp_held", 3, 0, 0, TRAP_BOUNDS },
    { "tp_held", -1, 0, 0, TRAP_BOUNDS },
    { "tp_node", 7, 9, 712, 0 },
    { "tp_node", -4, 2, -395, 0 },
    { "tp_list", 3, 9, 90309, 0 },
    { "tp_list", 12, -2, 121198, 0 },
    { "tp_optional", 6, 11, 1106, 0 },
    { "tp_optional", 2, 5, -2, 0 },
    { "tp_optional", INT64_MIN, 0, 0, TRAP_OVERFLOW },
    { "tp_span", 0, 2, 1432, 0 },
    { "tp_span", 1, -3, 13625, 0 },
    { "tp_late", 7, 3, 907035, 0 },
    { "tp_late", -5, -6, 894945, 0 },
    { "tp_late", 1, 40000, 0, TRAP_RANGE },
    { "tp_elems", 1, 2, 351210352112, 0 },
    { "tp_elems", 3, 3, 531210253113, 0 },
    { "tp_elems", -1, 4, 531240253084, 0 },
};

TEST_CASE(tuples)
{
    catch_traps();
    Program p;
    REQUIRE(build(&p, TUPLES));
    for (size_t k = 0; k < sizeof TUPLE_CASES / sizeof TUPLE_CASES[0]; k++) {
        CallCase t = TUPLE_CASES[k];
        int64_t got = 0;
        int trap = call_i2((I2 *)fn(&p, t.name), t.a, t.b, &got);
        if (trap != t.trap || (!trap && got != t.want)) {
            fprintf(stderr, "arm64 test: %s(%lld, %lld) gave %lld (trap %d)\n", t.name, (long long)t.a, (long long)t.b, (long long)got, trap);
            REQUIRE(false);
        }
    }
    REQUIRE(agree_on_grid(&p, TUPLE_FUNCTIONS, sizeof TUPLE_FUNCTIONS / sizeof TUPLE_FUNCTIONS[0]));
    release_program(&p);
    return true;
}

static const char *const NAMED_RESULT_FUNCTIONS[] = {
    "nr_div", "nr_find", "nr_kinds", "nr_addr", "nr_fibs", "nr_switch", "nr_nested", "nr_order",
};

static const CallCase NAMED_RESULT_CASES[] = {
    { "nr_div", 47, 5, 9002, 0 },
    { "nr_div", -13, 4, -3001, 0 },
    { "nr_div", 7, 0, 0, TRAP_DIVIDE },
    { "nr_div", INT64_MIN, -1, 0, TRAP_OVERFLOW },
    { "nr_find", 10, 9, 41, 0 },
    { "nr_find", 1, 3, 0, 0 },
    { "nr_find", -3, 5, 31, 0 },
    { "nr_find", 0, 1, 1, 0 },
    { "nr_find", 0, 16, 0, 0 },
    { "nr_kinds", 2, 3, 543906, 0 },
    { "nr_kinds", -1, -2, 240252, 0 },
    { "nr_kinds", 1, 4, 442908, 0 },
    { "nr_kinds", INT64_MAX, 0, 0, TRAP_OVERFLOW },
    { "nr_addr", 10, 3, 14, 0 },
    { "nr_addr", -4, 6, -20, 0 },
    { "nr_addr", INT64_MAX, -1, 0, TRAP_OVERFLOW },
    { "nr_fibs", 10, 1, 255, 0 },
    { "nr_fibs", 0, -5, -4, 0 },
    { "nr_fibs", 64, 2, 3, 0 },
    { "nr_fibs", 63, 0, 30282620817249, 0 },
    { "nr_switch", 4, 7, 7, 0 },
    { "nr_switch", 5, 7, 16, 0 },
    { "nr_switch", 6, 7, 14, 0 },
    { "nr_switch", -1, 7, 14, 0 },
    { "nr_switch", 1, INT64_MAX, 0, TRAP_OVERFLOW },
    { "nr_nested", 3, 9, 9031, 0 },
    { "nr_nested", 5, -1, 127050, 0 },
    { "nr_nested", -2, 200, 71981, 0 },
    { "nr_order", 7, 2, 28, 0 },
    { "nr_order", 2, 7, 27, 0 },
    { "nr_order", 4, 4, 44, 0 },
    { "nr_order", INT64_MAX, 0, 0, TRAP_OVERFLOW },
};

TEST_CASE(named_results)
{
    catch_traps();
    Program p;
    REQUIRE(build(&p, NAMED_RESULTS));
    for (size_t k = 0; k < sizeof NAMED_RESULT_CASES / sizeof NAMED_RESULT_CASES[0]; k++) {
        CallCase t = NAMED_RESULT_CASES[k];
        int64_t got = 0;
        int trap = call_i2((I2 *)fn(&p, t.name), t.a, t.b, &got);
        if (trap != t.trap || (!trap && got != t.want)) {
            fprintf(stderr, "arm64 test: %s(%lld, %lld) gave %lld (trap %d)\n", t.name, (long long)t.a, (long long)t.b, (long long)got, trap);
            REQUIRE(false);
        }
    }
    REQUIRE(agree_on_grid(&p, NAMED_RESULT_FUNCTIONS, sizeof NAMED_RESULT_FUNCTIONS / sizeof NAMED_RESULT_FUNCTIONS[0]));
    release_program(&p);
    return true;
}

static const char *const DEFER_FUNCTIONS[] = {
    "df_order", "df_loops", "df_label", "df_value", "df_results", "df_pinned", "df_globals", "df_nested",
    "df_slices", "df_opt", "df_arms", "df_calls", "df_vec", "df_assert", "df_unread",
};

static const CallCase DEFER_CASES[] = {
    { "df_order", 7, 2, 321009, 0 },
    { "df_order", 3, 1, 5421007, 0 },
    { "df_order", -4, 0, 5420992, 0 },
    { "df_order", INT64_MIN, 0, 0, TRAP_OVERFLOW },
    { "df_loops", 1, 7, 5803, 0 },
    { "df_loops", 0, 5, 1802, 0 },
    { "df_loops", 9, 6, 17906, 0 },
    { "df_label", 6, 1, 277081, 0 },
    { "df_label", 15, 3, 418569, 0 },
    { "df_label", 2, 0, 801497, 0 },
    { "df_value", 4, 5, 4104, 0 },
    { "df_value", 4, 1, 10105, 0 },
    { "df_value", INT64_MAX, 0, 0, TRAP_OVERFLOW },
    { "df_results", 9, 2, 1003, 0 },
    { "df_results", 2, 9, 209, 0 },
    { "df_results", INT64_MAX, 0, 0, TRAP_OVERFLOW },
    { "df_pinned", 3, 4, 304, 0 },
    { "df_globals", 6, 1, 651, 0 },
    { "df_nested", 0, 3, 51449, 0 },
    { "df_nested", 1, 2, 532449, 0 },
    { "df_nested", 2, 1, 549, 0 },
    { "df_nested", 3, 0, 59, 0 },
    { "df_slices", 1, 50, 507, 0 },
    { "df_slices", 5, 50, 27, 0 },
    { "df_slices", 2, -7, -64, 0 },
    { "df_opt", 4, 3, 10, 0 },
    { "df_opt", 4, 2, 3, 0 },
    { "df_arms", 0, 1, 125, 0 },
    { "df_arms", 1, 1, 135, 0 },
    { "df_arms", 1, 0, 1435, 0 },
    { "df_arms", 2, 0, 75, 0 },
    { "df_calls", 4, 5, 17, 0 },
    { "df_calls", INT64_MAX, 0, 0, TRAP_OVERFLOW },
    { "df_vec", 200, 100, 800, 0 },
    { "df_vec", -1, -1, 1530, 0 },
    { "df_assert", 2, 2, 4, 0 },
    { "df_assert", 1, 2, 0, TRAP_ASSERT },
    { "df_unread", 4, 1, 9044, 0 },
    { "df_unread", INT64_MAX, 0, 0, TRAP_OVERFLOW },
};

TEST_CASE(defers)
{
    catch_traps();
    Program p;
    REQUIRE(build(&p, DEFERS));
    for (size_t k = 0; k < sizeof DEFER_CASES / sizeof DEFER_CASES[0]; k++) {
        CallCase t = DEFER_CASES[k];
        int64_t got = 0;
        int trap = call_i2((I2 *)fn(&p, t.name), t.a, t.b, &got);
        if (trap != t.trap || (!trap && got != t.want)) {
            fprintf(stderr, "arm64 test: %s(%lld, %lld) gave %lld (trap %d)\n", t.name, (long long)t.a, (long long)t.b, (long long)got, trap);
            REQUIRE(false);
        }
    }
    REQUIRE(agree_on_grid(&p, DEFER_FUNCTIONS, sizeof DEFER_FUNCTIONS / sizeof DEFER_FUNCTIONS[0]));
    release_program(&p);
    return true;
}

static const char *const VALUE_OPTIONAL_FUNCTIONS[] = {
    "vo_basic", "vo_assign", "vo_calls", "vo_floats", "vo_many", "vo_narrow", "vo_trap",
    "vo_bools", "vo_enum", "vo_loop", "vo_defer", "vo_nested", "vo_through", "vo_wide",
    "vo_ufloat", "vo_fl32", "vo_pressure", "vo_loop2", "vo_equal", "vo_params", "vo_mixed",
};

static const CallCase VALUE_OPTIONAL_CASES[] = {
    { "vo_basic", 5, 2, 505, 0 },
    { "vo_basic", -1, 7, 6, 0 },
    { "vo_assign", 2, 1030, 8, 0 },
    { "vo_assign", 9, 4, 16, 0 },
    { "vo_assign", 1, -1, 1024, 0 },
    { "vo_calls", 4, 5, 90, 0 },
    { "vo_calls", 4, 1, 40, 0 },
    { "vo_calls", 0, 7, 70, 0 },
    { "vo_calls", 0, 0, -1, 0 },
    { "vo_floats", 3, 8, 6008, 0 },
    { "vo_floats", -2, 1, -4002, 0 },
    { "vo_many", 2, 3, -63320302, 0 },
    { "vo_many", 5, -2, 93678995, 0 },
    { "vo_narrow", -3, -2, -20000030, 0 },
    { "vo_narrow", 2, 7, -990000060, 0 },
    { "vo_narrow", 9, 4, -120000000, 0 },
    { "vo_trap", 9, 4, 10, 0 },
    { "vo_bools", 1, 3, 1, 0 },
    { "vo_bools", 1, 0, 12, 0 },
    { "vo_bools", 0, 5, 10, 0 },
    { "vo_enum", 0, 0, 0, 0 },
    { "vo_enum", 1, 0, 10, 0 },
    { "vo_enum", 2, 0, 20, 0 },
    { "vo_enum", 3, 0, 11, 0 },
    { "vo_loop", 3, 9, 10, 0 },
    { "vo_loop", 2, 0, -5, 0 },
    { "vo_defer", 4, 5, 9, 0 },
    { "vo_defer", 6, -2, 4, 0 },
    { "vo_nested", 4, 1, 403, 0 },
    { "vo_nested", 1, 4, 160, 0 },
    { "vo_nested", 0, 0, 3, 0 },
    { "vo_through", 5, 9, 5004, 0 },
    { "vo_through", 0, 2, 100, 0 },
    { "vo_wide", 3, 4, 1519, 0 },
    { "vo_wide", -1, 4, 6, 0 },
    { "vo_wide", -5, 0, -1, 0 },
    { "vo_ufloat", 1, 2, 20, 0 },
    { "vo_ufloat", -1, 5, 5, 0 },
    { "vo_ufloat", -3, 0, -7, 0 },
    { "vo_fl32", 6, 2, 12004, 0 },
    { "vo_fl32", 0, -3, -3997, 0 },
    { "vo_fl32", 1, 1, 4004, 0 },
    { "vo_pressure", 5, 3, 8632, 0 },
    { "vo_pressure", -2, 8, 24619, 0 },
    { "vo_loop2", 2, 7, 330, 0 },
    { "vo_loop2", -9, 3, -100, 0 },
    { "vo_equal", 4, 4, 11, 0 },
    { "vo_equal", 1, 0, 2, 0 },
    { "vo_equal", 5, 6, 10, 0 },
    { "vo_equal", 1, 6, 0, 0 },
    { "vo_params", 5, 2, 530, 0 },
    { "vo_params", 1, 2, 42, 0 },
    { "vo_mixed", 5, 2, 28, 0 },
    { "vo_mixed", 1, 4, 16, 0 },
    { "vo_trap", 3, 5, 0, TRAP_NIL },
    { "vo_many", INT64_MIN, 0, 0, TRAP_RANGE },
    { "vo_narrow", INT64_MIN, 0, 0, TRAP_RANGE },
    { "vo_floats", INT64_MIN, 0, 0, TRAP_RANGE },
    { "vo_defer", INT64_MAX, 1, 0, TRAP_OVERFLOW },
    { "vo_params", INT64_MIN, 0, 0, TRAP_OVERFLOW },
    { "vo_mixed", INT64_MIN, -1, 0, TRAP_OVERFLOW },
};

TEST_CASE(value_optionals)
{
    catch_traps();
    Program p;
    REQUIRE(build(&p, VALUE_OPTIONALS));
    for (size_t k = 0; k < sizeof VALUE_OPTIONAL_CASES / sizeof VALUE_OPTIONAL_CASES[0]; k++) {
        CallCase t = VALUE_OPTIONAL_CASES[k];
        int64_t got = 0;
        int trap = call_i2((I2 *)fn(&p, t.name), t.a, t.b, &got);
        if (trap != t.trap || (!trap && got != t.want)) {
            fprintf(stderr, "arm64 test: %s(%lld, %lld) gave %lld (trap %d)\n", t.name, (long long)t.a, (long long)t.b, (long long)got, trap);
            REQUIRE(false);
        }
    }
    REQUIRE(agree_on_grid(&p, VALUE_OPTIONAL_FUNCTIONS, sizeof VALUE_OPTIONAL_FUNCTIONS / sizeof VALUE_OPTIONAL_FUNCTIONS[0]));
    release_program(&p);
    return true;
}

static const char *const STORED_OPTIONAL_FUNCTIONS[] = {
    "so_fields", "so_copy", "so_small", "so_arrays", "so_pointers", "so_globals",
    "so_constants", "so_trap", "so_narrow", "so_floats", "so_slices", "so_nested", "so_rest",
    "so_tuples", "so_fconst",
};

static const CallCase STORED_OPTIONAL_CASES[] = {
    { "so_fields", 5, 2, 152190331, 0 },
    { "so_fields", -1, 7, 1007171203, 0 },
    { "so_fields", 3, 3, 93232013, 0 },
    { "so_copy", 5, 2, 5299330, 0 },
    { "so_copy", -1, 7, -6000600, 0 },
    { "so_copy", 3, 3, -6590659, 0 },
    { "so_small", 5, 2, 1056, 0 },
    { "so_small", -1, 7, -29, 0 },
    { "so_small", 12, 4, 2446, 0 },
    { "so_arrays", 5, 2, 365345, 0 },
    { "so_arrays", -1, 7, 527822, 0 },
    { "so_arrays", 0, 9, 743511, 0 },
    { "so_pointers", 5, 2, 253, 0 },
    { "so_pointers", -1, 7, 683, 0 },
    { "so_pointers", 0, 0, -117, 0 },
    { "so_globals", 5, 2, 40766199, 0 },
    { "so_globals", -1, 7, 40760179, 0 },
    { "so_globals", 12, 4, 40773193, 0 },
    { "so_constants", 5, 2, 100710538, 0 },
    { "so_constants", -1, 7, 100610472, 0 },
    { "so_constants", 0, 9, 3610483, 0 },
    { "so_trap", -1, 7, 6, 0 },
    { "so_trap", 12, 4, 16, 0 },
    { "so_narrow", 5, 2, 21, 0 },
    { "so_narrow", 3, 3, -3, 0 },
    { "so_floats", 5, 2, 137, 0 },
    { "so_floats", -1, 7, -17, 0 },
    { "so_floats", 0, 9, -14, 0 },
    { "so_slices", 5, 2, 20497, 0 },
    { "so_slices", 0, 9, 103057, 0 },
    { "so_nested", 5, 2, 5115, 0 },
    { "so_nested", -1, 7, -4776, 0 },
    { "so_nested", 12, 4, 12122, 0 },
    { "so_rest", 5, 2, 53524021, 0 },
    { "so_rest", -1, 7, 53774071, 0 },
    { "so_rest", 12, 4, 53444041, 0 },
    { "so_tuples", 5, 2, 5192, 0 },
    { "so_tuples", -1, 7, -753, 0 },
    { "so_tuples", 12, 4, 12184, 0 },
    { "so_tuples", 0, INT64_MAX, 0, TRAP_OVERFLOW },
    { "so_fconst", 5, 2, 122, 0 },
    { "so_fconst", -1, 7, 134, 0 },
    { "so_fconst", 12, 4, 189, 0 },
    { "so_trap", 5, 2, 0, TRAP_NIL },
    { "so_trap", 3, 3, 0, TRAP_NIL },
    { "so_fields", INT64_MIN, 3, 0, TRAP_OVERFLOW },
    { "so_nested", INT64_MAX, 0, 0, TRAP_RANGE },
    { "so_small", INT64_MAX, 0, 0, TRAP_OVERFLOW },
};

TEST_CASE(stored_optionals)
{
    catch_traps();
    Program p;
    REQUIRE(build(&p, STORED_OPTIONALS));
    for (size_t k = 0; k < sizeof STORED_OPTIONAL_CASES / sizeof STORED_OPTIONAL_CASES[0]; k++) {
        CallCase t = STORED_OPTIONAL_CASES[k];
        int64_t got = 0;
        int trap = call_i2((I2 *)fn(&p, t.name), t.a, t.b, &got);
        if (trap != t.trap || (!trap && got != t.want)) {
            fprintf(stderr, "arm64 test: %s(%lld, %lld) gave %lld (trap %d)\n", t.name, (long long)t.a, (long long)t.b, (long long)got, trap);
            REQUIRE(false);
        }
    }
    REQUIRE(agree_on_grid(&p, STORED_OPTIONAL_FUNCTIONS, sizeof STORED_OPTIONAL_FUNCTIONS / sizeof STORED_OPTIONAL_FUNCTIONS[0]));
    release_program(&p);
    return true;
}

static const char *const OPTIONAL_BRANCH_FUNCTIONS[] = {
    "ob_or", "ob_if", "ob_floats", "ob_slices", "ob_strings", "ob_small", "ob_equal",
    "ob_defer", "ob_store", "ob_constant", "ob_nested", "ob_loop", "ob_spill",
};

static const CallCase OPTIONAL_BRANCH_CASES[] = {
    { "ob_or", 5, 2, 555051, 0 },
    { "ob_or", -1, 7, 777071, 0 },
    { "ob_or", 12, 4, 1324122, 0 },
    { "ob_if", 5, 2, 504970500019994, 0 },
    { "ob_if", 3, 3, 303030600030003, 0 },
    { "ob_if", -1, 7, 698049600070007, 0 },
    { "ob_floats", 5, 2, 25025025125, 0 },
    { "ob_floats", -1, 7, 35035025150, 0 },
    { "ob_floats", 2, 6, 10030025150, 0 },
    { "ob_slices", 5, 2, 116486143930102614, 0 },
    { "ob_slices", -1, 7, 388905321451911, 0 },
    { "ob_slices", 2, 6, 388422386455532, 0 },
    { "ob_strings", 5, 2, 454323, 0 },
    { "ob_strings", 0, 9, 533319, 0 },
    { "ob_strings", 3, 3, 555333, 0 },
    { "ob_small", 5, 2, 1315, 0 },
    { "ob_small", 3, 3, 2413, 0 },
    { "ob_small", 12, 4, 1332, 0 },
    { "ob_small", 2, 6, 1412, 0 },
    { "ob_equal", 5, 2, 10980, 0 },
    { "ob_equal", 7, 7, 9370, 0 },
    { "ob_equal", -1, 7, 10260, 0 },
    { "ob_equal", 3, 3, 9410, 0 },
    { "ob_defer", 5, 2, 13222, 0 },
    { "ob_defer", 0, 9, 19322, 0 },
    { "ob_defer", 12, 4, 5322, 0 },
    { "ob_store", 5, 2, -7942, 0 },
    { "ob_store", -1, 7, 74078, 0 },
    { "ob_store", 12, 4, 124128, 0 },
    { "ob_constant", 5, 2, -943, 0 },
    { "ob_constant", -1, 7, -1013, 0 },
    { "ob_nested", 5, 2, 25, 0 },
    { "ob_nested", -1, 7, 74, 0 },
    { "ob_nested", 12, 4, 123, 0 },
    { "ob_loop", 5, 2, 12, 0 },
    { "ob_loop", 3, 3, 15, 0 },
    { "ob_loop", 7, 7, 14, 0 },
    { "ob_spill", 5, 2, 2412, 0 },
    { "ob_spill", 0, 0, 1020, 0 },
    { "ob_spill", 1, -1, 3427, 0 },
    { "ob_spill", -1, 7, 2327, 0 },
    { "ob_spill", 7, 7, 317, 0 },
    { "ob_equal", INT64_MAX, INT64_MAX, 9370, 0 },
    { "ob_or", INT64_MAX, 0, 0, TRAP_OVERFLOW },
    { "ob_if", INT64_MAX, INT64_MAX, 0, TRAP_OVERFLOW },
    { "ob_floats", INT64_MAX, 0, 0, TRAP_RANGE },
    { "ob_loop", INT64_MAX, 1, 0, TRAP_OVERFLOW },
    { "ob_defer", 1, INT64_MAX, 0, TRAP_OVERFLOW },
};

TEST_CASE(optional_branches)
{
    catch_traps();
    Program p;
    REQUIRE(build(&p, OPTIONAL_BRANCHES));
    for (size_t k = 0; k < sizeof OPTIONAL_BRANCH_CASES / sizeof OPTIONAL_BRANCH_CASES[0]; k++) {
        CallCase t = OPTIONAL_BRANCH_CASES[k];
        int64_t got = 0;
        int trap = call_i2((I2 *)fn(&p, t.name), t.a, t.b, &got);
        if (trap != t.trap || (!trap && got != t.want)) {
            fprintf(stderr, "arm64 test: %s(%lld, %lld) gave %lld (trap %d)\n", t.name, (long long)t.a, (long long)t.b, (long long)got, trap);
            REQUIRE(false);
        }
    }
    REQUIRE(agree_on_grid(&p, OPTIONAL_BRANCH_FUNCTIONS, sizeof OPTIONAL_BRANCH_FUNCTIONS / sizeof OPTIONAL_BRANCH_FUNCTIONS[0]));
    REQUIRE(count_words(&p, "ob_pick2", WORD_MOV_X, MOV_REGS) == 4);
    release_program(&p);
    return true;
}

static const char *const ERROR_UNION_FUNCTIONS[] = {
    "er_basic", "er_propagate", "er_void", "er_floats", "er_small", "er_pointers", "er_ifs",
    "er_defer", "er_loop", "er_nested", "er_values", "er_argcalls", "er_order", "er_scan",
    "er_wide", "er_sets", "er_union", "er_store", "er_run", "er_any", "er_fwd",
    "er_inferred", "er_optional", "er_handled", "er_arms", "er_rally",
    "er_tried", "er_hopped", "er_widened", "er_undone", "er_tidied", "er_rounded",
};

static const CallCase ERROR_UNION_CASES[] = {
    { "er_basic", 5, 2, 2000003, 0 },
    { "er_basic", -1, 7, -1006998, 0 },
    { "er_basic", 12, 4, 2999800, 0 },
    { "er_basic", 3, 3, 1001002, 0 },
    { "er_basic", 7, 0, -1000200, 0 },
    { "er_propagate", 5, 2, 4, 0 },
    { "er_propagate", -1, 7, -2, 0 },
    { "er_propagate", 12, 4, 14, 0 },
    { "er_propagate", 7, 0, 6, 0 },
    { "er_propagate", 150, 3, 152, 0 },
    { "er_void", 5, 2, 0, 0 },
    { "er_void", -1, 7, 4, 0 },
    { "er_void", 3, 3, 3, 0 },
    { "er_void", 150, 3, 111, 0 },
    { "er_void", -150, 200, 1012, 0 },
    { "er_floats", 5, 2, 250005000300, 0 },
    { "er_floats", -1, 7, -99982500200, 0 },
    { "er_floats", 12, 4, 600010000700, 0 },
    { "er_floats", 3, 3, 150007500225, 0 },
    { "er_floats", 7, 0, 350000000350, 0 },
    { "er_small", 5, 2, 4999811, 0 },
    { "er_small", -1, 7, 6999302, 0 },
    { "er_small", 12, 4, 11999611, 0 },
    { "er_small", 3, 3, 2999701, 0 },
    { "er_small", 7, 0, 7000310, 0 },
    { "er_pointers", 5, 2, 1301040011, 0 },
    { "er_pointers", 12, 4, 1301540011, 0 },
    { "er_pointers", 3, 3, 5301040111, 0 },
    { "er_pointers", 1, -1, 311040010, 0 },
    { "er_pointers", 2, 6, 301140011, 0 },
    { "er_ifs", 5, 2, 2002005, 0 },
    { "er_ifs", -1, 7, -5007008, 0 },
    { "er_ifs", 12, 4, 6003012, 0 },
    { "er_ifs", 3, 3, 1000993, 0 },
    { "er_ifs", 7, 0, 2999007, 0 },
    { "er_defer", 5, 2, 2021, 0 },
    { "er_defer", -1, 7, -1999, 0 },
    { "er_defer", 12, 4, 3021, 0 },
    { "er_defer", 3, 3, 1021, 0 },
    { "er_defer", 7, 0, -999, 0 },
    { "er_loop", 5, 2, 279, 0 },
    { "er_loop", -1, 7, 1, 0 },
    { "er_loop", 12, 4, 63, 0 },
    { "er_loop", 150, 3, 6295, 0 },
    { "er_loop", 1, -1, 7, 0 },
    { "er_nested", 5, 2, 20002, 0 },
    { "er_nested", -1, 7, -70001, 0 },
    { "er_nested", 12, 4, 30003, 0 },
    { "er_nested", 3, 3, 10001, 0 },
    { "er_nested", 7, 0, 0, 0 },
    { "er_values", 5, 2, 1234, 0 },
    { "er_values", -1, 7, 3334, 0 },
    { "er_values", 12, 4, 1144, 0 },
    { "er_values", 3, 3, 3234, 0 },
    { "er_values", -150, 200, 3344, 0 },
    { "er_argcalls", 5, 2, 517, 0 },
    { "er_argcalls", -1, 7, -1, 0 },
    { "er_argcalls", 12, 4, 1232, 0 },
    { "er_argcalls", 3, 3, 330, 0 },
    { "er_argcalls", 7, 0, 693, 0 },
    { "er_order", 5, 2, 7005, 0 },
    { "er_order", -1, 7, 99, 0 },
    { "er_order", 12, 4, 15012, 0 },
    { "er_order", 3, 3, 4003, 0 },
    { "er_order", 7, 0, 8107, 0 },
    { "er_scan", 5, 2, 3003, 0 },
    { "er_scan", -1, 7, 3007, 0 },
    { "er_scan", 299, 4, 3005, 0 },
    { "er_scan", 300, 3, 3003, 0 },
    { "er_scan", 0, 0, 3001, 0 },
    { "er_wide", 5, 2, 299997, 0 },
    { "er_wide", -1, 7, -7400, 0 },
    { "er_wide", 12, 4, 299992, 0 },
    { "er_wide", 3, 3, 0, 0 },
    { "er_wide", 299, 4, 299705, 0 },
    { "er_sets", 5, 2, 12, 0 },
    { "er_sets", -1, 7, -1, 0 },
    { "er_sets", 12, 4, 28, 0 },
    { "er_sets", 7, 0, -3, 0 },
    { "er_sets", 150, 2, -2, 0 },
    { "er_sets", 50, 1, -4, 0 },
    { "er_sets", 2, -1, -6, 0 },
    { "er_sets", 3, 1, -5, 0 },
    { "er_union", 5, 2, 2010, 0 },
    { "er_union", -1, 7, 4030, 0 },
    { "er_union", 3, 3, 3020, 0 },
    { "er_union", -5, -9, 1011, 0 },
    { "er_store", 5, 2, 112, 0 },
    { "er_store", -1, 7, 127, 0 },
    { "er_store", 12, 4, 124, 0 },
    { "er_store", 3, 3, 113, 0 },
    { "er_store", 7, 0, 210, 0 },
    { "er_run", 5, 2, 12, 0 },
    { "er_run", -1, 7, -1, 0 },
    { "er_run", 7, 0, -1, 0 },
    { "er_run", 60, 2, -40, 0 },
    { "er_any", 5, 2, 5, 0 },
    { "er_any", -1, 7, 1, 0 },
    { "er_any", 7, 0, 3, 0 },
    { "er_fwd", 5, 2, 12, 0 },
    { "er_fwd", -1, 7, -93, 0 },
    { "er_fwd", 7, 0, 14, 0 },
    { "er_fwd", 150, 1, -199, 0 },
    { "er_inferred", 5, 2, 100, 0 },
    { "er_inferred", -1, 7, -3998, 0 },
    { "er_inferred", 60, 100, -1002, 0 },
    { "er_inferred", 9, 9, 299, 0 },
    { "er_inferred", 12, 45, 715, 0 },
    { "er_inferred", 3, -6, 96, 0 },
    { "er_optional", 5, 2, 2, 0 },
    { "er_optional", 7, 3, -1, 0 },
    { "er_optional", -1, -9, -9, 0 },
    { "er_handled", 7, 0, 0, 0 },
    { "er_handled", -5, 0, 0, 0 },
    { "er_handled", -5, 2, -2, 0 },
    { "er_handled", 9, 2, 5, 0 },
    { "er_arms", -5, 0, 30, 0 },
    { "er_arms", -5, 2, 40, 0 },
    { "er_arms", 9, 2, 4, 0 },
    { "er_rally", 3, 5, -7, 0 },
    { "er_rally", 3, 100, 106, 0 },
    { "er_rally", -1, 2, 4, 0 },
    { "er_rally", 8, 0, -7, 0 },
    { "er_rally", 6, -2500, -497, 0 },
    { "er_tried", 7, 0, -1, 0 },
    { "er_tried", -5, 2, -2, 0 },
    { "er_tried", 1000, 3, -3, 0 },
    { "er_tried", 9, 2, 5, 0 },
    { "er_tried", 300, -2, -149, 0 },
    { "er_hopped", 7, 0, -1, 0 },
    { "er_hopped", -5, 2, -2, 0 },
    { "er_hopped", 7, 3, 1, 0 },
    { "er_hopped", 0, 3, -1, 0 },
    { "er_hopped", 4, -3, -2, 0 },
    { "er_hopped", 5, 2, 1, 0 },
    { "er_widened", 9, 2, 4, 0 },
    { "er_widened", 9, 0, -1, 0 },
    { "er_widened", -1, 300, 44, 0 },
    { "er_widened", 0, -1, 255, 0 },
    { "er_widened", 7, -2, -3, 0 },
    { "er_undone", 7, 0, -979, 0 },
    { "er_undone", -5, 2, -1979, 0 },
    { "er_undone", 9, 2, 4001, 0 },
    { "er_undone", 200, 2, -2479, 0 },
    { "er_undone", 60, 2, 30001, 0 },
    { "er_undone", 30, 1, -679, 0 },
    { "er_tidied", 3, 3, -297, 0 },
    { "er_tidied", 7, 0, -99, 0 },
    { "er_tidied", -5, 2, -198, 0 },
    { "er_tidied", 9, 2, 800, 0 },
    { "er_rounded", 0, 1, 2, 0 },
    { "er_rounded", 3, 2, -11133, 0 },
    { "er_rounded", 1, 2, 3, 0 },
    { "er_rounded", -3, 2, -10133, 0 },
    { "er_rounded", 2, 6, 104, 0 },
    { "er_basic", INT64_MAX, 1, 0, TRAP_OVERFLOW },
    { "er_void", INT64_MAX, 1, 0, TRAP_OVERFLOW },
    { "er_floats", INT64_MAX, 1, 0, TRAP_RANGE },
    { "er_small", INT64_MIN, 1, 0, TRAP_OVERFLOW },
    { "er_ifs", INT64_MAX, 1, 0, TRAP_OVERFLOW },
    { "er_loop", INT64_MAX, 1, 0, TRAP_OVERFLOW },
    { "er_nested", INT64_MAX, 1, 0, TRAP_OVERFLOW },
    { "er_argcalls", INT64_MIN, -1, 0, TRAP_OVERFLOW },
    { "er_order", INT64_MAX, 1, 0, TRAP_OVERFLOW },
    { "er_wide", INT64_MIN, 1, 0, TRAP_OVERFLOW },
    { "er_sets", 5, INT64_MAX, 0, TRAP_OVERFLOW },
};

TEST_CASE(error_unions)
{
    catch_traps();
    Program p;
    REQUIRE(build(&p, ERROR_UNIONS));
    for (size_t k = 0; k < sizeof ERROR_UNION_CASES / sizeof ERROR_UNION_CASES[0]; k++) {
        CallCase t = ERROR_UNION_CASES[k];
        int64_t got = 0;
        int trap = call_i2((I2 *)fn(&p, t.name), t.a, t.b, &got);
        if (trap != t.trap || (!trap && got != t.want)) {
            fprintf(stderr, "arm64 test: %s(%lld, %lld) gave %lld (trap %d)\n", t.name, (long long)t.a, (long long)t.b, (long long)got, trap);
            REQUIRE(false);
        }
    }
    REQUIRE(agree_on_grid(&p, ERROR_UNION_FUNCTIONS, sizeof ERROR_UNION_FUNCTIONS / sizeof ERROR_UNION_FUNCTIONS[0]));
    release_program(&p);
    return true;
}

static const char *const WIDE_ERROR_FUNCTIONS[] = { "ew_test", "ew_steps" };

static const CallCase WIDE_ERROR_CASES[] = {
    { "ew_test", 5, 2, 11006, 0 },  { "ew_test", -1, 7, 20100, 0 }, { "ew_test", 12, 4, 11012, 0 },
    { "ew_test", 7, 0, 11000, 0 },  { "ew_test", 0, -2, 20200, 0 }, { "ew_steps", 5, 2, 3, 0 },
    { "ew_steps", -1, 7, 0, 0 },    { "ew_steps", 1, 9, 53, 0 },    { "ew_steps", 0, 10, 50, 0 },
};

TEST_CASE(wide_error_codes)
{
    char *text = wide_errors_program();
    Program p;
    REQUIRE(build(&p, text));
    for (size_t k = 0; k < sizeof WIDE_ERROR_CASES / sizeof WIDE_ERROR_CASES[0]; k++) {
        CallCase t = WIDE_ERROR_CASES[k];
        int64_t got = 0;
        int trap = call_i2((I2 *)fn(&p, t.name), t.a, t.b, &got);
        if (trap != t.trap || (!trap && got != t.want)) {
            fprintf(stderr, "arm64 test: %s(%lld, %lld) gave %lld (trap %d)\n", t.name, (long long)t.a, (long long)t.b, (long long)got, trap);
            REQUIRE(false);
        }
    }
    REQUIRE(agree_on_grid(&p, WIDE_ERROR_FUNCTIONS, sizeof WIDE_ERROR_FUNCTIONS / sizeof WIDE_ERROR_FUNCTIONS[0]));
    release_program(&p);
    free(text);
    return true;
}

TEST_CASE(top_level_code)
{
    Program p;
    REQUIRE(check_text(&p, TOP_LEVEL, false, true));
    REQUIRE(jit_compile(&p.n, &p.c, CONVENTION_HOST, 0));
    native_set_context(&p.n, DEFAULT_CONTEXT);
    REQUIRE(p.c.ir.nexports == 1 && p.c.ir.callbacks[0] == p.c.entry);
    MAIN *entry = (MAIN *)native_export(&p.n, 0);
    I0 *count = (I0 *)fn(&p, "count");

    char a0[] = "chibi", a1[] = "run", a2[] = "x";
    char *argv[] = { a0, a1, a2, NULL };
    REQUIRE(entry(3, argv) == 9032);
    REQUIRE(count(CONTEXT) == 2);
    REQUIRE(entry(1, argv) == -1);
    REQUIRE(count(CONTEXT) == 0);
    REQUIRE(entry(2, argv + 1) == 4021);
    REQUIRE(count(CONTEXT) == 1);
    REQUIRE(entry(0, argv) == 0);
    REQUIRE(count(CONTEXT) == 0);
    release_program(&p);
    return true;
}

TEST_SUITE(arm64_tests)
{
    RUN_TEST_CASE(hidden_context);
    RUN_TEST_CASE(calls_into_c);
    RUN_TEST_CASE(stack_arguments);
    RUN_TEST_CASE(variadic_calls);
    RUN_TEST_CASE(callbacks);
    RUN_TEST_CASE(callback_stack_limit);
    RUN_TEST_CASE(exports);
    RUN_TEST_CASE(many_pointers);
    RUN_TEST_CASE(top_level_code);
    RUN_TEST_CASE(bit_counts);
    RUN_TEST_CASE(overflow_tests);
    RUN_TEST_CASE(mul_high_tests);
    RUN_TEST_CASE(bitcasts);
    RUN_TEST_CASE(optionals);
    RUN_TEST_CASE(strings);
    RUN_TEST_CASE(value_ifs);
    RUN_TEST_CASE(string_tables);
    RUN_TEST_CASE(struct_ifs);
    RUN_TEST_CASE(optional_fns);
    RUN_TEST_CASE(rest_args);
    RUN_TEST_CASE(inline_marks);
    RUN_TEST_CASE(asserts);
    RUN_TEST_CASE(targets);
    RUN_TEST_CASE(release_fast);
    RUN_TEST_CASE(vectors);
    RUN_TEST_CASE(int128);
    RUN_TEST_CASE(string_views);
    RUN_TEST_CASE(tuples);
    RUN_TEST_CASE(named_results);
    RUN_TEST_CASE(defers);
    RUN_TEST_CASE(value_optionals);
    RUN_TEST_CASE(stored_optionals);
    RUN_TEST_CASE(optional_branches);
    RUN_TEST_CASE(error_unions);
    RUN_TEST_CASE(wide_error_codes);
    RUN_TEST_CASE(narrow_parameters);
    RUN_TEST_CASE(constant_division);
    RUN_TEST_CASE(differential);
    RUN_TEST_CASE(store_while_loaded);
    RUN_TEST_CASE(unsupported_types);
    RUN_TEST_CASE(pointers);
    RUN_TEST_CASE(arrays);
    RUN_TEST_CASE(structs);
    RUN_TEST_CASE(slices);
    RUN_TEST_CASE(stored_slices);
    RUN_TEST_CASE(passed_structs);
    RUN_TEST_CASE(passed_arrays);
    RUN_TEST_CASE(left_to_right);
    RUN_TEST_CASE(large_structs);
    RUN_TEST_CASE(constant_conditions);
    RUN_TEST_CASE(constant_tables);
    RUN_TEST_CASE(globals);
    RUN_TEST_CASE(empty_globals);
    RUN_TEST_CASE(deep_logic);
    RUN_TEST_CASE(small_integers);
    RUN_TEST_CASE(wide_integers);
    RUN_TEST_CASE(control_flow);
    RUN_TEST_CASE(switches);
}

#else

TEST_SUITE(arm64_tests) {}

#endif
