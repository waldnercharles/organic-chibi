#define _DEFAULT_SOURCE

#include "../src/arena.h"
#include "../src/lexer.h"
#include "../test/samples.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif

#define XXH_STATIC_LINKING_ONLY
#include "../vendor/xxhash/xxhash.h"

#define MAX_THREADS 64

static u64
checksum(Tokens tokens, int n)
{
    u64 sum = 0;
    for (int k = 0; k < n - 1; k++) {
        u8 kind = tokens.kinds[k];
        u64 id = tokens.ids[k];
        bool named = kind == TK_IDENT || (kind >= TK_IF && kind < TK_IF + NKEYWORDS);
        sum += (u64)kind * 0x9E3779B97F4A7C15ULL + (u64)tokens.starts[k] +
               (named ? id * 0x165667B19E3779F9ULL : 0);
    }
    return sum;
}

static u64
lex_checksum(Lexer *lx, const char *src, Tokens tokens)
{
    return checksum(tokens, lexer_tokenize(lx, src, tokens));
}

static u64
line_checksum(Tokens tokens, int n)
{
    u64 sum = 0;
    for (int k = 0; k < n; k++) {
        sum = sum * 3 + lexer_token_starts_line(tokens, k);
    }

    return sum;
}

static u64
fingerprint(const Samples *s, Tokens tokens, Arena *arena)
{
    Lexer lx;
    lexer_init(&lx, arena, s->total_len);

    XXH64_state_t fs;
    XXH64_reset(&fs, 0);
    for (u32 i = 0; i < s->count; i++) {
        int n = lexer_tokenize(&lx, s->files[i].text, tokens);
        u64 sum = checksum(tokens, n) + line_checksum(tokens, n);
        XXH64_update(&fs, &sum, sizeof sum);
    }

    return XXH64_digest(&fs);
}

static double
now_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);

    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static u64
lex_pass(const Sample *files, u32 lo, u32 hi, size_t bytes, Tokens tokens, Arena *pass)
{
    arena_reset(pass);
    Lexer lx;
    lexer_init(&lx, pass, bytes);
    u64 sum = 0;
    for (u32 i = lo; i < hi; i++) {
        sum += lex_checksum(&lx, files[i].text, tokens);
    }
    return sum;
}

static void
cpu_counters(u64 *instructions, u64 *cycles)
{
#ifdef __APPLE__
    struct rusage_info_v4 ri;
    if (proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri) != 0) {
        perror("proc_pid_rusage");
        exit(1);
    }
    *instructions = ri.ri_instructions;
    *cycles = ri.ri_cycles;
#else
    *instructions = *cycles = 0;
#endif
}

typedef struct
{
    const Sample *files;
    u32 lo, hi;
    Tokens tokens;
    Arena pass;
    size_t bytes;
    u64 passes, sum;
    double seconds;
    u64 instructions, cycles;
} Worker;

static void *
worker_main(void *arg)
{
    Worker *w = arg;
    for (u32 i = w->lo; i < w->hi; i++) {
        w->bytes += w->files[i].len;
    }

    lex_pass(w->files, w->lo, w->hi, w->bytes, w->tokens, &w->pass);

    u64 i0, c0, i1, c1;
    cpu_counters(&i0, &c0);
    double t0 = now_sec();
    do {
        w->sum += lex_pass(w->files, w->lo, w->hi, w->bytes, w->tokens, &w->pass);
        w->passes++;
    } while (w->passes < 3 || now_sec() - t0 < 0.25);
    w->seconds = now_sec() - t0;
    cpu_counters(&i1, &c1);

    w->instructions = i1 - i0;
    w->cycles = c1 - c0;

    return NULL;
}

static volatile u64 g_sink;

static void
measure(const Samples *s, Arena *setup, u32 nthreads)
{
    pthread_t tids[MAX_THREADS];
    Worker w[MAX_THREADS];
    u32 per = s->count / nthreads, rem = s->count % nthreads, lo = 0;
    for (u32 t = 0; t < nthreads; t++) {
        w[t] = (Worker){
            .files = s->files,
            .lo = lo,
            .hi = lo + per + (t < rem ? 1 : 0),
            .tokens = lexer_alloc_tokens(setup, s->max_len),
        };
        lo = w[t].hi;
        if (pthread_create(&tids[t], NULL, worker_main, &w[t]) != 0) {
            perror("pthread_create");
            exit(1);
        }
    }

    for (u32 t = 0; t < nthreads; t++) {
        pthread_join(tids[t], NULL);
    }

    double bytes = 0, seconds = 0;
    for (u32 t = 0; t < nthreads; t++) {
        bytes += (double)w[t].bytes * (double)w[t].passes;
        seconds = w[t].seconds > seconds ? w[t].seconds : seconds;
        g_sink += w[t].sum;
    }

    printf("%4u %s %6.0f MB/s", nthreads, nthreads == 1 ? "thread: " : "threads:", bytes / 1e6 / seconds);
    if (nthreads == 1 && w[0].cycles > 0) {
        printf(
            ", %.3f cycles/B, %.3f instr/B",
            (double)w[0].cycles / bytes,
            (double)w[0].instructions / bytes
        );
    }
    printf("\n");
}

int
main(void)
{
    Samples s = samples_load(SAMPLES_DIR);
    if (s.count == 0) {
        fprintf(stderr, "bench: no samples in %s\n", SAMPLES_DIR);
        return 1;
    }

    Arena setup = { 0 }, scratch = { 0 };
    u64 print = fingerprint(&s, lexer_alloc_tokens(&setup, s.max_len), &scratch);
    printf(
        "bench: %u files, %.2f MB, fingerprint 0x%016llx\n",
        s.count,
        (double)s.total_len / (1024.0 * 1024.0),
        (unsigned long long)print
    );

    u32 ncpu = (u32)sysconf(_SC_NPROCESSORS_ONLN);
    ncpu = ncpu < MAX_THREADS ? ncpu : MAX_THREADS;

    measure(&s, &setup, 1);
    measure(&s, &setup, ncpu < s.count ? ncpu : s.count);

    return 0;
}
