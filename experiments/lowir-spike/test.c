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
    const char *unsupported = ok ? lowir_unsupported(&p.c) : NULL;
    if (unsupported) {
        fprintf(stderr, "lowir test: %s\n%s", unsupported, text);
        ok = false;
    }

    if (ok) {
        char prefix[32];
        snprintf(prefix, sizeof prefix, "%c%u_", tag, k);
        CppgmLowir *m = cppgm_lowir_create(CPPGM_LOWIR_ABI_VERSION, CPPGM_TARGET_LINUX);
        lowir_text = text;
        lowir_package(m, &p.c, prefix, LOWIR_SHARED);
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

    printf("lowir test at -O%u: %s\n", lowir_level, pu_test_failed() ? "FAILED" : "passed");
    return pu_test_failed();
}
