#include "arena.h"

#include <assert.h>
#include <stdlib.h>

#define ARENA_FIRST_CHUNK ((size_t)64 * 1024 - sizeof(ArenaChunk))

static ArenaChunk *
new_chunk(size_t cap)
{
    ArenaChunk *c = malloc(sizeof(ArenaChunk) + cap);
    if (c != NULL) {
        c->cap = cap;
    }

    return c;
}

static void
use_chunk(Arena *a, ArenaChunk *c)
{
    c->next = a->cur;
    a->cur = c;
    a->bottom = (u8 *)(c + 1);
    a->top = a->bottom + c->cap;
}

static bool
push_chunk(Arena *a, size_t need)
{
    ArenaChunk *c = NULL;
    while (a->free_list != NULL) {
        ArenaChunk *f = a->free_list;
        a->free_list = f->next;
        if (f->cap >= need) {
            c = f;
            break;
        }
        free(f);
    }

    if (c == NULL) {
        size_t cap = ARENA_FIRST_CHUNK;
        if (a->cur != NULL && a->cur->cap < ARENA_MAX_ALLOC / 4) {
            size_t doubled = (a->cur->cap + sizeof(ArenaChunk)) * 2 - sizeof(ArenaChunk);
            if (doubled > cap) {
                cap = doubled;
            }
        }
        if (cap < need) {
            cap = need;
        }

        c = new_chunk(cap);
        if (c == NULL) {
            return false;
        }
    }
    use_chunk(a, c);

    return true;
}

void *
arena_alloc_slow(Arena *a, size_t n, size_t align)
{
    if (n >= ARENA_MAX_ALLOC || align >= ARENA_MAX_ALLOC) {
        return NULL;
    }

    if (!push_chunk(a, n + (align - 1))) {
        return NULL;
    }

    uintptr_t t = ((uintptr_t)a->top - n) & ~(uintptr_t)(align - 1);
    a->top = (u8 *)t;
    return (void *)t;
}

bool
arena_reserve(Arena *a, size_t cap)
{
    if (cap >= ARENA_MAX_ALLOC) {
        return false;
    }

    if ((size_t)(a->top - a->bottom) >= cap) {
        return true;
    }

    return push_chunk(a, cap);
}

void
arena_restore(Arena *a, ArenaMark m)
{
    ArenaChunk *c = a->cur;
    while (c != m.cur) {
        assert(c != NULL && "arena_restore: mark is not from this arena (or was already released)");
        ArenaChunk *older = c->next;
        c->next = a->free_list;
        a->free_list = c;
        c = older;
    }

    a->cur = m.cur;
    a->bottom = m.cur != NULL ? (u8 *)(m.cur + 1) : NULL;
    a->top = m.top;
}

void
arena_reset(Arena *a)
{
    if (a->cur == NULL) {
        return;
    }

    ArenaChunk *older = a->cur->next;
    if (older != NULL) {
        ArenaChunk *tail = older;
        while (tail->next != NULL) {
            tail = tail->next;
        }
        tail->next = a->free_list;
        a->free_list = older;
    }

    a->cur->next = NULL;
    a->bottom = (u8 *)(a->cur + 1);
    a->top = a->bottom + a->cur->cap;
}

static void
free_chunks(ArenaChunk *c)
{
    while (c != NULL) {
        ArenaChunk *next = c->next;
        free(c);
        c = next;
    }
}

void
arena_free(Arena *a)
{
    free_chunks(a->cur);
    free_chunks(a->free_list);
    *a = (Arena){ 0 };
}
