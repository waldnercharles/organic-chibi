#define _GNU_SOURCE

#include "../../src/jit.h"
#include "../../src/lexer.h"
#include "../../src/parser.h"
#include "../../test/backend_programs.h"
#include "../../test/ir_reference.h"

#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>

typedef struct
{
    Arena arena;
    Lexer lx;
    ParsedFile file;
    Checker c;
    Native n;
} Program;

typedef int64_t I2(void *, int64_t, int64_t);

static uint8_t CONTEXT[16];
static sigjmp_buf trap_jump;
static volatile sig_atomic_t trap_kind;
static u32 agreed, refused, failed, skipped;

static const char *const X64_PROGRAMS[] = {
    "fn many_locals(a: int, b: int) -> int {\n"
    "    x1 := a + 1\n"
    "    x2 := b + 2\n"
    "    x3 := a - b\n"
    "    x4 := a + b\n"
    "    x5 := 7\n"
    "    x6 := 0\n"
    "    x7 := 0\n"
    "    for i in 0..b {\n"
    "        x6 += x1 + i\n"
    "        x7 += x2 * i\n"
    "        x5 = x5 + x3 - x4\n"
    "    }\n"
    "    return x1 + x2 + x3 + x4 + x5 + x6 + x7\n"
    "}\n",

    "fn mix(a: int, b: int, c: int, d: int, e: int, f: int, g: int, h: int) -> "
    "int {\n"
    "    return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f + 7 * g + 8 * h\n"
    "}\n"
    "fn many_args(a: int, b: int) -> int {\n"
    "    return mix(a, b, a + b, a - b, 1, 2, b, a) + mix(b, a, 3, 4, 5, 6, 7, "
    "8)\n"
    "}\n"
    "fn loop_calls(a: int, b: int) -> int {\n"
    "    total := 0\n"
    "    for i in 0..b {\n"
    "        total += mix(i, a, b, i, 1, 1, 1, i)\n"
    "    }\n"
    "    return total\n"
    "}\n"
    "fn live_across(a: int, b: int) -> int {\n"
    "    x := a * 3\n"
    "    y := b * 5\n"
    "    z := mix(a, b, 1, 2, 3, 4, 5, 6)\n"
    "    w := mix(b, a, x, y, z, 1, 2, 3)\n"
    "    return x + y + z + w\n"
    "}\n",

    "fn depth(n: int) -> int {\n"
    "    if n <= 0 {\n"
    "        return 0\n"
    "    }\n"
    "    return 1 + depth(n - 1)\n"
    "}\n"
    "fn fib(n: int) -> int {\n"
    "    if n < 2 {\n"
    "        return n\n"
    "    }\n"
    "    return fib(n - 1) + fib(n - 2)\n"
    "}\n"
    "fn recurse(a: int, b: int) -> int {\n"
    "    return depth(b) * 100 + depth(a) + fib(b) * 10000 + fib(a & 15)\n"
    "}\n",

    "fn small_math(a: int, b: int) -> int {\n"
    "    x: i16 = i16(a * 1000)\n"
    "    y: i16 = i16(b * 1000)\n"
    "    z: u8 = u8(b & 255)\n"
    "    w: i32 = i32(a) * i32(b) * 100000\n"
    "    return int(x + y) + int(z *% 3) + int(w)\n"
    "}\n"
    "fn small_wrap(a: int, b: int) -> int {\n"
    "    x: u8 = u8(a & 255)\n"
    "    y: u8 = u8(b & 255)\n"
    "    p: i8 = i8(a & 127) -% i8(b & 127)\n"
    "    return int(x -% y) * 1000 + int(p) + int(x *% y)\n"
    "}\n"
    "fn unsigned32(a: int, b: int) -> int {\n"
    "    x: u32 = u32(a & 0xFFFF)\n"
    "    y: u32 = u32(b & 0xFFFF)\n"
    "    return int(x * y) + int(x - y)\n"
    "}\n"
    "fn u32_mul(a: int, b: int) -> int {\n"
    "    x: u32 = u32(a & 0xFFFFF) * 4096\n"
    "    y: u32 = u32(b & 0xFFF)\n"
    "    return int(x * y)\n"
    "}\n"
    "fn u64_mul(a: int, b: int) -> int {\n"
    "    x: u64 = u64(a & 0xFFFFFFFFFF) * 0x1000000\n"
    "    y: u64 = u64(b & 0xFFFF) * 0x10000000\n"
    "    z := x * y\n"
    "    live := u64(a & 7) + u64(b & 7)\n"
    "    return int(z & 0x7FFFFFFFFFFFFFFF) + int(live * u64(b & 3))\n"
    "}\n"
    "fn mul_pressure(a: int, b: int) -> int {\n"
    "    p := u64(a & 15) + 1\n"
    "    q := u64(b & 15) + 2\n"
    "    r := u64(a & 3) + 3\n"
    "    s := u64(b & 3) + 4\n"
    "    t := (p + q) + (r * s) * (p * q + (q * s) * (r + (p * s)))\n"
    "    return int(t + p * q * r * s)\n"
    "}\n"
    "fn narrowing(a: int, b: int) -> int {\n"
    "    x: i8 = i8(a * 10)\n"
    "    y: u16 = u16(b * 6000)\n"
    "    z: i32 = i32(a * 200000000)\n"
    "    return int(x) + int(y) + int(z)\n"
    "}\n",

    "fn compares(a: int, b: int) -> int {\n"
    "    n := 0\n"
    "    if a < b {\n"
    "        n += 1\n"
    "    }\n"
    "    if a <= b {\n"
    "        n += 2\n"
    "    }\n"
    "    if a > b {\n"
    "        n += 4\n"
    "    }\n"
    "    if a >= b {\n"
    "        n += 8\n"
    "    }\n"
    "    if a == b {\n"
    "        n += 16\n"
    "    }\n"
    "    if a != b {\n"
    "        n += 32\n"
    "    }\n"
    "    p := a < b\n"
    "    q := u32(b & 7) > u32(a & 7)\n"
    "    if p == q {\n"
    "        n += 64\n"
    "    }\n"
    "    if a & 4 == 0 {\n"
    "        n += 128\n"
    "    }\n"
    "    return n\n"
    "}\n"
    "fn value_if(a: int, b: int) -> int {\n"
    "    x := if a > b then a - b else b - a\n"
    "    y := if a == 0 then 100 else if b == 0 then 200 else 300\n"
    "    return x * 1000 + y\n"
    "}\n",

    "fn deep(a: int, b: int) -> int {\n"
    "    return ((a + 1) * (b + 2) + (a + 3) * (b + 4)) * ((a + 5) * (b + 6) + "
    "(a + 7) * (b + 8)) +\n"
    "           ((a - 1) * (b - 2) + (a - 3) * (b - 4)) * ((a - 5) * (b - 6) - "
    "(a - 7) * (b - 8)) +\n"
    "           (((a + b) * (a - b) + (a + 2) * (b + 3)) * ((a + 4) - (b + 5) "
    "* (a + 6)))\n"
    "}\n"
    "fn big_constants(a: int, b: int) -> int {\n"
    "    x := a + 0x123456789\n"
    "    y := b * 0x10000000\n"
    "    z := a & 0xFFFFFFFF00\n"
    "    return x + y - z - 0x7FFFFFFFFFFF\n"
    "}\n",

    "fn pointers(a: int, b: int) -> int {\n"
    "    x := a\n"
    "    y := b\n"
    "    p := &x\n"
    "    q := &y\n"
    "    p.* = p.* + q.*\n"
    "    q.* = 3\n"
    "    return x * 10 + y\n"
    "}\n"
    "fn nested(a: int, b: int) -> int {\n"
    "    total := 0\n"
    "    outer: for i in 0..b {\n"
    "        for j in 0..a {\n"
    "            if j > i {\n"
    "                continue outer\n"
    "            }\n"
    "            if i + j > 15 {\n"
    "                break outer\n"
    "            }\n"
    "            total += i * j\n"
    "        }\n"
    "    }\n"
    "    return total\n"
    "}\n",
};

static void
on_trap(int sig, siginfo_t *info, void *context)
{
    (void)info;
    ucontext_t *uc = context;
    const u8 *pc = (const u8 *)(uintptr_t)uc->uc_mcontext.gregs[REG_RIP];
    bool ud1 = sig == SIGILL && pc[0] == 0x0F && pc[1] == 0xB9 && pc[2] == 0x40;
    trap_kind = ud1 ? pc[3] : 100 + sig;
    siglongjmp(trap_jump, 1);
}

static void
catch_traps(void)
{
    struct sigaction sa = { 0 };
    sa.sa_sigaction = on_trap;
    sa.sa_flags = SA_SIGINFO | SA_NODEFER;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGILL, &sa, NULL);
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
}

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
    if (ntokens <= 0) {
        return false;
    }
    Nodes nodes = parser_alloc_nodes(&p->arena, ntokens);
    Parser parser;
    int count = parser_parse(&parser, tokens, ntokens, nodes);
    if (count < 0 || parser.error) {
        return false;
    }
    p->file = (ParsedFile){ "a",   src,        tokens,
                            nodes, (u32)count, (u32)parser.top_count,
                            false };
    check_init(&p->c, &p->lx.names, &p->file, 1);
    check_package(&p->c);
    return p->c.ndiags == 0;
}

static void
release_program(Program *p)
{
    native_free(&p->n);
    check_free(&p->c);
    arena_free(&p->arena);
}

static bool
takes_two_ints(const Checker *c, const IrFunc *fn)
{
    const IrSlot *slots = c->ir.code.slots + fn->first_slot;
    u32 ret = (u32)c->types.arg1[c->decls.types[fn->decl]];
    return fn->nparams == 2 && slots[0].type == TYPE_ID_I64 &&
           slots[1].type == TYPE_ID_I64 && ret == TYPE_ID_I64;
}

static bool
agree_on_grid(Program *p, const char *label, bool verbose)
{
    IrReference ref;
    ir_reference_init(&ref, &p->c);
    bool ok = true;
    u32 tested = 0;
    for (u32 f = 0; ok && f < p->c.ir.nfuncs; f++) {
        const IrFunc *fn = &p->c.ir.funcs[f];
        if (!takes_two_ints(&p->c, fn)) {
            continue;
        }
        I2 *native = (I2 *)native_entry(&p->n, fn->decl);
        tested++;
        for (int64_t a = -3; ok && a <= 14; a++) {
            for (int64_t x = -3; ok && x <= 12; x++) {
                int64_t y = a < 14 ? a : INT64_MIN;
                u64 slots[2] = { (u64)y, (u64)x };
                u64 want = 0;
                int64_t got = 0;
                int want_trap = ir_reference_run(&ref, fn->decl, slots, &want);
                int got_trap = call_i2(native, y, x, &got);
                ok = want_trap == got_trap && (want_trap || (int64_t)want == got);
                if (!ok) {
                    Name n = names_get(&p->lx.names, p->c.decls.names[fn->decl]);
                    printf(
                        "FAIL %s: %.*s(%lld, %lld): reference %lld (trap %d), "
                        "x86-64 %lld (trap %d)\n",
                        label,
                        (int)n.len,
                        n.text,
                        (long long)y,
                        (long long)x,
                        (long long)want,
                        want_trap,
                        (long long)got,
                        got_trap
                    );
                }
            }
        }
    }
    ir_reference_free(&ref);
    if (ok && verbose) {
        printf("ok   %s: %u functions\n", label, tested);
    }
    agreed += ok ? tested : 0;
    failed += !ok;
    return ok;
}

static bool
run_text(const char *text, const char *label, bool verbose)
{
    Program p;
    if (!check_text(&p, text)) {
        release_program(&p);
        skipped++;
        return false;
    }
    if (!jit_compile(&p.n, &p.c, CONVENTION_HOST, 0)) {
        if (verbose) {
            printf("no   %s: %s\n", label, p.n.error);
        }
        release_program(&p);
        return false;
    }
    agree_on_grid(&p, label, verbose);
    release_program(&p);
    return true;
}

static void
run_functions(const char *text, const char *label)
{
    size_t len = strlen(text);
    char *one = malloc(2 * len + 1);
    const char *first = strncmp(text, "fn ", 3) == 0 ? text : strstr(text, "\nfn ");
    size_t prelude = first ? (size_t)(first - text) + (first != text) : 0;
    memcpy(one, text, prelude);
    const char *at = text + prelude;
    u32 k = 0;
    while ((at = strstr(at, "fn ")) != NULL) {
        if (at != text && at[-1] != '\n') {
            at += 3;
            continue;
        }
        const char *end = strstr(at, "\n}\n");
        if (!end) {
            break;
        }
        end += 3;
        memcpy(one + prelude, at, (size_t)(end - at));
        one[prelude + (size_t)(end - at)] = 0;
        char name[128];
        snprintf(name, sizeof name, "%s.%u", label, k++);
        Program p;
        if (check_text(&p, one)) {
            if (jit_compile(&p.n, &p.c, CONVENTION_HOST, 0)) {
                agree_on_grid(&p, name, false);
            } else {
                refused++;
            }
        }
        release_program(&p);
        at = end;
    }
    free(one);
}

int
main(int argc, char **argv)
{
    bool verbose = argc > 1 && strcmp(argv[1], "-v") == 0;
    catch_traps();
    for (u32 k = 0; k < NBACKEND_PROGRAMS; k++) {
        char label[32];
        snprintf(label, sizeof label, "program %u", k);
        if (!run_text(BACKEND_PROGRAMS[k], label, verbose)) {
            run_functions(BACKEND_PROGRAMS[k], label);
        }
    }
    for (u32 k = 0; k < sizeof X64_PROGRAMS / sizeof X64_PROGRAMS[0]; k++) {
        char label[32];
        snprintf(label, sizeof label, "x64 program %u", k);
        if (!run_text(X64_PROGRAMS[k], label, true)) {
            failed++;
        }
    }
    printf(
        "x64 check: %u functions agree with the reference, %u failed, "
        "%u refused, %u skipped\n",
        agreed,
        failed,
        refused,
        skipped
    );
    return failed != 0;
}
