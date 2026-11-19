#include <stdio.h>
#include "../src/lexer.h"
#include "../test/programs.h"
#include <stdlib.h>
#include <string.h>
extern u64 probe_names, probe_long, probe_cold, probe_new;
int main(void) {
    Arena a = {0};
    u64 rng = 1; Vocabulary v; vocabulary_init(&rng, &v, &a);
    static char *texts[1600]; size_t lens[1600], total = 0, maxl = 0;
    for (int i = 0; i < 1600; i++) {
        size_t size = 1024 + (size_t)random_below(&rng, 8192);
        char *t = calloc(size + CODE_SLACK + LEXER_SOURCE_PADDING, 1);
        lens[i] = random_code(&rng, t, &v, size); texts[i] = t; total += lens[i]; if (lens[i] > maxl) maxl = lens[i];
    }
    Tokens tk = lexer_alloc_tokens(&a, maxl);
    for (int pass = 0; pass < 2; pass++) {
        probe_names = probe_long = probe_cold = probe_new = 0;
        Lexer lx; lexer_init(&lx, &a, total);
        for (int i = 0; i < 1600; i++) lexer_tokenize(&lx, texts[i], tk);
        printf("pass %d: names %llu long(>16) %llu (%.2f%%) cold %llu (%.2f%%) new %llu distinct %u slots %u\n", pass, probe_names, probe_long, 100.0*probe_long/probe_names, probe_cold, 100.0*probe_cold/probe_names, probe_new, lx.names.count, lx.names.mask+1);
    }
}
