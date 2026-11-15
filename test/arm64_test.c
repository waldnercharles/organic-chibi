#define _DEFAULT_SOURCE

#include "../src/arm64.h"
#include "../vendor/pico_unit/pico_unit.h"

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
build(Program *p, const char *text)
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

TEST_SUITE(arm64_tests)
{
    RUN_TEST_CASE(small_integers);
    RUN_TEST_CASE(wide_integers);
    RUN_TEST_CASE(control_flow);
}

#else

TEST_SUITE(arm64_tests) {}

#endif
