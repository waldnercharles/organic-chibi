#ifndef CHIBI_ARENA_H
#define CHIBI_ARENA_H

#include "common.h"

typedef struct ArenaChunk
{
    struct ArenaChunk *next;
    size_t cap;
} ArenaChunk;

// PERF: The field order matters here. If `top` sat next to `bottom`, clang loads both
// with one 16-byte load, which waits for the pending store to `top` (twice as slow on my M4).
typedef struct
{
    u8 *top;
    ArenaChunk *cur;
    u8 *bottom;
    ArenaChunk *free_list;
} Arena;

#define ARENA_MAX_ALLOC ((size_t)1 << 62)

void *arena_alloc_slow(Arena *a, size_t n, size_t align);

static inline ALWAYS_INLINE void *
arena_alloc(Arena *a, size_t n, size_t align)
{
    if (UNLIKELY(n >= ARENA_MAX_ALLOC)) {
        return arena_alloc_slow(a, n, align);
    }

    uintptr_t t = ((uintptr_t)a->top - n) & ~(uintptr_t)(align - 1);
    if (UNLIKELY((intptr_t)t < (intptr_t)a->bottom)) {
        return arena_alloc_slow(a, n, align);
    }
    a->top = (u8 *)t;
    return (void *)t;
}

bool arena_reserve(Arena *a, size_t cap);

typedef struct
{
    u8 *top;
    ArenaChunk *cur;
} ArenaMark;

static inline ArenaMark
arena_mark(const Arena *a)
{
    return (ArenaMark){ a->top, a->cur };
}

void arena_restore(Arena *a, ArenaMark m);

void arena_reset(Arena *a);

void arena_free(Arena *a);

#endif
