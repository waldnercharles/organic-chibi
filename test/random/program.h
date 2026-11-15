#ifndef CHIBI_RANDOM_PROGRAM_H
#define CHIBI_RANDOM_PROGRAM_H

#include "../../src/common.h"

#include <stddef.h>

enum
{
    PROGRAM_ENTRIES = 4,
    PROGRAM_SIZE = 1 << 17,
};

size_t random_typed_program(u64 *rng, char *buf);

#endif
