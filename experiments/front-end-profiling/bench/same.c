#include "../src/lexer.h"
#include "../src/parser.h"
#include "../test/programs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int proto_parser_parse(Parser *p, Tokens tokens, int ntokens, Nodes out);
int main(void) {
    Arena a = {0};
    u64 rng = 1; Vocabulary v; vocabulary_init(&rng, &v, &a);
    size_t total = 0;
    static char *texts[1600]; size_t lens[1600], maxl = 0;
    for (int i = 0; i < 1600; i++) {
        size_t size = 1024 + (size_t)random_below(&rng, 8192);
        char *t = calloc(size + CODE_SLACK + LEXER_SOURCE_PADDING, 1);
        lens[i] = random_code(&rng, t, &v, size); texts[i] = t; total += lens[i]; if (lens[i] > maxl) maxl = lens[i];
    }
    Tokens tk = lexer_alloc_tokens(&a, maxl);
    Nodes n1 = parser_alloc_nodes(&a, (int)lexer_max_tokens(maxl)), n2 = parser_alloc_nodes(&a, (int)lexer_max_tokens(maxl));
    Lexer lx; lexer_init(&lx, &a, total);
    long nodes = 0;
    for (int i = 0; i < 1600; i++) {
        int n = lexer_tokenize(&lx, texts[i], tk);
        Parser p1, p2;
        int c1 = parser_parse(&p1, tk, n, n1), c2 = proto_parser_parse(&p2, tk, n, n2);
        if (c1 != c2 || memcmp(n1.kinds, n2.kinds, (size_t)c1) || memcmp(n1.tokens, n2.tokens, (size_t)c1 * 4)) { printf("DIFF file %d: %d vs %d\n", i, c1, c2); return 1; }
        nodes += c1;
    }
    printf("identical: 1600 files, %ld nodes\n", nodes);
    return 0;
}
