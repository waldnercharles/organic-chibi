#define _DEFAULT_SOURCE

#include "../src/c_source.h"
#include "../vendor/pico_unit/pico_unit.h"
#include "backend_programs.h"
#include "ir_reference.h"
#include "random/program.h"

#include <assert.h>
#include <dlfcn.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct
{
    Arena arena;
    Lexer lx;
    ParsedFile file;
    Checker c;
} Package;

static const char *const UNSUPPORTED[][2] = {
    { "struct V { v: u32x4, n: int }\n"
      "fn f(a: int) -> int {\n"
      "    x: V\n"
      "    x.n = a\n"
      "    return x.n\n"
      "}\n",
      "the C backend doesn't support vectors yet" },
    { "fn f(a: u32x4) -> int {\n"
      "    return 1\n"
      "}\n",
      "the C backend doesn't support vectors yet" },
    { "fn f() -> int {\n"
      "    return int(top_bits(u32x4.{ 1, 2, 3, 4 } == u32x4.{ 1, 0, 3, 0 }))\n"
      "}\n",
      "the C backend doesn't support vectors yet" },
    { "export fn half(x: f64) -> f64 {\n"
      "    return x * 0.5\n"
      "}\n",
      "the C backend doesn't support `export fn` yet" },
    { "extern fn labs(x: i64) -> i64\n"
      "fn f(a: int) -> int {\n"
      "    return labs(a)\n"
      "}\n",
      "the C backend doesn't support calls into C yet" },
    { "fn twice(x: int) -> int {\n"
      "    return x * 2\n"
      "}\n"
      "fn get() -> extern fn(x: int) -> int {\n"
      "    return twice\n"
      "}\n",
      "the C backend doesn't support callbacks yet" },
};

static bool
check_source(Package *p, const char *text)
{
    *p = (Package){ 0 };
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
        fprintf(stderr, "c_source test: the program does not parse\n%s", text);
        return false;
    }

    p->file = (ParsedFile){ "a",   src,        tokens,
                            nodes, (u32)count, (u32)parser.top_count,
                            false };
    check_init(&p->c, &p->lx.names);
    check_package(&p->c, &p->file, 1);
    if (p->c.ndiags) {
        check_print_diagnostics(stderr, &p->c);
        return false;
    }
    return true;
}

static void
release(Package *p)
{
    check_free(&p->c);
    arena_free(&p->arena);
}

TEST_CASE(reports_unsupported)
{
    FILE *out = fopen("/dev/null", "w");
    assert(out);

    bool ok = true;
    for (u32 k = 0; ok && k < sizeof UNSUPPORTED / sizeof UNSUPPORTED[0]; k++) {
        Package p;
        ok = check_source(&p, UNSUPPORTED[k][0]);
        const char *error = ok ? c_source_package(out, &p.c, "u_") : NULL;
        release(&p);
        if (ok && (!error || strcmp(error, UNSUPPORTED[k][1]) != 0)) {
            fprintf(stderr, "c_source test: expected \"%s\", got \"%s\"\n%s", UNSUPPORTED[k][1], error ? error : "no error", UNSUPPORTED[k][0]);
            ok = false;
        }
    }
    fclose(out);
    REQUIRE(ok);
    return true;
}

TEST_CASE(emits_doubling_structs)
{
    enum
    {
        LEVELS = 64,
    };
    char text[LEVELS * 48 + 128];
    int n = 0;
    for (int k = 0; k < LEVELS; k++) {
        n += snprintf(text + n, sizeof text - (size_t)n, "struct S%d { a: S%d, b: S%d }\n", k, k + 1, k + 1);
    }
    snprintf(text + n, sizeof text - (size_t)n, "struct S%d {}\nfn f() -> int {\n    x: S0\n    y := x\n    return 1\n}\n", LEVELS);

    FILE *out = fopen("/dev/null", "w");
    assert(out);
    Package p;
    bool ok = check_source(&p, text) && c_source_package(out, &p.c, "d_") == NULL;
    release(&p);
    fclose(out);
    REQUIRE(ok);
    return true;
}

#ifdef CHIBI_TEST_CC

#if defined(__APPLE__) && defined(__aarch64__)
#define TARGET " -arch arm64"
#elif defined(__APPLE__)
#define TARGET " -arch x86_64"
#else
#define TARGET ""
#endif

#define FLAGS                                                                  \
    "-std=c11 -O2 -fPIC -Wall -Wextra -Werror -Wno-unused-parameter "          \
    "-Wno-unused-variable -Wno-unused-but-set-variable -Wno-type-limits "      \
    "-fsanitize=undefined -fno-sanitize-recover=all -DCHIBI_FN="

enum
{
    MAX_JOBS = 8,
    GRID_FUNCTIONS = 229,
};

typedef struct
{
    char dir[32];
    u32 jobs;
    FILE *outs[MAX_JOBS];
    void *library;
} Build;

typedef int64_t I2(void *, int64_t, int64_t);
typedef int64_t E4(void *, int64_t, int64_t, int64_t, double);

static const char *const C_PROGRAMS[] = {
    "struct Deep { lines: [2]Line, n: u8 = 3 }\n"
    "struct Line { a: Point, b: Point }\n"
    "struct Point { x: int, y: int = 7 }\n"
    "\n"
    "fn order(a: int, b: int) -> int {\n"
    "    d: Deep\n"
    "    d.lines[1].b.x = a\n"
    "    d.n = u8(b & 255)\n"
    "    e := d\n"
    "    return e.lines[1].b.x + e.lines[0].a.y * 10 + int(e.n) * 100\n"
    "}\n",
    "fn addresses(a: int, b: int) -> int {\n"
    "    x := uintptr(a)\n"
    "    y := uintptr(b & 15)\n"
    "    if x < y {\n"
    "        return int(y - x)\n"
    "    }\n"
    "    return int(x * y + (x >> 2) - y)\n"
    "}\n",
    "const QUESTIONS := u8.[63, 63, 61, 63, 63, 47, 63, 63, 40]\n"
    "\n"
    "fn questions(a: int, b: int) -> int {\n"
    "    return int(QUESTIONS[a & 7]) * 1000 + int(QUESTIONS[(b & 7) + 1])\n"
    "}\n",
    "fn empty(a: int, b: int) -> int {\n"
    "    s: []int\n"
    "    t := s[0..0]\n"
    "    u := s[b - b..]\n"
    "    if s.ptr == t.ptr {\n"
    "        return a + t.len + u.len\n"
    "    }\n"
    "    return b\n"
    "}\n",
};

static uint8_t CONTEXT[16];
static jmp_buf trap_jump;
static volatile int trap_kind;

NORETURN void
chibi_trap(int kind)
{
    trap_kind = kind;
    longjmp(trap_jump, 1);
}

static const char *
grid_program(u32 k)
{
    return k < NBACKEND_PROGRAMS ? BACKEND_PROGRAMS[k] : C_PROGRAMS[k - NBACKEND_PROGRAMS];
}

static bool
open_build(Build *b)
{
    *b = (Build){ .dir = "/tmp/chibi-c-XXXXXX" };
    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    b->jobs = cpus < 1 ? 1 : cpus > MAX_JOBS ? MAX_JOBS : (u32)cpus;
    if (!mkdtemp(b->dir)) {
        return false;
    }
    for (u32 j = 0; j < b->jobs; j++) {
        char path[64];
        snprintf(path, sizeof path, "%s/p%u.c", b->dir, j);
        b->outs[j] = fopen(path, "w");
        assert(b->outs[j]);
        c_source_prelude(b->outs[j]);
    }
    return true;
}

static bool
add_package(Build *b, u32 k, const char *text, char tag)
{
    Package p;
    bool ok = check_source(&p, text);
    if (ok) {
        char prefix[32];
        snprintf(prefix, sizeof prefix, "%c%u_", tag, k);
        const char *error = c_source_package(b->outs[k % b->jobs], &p.c, prefix);
        if (error) {
            fprintf(stderr, "c_source test: %s\n%s", error, text);
            ok = false;
        }
    }
    release(&p);
    return ok;
}

static bool
finish_build(Build *b)
{
    for (u32 j = 0; j < b->jobs; j++) {
        fclose(b->outs[j]);
    }

    char command[4096];
    int n = snprintf(command, sizeof command, "cd %s && printf '%%s\\n'", b->dir);
    for (u32 j = 0; j < b->jobs; j++) {
        n += snprintf(command + n, sizeof command - (size_t)n, " p%u", j);
    }
    snprintf(command + n, sizeof command - (size_t)n, " | xargs -P %u -I{} " CHIBI_TEST_CC TARGET " " FLAGS " -c -o {}.o {}.c && " CHIBI_TEST_CC TARGET " -shared -fsanitize=undefined -o programs.so p*.o", b->jobs);
    if (system(command) != 0) {
        fprintf(stderr, "c_source test: `%s` failed\n", command);
        return false;
    }

    char path[64];
    snprintf(path, sizeof path, "%s/programs.so", b->dir);
    b->library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!b->library) {
        fprintf(stderr, "c_source test: %s\n", dlerror());
    }
    return b->library != NULL;
}

static void
close_build(Build *b, bool ok)
{
    if (b->library) {
        dlclose(b->library);
    }
    if (!ok) {
        fprintf(stderr, "c_source test: the generated C is in %s\n", b->dir);
        return;
    }

    char path[64];
    for (u32 j = 0; j < b->jobs; j++) {
        snprintf(path, sizeof path, "%s/p%u.c", b->dir, j);
        unlink(path);
        snprintf(path, sizeof path, "%s/p%u.o", b->dir, j);
        unlink(path);
    }
    snprintf(path, sizeof path, "%s/programs.so", b->dir);
    unlink(path);
    rmdir(b->dir);
}

static void *
lookup(const Build *b, const Package *p, u32 decl, char tag, u32 k)
{
    Name n = names_get(&p->lx.names, p->c.decls.names[decl]);
    char name[64];
    snprintf(name, sizeof name, "%c%u_%.*s", tag, k, (int)n.len, n.text);
    void *f = dlsym(b->library, name);
    assert(f);
    return f;
}

static int
call_i2(I2 *f, int64_t a, int64_t b, int64_t *result)
{
    trap_kind = 0;
    if (setjmp(trap_jump) == 0) {
        *result = f(CONTEXT, a, b);
        return 0;
    }
    return trap_kind;
}

static int
call_e4(E4 *f, const int64_t *args, double d, int64_t *result)
{
    trap_kind = 0;
    if (setjmp(trap_jump) == 0) {
        *result = f(CONTEXT, args[0], args[1], args[2], d);
        return 0;
    }
    return trap_kind;
}

static bool
takes_two_ints(const Checker *c, const IrFunc *fn)
{
    const IrSlot *slots = c->ir.code.slots + fn->first_slot;
    u32 ret = (u32)c->types.arg1[c->decls.types[fn->decl]];
    return fn->nparams == 2 && slots[0].type == TYPE_ID_I64 && slots[1].type == TYPE_ID_I64 &&
           ret == TYPE_ID_I64;
}

static bool
agree_on_grid(const Build *b, Package *p, u32 k, u32 *tested)
{
    IrReference ref;
    ir_reference_init(&ref, &p->c);
    bool ok = true;
    for (u32 f = 0; ok && f < p->c.ir.nfuncs; f++) {
        const IrFunc *fn = &p->c.ir.funcs[f];
        if (!takes_two_ints(&p->c, fn)) {
            continue;
        }
        I2 *compiled = (I2 *)lookup(b, p, fn->decl, 'g', k);
        *tested += 1;
        for (int64_t a = -3; ok && a <= 14; a++) {
            for (int64_t x = -3; ok && x <= 12; x++) {
                int64_t y = a < 14 ? a : INT64_MIN;
                u64 slots[2] = { (u64)y, (u64)x };
                u64 want = 0;
                int64_t got = 0;
                int want_trap = ir_reference_run(&ref, fn->decl, slots, &want);
                int got_trap = call_i2(compiled, y, x, &got);
                ok = want_trap == got_trap && (want_trap || (int64_t)want == got);
                if (!ok) {
                    Name n = names_get(&p->lx.names, p->c.decls.names[fn->decl]);
                    fprintf(
                        stderr,
                        "c_source test: program %u, %.*s(%lld, %lld): reference %lld (trap %d), C %lld (trap %d)\n",
                        k,
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
    return ok;
}

static u32
decl_named(Package *p, const char *name)
{
    u32 before = p->lx.names.count;
    u32 id = names_intern(&p->lx.names, name, (u32)strlen(name));
    u32 decl = id <= before ? p->c.package_names[id] : 0;
    assert(decl);
    return decl;
}

static bool
agree_on_random(const Build *b, Package *p, u32 k, u64 seed, u64 *rng, const char *text)
{
    IrReference ref;
    ir_reference_init(&ref, &p->c);
    bool ok = true;
    for (u32 e = 0; ok && e < PROGRAM_ENTRIES; e++) {
        char name[8];
        snprintf(name, sizeof name, "e%u", e);
        u32 decl = decl_named(p, name);
        E4 *compiled = (E4 *)lookup(b, p, decl, 'p', k);
        for (int run = 0; ok && run < 6; run++) {
            int64_t args[3] = { random_argument(rng), random_argument(rng), random_argument(rng) };
            double d = random_float_argument(rng);
            u64 slots[4] = { (u64)args[0], (u64)args[1], (u64)args[2], 0 };
            memcpy(&slots[3], &d, sizeof d);
            u64 want = 0;
            int64_t got = 0;
            int want_trap = ir_reference_run(&ref, decl, slots, &want);
            int got_trap = call_e4(compiled, args, d, &got);
            ok = want_trap == got_trap && (want_trap || (int64_t)want == got);
            if (!ok) {
                fprintf(
                    stderr,
                    "c_source test: seed %llu, %s(%lld, %lld, %lld, %g): reference %lld (trap %d), C %lld (trap %d)\n%s",
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
            }
        }
    }
    ir_reference_free(&ref);
    return ok;
}

TEST_CASE(agrees_with_reference)
{
    const char *env = getenv("CHIBI_DIFFERENTIAL");
    const char *seed_text = getenv("CHIBI_DIFFERENTIAL_SEED");
    u32 programs = env ? (u32)atoi(env) : 60;
    u64 rng = seed_text ? strtoull(seed_text, NULL, 10) : 0x5EED;
    Build b;
    REQUIRE(open_build(&b));

    char *text = malloc(PROGRAM_SIZE);
    u64 *seeds = malloc((programs + 1) * sizeof *seeds);
    u32 grid = NBACKEND_PROGRAMS + (u32)(sizeof C_PROGRAMS / sizeof C_PROGRAMS[0]);
    bool ok = true;
    for (u32 k = 0; ok && k < grid; k++) {
        ok = add_package(&b, k, grid_program(k), 'g');
    }
    for (u32 k = 0; ok && k < programs; k++) {
        seeds[k] = rng;
        size_t len = random_typed_program(&rng, text);
        text[len] = 0;
        ok = add_package(&b, k, text, 'p');
    }
    ok = ok && finish_build(&b);

    u32 tested = 0;
    for (u32 k = 0; ok && k < grid; k++) {
        Package p;
        ok = check_source(&p, grid_program(k)) && agree_on_grid(&b, &p, k, &tested);
        release(&p);
    }
    if (ok && tested != GRID_FUNCTIONS) {
        fprintf(stderr, "c_source test: %u functions take two ints, expected %u\n", tested, (u32)GRID_FUNCTIONS);
        ok = false;
    }

    u64 args = rng;
    for (u32 k = 0; ok && k < programs; k++) {
        u64 r = seeds[k];
        size_t len = random_typed_program(&r, text);
        text[len] = 0;
        Package p;
        ok = check_source(&p, text) && agree_on_random(&b, &p, k, seeds[k], &args, text);
        release(&p);
    }
    free(seeds);
    free(text);
    close_build(&b, ok);
    REQUIRE(ok);
    return true;
}

#endif

TEST_SUITE(c_source_tests)
{
    RUN_TEST_CASE(reports_unsupported);
    RUN_TEST_CASE(emits_doubling_structs);
#ifdef CHIBI_TEST_CC
    RUN_TEST_CASE(agrees_with_reference);
#endif
}
