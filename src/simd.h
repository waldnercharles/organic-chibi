#ifndef CHIBI_SIMD_H
#define CHIBI_SIMD_H

#include "common.h"

#include <string.h>

#pragma GCC diagnostic ignored "-Wpsabi"

#if !defined(CHIBI_SIMD_GENERIC) && defined(__aarch64__)
#define CHIBI_SIMD_NEON 1
#include <arm_neon.h>
typedef uint8x16_t u8x16;
#else
#if !defined(CHIBI_SIMD_GENERIC) && defined(__SSSE3__)
#define CHIBI_SIMD_SSE 1
#include <tmmintrin.h>
#endif
typedef u8 u8x16 __attribute__((vector_size(16)));
#endif

typedef u8 u8x64 __attribute__((vector_size(64)));
typedef u32 u32x4 __attribute__((vector_size(16)));

static inline u8x16
simd_load(const void *p)
{
    u8x16 v;
    memcpy(&v, p, sizeof v);
    return v;
}

#define simd_eq(a, b) ((__typeof__(a))((a) == (b)))

static inline u8x64
simd_any_bits(u8x64 a, u8x64 b)
{
    return (u8x64)((a & b) != 0);
}

static inline u8x64
simd_in_range(u8x64 v, u8 lo, u8 hi)
{
    return (u8x64)(v - lo <= (u8)(hi - lo));
}

#if defined(CHIBI_SIMD_NEON)

typedef u8 u8x8 __attribute__((vector_size(8)));
typedef u16 u16x8 __attribute__((vector_size(16)));

static inline u64
shrink_to_u64(u8x16 v)
{
    u8x8 narrowed = __builtin_convertvector((u16x8)v >> 4, u8x8);
    return (u64)narrowed;
}

static inline u64
simd_mask16(u8x16 yes)
{
    return shrink_to_u64(yes);
}

static inline unsigned
simd_first16(u64 mask)
{
    return (unsigned)__builtin_ctzll(mask) >> 2;
}

static inline u8x64
simd_load64(const void *p)
{
    uint8x16x4_t q = vld4q_u8(p);
    u8x64 v;
    memcpy(&v, &q, sizeof v);
    return v;
}

static inline u8x64
simd_next64(u8x64 v, const void *p)
{
    uint8x16x4_t q;
    memcpy(&q, &v, sizeof q);

    u8x16 byte64 = vld1q_dup_u8((const u8 *)p + 64);
    uint8x16x4_t next = { { q.val[1], q.val[2], q.val[3], vextq_u8(q.val[0], byte64, 1) } };

    memcpy(&v, &next, sizeof v);
    return v;
}

static inline u8x64
simd_lookup64(u8x16 table, u8x64 idx)
{
    uint8x16x4_t q;
    memcpy(&q, &idx, sizeof q);
    for (int i = 0; i < 4; i++) {
        q.val[i] = vqtbl1q_u8(table, q.val[i]);
    }

    memcpy(&idx, &q, sizeof idx);
    return idx;
}

// NEON has no movemask.
static inline u64
simd_mask64(u8x64 yes)
{
    uint8x16x4_t q;
    memcpy(&q, &yes, sizeof q);

    u8x16 ab = vsriq_n_u8(q.val[1], q.val[0], 1);
    u8x16 cd = vsriq_n_u8(q.val[3], q.val[2], 1);
    u8x16 abcd = vsriq_n_u8(cd, ab, 2);
    abcd = vsriq_n_u8(abcd, abcd, 4);
    return shrink_to_u64(abcd);
}

#elif defined(CHIBI_SIMD_SSE)

static inline u64
simd_mask16(u8x16 yes)
{
    return (u32)_mm_movemask_epi8((__m128i)yes);
}

static inline unsigned
simd_first16(u64 mask)
{
    return (unsigned)__builtin_ctzll(mask);
}

static inline u8x64
simd_lookup64(u8x16 table, u8x64 idx)
{
    u8x16 q[4];
    memcpy(q, &idx, sizeof q);
    for (int i = 0; i < 4; i++) {
        q[i] = (u8x16)_mm_shuffle_epi8((__m128i)table, (__m128i)q[i]);
    }

    memcpy(&idx, q, sizeof idx);
    return idx;
}

static inline u64
simd_mask64(u8x64 yes)
{
    u8x16 q[4];
    memcpy(q, &yes, sizeof q);

    u64 m = 0;
    for (int i = 0; i < 4; i++) {
        m |= (u64)simd_mask16(q[i]) << 16 * i;
    }
    return m;
}

#else

static inline u64
simd_mask16(u8x16 yes)
{
    u64 m = 0;
    for (int i = 0; i < 16; i++) {
        m |= (u64)(yes[i] >> 7) << i;
    }
    return m;
}

static inline unsigned
simd_first16(u64 mask)
{
    return (unsigned)__builtin_ctzll(mask);
}

static inline u8x64
simd_lookup64(u8x16 table, u8x64 idx)
{
    u8x64 r;
    for (int i = 0; i < 64; i++) {
        r[i] = table[idx[i] & 15];
    }
    return r;
}

static inline u64
simd_mask64(u8x64 yes)
{
    u64 m = 0;
    for (int i = 0; i < 64; i++) {
        m |= (u64)(yes[i] >> 7) << i;
    }
    return m;
}

#endif

#if !defined(CHIBI_SIMD_NEON)
static inline u8x64
simd_load64(const void *p)
{
    u8x64 v;
    memcpy(&v, p, sizeof v);
    return v;
}

static inline u8x64
simd_next64(u8x64 v, const void *p)
{
    (void)v;
    return simd_load64((const u8 *)p + 1);
}
#endif

#endif
