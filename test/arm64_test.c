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

    IrReference ref;
    ir_reference_init(&ref, &p.c);
    for (size_t f = 0; f < sizeof SLICE_FUNCTIONS / sizeof SLICE_FUNCTIONS[0]; f++) {
        u32 decl = decl_named(&p, SLICE_FUNCTIONS[f]);
        I2 *native = (I2 *)native_entry(&p.n, decl);
        for (int64_t a = -3; a <= 14; a++) {
            for (int64_t b = -3; b <= 12; b++) {
                int64_t x = a < 14 ? a : INT64_MIN;
                u64 slots[2] = { (u64)x, (u64)b };
                u64 want = 0;
                int want_trap = ir_reference_run(&ref, decl, slots, &want);
                int got_trap = call_i2(native, x, b, &r);
                if (want_trap != got_trap || (!want_trap && (int64_t)want != r)) {
                    fprintf(
                        stderr,
                        "slices: %s(%lld, %lld): reference %lld (trap %d), "
                        "native %lld (trap %d)\n",
                        SLICE_FUNCTIONS[f],
                        (long long)x,
                        (long long)b,
                        (long long)want,
                        want_trap,
                        (long long)r,
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
    RUN_TEST_CASE(constant_conditions);
    RUN_TEST_CASE(deep_logic);
    RUN_TEST_CASE(small_integers);
    RUN_TEST_CASE(wide_integers);
    RUN_TEST_CASE(control_flow);
    RUN_TEST_CASE(switches);
}

#else

TEST_SUITE(arm64_tests) {}

#endif
