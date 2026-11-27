#define _GNU_SOURCE

#include "../../src/jit.h"
#include "../../src/lexer.h"
#include "../../src/parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef int64_t Entry(void *);

static uint8_t CONTEXT[16];

int
main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: x64-debug FILE FUNCTION [SYMFILE]\n");
        return 2;
    }

    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        fprintf(stderr, "x64-debug: can't open %s\n", argv[1]);
        return 2;
    }
    fseek(f, 0, SEEK_END);
    size_t len = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);

    Arena arena = { 0 };
    Lexer lx;
    lexer_init(&lx, &arena, len);
    char *src = arena_alloc(&arena, len + LEXER_SOURCE_PADDING, 16);
    memset(src, 0, len + LEXER_SOURCE_PADDING);
    if (fread(src, 1, len, f) != len) {
        return 2;
    }
    fclose(f);

    Tokens tokens = lexer_alloc_tokens(&arena, len);
    int ntokens = lexer_tokenize(&lx, src, tokens);
    Nodes nodes = parser_alloc_nodes(&arena, ntokens);
    Parser parser;
    int count = parser_parse(&parser, tokens, ntokens, nodes);
    if (count < 0) {
        fprintf(stderr, "x64-debug: %s does not parse\n", argv[1]);
        return 1;
    }
    ParsedFile file = { argv[1], src, tokens, nodes, (u32)count, (u32)parser.top_count, false };
    Checker c;
    check_init(&c, &lx.names, &file, 1);
    check_package(&c);
    if (c.ndiags) {
        check_print_diagnostics(stderr, &c);
        return 1;
    }

    Native n;
    if (!jit_compile(&n, &c, CONVENTION_HOST, JIT_DEBUGGER | JIT_PERF_MAP)) {
        fprintf(stderr, "x64-debug: %s\n", n.error);
        return 1;
    }
    if (argc > 3) {
        FILE *out = fopen(argv[3], "wb");
        fwrite(n.symfile, 1, n.symfile_size, out);
        fclose(out);
    }
    native_set_context(&n, CONTEXT);

    u32 before = lx.names.count;
    u32 id = names_intern(&lx.names, argv[2], (u32)strlen(argv[2]));
    u32 decl = id <= before ? c.package_names[id] : 0;
    if (!decl) {
        fprintf(stderr, "x64-debug: no function `%s`\n", argv[2]);
        return 1;
    }
    Entry *entry = (Entry *)native_entry(&n, decl);
    printf("%s returned %lld\n", argv[2], (long long)entry(CONTEXT));

    char path[64];
    snprintf(path, sizeof path, "/tmp/perf-%d.map", (int)getpid());
    FILE *map = fopen(path, "r");
    char line[256];
    while (map && fgets(line, sizeof line, map)) {
        printf("perf map: %s", line);
    }
    if (map) {
        fclose(map);
        unlink(path);
    }

    native_free(&n);
    check_free(&c);
    arena_free(&arena);
    return 0;
}
