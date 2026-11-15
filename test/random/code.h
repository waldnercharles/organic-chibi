#ifndef CHIBI_RANDOM_CODE_H
#define CHIBI_RANDOM_CODE_H

#include "../../src/arena.h"

#define CODE_NAME_MAX_LEN 20
#define CODE_SLACK 4096

typedef struct
{
    char *text;
    u8 *lens;
    u32 *weights;
    u32 pools[CODE_NAME_MAX_LEN + 2];
} Vocabulary;

int random_below(u64 *rng, int n);

void vocabulary_init(u64 *rng, Vocabulary *v, Arena *arena);
size_t random_code(u64 *rng, char *buf, const Vocabulary *v, size_t size);

#endif
