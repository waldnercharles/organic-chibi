#ifndef CHIBI_PROGRAMS_H
#define CHIBI_PROGRAMS_H

#include "../src/common.h"

#define PIECE_MAX_LEN 64

typedef struct
{
    const char *const *items;
    int count;
} Pieces;

extern const Pieces all_pieces;

int random_below(u64 *rng, int n);
size_t random_program(u64 *rng, char *buf, Pieces pieces, int count);

#endif
