#ifndef CHIBI_NAMES_H
#define CHIBI_NAMES_H

#include "arena.h"
#include "common.h"

#include <string.h>

#define XXH_STATIC_LINKING_ONLY
#include "../vendor/xxhash/xxhash.h"

typedef struct
{
    u64 k0, k1;
    u64 hash;
    u32 len;
    u32 id;
} NameSlot;
static_assert(sizeof(NameSlot) == 32, "a slot must not straddle a cache line");

typedef struct
{
    const char *text;
    u32 len;
    u32 slot;
} Name;

typedef struct
{
    NameSlot *slots;
    u32 mask;
    Name *names;
    u32 count;
    u32 cap;
    Arena *arena;
} Names;

void names_init(Names *t, Arena *arena, u32 expected);

static inline Name
names_get(const Names *t, u32 id)
{
    return t->names[id];
}

u32 names_insert(Names *t, u32 slot, const char *s, u32 len, u64 k0, u64 k1, u64 hash);

void names_move_home(Names *t, u32 i, u32 home);

static inline u64
names_hash_short(u64 k0, u64 k1)
{
    __uint128_t m = (__uint128_t)(k0 ^ 0x2d358dccaa6c78a5ULL) *
                    (k1 ^ 0x8bb84b93962eacc9ULL);
    return (u64)m ^ (u64)(m >> 64);
}

static inline u64
names_hash(const char *s, u32 len, u64 k0, u64 k1)
{
    if (len > 16) {
        return XXH3_64bits(s, len);
    }
    return names_hash_short(k0, k1);
}

static inline u32
names_intern_hashed(Names *t, const char *s, u32 len, u64 k0, u64 k1, u64 h)
{
    u32 home = (u32)h & t->mask;
    for (u32 i = home;; i = (i + 1) & t->mask) {
        const NameSlot *slot = &t->slots[i];
        if (slot->id == 0) {
            return names_insert(t, i, s, len, k0, k1, h);
        }
        if (slot->k0 == k0 && slot->k1 == k1 && slot->len == len &&
            (len <= 16 || memcmp(t->names[slot->id].text + 16, s + 16, len - 16) == 0)) {
            u32 id = slot->id;
            if (i != home && len <= 16) {
                names_move_home(t, i, home);
            }
            return id;
        }
    }
}

static inline u32
names_intern(Names *t, const char *s, u32 len)
{
    u8 buf[16] = { 0 };
    memcpy(buf, s, len < 16 ? len : 16);

    u64 k0, k1;
    memcpy(&k0, buf, 8);
    memcpy(&k1, buf + 8, 8);

    return names_intern_hashed(t, s, len, k0, k1, names_hash(s, len, k0, k1));
}

#endif
