#define _DEFAULT_SOURCE
#include "../../src/arm64.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
main(int argc, char **argv)
{
    FILE *fp = fopen(argv[1], "rb");
    fseek(fp, 0, SEEK_END);
    size_t len = (size_t)ftell(fp);
    fseek(fp, 0, SEEK_SET);
    Arena arena = { 0 };
    Lexer lx;
    lexer_init(&lx, &arena, len);
    char *src = arena_alloc(&arena, len + LEXER_SOURCE_PADDING, 16);
    memset(src, 0, len + LEXER_SOURCE_PADDING);
    fread(src, 1, len, fp);
    Tokens t = lexer_alloc_tokens(&arena, len);
    int nt = lexer_tokenize(&lx, src, t);
    Nodes o = parser_alloc_nodes(&arena, nt);
    Parser p;
    int c = parser_parse(&p, t, nt, o);
    ParsedFile f = { "a", src, t, o, c, p.top_count, true };
    Checker ch;
    check_init(&ch, &lx.names);
    check_package(&ch, &f, 1);
    if (ch.ndiags) {
        check_print_diagnostics(stderr, &ch);
        return 1;
    }
    if (argc > 2) {
        check_dump_ir(stdout, &ch);
    }
    Native n;
    if (!arm64_compile(&n, &ch, CONVENTION_HOST)) {
        printf("error %s\n", n.error);
        return 1;
    }
    for (u32 k = 0; k < ch.ir.nfuncs; k++) {
        u32 *start = (u32 *)native_entry(&n, ch.ir.funcs[k].decl);
        u32 *end = k + 1 < ch.ir.nfuncs ? (u32 *)native_entry(&n, ch.ir.funcs[k + 1].decl) : n.code + n.words;
        printf("# function %u\n", k);
        for (u32 *w = start; w < end; w++) {
            printf("0x%02x 0x%02x 0x%02x 0x%02x\n", *w & 0xFF, (*w >> 8) & 0xFF, (*w >> 16) & 0xFF, *w >> 24);
        }
    }
}
