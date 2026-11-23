#include "exact.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LIMBS ((EXACT_FRACTION_BITS + 64) / 64)
#define WORK_LIMBS (2 * MAX_LIMBS + 24)

typedef struct
{
    u32 n;
    u64 w[WORK_LIMBS];
} Mag;

typedef struct
{
    u64 lo;
    u64 hi;
} Wide;

static const u64 pow10_u64[20] = {
    1ull,
    10ull,
    100ull,
    1000ull,
    10000ull,
    100000ull,
    1000000ull,
    10000000ull,
    100000000ull,
    1000000000ull,
    10000000000ull,
    100000000000ull,
    1000000000000ull,
    10000000000000ull,
    100000000000000ull,
    1000000000000000ull,
    10000000000000000ull,
    100000000000000000ull,
    1000000000000000000ull,
    10000000000000000000ull,
};

static void
mag_trim(Mag *m)
{
    while (m->n > 0 && m->w[m->n - 1] == 0) {
        m->n--;
    }
}

static void
mag_set(Mag *m, u64 v)
{
    m->w[0] = v;
    m->n = v != 0;
}

static u32
mag_bits(const Mag *m)
{
    return m->n ? 64 * m->n - (u32)__builtin_clzll(m->w[m->n - 1]) : 0;
}

static bool
mag_is_one(const Mag *m)
{
    return m->n == 1 && m->w[0] == 1;
}

static int
mag_cmp(const Mag *a, const Mag *b)
{
    if (a->n != b->n) {
        return a->n < b->n ? -1 : 1;
    }
    for (u32 i = a->n; i-- > 0;) {
        if (a->w[i] != b->w[i]) {
            return a->w[i] < b->w[i] ? -1 : 1;
        }
    }
    return 0;
}

static void
mag_copy(Mag *r, const Mag *a)
{
    r->n = a->n;
    memcpy(r->w, a->w, a->n * sizeof *a->w);
}

static void
mag_add(Mag *r, const Mag *a, const Mag *b)
{
    const Mag *x = a->n >= b->n ? a : b;
    const Mag *y = x == a ? b : a;
    u32 n = x->n;
    u32 m = y->n;

    u64 carry = 0;
    for (u32 i = 0; i < n; i++) {
        u64 v = x->w[i];
        u64 s = v + (i < m ? y->w[i] : 0);
        u64 t = s + carry;
        carry = (s < v) | (t < s);
        r->w[i] = t;
    }

    r->n = n;
    if (carry) {
        assert(r->n < WORK_LIMBS);
        r->w[r->n++] = carry;
    }
}

static void
mag_sub(Mag *r, const Mag *a, const Mag *b)
{
    assert(mag_cmp(a, b) >= 0);
    u64 borrow = 0;
    for (u32 i = 0; i < a->n; i++) {
        u64 x = a->w[i];
        u64 y = i < b->n ? b->w[i] : 0;
        u64 d = x - y;
        u64 next = (x < y) | (d < borrow);
        r->w[i] = d - borrow;
        borrow = next;
    }
    assert(borrow == 0);

    r->n = a->n;
    mag_trim(r);
}

static Wide
mul_add(u64 a, u64 b, u64 c, u64 d)
{
    u64 a0 = a & 0xFFFFFFFF;
    u64 a1 = a >> 32;
    u64 b0 = b & 0xFFFFFFFF;
    u64 b1 = b >> 32;
    u64 low = a0 * b0;
    u64 mid0 = a0 * b1;
    u64 mid1 = a1 * b0;
    u64 high = a1 * b1;
    u64 mid = (low >> 32) + (mid0 & 0xFFFFFFFF) + (mid1 & 0xFFFFFFFF);

    u64 lo = mid << 32 | (low & 0xFFFFFFFF);
    u64 hi = high + (mid0 >> 32) + (mid1 >> 32) + (mid >> 32);

    u64 s = lo + c;
    hi += s < c;
    u64 t = s + d;
    hi += t < d;
    return (Wide){ t, hi };
}

static void
mag_mul(Mag *r, const Mag *a, const Mag *b)
{
    assert(r != a && r != b);
    if (a->n == 0 || b->n == 0) {
        r->n = 0;
        return;
    }

    assert(a->n + b->n <= WORK_LIMBS);
    memset(r->w, 0, (a->n + b->n) * sizeof *r->w);
    for (u32 i = 0; i < a->n; i++) {
        u64 carry = 0;
        for (u32 j = 0; j < b->n; j++) {
            Wide t = mul_add(a->w[i], b->w[j], r->w[i + j], carry);
            r->w[i + j] = t.lo;
            carry = t.hi;
        }
        r->w[i + b->n] = carry;
    }

    r->n = a->n + b->n;
    mag_trim(r);
}

static void
mag_mul_add(Mag *m, u64 factor, u64 add)
{
    u64 carry = add;
    for (u32 i = 0; i < m->n; i++) {
        Wide t = mul_add(m->w[i], factor, carry, 0);
        m->w[i] = t.lo;
        carry = t.hi;
    }
    if (carry) {
        assert(m->n < WORK_LIMBS);
        m->w[m->n++] = carry;
    }
}

static void
mag_shl(Mag *r, const Mag *a, u32 shift)
{
    if (a->n == 0) {
        r->n = 0;
        return;
    }

    u32 words = shift / 64;
    u32 bits = shift % 64;
    u32 n = a->n + words + 1;
    assert(n <= WORK_LIMBS);

    r->w[n - 1] = bits ? a->w[a->n - 1] >> (64 - bits) : 0;
    for (u32 i = a->n; i-- > 1;) {
        r->w[i + words] = a->w[i] << bits | (bits ? a->w[i - 1] >> (64 - bits) : 0);
    }
    r->w[words] = a->w[0] << bits;
    memset(r->w, 0, words * sizeof *r->w);

    r->n = n;
    mag_trim(r);
}

static void
mag_shr(Mag *r, const Mag *a, u32 shift)
{
    u32 words = shift / 64;
    u32 bits = shift % 64;
    if (words >= a->n) {
        r->n = 0;
        return;
    }

    u32 n = a->n - words;
    for (u32 i = 0; i < n; i++) {
        u64 hi = i + words + 1 < a->n ? a->w[i + words + 1] : 0;
        r->w[i] = a->w[i + words] >> bits | (bits ? hi << (64 - bits) : 0);
    }

    r->n = n;
    mag_trim(r);
}

static u32
mag_ctz(const Mag *m)
{
    u32 i = 0;
    while (m->w[i] == 0) {
        i++;
    }
    return 64 * i + (u32)__builtin_ctzll(m->w[i]);
}

static u64
div_wide(u64 hi, u64 lo, u64 d, u64 *rem)
{
    assert(hi < d);
    if (hi == 0) {
        *rem = lo % d;
        return lo / d;
    }

    u32 s = (u32)__builtin_clzll(d);
    d <<= s;
    hi = hi << s | (s ? lo >> (64 - s) : 0);
    lo <<= s;

    u64 d1 = d >> 32;
    u64 d0 = d & 0xFFFFFFFF;
    u64 l1 = lo >> 32;
    u64 l0 = lo & 0xFFFFFFFF;

    u64 q1 = hi / d1;
    u64 r = hi - q1 * d1;
    while (q1 >> 32 || q1 * d0 > (r << 32 | l1)) {
        q1--;
        r += d1;
        if (r >> 32) {
            break;
        }
    }

    u64 mid = (hi << 32 | l1) - q1 * d;
    u64 q0 = mid / d1;
    r = mid - q0 * d1;
    while (q0 >> 32 || q0 * d0 > (r << 32 | l0)) {
        q0--;
        r += d1;
        if (r >> 32) {
            break;
        }
    }

    *rem = ((mid << 32 | l0) - q0 * d) >> s;
    return q1 << 32 | q0;
}

static void
mag_divmod(Mag *q, Mag *r, const Mag *a, const Mag *b)
{
    assert(b->n > 0 && q != r);
    if (mag_cmp(a, b) < 0) {
        q->n = 0;
        mag_copy(r, a);
        return;
    }

    u32 n = b->n;
    u32 m = a->n - n;
    if (n == 1) {
        u64 d = b->w[0];
        u64 rem = 0;
        for (u32 i = a->n; i-- > 0;) {
            q->w[i] = div_wide(rem, a->w[i], d, &rem);
        }
        q->n = a->n;
        mag_trim(q);
        mag_set(r, rem);
        return;
    }

    u32 s = (u32)__builtin_clzll(b->w[n - 1]);
    u64 vn[WORK_LIMBS];
    u64 un[WORK_LIMBS + 1];
    for (u32 i = n - 1; i > 0; i--) {
        vn[i] = b->w[i] << s | (s ? b->w[i - 1] >> (64 - s) : 0);
    }
    vn[0] = b->w[0] << s;
    un[a->n] = s ? a->w[a->n - 1] >> (64 - s) : 0;
    for (u32 i = a->n - 1; i > 0; i--) {
        un[i] = a->w[i] << s | (s ? a->w[i - 1] >> (64 - s) : 0);
    }
    un[0] = a->w[0] << s;

    for (u32 j = m + 1; j-- > 0;) {
        u64 qhat = UINT64_MAX;
        u64 rhat = un[j + n - 1] + vn[n - 1];
        bool fits = rhat >= vn[n - 1];
        if (un[j + n] < vn[n - 1]) {
            qhat = div_wide(un[j + n], un[j + n - 1], vn[n - 1], &rhat);
            fits = true;
        }
        while (fits) {
            Wide p = mul_add(qhat, vn[n - 2], 0, 0);
            if (p.hi < rhat || (p.hi == rhat && p.lo <= un[j + n - 2])) {
                break;
            }
            qhat--;
            rhat += vn[n - 1];
            fits = rhat >= vn[n - 1];
        }

        u64 carry = 0;
        u64 borrow = 0;
        for (u32 i = 0; i < n; i++) {
            Wide p = mul_add(qhat, vn[i], carry, 0);
            carry = p.hi;
            u64 lo = p.lo;
            u64 x = un[i + j];
            u64 d = x - lo;
            u64 next = (x < lo) | (d < borrow);
            un[i + j] = d - borrow;
            borrow = next;
        }
        u64 x = un[j + n];
        u64 d = x - carry;
        bool negative = (x < carry) | (d < borrow);
        un[j + n] = d - borrow;

        q->w[j] = qhat;
        if (negative) {
            q->w[j]--;
            u64 c = 0;
            for (u32 i = 0; i < n; i++) {
                u64 s = un[i + j] + vn[i];
                u64 t = s + c;
                c = (s < vn[i]) | (t < s);
                un[i + j] = t;
            }
            un[j + n] += c;
        }
    }

    q->n = m + 1;
    mag_trim(q);
    for (u32 i = 0; i < n; i++) {
        r->w[i] = un[i] >> s | (s ? un[i + 1] << (64 - s) : 0);
    }
    r->n = n;
    mag_trim(r);
}

static void
mag_gcd(Mag *g, const Mag *a, const Mag *b)
{
    if (a->n == 1 && b->n == 1) {
        u64 x = a->w[0];
        u64 y = b->w[0];
        u32 shift = (u32)__builtin_ctzll(x | y);
        x >>= __builtin_ctzll(x);
        while (y) {
            y >>= __builtin_ctzll(y);
            if (x > y) {
                u64 t = x;
                x = y;
                y = t;
            }
            y -= x;
        }
        mag_set(g, x << shift);
        return;
    }

    Mag x, y, q, r;
    mag_copy(&x, a);
    mag_copy(&y, b);
    while (y.n) {
        mag_divmod(&q, &r, &x, &y);
        mag_copy(&x, &y);
        mag_copy(&y, &r);
    }
    mag_copy(g, &x);
}

static void
mag_pow10(Mag *m, u32 k)
{
    mag_set(m, pow10_u64[k % 19]);
    for (; k >= 19; k -= 19) {
        mag_mul_add(m, pow10_u64[19], 0);
    }
}

static Exact *
add_value(Exacts *e)
{
    if (e->count == e->cap) {
        e->cap *= 2;
        e->values = realloc(e->values, e->cap * sizeof *e->values);
    }
    return &e->values[e->count++];
}

u32
exact_int(Exacts *e, int64_t v)
{
    Exact *x = add_value(e);
    *x = (Exact){ .small = v };
    return e->count - 1;
}

void
exacts_init(Exacts *e)
{
    *e = (Exacts){ 0 };
    e->cap = 64;
    e->values = malloc(e->cap * sizeof *e->values);
    e->limbs_cap = 64;
    e->limbs = malloc(e->limbs_cap * sizeof *e->limbs);

    [[maybe_unused]] u32 zero = exact_int(e, 0);
    [[maybe_unused]] u32 one = exact_int(e, 1);
    [[maybe_unused]] u32 minus_one = exact_int(e, -1);
    assert(zero == EXACT_ZERO && one == EXACT_ONE && minus_one == EXACT_MINUS_ONE);
}

void
exacts_free(Exacts *e)
{
    free(e->values);
    free(e->limbs);
}

static void
load(const Exacts *e, u32 a, Mag *num, Mag *den, bool *negative)
{
    Exact x = e->values[a];
    if (!x.big) {
        mag_set(num, x.small < 0 ? 0 - (u64)x.small : (u64)x.small);
        mag_set(den, 1);
        *negative = x.small < 0;
        return;
    }

    num->n = x.num;
    memcpy(num->w, e->limbs + x.small, x.num * sizeof *num->w);
    if (x.den) {
        den->n = x.den;
        memcpy(den->w, e->limbs + x.small + x.num, x.den * sizeof *den->w);
    } else {
        mag_set(den, 1);
    }
    *negative = x.negative;
}

static u32
store(Exacts *e, const Mag *num, const Mag *den, bool negative)
{
    bool integer = mag_is_one(den);
    if (integer && num->n <= 1) {
        u64 m = num->n ? num->w[0] : 0;
        if (m <= (u64)INT64_MAX + negative) {
            return exact_int(e, negative ? (int64_t)(0 - m) : (int64_t)m);
        }
    }

    u32 dn = integer ? 0 : den->n;
    u32 need = num->n + dn;
    if (e->nlimbs + need > e->limbs_cap) {
        e->limbs_cap = (e->nlimbs + need) * 2;
        e->limbs = realloc(e->limbs, e->limbs_cap * sizeof *e->limbs);
    }

    u32 first = e->nlimbs;
    memcpy(e->limbs + first, num->w, num->n * sizeof *num->w);
    memcpy(e->limbs + first + num->n, den->w, dn * sizeof *den->w);
    e->nlimbs += need;

    Exact *x = add_value(e);
    *x = (Exact){ .small = first,
                  .num = (u16)num->n,
                  .den = (u16)dn,
                  .big = true,
                  .negative = negative && num->n };
    return e->count - 1;
}

static u32
finish(Exacts *e, Mag *num, Mag *den, bool negative, u32 limit, ExactStatus *status)
{
    if (num->n == 0) {
        return EXACT_ZERO;
    }
    if (!mag_is_one(den)) {
        Mag g, q, r;
        mag_gcd(&g, num, den);
        if (!mag_is_one(&g)) {
            mag_divmod(&q, &r, num, &g);
            mag_copy(num, &q);
            mag_divmod(&q, &r, den, &g);
            mag_copy(den, &q);
        }
    }
    if (mag_bits(num) > limit || mag_bits(den) > limit) {
        *status = EXACT_TOO_LARGE;
        return EXACT_ZERO;
    }
    return store(e, num, den, negative);
}

static u32
add_digits(Mag *m, const char *p, u32 base, u32 *ndigits)
{
    u64 chunk = 0;
    u64 scale = 1;
    u32 n = 0;
    const char *q = p;
    for (;; q++) {
        u32 d;
        char ch = *q;
        if (ch >= '0' && ch <= '9') {
            d = (u32)(ch - '0');
        } else if (base == 16 && (ch | 0x20) >= 'a' && (ch | 0x20) <= 'f') {
            d = (u32)((ch | 0x20) - 'a' + 10);
        } else if (ch == '_') {
            continue;
        } else {
            break;
        }

        assert(d < base);
        if (scale > UINT64_MAX / base) {
            mag_mul_add(m, scale, chunk);
            if (m->n > MAX_LIMBS + 1) {
                return 0;
            }
            chunk = 0;
            scale = 1;
        }
        chunk = chunk * base + d;
        scale *= base;
        n++;
    }
    mag_mul_add(m, scale, chunk);
    mag_trim(m);
    *ndigits = n;
    return (u32)(q - p);
}

u32
exact_parse(Exacts *e, const char *text, u32 limit, ExactStatus *status)
{
    *status = EXACT_OK;
    const char *p = text;
    u32 base = 10;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'b' || p[1] == 'o')) {
        base = p[1] == 'x' ? 16 : p[1] == 'b' ? 2 : 8;
        p += 2;
    }

    u64 fast = 0;
    const char *q = p;
    while ((*q >= '0' && *q <= '9') || *q == '_') {
        if (*q != '_') {
            u64 d = (u64)(*q - '0');
            if (fast > (UINT64_MAX - d) / 10) {
                break;
            }
            fast = fast * 10 + d;
        }
        q++;
    }
    u32 places = 0;
    if (base == 10 && q[0] == '.' && q[1] >= '0' && q[1] <= '9') {
        for (q++; (*q >= '0' && *q <= '9') || *q == '_'; q++) {
            if (*q != '_') {
                u64 d = (u64)(*q - '0');
                if (places == 19 || fast > (UINT64_MAX - d) / 10) {
                    break;
                }
                fast = fast * 10 + d;
                places++;
            }
        }
    }
    bool plain = base == 10 && *q != 'e' && *q != 'E' &&
                 !((*q >= '0' && *q <= '9') || *q == '_');
    if (plain && places == 0 && fast <= (u64)INT64_MAX) {
        return exact_int(e, (int64_t)fast);
    }

    Mag num, den;
    if (plain) {
        mag_set(&num, fast);
        mag_set(&den, pow10_u64[places]);
        return finish(e, &num, &den, false, limit, status);
    }

    mag_set(&num, 0);
    u32 ndigits;
    u32 used = add_digits(&num, p, base, &ndigits);
    if (used == 0) {
        *status = EXACT_TOO_LARGE;
        return EXACT_ZERO;
    }
    p += used;
    mag_set(&den, 1);
    if (base != 10 || (*p != '.' && *p != 'e' && *p != 'E')) {
        return finish(e, &num, &den, false, limit, status);
    }

    int64_t scale = 0;
    if (p[0] == '.' && p[1] >= '0' && p[1] <= '9') {
        used = add_digits(&num, p + 1, 10, &ndigits);
        if (used == 0) {
            *status = EXACT_TOO_LARGE;
            return EXACT_ZERO;
        }
        p += 1 + used;
        scale = -(int64_t)ndigits;
    }
    if (*p == 'e' || *p == 'E') {
        p++;
        bool minus = *p == '-';
        p += *p == '-' || *p == '+';
        int64_t exponent = 0;
        while ((*p >= '0' && *p <= '9') || *p == '_') {
            if (*p != '_' && exponent < 1000000) {
                exponent = exponent * 10 + (*p - '0');
            }
            p++;
        }
        scale += minus ? -exponent : exponent;
    }

    if (num.n == 0) {
        return EXACT_ZERO;
    }

    int64_t bits = mag_bits(&num);
    int64_t scale_bits = (scale < 0 ? -scale : scale) * 3321928 / 1000000;
    if (scale > 0 ? bits + scale_bits > (int64_t)limit + 1
                  : scale_bits - bits > (int64_t)limit + 1) {
        *status = EXACT_TOO_LARGE;
        return EXACT_ZERO;
    }

    Mag power, t;
    mag_pow10(&power, (u32)(scale < 0 ? -scale : scale));
    if (scale > 0) {
        mag_mul(&t, &num, &power);
        mag_copy(&num, &t);
    } else {
        mag_copy(&den, &power);
    }
    return finish(e, &num, &den, false, limit, status);
}

static void
to_twos(u64 *w, u32 n, const Mag *m, bool negative)
{
    for (u32 i = 0; i < n; i++) {
        w[i] = i < m->n ? m->w[i] : 0;
    }
    if (negative) {
        u64 carry = 1;
        for (u32 i = 0; i < n; i++) {
            u64 t = ~w[i] + carry;
            carry &= t == 0;
            w[i] = t;
        }
    }
}

static bool
bitwise(Mag *r, ExactOp op, const Mag *a, bool as, const Mag *b, bool bs)
{
    u32 n = (a->n > b->n ? a->n : b->n) + 1;
    u64 x[WORK_LIMBS];
    u64 y[WORK_LIMBS];
    to_twos(x, n, a, as);
    to_twos(y, n, b, bs);

    for (u32 i = 0; i < n; i++) {
        r->w[i] = op == EXACT_AND ? x[i] & y[i]
                : op == EXACT_OR  ? x[i] | y[i]
                                  : x[i] ^ y[i];
    }

    bool negative = r->w[n - 1] >> 63;
    r->n = n;
    if (negative) {
        Mag m = *r;
        to_twos(r->w, n, &m, true);
    }
    mag_trim(r);
    return negative;
}

static NOINLINE u32
exact_slow(Exacts *e, ExactOp op, u32 a, u32 b, u32 limit, ExactStatus *status)
{
    Mag an, ad, bn, bd, rn, rd, x, y;
    bool as, bs;
    bool rs = false;
    load(e, a, &an, &ad, &as);
    load(e, b, &bn, &bd, &bs);
    mag_set(&rd, 1);

    switch (op) {
        case EXACT_ADD:
        case EXACT_SUB:
            if (op == EXACT_SUB) {
                bs = !bs && bn.n;
            }
            mag_mul(&x, &an, &bd);
            mag_mul(&y, &bn, &ad);
            if (as == bs) {
                mag_add(&rn, &x, &y);
                rs = as;
            } else if (mag_cmp(&x, &y) >= 0) {
                mag_sub(&rn, &x, &y);
                rs = as;
            } else {
                mag_sub(&rn, &y, &x);
                rs = bs;
            }
            mag_mul(&rd, &ad, &bd);
            break;
        case EXACT_MUL:
            mag_mul(&rn, &an, &bn);
            mag_mul(&rd, &ad, &bd);
            rs = as != bs;
            break;
        case EXACT_QUO:
            if (bn.n == 0) {
                *status = EXACT_DIVIDE_BY_ZERO;
                return EXACT_ZERO;
            }
            mag_mul(&rn, &an, &bd);
            mag_mul(&rd, &ad, &bn);
            rs = as != bs;
            break;
        case EXACT_DIV:
        case EXACT_REM:
            assert(mag_is_one(&ad) && mag_is_one(&bd));
            if (bn.n == 0) {
                *status = EXACT_DIVIDE_BY_ZERO;
                return EXACT_ZERO;
            }
            mag_divmod(&x, &y, &an, &bn);
            mag_copy(&rn, op == EXACT_DIV ? &x : &y);
            rs = op == EXACT_DIV ? as != bs : as;
            break;
        case EXACT_AND:
        case EXACT_OR:
        case EXACT_XOR:
            assert(mag_is_one(&ad) && mag_is_one(&bd));
            rs = bitwise(&rn, op, &an, as, &bn, bs);
            break;
        case EXACT_SHL:
        case EXACT_SHR: {
            assert(mag_is_one(&ad) && mag_is_one(&bd) && !bs);
            u64 count = bn.n == 0 ? 0 : bn.n == 1 ? bn.w[0] : UINT64_MAX;
            rs = as;
            if (op == EXACT_SHL) {
                if (an.n && (count > limit || mag_bits(&an) + count > limit)) {
                    *status = EXACT_TOO_LARGE;
                    return EXACT_ZERO;
                }
                mag_shl(&rn, &an, (u32)count);
            } else if (!as) {
                mag_shr(&rn, &an, count > 64u * WORK_LIMBS ? 64u * WORK_LIMBS : (u32)count);
            } else {
                Mag one;
                mag_set(&one, 1);
                mag_sub(&x, &an, &one);
                mag_shr(&y, &x, count > 64u * WORK_LIMBS ? 64u * WORK_LIMBS : (u32)count);
                mag_add(&rn, &y, &one);
            }
            break;
        }
        default: assert(false && "unknown exact operation"); return EXACT_ZERO;
    }

    return finish(e, &rn, &rd, rs, limit, status);
}

u32
exact_binary(Exacts *e, ExactOp op, u32 a, u32 b, u32 limit, ExactStatus *status)
{
    *status = EXACT_OK;
    Exact x = e->values[a];
    Exact y = e->values[b];
    if (x.big || y.big) {
        return exact_slow(e, op, a, b, limit, status);
    }

    int64_t u = x.small;
    int64_t v = y.small;
    int64_t r;
    switch (op) {
        case EXACT_ADD:
            if (!__builtin_add_overflow(u, v, &r)) {
                return exact_int(e, r);
            }
            break;
        case EXACT_SUB:
            if (!__builtin_sub_overflow(u, v, &r)) {
                return exact_int(e, r);
            }
            break;
        case EXACT_MUL:
            if (!__builtin_mul_overflow(u, v, &r)) {
                return exact_int(e, r);
            }
            break;
        case EXACT_QUO:
        case EXACT_DIV:
        case EXACT_REM:
            if (v == 0) {
                *status = EXACT_DIVIDE_BY_ZERO;
                return EXACT_ZERO;
            }
            if (u == INT64_MIN && v == -1) {
                break;
            }
            if (op == EXACT_REM) {
                return exact_int(e, u % v);
            }
            if (op == EXACT_DIV || u % v == 0) {
                return exact_int(e, u / v);
            }
            break;
        case EXACT_AND: return exact_int(e, u & v);
        case EXACT_OR: return exact_int(e, u | v);
        case EXACT_XOR: return exact_int(e, u ^ v);
        case EXACT_SHL:
            assert(v >= 0);
            if (v < 63) {
                r = (int64_t)((u64)u << v);
                if (r >> v == u) {
                    return exact_int(e, r);
                }
            }
            break;
        case EXACT_SHR:
            assert(v >= 0);
            return exact_int(e, v >= 63 ? (u < 0 ? -1 : 0) : u >> v);
        default: assert(false && "unknown exact operation");
    }
    return exact_slow(e, op, a, b, limit, status);
}

int
exact_sign(const Exacts *e, u32 a)
{
    Exact x = e->values[a];
    if (!x.big) {
        return (x.small > 0) - (x.small < 0);
    }
    return x.negative ? -1 : 1;
}

int
exact_compare(const Exacts *e, u32 a, u32 b)
{
    Exact x = e->values[a];
    Exact y = e->values[b];
    if (!x.big && !y.big) {
        return (x.small > y.small) - (x.small < y.small);
    }

    int sa = exact_sign(e, a);
    int sb = exact_sign(e, b);
    if (sa != sb) {
        return sa < sb ? -1 : 1;
    }

    Mag an, ad, bn, bd, l, r;
    bool as, bs;
    load(e, a, &an, &ad, &as);
    load(e, b, &bn, &bd, &bs);
    mag_mul(&l, &an, &bd);
    mag_mul(&r, &bn, &ad);
    int c = mag_cmp(&l, &r);
    return sa < 0 ? -c : c;
}

bool
exact_is_int(const Exacts *e, u32 a)
{
    Exact x = e->values[a];
    return !x.big || x.den == 0;
}

static NOINLINE bool
big_fits(const Exacts *e, Exact x, u32 bits, bool is_signed)
{
    if (x.den != 0 || (x.negative && !is_signed)) {
        return false;
    }

    u64 top = e->limbs[x.small + x.num - 1];
    u32 length = 64 * x.num - (u32)__builtin_clzll(top);
    if (length <= bits - is_signed) {
        return true;
    }
    bool power = (top & (top - 1)) == 0 && (x.num == 1 || e->limbs[x.small] == 0);
    return x.negative && length == bits && power;
}

bool
exact_fits(const Exacts *e, u32 a, u32 bits, bool is_signed)
{
    assert(bits >= 1 && bits <= 128);
    Exact x = e->values[a];
    if (x.big) {
        return big_fits(e, x, bits, is_signed);
    }
    if (bits >= 64) {
        return is_signed || x.small >= 0;
    }
    if (is_signed) {
        int64_t max = ((int64_t)1 << (bits - 1)) - 1;
        return x.small >= -max - 1 && x.small <= max;
    }
    return x.small >= 0 && (u64)x.small < (u64)1 << bits;
}

u64
exact_bits(const Exacts *e, u32 a)
{
    Exact x = e->values[a];
    if (!x.big) {
        return (u64)x.small;
    }
    assert(x.den == 0);
    u64 low = e->limbs[x.small];
    return x.negative ? 0 - low : low;
}

u64
exact_bits_high(const Exacts *e, u32 a)
{
    Exact x = e->values[a];
    if (!x.big) {
        return x.small < 0 ? ~0ull : 0;
    }
    assert(x.den == 0);
    u64 low = e->limbs[x.small];
    u64 high = x.num > 1 ? e->limbs[x.small + 1] : 0;
    return x.negative ? ~high + (low == 0) : high;
}

static u64
float_slow(const Exacts *e, u32 a, u32 width, ExactStatus *status)
{
    Mag n, d;
    bool negative;
    load(e, a, &n, &d, &negative);

    int p = width == 32 ? 24 : 53;
    int emin = width == 32 ? -126 : -1022;
    int emax = width == 32 ? 127 : 1023;
    u64 sign = (u64)negative << (width - 1);

    int exp = (int)mag_bits(&n) - (int)mag_bits(&d);
    Mag t;
    if (exp >= 0) {
        mag_shl(&t, &d, (u32)exp);
        exp -= mag_cmp(&n, &t) < 0;
    } else {
        mag_shl(&t, &n, (u32)-exp);
        exp -= mag_cmp(&t, &d) < 0;
    }
    if (exp > emax) {
        *status = EXACT_TOO_LARGE;
        return 0;
    }

    int q = (exp < emin ? emin : exp) - (p - 1);
    Mag num, den, m, r;
    if (q >= 0) {
        mag_copy(&num, &n);
        mag_shl(&den, &d, (u32)q);
    } else {
        mag_shl(&num, &n, (u32)-q);
        mag_copy(&den, &d);
    }
    mag_divmod(&m, &r, &num, &den);

    u64 mant = m.n ? m.w[0] : 0;
    assert(m.n <= 1 && mant < (u64)1 << p);
    mag_shl(&t, &r, 1);
    int half = mag_cmp(&t, &den);
    if (half > 0 || (half == 0 && (mant & 1))) {
        mant++;
    }
    if (mant == (u64)1 << p) {
        mant >>= 1;
        q++;
    }

    if (mant < (u64)1 << (p - 1)) {
        return sign | mant;
    }
    int biased = q + (p - 1) + emax;
    if (q + (p - 1) > emax) {
        *status = EXACT_TOO_LARGE;
        return 0;
    }
    return sign | (u64)biased << (p - 1) | (mant - ((u64)1 << (p - 1)));
}

static u64
float_bits(double v, u32 width)
{
    if (width == 32) {
        float f = (float)v;
        u32 b;
        memcpy(&b, &f, sizeof b);
        return b;
    }
    u64 b;
    memcpy(&b, &v, sizeof b);
    return b;
}

u64
exact_float(const Exacts *e, u32 a, u32 width, ExactStatus *status)
{
    assert(width == 32 || width == 64);
    *status = EXACT_OK;
    Exact x = e->values[a];
    u64 limit = (u64)1 << (width == 32 ? 24 : 53);
    if (!x.big && x.small > -(int64_t)limit && x.small < (int64_t)limit) {
        return float_bits((double)x.small, width);
    }
    if (x.big && x.num == 1 && x.den == 1) {
        u64 n = e->limbs[x.small];
        u64 d = e->limbs[x.small + 1];
        if (n < limit && d < limit) {
            double q = (double)n / (double)d;
            return float_bits(x.negative ? -q : q, width);
        }
    }
    return float_slow(e, a, width, status);
}

u32
exact_from_float(Exacts *e, u64 bits, u32 width)
{
    int p = width == 32 ? 24 : 53;
    int bias = width == 32 ? 127 : 1023;
    u32 exp_bits = width - (u32)p;
    bool negative = bits >> (width - 1) & 1;
    int biased = (int)(bits >> (p - 1) & (((u64)1 << exp_bits) - 1));
    u64 mant = bits & (((u64)1 << (p - 1)) - 1);
    assert(biased != (1 << exp_bits) - 1);

    int exp = 1 - bias - (p - 1);
    if (biased) {
        mant |= (u64)1 << (p - 1);
        exp = biased - bias - (p - 1);
    }
    if (mant == 0) {
        return EXACT_ZERO;
    }

    int tz = __builtin_ctzll(mant);
    mant >>= tz;
    exp += tz;

    Mag num, den;
    mag_set(&num, mant);
    mag_set(&den, 1);
    if (exp >= 0) {
        Mag t;
        mag_shl(&t, &num, (u32)exp);
        mag_copy(&num, &t);
    } else {
        mag_shl(&den, &den, (u32)-exp);
    }
    return store(e, &num, &den, negative);
}

static char *
mag_decimal(const Mag *m, char *end)
{
    char *p = end;
    *--p = 0;

    Mag x, q, r, chunk;
    mag_copy(&x, m);
    mag_set(&chunk, pow10_u64[19]);
    while (x.n > 1 || (x.n == 1 && x.w[0] >= pow10_u64[19])) {
        mag_divmod(&q, &r, &x, &chunk);
        u64 v = r.n ? r.w[0] : 0;
        for (int i = 0; i < 19; i++) {
            *--p = (char)('0' + v % 10);
            v /= 10;
        }
        mag_copy(&x, &q);
    }

    u64 v = x.n ? x.w[0] : 0;
    do {
        *--p = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    return p;
}

void
exact_print(FILE *out, const Exacts *e, u32 a)
{
    Exact x = e->values[a];
    if (!x.big) {
        fprintf(out, "%lld", (long long)x.small);
        return;
    }

    char buf[WORK_LIMBS * 20 + 1];
    char *end = buf + sizeof buf;
    Mag num, den;
    bool negative;
    load(e, a, &num, &den, &negative);
    fputs(negative ? "-" : "", out);
    if (x.den == 0) {
        fputs(mag_decimal(&num, end), out);
        return;
    }

    u32 twos = mag_ctz(&den);
    Mag odd, q, r, five;
    mag_shr(&odd, &den, twos);
    mag_set(&five, 5);
    u32 fives = 0;
    while (!mag_is_one(&odd)) {
        mag_divmod(&q, &r, &odd, &five);
        if (r.n) {
            break;
        }
        mag_copy(&odd, &q);
        fives++;
    }

    u32 k = twos > fives ? twos : fives;
    if (!mag_is_one(&odd) || k > 60) {
        fputs(mag_decimal(&num, end), out);
        fputc('/', out);
        fputs(mag_decimal(&den, end), out);
        return;
    }

    Mag scale, scaled;
    mag_pow10(&scale, k);
    mag_mul(&scaled, &num, &scale);
    mag_divmod(&q, &r, &scaled, &den);

    const char *digits = mag_decimal(&q, end);
    size_t len = strlen(digits);
    if (len <= k) {
        fputs("0.", out);
        for (size_t i = len; i < k; i++) {
            fputc('0', out);
        }
        fputs(digits, out);
        return;
    }
    fwrite(digits, 1, len - k, out);
    fputc('.', out);
    fputs(digits + len - k, out);
}
