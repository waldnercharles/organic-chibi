#include <stdio.h>
#include <stdlib.h>
#include "../src/lexer.h"
#include "../test/programs.h"
extern u64 hist_starts[65], hist_slow;
int main(void) {
    Arena a = {0}; u64 rng = 1; Vocabulary v; vocabulary_init(&rng, &v, &a);
    static char *texts[1600]; size_t total = 0, maxl = 0, lens[1600];
    for (int i = 0; i < 1600; i++) { size_t size = 1024 + (size_t)random_below(&rng, 8192); char *t = calloc(size + CODE_SLACK + LEXER_SOURCE_PADDING, 1); lens[i] = random_code(&rng, t, &v, size); texts[i] = t; total += lens[i]; if (lens[i] > maxl) maxl = lens[i]; }
    Tokens tk = lexer_alloc_tokens(&a, maxl); Lexer lx; lexer_init(&lx, &a, total);
    for (int i = 0; i < 1600; i++) lexer_tokenize(&lx, texts[i], tk);
    u64 n = 0, over8 = 0, over16 = 0, over24 = 0, sum = 0;
    for (int k = 0; k <= 64; k++) { n += hist_starts[k]; sum += k * hist_starts[k]; if (k > 8) over8 += hist_starts[k]; if (k > 16) over16 += hist_starts[k]; if (k > 24) over24 += hist_starts[k]; }
    printf("blocks %llu mean starts %.2f  >8: %.1f%%  >16: %.1f%%  >24: %.2f%%  slow blocks %.2f%%\n", n, (double)sum / n, 100.0*over8/n, 100.0*over16/n, 100.0*over24/n, 100.0*hist_slow/n);
}
