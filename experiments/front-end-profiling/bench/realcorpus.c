#define _DEFAULT_SOURCE
#include "../src/lexer.h"
#include "../test/programs.h"
#include <libproc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>
#include <mach/mach_time.h>

typedef struct { char *text; size_t len; } File;

static void counters(u64 *ins, u64 *cyc) {
    struct rusage_info_v4 ri;
    proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri);
    *ins = ri.ri_instructions; *cyc = ri.ri_cycles;
}
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

static void measure(const char *name, File *f, int n, size_t maxl) {
    Arena a = { 0 };
    Tokens tk = lexer_alloc_tokens(&a, maxl);
    size_t bytes = 0; u64 lines = 0, toks = 0;
    for (int i = 0; i < n; i++) {
        bytes += f[i].len;
        for (size_t k = 0; k < f[i].len; k++) lines += f[i].text[k] == '\n';
    }
    {
        Arena pass = { 0 }; Lexer lx; lexer_init(&lx, &pass, bytes);
        for (int i = 0; i < n; i++) toks += (u64)lexer_tokenize(&lx, f[i].text, tk);
        arena_free(&pass);
    }
    Arena pass = { 0 };
    u64 i0, c0, i1, c1, passes = 0, sum = 0;
    double t0 = now();
    counters(&i0, &c0);
    do {
        arena_reset(&pass);
        Lexer lx; lexer_init(&lx, &pass, bytes);
        for (int i = 0; i < n; i++) sum += (u64)lexer_tokenize(&lx, f[i].text, tk);
        passes++;
    } while (now() - t0 < 1.0);
    counters(&i1, &c1);
    double B = (double)bytes * passes;
    printf("%-10s files %4d bytes %8zu lines %6llu  B/line %.1f tok/line %.2f  %.3f cyc/B  %.3f ins/B  IPC %.2f  %.1f cyc/line (%llu)\n", name, n, bytes, lines,
           (double)bytes / lines, (double)toks / lines, (c1 - c0) / B, (i1 - i0) / B, (double)(i1 - i0) / (c1 - c0), (c1 - c0) / ((double)lines * passes), sum & 1);
}

static void cold(const char *name, File *f, int n, size_t maxl, File *junk, int njunk, size_t jmax) {
    Arena a = { 0 };
    Tokens tk = lexer_alloc_tokens(&a, maxl > jmax ? maxl : jmax);
    mach_timebase_info_data_t tb; mach_timebase_info(&tb);
    size_t bytes = 0; u64 ticks = 0, sum = 0;
    u64 i0, c0, i1, c1;
    double t0 = now();
    counters(&i0, &c0);
    int j = 0;
    size_t all = 0;
    for (int i = 0; i < n; i++) all += f[i].len;
    Arena p = { 0 }; Lexer lx; lexer_init(&lx, &p, all);
    for (int rep = 0; rep < 3; rep++)
    for (int i = 0; i < n; i++) {
        Arena pj = { 0 }; Lexer lj; lexer_init(&lj, &pj, 1 << 20);
        for (int k = 0; k < 200; k++) { sum += (u64)lexer_tokenize(&lj, junk[j].text, tk); j = (j + 1) % njunk; }
        arena_free(&pj);
        u64 a0 = mach_absolute_time();
        sum += (u64)lexer_tokenize(&lx, f[i].text, tk);
        u64 a1 = mach_absolute_time();
        ticks += a1 - a0; bytes += f[i].len;
    }
    counters(&i1, &c1);
    double ghz = (c1 - c0) / (now() - t0) / 1e9;
    double ns = (double)ticks * tb.numer / tb.denom;
    printf("cold %-10s files %4d  %.3f cyc/B at %.2f GHz (%llu)\n", name, n, ns * ghz / bytes, ghz, sum & 1);
}

int main(int argc, char **argv) {
    const char *dir = argv[1];
    int nmax = atoi(argv[2]);
    static File real[4096]; int nreal = 0; size_t real_bytes = 0, maxl = 0; int bad = 0;
    Arena a = { 0 };
    Tokens tk = lexer_alloc_tokens(&a, 1 << 22);
    for (int i = 1; i <= nmax; i++) {
        char path[512]; snprintf(path, sizeof path, "%s/%d.chibi", dir, i);
        FILE *fp = fopen(path, "rb"); if (!fp) continue;
        fseek(fp, 0, SEEK_END); long len = ftell(fp); fseek(fp, 0, SEEK_SET);
        char *t = calloc((size_t)len + LEXER_SOURCE_PADDING, 1);
        fread(t, 1, (size_t)len, fp); fclose(fp);
        Arena p = { 0 }; Lexer lx; lexer_init(&lx, &p, (size_t)len);
        int n = lexer_tokenize(&lx, t, tk);
        arena_free(&p);
        if (n == 0 || memchr(t, 0, (size_t)len)) { bad++; free(t); continue; }
        real[nreal++] = (File){ t, (size_t)len }; real_bytes += (size_t)len; if ((size_t)len > maxl) maxl = (size_t)len;
    }
    printf("real corpus: %d files lexed, %d rejected\n", nreal, bad);
    static File gen[4096]; int ngen = 0; size_t gen_bytes = 0, gmax = 0;
    u64 rng = 1; Vocabulary v; vocabulary_init(&rng, &v, &a);
    while (gen_bytes < real_bytes) {
        size_t size = 1024 + (size_t)random_below(&rng, 8192);
        char *t = calloc(size + CODE_SLACK + LEXER_SOURCE_PADDING, 1);
        size_t len = random_code(&rng, t, &v, size);
        gen[ngen++] = (File){ t, len }; gen_bytes += len; if (len > gmax) gmax = len;
    }
    static File junk[1024]; size_t jmax = 0;
    u64 jr = 99; Vocabulary jv; vocabulary_init(&jr, &jv, &a);
    for (int i = 0; i < 1024; i++) {
        size_t size = 1024 + (size_t)random_below(&jr, 8192);
        char *t = calloc(size + CODE_SLACK + LEXER_SOURCE_PADDING, 1);
        size_t len = random_code(&jr, t, &jv, size);
        junk[i] = (File){ t, len }; if (len > jmax) jmax = len;
    }
    if (argc > 3) {
        for (int r = 0; r < 3; r++) {
            cold("real", real, nreal, maxl, junk, 1024, jmax);
            cold("generated", gen, ngen, gmax, junk, 1024, jmax);
        }
        return 0;
    }
    for (int r = 0; r < 5; r++) {
        measure("real", real, nreal, maxl);
        measure("generated", gen, ngen, gmax);
    }
}
