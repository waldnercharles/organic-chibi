#define _DEFAULT_SOURCE

#include "../src/c_source.h"
#include "../vendor/pico_unit/pico_unit.h"
#include "backend_programs.h"
#include "ir_reference.h"
#include "random/program.h"
#include "vectors.h"

#include <assert.h>
#include <dlfcn.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct
{
    Arena arena;
    Lexer lx;
    ParsedFile files[2];
    Checker c;
} Package;

static const char *const UNSUPPORTED[][2] = {
    { "extern fn tally(..) -> int\n"
      "fn f(a: int) -> int {\n"
      "    return tally(a, 2)\n"
      "}\n",
      "C11 can't declare an `extern fn` that takes only `..`" },
};

static bool
check_files(Package *p, const char *const *texts, const char *const *paths, u32 n, const u32 *target, bool release_fast)
{
    *p = (Package){ 0 };
    size_t total = 0;
    for (u32 i = 0; i < n; i++) {
        total += strlen(texts[i]);
    }
    lexer_init(&p->lx, &p->arena, total);

    for (u32 i = 0; i < n; i++) {
        size_t len = strlen(texts[i]);
        char *src = arena_alloc(&p->arena, len + LEXER_SOURCE_PADDING, 16);
        memset(src, 0, len + LEXER_SOURCE_PADDING);
        memcpy(src, texts[i], len);

        Tokens tokens = lexer_alloc_tokens(&p->arena, len);
        int ntokens = lexer_tokenize(&p->lx, src, tokens);
        Nodes nodes = parser_alloc_nodes(&p->arena, ntokens);
        Parser parser;
        int count = parser_parse(&parser, tokens, ntokens, nodes);
        if (count < 0) {
            fprintf(stderr, "c_source test: the program does not parse\n%s", texts[i]);
            return false;
        }
        p->files[i] = (ParsedFile){ paths[i], src,        tokens,
                                    nodes,    (u32)count, (u32)parser.top_count,
                                    false };
    }

    check_init(&p->c, &p->lx.names);
    p->c.release_fast = release_fast;
    if (target) {
        memcpy(p->c.target, target, sizeof p->c.target);
    }
    check_package(&p->c, p->files, n);
    if (p->c.ndiags) {
        check_print_diagnostics(stderr, &p->c);
        return false;
    }
    return true;
}

static bool
check_source(Package *p, const char *text)
{
    return check_files(p, &text, (const char *const[]){ "a" }, 1, NULL, false);
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

TEST_CASE(writes_inline_marks)
{
    char *text = NULL;
    size_t len = 0;
    FILE *out = open_memstream(&text, &len);
    assert(out);
    Package p;
    bool ok = check_source(&p, INLINE_MARKS) && c_source_package(out, &p.c, "m_") == NULL;
    release(&p);
    fclose(out);

    static const char *const want[] = {
        "\nCHIBI_FN __attribute__((always_inline)) int64_t m_inl_twice(void *ctx, int64_t s0);\n",
        "\nCHIBI_FN __attribute__((noinline)) int64_t m_inl_step(void *ctx, int64_t s0, int64_t s1);\n",
        "\nCHIBI_FN __attribute__((always_inline)) int64_t m_inl_mix(void *ctx, int64_t s0, int64_t s1)\n{",
        "\nCHIBI_FN __attribute__((noinline)) int64_t m_inl_calls(void *ctx, int64_t s0, int64_t s1)\n{",
    };
    for (size_t k = 0; ok && k < sizeof want / sizeof want[0]; k++) {
        if (!strstr(text, want[k])) {
            fprintf(stderr, "c_source test: missing %s", want[k]);
            ok = false;
        }
    }
    free(text);
    REQUIRE(ok);
    return true;
}

static const u32 LINUX_X86_64[NTARGETS] = { NAME_LINUX, NAME_X86_64 };

static const char OS_GUARD[] =
    "\n#ifndef __linux__\n"
    "#error \"chibi wrote this C for target_os linux\"\n"
    "#endif\n";

static const char ARCH_GUARD[] =
    "\n#ifndef __x86_64__\n"
    "#error \"chibi wrote this C for target_arch x86_64\"\n"
    "#endif\n";

static const char TARGET_TG_ARCH_C[] =
    "CHIBI_FN int64_t t_tg_arch(void *ctx, int64_t s0, int64_t s1)\n"
    "{\n"
    "    int64_t v1;\n"
    "    int64_t v2;\n"
    "    int64_t v3;\n"
    "    int64_t v6;\n"
    "    int64_t v7;\n"
    "    int64_t v8;\n"
    "#line 4\n"
    "\n"
    "\n"
    "\n"
    "    v6 = s0; v7 = s1; if (__builtin_sub_overflow(v6, v7, &v8)) chibi_trap(1); return v8; chibi_trap(5);\n"
    "}\n";

static const char TARGET_TG_OS_C[] =
    "CHIBI_FN int64_t t_tg_os(void *ctx, int64_t s0, int64_t s1)\n"
    "{\n"
    "    int64_t s2;\n"
    "    int64_t v0;\n"
    "    int64_t v4;\n"
    "    int64_t v5;\n"
    "    uint8_t v6;\n"
    "    int64_t v8;\n"
    "    int64_t v9;\n"
    "    int64_t v10;\n"
    "    int64_t v11;\n"
    "    int64_t v18;\n"
    "    int64_t v19;\n"
    "    int64_t v20;\n"
    "    int64_t v21;\n"
    "    int64_t v24;\n"
    "    int64_t v25;\n"
    "    int64_t v26;\n"
    "    int64_t v27;\n"
    "    int64_t v31;\n"
    "#line 11\n"
    "    v0 = s0; s2 = v0;\n"
    "    {\n"
    "\n"
    "\n"
    "\n"
    "        {\n"
    "\n"
    "\n"
    "            v24 = s2; if (__builtin_mul_overflow(v24, 3, &v25)) chibi_trap(1); v26 = s1; if (__builtin_add_overflow(v25, v26, &v27)) chibi_trap(1); s2 = v27;\n"
    "\n"
    "        } } v31 = s2; return v31; chibi_trap(5);\n"
    "}\n";

static char *
target_source(const char *text, const u32 *target)
{
    char *got = NULL;
    size_t len = 0;
    FILE *out = open_memstream(&got, &len);
    assert(out);
    Package p;
    bool ok = check_files(&p, &text, (const char *const[]){ "a" }, 1, target, false) &&
              c_source_package(out, &p.c, "t_") == NULL;
    release(&p);
    fclose(out);
    if (!ok) {
        free(got);
        return NULL;
    }
    return got;
}

static u32
count_text(const char *text, const char *part)
{
    u32 n = 0;
    for (const char *at = strstr(text, part); at; at = strstr(at + 1, part)) {
        n++;
    }
    return n;
}

TEST_CASE(writes_assumptions)
{
    char *text = NULL;
    size_t len = 0;
    FILE *out = open_memstream(&text, &len);
    assert(out);
    const char *source = RELEASE_FAST_GRID;
    Package p;
    bool ok = check_files(&p, &source, (const char *const[]){ "a" }, 1, NULL, true) &&
              c_source_package(out, &p.c, "r_") == NULL;
    release(&p);
    fclose(out);

    char trap[32];
    snprintf(trap, sizeof trap, "chibi_trap(%d)", TRAP_ASSERT);
    ok = ok && count_text(text, ") __builtin_unreachable();\n") == 5 && count_text(text, trap) == 0;
    free(text);
    REQUIRE(ok);
    return true;
}

TEST_CASE(writes_target_guards)
{
    char *all = target_source(TARGETS, LINUX_X86_64);
    char *arch = target_source("fn f() -> bool {\n    return target_arch == .arm64\n}\n", LINUX_X86_64);
    char *none = target_source("fn f() -> int {\n    return 1\n}\n", LINUX_X86_64);
    REQUIRE(all && arch && none);

    bool ok = count_text(all, OS_GUARD) == 1 && count_text(all, ARCH_GUARD) == 1 &&
              count_text(all, "#error") == 2 && count_text(all, TARGET_TG_ARCH_C) == 1 &&
              count_text(all, TARGET_TG_OS_C) == 1 && count_text(all, "c_tg_missing(") == 1;
    ok = ok && count_text(arch, ARCH_GUARD) == 1 && count_text(arch, "#error") == 1;
    ok = ok && count_text(none, "#error") == 0;
    if (!ok) {
        fprintf(stderr, "c_source test: target guards\n%s\n%s\n%s", all, arch, none);
    }
    free(all);
    free(arch);
    free(none);
    REQUIRE(ok);
    return true;
}

static const char LAYOUT_WALK[] =
    "struct B { x: u8, y: u32 }\n"
    "struct C { z: u16 }\n"
    "struct D { w: u64, next: ?*D }\n"
    "struct E { v: i8, back: ?*A }\n"
    "struct F { u: u32 }\n"
    "struct G { t: u8 }\n"
    "struct H { s: u64, r: u8 }\n"
    "struct Q { p: i16 }\n"
    "struct R { o: u16, n: u64 }\n"
    "struct S { m: u8 }\n"
    "struct Unseen { q: u32 }\n"
    "struct A {\n"
    "    b: B,\n"
    "    cs: [3]C,\n"
    "    d: ?*D,\n"
    "    e: [*]E,\n"
    "    f: extern fn(g: *G) -> F,\n"
    "    s: []S,\n"
    "}\n"
    "\n"
    "extern fn take(h: *H) -> int\n"
    "extern shared: R\n"
    "\n"
    "fn back(q: *Q) -> int {\n"
    "    return int(q.p)\n"
    "}\n"
    "\n"
    "fn local(u: *Unseen) -> int {\n"
    "    return int(u.q)\n"
    "}\n"
    "\n"
    "fn get_back() -> extern fn(q: *Q) -> int {\n"
    "    return back\n"
    "}\n"
    "\n"
    "export fn give(a: *A) -> int {\n"
    "    return int(a.b.x)\n"
    "}\n";

static const char LAYOUT_WALK_C[] =
    "#include <stddef.h>\n"
    "\n"
    "_Static_assert(sizeof(B) == 8 && _Alignof(B) == 4, \"B\");\n"
    "_Static_assert(offsetof(B, x) == 0, \"B.x\");\n"
    "_Static_assert(offsetof(B, y) == 4, \"B.y\");\n"
    "\n"
    "_Static_assert(sizeof(C) == 2 && _Alignof(C) == 2, \"C\");\n"
    "_Static_assert(offsetof(C, z) == 0, \"C.z\");\n"
    "\n"
    "_Static_assert(sizeof(D) == 16 && _Alignof(D) == 8, \"D\");\n"
    "_Static_assert(offsetof(D, w) == 0, \"D.w\");\n"
    "_Static_assert(offsetof(D, next) == 8, \"D.next\");\n"
    "\n"
    "_Static_assert(sizeof(E) == 16 && _Alignof(E) == 8, \"E\");\n"
    "_Static_assert(offsetof(E, v) == 0, \"E.v\");\n"
    "_Static_assert(offsetof(E, back) == 8, \"E.back\");\n"
    "\n"
    "_Static_assert(sizeof(F) == 4 && _Alignof(F) == 4, \"F\");\n"
    "_Static_assert(offsetof(F, u) == 0, \"F.u\");\n"
    "\n"
    "_Static_assert(sizeof(G) == 1 && _Alignof(G) == 1, \"G\");\n"
    "_Static_assert(offsetof(G, t) == 0, \"G.t\");\n"
    "\n"
    "_Static_assert(sizeof(H) == 16 && _Alignof(H) == 8, \"H\");\n"
    "_Static_assert(offsetof(H, s) == 0, \"H.s\");\n"
    "_Static_assert(offsetof(H, r) == 8, \"H.r\");\n"
    "\n"
    "_Static_assert(sizeof(Q) == 2 && _Alignof(Q) == 2, \"Q\");\n"
    "_Static_assert(offsetof(Q, p) == 0, \"Q.p\");\n"
    "\n"
    "_Static_assert(sizeof(R) == 16 && _Alignof(R) == 8, \"R\");\n"
    "_Static_assert(offsetof(R, o) == 0, \"R.o\");\n"
    "_Static_assert(offsetof(R, n) == 8, \"R.n\");\n"
    "\n"
    "_Static_assert(sizeof(S) == 1 && _Alignof(S) == 1, \"S\");\n"
    "_Static_assert(offsetof(S, m) == 0, \"S.m\");\n"
    "\n"
    "_Static_assert(sizeof(A) == 56 && _Alignof(A) == 8, \"A\");\n"
    "_Static_assert(offsetof(A, b) == 0, \"A.b\");\n"
    "_Static_assert(offsetof(A, cs) == 8, \"A.cs\");\n"
    "_Static_assert(offsetof(A, d) == 16, \"A.d\");\n"
    "_Static_assert(offsetof(A, e) == 24, \"A.e\");\n"
    "_Static_assert(offsetof(A, f) == 32, \"A.f\");\n"
    "_Static_assert(offsetof(A, s) == 40, \"A.s\");\n";

static char *
layouts_of(const char *text)
{
    char *got = NULL;
    size_t len = 0;
    FILE *out = open_memstream(&got, &len);
    assert(out);
    Package p;
    bool ok = check_source(&p, text);
    if (ok) {
        c_source_layouts(out, &p.c);
    }
    release(&p);
    fclose(out);
    if (!ok) {
        free(got);
        return NULL;
    }
    return got;
}

TEST_CASE(writes_layouts)
{
    char *got = layouts_of(LAYOUT_WALK);
    bool ok = got && strcmp(got, LAYOUT_WALK_C) == 0;
    if (!ok) {
        fprintf(stderr, "c_source test: layouts\n%s", got ? got : "the program failed to check\n");
    }
    free(got);
    REQUIRE(ok);
    return true;
}

#ifdef CHIBI_TEST_CC

#if defined(__APPLE__) && defined(__aarch64__)
#define TARGET " -arch arm64"
#define LINK " -undefined dynamic_lookup"
#elif defined(__APPLE__)
#define TARGET " -arch x86_64"
#define LINK " -undefined dynamic_lookup"
#else
#define TARGET ""
#define LINK ""
#endif

#define FLAGS                                                                  \
    "-std=c11 -O2 -fPIC -Wall -Wextra -Werror -Wno-unused-parameter "          \
    "-Wno-unused-variable -Wno-unused-but-set-variable -Wno-type-limits "      \
    "-fno-lax-vector-conversions -fsanitize=undefined "                        \
    "-fno-sanitize-recover=all -DCHIBI_FN="

enum
{
    MAX_JOBS = 8,
    GRID_FUNCTIONS = 351,
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

#ifdef __APPLE__
#define STDERR_NAME "__stderrp"
#else
#define STDERR_NAME "stderr"
#endif

static const char INTEROP[] =
    "struct P { x: int, y: int }\n"
    "struct W { a: int, b: int, c: int }\n"
    "struct Hook { f: extern fn(x: int) -> int, n: int }\n"
    "\n"
    "const table := W.{ 7, 8, 9 }\n"
    "const K: int = -7\n"
    "\n"
    "extern fn labs(x: i64) -> i64\n"
    "extern fn memset(p: *[8]u8, c: i32, n: u64) -> *[8]u8\n"
    "extern fn strlen(s: [*]u8) -> u64\n"
    "extern fn snprintf(buf: [*]u8, n: u64, fmt: [*]u8, ..) -> i32\n"
    "extern fn qsort(base: *[8]int, n: u64, size: u64, cmp: extern fn(a: *int, b: *int) -> i32)\n"
    "extern fn chibi_cs_mixed(a: i8, b: f64, c: u16, d: f32, e: i32, f: bool) -> f64\n"
    "extern fn chibi_cs_eight(a: int, b: int, c: int, d: int, e: int, f: int, g: int, h: int) -> int\n"
    "extern fn chibi_cs_u8() -> u8\n"
    "extern fn chibi_cs_i8() -> i8\n"
    "extern fn chibi_cs_u16() -> u16\n"
    "extern fn chibi_cs_i16() -> i16\n"
    "extern fn chibi_cs_u32() -> u32\n"
    "extern fn chibi_cs_i32() -> i32\n"
    "extern fn chibi_cs_bool(x: int) -> bool\n"
    "extern fn chibi_cs_f32(x: f32) -> f32\n"
    "extern fn chibi_cs_point(p: P, k: int) -> P\n"
    "extern fn chibi_cs_scribble(w: W) -> int\n"
    "extern fn chibi_cs_wide(k: int) -> W\n"
    "pub extern fn chibi_cs_tick()\n"
    "extern fn chibi_cs_va(n: int, ..) -> f64\n"
    "extern fn chibi_cs_vpoint(p: P, n: int, ..) -> int\n"
    "extern fn chibi_cs_twice(x: int) -> int\n"
    "extern fn chibi_cs_each(f: extern fn(x: int) -> int, n: int) -> int\n"
    "extern fn chibi_cs_same(a: extern fn(x: int) -> int, b: extern fn(x: int) -> int) -> bool\n"
    "extern fn chibi_cs_call_seven(f: extern fn(a: int, b: i8, c: u16, d: i32, e: u32, f: bool, g: *int) -> int, x: int) -> int\n"
    "extern fn chibi_cs_call_wide(f: extern fn(w: W, k: int) -> W, k: int) -> int\n"
    "extern fn chibi_cs_call_void(f: extern fn(p: *int), p: *int)\n"
    "extern fn chibi_cs_unused(x: int) -> int\n"
    "extern fn chibi_cs_swap_hook(h: ?extern fn(x: int) -> int) -> ?extern fn(x: int) -> int\n"
    "extern fn chibi_cs_fire_hook(x: int) -> int\n"
    "\n"
    "extern chibi_cs_ticks: int\n"
    "extern chibi_cs_origin: P\n"
    "extern chibi_cs_codes: [3]u8\n"
    "extern " STDERR_NAME ": ?*u8\n"
    "\n"
    "fn after_c(x: int) -> int {\n"
    "    return labs(x) + labs(x - 10)\n"
    "}\n"
    "\n"
    "fn mixed(x: int) -> f64 {\n"
    "    return chibi_cs_mixed(i8(x) - 5, f64(x) * 0.5, u16(x) + 300, f32(x) + 1.5, i32(x) * -7, x > 3)\n"
    "}\n"
    "\n"
    "fn eight(x: int) -> int {\n"
    "    return chibi_cs_eight(x + 1, x * 2, x - 3, 4, x + 5, 6, x * 7, x + 8)\n"
    "}\n"
    "\n"
    "fn narrow(x: int) -> int {\n"
    "    s := int(chibi_cs_u8() / 3) + int(chibi_cs_i8() / 3) * 10 + int(chibi_cs_u16() / 7)\n"
    "    s += int(chibi_cs_i16() / 3) + int(chibi_cs_u32() / 5) + int(chibi_cs_i32() / 2)\n"
    "    if chibi_cs_bool(x) {\n"
    "        return -s\n"
    "    }\n"
    "    return s\n"
    "}\n"
    "\n"
    "fn floats(x: int) -> f64 {\n"
    "    return f64(chibi_cs_f32(f32(x) + 0.25))\n"
    "}\n"
    "\n"
    "fn point(x: int) -> int {\n"
    "    p := chibi_cs_point(P.{ x, x + 1 }, 3)\n"
    "    return p.x * 100 + p.y\n"
    "}\n"
    "\n"
    "fn scribble(x: int) -> int {\n"
    "    w := W.{ x, 2, 3 }\n"
    "    s := chibi_cs_scribble(w)\n"
    "    return s * 1000 + w.a + w.b + w.c\n"
    "}\n"
    "\n"
    "fn from_table(x: int) -> int {\n"
    "    return chibi_cs_scribble(table) * 10000 + chibi_cs_scribble(table) + x\n"
    "}\n"
    "\n"
    "fn wide(x: int) -> int {\n"
    "    w := chibi_cs_wide(x)\n"
    "    return w.a + w.b * 10 + w.c * 100\n"
    "}\n"
    "\n"
    "fn ticks(n: int) -> int {\n"
    "    for i in 0..n {\n"
    "        chibi_cs_tick()\n"
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
    "}\n"
    "\n"
    "fn format(buf: [*]u8, fmt: [*]u8, x: int) -> i32 {\n"
    "    return snprintf(buf, 128, fmt, x, i8(x) - 3, u16(x) + 7, f32(x) * 0.5, f64(x) / 4.0, x > 2, 123456789012, 7, -1, K, 18446744073709551615, 2.5)\n"
    "}\n"
    "\n"
    "fn literal_format(buf: [*]u8, x: int) -> i32 {\n"
    "    return snprintf(buf, 128, \"%s=%ld|%s|\", \"x\", x, \"\\u00e9\\t\")\n"
    "}\n"
    "\n"
    "fn literal_length(x: int) -> int {\n"
    "    return int(strlen(\"chibi\")) * 100 + int(strlen(\"\")) + x\n"
    "}\n"
    "\n"
    "fn add_ticks(x: int) -> int {\n"
    "    chibi_cs_ticks += x\n"
    "    return chibi_cs_ticks\n"
    "}\n"
    "\n"
    "fn move_origin(x: int) -> int {\n"
    "    chibi_cs_origin.x += x\n"
    "    p := &chibi_cs_origin\n"
    "    p.y = p.x * 2\n"
    "    return chibi_cs_origin.y + int(chibi_cs_codes[2])\n"
    "}\n"
    "\n"
    "fn error_stream() -> ?*u8 {\n"
    "    return " STDERR_NAME "\n"
    "}\n"
    "\n"
    "fn pairs(x: int) -> f64 {\n"
    "    return chibi_cs_va(5, x, 0.5, K, f32(x), x * 4, 1.25, int(-9), 2.0, x + 6, 0.75)\n"
    "}\n"
    "\n"
    "fn vpoint(x: int) -> int {\n"
    "    return chibi_cs_vpoint(P.{ x, x + 1 }, 3, x * 2, x + 5, K)\n"
    "}\n"
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
    "    return chibi_cs_each(tap, n) * 1000 + chibi_cs_each(chibi_cs_twice, n)\n"
    "}\n"
    "\n"
    "fn hooked(n: int) -> int {\n"
    "    f: extern fn(x: int) -> int = tap\n"
    "    h := Hook.{ .f = f, .n = n + 1 }\n"
    "    return chibi_cs_each(h.f, h.n)\n"
    "}\n"
    "\n"
    "fn get_tap() -> extern fn(x: int) -> int {\n"
    "    return tap\n"
    "}\n"
    "\n"
    "fn get_twice() -> extern fn(x: int) -> int {\n"
    "    return chibi_cs_twice\n"
    "}\n"
    "\n"
    "fn hooks(n: int) -> int {\n"
    "    old := chibi_cs_swap_hook(tap)\n"
    "    a := chibi_cs_fire_hook(n)\n"
    "    mine := chibi_cs_swap_hook(old)\n"
    "    b := chibi_cs_fire_hook(n)\n"
    "    c := 0\n"
    "    if mine != nil {\n"
    "        c = chibi_cs_each(mine, 2)\n"
    "        if mine == get_tap() {\n"
    "            c += 50\n"
    "        }\n"
    "    }\n"
    "    _ = chibi_cs_swap_hook(bitcast(?extern fn(x: int) -> int, int(1)))\n"
    "    back := chibi_cs_swap_hook(old)\n"
    "    return a * 1000000 + b * 10000 + c * 100 + bitcast(int, back)\n"
    "}\n"
    "\n"
    "fn same(x: int) -> int {\n"
    "    s := x\n"
    "    if chibi_cs_same(tap, tap) {\n"
    "        s += 1\n"
    "    }\n"
    "    if chibi_cs_same(tap, chibi_cs_twice) {\n"
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
    "fn seven(a: int, b: i8, c: u16, d: i32, e: u32, f: bool, g: *int) -> int {\n"
    "    s := a + int(b) * 10 + int(c) * 1000 + int(d) * 100000 + int(e) * 10000000 + g.*\n"
    "    if f {\n"
    "        s = -s\n"
    "    }\n"
    "    return s\n"
    "}\n"
    "\n"
    "fn call_seven(x: int) -> int {\n"
    "    return chibi_cs_call_seven(seven, x)\n"
    "}\n"
    "\n"
    "fn widen(w: W, k: int) -> W {\n"
    "    return W.{ w.a + k, w.b * k, w.c - k }\n"
    "}\n"
    "\n"
    "fn call_wide(x: int) -> int {\n"
    "    return chibi_cs_call_wide(widen, x)\n"
    "}\n"
    "\n"
    "fn bump(p: *int) {\n"
    "    p.* += 1\n"
    "}\n"
    "\n"
    "fn call_void(x: int) -> int {\n"
    "    n := x\n"
    "    chibi_cs_call_void(bump, &n)\n"
    "    return n\n"
    "}\n"
    "\n"
    "export fn cs_add3(a: int, b: i32, c: u8) -> int {\n"
    "    return a + int(b) * 10 + int(c) * 1000\n"
    "}\n"
    "\n"
    "export fn cs_half(x: f64) -> f64 {\n"
    "    return x * 0.5\n"
    "}\n"
    "\n"
    "export fn cs_swap(p: P) -> P {\n"
    "    return P.{ p.y, p.x }\n"
    "}\n"
    "\n"
    "export fn cs_tick() {\n"
    "    chibi_cs_tick()\n"
    "}\n"
    "\n"
    "export fn cs_step(x: int) -> int {\n"
    "    return 2 * x + 1\n"
    "}\n"
    "\n"
    "fn uses(n: int) -> int {\n"
    "    return chibi_cs_each(cs_step, n) * 1000 + cs_step(n)\n"
    "}\n"
    "\n"
    "fn get_step() -> extern fn(x: int) -> int {\n"
    "    return cs_step\n"
    "}\n"
    "\n"
    "export fn main(argc: i32, argv: [*][*]u8) -> i32 {\n"
    "    n := 0\n"
    "    for i in 0..int(argc) {\n"
    "        n += int(strlen(argv[i]))\n"
    "    }\n"
    "    return argc + i32(n) * 100\n"
    "}\n";

struct i0_P
{
    int64_t x;
    int64_t y;
};

struct i0_W
{
    int64_t a;
    int64_t b;
    int64_t c;
};

typedef int64_t I0(void *);
typedef int64_t I1(void *, int64_t);
typedef double D1(void *, int64_t);
typedef int32_t FORMAT(void *, uint8_t *, uint8_t *, int64_t);
typedef int32_t LITERAL_FORMAT(void *, uint8_t *, int64_t);
typedef void SORT(void *, uint8_t *, uint8_t);
typedef uint8_t *GET(void *);
typedef int64_t ADD3(int64_t, int32_t, uint8_t);
typedef double HALF(double);
typedef struct i0_P SWAP(struct i0_P);
typedef void TICK(void);
typedef int64_t STEP(int64_t);
typedef int32_t MAIN(int32_t, uint8_t *);

static uint8_t CONTEXT[16];
static jmp_buf trap_jump;
static volatile int trap_kind;

void *chibi_context = CONTEXT;
int64_t chibi_cs_ticks;
struct i0_P chibi_cs_origin = { 1, 2 };
uint8_t chibi_cs_codes[3] = { 5, 6, 7 };

NORETURN void
chibi_trap(int kind)
{
    trap_kind = kind;
    longjmp(trap_jump, 1);
}

double
chibi_cs_mixed(int8_t a, double b, uint16_t c, float d, int32_t e, bool f)
{
    return a + b * 2 + c * 3 + d * 4 + e * 5 + (f ? 1000 : 0);
}

int64_t
chibi_cs_eight(int64_t a, int64_t b, int64_t c, int64_t d, int64_t e, int64_t f, int64_t g, int64_t h)
{
    return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f + 7 * g + 8 * h;
}

uint64_t
chibi_cs_u8(void)
{
    return 0xDEADBEEFCAFE0105u;
}

uint64_t
chibi_cs_i8(void)
{
    return 0x123456789ABCDE80u;
}

uint64_t
chibi_cs_u16(void)
{
    return 0xFFFFFFFFFFFF8001u;
}

uint64_t
chibi_cs_i16(void)
{
    return 0x0000000100008000u;
}

uint64_t
chibi_cs_u32(void)
{
    return 0xFFFFFFFF80000007u;
}

uint64_t
chibi_cs_i32(void)
{
    return 0x00000001FFFFFFF9u;
}

uint64_t
chibi_cs_bool(int64_t x)
{
    return 0xFFFFFFFFFFFFFF00u | (uint64_t)(x & 1);
}

float
chibi_cs_f32(float x)
{
    return x * 2.0f;
}

struct i0_P
chibi_cs_point(struct i0_P p, int64_t k)
{
    return (struct i0_P){ p.x + k, p.y * k };
}

int64_t
chibi_cs_scribble(struct i0_W w)
{
    int64_t s = w.a + w.b * 10 + w.c * 100;

    volatile struct i0_W *v = &w;
    v->a = 900;
    v->b = 800;
    v->c = 700;
    return s;
}

struct i0_W
chibi_cs_wide(int64_t k)
{
    return (struct i0_W){ k, k + 1, k + 2 };
}

void
chibi_cs_tick(void)
{
    chibi_cs_ticks++;
}

double
chibi_cs_va(int64_t n, ...)
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
chibi_cs_vpoint(struct i0_P p, int64_t n, ...)
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
chibi_cs_twice(int64_t x)
{
    return 2 * x;
}

int64_t
chibi_cs_each(int64_t (*f)(int64_t), int64_t n)
{
    int64_t s = 0;
    for (int64_t i = 0; i < n; i++) {
        s = s * 3 + f(i);
    }
    return s;
}

bool
chibi_cs_same(int64_t (*a)(int64_t), int64_t (*b)(int64_t))
{
    return a == b;
}

static STEP *cs_hook;

STEP *
chibi_cs_swap_hook(STEP *h)
{
    STEP *old = cs_hook;
    cs_hook = h;
    return old;
}

int64_t
chibi_cs_fire_hook(int64_t x)
{
    return cs_hook ? cs_hook(x) : -1;
}

int64_t
chibi_cs_call_seven(int64_t (*f)(int64_t, int8_t, uint16_t, int32_t, uint32_t, uint8_t, uint8_t *), int64_t x)
{
    int64_t g = x * 11;
    return f(x, (int8_t)(x - 50), (uint16_t)(x * 1000), (int32_t)(x * -100000), (uint32_t)x * 200000000u, (uint8_t)(x & 1), (uint8_t *)&g);
}

int64_t
chibi_cs_call_wide(struct i0_W (*f)(struct i0_W, int64_t), int64_t k)
{
    struct i0_W w = f((struct i0_W){ k, k + 1, k + 2 }, 7);
    return w.a + w.b * 10 + w.c * 100;
}

void
chibi_cs_call_void(void (*f)(uint8_t *), uint8_t *p)
{
    f(p);
    f(p);
}

static const char *
grid_program(u32 k)
{
    return k < NBACKEND_PROGRAMS ? BACKEND_PROGRAMS[k] : C_PROGRAMS[k - NBACKEND_PROGRAMS];
}

static bool
open_build(Build *b, u32 files)
{
    *b = (Build){ .dir = "/tmp/chibi-c-XXXXXX" };
    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    b->jobs = files ? files : cpus < 1 ? 1 : cpus > MAX_JOBS ? MAX_JOBS : (u32)cpus;
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

static const char *
integer_name(u32 type)
{
    static const char *const integers[] = {
        [TYPE_ID_BOOL] = "uint8_t", [TYPE_ID_I8] = "int8_t", [TYPE_ID_I16] = "int16_t",
        [TYPE_ID_I32] = "int32_t", [TYPE_ID_I64] = "int64_t", [TYPE_ID_U8] = "uint8_t",
        [TYPE_ID_U16] = "uint16_t", [TYPE_ID_U32] = "uint32_t", [TYPE_ID_U64] = "uint64_t",
    };
    return type < sizeof integers / sizeof integers[0] ? integers[type] : NULL;
}

static void
put_wrappers(FILE *out, const Package *p, const char *prefix)
{
    const Checker *c = &p->c;
    for (u32 f = 0; f < c->ir.nfuncs; f++) {
        const IrFunc *fn = &c->ir.funcs[f];
        const IrSlot *slots = c->ir.code.slots + fn->first_slot;
        bool plain = fn->nparams <= 4 && integer_name((u32)c->types.arg1[c->decls.types[fn->decl]]);
        for (u32 s = 0; s < fn->nparams; s++) {
            plain &= integer_name(slots[s].type) != NULL;
        }
        if (!plain) {
            continue;
        }

        Name n = names_get(&p->lx.names, c->decls.names[fn->decl]);
        fprintf(out, "\nint64_t w%s%.*s(void *ctx, int64_t a0, int64_t a1, int64_t a2, int64_t a3)\n", prefix, (int)n.len, n.text);
        fprintf(out, "{\n    return (int64_t)%s%.*s(ctx", prefix, (int)n.len, n.text);
        for (u32 s = 0; s < fn->nparams; s++) {
            fprintf(out, ", (%s)a%u", integer_name(slots[s].type), s);
        }
        fputs(");\n}\n", out);
    }
}

static bool
add_package(Build *b, u32 k, const char *text, char tag, bool wrap)
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
        } else if (wrap) {
            put_wrappers(b->outs[k % b->jobs], &p, prefix);
        }
    }
    release(&p);
    return ok;
}

static bool
finish_build(Build *b, const char *flags)
{
    for (u32 j = 0; j < b->jobs; j++) {
        fclose(b->outs[j]);
    }

    char command[4096];
    int n = snprintf(command, sizeof command, "cd %s && printf '%%s\\n'", b->dir);
    for (u32 j = 0; j < b->jobs; j++) {
        n += snprintf(command + n, sizeof command - (size_t)n, " p%u", j);
    }
    snprintf(command + n, sizeof command - (size_t)n, " | xargs -P %u -I{} " CHIBI_TEST_CC TARGET " " FLAGS "%s -c -o {}.o {}.c && " CHIBI_TEST_CC TARGET " -shared" LINK " -fsanitize=undefined -o programs.so p*.o", b->jobs, flags);
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
    REQUIRE(open_build(&b, 0));

    char *text = malloc(PROGRAM_SIZE);
    u64 *seeds = malloc((programs + 1) * sizeof *seeds);
    u32 grid = NBACKEND_PROGRAMS + (u32)(sizeof C_PROGRAMS / sizeof C_PROGRAMS[0]);
    bool ok = true;
    for (u32 k = 0; ok && k < grid; k++) {
        ok = add_package(&b, k, grid_program(k), 'g', false);
    }
    for (u32 k = 0; ok && k < programs; k++) {
        seeds[k] = rng;
        size_t len = random_typed_program(&rng, text);
        text[len] = 0;
        ok = add_package(&b, k, text, 'p', false);
    }
    ok = ok && finish_build(&b, "");

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

static void *
symbol(const Build *b, const char *name)
{
    void *f = dlsym(b->library, name);
    if (!f) {
        fprintf(stderr, "c_source test: %s is not in the library\n", name);
        abort();
    }
    return f;
}

static bool
gives(const char *name, int64_t x, int64_t got, int64_t want)
{
    if (got != want) {
        fprintf(stderr, "c_source test: %s(%lld) gave %lld, expected %lld\n", name, (long long)x, (long long)got, (long long)want);
    }
    return got == want;
}

static bool
gives_f64(const char *name, int64_t x, double got, double want)
{
    if (got != want) {
        fprintf(stderr, "c_source test: %s(%lld) gave %a, expected %a\n", name, (long long)x, got, want);
    }
    return got == want;
}

static bool
calls_c(const Build *b, int64_t x)
{
    int64_t narrow = 1 - 420 + 4681 - 10922 + 429496731 - 3;
    int64_t point = ((3 * x + 2) * 10 + 2 * x) * 10 + x + 5;

    bool ok = gives("after_c", x, ((I1 *)symbol(b, "i0_after_c"))(CONTEXT, x), x + llabs(x - 10));
    ok &= gives_f64("mixed", x, ((D1 *)symbol(b, "i0_mixed"))(CONTEXT, x), -26.0 * (double)x + 901 + (x > 3 ? 1000 : 0));
    ok &= gives("eight", x, ((I1 *)symbol(b, "i0_eight"))(CONTEXT, x), 70 * x + 133);
    ok &= gives("narrow", x, ((I1 *)symbol(b, "i0_narrow"))(CONTEXT, x), x & 1 ? -narrow : narrow);
    ok &= gives_f64("floats", x, ((D1 *)symbol(b, "i0_floats"))(CONTEXT, x), (double)(((float)x + 0.25f) * 2.0f));
    ok &= gives("point", x, ((I1 *)symbol(b, "i0_point"))(CONTEXT, x), 103 * x + 303);
    ok &= gives("scribble", x, ((I1 *)symbol(b, "i0_scribble"))(CONTEXT, x), 1001 * x + 320005);
    ok &= gives("from_table", x, ((I1 *)symbol(b, "i0_from_table"))(CONTEXT, x), 9870987 + x);
    ok &= gives("wide", x, ((I1 *)symbol(b, "i0_wide"))(CONTEXT, x), 111 * x + 210);
    ok &= gives("fill", x, ((I1 *)symbol(b, "i0_fill"))(CONTEXT, x), 8 * x);
    ok &= gives("vpoint", x, ((I1 *)symbol(b, "i0_vpoint"))(CONTEXT, x), point * 10 - 7);
    ok &= gives("literal_length", x, ((I1 *)symbol(b, "i0_literal_length"))(CONTEXT, x), 500 + x);

    int64_t ticks = chibi_cs_ticks;
    ok &= gives("ticks", x, ((I1 *)symbol(b, "i0_ticks"))(CONTEXT, x), x);
    ok &= gives("chibi_cs_ticks", x, chibi_cs_ticks, ticks + x);

    ticks = chibi_cs_ticks;
    ok &= gives("add_ticks", x, ((I1 *)symbol(b, "i0_add_ticks"))(CONTEXT, x), ticks + x);
    ok &= gives("chibi_cs_ticks", x, chibi_cs_ticks, ticks + x);

    int64_t origin = chibi_cs_origin.x + x;
    ok &= gives("move_origin", x, ((I1 *)symbol(b, "i0_move_origin"))(CONTEXT, x), 2 * origin + 7);
    ok &= gives("chibi_cs_origin", x, chibi_cs_origin.x * 1000 + chibi_cs_origin.y, origin * 1002);
    return ok;
}

static bool
formats(const Build *b, int64_t x)
{
    char fmt[64] = "%ld %d %d %.2f %.3f %d %ld %d %d %ld %lu %.1f";
    char buf[128] = { 0 }, want[128];
    int n = snprintf(want, sizeof want, fmt, (long)x, (int)(int8_t)(x - 3), (int)(uint16_t)(x + 7), (double)((float)x * 0.5f), (double)x / 4.0, (int)(x > 2), 123456789012L, 7, -1, -7L, 18446744073709551615UL, 2.5);
    if (((FORMAT *)symbol(b, "i0_format"))(CONTEXT, (uint8_t *)buf, (uint8_t *)fmt, x) != n || strcmp(buf, want) != 0) {
        fprintf(stderr, "c_source test: format(%lld) gave \"%s\", expected \"%s\"\n", (long long)x, buf, want);
        return false;
    }

    char line[128] = { 0 }, want_line[128];
    int m = snprintf(want_line, sizeof want_line, "%s=%ld|%s|", "x", (long)x, "\303\251\t");
    if (((LITERAL_FORMAT *)symbol(b, "i0_literal_format"))(CONTEXT, (uint8_t *)line, x) != m || strcmp(line, want_line) != 0) {
        fprintf(stderr, "c_source test: literal_format(%lld) gave \"%s\", expected \"%s\"\n", (long long)x, line, want_line);
        return false;
    }

    int64_t k[5] = { x, -7, x * 4, -9, x + 6 };
    double d[5] = { 0.5, (double)(float)x, 1.25, 2.0, 0.75 };
    double s = 0;
    for (int i = 0; i < 5; i++) {
        s = s * 2.0 + (double)k[i] + d[i];
    }
    return gives_f64("pairs", x, ((D1 *)symbol(b, "i0_pairs"))(CONTEXT, x), s);
}

static bool
sorts(const Build *b, int64_t x)
{
    int64_t a[8] = { 5, -3, 9, 0, x, -7, 3, x - 4 };
    SORT *sort = (SORT *)symbol(b, "i0_sort");

    sort(CONTEXT, (uint8_t *)a, 1);
    bool ok = true;
    for (int i = 1; i < 8; i++) {
        ok &= a[i - 1] <= a[i];
    }

    sort(CONTEXT, (uint8_t *)a, 0);
    for (int i = 1; i < 8; i++) {
        ok &= a[i - 1] >= a[i];
    }

    if (!ok) {
        fprintf(stderr, "c_source test: sort(%lld) left the numbers out of order\n", (long long)x);
    }
    return ok;
}

static int64_t
fold_each(int64_t n, int64_t times, int64_t plus)
{
    int64_t s = 0;
    for (int64_t i = 0; i < n; i++) {
        s = s * 3 + i * times + plus;
    }
    return s;
}

static int64_t
seven_of(int64_t x)
{
    int64_t s = x + (int8_t)(x - 50) * 10 + (uint16_t)(x * 1000) * 1000 +
                (int64_t)(int32_t)(x * -100000) * 100000 +
                (int64_t)((uint32_t)x * 200000000u) * 10000000 + x * 11;
    return x & 1 ? -s : s;
}

static bool
calls_back(const Build *b, int64_t x)
{
    bool ok = gives("each", x, ((I1 *)symbol(b, "i0_each"))(CONTEXT, x), fold_each(x, 3, 1) * 1000 + fold_each(x, 2, 0));
    ok &= gives("hooked", x, ((I1 *)symbol(b, "i0_hooked"))(CONTEXT, x), fold_each(x + 1, 3, 1));
    ok &= gives("same", x, ((I1 *)symbol(b, "i0_same"))(CONTEXT, x), x + 1101);
    ok &= gives("hooks", x, ((I1 *)symbol(b, "i0_hooks"))(CONTEXT, x % 7), (3 * (x % 7) + 1) * 1000000 - 10000 + 5701);
    ok &= gives("call_seven", x, ((I1 *)symbol(b, "i0_call_seven"))(CONTEXT, x), seven_of(x));
    ok &= gives("call_wide", x, ((I1 *)symbol(b, "i0_call_wide"))(CONTEXT, x), 171 * x - 423);
    ok &= gives("call_void", x, ((I1 *)symbol(b, "i0_call_void"))(CONTEXT, x), x + 2);
    return ok;
}

static bool
exports(const Build *b, int64_t x)
{
    int32_t m = (int32_t)(x * -1000);
    uint8_t c = (uint8_t)(x * 13);

    bool ok = gives("cs_add3", x, ((ADD3 *)symbol(b, "cs_add3"))(x, m, c), x + m * 10LL + c * 1000LL);
    ok &= gives_f64("cs_half", x, ((HALF *)symbol(b, "cs_half"))((double)x), (double)x * 0.5);
    ok &= gives("cs_step", x, ((STEP *)symbol(b, "cs_step"))(x), 2 * x + 1);
    ok &= gives("uses", x, ((I1 *)symbol(b, "i0_uses"))(CONTEXT, x % 6), fold_each(x % 6, 2, 1) * 1000 + 2 * (x % 6) + 1);

    struct i0_P p = ((SWAP *)symbol(b, "cs_swap"))((struct i0_P){ x, x + 5 });
    ok &= gives("cs_swap", x, p.x * 1000 + p.y, (x + 5) * 1000 + x);

    int64_t ticks = chibi_cs_ticks;
    ((TICK *)symbol(b, "cs_tick"))();
    ok &= gives("cs_tick", x, chibi_cs_ticks, ticks + 1);
    return ok;
}

TEST_CASE(works_with_c)
{
    Build b;
    REQUIRE(open_build(&b, 0));
    bool ok = add_package(&b, 0, INTEROP, 'i', false) && finish_build(&b, "");
    for (int64_t x = 0; ok && x < 20; x++) {
        ok = calls_c(&b, x);
        ok &= formats(&b, x);
        ok &= sorts(&b, x);
        ok &= calls_back(&b, x);
        ok &= exports(&b, x);
    }

    if (ok) {
        char *args[] = { "a", "bb", "ccc", NULL };
        GET *tap = (GET *)symbol(&b, "i0_get_tap");
        bool same = ((GET *)symbol(&b, "i0_get_step"))(CONTEXT) == (uint8_t *)symbol(&b, "cs_step");
        same &= ((GET *)symbol(&b, "i0_get_twice"))(CONTEXT) == (uint8_t *)chibi_cs_twice;
        same &= ((GET *)symbol(&b, "i0_error_stream"))(CONTEXT) == (uint8_t *)stderr;
        same &= tap(CONTEXT) == tap(CONTEXT) && tap(CONTEXT) != (uint8_t *)symbol(&b, "i0_tap");
        if (!same) {
            fprintf(stderr, "c_source test: callbacks or `stderr` don't have the expected addresses\n");
        }
        ok = gives("main", 3, ((MAIN *)symbol(&b, "main"))(3, (uint8_t *)args), 603) && same;
    }
    close_build(&b, ok);
    REQUIRE(ok);
    return true;
}

typedef int64_t I4(void *, int64_t, int64_t, int64_t, int64_t);

static const char VECTOR_MASK[] =
    "import \"std/simd\"\n"
    "\n"
    "fn mask(a: u8, k: int) -> int {\n"
    "    v: u8x16 = a\n"
    "    w := u8x16.{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 }\n"
    "    return int(simd.top_bits(w +% v > 15)) + k\n"
    "}\n";

static const VectorRun VECTOR_MASK_RESULTS[] = {
    { "mask", { 9, 1 }, 65409, 0 },
    { "mask", { 250, 0 }, 63, 0 },
    { "mask", { 0, 5 }, 5, 0 },
};

static const char VECTOR_PICK[] =
    "import \"std/simd\"\n"
    "\n"
    "fn pick(a: u8, k: int) -> int {\n"
    "    v: u8x16 = a\n"
    "    w := u8x16.{ 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25 }\n"
    "    r := simd.lookup(w, v)\n"
    "    return int(r[k])\n"
    "}\n";

static const VectorRun VECTOR_PICK_RESULTS[] = {
    { "pick", { 3, 0 }, 13, 0 },
    { "pick", { 15, 7 }, 25, 0 },
    { "pick", { 16, 2 }, 0, 0 },
    { "pick", { 200, 1 }, 0, 0 },
};

static int
call_i4(I4 *f, const int64_t *args, int64_t *result)
{
    trap_kind = 0;
    if (setjmp(trap_jump) == 0) {
        *result = f(CONTEXT, args[0], args[1], args[2], args[3]);
        return 0;
    }
    return trap_kind;
}

static bool
runs_agree(const Build *b, u32 k, const VectorRun *runs, size_t n, const char *flags)
{
    bool ok = true;
    for (size_t i = 0; ok && i < n; i++) {
        const VectorRun *r = &runs[i];
        char name[64];
        snprintf(name, sizeof name, "wv%u_%s", k, r->name);
        int64_t args[4] = { r->args[0], r->args[1], r->args[2], 0 };
        int64_t got = 0;
        int trap = call_i4((I4 *)symbol(b, name), args, &got);
        ok = trap == r->trap && (trap || got == r->want);
        if (!ok) {
            fprintf(
                stderr,
                "c_source test: with \"%s\", %s(%lld, %lld, %lld) gave %lld (trap %d), want %lld (trap %d)\n",
                flags,
                r->name,
                (long long)r->args[0],
                (long long)r->args[1],
                (long long)r->args[2],
                (long long)got,
                trap,
                (long long)r->want,
                r->trap
            );
        }
    }
    return ok;
}

static bool
grid_agrees(const Build *b, u32 k, const GridFunction *g, const char *flags)
{
    char name[64];
    snprintf(name, sizeof name, "wv%u_%s", k, g->name);
    I4 *f = (I4 *)symbol(b, name);
    u64 mask = g->lanes == 2 ? ~0ull : (1ull << (128 / g->lanes)) - 1;
    for (int64_t a = 0; a < 16; a++) {
        for (int64_t x = 0; x < 16; x++) {
            for (int64_t op = 0; op < 20; op++) {
                int64_t args[4] = { a, x, op, (a * 3 + x + op) & (g->lanes - 1) };
                int64_t got = 0;
                int trap = call_i4(f, args, &got);
                u64 want = g->oracle(a, x, op, args[3]);
                if (trap || (((u64)got ^ want) & mask)) {
                    fprintf(
                        stderr,
                        "c_source test: with \"%s\", %s(%lld, %lld, %lld, %lld) gave %llu (trap %d), want %llu\n",
                        flags,
                        g->name,
                        (long long)a,
                        (long long)x,
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

TEST_CASE(runs_vectors)
{
#if defined(__x86_64__)
    static const char *const variants[] = { "", " -mssse3", " -DCHIBI_SIMD_GENERIC" };
#else
    static const char *const variants[] = { "", " -DCHIBI_SIMD_GENERIC" };
#endif
    bool ok = true;
    for (u32 v = 0; ok && v < sizeof variants / sizeof variants[0]; v++) {
        Build b;
        REQUIRE(open_build(&b, 3));
        ok = add_package(&b, 0, VECTOR_RUNS, 'v', true) && add_package(&b, 1, VECTOR_GRID, 'v', true) &&
             add_package(&b, 2, VECTOR_BODY, 'v', true) && add_package(&b, 3, VECTOR_MASK, 'v', true) &&
             add_package(&b, 4, VECTOR_MASK, 'v', true) && add_package(&b, 5, VECTOR_PICK, 'v', true) &&
             finish_build(&b, variants[v]);
        ok = ok && runs_agree(&b, 0, VECTOR_RESULTS, NVECTOR_RESULTS, variants[v]);
        ok = ok && runs_agree(&b, 2, VECTOR_BODY_RESULTS, NVECTOR_BODY_RESULTS, variants[v]);
        for (u32 k = 3; ok && k < 5; k++) {
            ok = runs_agree(&b, k, VECTOR_MASK_RESULTS, sizeof VECTOR_MASK_RESULTS / sizeof VECTOR_MASK_RESULTS[0], variants[v]);
        }
        ok = ok && runs_agree(&b, 5, VECTOR_PICK_RESULTS, sizeof VECTOR_PICK_RESULTS / sizeof VECTOR_PICK_RESULTS[0], variants[v]);
        for (size_t f = 0; ok && f < NVECTOR_GRID_FUNCTIONS; f++) {
            ok = grid_agrees(&b, 1, &VECTOR_GRID_FUNCTIONS[f], variants[v]);
        }
        close_build(&b, ok);
    }
    REQUIRE(ok);
    return true;
}

static const char LINES_A[] =
    "fn plain(a: int, b: int) -> int {\n"
    "    x := a + 1\n"
    "    y := x * b\n"
    "\n"
    "    // Lines without code, more than\n"
    "    // the C keeps as empty lines.\n"
    "\n"
    "\n"
    "\n"
    "\n"
    "\n"
    "\n"
    "    return y + b\n"
    "}\n"
    "\n"
    "fn branches(a: int, b: int) -> int {\n"
    "    if a + b > 100 {\n"
    "        return 1\n"
    "    } else if a * b > 100 {\n"
    "        return 2\n"
    "    } else {\n"
    "        return a - b - 1000\n"
    "    }\n"
    "}\n"
    "\n"
    "fn loops(a: int, b: int) -> int {\n"
    "    i := 0\n"
    "    total := 0\n"
    "    while i * b < a {\n"
    "        total += i\n"
    "        i += 1\n"
    "    }\n"
    "    for k in 0..a {\n"
    "        total += k * b\n"
    "    }\n"
    "    return total / (b - 3)\n"
    "}\n"
    "\n"
    "fn arms(a: int, b: int) -> int {\n"
    "    switch a {\n"
    "        1 => return b * 2\n"
    "        2 => {\n"
    "            c := b + 1\n"
    "            return c * c\n"
    "        }\n"
    "        _ => {}\n"
    "    }\n"
    "    v := switch b {\n"
    "        5 => a * 1000\n"
    "        _ => a - b\n"
    "    }\n"
    "    return v - 10\n"
    "}\n"
    "\n"
    "fn spread(a: int, b: int) -> int {\n"
    "    return add(\n"
    "        a,\n"
    "        b * 2,\n"
    "    )\n"
    "}\n"
    "\n"
    "fn add(a: int, b: int) -> int {\n"
    "    return a + b\n"
    "}\n"
    "\n"
    "fn block(a: int, b: int) -> int {\n"
    "    v := blk: {\n"
    "        if a > 0 {\n"
    "            break blk a * b\n"
    "        }\n"
    "        break blk 0\n"
    "    }\n"
    "    return v + 1\n"
    "}\n"
    "\n"
    "fn twice(a: int, b: int) -> int => a * b\n"
    "\n"
    "fn scrutinee(a: int, b: int) -> int {\n"
    "    switch a + b {\n"
    "        0 => return 1\n"
    "        _ => return 2\n"
    "    }\n"
    "}\n"
    "\n"
    "fn effect(a: int, b: int) -> int {\n"
    "    consume(a * b)\n"
    "    return 0\n"
    "}\n"
    "\n"
    "fn consume(x: int) {}\n"
    "\n"
    "fn header(a: int, b: int) -> int {\n"
    "    total := 0\n"
    "    for k in 0..a + b {\n"
    "        total += k\n"
    "    }\n"
    "    return total\n"
    "}\n"
    "\n"
    "fn outer(a: int, b: int) -> int {\n"
    "    return inner(a) + b\n"
    "}\n"
    "\n"
    "fn inner(a: int) => a * 3\n"
    "\n"
    "fn lanes(a: int, b: int) -> int {\n"
    "    v: i64x2 = a; return v[1] * b\n"
    "}\n"
    "\n"
    "fn flat(a: int, b: int) -> int {\n"
    "return later(a) * b\n"
    "}\n"
    "\n"
    "fn later(a: int) => a + 1\n"
    "\n"
    "fn flat2(a: int, b: int) -> int {\n"
    "return a * b\n"
    "}\n";

static const char LINES_B[] =
    "fn pick(a: int, b: int) -> int {\n"
    "    table: [3]int = .[10, 20, 30]\n"
    "    v: int = table[a]\n"
    "    return v * b\n"
    "}\n";

static const char *const LINE_TEXTS[] = { LINES_A, LINES_B };
static const char *const LINE_PATHS[] = {
    "lines/q\"s\\b?\?/\n\303\251.chibi",
    "b.chibi",
};

typedef struct
{
    const char *name;
    int64_t a;
    int64_t b;
    u32 file;
    int line;
    int kind;
} LineTrap;

static const LineTrap LINE_TRAPS[] = {
    { "plain", INT64_MAX, 1, 0, 2, TRAP_OVERFLOW },
    { "plain", 3, INT64_MAX, 0, 3, TRAP_OVERFLOW },
    { "plain", 0, INT64_MAX, 0, 13, TRAP_OVERFLOW },
    { "branches", INT64_MAX, 1, 0, 17, TRAP_OVERFLOW },
    { "branches", 2, INT64_MIN, 0, 19, TRAP_OVERFLOW },
    { "branches", INT64_MIN, 0, 0, 22, TRAP_OVERFLOW },
    { "loops", INT64_MAX, INT64_C(1) << 62, 0, 29, TRAP_OVERFLOW },
    { "loops", 3, INT64_MAX, 0, 34, TRAP_OVERFLOW },
    { "loops", 0, 3, 0, 36, TRAP_DIVIDE },
    { "arms", 1, INT64_MAX, 0, 41, TRAP_OVERFLOW },
    { "arms", 2, INT64_MAX, 0, 43, TRAP_OVERFLOW },
    { "arms", 2, INT64_C(1) << 32, 0, 44, TRAP_OVERFLOW },
    { "arms", INT64_MAX, 5, 0, 49, TRAP_OVERFLOW },
    { "arms", INT64_MIN, 1, 0, 50, TRAP_OVERFLOW },
    { "arms", INT64_MIN + 6, 1, 0, 52, TRAP_OVERFLOW },
    { "spread", 1, INT64_MAX, 0, 56, TRAP_OVERFLOW },
    { "spread", INT64_MAX, 1, 0, 63, TRAP_OVERFLOW },
    { "block", 2, INT64_MAX, 0, 69, TRAP_OVERFLOW },
    { "block", INT64_MAX, 1, 0, 73, TRAP_OVERFLOW },
    { "twice", INT64_MAX, 2, 0, 76, TRAP_OVERFLOW },
    { "scrutinee", INT64_MAX, 1, 0, 79, TRAP_OVERFLOW },
    { "effect", INT64_MAX, 2, 0, 86, TRAP_OVERFLOW },
    { "header", INT64_MAX, 1, 0, 94, TRAP_OVERFLOW },
    { "outer", INT64_MAX, 0, 0, 104, TRAP_OVERFLOW },
    { "outer", 1, INT64_MAX, 0, 101, TRAP_OVERFLOW },
    { "lanes", INT64_MAX, 2, 0, 107, TRAP_OVERFLOW },
    { "flat", 1, INT64_MAX, 0, 111, TRAP_OVERFLOW },
    { "flat", INT64_MAX, 1, 0, 114, TRAP_OVERFLOW },
    { "flat2", INT64_MAX, 2, 0, 117, TRAP_OVERFLOW },
    { "pick", 3, 1, 1, 3, TRAP_BOUNDS },
    { "pick", 1, INT64_MAX, 1, 4, TRAP_OVERFLOW },
};

static const char *volatile trap_file;
static volatile int trap_line;

NORETURN void
chibi_lines_trap(int kind, const char *file, int line)
{
    trap_file = file;
    trap_line = line;
    chibi_trap(kind);
}

TEST_CASE(maps_lines)
{
    Build b;
    REQUIRE(open_build(&b, 1));
    fputs(
        "__attribute__((noreturn)) void\n"
        "chibi_lines_trap(int kind, const char *file, int line);\n"
        "#define chibi_trap(kind) chibi_lines_trap(kind, __FILE__, __LINE__)\n",
        b.outs[0]
    );
    Package p;
    bool ok = check_files(&p, LINE_TEXTS, LINE_PATHS, 2, NULL, false) &&
              c_source_package(b.outs[0], &p.c, "l0_") == NULL;
    release(&p);
    ok = finish_build(&b, "") && ok;

    for (size_t k = 0; ok && k < sizeof LINE_TRAPS / sizeof LINE_TRAPS[0]; k++) {
        const LineTrap *t = &LINE_TRAPS[k];
        char name[32];
        snprintf(name, sizeof name, "l0_%s", t->name);
        trap_file = "";
        trap_line = 0;
        int64_t result;
        int kind = call_i2((I2 *)symbol(&b, name), t->a, t->b, &result);
        ok = kind == t->kind && trap_line == t->line &&
             strcmp(trap_file, LINE_PATHS[t->file]) == 0;
        if (!ok) {
            fprintf(
                stderr,
                "c_source test: %s(%lld, %lld) gave trap %d at %s:%d, want %d at %s:%d\n",
                t->name,
                (long long)t->a,
                (long long)t->b,
                kind,
                trap_file,
                trap_line,
                t->kind,
                LINE_PATHS[t->file],
                t->line
            );
        }
    }
    close_build(&b, ok);
    REQUIRE(ok);
    return true;
}

TEST_CASE(refuses_other_targets)
{
    static const u32 windows[NTARGETS] = { NAME_WINDOWS, TARGET_ARCH_HOST };
    char *text = target_source(TARGETS, windows);
    REQUIRE(text);

    char dir[] = "/tmp/chibi-c-XXXXXX";
    REQUIRE(mkdtemp(dir));
    char source[64], log[64], command[256];
    snprintf(source, sizeof source, "%s/t.c", dir);
    snprintf(log, sizeof log, "%s/t.log", dir);
    snprintf(command, sizeof command, CHIBI_TEST_CC TARGET " -std=c11 -fsyntax-only '%s' 2> '%s'", source, log);

    FILE *out = fopen(source, "w");
    assert(out);
    c_source_prelude(out);
    fputs(text, out);
    fclose(out);
    free(text);
    bool refused = system(command) != 0;

    char said[4096] = "";
    FILE *in = fopen(log, "r");
    assert(in);
    size_t n = fread(said, 1, sizeof said - 1, in);
    said[n] = 0;
    fclose(in);
    unlink(source);
    unlink(log);
    rmdir(dir);

    bool ok = refused && strstr(said, "chibi wrote this C for target_os windows");
    if (!ok) {
        fprintf(stderr, "c_source test: C written for windows gave\n%s", said);
    }
    REQUIRE(ok);
    return true;
}

static const char LAYOUT_HEADER[] =
    "typedef struct { uint16_t z; } C;\n"
    "typedef struct D { uint64_t w; struct D *next; } D;\n"
    "typedef struct E { int8_t v; struct A *back; } E;\n"
    "typedef struct { uint32_t u; } F;\n"
    "typedef struct { uint8_t t; } G;\n"
    "typedef struct { uint64_t s; uint8_t r; } H;\n"
    "typedef struct { int16_t p; } Q;\n"
    "typedef struct { uint16_t o; uint64_t n; } R;\n"
    "typedef struct { uint8_t m; } S;\n"
    "typedef struct A { B b; C cs[3]; D *d; E *e; F (*f)(G *); struct { S *items; uint64_t count; } s; } A;\n";

TEST_CASE(layouts_catch_drift)
{
    static const struct
    {
        const char *b;
        bool refused;
    } cases[] = {
        { "typedef struct { uint8_t x; uint32_t y; } B;\n", false },
        { "typedef struct { uint8_t x; uint16_t y; } B;\n", true },
    };

    char dir[] = "/tmp/chibi-c-XXXXXX";
    REQUIRE(mkdtemp(dir));
    char header[64], source[64], log[64], command[320];
    snprintf(header, sizeof header, "%s/t.h", dir);
    snprintf(source, sizeof source, "%s/t.c", dir);
    snprintf(log, sizeof log, "%s/t.log", dir);
    snprintf(command, sizeof command, CHIBI_TEST_CC TARGET " -std=c11 -Wall -Wextra -Werror -fsyntax-only -include '%s' '%s' 2> '%s'", header, source, log);

    char *text = layouts_of(LAYOUT_WALK);
    bool ok = text != NULL;
    FILE *out = fopen(source, "w");
    assert(out);
    fputs(ok ? text : "", out);
    fclose(out);
    free(text);

    for (size_t k = 0; ok && k < sizeof cases / sizeof cases[0]; k++) {
        FILE *h = fopen(header, "w");
        assert(h);
        fputs("#include <stdint.h>\n\n", h);
        fputs(cases[k].b, h);
        fputs(LAYOUT_HEADER, h);
        fclose(h);
        bool refused = system(command) != 0;

        char said[4096] = "";
        FILE *in = fopen(log, "r");
        assert(in);
        size_t n = fread(said, 1, sizeof said - 1, in);
        said[n] = 0;
        fclose(in);

        ok = refused == cases[k].refused && (!refused || strstr(said, "B.y"));
        if (!ok) {
            fprintf(stderr, "c_source test: layouts against %s gave\n%s", cases[k].b, said);
        }
    }
    unlink(header);
    unlink(source);
    unlink(log);
    rmdir(dir);
    REQUIRE(ok);
    return true;
}

#endif

TEST_SUITE(c_source_tests)
{
    RUN_TEST_CASE(reports_unsupported);
    RUN_TEST_CASE(emits_doubling_structs);
    RUN_TEST_CASE(writes_inline_marks);
    RUN_TEST_CASE(writes_assumptions);
    RUN_TEST_CASE(writes_target_guards);
    RUN_TEST_CASE(writes_layouts);
#ifdef CHIBI_TEST_CC
    RUN_TEST_CASE(agrees_with_reference);
    RUN_TEST_CASE(works_with_c);
    RUN_TEST_CASE(runs_vectors);
    RUN_TEST_CASE(maps_lines);
    RUN_TEST_CASE(refuses_other_targets);
    RUN_TEST_CASE(layouts_catch_drift);
#endif
}
