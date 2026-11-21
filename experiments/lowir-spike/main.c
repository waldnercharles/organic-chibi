#include "../../src/driver.c"
#include "lowir.h"
#include "lowir_interface.h"

#include <time.h>

static const char LOWIR_USAGE[] = "usage: chibi-lowir lowir [-o program] "
                                  "[-O0..3] [-c] [--wrap] "
                                  "[--shared] [--ir file.lowir] [--times] "
                                  "file.chibi... [-- linker arguments]\n"
                                  "       chibi-lowir runtime\n";

typedef struct
{
    const char *output;
    const char *ir;
    const char **files;
    u32 nfiles;
    char **link_args;
    u32 nlink_args;
    u32 level;
    u32 flags;
    bool object;
    bool times;
} LowirOptions;

static double
seconds(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

static bool
parse_lowir_options(FILE *err, LowirOptions *o, int argc, char **argv)
{
    *o = (LowirOptions){ .files = malloc((size_t)argc * sizeof *o->files), .level = 2 };
    for (int i = 2; i < argc; i++) {
        const char *a = argv[i];
        if ((strcmp(a, "-o") == 0 || strcmp(a, "--ir") == 0) && i + 1 < argc) {
            *(a[1] == 'o' ? &o->output : &o->ir) = argv[++i];
        } else if (a[0] == '-' && a[1] == 'O' && a[2] >= '0' && a[2] <= '3' && !a[3]) {
            o->level = (u32)(a[2] - '0');
        } else if (strcmp(a, "-c") == 0) {
            o->object = true;
        } else if (strcmp(a, "--wrap") == 0) {
            o->flags |= LOWIR_WRAP;
        } else if (strcmp(a, "--shared") == 0) {
            o->flags |= LOWIR_SHARED;
        } else if (strcmp(a, "--times") == 0) {
            o->times = true;
        } else if (strcmp(a, "--") == 0) {
            o->link_args = argv + i + 1;
            o->nlink_args = (u32)(argc - i - 1);
            break;
        } else if (a[0] == '-') {
            fprintf(err, "chibi-lowir: unknown option %s\n%s", a, LOWIR_USAGE);
            return false;
        } else {
            o->files[o->nfiles++] = a;
        }
    }
    if (o->nfiles == 0 || !o->output) {
        fputs(LOWIR_USAGE, err);
        return false;
    }
    return true;
}

static bool
write_object(FILE *err, const CppgmLowir *m, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(err, "chibi-lowir: %s: cannot create\n", path);
        return false;
    }

    size_t size = (size_t)cppgm_lowir_object_size(m);
    bool ok = fwrite(cppgm_lowir_object_data(m), 1, size, f) == size;
    ok = fclose(f) == 0 && ok;
    if (!ok) {
        fprintf(err, "chibi-lowir: %s: cannot write\n", path);
    }
    return ok;
}

static bool
link_program(FILE *err, const char *object, const LowirOptions *o)
{
    char *command = NULL;
    size_t size = 0;
    FILE *m = open_memstream(&command, &size);
    const char *cc = getenv("CC");
    fprintf(m, "%s -O2 -w -o", cc && *cc ? cc : "cc");
    put_quoted(m, o->output);
    fputs(" -x c - -x none", m);
    put_quoted(m, object);
    for (u32 i = 0; i < o->nlink_args; i++) {
        put_quoted(m, o->link_args[i]);
    }
    fclose(m);

    FILE *pipe = popen(command, "w");
    int status = -1;
    if (pipe) {
        c_source_runtime(pipe);
        status = pclose(pipe);
    }
    if (status != 0) {
        fprintf(err, "chibi-lowir: the linker failed: %s\n", command);
    }
    free(command);
    return status == 0;
}

static bool
lowir_build(FILE *err, const Checker *c, const LowirOptions *o, double *times)
{
    CppgmLowir *m = cppgm_lowir_create(CPPGM_LOWIR_ABI_VERSION, CPPGM_TARGET_LINUX);
    double t0 = seconds();
    lowir_package(m, c, "p_", o->flags);
    double t1 = seconds();
    bool ok = cppgm_lowir_validate(m) && (!o->ir || cppgm_lowir_write(m, o->ir));
    double t2 = seconds();
    ok = ok && cppgm_lowir_compile(m, o->level, CPPGM_CODE_PIC);
    double t3 = seconds();
    if (!ok) {
        fprintf(err, "chibi-lowir: %s\n", cppgm_lowir_error(m));
        cppgm_lowir_destroy(m);
        return false;
    }

    times[1] = t1 - t0;
    times[2] = t2 - t1;
    times[3] = t3 - t2;
    if (o->object) {
        ok = write_object(err, m, o->output);
        cppgm_lowir_destroy(m);
        return ok;
    }

    size_t n = strlen(o->output);
    char *object = malloc(n + 3);
    memcpy(object, o->output, n);
    memcpy(object + n, ".o", 3);
    ok = write_object(err, m, object);
    cppgm_lowir_destroy(m);
    double t4 = seconds();
    ok = ok && link_program(err, object, o);
    times[4] = seconds() - t4;
    unlink(object);
    free(object);
    return ok;
}

static int
lowir_command(FILE *err, int argc, char **argv)
{
    LowirOptions o;
    Program p = { 0 };
    double times[5] = { 0 };
    double t0 = seconds();
    bool ok = parse_lowir_options(err, &o, argc, argv) &&
              check_program(err, &p, o.files, o.nfiles);
    times[0] = seconds() - t0;

    const char *unsupported = ok ? lowir_unsupported(&p.c) : NULL;
    if (unsupported) {
        fprintf(err, "chibi-lowir: %s\n", unsupported);
        ok = false;
    }
    ok = ok && (o.object || has_main(err, &p));
    ok = ok && lowir_build(err, &p.c, &o, times);
    if (ok && o.times) {
        fprintf(
            err,
            "front %.2f ms  translate %.2f ms  validate %.2f ms  compile %.2f "
            "ms  link %.2f ms\n",
            times[0] * 1e3,
            times[1] * 1e3,
            times[2] * 1e3,
            times[3] * 1e3,
            times[4] * 1e3
        );
    }

    if (p.checked) {
        check_free(&p.c);
    }
    arena_free(&p.arena);
    free(o.files);
    return !ok;
}

int
main(int argc, char **argv)
{
    const char *command = argc > 1 ? argv[1] : "";
    if (strcmp(command, "lowir") == 0) {
        return lowir_command(stderr, argc, argv);
    }
    if (argc == 2 && strcmp(command, "runtime") == 0) {
        c_source_runtime(stdout);
        return 0;
    }
    return driver_run(stdout, stderr, argc, argv);
}
