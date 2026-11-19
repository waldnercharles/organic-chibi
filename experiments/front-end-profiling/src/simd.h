#ifndef CHIBI_SIMD_H
#define CHIBI_SIMD_H

#include "common.h"

#include <string.h>

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

typedef u32 u32x4 __attribute__((vector_size(16)));

typedef struct
{
    u8x16 v[4];
} u8x64;

static inline u8x16
simd_load(const void *p)
{
    u8x16 v;
    memcpy(&v, p, sizeof v);
    return v;
}

#define simd_eq(a, b) ((__typeof__(a))((a) == (b)))

static inline u8x16
simd_any_bits(u8x16 a, u8x16 b)
{
    return (u8x16)((a & b) != 0);
}

static inline u8x16
simd_in_range(u8x16 v, u8 lo, u8 hi)
{
    return (u8x16)(v - lo <= (u8)(hi - lo));
}

#if defined(CHIBI_SIMD_NEON)

typedef u8 u8x8 __attribute__((vector_size(8)));
typedef u16 u16x8 __attribute__((vector_size(16)));

static inline u8x16
simd_lookup16(u8x16 table, u8x16 idx)
{
    return vqtbl1q_u8(table, idx);
}

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
simd_load64(const u8 *p)
{
    uint8x16x4_t q = vld4q_u8(p);
    return (u8x64){ { q.val[0], q.val[1], q.val[2], q.val[3] } };
}

static inline u8x64
simd_next64(u8x64 v, const u8 *p)
{
    u8x16 byte64 = vld1q_dup_u8(p + 64);
    u8x16 after3 = vextq_u8(v.v[0], byte64, 1);
    return (u8x64){ { v.v[1], v.v[2], v.v[3], after3 } };
}

// NEON has no movemask.
static inline u64
simd_mask64(u8x16 a, u8x16 b, u8x16 c, u8x16 d)
{
    u8x16 ab = vsriq_n_u8(b, a, 1);
    u8x16 cd = vsriq_n_u8(d, c, 1);
    u8x16 abcd = vsriq_n_u8(cd, ab, 2);
    abcd = vsriq_n_u8(abcd, abcd, 4);
    return shrink_to_u64(abcd);
}

#elif defined(CHIBI_SIMD_SSE)

static inline u8x16
simd_lookup16(u8x16 table, u8x16 idx)
{
    return (u8x16)_mm_shuffle_epi8((__m128i)table, (__m128i)idx);
}

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

static inline u64
simd_mask64(u8x16 a, u8x16 b, u8x16 c, u8x16 d)
{
    u64 m0 = (u32)_mm_movemask_epi8((__m128i)a);
    u64 m1 = (u32)_mm_movemask_epi8((__m128i)b);
    u64 m2 = (u32)_mm_movemask_epi8((__m128i)c);
    u64 m3 = (u32)_mm_movemask_epi8((__m128i)d);
    return m0 | m1 << 16 | m2 << 32 | m3 << 48;
}

#else

static inline u8x16
simd_lookup16(u8x16 table, u8x16 idx)
{
    u8x16 r;
    for (int i = 0; i < 16; i++) {
        r[i] = table[idx[i] & 15];
    }
    return r;
}

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

static inline u64
simd_mask64(u8x16 a, u8x16 b, u8x16 c, u8x16 d)
{
    return simd_mask16(a) | simd_mask16(b) << 16 | simd_mask16(c) << 32 |
           simd_mask16(d) << 48;
}

#endif

#if !defined(CHIBI_SIMD_NEON)
static inline u8x64
simd_load64(const void *p)
{
    const u8 *b = p;
    return (u8x64){
        { simd_load(b), simd_load(b + 16), simd_load(b + 32), simd_load(b + 48) }
    };
}

static inline u8x64
simd_next64(u8x64 v, const void *p)
{
    (void)v;
    return simd_load64((const u8 *)p + 1);
}
#endif

#endif
