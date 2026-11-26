#define _DEFAULT_SOURCE

#include "../../src/jit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#ifdef __APPLE__
#include <libproc.h>
#include <pthread.h>
#include <sys/resource.h>
#else
#include <linux/perf_event.h>
#include <sys/syscall.h>
#endif

#define RUNS 11

enum
{
    LEX_PARSE,
    CHECK,
    EMIT,
    TOTAL,
    NPHASES,
};

static const char *const phase_names[NPHASES] = { "lex + parse", "check", "arm64", "all four" };

#ifndef __APPLE__
static int perf_group = -1;

static void
open_counters(void)
{
    struct perf_event_attr a = {
        .type = PERF_TYPE_HARDWARE,
        .size = sizeof a,
        .config = PERF_COUNT_HW_CPU_CYCLES,
        .read_format = PERF_FORMAT_GROUP,
        .exclude_kernel = 1,
        .exclude_hv = 1,
    };
    perf_group = (int)syscall(SYS_perf_event_open, &a, 0, -1, -1, 0);
    if (perf_group < 0) {
        return;
    }

    a.config = PERF_COUNT_HW_INSTRUCTIONS;
    syscall(SYS_perf_event_open, &a, 0, -1, perf_group, 0);
}
#endif

static u64
cpu_cycles(u64 *instructions)
{
#ifdef __APPLE__
    struct rusage_info_v4 ri;
    proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri);
    *instructions = ri.ri_instructions;
    return ri.ri_cycles;
#else
    u64 v[3] = { 0, 0, 0 };
    if (perf_group < 0 || read(perf_group, v, sizeof v) < 0) {
        *instructions = 0;
        return 0;
    }
    *instructions = v[2];
    return v[1];
#endif
}

static double
now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
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

static void
wait_turn(int fd)
{
    char token;
    if (read(fd, &token, 1) != 1) {
        fprintf(stderr, "arm64-backend: the other bench stopped\n");
        exit(1);
    }
}

static void
pass_turn(int fd)
{
    if (write(fd, "t", 1) != 1) {
        exit(1);
    }
}

int
main(int argc, char **argv)
{
#ifdef __APPLE__
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#else
    open_counters();
#endif

    const char *lockstep = getenv("CHIBI_BENCH_LOCKSTEP");
    int turn_in = -1;
    int turn_out = -1;
    if (lockstep) {
        sscanf(lockstep, "%d %d", &turn_in, &turn_out);
    }

    int n = argc - 1;
    Arena sources = { 0 };
    char **srcs = calloc((size_t)n, sizeof *srcs);
    size_t *lens = calloc((size_t)n, sizeof *lens);
    size_t total = 0;
    u64 lines = 0;
    for (int i = 0; i < n; i++) {
        FILE *f = fopen(argv[i + 1], "rb");
        fseek(f, 0, SEEK_END);
        long len = ftell(f);
        fseek(f, 0, SEEK_SET);
        srcs[i] = arena_alloc(&sources, (size_t)len + LEXER_SOURCE_PADDING, 16);
        if (fread(srcs[i], 1, (size_t)len, f) != (size_t)len) {
            return 1;
        }
        memset(srcs[i] + len, 0, LEXER_SOURCE_PADDING);
        lens[i] = (size_t)len;
        total += (size_t)len;
        for (long j = 0; j < len; j++) {
            lines += srcs[i][j] == '\n';
        }
        fclose(f);
    }

    double cycles[NPHASES][RUNS], seconds[NPHASES][RUNS], instructions[NPHASES][RUNS];
    u64 i0, i1, i2, i3;
    u64 ir_count = 0, words = 0, nodes = 0;
    u32 diags = 0;
    ParsedFile *files = calloc((size_t)n, sizeof *files);
    for (int r = 0; r < RUNS; r++) {
        if (lockstep) {
            wait_turn(turn_in);
        }

        double t0 = now();
        u64 c0 = cpu_cycles(&i0);
        Arena arena = { 0 };
        Lexer lx;
        lexer_init(&lx, &arena, total);
        nodes = 0;
        for (int i = 0; i < n; i++) {
            Tokens tokens = lexer_alloc_tokens(&arena, lens[i]);
            int ntok = lexer_tokenize(&lx, srcs[i], tokens);
            Nodes out = parser_alloc_nodes(&arena, ntok);
            Parser p;
            int count = parser_parse(&p, tokens, ntok, out);
            if (ntok == 0 || p.error) {
                fprintf(stderr, "arm64-backend: %s does not parse\n", argv[i + 1]);
                return 1;
            }
            files[i] = (ParsedFile){ argv[i + 1], srcs[i],          tokens, out,
                                     (u32)count,  (u32)p.top_count, false };
            nodes += (u64)count;
        }
        double t1 = now();
        u64 c1 = cpu_cycles(&i1);

        Checker c;
        check_init(&c, &lx.names, files, (u32)n);
        check_package(&c);
        double t2 = now();
        u64 c2 = cpu_cycles(&i2);
        diags = c.ndiags;
        if (diags) {
            check_print_diagnostics(stderr, &c);
            return 1;
        }

        Native native;
        if (!jit_compile(&native, &c, CONVENTION_HOST)) {
            fprintf(stderr, "arm64-backend: %s\n", native.error);
            return 1;
        }
        double t3 = now();
        u64 c3 = cpu_cycles(&i3);
        ir_count = c.ir.code.count;
        words = native.code_size / sizeof(u32);

        native_free(&native);
        check_free(&c);
        arena_free(&arena);
        if (lockstep) {
            pass_turn(turn_out);
        }

        cycles[LEX_PARSE][r] = (double)(c1 - c0);
        cycles[CHECK][r] = (double)(c2 - c1);
        cycles[EMIT][r] = (double)(c3 - c2);
        cycles[TOTAL][r] = (double)(c3 - c0);
        instructions[LEX_PARSE][r] = (double)(i1 - i0);
        instructions[CHECK][r] = (double)(i2 - i1);
        instructions[EMIT][r] = (double)(i3 - i2);
        instructions[TOTAL][r] = (double)(i3 - i0);
        seconds[LEX_PARSE][r] = t1 - t0;
        seconds[CHECK][r] = t2 - t1;
        seconds[EMIT][r] = t3 - t2;
        seconds[TOTAL][r] = t3 - t0;
    }

    if (lockstep) {
        for (int r = 0; r < RUNS; r++) {
            printf("run");
            for (int k = 0; k < NPHASES; k++) {
                printf(" %.4f %.0f", seconds[k][r] * 1e3, cycles[k][r]);
            }
            printf("\n");
        }
    }

    printf(
        "arm64-backend: %d files, %llu lines, %llu nodes, %llu IR instructions "
        "(%.2f per line), %llu arm64 instructions (%.2f per IR instruction)\n",
        n,
        (unsigned long long)lines,
        (unsigned long long)nodes,
        (unsigned long long)ir_count,
        (double)ir_count / lines,
        (unsigned long long)words,
        (double)words / ir_count
    );
    for (int k = 0; k < NPHASES; k++) {
        double cy = median(cycles[k], RUNS), sec = median(seconds[k], RUNS),
               in = median(instructions[k], RUNS);
        printf(
            "  %-12s %7.1f cycles per line, %6.2f M lines per second, %6.1f ms",
            phase_names[k],
            cy / lines,
            lines / sec / 1e6,
            sec * 1e3
        );
        if (k == EMIT) {
            printf(", %.1f cycles and %.1f instructions per IR instruction", cy / ir_count, in / ir_count);
        }
        printf(", IPC %.2f\n", in / cy);
    }
    return 0;
}
