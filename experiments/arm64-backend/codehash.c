#define _DEFAULT_SOURCE

#include "../../src/arm64.h"
#include "../../src/lexer.h"
#include "../../src/parser.h"
#include "../../test/backend_programs.h"
#include "../../test/random/program.h"
#include "../../vendor/xxhash/xxhash.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    Arena arena;
    Lexer lx;
    ParsedFile *files;
    Checker c;
} Unit;

static FILE *out;
static u32 compiled, skipped;

static void
emit_hash(const char *label, u32 index, const Checker *c)
{
    for (int convention = 0; convention < 2; convention++) {
        Native n;
        Convention cv = convention ? CONVENTION_APPLE : CONVENTION_AAPCS64;
        if (!arm64_compile(&n, c, cv)) {
            fprintf(out, "%s %u %d refused %s\n", label, index, convention, n.error);
            native_free(&n);
            continue;
        }
        u64 h = XXH3_64bits(n.code, n.code_size);
        u64 d = XXH3_64bits(n.data, c->ir.ndata);
        fprintf(out, "%s %u %d %zu %016llx %016llx\n", label, index, convention, n.code_size,
                (unsigned long long)h, (unsigned long long)d);
        native_free(&n);
        compiled++;
    }
}

static void
run_files(const char *label, u32 index, char **texts, u32 n, bool release_fast, bool entry)
{
    Unit u = { 0 };
    size_t total = 0;
    for (u32 i = 0; i < n; i++) {
        total += strlen(texts[i]);
    }
    lexer_init(&u.lx, &u.arena, total);
    u.files = calloc(n, sizeof *u.files);
    for (u32 i = 0; i < n; i++) {
        size_t len = strlen(texts[i]);
        char *src = arena_alloc(&u.arena, len + LEXER_SOURCE_PADDING, 16);
        memset(src, 0, len + LEXER_SOURCE_PADDING);
        memcpy(src, texts[i], len);
        Tokens tokens = lexer_alloc_tokens(&u.arena, len);
        int ntokens = lexer_tokenize(&u.lx, src, tokens);
        if (ntokens <= 0) {
            skipped++;
            free(u.files);
            arena_free(&u.arena);
            return;
        }
        Nodes nodes = parser_alloc_nodes(&u.arena, ntokens);
        Parser parser;
        int count = parser_parse(&parser, tokens, ntokens, nodes);
        if (count < 0 || parser.error) {
            skipped++;
            free(u.files);
            arena_free(&u.arena);
            return;
        }
        u.files[i] = (ParsedFile){ "a", src, tokens, nodes, (u32)count, (u32)parser.top_count, entry };
    }
    check_init(&u.c, &u.lx.names, u.files, n);
    u.c.release_fast = release_fast;
    check_package(&u.c);
    if (u.c.ndiags) {
        skipped++;
    } else {
        emit_hash(label, index, &u.c);
    }
    check_free(&u.c);
    free(u.files);
    arena_free(&u.arena);
}

static void
run_text(const char *label, u32 index, const char *text)
{
    char *t = (char *)text;
    run_files(label, index, &t, 1, false, false);
    run_files(label, index, &t, 1, true, false);
    run_files(label, index, &t, 1, false, true);
}

static char *
read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
        exit(1);
    }
    buf[n] = 0;
    fclose(f);
    *len = (size_t)n;
    return buf;
}

int
main(int argc, char **argv)
{
    out = stdout;
    int randoms = atoi(argv[1]);
    const char *extracted = argv[2];

    for (u32 i = 0; i < NBACKEND_PROGRAMS; i++) {
        run_text("backend", i, BACKEND_PROGRAMS[i]);
    }
    char *wide = wide_errors_program();
    run_text("wide", 0, wide);
    free(wide);

    char *text = malloc(PROGRAM_SIZE);
    u64 rng = 0x5EED;
    for (int k = 0; k < randoms; k++) {
        size_t len = random_typed_program(&rng, text);
        text[len] = 0;
        run_text("random", (u32)k, text);
    }

    size_t len;
    char *all = read_file(extracted, &len);
    u32 index = 0;
    for (size_t at = 0; at < len; index++) {
        run_text("extracted", index, all + at);
        at += strlen(all + at) + 1;
    }

    if (argc > 3) {
        u32 n = (u32)(argc - 3);
        char **texts = calloc(n, sizeof *texts);
        for (u32 i = 0; i < n; i++) {
            size_t l;
            texts[i] = read_file(argv[3 + i], &l);
        }
        run_files("corpus", 0, texts, n, false, false);
        run_files("corpus", 1, texts, n, true, false);
    }
    fprintf(stderr, "compiled %u, skipped %u\n", compiled, skipped);
    return 0;
}
