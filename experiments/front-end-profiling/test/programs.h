#ifndef CHIBI_PROGRAMS_H
#define CHIBI_PROGRAMS_H

#include "../src/arena.h"

#define PIECE_MAX_LEN 64
#define CODE_NAME_MAX_LEN 20
#define CODE_SLACK 4096

typedef struct
{
    const char *const *items;
    int count;
} Pieces;

extern const Pieces all_pieces;

typedef struct
{
    char *text;
    u8 *lens;
    u32 *weights;
    u32 pools[CODE_NAME_MAX_LEN + 2];
} Vocabulary;

int random_below(u64 *rng, int n);
size_t random_program(u64 *rng, char *buf, Pieces pieces, int count);

void vocabulary_init(u64 *rng, Vocabulary *v, Arena *arena);
size_t random_code(u64 *rng, char *buf, const Vocabulary *v, size_t size);

#endif
