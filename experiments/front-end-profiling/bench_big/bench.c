#define _DEFAULT_SOURCE

#include "../src/arena.h"
#include "../src/lexer.h"
#include "../src/parser.h"
#include "../test/programs.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif

#define XXH_STATIC_LINKING_ONLY
#include "../vendor/xxhash/xxhash.h"

#define MAX_THREADS 64
#define NPROGRAMS 22400
#define PROGRAM_MIN_SIZE 1024
#define PROGRAM_SIZE_RANGE 8192

typedef struct
{
    char *text;
    size_t len;
} Program;

typedef struct
{
    Program *files;
    u32 count;
    size_t total_len, max_len;
} Programs;

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
line_checksum(Tokens tokens, int n)
{
    u64 sum = 0;
    for (int k = 0; k < n; k++) {
        sum = sum * 3 + lexer_token_starts_line(tokens, k);
    }

    return sum;
}

static u64
node_checksum(Nodes nodes, int n)
{
    u64 sum = 0;
    for (int k = 0; k < n; k++) {
        sum = sum * 31 + (u64)nodes.kinds[k] * 0x9E3779B97F4A7C15ULL +
              nodes.tokens[k];
    }
    return sum;
}

static u64
fingerprint(const Programs *s, Tokens tokens, Nodes nodes, Arena *arena)
{
    Lexer lx;
    lexer_init(&lx, arena, s->total_len);

    XXH64_state_t fs;
    XXH64_reset(&fs, 0);
    for (u32 i = 0; i < s->count; i++) {
        int n = lexer_tokenize(&lx, s->files[i].text, tokens);

        Parser parser;
        int count = parser_parse(&parser, tokens, n, nodes);
        if (count < 0) {
            fprintf(stderr, "bench: program %u does not parse\n", i);
            parser_print_error(&parser, s->files[i].text);
            exit(1);
        }

        u64 sum = checksum(tokens, n) + line_checksum(tokens, n) +
                  node_checksum(nodes, count);
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
    const Program *files;
    u32 lo, hi;
    Tokens tokens;
    Nodes nodes;
    bool parse;
    Arena pass;
    size_t bytes;
    u64 passes, sum;
    double seconds;
    u64 instructions, cycles;
} Worker;

static u64
run_pass(Worker *w)
{
    arena_reset(&w->pass);
    Lexer lx;
    lexer_init(&lx, &w->pass, w->bytes);

    u64 sum = 0;
    for (u32 i = w->lo; i < w->hi; i++) {
        int n = lexer_tokenize(&lx, w->files[i].text, w->tokens);
        sum += checksum(w->tokens, n);
        if (w->parse) {
            Parser parser;
            sum += (u64)parser_parse(&parser, w->tokens, n, w->nodes);
        }
    }
    return sum;
}

static void *
worker_main(void *arg)
{
    Worker *w = arg;
    for (u32 i = w->lo; i < w->hi; i++) {
        w->bytes += w->files[i].len;
    }

    run_pass(w);

    u64 i0, c0, i1, c1;
    cpu_counters(&i0, &c0);
    double t0 = now_sec();
    do {
        w->sum += run_pass(w);
        w->passes++;
    } while (w->passes < 3 || now_sec() - t0 < 0.25);
    w->seconds = now_sec() - t0;
    cpu_counters(&i1, &c1);

    w->instructions = i1 - i0;
    w->cycles = c1 - c0;

    return NULL;
}

static volatile u64 g_sink;

typedef struct
{
    double mb_per_sec;
    double cycles_per_byte;
    double instructions_per_byte;
} Speed;

static Speed
measure(const Programs *s, Arena *setup, u32 nthreads, bool parse)
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
            .nodes = parser_alloc_nodes(setup, (int)lexer_max_tokens(s->max_len)),
            .parse = parse,
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

    return (Speed){
        .mb_per_sec = bytes / 1e6 / seconds,
        .cycles_per_byte = (double)w[0].cycles / bytes,
        .instructions_per_byte = (double)w[0].instructions / bytes,
    };
}

static void
print_speed(const char *what, u32 nthreads, Speed speed)
{
    printf(
        "%-14s %3u %s %6.0f MB/s",
        what,
        nthreads,
        nthreads == 1 ? "thread: " : "threads:",
        speed.mb_per_sec
    );
    if (nthreads == 1 && speed.cycles_per_byte > 0) {
        printf(", %.3f cycles/B, %.3f instr/B", speed.cycles_per_byte, speed.instructions_per_byte);
    }
    printf("\n");
}

static Programs
make_programs(Arena *scratch, u64 seed)
{
    u64 rng = seed;
    Vocabulary v;
    vocabulary_init(&rng, &v, scratch);

    Programs s = { .files = malloc(NPROGRAMS * sizeof *s.files), .count = NPROGRAMS };
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    for (u32 i = 0; i < NPROGRAMS; i++) {
        size_t size = PROGRAM_MIN_SIZE + (size_t)random_below(&rng, PROGRAM_SIZE_RANGE);
        size_t cap = size + CODE_SLACK + LEXER_SOURCE_PADDING;

        void *text;
        if (posix_memalign(&text, page, cap) != 0) {
            perror("posix_memalign");
            exit(1);
        }

        memset(text, 0, cap);
        size_t len = random_code(&rng, text, &v, size);

        s.files[i] = (Program){ text, len };
        s.total_len += len;
        s.max_len = len > s.max_len ? len : s.max_len;
    }

    return s;
}

int
main(int argc, char **argv)
{
    bool train = argc > 1 && strcmp(argv[1], "train") == 0;

    Arena setup = { 0 }, scratch = { 0 };
    Programs s = make_programs(&scratch, train ? 2 : 1);

    Tokens tokens = lexer_alloc_tokens(&setup, s.max_len);
    Nodes nodes = parser_alloc_nodes(&setup, (int)lexer_max_tokens(s.max_len));
    u64 print = fingerprint(&s, tokens, nodes, &scratch);
    if (train) {
        return 0;
    }

    printf(
        "bench: %u programs, %.2f MB, fingerprint 0x%016llx\n",
        s.count,
        (double)s.total_len / (1024.0 * 1024.0),
        (unsigned long long)print
    );

    u32 ncpu = (u32)sysconf(_SC_NPROCESSORS_ONLN);
    ncpu = ncpu < MAX_THREADS ? ncpu : MAX_THREADS;

    u32 nthreads = ncpu < s.count ? ncpu : s.count;

    Speed lex = measure(&s, &setup, 1, false);
    print_speed("lexer", 1, lex);
    print_speed("lexer", nthreads, measure(&s, &setup, nthreads, false));

    Speed both = measure(&s, &setup, 1, true);
    print_speed("lexer + parser", 1, both);
    print_speed("lexer + parser", nthreads, measure(&s, &setup, nthreads, true));

    printf(
        "parser alone:  %.3f cycles/B, %.3f instr/B\n",
        both.cycles_per_byte - lex.cycles_per_byte,
        both.instructions_per_byte - lex.instructions_per_byte
    );

    return 0;
}
