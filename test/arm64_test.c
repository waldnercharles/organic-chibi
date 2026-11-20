#define _DEFAULT_SOURCE

#include "../src/arm64.h"
#include "../vendor/pico_unit/pico_unit.h"
#include "ir_reference.h"
#include "random/code.h"
#include "random/program.h"

#include <assert.h>
#include <math.h>
#include <setjmp.h>
#include <signal.h>
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
check_text(Program *p, const char *text)
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
                            true };
    check_init(&p->c, &p->lx.names);
    check_package(&p->c, &p->file, 1);
    if (p->c.ndiags) {
        check_print_diagnostics(stderr, &p->c);
        return false;
    }
    return true;
}

static bool
build(Program *p, const char *text)
{
    if (!check_text(p, text)) {
        return false;
    }
    if (!arm64_compile(&p->n, &p->c)) {
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

typedef int64_t I2(int64_t, int64_t);

static int
call_i2(I2 *f, int64_t a, int64_t b, int64_t *result)
{
    trap_kind = 0;
    if (sigsetjmp(trap_jump, 1) == 0) {
        *result = f(a, b);
        return 0;
    }
    return trap_kind;
}

static const char ARITHMETIC
    [] = "fn add_i8(a: int, b: int) -> int {\n"
         "    x: i8 = i8(a)\n"
         "    y: i8 = i8(b)\n"
         "    return int(x + y)\n"
         "}\n"
         "fn sub_u8(a: int, b: int) -> int {\n"
         "    x: u8 = u8(a)\n"
         "    y: u8 = u8(b)\n"
         "    return int(x - y)\n"
         "}\n"
         "fn mul_i8(a: int, b: int) -> int {\n"
         "    x: i8 = i8(a)\n"
         "    y: i8 = i8(b)\n"
         "    return int(x * y)\n"
         "}\n"
         "fn wrap_u8(a: int, b: int) -> int {\n"
         "    x: u8 = u8(a)\n"
         "    y: u8 = u8(b)\n"
         "    return int(x *% y +% x)\n"
         "}\n"
         "fn div_i8(a: int, b: int) -> int {\n"
         "    x: i8 = i8(a)\n"
         "    y: i8 = i8(b)\n"
         "    return int(x / y)\n"
         "}\n"
         "fn rem_i8(a: int, b: int) -> int {\n"
         "    x: i8 = i8(a)\n"
         "    y: i8 = i8(b)\n"
         "    return int(x % y)\n"
         "}\n"
         "fn shl_u8(a: int, b: int) -> int {\n"
         "    x: u8 = u8(a)\n"
         "    y: u8 = u8(b)\n"
         "    return int(x << y)\n"
         "}\n"
         "fn shr_i8(a: int, b: int) -> int {\n"
         "    x: i8 = i8(a)\n"
         "    return int(x >> b)\n"
         "}\n"
         "fn neg_i8(a: int, b: int) -> int {\n"
         "    x: i8 = i8(a)\n"
         "    return int(-x) + b * 0\n"
         "}\n"
         "fn add_i32(a: int, b: int) -> int {\n"
         "    x: i32 = i32(a)\n"
         "    y: i32 = i32(b)\n"
         "    return int(x + y)\n"
         "}\n"
         "fn sub_u32(a: int, b: int) -> int {\n"
         "    x: u32 = u32(a)\n"
         "    y: u32 = u32(b)\n"
         "    return int(x - y)\n"
         "}\n"
         "fn mul_i32(a: int, b: int) -> int {\n"
         "    x: i32 = i32(a)\n"
         "    y: i32 = i32(b)\n"
         "    return int(x * y)\n"
         "}\n"
         "fn mul_u32(a: int, b: int) -> int {\n"
         "    x: u32 = u32(a)\n"
         "    y: u32 = u32(b)\n"
         "    return int(x * y)\n"
         "}\n"
         "fn div_i32(a: int, b: int) -> int {\n"
         "    x: i32 = i32(a)\n"
         "    y: i32 = i32(b)\n"
         "    return int(x / y)\n"
         "}\n"
         "fn add_i64(a: int, b: int) -> int => a + b\n"
         "fn sub_i64(a: int, b: int) -> int => a - b\n"
         "fn mul_i64(a: int, b: int) -> int => a * b\n"
         "fn div_i64(a: int, b: int) -> int => a / b\n"
         "fn rem_i64(a: int, b: int) -> int => a % b\n"
         "fn wrap_i64(a: int, b: int) -> int => a *% b -% a\n"
         "fn shl_i64(a: int, b: int) -> int => a << b\n"
         "fn shr_i64(a: int, b: int) -> int => a >> b\n"
         "fn neg_i64(a: int, b: int) -> int => -a + b * 0\n"
         "fn to_u8(a: int, b: int) -> int => int(u8(a)) + b * 0\n"
         "fn mask_u8(a: int, b: int) -> int => int(u8(a & 0xFF)) + b * 0\n"
         "fn mul_u64(a: int, b: int) -> int {\n"
         "    x: u64 = u64(a & 0x7FFFFFFFFFFFFFFF)\n"
         "    y: u64 = u64(b & 0x7FFFFFFFFFFFFFFF)\n"
         "    return int(x * y & 0x7FFFFFFFFFFFFFFF)\n"
         "}\n";

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

static const char
    CONTROL[] = "fn sum(n: int) -> int {\n"
                "    total := 0\n"
                "    for i in 0..n {\n"
                "        total += i\n"
                "    }\n"
                "    return total\n"
                "}\n"
                "fn collatz(n: int) -> int {\n"
                "    steps := 0\n"
                "    x := n\n"
                "    while x != 1 {\n"
                "        if x % 2 == 0 {\n"
                "            x = x / 2\n"
                "        } else {\n"
                "            x = 3 * x + 1\n"
                "        }\n"
                "        steps += 1\n"
                "    }\n"
                "    return steps\n"
                "}\n"
                "fn fib(n: int) -> int {\n"
                "    if n < 2 {\n"
                "        return n\n"
                "    }\n"
                "    return fib(n - 1) + fib(n - 2)\n"
                "}\n"
                "fn is_even(n: int) -> bool {\n"
                "    if n == 0 {\n"
                "        return true\n"
                "    }\n"
                "    return is_odd(n - 1)\n"
                "}\n"
                "fn is_odd(n: int) -> bool {\n"
                "    if n == 0 {\n"
                "        return false\n"
                "    }\n"
                "    return is_even(n - 1)\n"
                "}\n"
                "fn bytes() -> int {\n"
                "    n := 0\n"
                "    lo: u8 = 0\n"
                "    hi: u8 = 255\n"
                "    for k in lo..=hi {\n"
                "        n += 1\n"
                "    }\n"
                "    return n\n"
                "}\n"
                "fn skips(n: int) -> int {\n"
                "    total := 0\n"
                "    i := 0\n"
                "    while true {\n"
                "        i += 1\n"
                "        if i > n {\n"
                "            break\n"
                "        }\n"
                "        if i % 3 == 0 {\n"
                "            continue\n"
                "        }\n"
                "        total += i\n"
                "    }\n"
                "    return total\n"
                "}\n"
                "fn logic(a: int, b: int) -> int {\n"
                "    if a > 0 and b > 0 or a == b {\n"
                "        return 1\n"
                "    }\n"
                "    return 0\n"
                "}\n"
                "fn flags(a: bool, b: bool, c: bool) -> bool {\n"
                "    return a and (b or c)\n"
                "}\n"
                "fn count_flags(a: int, b: int) -> int {\n"
                "    n := 0\n"
                "    p := a > b\n"
                "    q := a < b or a == 0\n"
                "    if p or q and !p {\n"
                "        n += 1\n"
                "    }\n"
                "    return n + 10 * a\n"
                "}\n"
                "fn deep(a: int, b: int, c: int, d: int) -> int {\n"
                "    return a * b + (c * d + (a * c + (b * d + (a * d + (b * c "
                "+ (a * a + (b * b + (c * c + d * d))))))))\n"
                "}\n"
                "fn live(a: int) -> int {\n"
                "    return a + fib(10) * (a + fib(5))\n"
                "}\n"
                "fn mix(a: int, b: f64, c: i32, d: f32) -> f64 {\n"
                "    return f64(a) + b + f64(c) + f64(d)\n"
                "}\n"
                "fn use_mix() -> f64 {\n"
                "    return mix(1, 2.5, 3, 4.5) * 2\n"
                "}\n"
                "fn scale(x: int, k: int = 10) -> int {\n"
                "    return x * k\n"
                "}\n"
                "fn use_scale(x: int) -> int {\n"
                "    return scale(x) + scale(x, 2)\n"
                "}\n"
                "fn many(a: int, b: int, c: int, d: int, e: int, f: int, g: "
                "int, h: int) -> int {\n"
                "    return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f + 7 * g "
                "+ 8 * h\n"
                "}\n"
                "fn use_many() -> int {\n"
                "    return many(1, 2, 3, 4, 5, 6, 7, 8)\n"
                "}\n"
                "fn floats(x: f32, y: f64) -> f64 {\n"
                "    h := x / 2\n"
                "    if h > 1.5 and y <= 10 {\n"
                "        return f64(h) * y - 0.25\n"
                "    }\n"
                "    return -y\n"
                "}\n"
                "fn to_int(x: f64) -> int => int(x)\n"
                "fn to_u8f(x: f32) -> int => u8(x)\n"
                "fn to_i32f(x: f64) -> int => i32(x)\n"
                "fn one() -> int {\n"
                "    return 1\n"
                "}\n"
                "fn two() -> int {\n"
                "    return 2\n"
                "}\n"
                "fn call_one() -> int {\n"
                "    return one() + 10\n"
                "}\n";

typedef int64_t I1(int64_t);
typedef int64_t I0(void);
typedef bool B1(int64_t);
typedef bool B3(bool, bool, bool);
typedef int64_t I4(int64_t, int64_t, int64_t, int64_t);
typedef double D4(int64_t, double, int32_t, float);
typedef double D0(void);
typedef double DFD(float, double);
typedef int64_t ID(double);
typedef int64_t IF(float);

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
        *result = f(x);
        return 0;
    }
    return trap_kind;
}

static int
call_if(IF *f, float x, int64_t *result)
{
    trap_kind = 0;
    if (sigsetjmp(trap_jump, 1) == 0) {
        *result = f(x);
        return 0;
    }
    return trap_kind;
}

TEST_CASE(control_flow)
{
    Program p;
    REQUIRE(build(&p, CONTROL));
    catch_traps();
    REQUIRE(((I1 *)fn(&p, "sum"))(100) == 4950);
    REQUIRE(((I1 *)fn(&p, "sum"))(0) == 0);
    REQUIRE(((I1 *)fn(&p, "collatz"))(27) == 111);
    REQUIRE(((I1 *)fn(&p, "fib"))(20) == 6765);
    REQUIRE(((B1 *)fn(&p, "is_even"))(10));
    REQUIRE(!((B1 *)fn(&p, "is_even"))(7));
    REQUIRE(((I0 *)fn(&p, "bytes"))() == 256);
    REQUIRE(((I1 *)fn(&p, "skips"))(10) == 1 + 2 + 4 + 5 + 7 + 8 + 10);

    I2 *logic = (I2 *)fn(&p, "logic");
    I2 *count_flags = (I2 *)fn(&p, "count_flags");
    for (int64_t a = -2; a <= 2; a++) {
        for (int64_t b = -2; b <= 2; b++) {
            REQUIRE(logic(a, b) == ((a > 0 && b > 0) || a == b));
            bool pp = a > b, q = a < b || a == 0;
            REQUIRE(count_flags(a, b) == (pp || (q && !pp)) + 10 * a);
        }
    }
    B3 *flags = (B3 *)fn(&p, "flags");
    for (int m = 0; m < 8; m++) {
        bool a = m & 1, b = m & 2, c = m & 4;
        REQUIRE(flags(a, b, c) == (a && (b || c)));
    }

    I4 *deep = (I4 *)fn(&p, "deep");
    REQUIRE(deep(3, -5, 7, 11) == deep_reference(3, -5, 7, 11));
    REQUIRE(deep(-1, 2, -3, 4) == deep_reference(-1, 2, -3, 4));
    REQUIRE(((I1 *)fn(&p, "live"))(4) == 4 + 55 * (4 + 5));

    REQUIRE(((D4 *)fn(&p, "mix"))(1, 2.5, 3, 4.5f) == 11.0);
    REQUIRE(((D0 *)fn(&p, "use_mix"))() == 22.0);
    REQUIRE(((I1 *)fn(&p, "use_scale"))(3) == 36);
    REQUIRE(((I0 *)fn(&p, "use_many"))() == 1 + 4 + 9 + 16 + 25 + 36 + 49 + 64);

    DFD *floats = (DFD *)fn(&p, "floats");
    REQUIRE(floats(5.0f, 4.0) == 2.5 * 4.0 - 0.25);
    REQUIRE(floats(2.0f, 4.0) == -4.0);
    REQUIRE(floats(5.0f, NAN) == -NAN || isnan(floats(5.0f, NAN)));

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
    REQUIRE(call_one() == 11);
    native_replace(&p.n, decl_named(&p, "one"), fn(&p, "two"));
    REQUIRE(call_one() == 12);

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
    check_init(&p.c, &p.lx.names);
    check_package(&p.c, &p.file, 1);
    bool ok = count >= 0 && p.c.ndiags == 0 && !arm64_compile(&p.n, &p.c) &&
              p.n.error && strstr(p.n.error, "supports only") != NULL;
    if (!ok) {
        fprintf(stderr, "arm64 test: expected the backend to reject\n%s", text);
    }
    release_program(&p);
    return ok;
}

TEST_CASE(unsupported_types)
{
    REQUIRE(rejects("fn b(a: int) -> !void {\n    if a > 0 {\n        return\n    }\n}\n"));
    REQUIRE(rejects("fn o(p: ?int) -> ?int {\n    return p\n}\n"));
    REQUIRE(rejects("fn s(p: ?[]u8) -> int {\n    return 0\n}\n"));
    return true;
}

static const char POINTERS[] = "fn bump(p: *int, by: int) {\n"
                               "    p.* += by\n"
                               "}\n"
                               "fn swap(a: *i32, b: *i32) {\n"
                               "    t := a.*\n"
                               "    a.* = b.*\n"
                               "    b.* = t\n"
                               "}\n"
                               "fn set(p: *int, v: int) {\n"
                               "    p.* = v\n"
                               "}\n"
                               "fn pick(a: *int, b: *int, first: bool) -> *int {\n"
                               "    if first {\n"
                               "        return a\n"
                               "    }\n"
                               "    return b\n"
                               "}\n"
                               "fn run(x: int, y: int) -> int {\n"
                               "    n := x\n"
                               "    q := &n\n"
                               "    bump(q, 5)\n"
                               "    bump(&n, y)\n"
                               "    r := &q\n"
                               "    r.*.* = q.* * 2\n"
                               "    return n\n"
                               "}\n"
                               "fn over(a: int, b: int) -> int {\n"
                               "    n := a\n"
                               "    bump(&n, b)\n"
                               "    return n\n"
                               "}\n"
                               "fn reload(a: int, b: int) -> int {\n"
                               "    n := a\n"
                               "    set(&n, n + b)\n"
                               "    m := n\n"
                               "    set(&n, m * 3)\n"
                               "    return n + m\n"
                               "}\n"
                               "fn alias(a: int, b: int) -> int {\n"
                               "    n := a\n"
                               "    p := &n\n"
                               "    q := &n\n"
                               "    p.* += b\n"
                               "    q.* *= 2\n"
                               "    return n + p.* + q.*\n"
                               "}\n"
                               "fn narrow(a: int, b: int) -> int {\n"
                               "    x: i32 = i32(a & 0xFFFF)\n"
                               "    y: i32 = i32(b & 0xFFFF)\n"
                               "    swap(&x, &y)\n"
                               "    c: u8 = u8(a & 0xFF)\n"
                               "    p := &c\n"
                               "    p.* +%= 100\n"
                               "    s: i16 = -5\n"
                               "    ps := &s\n"
                               "    ps.* -= 1\n"
                               "    f: f32 = 1.5\n"
                               "    g := &f\n"
                               "    g.* *= 2\n"
                               "    t := a > b\n"
                               "    h := &t\n"
                               "    h.* = !h.*\n"
                               "    r := int(x) * 1000000 + int(y)\n"
                               "    r += int(c) * 10\n"
                               "    r += int(s)\n"
                               "    if t {\n"
                               "        r += 7\n"
                               "    }\n"
                               "    return r + int(f)\n"
                               "}\n"
                               "fn choose(a: int, b: int) -> int {\n"
                               "    x := a\n"
                               "    y := b\n"
                               "    pick(&x, &y, a < b).* += 100\n"
                               "    return x * 1000 + y\n"
                               "}\n"
                               "fn same(a: int, b: int) -> int {\n"
                               "    x := a\n"
                               "    y := b\n"
                               "    p := &x\n"
                               "    q := &x\n"
                               "    r := &y\n"
                               "    n := 0\n"
                               "    if p == q {\n"
                               "        n += 1\n"
                               "    }\n"
                               "    if p != r {\n"
                               "        n += 10\n"
                               "    }\n"
                               "    if p == r {\n"
                               "        n += 100\n"
                               "    }\n"
                               "    return n\n"
                               "}\n"
                               "fn accumulate(a: int, b: int) -> int {\n"
                               "    total := b\n"
                               "    p := &total\n"
                               "    for i in 0..a {\n"
                               "        p.* += i\n"
                               "    }\n"
                               "    return total\n"
                               "}\n";

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
    REQUIRE(((I2 *)fn(&p, "run"))(1, 1) == 14);
    REQUIRE(((I2 *)fn(&p, "run"))(7, -3) == 18);
    REQUIRE(call_i2((I2 *)fn(&p, "over"), 5, 6, &r) == 0 && r == 11);
    REQUIRE(call_i2((I2 *)fn(&p, "over"), INT64_MAX, 1, &r) == TRAP_OVERFLOW);
    REQUIRE(((I2 *)fn(&p, "reload"))(4, 1) == 20);
    REQUIRE(((I2 *)fn(&p, "reload"))(-2, 0) == -8);
    REQUIRE(((I2 *)fn(&p, "alias"))(3, 1) == 24);
    REQUIRE(((I2 *)fn(&p, "alias"))(-5, 2) == -18);
    REQUIRE(((I2 *)fn(&p, "narrow"))(1, 2) == 2001015);
    REQUIRE(((I2 *)fn(&p, "narrow"))(300, 70000) == 4464001744);
    REQUIRE(((I2 *)fn(&p, "narrow"))(200, 0) == 637);
    REQUIRE(((I2 *)fn(&p, "choose"))(1, 2) == 101002);
    REQUIRE(((I2 *)fn(&p, "choose"))(5, 3) == 5103);
    REQUIRE(((I2 *)fn(&p, "same"))(1, 2) == 11);
    REQUIRE(((I2 *)fn(&p, "same"))(4, 4) == 11);
    REQUIRE(((I2 *)fn(&p, "accumulate"))(10, 0) == 45);
    REQUIRE(((I2 *)fn(&p, "accumulate"))(0, 7) == 7);
    release_program(&p);

    char *text = malloc(64 * 1024);
    REQUIRE(build(&p, large_frame(text, 64 * 1024)));
    free(text);
    REQUIRE(((I2 *)fn(&p, "big"))(1, 0) == 180901);
    REQUIRE(((I2 *)fn(&p, "big"))(-7, 3) == 176096);
    release_program(&p);
    return true;
}

static const char ARRAYS
    [] = "fn sum(a: int, b: int) -> int {\n"
         "    x: [4]int\n"
         "    x[0] = a\n"
         "    x[1] = b\n"
         "    x[2] += 3\n"
         "    y := x\n"
         "    y[3] = 100\n"
         "    t := 0\n"
         "    for v in y {\n"
         "        t += v\n"
         "    }\n"
         "    return t + x.len + x[3]\n"
         "}\n"
         "fn lit(i: int, j: int) -> int {\n"
         "    a: [3]int = .[10, 20, 30]\n"
         "    b := u8.[1, 2, 3]\n"
         "    m := [2]int.[.[1, 2], .[3, 4]]\n"
         "    return a[i] + int(b[i]) + m[j][1]\n"
         "}\n"
         "fn idx(p: *[3]i32, k: u8) -> i32 {\n"
         "    p.*[k] = 7\n"
         "    q := &p.*[2]\n"
         "    q.* += 1\n"
         "    s: i32 = 0\n"
         "    for v, j in p.* {\n"
         "        s += v * i32(j)\n"
         "    }\n"
         "    return s\n"
         "}\n"
         "fn via(a: int, b: int) -> int {\n"
         "    arr: [3]i32 = .[i32(a), 2, 3]\n"
         "    s := idx(&arr, u8(b & 255))\n"
         "    return int(s) * 1000 + int(arr[0])\n"
         "}\n"
         "fn tails(a: int, b: int) -> int {\n"
         "    x: [100]u8\n"
         "    for i in 0..x.len {\n"
         "        x[i] = u8((i + a) & 255)\n"
         "    }\n"
         "    y := x\n"
         "    h: [33]i16\n"
         "    for i in 0..h.len {\n"
         "        h[i] = i16(i * b)\n"
         "    }\n"
         "    g := h\n"
         "    w: [33]i32\n"
         "    w[32] = i32(a)\n"
         "    z := w\n"
         "    return int(y[99]) * 1000000 + int(g[32]) * 100 + int(z[32]) + "
         "int(y[96])\n"
         "}\n"
         "fn rezero(a: int, b: int) -> int {\n"
         "    t := 0\n"
         "    for k in 0..3 {\n"
         "        z: [40]u64\n"
         "        s: [7]u8\n"
         "        t += int(z[39]) + int(z[20]) + int(s[6])\n"
         "        z[39] = u64(a & 0xFFFF) + 1\n"
         "        z[20] = u64(b & 0xFF)\n"
         "        s[6] = 9\n"
         "    }\n"
         "    return t\n"
         "}\n"
         "fn big(a: int, b: int) -> int {\n"
         "    x: [5000]int\n"
         "    x[0] = a\n"
         "    x[4999] = b\n"
         "    y := x\n"
         "    return y[0] + y[4999] + x.len\n"
         "}\n"
         "fn odd(a: int, b: int) -> int {\n"
         "    m: [7][3]u8\n"
         "    for i in 0..m.len {\n"
         "        for j in 0..3 {\n"
         "            m[i][j] = u8(i * 10 + j)\n"
         "        }\n"
         "    }\n"
         "    r := m[a]\n"
         "    return int(r[b]) + int(m[6][2])\n"
         "}\n"
         "fn rows(i: int, j: int) -> int {\n"
         "    g: [2][3]u16 = .[.[1, 2, 3], .[7, 8, 9]]\n"
         "    p := &g\n"
         "    p.*[1] = .[4, 5, 6]\n"
         "    m := p.*\n"
         "    m[0] = m[1]\n"
         "    r := m[i]\n"
         "    n := p.*.len + m[1].len + r.len\n"
         "    for row in m {\n"
         "        if row[0] == 4 and j == 0 {\n"
         "            continue\n"
         "        }\n"
         "        for v in row {\n"
         "            if v == 6 {\n"
         "                break\n"
         "            }\n"
         "            n += int(v)\n"
         "        }\n"
         "    }\n"
         "    return n * 100 + int(r[2]) + int(g[0][0])\n"
         "}\n"
         "fn kinds(a: int, b: int) -> int {\n"
         "    f: [3]f32 = .[1.5, 2.5, 3.5]\n"
         "    d := f64.[0.25, 0.5]\n"
         "    flags: [4]bool\n"
         "    flags[a & 3] = true\n"
         "    k: i8 = i8(b)\n"
         "    u: u32 = u32(a & 1)\n"
         "    n := 0\n"
         "    for v, i in flags {\n"
         "        if v {\n"
         "            n += i\n"
         "        }\n"
         "    }\n"
         "    return int(f[u] * 2.0) + int(d[1] * 8.0) + n * 100 + int(f[k])\n"
         "}\n"
         "fn swap_rows(a: int, b: int) -> int {\n"
         "    m: [2][2]int = .[.[a, b], .[b, a]]\n"
         "    m = .[m[1], m[0]]\n"
         "    return m[0][0] * 10 + m[0][1]\n"
         "}\n";

TEST_CASE(arrays)
{
    catch_traps();
    Program p;
    REQUIRE(build(&p, ARRAYS));
    int64_t r = 0;
    REQUIRE(((I2 *)fn(&p, "sum"))(1, 2) == 110);
    REQUIRE(((I2 *)fn(&p, "sum"))(-5, 7) == 109);
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
    REQUIRE(((I2 *)fn(&p, "tails"))(1, 2) == 100006498);
    REQUIRE(((I2 *)fn(&p, "tails"))(200, -1) == 42997040);
    REQUIRE(((I2 *)fn(&p, "rezero"))(7, 3) == 0);
    REQUIRE(((I2 *)fn(&p, "rezero"))(-1, 255) == 0);
    REQUIRE(((I2 *)fn(&p, "big"))(1, 2) == 5003);
    REQUIRE(((I2 *)fn(&p, "big"))(-3, 9) == 5006);
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
    REQUIRE(((I2 *)fn(&p, "swap_rows"))(1, 2) == 21);
    REQUIRE(((I2 *)fn(&p, "swap_rows"))(5, -3) == -25);
    release_program(&p);
    return true;
}

static const char STRUCTS
    [] = "enum Color: u8 { red = 1, green, blue }\n"
         "struct Mixed { a: u8, b: i64, c: u16, d: f32, e: bool, f: [3]i16, g: "
         "Color = .green }\n"
         "struct Point { x: int, y: int = 7 }\n"
         "struct Line { a: Point, b: Point }\n"
         "struct Deep { lines: [3]Line, n: u8 = 5 }\n"
         "\n"
         "fn fill(m: *Mixed, k: int) -> int {\n"
         "    m.a = u8(k & 255)\n"
         "    m.b = k * 1000\n"
         "    m.c = u16(k & 0x7FFF) + 1\n"
         "    m.d = f32(k) * 0.5\n"
         "    m.e = k > 0\n"
         "    m.f[0] = i16(k)\n"
         "    m.f[2] = i16(k) - 1\n"
         "    m.g = .blue\n"
         "    return int(m.a) + m.f.len\n"
         "}\n"
         "\n"
         "fn read(ms: *[3]Mixed) -> int {\n"
         "    t := 0\n"
         "    for m, i in ms.* {\n"
         "        t += int(m.a) + int(m.b) + int(m.c) + int(m.f[1]) * i\n"
         "        if m.e and m.g == .red {\n"
         "            t += 1000000\n"
         "        }\n"
         "    }\n"
         "    return t + int(ms.*[1].d * 4.0)\n"
         "}\n"
         "\n"
         "fn lines(a: int, b: int) -> int {\n"
         "    l := Line.{ .{ a, b }, .{ .x = b } }\n"
         "    m := l\n"
         "    m.a.y += 100\n"
         "    l.b = m.a\n"
         "    d: Deep\n"
         "    d.lines[1] = m\n"
         "    e := d\n"
         "    return l.a.x + l.a.y * 10 + l.b.y * 1000 + m.b.y * 100000 + "
         "int(e.n) * 10000000 + e.lines[1].a.y + e.lines[2].b.y\n"
         "}\n"
         "\n"
         "fn defaults(a: int, b: int) -> int {\n"
         "    t := 0\n"
         "    for k in 0..3 {\n"
         "        d: Deep\n"
         "        t += d.lines[2].b.y + int(d.n)\n"
         "        d.lines[2].b.y = a\n"
         "        d.n = u8(b & 255)\n"
         "    }\n"
         "    return t\n"
         "}\n"
         "\n"
         "fn through(p: *Point, k: int) -> int {\n"
         "    p.x += k\n"
         "    q := &p.y\n"
         "    q.* *= 2\n"
         "    r := p.*\n"
         "    r.x = 0\n"
         "    return p.x + r.y\n"
         "}\n"
         "\n"
         "fn via(a: int, b: int) -> int {\n"
         "    pt := Point.{ a, b }\n"
         "    s := through(&pt, 3)\n"
         "    return s * 1000 + pt.x + pt.y\n"
         "}\n"
         "\n"
         "fn many(a: int, b: int) -> int {\n"
         "    big: [100]Mixed\n"
         "    big[99].b = i64(a)\n"
         "    big[a & 63].f[2] = i16(b)\n"
         "    c := big\n"
         "    n := 0\n"
         "    if c[50].g == .green {\n"
         "        n = 1\n"
         "    }\n"
         "    return int(c[99].b) + int(c[a & 63].f[2]) * 10 + n * 100000\n"
         "}\n"
         "\n"
         "fn poke(a: int, b: int) -> int {\n"
         "    m: Mixed\n"
         "    m.f[a] = i16(b)\n"
         "    return int(m.f[2])\n"
         "}\n";

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

typedef int64_t MI(Mixed *, int64_t);
typedef int64_t M3(Mixed *);

TEST_CASE(structs)
{
    catch_traps();
    Program p;
    REQUIRE(build(&p, STRUCTS));
    int64_t r = 0;

    Mixed ms[3];
    memset(ms, 0xAB, sizeof ms);
    REQUIRE(((MI *)fn(&p, "fill"))(&ms[0], 5) == 8);
    REQUIRE(ms[0].a == 5 && ms[0].b == 5000 && ms[0].c == 6 && ms[0].d == 2.5f);
    REQUIRE(ms[0].e && ms[0].f[0] == 5 && ms[0].f[2] == 4 && ms[0].g == 3);
    REQUIRE(((MI *)fn(&p, "fill"))(&ms[2], -3) == 256);
    REQUIRE(ms[2].a == 253 && ms[2].b == -3000 && ms[2].c == 32766 && ms[2].d == -1.5f);
    REQUIRE(!ms[2].e && ms[2].f[0] == -3 && ms[2].f[2] == -4 && ms[2].g == 3);
    memset(&ms[1], 0, sizeof ms[1]);
    ms[0].f[1] = 0;
    ms[2].f[1] = 0;
    ms[1].d = 2.5f;
    ms[1].f[1] = 9;
    ms[1].e = true;
    ms[1].g = 1;
    REQUIRE(((M3 *)fn(&p, "read"))(ms) == 1035049);

    REQUIRE(((I2 *)fn(&p, "lines"))(3, 4) == 50804154);
    REQUIRE(((I2 *)fn(&p, "lines"))(-2, 9) == 50809204);
    REQUIRE(((I2 *)fn(&p, "defaults"))(11, 200) == 36);
    REQUIRE(((I2 *)fn(&p, "via"))(1, 2) == 8008);
    REQUIRE(((I2 *)fn(&p, "via"))(-50, 6) == -35035);
    REQUIRE(((I2 *)fn(&p, "many"))(7, -3) == 99977);
    REQUIRE(((I2 *)fn(&p, "many"))(64, 1000) == 110064);
    REQUIRE(call_i2((I2 *)fn(&p, "poke"), 2, -7, &r) == 0 && r == -7);
    REQUIRE(call_i2((I2 *)fn(&p, "poke"), 1, 5, &r) == 0 && r == 0);
    REQUIRE(call_i2((I2 *)fn(&p, "poke"), 3, 5, &r) == TRAP_BOUNDS);
    release_program(&p);
    return true;
}

static const char
    SLICES[] = "fn sum(xs: []int) -> int {\n"
               "    total := 0\n"
               "    for i in 0..xs.len {\n"
               "        total += xs[i]\n"
               "    }\n"
               "    return total\n"
               "}\n"
               "fn count(s: []u8, c: u8) -> int {\n"
               "    n := 0\n"
               "    rest := s\n"
               "    while rest.len > 0 {\n"
               "        if rest[0] == c {\n"
               "            n += 1\n"
               "        }\n"
               "        rest = rest[1..]\n"
               "    }\n"
               "    return n\n"
               "}\n"
               "fn first(s: []u8) -> int {\n"
               "    return int(s[0])\n"
               "}\n"
               "fn total(s: []u8) -> int {\n"
               "    n := 0\n"
               "    rest := s\n"
               "    while rest.len > 0 {\n"
               "        n += first(rest)\n"
               "        rest = rest[1..]\n"
               "    }\n"
               "    return n\n"
               "}\n"
               "fn tail(s: []int) -> []int {\n"
               "    return s[1..]\n"
               "}\n"
               "fn choose(x: []int, y: []int, left: bool) -> []int {\n"
               "    if left {\n"
               "        return x\n"
               "    }\n"
               "    return y\n"
               "}\n"
               "fn flip(n: int, x: []int) -> []int {\n"
               "    return x[n..]\n"
               "}\n"
               "fn fsum(xs: []f32) -> f32 {\n"
               "    t: f32 = 0.0\n"
               "    for v in xs {\n"
               "        t += v\n"
               "    }\n"
               "    return t\n"
               "}\n"
               "fn mix(a: int, s: []int, b: int, t: []u8) -> int {\n"
               "    return a * s[0] + b * int(t[t.len - 1]) + s.len * 100 + "
               "t.len * 10000\n"
               "}\n"
               "fn kernels(a: int, b: int) -> int {\n"
               "    x: [5]int = .[1, 2, 3, 4, 5]\n"
               "    bytes := u8.[1, 2, 32, 3]\n"
               "    t := tail(x)\n"
               "    for v, i in t {\n"
               "        x[i] += v\n"
               "    }\n"
               "    return sum(x) + count(bytes, u8(a)) * 1000 + "
               "total(bytes[1..=2]) + sum(x[1..3]) + t.len * b\n"
               "}\n"
               "fn window(a: int, b: int) -> int {\n"
               "    x: [10]int\n"
               "    for i in 0..x.len {\n"
               "        x[i] = i * i\n"
               "    }\n"
               "    s := x[a..b]\n"
               "    return sum(s) * 100 + s.len\n"
               "}\n"
               "fn ends(a: int, b: int) -> int {\n"
               "    x: [10]int\n"
               "    for i in 0..x.len {\n"
               "        x[i] = i + 1\n"
               "    }\n"
               "    s := x[a..=b]\n"
               "    t := x[a..]\n"
               "    u := x[..b]\n"
               "    v := x[..=b]\n"
               "    return s.len * 1000000 + t.len * 10000 + u.len * 100 + "
               "v.len + sum(s)\n"
               "}\n"
               "fn nested(a: int, b: int) -> int {\n"
               "    x: [10]u8\n"
               "    for i in 0..x.len {\n"
               "        x[i] = u8(i * 3)\n"
               "    }\n"
               "    s := x[1..9]\n"
               "    t := s[a..]\n"
               "    u := t[..b]\n"
               "    n := 0\n"
               "    for v in u {\n"
               "        n += int(v)\n"
               "    }\n"
               "    return u.len * 1000 + n\n"
               "}\n"
               "fn known(a: int, b: int) -> int {\n"
               "    x: [6]int = .[10, 20, 30, 40, 50, 60]\n"
               "    m := x[1..5][2] + x[1..5].len * 1000\n"
               "    return m + x[1..5][a] * b + x[2..][1..3][a]\n"
               "}\n"
               "fn sizes(a: int, b: int) -> int {\n"
               "    h: [6]i16 = .[1, -2, 3, -4, 5, -6]\n"
               "    w: [4]i32 = .[100, 200, 300, 400]\n"
               "    f: [3]f64 = .[0.5, 1.5, 2.5]\n"
               "    g: [4]bool\n"
               "    g[b & 3] = true\n"
               "    hs := h[a..]\n"
               "    ws := w[b..]\n"
               "    fs := f[a..]\n"
               "    gs := g[a..]\n"
               "    n := 0\n"
               "    for v in hs {\n"
               "        n += int(v)\n"
               "    }\n"
               "    for v in ws {\n"
               "        n += int(v)\n"
               "    }\n"
               "    d: f64 = 0.0\n"
               "    for v in fs {\n"
               "        d += v\n"
               "    }\n"
               "    for v, i in gs {\n"
               "        if v {\n"
               "            n += 1000 * (i + 1)\n"
               "        }\n"
               "    }\n"
               "    return n + int(d * 10.0)\n"
               "}\n"
               "fn rows(a: int, b: int) -> int {\n"
               "    m: [4][3]u8\n"
               "    for i in 0..m.len {\n"
               "        for j in 0..3 {\n"
               "            m[i][j] = u8(i * 10 + j)\n"
               "        }\n"
               "    }\n"
               "    s := m[a..]\n"
               "    r := s[b]\n"
               "    t := s[b..]\n"
               "    return int(r[2]) * 1000 + t.len * 100 + int(s[0][1])\n"
               "}\n"
               "fn through(p: *[4]int, a: int) -> int {\n"
               "    s := p.*[a..]\n"
               "    s[0] = 99\n"
               "    return s.len\n"
               "}\n"
               "fn pointed(a: int, b: int) -> int {\n"
               "    x: [4]int = .[1, 2, 3, 4]\n"
               "    n := through(&x, a)\n"
               "    return n * 1000 + x[0] + x[1] + x[2] + x[3] + b\n"
               "}\n"
               "fn empty(a: int, b: int) -> int {\n"
               "    s: []int\n"
               "    n := s.len\n"
               "    for v in s {\n"
               "        n += v\n"
               "    }\n"
               "    if a > 0 {\n"
               "        return s[b]\n"
               "    }\n"
               "    return n + b\n"
               "}\n"
               "fn swap(a: int, b: int) -> int {\n"
               "    x: [6]int = .[1, 2, 3, 4, 5, 6]\n"
               "    s := x[..a]\n"
               "    t := x[a..]\n"
               "    for k in 0..b {\n"
               "        u := s\n"
               "        s = t\n"
               "        t = u\n"
               "    }\n"
               "    return sum(s) * 100 + t.len\n"
               "}\n"
               "fn picks(a: int, b: int) -> int {\n"
               "    x: [3]int = .[1, 2, 3]\n"
               "    y: [4]int = .[10, 20, 30, 40]\n"
               "    s := choose(x, y, a > b)\n"
               "    t := flip(a, s)\n"
               "    return sum(t) * 10 + t.len\n"
               "}\n"
               "fn mixer(a: int, b: int) -> int {\n"
               "    x: [3]int = .[5, 6, 7]\n"
               "    y := u8.[1, 2, 9]\n"
               "    return mix(a, x[1..], b, y[..b])\n"
               "}\n"
               "fn scan(a: int, b: int) -> int {\n"
               "    x: [8]int = .[3, 1, 4, 1, 5, 9, 2, 6]\n"
               "    s := x[a..]\n"
               "    n := 0\n"
               "    for v, i in s {\n"
               "        if v == b {\n"
               "            break\n"
               "        }\n"
               "        if (v & 1) == 0 {\n"
               "            continue\n"
               "        }\n"
               "        n += v * (i + 1)\n"
               "    }\n"
               "    return n\n"
               "}\n"
               "fn literal(a: int, b: int) -> int {\n"
               "    return sum(.[a, b, 3]) + count(u8.[7, 7, 1], u8(b))\n"
               "}\n"
               "fn narrow(a: int, b: int) -> int {\n"
               "    x: [5]int = .[10, 20, 30, 40, 50]\n"
               "    s := x[1..]\n"
               "    k := i8(a)\n"
               "    j := u8(b)\n"
               "    return s[k] + s[j] * 100 + x[j..][k]\n"
               "}\n"
               "fn tails(a: int, b: int) -> int {\n"
               "    x: [4]int = .[1, 2, 3, 4]\n"
               "    s := x[..a]\n"
               "    n := 0\n"
               "    for v in tail(s) {\n"
               "        n += v\n"
               "    }\n"
               "    return n * 10 + tail(s).len + b\n"
               "}\n"
               "fn floats(a: int, b: int) -> int {\n"
               "    f := f32.[0.5, 1.0, 2.0, 4.0]\n"
               "    return int(fsum(f[a..b]) * 2.0)\n"
               "}\n"
               "fn many(a: int, b: int) -> int {\n"
               "    x: [12]int\n"
               "    for i in 0..x.len {\n"
               "        x[i] = i * b\n"
               "    }\n"
               "    s0 := x[a..]\n"
               "    s1 := x[a..a + 1]\n"
               "    s2 := x[a..a + 2]\n"
               "    s3 := x[a..a + 3]\n"
               "    s4 := x[a..a + 4]\n"
               "    s5 := x[a..a + 5]\n"
               "    s6 := x[a..a + 6]\n"
               "    s7 := x[a..a + 7]\n"
               "    n := 0\n"
               "    for k in 0..3 {\n"
               "        n += sum(s0) + sum(s1) + sum(s2) + sum(s3) + sum(s4) + "
               "sum(s5) + sum(s6) + sum(s7)\n"
               "        s0 = s0[1..]\n"
               "        s7 = s7[..s7.len - 1]\n"
               "    }\n"
               "    return n + s0.len * 1000 + s7.len * 100000\n"
               "}\n"
               "fn measure(n: int, s: []int) -> int {\n"
               "    return n * 100 + s.len * 10 + s[0]\n"
               "}\n"
               "fn chain(a: int, b: int) -> int {\n"
               "    x: [4]int = .[a, b, 3, 4]\n"
               "    return measure(a, tail(x)) + measure(b, tail(tail(x)))\n"
               "}\n"
               "fn rebase(a: int, b: int) -> int {\n"
               "    x: [6]int = .[10, 20, 30, 40, 50, 60]\n"
               "    s := x[0..a]\n"
               "    t := x[b..]\n"
               "    s = t[s.len..]\n"
               "    return s[0] * 10 + s.len\n"
               "}\n"
               "fn skip2(s: []int) -> []int {\n"
               "    return tail(tail(s))\n"
               "}\n"
               "fn swapped(u0: int, u1: int, s: []int) -> []int {\n"
               "    t := s[1..]\n"
               "    a0 := 1\n"
               "    a1 := 2\n"
               "    a2 := 3\n"
               "    a3 := 4\n"
               "    a4 := 5\n"
               "    a5 := 6\n"
               "    a6 := 7\n"
               "    a7 := 8\n"
               "    a8 := 9\n"
               "    a9 := 10\n"
               "    a10 := 11\n"
               "    a11 := 12\n"
               "    a12 := 13\n"
               "    a13 := 14\n"
               "    a14 := 15\n"
               "    a15 := 16\n"
               "    a16 := 17\n"
               "    a17 := 18\n"
               "    m := 0\n"
               "    for k in 0..3 {\n"
               "        m += t[0] + t[1] + t[2] + t[3] + t[4] + t.len\n"
               "        a0 += 1\n"
               "        a1 += 1\n"
               "        a2 += 1\n"
               "        a3 += 1\n"
               "        a4 += 1\n"
               "        a5 += 1\n"
               "        a6 += 1\n"
               "        a7 += 1\n"
               "        a8 += 1\n"
               "        a9 += 1\n"
               "        a10 += 1\n"
               "        a11 += 1\n"
               "        a12 += 1\n"
               "        a13 += 1\n"
               "        a14 += 1\n"
               "        a15 += 1\n"
               "        a16 += 1\n"
               "        a17 += 1\n"
               "    }\n"
               "    if m == a0 + a1 + a2 + a3 + a4 + a5 + a6 + a7 + a8 + a9 + "
               "a10 + a11 + a12 + a13 + a14 + a15 + a16 + a17 {\n"
               "        return t[1..]\n"
               "    }\n"
               "    return t\n"
               "}\n"
               "fn lone(u0: int, u1: int, s: []int) -> []int {\n"
               "    t := s[1..]\n"
               "    a0 := 1\n"
               "    a1 := 2\n"
               "    a2 := 3\n"
               "    a3 := 4\n"
               "    a4 := 5\n"
               "    a5 := 6\n"
               "    a6 := 7\n"
               "    a7 := 8\n"
               "    a8 := 9\n"
               "    a9 := 10\n"
               "    a10 := 11\n"
               "    a11 := 12\n"
               "    a12 := 13\n"
               "    a13 := 14\n"
               "    a14 := 15\n"
               "    a15 := 16\n"
               "    a16 := 17\n"
               "    a17 := 18\n"
               "    m := 0\n"
               "    for k in 0..3 {\n"
               "        m += t[0] + t.len + t.len + t.len + t.len + t.len\n"
               "        a0 += 1\n"
               "        a1 += 1\n"
               "        a2 += 1\n"
               "        a3 += 1\n"
               "        a4 += 1\n"
               "        a5 += 1\n"
               "        a6 += 1\n"
               "        a7 += 1\n"
               "        a8 += 1\n"
               "        a9 += 1\n"
               "        a10 += 1\n"
               "        a11 += 1\n"
               "        a12 += 1\n"
               "        a13 += 1\n"
               "        a14 += 1\n"
               "        a15 += 1\n"
               "        a16 += 1\n"
               "        a17 += 1\n"
               "    }\n"
               "    if m == a0 + a1 + a2 + a3 + a4 + a5 + a6 + a7 + a8 + a9 + "
               "a10 + a11 + a12 + a13 + a14 + a15 + a16 + a17 {\n"
               "        return t[1..]\n"
               "    }\n"
               "    return t\n"
               "}\n";

typedef struct
{
    const void *ptr;
    int64_t len;
} Slice;

typedef int64_t SI(Slice);
typedef int64_t SU8(Slice, uint8_t);
typedef Slice SS(Slice);
typedef float SF(Slice);
typedef int64_t ISIS(int64_t, Slice, int64_t, Slice);
typedef Slice IIS(int64_t, int64_t, Slice);

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
    REQUIRE(((SI *)fn(&p, "sum"))((Slice){ xs, 3 }) == 15);
    REQUIRE(((SU8 *)fn(&p, "count"))((Slice){ "a,b,,c", 6 }, ',') == 3);
    Slice t = ((SS *)fn(&p, "tail"))((Slice){ xs, 3 });
    REQUIRE(t.ptr == xs + 1 && t.len == 2);
    REQUIRE(((SF *)fn(&p, "fsum"))((Slice){ fs, 3 }) == 7.5f);
    REQUIRE(((ISIS *)fn(&p, "mix"))(2, (Slice){ xs, 3 }, 3, (Slice){ "xyz", 2 }) == 20671);
    t = ((SS *)fn(&p, "skip2"))((Slice){ xs, 3 });
    REQUIRE(t.ptr == xs + 2 && t.len == 1);

    int64_t ws[8] = { 0, 10, 20, 30, 40, 50, 60, 70 };
    int64_t vs[8] = { 0, 10, 12, 14, 16, 16, 0, 0 };
    int64_t us[8] = { 0, 40, 0, 0, 0, 0, 0, 0 };
    IIS *swapped = (IIS *)fn(&p, "swapped");
    IIS *lone = (IIS *)fn(&p, "lone");
    t = swapped(0, 0, (Slice){ ws, 8 });
    REQUIRE(t.ptr == ws + 1 && t.len == 7);
    t = swapped(0, 0, (Slice){ vs, 8 });
    REQUIRE(t.ptr == vs + 2 && t.len == 6);
    t = lone(0, 0, (Slice){ ws, 8 });
    REQUIRE(t.ptr == ws + 1 && t.len == 7);
    t = lone(0, 0, (Slice){ us, 8 });
    REQUIRE(t.ptr == us + 2 && t.len == 6);

    REQUIRE(agree_on_grid(&p, SLICE_FUNCTIONS, sizeof SLICE_FUNCTIONS / sizeof SLICE_FUNCTIONS[0])
    );
    release_program(&p);
    return true;
}

static const char
    STORED_SLICES[] = "struct Text { data: []u8, n: int, tag: u8 }\n"
                      "struct Grid { rows: [][]int, w: int }\n"
                      "struct Span { s: []int, k: int }\n"
                      "struct Node { kids: []Node, v: int }\n"
                      "\n"
                      "fn measure(t: *Text) -> int {\n"
                      "    n := 0\n"
                      "    for c in t.data {\n"
                      "        if c == 44 {\n"
                      "            n += 1\n"
                      "        }\n"
                      "    }\n"
                      "    t.n = n\n"
                      "    t.tag = t.data[0]\n"
                      "    t.data = t.data[1..]\n"
                      "    return n * 100 + t.data.len\n"
                      "}\n"
                      "\n"
                      "fn total(g: *Grid) -> int {\n"
                      "    s := 0\n"
                      "    for row, i in g.rows {\n"
                      "        for x in row {\n"
                      "            s += x * (i + 1)\n"
                      "        }\n"
                      "    }\n"
                      "    g.w = g.rows[0].len\n"
                      "    return s\n"
                      "}\n"
                      "\n"
                      "fn get(g: *Grid, i: int) -> []int {\n"
                      "    return g.rows[i]\n"
                      "}\n"
                      "\n"
                      "fn put(g: *Grid, i: int, s: []int) {\n"
                      "    g.rows[i] = s\n"
                      "}\n"
                      "\n"
                      "fn spans(a: int, b: int) -> int {\n"
                      "    xs := int.[1, 2, 3, 4, 5, 6]\n"
                      "    p := Span.{ xs[a..], b }\n"
                      "    q := p\n"
                      "    q.s = q.s[..b]\n"
                      "    ps := Span.[p, q, .{ .k = 7 }]\n"
                      "    t := 0\n"
                      "    for e in ps {\n"
                      "        t = t * 10 + e.s.len\n"
                      "        t = t * 10 + e.k\n"
                      "    }\n"
                      "    return t\n"
                      "}\n"
                      "\n"
                      "fn table(a: int, b: int) -> int {\n"
                      "    x := int.[1, 2, 3]\n"
                      "    y := int.[10, 20]\n"
                      "    z: [4]int\n"
                      "    m := []int.[x, y[1..], z[a..]]\n"
                      "    m[b % 3] = x[..b]\n"
                      "    t := 0\n"
                      "    for r, i in m {\n"
                      "        for v in r {\n"
                      "            t += v * (i + 1)\n"
                      "        }\n"
                      "        t = t * 10 + r.len\n"
                      "    }\n"
                      "    return t\n"
                      "}\n"
                      "\n"
                      "fn deep(a: int, b: int) -> int {\n"
                      "    x := int.[1, 2, 3, 4]\n"
                      "    rows := []int.[x[..a], x[a..]]\n"
                      "    all: [][]int = rows\n"
                      "    two := all[1..]\n"
                      "    pick := two[0]\n"
                      "    return pick.len * 100 + all[0].len * 10 + pick[b]\n"
                      "}\n"
                      "\n"
                      "fn next(p: *[]int) -> int {\n"
                      "    v := p.*[0]\n"
                      "    p.* = p.*[1..]\n"
                      "    return v\n"
                      "}\n"
                      "\n"
                      "fn walk(a: int, b: int) -> int {\n"
                      "    x := int.[1, 2, 3, 4, 5]\n"
                      "    s: []int = x\n"
                      "    p := &s\n"
                      "    t := 0\n"
                      "    for _ in 0..a {\n"
                      "        t = t * 10 + next(p)\n"
                      "    }\n"
                      "    return t * 10 + s.len + p.len * 100 + b\n"
                      "}\n"
                      "\n"
                      "fn cut(p: *[]int, k: int) -> int {\n"
                      "    p.* = p.*[k..]\n"
                      "    return k\n"
                      "}\n"
                      "\n"
                      "fn mix(s: []int, k: int, t: []int, b: int) -> int {\n"
                      "    return s.len * 1000 + k * 100 + t.len * 10 + t[b]\n"
                      "}\n"
                      "\n"
                      "fn order(a: int, b: int) -> int {\n"
                      "    x := int.[1, 2, 3, 4, 5]\n"
                      "    s: []int = x\n"
                      "    return mix(s, cut(&s, a), s, b)\n"
                      "}\n"
                      "\n"
                      "fn chain(a: int, b: int) -> int {\n"
                      "    n0 := Node.{ .v = a }\n"
                      "    leaves := Node.[n0, .{ .v = b }]\n"
                      "    mid := Node.{ leaves, 10 }\n"
                      "    tops := Node.[mid, mid]\n"
                      "    root := Node.{ tops[..1], 100 }\n"
                      "    t := root.v\n"
                      "    for m in root.kids {\n"
                      "        t += m.v\n"
                      "        for l in m.kids {\n"
                      "            t = t * 10 + l.v + l.kids.len\n"
                      "        }\n"
                      "    }\n"
                      "    return t\n"
                      "}\n";

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

typedef int64_t TI(Text *);
typedef int64_t GI(Grid *);
typedef Slice GS(Grid *, int64_t);
typedef void GP(Grid *, int64_t, Slice);

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
    REQUIRE(((TI *)fn(&p, "measure"))(&t) == 305);
    REQUIRE(t.n == 3 && t.tag == 'x' && t.data.ptr == text + 1 && t.data.len == 5);

    int64_t r0[2] = { 1, 2 };
    int64_t r1[3] = { 3, 4, 5 };
    Slice rows[2] = { { r0, 2 }, { r1, 3 } };
    Grid g = { { rows, 2 }, 0 };
    REQUIRE(((GI *)fn(&p, "total"))(&g) == 27 && g.w == 2);
    Slice row = ((GS *)fn(&p, "get"))(&g, 1);
    REQUIRE(row.ptr == r1 && row.len == 3);
    ((GP *)fn(&p, "put"))(&g, 0, (Slice){ r1 + 1, 2 });
    REQUIRE(rows[0].ptr == r1 + 1 && rows[0].len == 2);
    REQUIRE(((GI *)fn(&p, "total"))(&g) == 33 && g.w == 2);

    REQUIRE(agree_on_grid(
        &p,
        STORED_SLICE_FUNCTIONS,
        sizeof STORED_SLICE_FUNCTIONS / sizeof STORED_SLICE_FUNCTIONS[0]
    ));
    release_program(&p);
    return true;
}

static const char PASSED_STRUCTS[] =
    "struct B1 { a: u8 }\n"
    "struct B3 { a: u8, b: u8, c: u8 }\n"
    "struct H3 { a: u16, b: u16, c: u16 }\n"
    "struct B7 { a: [7]u8 }\n"
    "struct P { x: i32, y: i32 }\n"
    "struct B11 { a: [11]u8 }\n"
    "struct I3 { a: i32, b: i32, c: i32 }\n"
    "struct H7 { a: [7]u16 }\n"
    "struct B15 { a: [15]u8 }\n"
    "struct LD { a: int, b: f64 }\n"
    "struct FI { a: f32, b: i32 }\n"
    "struct FD { a: f32, b: f64 }\n"
    "struct S { s: []u8 }\n"
    "struct V3 { x: f32, y: f32, z: f32 }\n"
    "struct V4 { v: [4]f32 }\n"
    "struct D4 { x: f64, y: f64, z: f64, w: f64 }\n"
    "struct V2 { x: f32, y: f32 }\n"
    "struct N3 { a: V2, z: f32 }\n"
    "struct D1 { x: f64 }\n"
    "struct D2 { x: f64, y: f64 }\n"
    "\n"
    "fn tB1(s: B1) -> B1 {\n"
    "    return B1.{ s.a +% 1 }\n"
    "}\n"
    "\n"
    "fn tB3(s: B3) -> B3 {\n"
    "    return B3.{ s.a +% 1, s.b +% 2, s.c +% 3 }\n"
    "}\n"
    "\n"
    "fn tH3(s: H3) -> H3 {\n"
    "    return H3.{ s.a +% 1, s.b +% 2, s.c +% 3 }\n"
    "}\n"
    "\n"
    "fn tB7(s: B7) -> B7 {\n"
    "    r := s\n"
    "    for i in 0..7 {\n"
    "        r.a[i] +%= u8(i + 1)\n"
    "    }\n"
    "    return r\n"
    "}\n"
    "\n"
    "fn tP(s: P) -> P {\n"
    "    return P.{ s.y, s.x }\n"
    "}\n"
    "\n"
    "fn tB11(s: B11) -> B11 {\n"
    "    r := s\n"
    "    for i in 0..11 {\n"
    "        r.a[i] +%= u8(i + 1)\n"
    "    }\n"
    "    return r\n"
    "}\n"
    "\n"
    "fn tI3(s: I3) -> I3 {\n"
    "    return I3.{ s.c, s.a, s.b }\n"
    "}\n"
    "\n"
    "fn tH7(s: H7) -> H7 {\n"
    "    r := s\n"
    "    for i in 0..7 {\n"
    "        r.a[i] +%= u16(i + 1)\n"
    "    }\n"
    "    return r\n"
    "}\n"
    "\n"
    "fn tB15(s: B15) -> B15 {\n"
    "    r := s\n"
    "    for i in 0..15 {\n"
    "        r.a[i] +%= u8(i + 1)\n"
    "    }\n"
    "    return r\n"
    "}\n"
    "\n"
    "fn tLD(s: LD) -> LD {\n"
    "    return LD.{ s.a + 1, s.b * 2 }\n"
    "}\n"
    "\n"
    "fn tFI(s: FI) -> FI {\n"
    "    return FI.{ s.a + 0.5, s.b - 1 }\n"
    "}\n"
    "\n"
    "fn tFD(s: FD) -> FD {\n"
    "    return FD.{ s.a * 2, s.b + 0.25 }\n"
    "}\n"
    "\n"
    "fn tS(s: S) -> S {\n"
    "    return S.{ s.s[1..] }\n"
    "}\n"
    "\n"
    "fn tV3(s: V3) -> V3 {\n"
    "    return V3.{ s.z, s.x, s.y }\n"
    "}\n"
    "\n"
    "fn tV4(s: V4) -> V4 {\n"
    "    r := s\n"
    "    r.v[0] += 1\n"
    "    r.v[3] -= 1\n"
    "    return r\n"
    "}\n"
    "\n"
    "fn tD4(s: D4) -> D4 {\n"
    "    return D4.{ s.w, s.z, s.y, s.x }\n"
    "}\n"
    "\n"
    "fn tN3(s: N3) -> N3 {\n"
    "    return N3.{ V2.{ s.z, s.a.x }, s.a.y }\n"
    "}\n"
    "\n"
    "fn tD1(s: D1) -> D1 {\n"
    "    return D1.{ -s.x }\n"
    "}\n"
    "\n"
    "fn mix(a: int, b: B3, c: f32, d: V3, e: P, f: f64, g: D2, h: u8) -> f64 "
    "{\n"
    "    n := a + int(b.a) * 2 + int(b.b) * 3 + int(b.c) * 4 + int(e.x) * 5 + "
    "int(e.y) * 6 + int(h) * 7\n"
    "    return f64(n) + f64(c) * 8 + f64(d.x) * 9 + f64(d.y) * 10 + f64(d.z) "
    "* 11 + f * 12 + g.x * 13 + g.y * 14\n"
    "}\n"
    "\n"
    "fn first(s: P, z: i32) -> i32 {\n"
    "    return s.x + z\n"
    "}\n"
    "\n"
    "fn bump(q: *P) -> i32 {\n"
    "    q.x = 100\n"
    "    return 0\n"
    "}\n"
    "\n"
    "fn late(a: int, b: int) -> int {\n"
    "    p := P.{ i32(a & 0xFF), i32(b & 0xFF) }\n"
    "    return int(first(p, bump(&p)))\n"
    "}\n"
    "\n"
    "fn chain(a: int, b: int) -> int {\n"
    "    p := P.{ i32(a & 0xFFFF), i32(b & 0xFFFF) }\n"
    "    q := tP(tP(tP(p)))\n"
    "    v := tV3(tV3(V3.{ f32(a & 7), f32(b & 7), 2.5 }))\n"
    "    t := tB3(B3.{ u8(a & 0xFF), u8(b & 0xFF), 250 })\n"
    "    w := tD4(D4.{ 1, f64(a & 3), f64(b & 3), 4 })\n"
    "    k := tLD(LD.{ a & 0xFFFF, f64(b & 15) })\n"
    "    r := mix(a & 0xFF, t, v.x, v, q, w.y, D2.{ w.x, w.z }, u8(b & 0x7F))\n"
    "    return int(r) + int(q.x) * 3 + int(t.c) + int(k.b) + k.a\n"
    "}\n"
    "\n"
    "fn rows(a: int, b: int) -> int {\n"
    "    xs: [4]P\n"
    "    for i in 0..4 {\n"
    "        xs[i] = P.{ i32(a & 0xFF) + i32(i), i32(b & 0xFF) - i32(i) }\n"
    "    }\n"
    "    total := 0\n"
    "    for i in 0..4 {\n"
    "        total += int(first(tP(xs[i]), i32(i)))\n"
    "    }\n"
    "    pp := &xs[a & 3]\n"
    "    return total * 100 + int(tP(pp.*).x)\n"
    "}\n"
    "\n"
    "fn lateI(s: LD, k: int) -> int {\n"
    "    return s.a * 1000 + int(s.b) * 10 + k\n"
    "}\n"
    "\n"
    "fn lateF(v: V3, x: f64) -> f64 {\n"
    "    return f64(v.x) * 100 + f64(v.y) * 10 + f64(v.z) + x * 1000\n"
    "}\n"
    "\n"
    "fn lateX(s: LD, v: V3, x: f64) -> f64 {\n"
    "    return f64(s.a) + s.b * 10 + f64(v.z) * 100 + x * 1000\n"
    "}\n"
    "\n"
    "fn lateK(v: V3, k: int) -> int {\n"
    "    return int(v.x) * 100 + k\n"
    "}\n"
    "\n"
    "fn lateS(s: LD, t: []u8) -> int {\n"
    "    return s.a + int(s.b) + t.len * 100 + int(t[0])\n"
    "}\n"
    "\n"
    "fn tails(a: int, b: int) -> int {\n"
    "    k := i32(b & 0x7F)\n"
    "    x := f32(a & 7)\n"
    "    bytes := u8.[9, 8, 7]\n"
    "    bytes[0] = u8(b & 0xFF)\n"
    "    r := lateI(LD.{ a & 0xFF, f64(b & 15) }, k)\n"
    "    q := lateF(V3.{ f32(a & 3), f32(b & 3), 1 }, x)\n"
    "    y := lateX(LD.{ a & 0xF, 2 }, V3.{ 1, 2, f32(b & 7) }, x)\n"
    "    z := lateK(V3.{ f32(b & 3), 0, 0 }, k)\n"
    "    u := lateS(LD.{ a & 0xF, 2 }, bytes)\n"
    "    return r + int(q) + int(y) + z + u\n"
    "}\n"
    "\n"
    "fn eB3(s: B3, k: u8) -> B3 {\n"
    "    return s\n"
    "}\n"
    "\n"
    "fn eV3(k: f32, s: V3) -> V3 {\n"
    "    return s\n"
    "}\n"
    "\n"
    "fn eB15(s: B15, t: B7) -> B15 {\n"
    "    return s\n"
    "}\n"
    "\n"
    "fn eD4(a: D4, b: P, c: f64) -> D4 {\n"
    "    return a\n"
    "}\n"
    "\n"
    "fn eS(s: S, n: int) -> S {\n"
    "    return s\n"
    "}\n"
    "\n"
    "fn callB3(a: int, b: int) -> int {\n"
    "    t := eB3(B3.{ u8(a & 0xFF), u8(b & 0xFF), 7 }, 3)\n"
    "    return int(t.a) + int(t.b) * 256 + int(t.c) * 65536\n"
    "}\n"
    "\n"
    "fn callV3(a: int, b: int) -> int {\n"
    "    v := eV3(0.5, V3.{ f32(a & 15), f32(b & 15), 3 })\n"
    "    return int(v.x * 2) * 10000 + int(v.y * 2) * 100 + int(v.z * 2)\n"
    "}\n"
    "\n"
    "fn callB15(a: int, b: int) -> int {\n"
    "    x: B15\n"
    "    for i in 0..15 {\n"
    "        x.a[i] = u8((a + i * 3) & 0xFF)\n"
    "    }\n"
    "    y := eB15(x, B7.{ .[1, 2, 3, 4, 5, 6, u8(b & 0xFF)] })\n"
    "    total := 0\n"
    "    for i in 0..15 {\n"
    "        total = total * 3 + int(y.a[i])\n"
    "    }\n"
    "    return total\n"
    "}\n"
    "\n"
    "fn callD4(a: int, b: int) -> int {\n"
    "    d := eD4(D4.{ 1, 2, 3, f64(b & 15) }, P.{ i32(a & 0xFF), 6 }, 0.25)\n"
    "    return int(d.x * 4) * 1000000 + int(d.y * 4) * 10000 + int(d.z * 4) * "
    "100 + int(d.w * 4)\n"
    "}\n"
    "\n"
    "fn callS(a: int, b: int) -> int {\n"
    "    bytes := u8.[1, 2, 3, 4, 5, 6]\n"
    "    r := eS(S.{ bytes[a & 1..] }, b & 3)\n"
    "    return r.s.len * 100 + int(r.s[0])\n"
    "}\n";

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

typedef double MIX(int64_t, CB3, float, CV3, CP, double, CD2, uint8_t);

static CB3
c_eB3(CB3 s, uint8_t k)
{
    return (CB3){ (uint8_t)(s.a + k), (uint8_t)(s.b + k), (uint8_t)(s.c + k) };
}

static CV3
c_eV3(float k, CV3 s)
{
    return (CV3){ s.x + k, s.y + k, s.z + k };
}

static CB15
c_eB15(CB15 s, CB7 t)
{
    for (int i = 0; i < 15; i++) {
        s.a[i] = (uint8_t)(s.a[i] + t.a[i % 7]);
    }
    return s;
}

static CD4
c_eD4(CD4 a, CP b, double c)
{
    return (CD4){ a.w + c, a.z + b.x, a.y + b.y, a.x };
}

static CS
c_eS(CS s, int64_t n)
{
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
            if (((I2 *)fn(p, "callB3"))(a, b) != t ||
                ((I2 *)fn(p, "callV3"))(a, b) != vx * 10000 + vy * 100 + 7 ||
                ((I2 *)fn(p, "callB15"))(a, b) != sum ||
                ((I2 *)fn(p, "callD4"))(a, b) != d ||
                ((I2 *)fn(p, "callS"))(a, b) != (6 - off) * 100 + 1 + off) {
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

    REQUIRE(((CB1(*)(CB1))fn(&p, "tB1"))((CB1){ 0xFF }).a == 0);
    CB3 b3 = ((CB3(*)(CB3))fn(&p, "tB3"))((CB3){ 10, 20, 254 });
    REQUIRE(b3.a == 11 && b3.b == 22 && b3.c == 1);
    CH3 h3 = ((CH3(*)(CH3))fn(&p, "tH3"))((CH3){ 1000, 0xFFFF, 7 });
    REQUIRE(h3.a == 1001 && h3.b == 1 && h3.c == 10);
    CB7 b7 = ((CB7(*)(CB7))fn(&p, "tB7"))((CB7){ { 1, 2, 3, 4, 5, 6, 0xFF } });
    for (int i = 0; i < 7; i++) {
        REQUIRE(b7.a[i] == (uint8_t)((i < 6 ? i + 1 : 0xFF) + i + 1));
    }
    CP pi = ((CP(*)(CP))fn(&p, "tP"))((CP){ -5, 7 });
    REQUIRE(pi.x == 7 && pi.y == -5);
    CB11 b11;
    for (int i = 0; i < 11; i++) {
        b11.a[i] = (uint8_t)(0xA0 + i);
    }
    b11 = ((CB11(*)(CB11))fn(&p, "tB11"))(b11);
    for (int i = 0; i < 11; i++) {
        REQUIRE(b11.a[i] == (uint8_t)(0xA0 + 2 * i + 1));
    }
    CI3 i3 = ((CI3(*)(CI3))fn(&p, "tI3"))((CI3){ 1, -2, 3 });
    REQUIRE(i3.a == 3 && i3.b == 1 && i3.c == -2);
    CH7 h7;
    for (int i = 0; i < 7; i++) {
        h7.a[i] = (uint16_t)(0xF000 + i * 0x111);
    }
    h7 = ((CH7(*)(CH7))fn(&p, "tH7"))(h7);
    for (int i = 0; i < 7; i++) {
        REQUIRE(h7.a[i] == (uint16_t)(0xF000 + i * 0x111 + i + 1));
    }
    CB15 b15;
    for (int i = 0; i < 15; i++) {
        b15.a[i] = (uint8_t)(0x30 + 7 * i);
    }
    b15 = ((CB15(*)(CB15))fn(&p, "tB15"))(b15);
    for (int i = 0; i < 15; i++) {
        REQUIRE(b15.a[i] == (uint8_t)(0x30 + 8 * i + 1));
    }
    CLD ld = ((CLD(*)(CLD))fn(&p, "tLD"))((CLD){ -9, 1.25 });
    REQUIRE(ld.a == -8 && ld.b == 2.5);
    CFI fi = ((CFI(*)(CFI))fn(&p, "tFI"))((CFI){ 2.0f, 40 });
    REQUIRE(fi.a == 2.5f && fi.b == 39);
    CFD fd = ((CFD(*)(CFD))fn(&p, "tFD"))((CFD){ 1.5f, -1.0 });
    REQUIRE(fd.a == 3.0f && fd.b == -0.75);
    static const uint8_t text[] = "hello";
    CS s = ((CS(*)(CS))fn(&p, "tS"))((CS){ { text, 5 } });
    REQUIRE(s.s.ptr == text + 1 && s.s.len == 4);
    CV3 v3 = ((CV3(*)(CV3))fn(&p, "tV3"))((CV3){ 1, 2, 3 });
    REQUIRE(v3.x == 3 && v3.y == 1 && v3.z == 2);
    CV4 v4 = ((CV4(*)(CV4))fn(&p, "tV4"))((CV4){ { 1, 2, 3, 4 } });
    REQUIRE(v4.v[0] == 2 && v4.v[1] == 2 && v4.v[2] == 3 && v4.v[3] == 3);
    CD4 d4 = ((CD4(*)(CD4))fn(&p, "tD4"))((CD4){ 1, 2, 3, 4 });
    REQUIRE(d4.x == 4 && d4.y == 3 && d4.z == 2 && d4.w == 1);
    CN3 n3 = ((CN3(*)(CN3))fn(&p, "tN3"))((CN3){ { 1, 2 }, 3 });
    REQUIRE(n3.a.x == 3 && n3.a.y == 1 && n3.z == 2);
    REQUIRE(((CD1(*)(CD1))fn(&p, "tD1"))((CD1){ 2.5 }).x == -2.5);
    double m = ((MIX *)fn(&p, "mix"))(
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
    REQUIRE(((I2 *)fn(&p, "late"))(5, 6) == 5);

    REQUIRE(agree_on_grid(
        &p,
        PASSED_STRUCT_FUNCTIONS,
        sizeof PASSED_STRUCT_FUNCTIONS / sizeof PASSED_STRUCT_FUNCTIONS[0]
    ));
    REQUIRE(passes_to_c(&p));
    release_program(&p);

    Program q;
    REQUIRE(check_text(
        &q,
        "struct P { a: int, b: int }\n"
        "fn f(a: int, b: int, c: int, d: int, e: int, f: int, g: int, p: P) -> "
        "int {\n"
        "    return a + p.b\n"
        "}\n"
    ));
    REQUIRE(!arm64_compile(&q.n, &q.c) && strstr(q.n.error, "more than 8 integer"));
    release_program(&q);
    return true;
}

static const char LEFT_TO_RIGHT[] =
    "struct P { x: i32, y: i32 }\n"
    "struct Q { p: P, n: int }\n"
    "struct L { p: P, q: P }\n"
    "struct Z { p: P, n: int, m: int }\n"
    "struct PB { p: P, ok: bool }\n"
    "struct WZ { a: [3]i32, n: int, m: int }\n"
    "struct Y { n: i32, p: P, m: int }\n"
    "\n"
    "fn bump(q: *P) -> i32 {\n"
    "    q.x += 100\n"
    "    return 1\n"
    "}\n"
    "\n"
    "fn grow(r: *[3]i32) -> i32 {\n"
    "    r.*[0] += 1000\n"
    "    r.*[2] += 2000\n"
    "    return 2\n"
    "}\n"
    "\n"
    "fn twice(m: *[2][2]i32) -> int {\n"
    "    m.*[0][1] += 50\n"
    "    return 1\n"
    "}\n"
    "\n"
    "fn first(s: P, z: i32) -> i32 {\n"
    "    return s.x + z\n"
    "}\n"
    "\n"
    "fn total(s: []i32, z: i32) -> int {\n"
    "    return int(s[0]) + int(z)\n"
    "}\n"
    "\n"
    "fn kept(s: P, b: int) -> int {\n"
    "    l := L.{ s, P.{ i32(b & 0xFF), 0 } }\n"
    "    return int(l.p.x) * 1000 + int(l.q.x)\n"
    "}\n"
    "\n"
    "fn arrays(a: int, b: int) -> int {\n"
    "    r := i32.[i32(a & 0xFF), i32(b & 0xFF), 3]\n"
    "    m := [3]i32.[r, .[0, 0, grow(&r)]]\n"
    "    return int(m[0][0]) * 1000000 + int(m[0][2]) * 100000 + int(r[0]) * 10 + int(m[1][2])\n"
    "}\n"
    "\n"
    "fn structs(a: int, b: int) -> int {\n"
    "    p := P.{ i32(a & 0xFF), 3 }\n"
    "    xs := P.[p, .{ bump(&p), 0 }]\n"
    "    return int(xs[0].x) * 10000 + int(p.x) * 10 + int(xs[1].x)\n"
    "}\n"
    "\n"
    "fn fields(a: int, b: int) -> int {\n"
    "    p := P.{ i32(a & 0xFF), i32(b & 0xFF) }\n"
    "    l := L.{ p, P.{ bump(&p), 0 } }\n"
    "    return int(l.p.x) * 10000 + int(p.x) * 10 + int(l.q.x)\n"
    "}\n"
    "\n"
    "fn named(a: int, b: int) -> int {\n"
    "    p := P.{ i32(a & 0xFF), 7 }\n"
    "    z := Z.{ .p = p, .n = int(bump(&p)) }\n"
    "    return int(z.p.x) * 1000 + z.n * 100 + z.m + int(p.x) * 100000\n"
    "}\n"
    "\n"
    "fn zeroed(a: int, b: int) -> int {\n"
    "    r := i32.[i32(a & 0xFF), 1, 2]\n"
    "    w := WZ.{ .a = r, .n = int(grow(&r)) }\n"
    "    return int(w.a[0]) * 100000 + int(r[0]) * 10 + w.n + w.m\n"
    "}\n"
    "\n"
    "fn branch(a: int, b: int) -> int {\n"
    "    p := P.{ i32(a & 0xFF), 0 }\n"
    "    t := PB.{ p, b > 3 and bump(&p) > 0 }\n"
    "    k := 0\n"
    "    if t.ok {\n"
    "        k = 1\n"
    "    }\n"
    "    return int(t.p.x) * 1000 + int(p.x) * 10 + k\n"
    "}\n"
    "\n"
    "fn either(a: int, b: int) -> int {\n"
    "    p := P.{ i32(a & 0xFF), 0 }\n"
    "    t := PB.{ p, b > 5 or bump(&p) > 0 }\n"
    "    u := PB.{ p, b > 2 and a > 1 }\n"
    "    k := 0\n"
    "    if t.ok and !u.ok {\n"
    "        k = 1\n"
    "    }\n"
    "    return int(t.p.x) * 1000000 + int(u.p.x) * 1000 + int(p.x) * 2 + k\n"
    "}\n"
    "\n"
    "fn block(a: int, b: int) -> int {\n"
    "    p := P.{ i32(a & 0xFF), 0 }\n"
    "    l := L.{ p, P.{ v: { p.x = 9; break v i32(b & 0xFF) }, 0 } }\n"
    "    return int(l.p.x) * 100000 + int(p.x) * 1000 + int(l.q.x)\n"
    "}\n"
    "\n"
    "fn chosen(a: int, b: int) -> int {\n"
    "    p := P.{ i32(a & 0xFF), 0 }\n"
    "    l := L.{ p, P.{ switch b & 3 { 0 => bump(&p), 1 => 5, _ => 6 }, 0 } }\n"
    "    return int(l.p.x) * 100000 + int(p.x) * 100 + int(l.q.x)\n"
    "}\n"
    "\n"
    "fn pass(a: int, b: int) -> int {\n"
    "    p := P.{ i32(a & 0xFF), i32(b & 0xFF) }\n"
    "    q := &p\n"
    "    l := L.{ q.*, P.{ first(q.*, bump(q)), 0 } }\n"
    "    return int(l.p.x) * 1000000 + int(l.q.x) * 1000 + int(p.x)\n"
    "}\n"
    "\n"
    "fn base(a: int, b: int) -> int {\n"
    "    r := i32.[i32(a & 0xFF), 0, 0]\n"
    "    l := P.{ r[grow(&r)], r[0] }\n"
    "    return int(l.x) * 100000 + int(l.y)\n"
    "}\n"
    "\n"
    "fn rows(a: int, b: int) -> int {\n"
    "    m := [2]i32.[.[i32(a & 0xFF), 0], .[0, 0]]\n"
    "    v := P.{ m[0][twice(&m)], m[0][0] }\n"
    "    return int(v.x) * 100000 + int(v.y)\n"
    "}\n"
    "\n"
    "fn slices(a: int, b: int) -> int {\n"
    "    r := i32.[i32(a & 0xFF), 0, 0]\n"
    "    return total(r, grow(&r))\n"
    "}\n"
    "\n"
    "fn elems(a: int, b: int) -> int {\n"
    "    xs: [3]P\n"
    "    xs[1] = P.{ i32(a & 0xFF), 0 }\n"
    "    l := L.{ xs[1], P.{ bump(&xs[1]), 0 } }\n"
    "    return int(l.p.x) * 10000 + int(xs[1].x)\n"
    "}\n"
    "\n"
    "fn inner(a: int, b: int) -> int {\n"
    "    q := Q.{ P.{ i32(a & 0xFF), 0 }, b }\n"
    "    l := L.{ q.p, P.{ bump(&q.p), 0 } }\n"
    "    return int(l.p.x) * 10000 + int(q.p.x)\n"
    "}\n"
    "\n"
    "fn loops(a: int, b: int) -> int {\n"
    "    xs := P.[.{ i32(a & 0xFF), 0 }, .{ 1, 2 }]\n"
    "    sum := 0\n"
    "    for v in xs {\n"
    "        l := L.{ v, P.{ bump(&xs[0]), 0 } }\n"
    "        sum = sum * 1000 + int(l.p.x)\n"
    "    }\n"
    "    return sum + int(xs[0].x) * 1000000\n"
    "}\n"
    "\n"
    "fn params(a: int, b: int) -> int {\n"
    "    return kept(P.{ i32(a & 0xFF), 0 }, b)\n"
    "}\n"
    "\n"
    "fn under(a: int, b: int) -> int {\n"
    "    m := [2]i32.[.[1, 2], .[3, 4]]\n"
    "    k := [2]i32.[.[0, 1], .[0, 0]]\n"
    "    p := P.{ i32(a & 0xFF), 0 }\n"
    "    y := Y.{ .n = m[0][k[0][twice(&k)] & 1], .p = p, .m = int(bump(&p)) }\n"
    "    return int(y.p.x) * 10000 + int(p.x) * 10 + int(y.n)\n"
    "}\n";

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
                int64_t got = ((I2 *)fn(&p, name))(a, b);
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

static const char LARGE_STRUCTS[] =
    "struct W { a: int, b: int, c: int }\n"
    "struct O { n: int, w: W }\n"
    "struct WA { a: [3]int, n: int }\n"
    "struct B17 { a: [17]u8 }\n"
    "struct F5 { a: [5]f32 }\n"
    "struct D5 { a: [5]f64 }\n"
    "struct D2 { x: f64, y: f64 }\n"
    "struct M { n: i32, d: D2, s: []u8 }\n"
    "struct Big { a: [9]int }\n"
    "struct Huge { a: [40]int }\n"
    "\n"
    "fn tW(s: W) -> W {\n"
    "    return W.{ s.c, s.a, s.b }\n"
    "}\n"
    "\n"
    "fn tB17(s: B17) -> B17 {\n"
    "    r := s\n"
    "    for i in 0..17 {\n"
    "        r.a[i] +%= u8(i + 1)\n"
    "    }\n"
    "    return r\n"
    "}\n"
    "\n"
    "fn tF5(s: F5) -> F5 {\n"
    "    r: F5\n"
    "    for i in 0..5 {\n"
    "        r.a[i] = s.a[4 - i] * 2\n"
    "    }\n"
    "    return r\n"
    "}\n"
    "\n"
    "fn tD5(s: D5) -> D5 {\n"
    "    r: D5\n"
    "    for i in 0..5 {\n"
    "        r.a[i] = f64(i) - s.a[i]\n"
    "    }\n"
    "    return r\n"
    "}\n"
    "\n"
    "fn tM(s: M) -> M {\n"
    "    return M.{ s.n + 1, D2.{ s.d.y, s.d.x }, s.s[1..] }\n"
    "}\n"
    "\n"
    "fn tBig(s: Big) -> Big {\n"
    "    r: Big\n"
    "    for i in 0..9 {\n"
    "        r.a[i] = s.a[8 - i] + 1\n"
    "    }\n"
    "    return r\n"
    "}\n"
    "\n"
    "fn tHuge(s: Huge) -> Huge {\n"
    "    r: Huge\n"
    "    for i in 0..40 {\n"
    "        r.a[i] = s.a[39 - i] * 3\n"
    "    }\n"
    "    return r\n"
    "}\n"
    "\n"
    "fn mixL(\n"
    "    a: int, w: W, f: f32, v: F5, b: Big, d: f64, x: D2, k: u8,\n"
    ") -> f64 {\n"
    "    n := a + w.a * 2 + w.b * 3 + w.c * 4\n"
    "    n += b.a[0] * 5 + b.a[8] * 6 + int(k) * 7\n"
    "    r := f64(n) + f64(f) * 8 + f64(v.a[0]) * 9 + f64(v.a[4]) * 10\n"
    "    return r + d * 11 + x.x * 12 + x.y * 13\n"
    "}\n"
    "\n"
    "fn readAfter(s: W, q: *W) -> int {\n"
    "    q.a = 100\n"
    "    return s.a\n"
    "}\n"
    "\n"
    "fn alias(a: int, b: int) -> int {\n"
    "    w := W.{ a & 0xFF, b & 0xFF, 3 }\n"
    "    r := readAfter(w, &w)\n"
    "    return r * 1000 + w.a\n"
    "}\n"
    "\n"
    "fn fieldAlias(a: int, b: int) -> int {\n"
    "    o := O.{ b & 0xFF, W.{ a & 0xFF, 0, 0 } }\n"
    "    r := readAfter(o.w, &o.w)\n"
    "    return r * 1000 + o.w.a + o.n * 1000000\n"
    "}\n"
    "\n"
    "fn readSlice(s: WA, t: []int) -> int {\n"
    "    t[0] = 100\n"
    "    return s.a[0]\n"
    "}\n"
    "\n"
    "fn sliceAlias(a: int, b: int) -> int {\n"
    "    x := WA.{ .[a & 0xFF, 1, 2], b & 0xFF }\n"
    "    r := readSlice(x, x.a[0..2])\n"
    "    return r * 1000 + x.a[0]\n"
    "}\n"
    "\n"
    "fn convertAlias(a: int, b: int) -> int {\n"
    "    x := WA.{ .[a & 0xFF, 1, 2], b & 0xFF }\n"
    "    r := readSlice(x, x.a)\n"
    "    return r * 1000 + x.a[0]\n"
    "}\n"
    "\n"
    "fn derefAlias(a: int, b: int) -> int {\n"
    "    w := W.{ a & 0xFF, b & 0xFF, 3 }\n"
    "    p := &w\n"
    "    r := readAfter(p.*, p)\n"
    "    return r * 1000 + w.a\n"
    "}\n"
    "\n"
    "fn pass(s: W, q: *W) -> int {\n"
    "    return readAfter(s, q) + s.a\n"
    "}\n"
    "\n"
    "fn passAlias(a: int, b: int) -> int {\n"
    "    w := W.{ a & 0xFF, b & 0xFF, 3 }\n"
    "    return pass(w, &w) * 1000 + w.a\n"
    "}\n"
    "\n"
    "fn mk(a: int) -> W {\n"
    "    return W.{ a, a + 1, a + 2 }\n"
    "}\n"
    "\n"
    "fn sum(s: W) -> int {\n"
    "    return s.a + s.b * 10 + s.c * 100\n"
    "}\n"
    "\n"
    "fn results(a: int, b: int) -> int {\n"
    "    x := mk(a & 0xFF)\n"
    "    y := sum(mk(b & 0xFF))\n"
    "    return sum(x) * 100000 + y + mk(a & 7).c * 1000000000\n"
    "}\n"
    "\n"
    "fn rotate(s: W) -> W {\n"
    "    return W.{ s.c, s.a, s.b }\n"
    "}\n"
    "\n"
    "fn selfAssign(a: int, b: int) -> int {\n"
    "    w := W.{ a & 0xFF, b & 0xFF, 7 }\n"
    "    w = rotate(w)\n"
    "    w = rotate(w)\n"
    "    return sum(w)\n"
    "}\n"
    "\n"
    "fn rotatePtr(q: *W) -> W {\n"
    "    return W.{ q.c, q.a, q.b }\n"
    "}\n"
    "\n"
    "fn ptrAssign(a: int, b: int) -> int {\n"
    "    w := W.{ a & 0xFF, b & 0xFF, 7 }\n"
    "    w = rotatePtr(&w)\n"
    "    return sum(w)\n"
    "}\n"
    "\n"
    "fn loopPass(a: int, b: int) -> int {\n"
    "    w := W.{ a & 0xFF, b & 0xFF, 0 }\n"
    "    total := 0\n"
    "    for i in 0..3 {\n"
    "        total += sum(w) * (i + 1)\n"
    "        w.a += 1\n"
    "    }\n"
    "    return total\n"
    "}\n"
    "\n"
    "fn bigChain(a: int, b: int) -> int {\n"
    "    x: Big\n"
    "    for i in 0..9 {\n"
    "        x.a[i] = (a + i) & 0xFF\n"
    "    }\n"
    "    y := tBig(tBig(x))\n"
    "    total := 0\n"
    "    for i in 0..9 {\n"
    "        total = total * 3 + y.a[i]\n"
    "    }\n"
    "    return total + x.a[0] * 1000000000\n"
    "}\n"
    "\n"
    "fn retParam(s: W) -> W {\n"
    "    return s\n"
    "}\n"
    "\n"
    "fn retTwice(a: int, b: int) -> int {\n"
    "    w := W.{ a & 0xFFFF, b & 0xFFFF, 1 }\n"
    "    return sum(retParam(retParam(w)))\n"
    "}\n"
    "\n"
    "fn hugeLoop(a: int, b: int) -> int {\n"
    "    h: Huge\n"
    "    for i in 0..40 {\n"
    "        h.a[i] = (a + i * b) & 0xFFF\n"
    "    }\n"
    "    r := tHuge(h)\n"
    "    total := 0\n"
    "    for i in 0..40 {\n"
    "        total = (total * 31 + r.a[i]) & 0xFFFFFFF\n"
    "    }\n"
    "    return total\n"
    "}\n"
    "\n"
    "fn readHuge(s: Huge, q: *Huge) -> int {\n"
    "    q.a[39] = 5\n"
    "    return s.a[39] + s.a[0]\n"
    "}\n"
    "\n"
    "fn hugeAlias(a: int, b: int) -> int {\n"
    "    h: Huge\n"
    "    h.a[0] = a & 0xFF\n"
    "    h.a[39] = b & 0xFF\n"
    "    r := readHuge(h, &h)\n"
    "    return r * 1000 + h.a[39]\n"
    "}\n"
    "\n"
    "fn readBig(s: Big, q: *Big) -> int {\n"
    "    q.a[8] = 7\n"
    "    return s.a[8] * 10 + s.a[0]\n"
    "}\n"
    "\n"
    "fn bigAlias(a: int, b: int) -> int {\n"
    "    x: Big\n"
    "    x.a[0] = a & 0xFF\n"
    "    x.a[8] = b & 0xFF\n"
    "    p := &x\n"
    "    return readBig(x, p) * 1000 + x.a[8]\n"
    "}\n"
    "\n"
    "fn readB17(s: B17, q: *B17) -> int {\n"
    "    q.a[16] = 9\n"
    "    return int(s.a[16]) * 1000 + int(s.a[0])\n"
    "}\n"
    "\n"
    "fn b17Alias(a: int, b: int) -> int {\n"
    "    x: B17\n"
    "    x.a[0] = u8(a & 0xFF)\n"
    "    x.a[16] = u8(b & 0xFF)\n"
    "    return readB17(x, &x) * 10 + int(x.a[16])\n"
    "}\n"
    "\n"
    "fn keep(s: W, q: *W) -> W {\n"
    "    q.a = 0\n"
    "    return s\n"
    "}\n"
    "\n"
    "fn copyAndReturn(a: int, b: int) -> W {\n"
    "    w := W.{ a, b, 7 }\n"
    "    r := keep(w, &w)\n"
    "    return W.{ r.a, r.b + w.a, r.c }\n"
    "}\n"
    "\n"
    "fn copyReturn(a: int, b: int) -> int {\n"
    "    return sum(copyAndReturn(a & 0xFF, b & 0xFF))\n"
    "}\n"
    "\n"
    "fn three(s: B17, t: W, u: Big) -> int {\n"
    "    n := int(s.a[0]) + int(s.a[16]) * 10 + t.a * 100 + t.c * 1000\n"
    "    return n + u.a[0] * 10000 + u.a[8] * 100000\n"
    "}\n"
    "\n"
    "fn copyThree(a: int, b: int) -> int {\n"
    "    x: B17\n"
    "    x.a[0] = u8(a & 7)\n"
    "    x.a[16] = u8(b & 7)\n"
    "    w := W.{ b & 7, 0, a & 7 }\n"
    "    big: Big\n"
    "    big.a[0] = (a + b) & 7\n"
    "    big.a[8] = 9\n"
    "    p := &x\n"
    "    q := &w\n"
    "    r := &big\n"
    "    return three(p.*, q.*, r.*)\n"
    "}\n"
    "\n"
    "fn spillParam(s: W, k: int) -> int {\n"
    "    a := k + 1\n"
    "    b := k + 2\n"
    "    c := k + 3\n"
    "    d := k + 4\n"
    "    e := k + 5\n"
    "    f := k + 6\n"
    "    g := k + 7\n"
    "    h := k + 8\n"
    "    i := k + 9\n"
    "    j := k + 10\n"
    "    m := k + 11\n"
    "    n := k + 12\n"
    "    total := sum(W.{ k, 1, 2 })\n"
    "    for r in 0..3 {\n"
    "        total += a * b + c * d + e * f + g * h + i * j + m * n + r\n"
    "        a += 1\n"
    "        c += 1\n"
    "        e += 1\n"
    "        g += 1\n"
    "        i += 1\n"
    "        m += 1\n"
    "    }\n"
    "    return total + s.a + s.b * 10\n"
    "}\n"
    "\n"
    "fn callSpill(a: int, b: int) -> int {\n"
    "    return spillParam(W.{ a & 0xFF, b & 0xFF, 0 }, a & 7)\n"
    "}\n"
    "\n"
    "fn bumpW(q: *W) -> int {\n"
    "    q.a += 100\n"
    "    return 1\n"
    "}\n"
    "\n"
    "fn first(s: W, k: int) -> int {\n"
    "    return s.a * 10 + k\n"
    "}\n"
    "\n"
    "fn lateL(a: int, b: int) -> int {\n"
    "    w := W.{ a & 0xFF, b & 0xFF, 0 }\n"
    "    return first(w, bumpW(&w)) * 1000 + w.a\n"
    "}\n"
    "\n"
    "fn callMix(a: int, b: int) -> int {\n"
    "    v: F5\n"
    "    v.a[0] = f32(a & 7)\n"
    "    v.a[4] = 2\n"
    "    big: Big\n"
    "    big.a[0] = b & 0xFF\n"
    "    big.a[8] = 3\n"
    "    x := D2.{ 1, 2 }\n"
    "    k := u8(b & 0x7F)\n"
    "    return int(mixL(a & 0xFF, W.{ 1, 2, 3 }, 0.5, v, big, 0.25, x, k))\n"
    "}\n"
    "\n"
    "fn eW(s: W, k: int) -> W {\n"
    "    return s\n"
    "}\n"
    "\n"
    "fn callEW(a: int, b: int) -> int {\n"
    "    w := W.{ a & 0xFF, b & 0xFF, 5 }\n"
    "    r := eW(w, 3)\n"
    "    return sum(r) * 1000 + sum(w)\n"
    "}\n"
    "\n"
    "fn eBig(s: Big) -> Big {\n"
    "    return s\n"
    "}\n"
    "\n"
    "fn callEBig(a: int, b: int) -> int {\n"
    "    x: Big\n"
    "    for i in 0..9 {\n"
    "        x.a[i] = (a & 0xFF) + i\n"
    "    }\n"
    "    y := eBig(x)\n"
    "    return y.a[0] * 100 + y.a[8] + x.a[8] * 1000000\n"
    "}\n"
    "\n"
    "fn see(s: W) -> int {\n"
    "    return s.a\n"
    "}\n"
    "\n"
    "fn seeWA(s: WA) -> int {\n"
    "    return s.n\n"
    "}\n"
    "\n"
    "fn seeLocal(a: int, b: int) -> int {\n"
    "    w := W.{ a, b, 0 }\n"
    "    _ = see(w)\n"
    "    return w.a\n"
    "}\n"
    "\n"
    "fn seeAddressed(a: int, b: int) -> int {\n"
    "    w := W.{ a, b, 0 }\n"
    "    p := &w\n"
    "    _ = see(w)\n"
    "    return w.a + p.b - b\n"
    "}\n"
    "\n"
    "fn seeLater(a: int, b: int) -> int {\n"
    "    w := W.{ a, b, 0 }\n"
    "    _ = see(w)\n"
    "    r := w.a\n"
    "    p := &w\n"
    "    return r + p.b - b\n"
    "}\n"
    "\n"
    "fn seeParam(s: W) -> int {\n"
    "    _ = see(s)\n"
    "    return s.a\n"
    "}\n"
    "\n"
    "fn seeDeref(p: *W) -> int {\n"
    "    _ = see(p.*)\n"
    "    return p.a\n"
    "}\n"
    "\n"
    "fn seeField(a: int, b: int) -> int {\n"
    "    o := O.{ b, W.{ a, b, 0 } }\n"
    "    _ = see(o.w)\n"
    "    return o.w.a\n"
    "}\n"
    "\n"
    "fn seeElement(a: int, b: int) -> int {\n"
    "    xs: [2]W\n"
    "    xs[1] = W.{ a, b, 0 }\n"
    "    _ = see(xs[1])\n"
    "    return xs[1].a\n"
    "}\n"
    "\n"
    "fn seeLoop(a: int, b: int) -> int {\n"
    "    xs: [2]W\n"
    "    xs[0] = W.{ a, b, 0 }\n"
    "    total := 0\n"
    "    for v in xs {\n"
    "        _ = see(v)\n"
    "        total += v.a\n"
    "    }\n"
    "    return total * 10 + xs[0].a\n"
    "}\n"
    "\n"
    "fn seeSliced(a: int, b: int) -> int {\n"
    "    x := WA.{ .[a, 0, 0], b }\n"
    "    t := x.a[0..1]\n"
    "    _ = seeWA(x)\n"
    "    return x.a[0] + t.len - 1\n"
    "}\n";

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

typedef double MIXL(int64_t, CW, float, CF5, CBig, double, CD2, uint8_t);

static CW
c_eW(const CW *s, int64_t k)
{
    return (CW){ s->a + k, s->b * 2, s->c - k };
}

static CBig
c_eBig(const CBig *s)
{
    CBig r;
    for (int i = 0; i < 9; i++) {
        r.a[i] = s->a[i] * 2;
    }
    return r;
}

static int64_t
c_see(CW *s)
{
    s->a = 999;
    return 0;
}

static int64_t
c_seeWA(CWA *s)
{
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
                int64_t got = ((I2 *)fn(p, names[f]))(a, b);
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

    CW w = ((CW(*)(CW))fn(&p, "tW"))((CW){ 1, -2, 3 });
    REQUIRE(w.a == 3 && w.b == 1 && w.c == -2);
    CB17 b17;
    for (int i = 0; i < 17; i++) {
        b17.a[i] = (uint8_t)(0xF0 + i);
    }
    b17 = ((CB17(*)(CB17))fn(&p, "tB17"))(b17);
    for (int i = 0; i < 17; i++) {
        REQUIRE(b17.a[i] == (uint8_t)(0xF0 + 2 * i + 1));
    }
    CF5 f5 = ((CF5(*)(CF5))fn(&p, "tF5"))((CF5){ { 1, 2, 3, 4, 5 } });
    for (int i = 0; i < 5; i++) {
        REQUIRE(f5.a[i] == (float)(2 * (5 - i)));
    }
    CD5 d5 = ((CD5(*)(CD5))fn(&p, "tD5"))((CD5){ { 1.5, -2, 3, 0.25, 8 } });
    REQUIRE(d5.a[0] == -1.5 && d5.a[1] == 3 && d5.a[2] == -1 && d5.a[3] == 2.75 &&
            d5.a[4] == -4);
    static const uint8_t text[] = "hello";
    CM m = ((CM(*)(CM))fn(&p, "tM"))((CM){ 7, { 1.5, -2.5 }, { text, 5 } });
    REQUIRE(m.n == 8 && m.d.x == -2.5 && m.d.y == 1.5 && m.s.ptr == text + 1 &&
            m.s.len == 4);
    CBig big;
    for (int i = 0; i < 9; i++) {
        big.a[i] = i * 11 - 3;
    }
    big = ((CBig(*)(CBig))fn(&p, "tBig"))(big);
    for (int i = 0; i < 9; i++) {
        REQUIRE(big.a[i] == (8 - i) * 11 - 2);
    }
    CHuge huge;
    for (int i = 0; i < 40; i++) {
        huge.a[i] = i * i - 7;
    }
    huge = ((CHuge(*)(CHuge))fn(&p, "tHuge"))(huge);
    for (int i = 0; i < 40; i++) {
        REQUIRE(huge.a[i] == ((39 - i) * (39 - i) - 7) * 3);
    }
    CBig mb = { { 8, 0, 0, 0, 0, 0, 0, 0, 9 } };
    double mix = ((MIXL *)fn(&p, "mixL"))(
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
    REQUIRE(matches_by_hand(&p, SEE_FUNCTIONS, sees, large_want));
    CW cw = { 5, 6, 7 };
    REQUIRE(((int64_t(*)(CW))fn(&p, "seeParam"))(cw) == 999 && cw.a == 5);
    REQUIRE(((int64_t(*)(CW *))fn(&p, "seeDeref"))(&cw) == 5 && cw.a == 5);
    release_program(&p);
    return true;
}

static const char CONSTANT_CONDITIONS[] = "const DEBUG := false\n"
                                          "fn g() -> bool {\n"
                                          "    return true\n"
                                          "}\n"
                                          "fn f(a: int, b: bool) -> int {\n"
                                          "    if b {\n"
                                          "        return a + 1000\n"
                                          "    }\n"
                                          "    return a\n"
                                          "}\n"
                                          "fn t(x: int) -> int {\n"
                                          "    return f(x, DEBUG and g())\n"
                                          "}\n"
                                          "fn u(x: int) -> int {\n"
                                          "    return f(x, !DEBUG or g())\n"
                                          "}\n"
                                          "fn v(x: int) -> int {\n"
                                          "    if DEBUG {\n"
                                          "        return f(x, g())\n"
                                          "    } else {\n"
                                          "        y := x * 2\n"
                                          "        return y\n"
                                          "    }\n"
                                          "}\n"
                                          "fn w(x: int) -> int {\n"
                                          "    if !DEBUG {\n"
                                          "        return x + 1\n"
                                          "    } else {\n"
                                          "        return f(x, g())\n"
                                          "    }\n"
                                          "}\n";

TEST_CASE(constant_conditions)
{
    Program p;
    REQUIRE(build(&p, CONSTANT_CONDITIONS));
    REQUIRE(((I1 *)fn(&p, "t"))(5) == 5);
    REQUIRE(((I1 *)fn(&p, "u"))(7) == 1007);
    REQUIRE(((I1 *)fn(&p, "v"))(4) == 8);
    REQUIRE(((I1 *)fn(&p, "w"))(4) == 5);
    release_program(&p);
    return true;
}

static const char CONSTANT_TABLES[] =
    "const squares := int.[0, 1, 4, 9, 16, 25, 36, 49]\n"
    "const bytes := u8.[1, 200, 255, 0, 17, 128, 64, 3]\n"
    "const odd := u8.[5, 6, 7]\n"
    "const signed := i8.[-128, -1, 0, 1, 127, -50, 50, 7]\n"
    "const halves := i16.[-30000, -2, 2, 30000]\n"
    "const words := u32.[4000000000, 1, 2, 3]\n"
    "const floats := f64.[0.5, -1.25, 3.0, 100.0]\n"
    "const singles := f32.[1.5, -2.5, 0.25, 8.0]\n"
    "const flags := bool.[true, false, false, true]\n"
    "const colors := Color.[.red, .green, .blue, .green]\n"
    "const grid := [3]int.[.[1, 2, 3], .[4, 5, 6], .[7, 8, 9], .[10, 11, 12]]\n"
    "const same := grid\n"
    "const origin := Point.{ 3, -4 }\n"
    "const corners := Point.[.{ 0, 0 }, .{ 10, 0 }, .{ 10, 10 }, origin]\n"
    "const box := Box.{ .lo = .{ 1, 2 }, .hi = .{ 30, 40 } }\n"
    "const deep := Deep.{ .tag = 9, .a = .[-1, 2, -3] }\n"
    "const wide := Wide.{ 1, 2, 3 }\n"
    "const huge := Huge.{ .head = 5, .tail = 9 }\n"
    "const far := Mega.{ .last = 77 }\n"
    "const after := i16.[-7, 70]\n"
    "const empty := Empty.{}\n"
    "const scale := 3\n"
    "const scaled := int.[scale, scale * scale, -scale]\n"
    "const typed: [3]u8 = .[7, 8, 9]\n"
    "const named: Point = .{ .y = 5, .x = 6 }\n"
    "const nested := [2]Point.[.[.{ 1, 2 }, .{ 3, 4 }], .[.{ 5, 6 }, .{ 7, 8 }]]\n"
    "\n"
    "enum Color: u8 { red = 1, green = 2, blue = 3 }\n"
    "struct Point { x: int, y: int }\n"
    "struct Box { lo: Point, hi: Point }\n"
    "struct Deep { tag: int, w: f32 = 0.5, p: Point, a: [3]i16, k: u8 = 200 }\n"
    "struct Wide { a: int, b: int, c: int }\n"
    "struct Huge { head: int, pad: [1000]int, tail: int }\n"
    "struct Mega { pad: [200000]int, last: int }\n"
    "struct Empty {}\n"
    "\n"
    "fn addP(p: Point, k: int) -> int {\n"
    "    return p.x + p.y * k\n"
    "}\n"
    "\n"
    "fn sumW(w: Wide, k: int) -> int {\n"
    "    return w.a * 100 + w.b * 10 + w.c + k\n"
    "}\n"
    "\n"
    "fn pick(i: int) -> Point {\n"
    "    return corners[i & 3]\n"
    "}\n"
    "\n"
    "fn wideOf() -> Wide {\n"
    "    return wide\n"
    "}\n"
    "\n"
    "fn hugeSum(h: Huge, k: int) -> int {\n"
    "    return h.head + h.tail * k + h.pad[k & 7]\n"
    "}\n"
    "\n"
    "fn square(a: int, b: int) -> int {\n"
    "    return squares[a & 7] + b\n"
    "}\n"
    "\n"
    "fn byteAt(a: int, b: int) -> int {\n"
    "    return int(bytes[a & 7]) * 10 + b\n"
    "}\n"
    "\n"
    "fn oddAt(a: int, b: int) -> int {\n"
    "    return int(odd[(a & 3) % 3]) + b\n"
    "}\n"
    "\n"
    "fn signedAt(a: int, b: int) -> int {\n"
    "    return int(signed[a & 7]) * 10 + b\n"
    "}\n"
    "\n"
    "fn halfAt(a: int, b: int) -> int {\n"
    "    return int(halves[a & 3]) + b\n"
    "}\n"
    "\n"
    "fn wordAt(a: int, b: int) -> int {\n"
    "    return int(words[a & 3]) + b\n"
    "}\n"
    "\n"
    "fn floatAt(a: int, b: int) -> int {\n"
    "    return int(floats[a & 3] * 4.0) + b\n"
    "}\n"
    "\n"
    "fn singleAt(a: int, b: int) -> int {\n"
    "    return int(f64(singles[a & 3]) * 4.0) + b\n"
    "}\n"
    "\n"
    "fn flagAt(a: int, b: int) -> int {\n"
    "    if flags[a & 3] {\n"
    "        return 1000 + b\n"
    "    }\n"
    "    return b\n"
    "}\n"
    "\n"
    "fn colorAt(a: int, b: int) -> int {\n"
    "    c := colors[a & 3]\n"
    "    if c == .red {\n"
    "        return 100 + b\n"
    "    }\n"
    "    if c == .green {\n"
    "        return 200 + b\n"
    "    }\n"
    "    return 300 + b\n"
    "}\n"
    "\n"
    "fn gridAt(a: int, b: int) -> int {\n"
    "    return grid[a & 3][(b & 3) % 3] * 100 + same[(a + 1) & 3][(b & 3) % 3]\n"
    "}\n"
    "\n"
    "fn rowSum(a: int, b: int) -> int {\n"
    "    s := 0\n"
    "    for v in grid[a & 3] {\n"
    "        s += v\n"
    "    }\n"
    "    return s * 10 + b\n"
    "}\n"
    "\n"
    "fn allSum(a: int, b: int) -> int {\n"
    "    s := 0\n"
    "    for row, i in grid {\n"
    "        for v in row {\n"
    "            s += v * (i + 1)\n"
    "        }\n"
    "    }\n"
    "    return s + a + b\n"
    "}\n"
    "\n"
    "fn pointAt(a: int, b: int) -> int {\n"
    "    p := corners[a & 3]\n"
    "    return p.x * 100 + p.y + b\n"
    "}\n"
    "\n"
    "fn originAt(a: int, b: int) -> int {\n"
    "    return origin.x * 1000 + origin.y + a + b\n"
    "}\n"
    "\n"
    "fn boxArea(a: int, b: int) -> int {\n"
    "    return (box.hi.x - box.lo.x) * (box.hi.y - box.lo.y) + a * b\n"
    "}\n"
    "\n"
    "fn deepAt(a: int, b: int) -> int {\n"
    "    return deep.tag * 1000 + int(deep.w * 4.0) * 100 + deep.p.x + int(deep.a[(a & 3) % 3]) * 10 + int(deep.k) + b\n"
    "}\n"
    "\n"
    "fn passPoint(a: int, b: int) -> int {\n"
    "    return addP(corners[a & 3], b)\n"
    "}\n"
    "\n"
    "fn passWide(a: int, b: int) -> int {\n"
    "    return sumW(wide, a) + b\n"
    "}\n"
    "\n"
    "fn retPoint(a: int, b: int) -> int {\n"
    "    p := pick(a)\n"
    "    return p.x * 7 + p.y + b\n"
    "}\n"
    "\n"
    "fn retWide(a: int, b: int) -> int {\n"
    "    w := wideOf()\n"
    "    return w.a + w.b * 10 + w.c * 100 + a + b\n"
    "}\n"
    "\n"
    "fn copyChange(a: int, b: int) -> int {\n"
    "    p := origin\n"
    "    p.x = a\n"
    "    p.y += b\n"
    "    return p.x * 100 + p.y + origin.x * 10000 + origin.y * 1000000\n"
    "}\n"
    "\n"
    "fn arrayCopy(a: int, b: int) -> int {\n"
    "    t := squares\n"
    "    t[a & 7] = b\n"
    "    return t[a & 7] * 1000 + squares[a & 7]\n"
    "}\n"
    "\n"
    "fn hugeAt(a: int, b: int) -> int {\n"
    "    return huge.tail * 10 + huge.head + huge.pad[a & 511] + int(after[a & 1]) + b\n"
    "}\n"
    "\n"
    "fn passHuge(a: int, b: int) -> int {\n"
    "    return hugeSum(huge, a) + b\n"
    "}\n"
    "\n"
    "fn farAt(a: int, b: int) -> int {\n"
    "    return far.last * 1000 + far.pad[a & 0xFFFF] + int(after[a & 1]) + b\n"
    "}\n"
    "\n"
    "fn scaledAt(a: int, b: int) -> int {\n"
    "    return scaled[(a & 3) % 3] * 10 + b\n"
    "}\n"
    "\n"
    "fn annotated(a: int, b: int) -> int {\n"
    "    return int(typed[(a & 3) % 3]) * 100 + named.x * 10 + named.y + b\n"
    "}\n"
    "\n"
    "fn nestedAt(a: int, b: int) -> int {\n"
    "    p := nested[a & 1][b & 1]\n"
    "    return p.x * 10 + p.y\n"
    "}\n"
    "\n"
    "fn lengths(a: int, b: int) -> int {\n"
    "    return squares.len * 100 + grid.len * 10 + grid[a & 3].len + b\n"
    "}\n"
    "\n"
    "fn emptyAt(a: int, b: int) -> int {\n"
    "    e := empty\n"
    "    _ = e\n"
    "    return a - b\n"
    "}\n"
    "\n"
    "fn loopTables(a: int, b: int) -> int {\n"
    "    s := 0\n"
    "    i := 0\n"
    "    while i < 8 {\n"
    "        s += squares[i] * int(bytes[(i + a) & 7]) + int(signed[i]) * b\n"
    "        i += 1\n"
    "    }\n"
    "    return s\n"
    "}\n"
    "\n"
    "fn trapIndex(a: int, b: int) -> int {\n"
    "    return squares[a] + b\n"
    "}\n"
    "\n"
    "fn trapRow(a: int, b: int) -> int {\n"
    "    return grid[a][b]\n"
    "}\n";

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

    CW w = ((CW(*)(void))fn(&p, "wideOf"))();
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

typedef bool B8(bool, bool, bool, bool, bool, bool, bool, bool);

TEST_CASE(deep_logic)
{
    Program p;
    REQUIRE(build(
        &p,
        "fn deep(a: bool, b: bool, c: bool, d: bool, e: bool, f: bool, g: "
        "bool, h: bool) -> bool {\n"
        "    return a and (b or (c and (d or (e and (f or (g and h))))))\n"
        "}\n"
    ));
    B8 *deep = (B8 *)fn(&p, "deep");
    for (int m = 0; m < 256; m++) {
        bool v[8];
        for (int k = 0; k < 8; k++) {
            v[k] = (m >> k) & 1;
        }
        bool want = v[0] &&
                    (v[1] || (v[2] && (v[3] || (v[4] && (v[5] || (v[6] && v[7]))))));
        REQUIRE(deep(v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7]) == want);
    }
    release_program(&p);
    return true;
}

typedef int64_t E4(int64_t, int64_t, int64_t, double);

static int
call_e4(E4 *f, const int64_t *args, double d, int64_t *result)
{
    trap_kind = 0;
    if (sigsetjmp(trap_jump, 1) == 0) {
        *result = f(args[0], args[1], args[2], d);
        return 0;
    }
    return trap_kind;
}

static int64_t
random_argument(u64 *rng)
{
    static const int64_t edges[] = {
        0,         1,          -1,        127,       128,     -128,   -129,
        255,       256,        32767,     -32768,    65535,   65536,  INT32_MAX,
        INT32_MIN, UINT32_MAX, INT64_MAX, INT64_MIN, 1000003, -999983
    };
    int pick = random_below(rng, 10);
    if (pick < 6) {
        return random_below(rng, 41) - 20;
    }
    if (pick < 8) {
        return random_below(rng, 2001) - 1000;
    }
    return edges[random_below(rng, sizeof edges / sizeof edges[0])];
}

static double
random_float_argument(u64 *rng)
{
    static const double edges[] = { 0.0,   -0.0, 0.5,   -2.75, 1e18,
                                    -1e19, 3e9,  1e300, NAN,   INFINITY };
    if (random_below(rng, 4) == 0) {
        return edges[random_below(rng, sizeof edges / sizeof edges[0])];
    }
    return (random_below(rng, 4001) - 2000) / 8.0;
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

        IrReference ref;
        ir_reference_init(&ref, &p.c);
        for (u32 e = 0; e < PROGRAM_ENTRIES; e++) {
            char name[8];
            snprintf(name, sizeof name, "e%u", e);
            u32 decl = decl_named(&p, name);
            E4 *f = (E4 *)native_entry(&p.n, decl);
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
                        "differential: seed %llu, %s(%lld, %lld, %lld, %g): "
                        "reference %lld "
                        "(trap %d), native %lld (trap %d)\n%s",
                        (unsigned long long)seed,
                        name,
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
                    release_program(&p);
                    free(text);
                    REQUIRE(false);
                }
            }
        }
        ir_reference_free(&ref);
        release_program(&p);
    }
    free(text);
    return true;
}

static const char SWITCHES
    [] = "enum Dir { north, east, south, west }\n"
         "enum Sparse { a = 1, b = 5, c = 9, d = 200, e = 1000, f = -7 }\n"
         "enum Wide: int { low = -2, a, b, c, far = 30 }\n"
         "fn s0(a: int, b: int) -> int {\n"
         "    switch a {\n"
         "        0 => return 10\n"
         "        1, 2 => return 20\n"
         "        5..=9 => return 30\n"
         "        _ => return b\n"
         "    }\n"
         "}\n"
         "fn s1(a: int, b: int) -> int {\n"
         "    x := switch a {\n"
         "        0 => 1\n"
         "        1 => 2\n"
         "        2 => 3\n"
         "        4 => 5\n"
         "        5..=7 => b\n"
         "        9 => 11\n"
         "        _ => -1\n"
         "    }\n"
         "    return x * 2\n"
         "}\n"
         "fn s2(a: int, b: int) -> int {\n"
         "    switch a {\n"
         "        -1000 => return 1\n"
         "        -5 => return 2\n"
         "        3 => return 3\n"
         "        100..=200 => return 4\n"
         "        1000 => return 5\n"
         "        5000 => return 6\n"
         "        70000 => return 7\n"
         "        1 << 40 => return 8\n"
         "        _ => return b\n"
         "    }\n"
         "}\n"
         "fn s3(a: int, b: int) -> int {\n"
         "    x: i8 = i8(a & 0x7F) - 64\n"
         "    switch x {\n"
         "        -128..=-60 => return 1\n"
         "        -3..=3 => return 2\n"
         "        10, 20, 30, 40, 50 => return 3\n"
         "        _ => return int(x) + b\n"
         "    }\n"
         "}\n"
         "fn s4(a: int, b: int) -> int {\n"
         "    d := Dir(a & 3)\n"
         "    return switch d {\n"
         "        .north => 1\n"
         "        .east => 2\n"
         "        .south => b\n"
         "        .west => 4\n"
         "    }\n"
         "}\n"
         "fn s5(a: int, b: int) -> int {\n"
         "    s := Sparse(a)\n"
         "    v := switch s {\n"
         "        .a, .b => 1\n"
         "        .c => 2\n"
         "        _ => int(s)\n"
         "    }\n"
         "    return v + b\n"
         "}\n"
         "fn s6(a: int, b: int) -> int {\n"
         "    total := 0\n"
         "    i := 0\n"
         "    while i < 10 {\n"
         "        sw: switch (a + i) % 4 {\n"
         "            0 => total += 1\n"
         "            1 => {\n"
         "                if b > 0 {\n"
         "                    break sw\n"
         "                }\n"
         "                total += 100\n"
         "            }\n"
         "            2 => break\n"
         "            _ => {}\n"
         "        }\n"
         "        i += 1\n"
         "    }\n"
         "    return total\n"
         "}\n"
         "fn s7(a: int, b: int) -> int {\n"
         "    c := a > b\n"
         "    n := switch c { true => 1, false => 2 }\n"
         "    d := Dir(b & 3)\n"
         "    return n * 10 + int(u8(d)) + int(i8(Sparse.f))\n"
         "}\n"
         "fn s8(a: int, b: int) -> int {\n"
         "    x: u8 = u8(a & 0xFF)\n"
         "    y: i16 = i16(b & 0x7FF)\n"
         "    v := switch a & 3 {\n"
         "        0 => x\n"
         "        1 => y\n"
         "        2 => 7\n"
         "        _ => i32(a & 0xFFFF)\n"
         "    }\n"
         "    return int(v)\n"
         "}\n"
         "fn s9(a: int, b: int) -> int {\n"
         "    x := u8(a & 0xFF)\n"
         "    switch x {\n"
         "        0..=99 => return 1 + b\n"
         "        100..=199 => return 2\n"
         "        200..=255 => return 3\n"
         "    }\n"
         "}\n"
         "fn s10(a: int, b: int) -> int {\n"
         "    x := u64(a & 0x7FFFFFFFFFFFFFFF) | 0x8000000000000000\n"
         "    switch x {\n"
         "        0x8000000000000000 => return 1\n"
         "        0x8000000000000001..=0x80000000000000FF => return 2\n"
         "        0xFFFFFFFFFFFFFFFF => return 3\n"
         "        _ => return 4 + b\n"
         "    }\n"
         "}\n"
         "fn s12(a: int, b: int) -> int {\n"
         "    v: i16 = i16((a & 63)) - 32\n"
         "    return int(Wide(v)) + b\n"
         "}\n"
         "fn s11(a: int, b: int) -> int {\n"
         "    d := Sparse(i16(a))\n"
         "    return int(d) + b\n"
         "}\n";

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
        "}\n"
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
    REQUIRE(arm64_compile(&p.n, &p.c));
    REQUIRE(((I1 *)fn(&p, "f"))(7) == 75);
    release_program(&p);
    return true;
}

typedef uint64_t U1(uint64_t);

static int
call_u1(U1 *f, uint64_t a, uint64_t *result)
{
    trap_kind = 0;
    if (sigsetjmp(trap_jump, 1) == 0) {
        *result = f(a);
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

typedef int64_t W1(uint64_t);

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
    REQUIRE(((W1 *)fn(&p, "t"))(0x100000009ull) == 3);
    REQUIRE(((W1 *)fn(&p, "t_store"))(0x100000009ull) == 9);
    REQUIRE(((W1 *)fn(&p, "t_call"))(0x100000009ull) == 4);
    REQUIRE(((W1 *)fn(&p, "t8"))(0xFFFFFF0Full) == 5);
    REQUIRE(((W1 *)fn(&p, "ti8"))(0x1F4ull) == -4);
    REQUIRE(((W1 *)fn(&p, "tb"))(0x100ull) == 0);
    release_program(&p);
    return true;
}

TEST_SUITE(arm64_tests)
{
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
    RUN_TEST_CASE(left_to_right);
    RUN_TEST_CASE(large_structs);
    RUN_TEST_CASE(constant_conditions);
    RUN_TEST_CASE(constant_tables);
    RUN_TEST_CASE(deep_logic);
    RUN_TEST_CASE(small_integers);
    RUN_TEST_CASE(wide_integers);
    RUN_TEST_CASE(control_flow);
    RUN_TEST_CASE(switches);
}

#else

TEST_SUITE(arm64_tests) {}

#endif
