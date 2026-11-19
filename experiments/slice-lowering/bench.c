#define _DEFAULT_SOURCE

#include "kernels.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(__APPLE__)
#include <libproc.h>
#include <pthread.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

#define INTS 4096
#define BYTES 16384
#define RUNS 11

typedef struct
{
    Ints ints;
    Bytes bytes;
    uint8_t c;
} Input;

typedef int64_t Run(const Input *in);

typedef struct
{
    const char *name;
    Run *run;
} Form;

typedef struct
{
    const char *name;
    const Form *forms;
    int nforms;
    int passes;
    bool per_byte;
    uint8_t c;
} Kernel;

static const char SOURCE[] = "fn collatz(n: int) -> int {\n"
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
                             "}\n";

static int64_t
run_sum_a(const Input *in)
{
    return sum_a(&in->ints);
}

static int64_t
run_sum_c(const Input *in)
{
    return sum_c(in->ints);
}

static int64_t
run_sum_ref(const Input *in)
{
    return sum_ref(in->ints);
}

static int64_t
run_count_a(const Input *in)
{
    return count_a(&in->bytes, in->c);
}

static int64_t
run_count_c(const Input *in)
{
    return count_c(in->bytes, in->c);
}

static int64_t
run_count_ref(const Input *in)
{
    return count_ref(in->bytes, in->c);
}

static int64_t
run_total_a(const Input *in)
{
    return total_a(&in->bytes);
}

static int64_t
run_total_a1(const Input *in)
{
    return total_a1(in->bytes);
}

static int64_t
run_total_c(const Input *in)
{
    return total_c(in->bytes);
}

static int64_t
run_total_ref(const Input *in)
{
    return total_ref(in->bytes);
}

static const Form SUM[] = {
    { "A, in memory", run_sum_a },
    { "C, in two registers", run_sum_c },
    { "clang -O2, same checks", run_sum_ref },
};

static const Form COUNT[] = {
    { "A, in memory", run_count_a },
    { "C, in two registers", run_count_c },
    { "clang -O2, same checks", run_count_ref },
};

static const Form TOTAL[] = {
    { "A, hidden pointer, caller copies", run_total_a },
    { "A, two registers, callee stores", run_total_a1 },
    { "C, in two registers", run_total_c },
    { "clang -O2, same checks", run_total_ref },
};

static uint64_t state = 0x9E3779B97F4A7C15ull;

static uint64_t
next(void)
{
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
}

static uint64_t
now(void)
{
#if defined(__APPLE__)
    struct rusage_info_v4 ri;
    proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri);
    return ri.ri_cycles;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
#endif
}

static double
median(double *v, int n)
{
    for (int i = 1; i < n; i++) {
        double x = v[i];
        int j = i;
        for (; j > 0 && v[j - 1] > x; j--) {
            v[j] = v[j - 1];
        }
        v[j] = x;
    }
    return v[n / 2];
}

static double
cycles_per_tick(void)
{
#if defined(__APPLE__)
    return 1.0;
#else
    double per[RUNS];
    for (int r = 0; r < RUNS; r++) {
        uint64_t t0 = now();
        calibrate(1 << 16);
        uint64_t t1 = now();
        per[r] = 64.0 * (1 << 16) / (double)(t1 - t0);
    }
    return median(per, RUNS);
#endif
}

static bool
agree(const Form *forms, int nforms, const Input *in, int64_t expect, const char *what)
{
    bool ok = true;
    for (int f = 0; f < nforms; f++) {
        int64_t got = forms[f].run(in);
        if (got != expect) {
            fprintf(stderr, "%s, %s: computed %lld, expected %lld\n", what, forms[f].name, (long long)got, (long long)expect);
            ok = false;
        }
    }
    return ok;
}

static bool
check(const int64_t *ints, const uint8_t *text)
{
    static const int64_t lens[] = { 0, 1, 2, 7, 64, 1000 };

    bool ok = true;
    for (size_t k = 0; k < sizeof lens / sizeof lens[0]; k++) {
        int64_t len = lens[k];

        int64_t sum = 0;
        int64_t spaces = 0;
        int64_t bytes = 0;
        for (int64_t i = 0; i < len; i++) {
            sum += ints[i];
            spaces += text[i] == ' ';
            bytes += text[i];
        }

        Input in = { { ints, len }, { text, len }, ' ' };
        ok = agree(SUM, sizeof SUM / sizeof SUM[0], &in, sum, "sum") && ok;
        ok = agree(COUNT, sizeof COUNT / sizeof COUNT[0], &in, spaces, "count ' '") && ok;
        ok = agree(TOTAL, sizeof TOTAL / sizeof TOTAL[0], &in, bytes, "total") && ok;

        in.c = 1;
        ok = agree(COUNT, sizeof COUNT / sizeof COUNT[0], &in, 0, "count 1") && ok;
    }
    return ok;
}

static double
measure(Run *run, const Input *in, int64_t elements, int passes)
{
    int64_t expect = run(in);

    double per[RUNS];
    for (int r = 0; r < RUNS; r++) {
        int64_t total = 0;
        uint64_t t0 = now();
        for (int p = 0; p < passes; p++) {
            total += run(in);
        }
        uint64_t t1 = now();
        if (total != expect * passes) {
            fprintf(stderr, "result changed between passes\n");
            exit(1);
        }
        per[r] = (double)(t1 - t0) / ((double)elements * passes);
    }
    return median(per, RUNS);
}

int
main(int argc, char **argv)
{
#if defined(__APPLE__)
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
    int64_t *ints = malloc(INTS * sizeof *ints);
    for (int i = 0; i < INTS; i++) {
        ints[i] = (int64_t)(next() % 1000);
    }

    uint8_t *text = malloc(BYTES);
    for (int i = 0; i < BYTES; i++) {
        text[i] = (uint8_t)SOURCE[i % (int)(sizeof SOURCE - 1)];
    }

    if (!check(ints, text)) {
        return 1;
    }
    printf("every form agrees with the C reference\n");
    if (argc > 1 && strcmp(argv[1], "check") == 0) {
        return 0;
    }

    const Kernel kernels[] = {
        { "sum of a []int, indexed (per element)", SUM, sizeof SUM / sizeof SUM[0], 1000, false, 0 },
        { "count of a byte that never matches, re-sliced (per byte)", COUNT, sizeof COUNT / sizeof COUNT[0], 200, true, 1 },
        { "count of ' ' in source text, re-sliced (per byte)", COUNT, sizeof COUNT / sizeof COUNT[0], 200, true, ' ' },
        { "total of first(rest), one call per byte (per byte)", TOTAL, sizeof TOTAL / sizeof TOTAL[0], 100, true, 0 },
    };

    double scale = cycles_per_tick();
#if defined(__APPLE__)
    printf("cycles from the process's cycle counter\n");
#else
    printf("cycles estimated from wall time: a chain of dependent adds runs at %.2f GHz\n", scale);
#endif

    for (size_t k = 0; k < sizeof kernels / sizeof kernels[0]; k++) {
        const Kernel *kn = &kernels[k];
        Input in = { { ints, INTS }, { text, BYTES }, kn->c };
        int64_t elements = kn->per_byte ? BYTES : INTS;

        printf("\n%s\n", kn->name);
        double base = 0;
        for (int f = 0; f < kn->nforms; f++) {
            double cycles = measure(kn->forms[f].run, &in, elements, kn->passes) * scale;
            if (f == 0) {
                base = cycles;
            }
            printf("  %-34s %6.2f cycles  %5.2fx of A\n", kn->forms[f].name, cycles, cycles / base);
        }
    }

#if !defined(__APPLE__)
    printf("\nthe chain of adds ran at %.2f GHz after the runs\n", cycles_per_tick());
#endif
    return 0;
}
