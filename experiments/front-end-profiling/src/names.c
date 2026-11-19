#include "names.h"

#include <stdio.h>
#include <stdlib.h>

#define NAMES_MIN_EXPECTED 32

static void *
alloc_or_exit(Names *t, size_t n, size_t align)
{
    void *p = arena_alloc(t->arena, n, align);
    if (!p) {
        fprintf(stderr, "chibi: out of memory (name table)\n");
        exit(1);
    }
    return p;
}

static u32
next_pow2(u64 x)
{
    u64 p = 1;
    while (p < x) {
        p <<= 1;
    }

    if (p > (1ULL << 31)) {
        fprintf(stderr, "chibi: name table too large\n");
        exit(1);
    }

    return (u32)p;
}

static NameSlot *
alloc_slots(Names *t, u32 nslots)
{
    NameSlot *slots = alloc_or_exit(t, (size_t)nslots * sizeof *slots, 64);
    memset(slots, 0, (size_t)nslots * sizeof *slots);
    return slots;
}

void
names_init(Names *t, Arena *arena, u32 expected)
{
    if (expected < NAMES_MIN_EXPECTED) {
        expected = NAMES_MIN_EXPECTED;
    }

    *t = (Names){ .arena = arena };
    u32 nslots = next_pow2((u64)expected * 2);
    t->slots = alloc_slots(t, nslots);
    t->mask = nslots - 1;

    t->cap = next_pow2((u64)expected + 1);
    t->names = alloc_or_exit(t, (size_t)t->cap * sizeof *t->names, alignof(Name));
    t->names[0] = (Name){ "", 0, 0 };
    t->count = 1;
}

static void
grow(Names *t)
{
    u32 nslots = next_pow2((u64)(t->mask + 1) * 2);
    u32 mask = nslots - 1;
    NameSlot *slots = alloc_slots(t, nslots);

    for (u32 id = 1; id < t->count; id++) {
        NameSlot s = t->slots[t->names[id].slot];
        u32 j = (u32)s.hash & mask;
        while (slots[j].id != 0) {
            j = (j + 1) & mask;
        }
        slots[j] = s;
        t->names[id].slot = j;
    }

    t->slots = slots;
    t->mask = mask;
}

u32
names_insert(Names *t, u32 slot, const char *s, u32 len, u64 k0, u64 k1, u64 hash)
{
    if ((u64)t->count * 2 > (u64)t->mask + 1) {
        grow(t);
        slot = (u32)hash & t->mask;
        while (t->slots[slot].id != 0) {
            slot = (slot + 1) & t->mask;
        }
    }

    if (t->count == t->cap) {
        Name *names = alloc_or_exit(t, (size_t)t->cap * 2 * sizeof *names, alignof(Name));
        memcpy(names, t->names, (size_t)t->cap * sizeof *names);
        t->names = names;
        t->cap *= 2;
    }

    u32 id = t->count++;
    t->names[id] = (Name){ s, len, slot };
    t->slots[slot] = (NameSlot){ .k0 = k0, .k1 = k1, .hash = hash, .len = len, .id = id };
    return id;
}

void
names_move_home(Names *t, u32 i, u32 home)
{
    NameSlot moved = t->slots[i], evicted = t->slots[home];
    t->slots[home] = moved;
    t->slots[i] = evicted;
    t->names[moved.id].slot = home;
    t->names[evicted.id].slot = i;
}
