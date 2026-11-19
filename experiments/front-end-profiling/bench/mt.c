#define _DEFAULT_SOURCE
#include "../src/lexer.h"
#include "../src/parser.h"
#include "../test/programs.h"
#include <libproc.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#define NP 1600
typedef struct { char *text; size_t len; } File;
static File files[NP]; static size_t total, maxl;
static int parse_too; static double secs = 1.0;
typedef struct { int lo, hi; u64 bytes; pthread_t t; u64 sum; } W;
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static void *run(void *arg) {
    W *w = arg; Arena a = { 0 }, pass = { 0 };
    Tokens tk = lexer_alloc_tokens(&a, maxl); Nodes nd = parser_alloc_nodes(&a, (int)lexer_max_tokens(maxl));
    size_t bytes = 0; for (int i = w->lo; i < w->hi; i++) bytes += files[i].len;
    double t0 = now();
    do {
        arena_reset(&pass); Lexer lx; lexer_init(&lx, &pass, bytes);
        for (int i = w->lo; i < w->hi; i++) {
            int n = lexer_tokenize(&lx, files[i].text, tk); w->sum += (u64)n;
            if (parse_too) { Parser p; w->sum += (u64)parser_parse(&p, tk, n, nd); }
        }
        w->bytes += bytes;
    } while (now() - t0 < secs);
    return NULL;
}
static void counters(u64 *ins, u64 *cyc) { struct rusage_info_v4 ri; proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri); *ins = ri.ri_instructions; *cyc = ri.ri_cycles; }
int main(int argc, char **argv) {
    int nt = atoi(argv[1]); int full = strcmp(argv[2], "full") == 0; parse_too = argc > 3 && strcmp(argv[3], "parse") == 0;
    Arena s = { 0 }; u64 rng = 1; Vocabulary v; vocabulary_init(&rng, &v, &s);
    for (int i = 0; i < NP; i++) {
        size_t size = 1024 + (size_t)random_below(&rng, 8192);
        char *t = calloc(size + CODE_SLACK + LEXER_SOURCE_PADDING, 1);
        files[i] = (File){ t, random_code(&rng, t, &v, size) }; total += files[i].len; if (files[i].len > maxl) maxl = files[i].len;
    }
    W w[64] = { 0 };
    for (int t = 0; t < nt; t++) { w[t].lo = full ? 0 : NP * t / nt; w[t].hi = full ? NP : NP * (t + 1) / nt; }
    u64 i0, c0, i1, c1; counters(&i0, &c0); double t0 = now();
    for (int t = 0; t < nt; t++) pthread_create(&w[t].t, NULL, run, &w[t]);
    u64 bytes = 0;
    for (int t = 0; t < nt; t++) { pthread_join(w[t].t, NULL); bytes += w[t].bytes; }
    double dt = now() - t0; counters(&i1, &c1);
    printf("%2d threads %-5s %-5s %7.0f MB/s  %.3f cyc/B (all threads)  %.3f ins/B\n", nt, full ? "full" : "split", parse_too ? "parse" : "lex", bytes / 1e6 / dt, (double)(c1 - c0) / bytes, (double)(i1 - i0) / bytes);
}
