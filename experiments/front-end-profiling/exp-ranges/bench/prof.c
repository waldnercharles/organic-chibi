#define _DEFAULT_SOURCE

#include "../src/arena.h"
#include "../src/lexer.h"
#include "../src/parser.h"
#include "../test/programs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <libproc.h>
#include <sys/resource.h>
#include <mach-o/dyld.h>

#define NPROGRAMS 1600
#define PROGRAM_MIN_SIZE 1024
#define PROGRAM_SIZE_RANGE 8192

typedef struct
{
    char *text;
    size_t len;
} Program;

static Program files[NPROGRAMS];
static size_t total_len, max_len;

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
    struct rusage_info_v4 ri;
    proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri);
    *instructions = ri.ri_instructions;
    *cycles = ri.ri_cycles;
}

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

static void
make_programs(Arena *scratch, u64 seed)
{
    u64 rng = seed;
    Vocabulary v;
    vocabulary_init(&rng, &v, scratch);
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    for (u32 i = 0; i < NPROGRAMS; i++) {
        size_t size = PROGRAM_MIN_SIZE + (size_t)random_below(&rng, PROGRAM_SIZE_RANGE);
        size_t cap = size + CODE_SLACK + LEXER_SOURCE_PADDING;
        void *text;
        posix_memalign(&text, page, cap);
        memset(text, 0, cap);
        size_t len = random_code(&rng, text, &v, size);
        files[i] = (Program){ text, len };
        total_len += len;
        max_len = len > max_len ? len : max_len;
    }
}

static volatile u64 sink;

int
main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "stats";
    printf("load %p\n", (void *)_dyld_get_image_header(0)); fflush(stdout);
    double seconds = argc > 2 ? atof(argv[2]) : 1.0;
    if (argc > 3 && strcmp(argv[3], "dump") == 0) {
        Arena s = { 0 };
        make_programs(&s, 1);
        fwrite(files[0].text, 1, files[0].len, stdout);
        return 0;
    }

    Arena setup = { 0 }, scratch = { 0 }, pass = { 0 };
    make_programs(&scratch, getenv("SEED") ? (u64)atoi(getenv("SEED")) : 1);

    Tokens tokens = lexer_alloc_tokens(&setup, max_len);
    Nodes nodes = parser_alloc_nodes(&setup, (int)lexer_max_tokens(max_len));

    u64 lines = 0, blank = 0, ntokens = 0, nnodes = 0, nidents = 0, nkw = 0, nline_tokens = 0;
    u64 kind_hist[NKINDS] = { 0 };
    u64 node_hist[NNODES] = { 0 };
    {
        Lexer lx;
        lexer_init(&lx, &pass, total_len);
        for (u32 i = 0; i < NPROGRAMS; i++) {
            const char *t = files[i].text;
            bool empty = true;
            for (size_t k = 0; k < files[i].len; k++) {
                if (t[k] == '\n') {
                    lines++;
                    blank += empty;
                    empty = true;
                } else if (t[k] != ' ' && t[k] != '\t' && t[k] != '\r') {
                    empty = false;
                }
            }
            if (files[i].len && t[files[i].len - 1] != '\n') {
                lines++;
            }
            int n = lexer_tokenize(&lx, t, tokens);
            ntokens += (u64)n;
            for (int k = 0; strcmp(mode, "lex") != 0 && k < n; k++) {
                kind_hist[tokens.kinds[k] < NKINDS ? tokens.kinds[k] : 0]++;
                nline_tokens += lexer_token_starts_line(tokens, k);
            }
            Parser p;
            int c = strcmp(mode, "lex") == 0 ? 0 : parser_parse(&p, tokens, n, nodes);
            if (c < 0) {
                if (strcmp(mode, "lex") != 0) {
                    fprintf(stderr, "parse fail %u\n", i);
                    return 1;
                }
                c = 0;
            }
            nnodes += (u64)c;
            for (int k = 0; k < c; k++) {
                node_hist[nodes.kinds[k]]++;
            }
        }
        nidents = kind_hist[TK_IDENT];
        for (int k = TK_IF; k < TK_IF + NKEYWORDS; k++) {
            nkw += kind_hist[k];
        }
        if (strcmp(mode, "stats") == 0) {
            printf("bytes %zu lines %llu blank %llu tokens %llu nodes %llu idents %llu keywords %llu names %u line-first-tokens %llu\n",
                   total_len, lines, blank, ntokens, nnodes, nidents, nkw, lx.names.count, nline_tokens);
            printf("bytes/line %.2f tokens/line %.2f nodes/token %.3f bytes/token %.2f\n",
                   (double)total_len / lines, (double)ntokens / lines, (double)nnodes / ntokens,
                   (double)total_len / ntokens);
            for (int k = 0; k < NKINDS; k++) {
                if (kind_hist[k]) {
                    printf("  tok %-10s %6.2f%%\n", lexer_kind_name(k), 100.0 * kind_hist[k] / ntokens);
                }
            }
            for (int k = 0; k < NNODES; k++) {
                if (node_hist[k]) {
                    printf("  node %3d %6.2f%%\n", k, 100.0 * node_hist[k] / nnodes);
                }
            }
            return 0;
        }
    }

    Tokens *all = NULL;
    int *counts = NULL;
    if (strcmp(mode, "parse") == 0) {
        all = malloc(NPROGRAMS * sizeof *all);
        counts = malloc(NPROGRAMS * sizeof *counts);
        Lexer lx;
        lexer_init(&lx, &setup, total_len);
        for (u32 i = 0; i < NPROGRAMS; i++) {
            all[i] = lexer_alloc_tokens(&setup, files[i].len);
            counts[i] = lexer_tokenize(&lx, files[i].text, all[i]);
        }
    }

    int m = strcmp(mode, "lex") == 0      ? 0
          : strcmp(mode, "lexsum") == 0 ? 1
          : strcmp(mode, "both") == 0   ? 2
          : strcmp(mode, "parse") == 0  ? 3
          : strcmp(mode, "bothsum") == 0 ? 4
                                         : -1;
    if (m < 0) {
        fprintf(stderr, "unknown mode\n");
        return 1;
    }

    u32 nfiles = getenv("NFILES") ? (u32)atoi(getenv("NFILES")) : NPROGRAMS;
    size_t used_len = 0;
    u64 used_tokens = 0;
    for (u32 i = 0; i < nfiles; i++) {
        used_len += files[i].len;
    }
    if (m == 3) {
        for (u32 i = 0; i < nfiles; i++) {
            used_tokens += (u64)counts[i];
        }
    }
    u64 passes = 0, sum = 0;
    u64 i0, c0, i1, c1;
    double t0 = now_sec();
    cpu_counters(&i0, &c0);
    do {
        Lexer lx;
        if (m != 3) {
            arena_reset(&pass);
            lexer_init(&lx, &pass, total_len);
        }
        for (u32 i = 0; i < nfiles; i++) {
            if (m == 3) {
                Parser p;
                sum += (u64)parser_parse(&p, all[i], counts[i], nodes);
                continue;
            }
            int n = lexer_tokenize(&lx, files[i].text, tokens);
            if (m == 1 || m == 4) {
                sum += checksum(tokens, n);
            } else {
                sum += (u64)n;
            }
            if (m == 2 || m == 4) {
                Parser p;
                sum += (u64)parser_parse(&p, tokens, n, nodes);
            }
        }
        passes++;
    } while (passes < 3 || now_sec() - t0 < seconds);
    cpu_counters(&i1, &c1);
    double dt = now_sec() - t0;
    sink = sum;

    double B = (double)used_len * passes;
    if (nfiles != NPROGRAMS) {
        double cyc = (double)(c1 - c0), ins = (double)(i1 - i0);
        printf("%-7s nfiles %u bytes %zu  %.3f cyc/B  %.3f ins/B  IPC %.2f  %.2f cyc/tok  %.2f ins/tok\n", mode, nfiles, used_len, cyc / B, ins / B, ins / cyc,
               used_tokens ? cyc / ((double)used_tokens * passes) : 0.0, used_tokens ? ins / ((double)used_tokens * passes) : 0.0);
        return 0;
    }
    double cyc = (double)(c1 - c0), ins = (double)(i1 - i0);
    printf("%-7s %6.0f MB/s  %.3f cyc/B  %.3f ins/B  IPC %.2f  %.1f cyc/line  %.2f cyc/tok  %.2f cyc/node  %.2f GHz\n",
           mode, B / 1e6 / dt, cyc / B, ins / B, ins / cyc,
           cyc / ((double)lines * passes), cyc / ((double)ntokens * passes),
           cyc / ((double)nnodes * passes), cyc / dt / 1e9);
    return 0;
}
