#define PICO_UNIT_IMPLEMENTATION
#define CHIBI_TEST_CC "cc"

#include "../../test/c_source_test.c"
#include "lowir.h"
#include "lowir_interface.h"

#include <sys/wait.h>

static u32 lowir_level = 2;
static const char *lowir_text;
static const char *crash_dir;
static u32 crashes;

static void
show_program(void)
{
    if (lowir_text) {
        fprintf(stderr, "lowir test: while translating\n%s", lowir_text);
    }
}

static bool
open_objects(Build *b)
{
    *b = (Build){ .dir = "/tmp/chibi-l-XXXXXX" };
    return mkdtemp(b->dir) != NULL;
}

static bool
write_package(const Build *b, CppgmLowir *m, const char *prefix)
{
    char path[64];
    snprintf(path, sizeof path, "%s/%s.o", b->dir, prefix);
    FILE *f = fopen(path, "wb");
    assert(f);
    size_t size = (size_t)cppgm_lowir_object_size(m);
    bool ok = fwrite(cppgm_lowir_object_data(m), 1, size, f) == size;
    return fclose(f) == 0 && ok;
}

static bool
add_object(const Build *b, u32 k, const char *text, char tag)
{
    Package p;
    bool ok = check_source(&p, text);
    if (ok) {
        char prefix[32];
        snprintf(prefix, sizeof prefix, "%c%u_", tag, k);
        CppgmLowir *m = cppgm_lowir_create(CPPGM_LOWIR_ABI_VERSION, CPPGM_TARGET_LINUX);
        lowir_text = text;
        lowir_package(m, &p.c, prefix, LOWIR_SHARED, "");
        lowir_text = NULL;
        ok = cppgm_lowir_validate(m) &&
             cppgm_lowir_compile(m, lowir_level, CPPGM_CODE_PIC);
        if (!ok) {
            fprintf(stderr, "lowir test: %s\n%s", cppgm_lowir_error(m), text);
        }
        ok = ok && write_package(b, m, prefix);
        cppgm_lowir_destroy(m);
    }
    release(&p);
    return ok;
}

static void
put_prototypes(FILE *out, const Package *p, const char *prefix)
{
    const Checker *c = &p->c;
    for (u32 f = 0; f < c->ir.nfuncs; f++) {
        const IrFunc *fn = &c->ir.funcs[f];
        const IrSlot *slots = c->ir.code.slots + fn->first_slot;
        const char *result = integer_name((u32)c->types.arg1[c->decls.types[fn->decl]]);
        bool plain = fn->nparams <= 4 && result;
        for (u32 s = 0; s < fn->nparams; s++) {
            plain &= integer_name(slots[s].type) != NULL;
        }
        if (!plain) {
            continue;
        }

        Name n = names_get(&p->lx.names, c->decls.names[fn->decl]);
        fprintf(out, "%s %s%.*s(void *ctx", result, prefix, (int)n.len, n.text);
        for (u32 s = 0; s < fn->nparams; s++) {
            fprintf(out, ", %s", integer_name(slots[s].type));
        }
        fputs(");\n", out);
    }
}

static bool
add_wrappers(const Build *b, u32 k, const char *text, char tag)
{
    Package p;
    bool ok = check_source(&p, text);
    char prefix[32];
    snprintf(prefix, sizeof prefix, "%c%u_", tag, k);
    char path[64];
    snprintf(path, sizeof path, "%s/w%s.c", b->dir, prefix);
    FILE *f = ok ? fopen(path, "w") : NULL;
    if (f) {
        fputs("#include <stdint.h>\n\n", f);
        put_prototypes(f, &p, prefix);
        put_wrappers(f, &p, prefix);
        ok = fclose(f) == 0;
    }
    release(&p);

    char command[192];
    snprintf(command, sizeof command, "cc -c -O1 -o %s/w%s.o %s", b->dir, prefix, path);
    ok = ok && system(command) == 0;
    if (!ok) {
        fprintf(stderr, "lowir test: can't build the wrappers in %s\n", path);
    }
    return ok;
}

static bool
add_isolated(const Build *b, u32 k, const char *text, char tag, u64 seed, bool *crashed)
{
    *crashed = false;
    if (!crash_dir) {
        return add_object(b, k, text, tag);
    }

    fflush(NULL);
    pid_t child = fork();
    if (child == 0) {
        _exit(add_object(b, k, text, tag) ? 0 : 1);
    }
    int status = 0;
    waitpid(child, &status, 0);
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status) == 0;
    }

    char path[256];
    snprintf(path, sizeof path, "%s/crash-%llu.chibi", crash_dir, (unsigned long long)seed);
    FILE *f = fopen(path, "w");
    assert(f);
    fputs(text, f);
    fclose(f);
    crashes++;
    *crashed = true;
    return true;
}

static bool
link_objects(Build *b)
{
    char command[256];
    snprintf(command, sizeof command, "cd %s && cc -shared -o programs.so *.o", b->dir);
    if (system(command) != 0) {
        fprintf(stderr, "lowir test: `%s` failed\n", command);
        return false;
    }

    char path[64];
    snprintf(path, sizeof path, "%s/programs.so", b->dir);
    b->library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!b->library) {
        fprintf(stderr, "lowir test: %s\n", dlerror());
    }
    return b->library != NULL;
}

static void
close_objects(Build *b, bool ok)
{
    if (b->library) {
        dlclose(b->library);
    }
    if (!ok) {
        fprintf(stderr, "lowir test: the objects are in %s\n", b->dir);
        return;
    }

    char command[64];
    snprintf(command, sizeof command, "rm -rf %s", b->dir);
    if (system(command) != 0) {
        fprintf(stderr, "lowir test: `%s` failed\n", command);
    }
}

TEST_CASE(lowir_agrees_with_reference)
{
    const char *env = getenv("CHIBI_DIFFERENTIAL");
    const char *seed_text = getenv("CHIBI_DIFFERENTIAL_SEED");
    u32 programs = env ? (u32)atoi(env) : 60;
    u64 rng = seed_text ? strtoull(seed_text, NULL, 10) : 0x5EED;
    Build b;
    REQUIRE(open_objects(&b));

    char *text = malloc(PROGRAM_SIZE);
    u64 *seeds = malloc((programs + 1) * sizeof *seeds);
    bool *crashed = calloc(programs + 1, sizeof *crashed);
    u32 grid = NBACKEND_PROGRAMS + (u32)(sizeof C_PROGRAMS / sizeof C_PROGRAMS[0]);
    bool ok = true;
    for (u32 k = 0; ok && k < grid; k++) {
        ok = add_object(&b, k, grid_program(k), 'g');
    }
    for (u32 k = 0; ok && k < programs; k++) {
        seeds[k] = rng;
        size_t len = random_typed_program(&rng, text);
        text[len] = 0;
        ok = add_isolated(&b, k, text, 'p', seeds[k], &crashed[k]);
    }
    ok = ok && link_objects(&b);

    u32 tested = 0;
    for (u32 k = 0; ok && k < grid; k++) {
        Package p;
        ok = check_source(&p, grid_program(k)) && agree_on_grid(&b, &p, k, &tested);
        release(&p);
    }
    if (ok && tested != GRID_FUNCTIONS) {
        fprintf(stderr, "lowir test: %u functions take two ints, expected %u\n", tested, (u32)GRID_FUNCTIONS);
        ok = false;
    }

    u64 args = rng;
    u32 wrong = 0;
    for (u32 k = 0; ok && k < programs; k++) {
        if (crashed[k]) {
            continue;
        }
        u64 r = seeds[k];
        size_t len = random_typed_program(&r, text);
        text[len] = 0;
        Package p;
        bool agrees = check_source(&p, text) &&
                      agree_on_random(&b, &p, k, seeds[k], &args, text);
        release(&p);
        wrong += !agrees;
        ok = agrees || crash_dir;
    }
    if (wrong) {
        fprintf(stderr, "lowir test: %u of %u random programs disagree with the reference\n", wrong, programs);
        ok = false;
    }
    if (crashes) {
        fprintf(stderr, "lowir test: %u of %u random programs crashed the compiler; they are in %s\n", crashes, programs, crash_dir);
        ok = false;
    }
    free(crashed);
    free(seeds);
    free(text);
    close_objects(&b, ok);
    REQUIRE(ok);
    return true;
}

TEST_CASE(lowir_works_with_c)
{
    Build b;
    REQUIRE(open_objects(&b));
    bool ok = add_object(&b, 0, INTEROP, 'i') && link_objects(&b);
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
        bool same = ((GET *)symbol(&b, "i0_get_step"))(CONTEXT) ==
                    (uint8_t *)symbol(&b, "cs_step");
        same &= ((GET *)symbol(&b, "i0_get_twice"))(CONTEXT) == (uint8_t *)chibi_cs_twice;
        same &= tap(CONTEXT) == tap(CONTEXT) &&
                tap(CONTEXT) != (uint8_t *)symbol(&b, "i0_tap");
        if (!same) {
            fprintf(stderr, "lowir test: callbacks don't have the expected addresses\n");
        }
        ok = gives("main", 3, ((MAIN *)symbol(&b, "main"))(3, (uint8_t *)args), 603) &&
             same;
    }
    close_objects(&b, ok);
    REQUIRE(ok);
    return true;
}

static const char WIDE_EDGES[] =
    "import \"std/bits\"\n"
    "\n"
    "fn fold(x: i128) -> int {\n"
    "    u := bitcast(u128, x)\n"
    "    return bitcast(int, u64(u & 0xFFFF_FFFF_FFFF_FFFF)) ^ bitcast(int, u64(u >> 64))\n"
    "}\n"
    "\n"
    "fn signed_from_float(a: int, b: int) -> int {\n"
    "    k := a & 3\n"
    "    d: f64 = -170141183460469231731687303715884105728.0\n"
    "    if k == 1 {\n"
    "        d = d * (1.0 + 1.0 / 4503599627370496.0)\n"
    "    }\n"
    "    if k == 2 {\n"
    "        d = -d - 37778931862957161709568.0\n"
    "    }\n"
    "    if k == 3 {\n"
    "        d = -d\n"
    "    }\n"
    "    return fold(i128(d)) ^ b\n"
    "}\n"
    "\n"
    "fn unsigned_from_float(a: int, b: int) -> int {\n"
    "    k := a & 3\n"
    "    d: f64 = 340282366920938463463374607431768211456.0\n"
    "    if k == 1 {\n"
    "        d = d - 37778931862957161709568.0\n"
    "    }\n"
    "    if k == 2 {\n"
    "        d = -0.5\n"
    "    }\n"
    "    if k == 3 {\n"
    "        d = -1.0\n"
    "    }\n"
    "    return fold(bitcast(i128, u128(d))) ^ b\n"
    "}\n"
    "\n"
    "fn negate_min(a: int, b: int) -> int {\n"
    "    x := i128(-0x8000_0000_0000_0000_0000_0000_0000_0000) + i128(a & 1)\n"
    "    return fold(-x) ^ b\n"
    "}\n"
    "\n"
    "fn divide_min(a: int, b: int) -> int {\n"
    "    x := i128(-0x8000_0000_0000_0000_0000_0000_0000_0000) + i128(a & 1)\n"
    "    return fold(x / -1) ^ b\n"
    "}\n"
    "\n"
    "fn divide_low_ones(a: int, b: int) -> int {\n"
    "    x := i128(-0x8000_0000_0000_0000_0000_0000_0000_0000) + i128(a & 1)\n"
    "    return fold(x / 0xFFFF_FFFF_FFFF_FFFF) ^ fold(x % 0xFFFF_FFFF_FFFF_FFFF) ^ b\n"
    "}\n"
    "\n"
    "fn remainder_min(a: int, b: int) -> int {\n"
    "    x := i128(-0x8000_0000_0000_0000_0000_0000_0000_0000) + i128(a & 1)\n"
    "    y := i128(b) - i128(b) - 1\n"
    "    return fold(x % y) ^ fold(x % -1) ^ b\n"
    "}\n"
    "\n"
    "fn narrow_signed(a: int, b: int) -> int {\n"
    "    x := i128(0xFFFF_FFFF_FFFF_FFFF) + i128(a & 1)\n"
    "    y := i128(-0x8000_0000_0000_0000) - i128(b & 1)\n"
    "    return bitcast(int, u64(x)) ^ int(y)\n"
    "}\n"
    "\n"
    "fn multiply_highs(a: int, b: int) -> int {\n"
    "    x := (u128(a & 1) << 64) + u128(b & 7)\n"
    "    y := (u128(b & 1) << 64) + 1\n"
    "    return fold(bitcast(i128, x * y)) ^ a\n"
    "}\n"
    "\n"
    "fn multiply_carry(a: int, b: int) -> int {\n"
    "    x := u128(0xFFFF_FFFF_FFFF_FFFF)\n"
    "    y := (u128(1) << 64) + 1 + u128(a & 1)\n"
    "    return fold(bitcast(i128, x * y)) ^ b\n"
    "}\n"
    "\n"
    "fn count_narrow(a: int, b: int) -> int {\n"
    "    x := u128(a & 1) << 100\n"
    "    return int(i8(bits.leading_zeros(x))) ^ int(u8(bits.trailing_zeros(x))) ^ b\n"
    "}\n"
    "\n"
    "fn narrow_unsigned(a: int, b: int) -> int {\n"
    "    x := u128(0x7FFF_FFFF_FFFF_FFFF) + u128(a & 1)\n"
    "    y := u128(0xFFFF_FFFF_FFFF_FFFF) + u128(b & 1)\n"
    "    return int(x) ^ bitcast(int, u64(y))\n"
    "}\n";

TEST_CASE(lowir_passes_arrays)
{
    Build b;
    REQUIRE(open_objects(&b));
    bool ok = add_object(&b, 0, ARRAY_ARGS, 'g') && link_objects(&b);

    u32 tested = 0;
    if (ok) {
        Package p;
        ok = check_source(&p, ARRAY_ARGS) && agree_on_grid(&b, &p, 0, &tested);
        release(&p);
    }
    if (ok && tested != 15) {
        fprintf(stderr, "lowir test: %u array functions take two ints, expected 15\n", tested);
        ok = false;
    }
    close_objects(&b, ok);
    REQUIRE(ok);
    return true;
}

TEST_CASE(lowir_converts_wide_edges)
{
    Build b;
    REQUIRE(open_objects(&b));
    bool ok = add_object(&b, 0, WIDE_EDGES, 'g') && link_objects(&b);

    u32 tested = 0;
    if (ok) {
        Package p;
        ok = check_source(&p, WIDE_EDGES) && agree_on_grid(&b, &p, 0, &tested);
        release(&p);
    }
    if (ok && tested != 11) {
        fprintf(stderr, "lowir test: %u 128-bit edge functions take two ints, expected 11\n", tested);
        ok = false;
    }
    close_objects(&b, ok);
    REQUIRE(ok);
    return true;
}

TEST_CASE(lowir_handles_wide_error_codes)
{
    char *text = wide_errors_program();
    Build b;
    REQUIRE(open_objects(&b));
    bool ok = add_object(&b, 0, text, 'g') && link_objects(&b);

    u32 tested = 0;
    if (ok) {
        Package p;
        ok = check_source(&p, text) && agree_on_grid(&b, &p, 0, &tested);
        release(&p);
    }
    if (ok && tested != 2) {
        fprintf(stderr, "lowir test: %u wide error functions take two ints, expected 2\n", tested);
        ok = false;
    }
    close_objects(&b, ok);
    free(text);
    REQUIRE(ok);
    return true;
}

TEST_CASE(lowir_runs_vectors)
{
    const char *const programs[] = { VECTOR_RUNS, VECTOR_GRID, VECTOR_BODY, VECTOR_MASK,
                                     VECTOR_MASK, VECTOR_PICK, VECTOR_WIDE_RUNS, VECTOR_WIDE_GRID };
    Build b;
    REQUIRE(open_objects(&b));
    bool ok = true;
    for (u32 k = 0; ok && k < sizeof programs / sizeof programs[0]; k++) {
        ok = add_object(&b, k, programs[k], 'v') &&
             add_wrappers(&b, k, programs[k], 'v');
    }
    ok = ok && link_objects(&b);

    ok = ok && runs_agree(&b, 0, VECTOR_RESULTS, NVECTOR_RESULTS, "lowir");
    ok = ok && runs_agree(&b, 2, VECTOR_BODY_RESULTS, NVECTOR_BODY_RESULTS, "lowir");
    for (u32 k = 3; ok && k < 5; k++) {
        ok = runs_agree(
            &b,
            k,
            VECTOR_MASK_RESULTS,
            sizeof VECTOR_MASK_RESULTS / sizeof VECTOR_MASK_RESULTS[0],
            "lowir"
        );
    }
    ok = ok && runs_agree(
                   &b,
                   5,
                   VECTOR_PICK_RESULTS,
                   sizeof VECTOR_PICK_RESULTS / sizeof VECTOR_PICK_RESULTS[0],
                   "lowir"
               );
    ok = ok && runs_agree(&b, 6, VECTOR_WIDE_RESULTS, NVECTOR_WIDE_RESULTS, "lowir");
    for (size_t f = 0; ok && f < NVECTOR_GRID_FUNCTIONS; f++) {
        ok = grid_agrees(&b, 1, &VECTOR_GRID_FUNCTIONS[f], "lowir");
    }
    for (size_t f = 0; ok && f < NVECTOR_WIDE_GRID_FUNCTIONS; f++) {
        ok = grid_agrees(&b, 7, &VECTOR_WIDE_GRID_FUNCTIONS[f], "lowir");
    }
    close_objects(&b, ok);
    REQUIRE(ok);
    return true;
}

int
main(void)
{
    const char *level = getenv("CHIBI_LOWIR_LEVEL");
    lowir_level = level ? (u32)atoi(level) : 2;
    crash_dir = getenv("CHIBI_LOWIR_CRASHES");
    atexit(show_program);
    pu_display_quiet(true);

    RUN_TEST_CASE(lowir_agrees_with_reference);
    RUN_TEST_CASE(lowir_works_with_c);
    RUN_TEST_CASE(lowir_passes_arrays);
    RUN_TEST_CASE(lowir_converts_wide_edges);
    RUN_TEST_CASE(lowir_handles_wide_error_codes);
    RUN_TEST_CASE(lowir_runs_vectors);

    printf("lowir test at -O%u: %s\n", lowir_level, pu_test_failed() ? "FAILED" : "passed");
    return pu_test_failed();
}
