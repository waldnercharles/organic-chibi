#define _DEFAULT_SOURCE

#include "../../src/check.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#ifdef __APPLE__
#include <libproc.h>
#include <pthread.h>
#include <sys/resource.h>
#endif

#define RUNS 11

static void
cpu_counters(u64 *instructions, u64 *cycles)
{
#ifdef __APPLE__
    struct rusage_info_v4 ri;
    proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri);
    *instructions = ri.ri_instructions;
    *cycles = ri.ri_cycles;
#else
    *instructions = *cycles = 0;
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

int
main(int argc, char **argv)
{
#ifdef __APPLE__
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif

    int n = argc - 1;
    Arena arena = { 0 };
    char **srcs = calloc((size_t)n, sizeof *srcs);
    size_t *lens = calloc((size_t)n, sizeof *lens);
    size_t total = 0;
    u64 lines = 0;
    for (int i = 0; i < n; i++) {
        FILE *f = fopen(argv[i + 1], "rb");
        fseek(f, 0, SEEK_END);
        long len = ftell(f);
        fseek(f, 0, SEEK_SET);
        srcs[i] = arena_alloc(&arena, (size_t)len + LEXER_SOURCE_PADDING, 16);
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

    Lexer lx;
    lexer_init(&lx, &arena, total);
    ParsedFile *files = calloc((size_t)n, sizeof *files);
    u64 nodes = 0;
    for (int i = 0; i < n; i++) {
        Tokens tokens = lexer_alloc_tokens(&arena, lens[i]);
        int ntok = lexer_tokenize(&lx, srcs[i], tokens);
        Nodes out = parser_alloc_nodes(&arena, ntok);
        Parser p;
        int count = parser_parse(&p, tokens, ntok, out);
        if (ntok == 0 || p.error) {
            fprintf(stderr, "check-decls: %s does not parse\n", argv[i + 1]);
            return 1;
        }
        files[i] = (ParsedFile){ argv[i + 1], srcs[i], tokens, out, (u32)count, (u32)p.top_count, false };
        nodes += (u64)count;
    }

    double cycles[RUNS], instructions[RUNS], seconds[RUNS];
    u32 decls = 0, types = 0, diags = 0;
    for (int r = 0; r < RUNS; r++) {
        u64 i0, c0, i1, c1;
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        cpu_counters(&i0, &c0);
        Checker c;
        check_init(&c, &lx.names);
        check_package(&c, files, (u32)n);
        decls = c.decls.count - 1;
        types = c.types.count;
        diags = c.ndiags;
        check_free(&c);
        cpu_counters(&i1, &c1);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        seconds[r] = (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) / 1e9;
        cycles[r] = (double)(c1 - c0);
        instructions[r] = (double)(i1 - i0);
    }

    double cy = median(cycles, RUNS), in = median(instructions, RUNS), sec = median(seconds, RUNS);
    printf("check-decls: %d files, %llu lines, %llu nodes (%.2f per line), %u declarations, %u types, %u diagnostics\n",
           n, (unsigned long long)lines, (unsigned long long)nodes, (double)nodes / lines, decls, types, diags);
    printf("check-decls: %.1f cycles per line, %.1f cycles per node, %.0f cycles per declaration, %.2f instructions per node, IPC %.2f\n",
           cy / lines, cy / nodes, cy / decls, in / nodes, in / cy);
    printf("check-decls: %.2f M lines per second, %.1f ms per run, %.2f GHz\n", lines / sec / 1e6, sec * 1e3, cy / sec / 1e9);
    return diags != 0;
}
