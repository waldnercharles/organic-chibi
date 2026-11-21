#include "vectors.h"

#include "../src/arm64.h"

const char VECTOR_BODY[] =
    "fn lanes(a: u8, n: u32, k: int) -> u8 {\n"
    "    v: u8x16 = a\n"
    "    t := u8x16.{ 32, 0, 0, 0, 0, 0, 0, 0, 0, 9, 10, 0, 0, 13, 0, 0 }\n"
    "    m := (v | 0x20) -% 97 <= t & ~(v >> n) ^ -%v\n"
    "    t[3] = a\n"
    "    t[k] += 1\n"
    "    return m[k] +% t[0]\n"
    "}\n"
    "\n"
    "fn mixed(a: u64, b: i32, k: int) -> u64 {\n"
    "    w := u64x2.{ a, 7 }\n"
    "    z: i32x4\n"
    "    z = (z +% -1) << 2 == b\n"
    "    q: u64x2 = p: { if a > 3 { break p w *% 3 }; break p 9 }\n"
    "    return w[k] +% q[k] +% u64(z[k] & 1)\n"
    "}\n";

const char VECTOR_RUNS[] =
    "fn ident_mask(b: u8) -> int {\n"
    "    v: u8x16\n"
    "    for k in 0..16 {\n"
    "        v[k] = b +% u8(k)\n"
    "    }\n"
    "    letter := (v | 0x20) -% 97 <= 25\n"
    "    digit := v -% 48 <= 9\n"
    "    r := letter | digit | (v == 95)\n"
    "    m := 0\n"
    "    for k in 0..16 {\n"
    "        if r[k] != 0 {\n"
    "            m |= 1 << k\n"
    "        }\n"
    "    }\n"
    "    return m\n"
    "}\n"
    "\n"
    "fn ws_mask(b: u8) -> int {\n"
    "    ws := u8x16.{ 32, 0, 0, 0, 0, 0, 0, 0, 0, 9, 10, 0, 0, 13, 0, 0 }\n"
    "    v: u8x16\n"
    "    for k in 0..16 {\n"
    "        v[k] = b +% u8(k)\n"
    "    }\n"
    "    idx := v & 15\n"
    "    r: u8x16\n"
    "    for k in 0..16 {\n"
    "        r[k] = ws[idx[k]]\n"
    "    }\n"
    "    hit := r == v\n"
    "    m := 0\n"
    "    for k in 0..16 {\n"
    "        if hit[k] == 255 {\n"
    "            m |= 1 << k\n"
    "        }\n"
    "    }\n"
    "    return m\n"
    "}\n"
    "\n"
    "fn wide(a: int, k: int) -> int {\n"
    "    x: i64x2 = a\n"
    "    y: u64x2 = u64(a & 0xFFFF)\n"
    "    z := x *% 3 +% 1\n"
    "    s := x < 0\n"
    "    t := y >= 5\n"
    "    return z[k] +% s[k] *% 10 +% i64(t[k] & 7)\n"
    "}\n"
    "\n"
    "fn shifts(b: i8, s: u32, k: int) -> int {\n"
    "    x: i8x16 = b\n"
    "    y := x >> s\n"
    "    z := (x << 2) ^ ~y\n"
    "    return int(z[k]) * 1000 + int(y[k])\n"
    "}\n"
    "\n"
    "fn pick(c: bool, a: u16, k: int) -> int {\n"
    "    x: u16x8 = a\n"
    "    v: u16x8 = p: { if c { break p x -% 1 }; break p 7 }\n"
    "    return int(v[k])\n"
    "}\n"
    "\n"
    "fn choose(op: int, a: u8, k: int) -> u8 {\n"
    "    x: u8x16 = a\n"
    "    r := switch op {\n"
    "        0 => x +% 1\n"
    "        1 => x\n"
    "        _ => 9\n"
    "    }\n"
    "    return r[k]\n"
    "}\n"
    "\n"
    "fn table(k: int) -> int {\n"
    "    t := i16x8.{ -1, 2, -300, 4, 5, 6, 7, 32767 }\n"
    "    u := u32x4.{ 1, 2, 3, 4000000000 }\n"
    "    return int(t[k]) + int(u[k & 3])\n"
    "}\n"
    "\n"
    "fn wide_constants(a: int, k: int) -> int {\n"
    "    x: i64x2 = a\n"
    "    y := (x +% 0x123456789) ^ -2\n"
    "    h: u16x8 = u16(a & 0xFFFF)\n"
    "    g := h +% 300\n"
    "    w: u32x4 = u32(a & 0xFFFFFFFF)\n"
    "    f := w ^ 511\n"
    "    return y[k] +% int(g[k]) * 3 +% int(f[k])\n"
    "}\n"
    "\n"
    "fn shift_edges(a: i32, n: int, k: int) -> int {\n"
    "    x: i32x4 = a\n"
    "    y := x >> 0\n"
    "    z := x << n\n"
    "    return int(y[k]) * 1000 + int(z[k])\n"
    "}\n"
    "\n"
    "fn load_lanes(off: int, k: int) -> int {\n"
    "    s: [48]u8\n"
    "    for i in 0..48 {\n"
    "        s[i] = u8(i) *% 13 +% 5\n"
    "    }\n"
    "    p: [*]u8 = &s\n"
    "    v := unaligned_load(u8x16, p + off)\n"
    "    return int(v[k])\n"
    "}\n"
    "\n"
    "fn load_numbers(off: int, w: int) -> int {\n"
    "    s: [48]u8\n"
    "    for i in 0..48 {\n"
    "        s[i] = u8(i) *% 29 +% 200\n"
    "    }\n"
    "    p: [*]u8 = &s\n"
    "    return switch w {\n"
    "        0 => unaligned_load(int, p + off)\n"
    "        1 => int(unaligned_load(u32, p + off))\n"
    "        2 => int(unaligned_load(i16, p + off))\n"
    "        3 => int(unaligned_load(i8, p + off))\n"
    "        _ => int(unaligned_load(u16, p + off))\n"
    "    }\n"
    "}\n"
    "\n"
    "fn load_words(k: int) -> int {\n"
    "    a := u32.[0x03020100, 0x07060504, 0x0B0A0908, 0x0F0E0D0C, 0x13121110]\n"
    "    q: [*]u32 = &a\n"
    "    v := unaligned_load(u8x16, q + 1)\n"
    "    return int(v[k])\n"
    "}\n"
    "\n"
    "fn store_lanes(off: int, k: int) -> int {\n"
    "    s: [48]u8\n"
    "    v: u8x16\n"
    "    for i in 0..16 {\n"
    "        v[i] = u8(i) +% 100\n"
    "    }\n"
    "    p: [*]u8 = &s\n"
    "    unaligned_store(p + off, v ^ 0x80)\n"
    "    return int(s[k])\n"
    "}\n"
    "\n"
    "fn store_numbers(off: int, k: int) -> int {\n"
    "    s: [24]u8\n"
    "    p: [*]u8 = &s\n"
    "    unaligned_store(p + off, u32(0x44332211))\n"
    "    unaligned_store(p + off + 4, i16(-2))\n"
    "    unaligned_store(p + off + 6, u8(7))\n"
    "    unaligned_store(p + off + 7, u64(0x0102030405060708))\n"
    "    return int(s[k])\n"
    "}\n"
    "\n"
    "fn store_words(k: int) -> int {\n"
    "    a: [6]u32\n"
    "    q: [*]u32 = &a\n"
    "    v: u8x16\n"
    "    for i in 0..16 {\n"
    "        v[i] = u8(i) +% 1\n"
    "    }\n"
    "    unaligned_store(q + 1, v)\n"
    "    return int(a[k])\n"
    "}\n"
    "\n"
    "fn floats(off: int) -> int {\n"
    "    s: [24]u8\n"
    "    p: [*]u8 = &s\n"
    "    unaligned_store(p + off, f64(1.5))\n"
    "    unaligned_store(p + off + 8, f32(-2.25))\n"
    "    x := unaligned_load(f64, p + off)\n"
    "    y := unaligned_load(f32, p + off + 8)\n"
    "    return int(x * 4.0) * 100 + int(y * 4.0)\n"
    "}\n"
    "\n"
    "fn lookups(a: u8, k: int) -> int {\n"
    "    t := u8x16.{ 200, 7, 33, 1, 0, 99, 254, 255, 128, 127, 64, 3, 2, 18, 45, 90 }\n"
    "    v: u8x16\n"
    "    for i in 0..16 {\n"
    "        v[i] = a +% u8(i)\n"
    "    }\n"
    "    w := lookup(t, v)\n"
    "    return int(w[k])\n"
    "}\n"
    "\n"
    "fn signed_lookups(a: i8, k: int) -> int {\n"
    "    t := i8x16.{ -1, 2, -3, 4, -5, 6, -7, 8, -9, 10, -11, 12, -13, 14, -15, 127 }\n"
    "    v: i8x16\n"
    "    for i in 0..16 {\n"
    "        v[i] = a +% i8(i)\n"
    "    }\n"
    "    w := lookup(t, v)\n"
    "    return int(w[k])\n"
    "}\n"
    "\n"
    "fn top_bits_of(a: int, w: int) -> int {\n"
    "    s: [16]u8\n"
    "    for i in 0..16 {\n"
    "        if ((a >> i) & 1) != 0 {\n"
    "            s[i] = u8(i) | 128\n"
    "        } else {\n"
    "            s[i] = u8(i) ^ 127\n"
    "        }\n"
    "    }\n"
    "    p: [*]u8 = &s\n"
    "    return switch w {\n"
    "        0 => int(top_bits(unaligned_load(u8x16, p)))\n"
    "        1 => int(top_bits(unaligned_load(i16x8, p)))\n"
    "        2 => int(top_bits(unaligned_load(u32x4, p)))\n"
    "        _ => int(top_bits(unaligned_load(i64x2, p)))\n"
    "    }\n"
    "}\n"
    "\n"
    "fn first_space(n: int, m: int) -> int {\n"
    "    s: [64]u8\n"
    "    for i in 0..64 {\n"
    "        s[i] = 97\n"
    "    }\n"
    "    s[n] = 32\n"
    "    s[m] = 9\n"
    "    ws := u8x16.{ 32, 0, 0, 0, 0, 0, 0, 0, 0, 9, 10, 0, 0, 13, 0, 0 }\n"
    "    p: [*]u8 = &s\n"
    "    at := 0\n"
    "    while at < 64 {\n"
    "        v := unaligned_load(u8x16, p + at)\n"
    "        hits := top_bits(lookup(ws, v & 15) == v)\n"
    "        if hits != 0 {\n"
    "            return at + trailing_zeros(hits)\n"
    "        }\n"
    "        at += 16\n"
    "    }\n"
    "    return -1\n"
    "}\n"
    "\n"
    "fn mask_zeros(a: int, w: int) -> int {\n"
    "    s: [16]u8\n"
    "    for i in 0..16 {\n"
    "        if ((a >> i) & 1) != 0 {\n"
    "            s[i] = 255\n"
    "        }\n"
    "    }\n"
    "    p: [*]u8 = &s\n"
    "    if w == 0 {\n"
    "        return leading_zeros(top_bits(unaligned_load(u8x16, p))) * 100 + trailing_zeros(top_bits(unaligned_load(u8x16, p)))\n"
    "    }\n"
    "    return leading_zeros(top_bits(unaligned_load(u32x4, p))) * 100 + trailing_zeros(top_bits(unaligned_load(u32x4, p)))\n"
    "}\n"
    "\n"
    "struct Pair { a: int, b: int }\n"
    "\n"
    "fn vadd(a: u8x16, b: u8x16) -> u8x16 {\n"
    "    return a +% b\n"
    "}\n"
    "\n"
    "fn call_add(x: u8, y: u8, k: int) -> int {\n"
    "    a: u8x16 = x\n"
    "    b := u8x16.{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 }\n"
    "    s := vadd(a, b +% y)\n"
    "    return s[k]\n"
    "}\n"
    "\n"
    "fn spread(a: u8x16, x: f64, b: u8x16, c: u8x16, d: u8x16, e: u8x16, f: u8x16, g: u8x16, h: u8x16, i: u8x16, n: int, j: u8x16) -> int {\n"
    "    s := a +% b *% 2 +% c *% 3 +% d *% 4 +% e *% 5 +% f *% 6 +% g *% 7 +% h *% 8 +% i *% 9 +% j *% 10\n"
    "    return int(s[n]) + int(x)\n"
    "}\n"
    "\n"
    "fn call_spread(x: u8, y: u8, k: int) -> int {\n"
    "    a: u8x16 = x\n"
    "    b := u8x16.{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 }\n"
    "    return spread(a, 1000.0, b, a +% 1, b +% 2, a +% 3, b +% 4, a +% 5, b +% y, a +% y, k, b *% 3)\n"
    "}\n"
    "\n"
    "fn mix(p1: int, p2: int, p3: int, p4: int, p5: int, p6: int, p7: int, p8: int, a: u8x16, b: u8x16, c: u8x16, d: u8x16, e: u8x16, f: u8x16, g: u8x16, h: u8x16, i: u8x16, q: i8) -> int {\n"
    "    s := a +% b *% 2 +% c *% 3 +% d *% 4 +% e *% 5 +% f *% 6 +% g *% 7 +% h *% 8\n"
    "    return p1 + p2 * 2 + p3 * 3 + p4 * 4 + p5 * 5 + p6 * 6 + p7 * 7 + p8 * 8 + int(s[0]) * 100 + int(i[1]) * 100000 + int(q) * 100000000\n"
    "}\n"
    "\n"
    "fn call_mix(x: u8, y: u8, k: int) -> int {\n"
    "    a: u8x16 = x\n"
    "    b: u8x16 = y\n"
    "    return mix(k, 2, 3, 4, 5, 6, 7, k + 8, a, b, a +% 1, b +% 1, a +% 2, b +% 2, a +% 3, b +% 3, a +% b, -3)\n"
    "}\n"
    "\n"
    "fn leaf(a: u8x16, x: f64, b: u8x16, y: f64) -> f64 {\n"
    "    return x * 2.0 + y + f64(a[0]) + f64(b[1])\n"
    "}\n"
    "\n"
    "fn call_leaf(x: u8, y: u8, k: int) -> int {\n"
    "    a: u8x16 = x\n"
    "    b: u8x16 = y\n"
    "    return int(leaf(a, 1.5, b, f64(k)))\n"
    "}\n"
    "\n"
    "fn ones() -> u8x16 {\n"
    "    v: u8x16 = 1\n"
    "    return v\n"
    "}\n"
    "\n"
    "fn after_call(v: u8x16) -> u8x16 {\n"
    "    return v +% ones()\n"
    "}\n"
    "\n"
    "fn call_after(x: u8, k: int) -> int {\n"
    "    a: u8x16 = x\n"
    "    r := after_call(a)\n"
    "    return r[k]\n"
    "}\n"
    "\n"
    "fn compose(x: u8, y: u8, k: int) -> int {\n"
    "    a: u8x16 = x\n"
    "    b: u8x16 = y\n"
    "    c := u8x16.{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 }\n"
    "    r := c +% vadd(vadd(a, b), c *% 2) +% vadd(c, 7)\n"
    "    return r[k]\n"
    "}\n"
    "\n"
    "fn rsum(v: u8x16, n: int) -> u8x16 {\n"
    "    if n == 0 {\n"
    "        return v\n"
    "    }\n"
    "    w := rsum(v +% 1, n - 1)\n"
    "    return w +% v\n"
    "}\n"
    "\n"
    "fn call_rsum(x: u8, n: int, k: int) -> int {\n"
    "    v := u8x16.{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 } +% x\n"
    "    r := rsum(v, n)\n"
    "    return r[k]\n"
    "}\n"
    "\n"
    "fn with_pair(p: Pair, v: u8x16, w: u8x16) -> int {\n"
    "    s := v +% w\n"
    "    return p.a * 1000 + p.b * 10 + int(s[3])\n"
    "}\n"
    "\n"
    "fn call_pair(x: u8, y: u8, k: int) -> int {\n"
    "    p: Pair = .{ .a = k, .b = 7 }\n"
    "    a: u8x16 = x\n"
    "    return with_pair(p, a, a +% y)\n"
    "}\n"
    "\n"
    "fn twice(v: u8x16) {\n"
    "    return v +% v\n"
    "}\n"
    "\n"
    "fn dropped(x: u8, k: int) -> int {\n"
    "    a: u8x16 = x\n"
    "    _ = vadd(a, a)\n"
    "    r := twice(a)\n"
    "    return r[k]\n"
    "}\n"
    "\n"
    "fn rest(s: []u8) -> []u8 {\n"
    "    return s[1..]\n"
    "}\n"
    "\n"
    "fn lanes_and_len(v: u8x16, s: []u8) -> int {\n"
    "    return int(v[2]) * 1000 + s.len * 10 + int(s[0])\n"
    "}\n"
    "\n"
    "fn call_slice(x: u8, n: int, k: int) -> int {\n"
    "    arr: [8]u8\n"
    "    for i in 0..8 {\n"
    "        arr[i] = u8(i) +% x\n"
    "    }\n"
    "    a: u8x16 = x\n"
    "    return lanes_and_len(a +% 1, rest(arr[k..n]))\n"
    "}\n"
    "\n"
    "fn eight_floats(a: f64, b: f64, c: f64, d: f64, e: f64, f: f64, g: f64, h: f64, v: u8x16) -> int {\n"
    "    return int(a + b * 2.0 + c * 3.0 + d * 4.0 + e * 5.0 + f * 6.0 + g * 7.0 + h * 8.0) + int(v[5]) * 1000\n"
    "}\n"
    "\n"
    "fn call_eight(x: u8, y: u8, k: int) -> int {\n"
    "    v: u8x16 = x\n"
    "    return eight_floats(f64(k), 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, f64(y), v +% 1)\n"
    "}\n"
    "\n"
    "fn ignore(v: u8x16, w: u8x16, x: int) -> int {\n"
    "    return x * 3\n"
    "}\n"
    "\n"
    "fn call_ignore(x: u8, y: u8, k: int) -> int {\n"
    "    a: u8x16 = x\n"
    "    return ignore(a, a +% y, k)\n"
    "}\n"
    "\n"
    "fn fill(x: u8) -> u8x16 {\n"
    "    v: u8x16 = x\n"
    "    return v\n"
    "}\n"
    "\n"
    "fn call_nested(x: u8, y: u8, k: int) -> int {\n"
    "    a: u8x16 = x\n"
    "    r := vadd(a +% y, fill(3))\n"
    "    return r[k]\n"
    "}\n"
    "\n"
    "fn weigh(v: u8x16, a: int, b: int, c: int) -> int {\n"
    "    return int(v[0]) + a * 100 + b * 10000 + c * 1000000\n"
    "}\n"
    "\n"
    "fn call_weigh(x: u8, y: int, k: int) -> int {\n"
    "    v: u8x16 = x\n"
    "    return weigh(v, y, k, y + k)\n"
    "}\n";

const VectorRun VECTOR_BODY_RESULTS[] = {
    { "lanes", { 65, 1, 3 }, 31, 0 },
    { "lanes", { 9, 3, 0 }, 32, 0 },
    { "lanes", { 255, 0, 3 }, 0, TRAP_OVERFLOW },
    { "lanes", { 10, 8, 0 }, 0, TRAP_SHIFT },
    { "lanes", { 10, 2, 16 }, 0, TRAP_BOUNDS },
    { "mixed", { 10, -4, 0 }, 41, 0 },
    { "mixed", { 10, -4, 1 }, 29, 0 },
    { "mixed", { 2, 5, 1 }, 16, 0 },
    { "mixed", { -1, -4, 0 }, -3, 0 },
    { "mixed", { 5, -4, 2 }, 0, TRAP_BOUNDS },
};

const size_t NVECTOR_BODY_RESULTS = sizeof VECTOR_BODY_RESULTS / sizeof VECTOR_BODY_RESULTS[0];

const VectorRun VECTOR_RESULTS[] = {
    { "ident_mask", { 40 }, 65280, 0 },
    { "ident_mask", { 90 }, 65441, 0 },
    { "ws_mask", { 0 }, 9728, 0 },
    { "ws_mask", { 1 }, 4864, 0 },
    { "ws_mask", { 25 }, 128, 0 },
    { "wide", { -2, 0 }, -8, 0 },
    { "wide", { -2, 1 }, -8, 0 },
    { "wide", { 7, 0 }, 29, 0 },
    { "wide", { 70000, 1 }, 210008, 0 },
    { "wide", { 1, 2 }, 0, TRAP_BOUNDS },
    { "shifts", { -100, 2, 3 }, 103975, 0 },
    { "shifts", { 100, 7, 0 }, 111000, 0 },
    { "shifts", { 5, 8, 0 }, 0, TRAP_SHIFT },
    { "pick", { 1, 0, 3 }, 65535, 0 },
    { "pick", { 0, 0, 2 }, 7, 0 },
    { "pick", { 1, 5, 8 }, 0, TRAP_BOUNDS },
    { "choose", { 0, 5, 3 }, 6, 0 },
    { "choose", { 1, 5, 0 }, 5, 0 },
    { "choose", { 2, 5, 15 }, 9, 0 },
    { "choose", { 0, 255, 1 }, 0, 0 },
    { "choose", { 1, 5, -1 }, 0, TRAP_BOUNDS },
    { "table", { 0 }, 0, 0 },
    { "table", { 2 }, -297, 0 },
    { "table", { 7 }, 4000032767, 0 },
    { "table", { 8 }, 0, TRAP_BOUNDS },
    { "wide_constants", { 5, 0 }, -4886716931, 0 },
    { "wide_constants", { -1, 1 }, -591750665, 0 },
    { "wide_constants", { 1, 2 }, 0, TRAP_BOUNDS },
    { "shift_edges", { -7, 3, 1 }, -7056, 0 },
    { "shift_edges", { 5, 31, 3 }, -2147478648, 0 },
    { "shift_edges", { -7, 32, 0 }, 0, TRAP_SHIFT },
    { "shift_edges", { -7, 0x100000001, 0 }, 0, TRAP_SHIFT },
    { "shift_edges", { -7, -1, 0 }, 0, TRAP_SHIFT },
    { "load_lanes", { 0, 0 }, 5, 0 },
    { "load_lanes", { 1, 15 }, 213, 0 },
    { "load_lanes", { 7, 3 }, 135, 0 },
    { "load_lanes", { 32, 15 }, 104, 0 },
    { "load_lanes", { 3, 16 }, 0, TRAP_BOUNDS },
    { "load_numbers", { 0, 0 }, -7820965588129094200, 0 },
    { "load_numbers", { 3, 0 }, -1527370551216489441, 0 },
    { "load_numbers", { 5, 1 }, 2962454105, 0 },
    { "load_numbers", { 7, 2 }, -20333, 0 },
    { "load_numbers", { 2, 2 }, 7938, 0 },
    { "load_numbers", { 1, 3 }, -27, 0 },
    { "load_numbers", { 9, 3 }, -51, 0 },
    { "load_numbers", { 11, 4 }, 9223, 0 },
    { "load_words", { 0 }, 4, 0 },
    { "load_words", { 15 }, 19, 0 },
    { "load_words", { 16 }, 0, TRAP_BOUNDS },
    { "store_lanes", { 1, 0 }, 0, 0 },
    { "store_lanes", { 1, 1 }, 228, 0 },
    { "store_lanes", { 1, 16 }, 243, 0 },
    { "store_lanes", { 1, 17 }, 0, 0 },
    { "store_lanes", { 32, 47 }, 243, 0 },
    { "store_lanes", { 5, 48 }, 0, TRAP_BOUNDS },
    { "store_numbers", { 1, 0 }, 0, 0 },
    { "store_numbers", { 1, 1 }, 17, 0 },
    { "store_numbers", { 1, 4 }, 68, 0 },
    { "store_numbers", { 1, 5 }, 254, 0 },
    { "store_numbers", { 1, 6 }, 255, 0 },
    { "store_numbers", { 1, 7 }, 7, 0 },
    { "store_numbers", { 1, 8 }, 8, 0 },
    { "store_numbers", { 1, 15 }, 1, 0 },
    { "store_numbers", { 1, 16 }, 0, 0 },
    { "store_numbers", { 9, 23 }, 1, 0 },
    { "store_words", { 0 }, 0, 0 },
    { "store_words", { 1 }, 67305985, 0 },
    { "store_words", { 4 }, 269422093, 0 },
    { "store_words", { 5 }, 0, 0 },
    { "store_words", { 6 }, 0, TRAP_BOUNDS },
    { "floats", { 0 }, 591, 0 },
    { "floats", { 3 }, 591, 0 },
    { "lookups", { 0, 0 }, 200, 0 },
    { "lookups", { 0, 15 }, 90, 0 },
    { "lookups", { 3, 4 }, 255, 0 },
    { "lookups", { 10, 5 }, 90, 0 },
    { "lookups", { 10, 6 }, 0, 0 },
    { "lookups", { 250, 5 }, 0, 0 },
    { "lookups", { 250, 6 }, 200, 0 },
    { "lookups", { 250, 15 }, 127, 0 },
    { "lookups", { 0, 16 }, 0, TRAP_BOUNDS },
    { "signed_lookups", { 0, 0 }, -1, 0 },
    { "signed_lookups", { 0, 15 }, 127, 0 },
    { "signed_lookups", { -1, 0 }, 0, 0 },
    { "signed_lookups", { -1, 1 }, -1, 0 },
    { "signed_lookups", { -128, 3 }, 0, 0 },
    { "signed_lookups", { 5, 10 }, 127, 0 },
    { "signed_lookups", { 5, 11 }, 0, 0 },
    { "top_bits_of", { 42435, 0 }, 42435, 0 },
    { "top_bits_of", { 65535, 0 }, 65535, 0 },
    { "top_bits_of", { 0, 0 }, 0, 0 },
    { "top_bits_of", { 42435, 1 }, 201, 0 },
    { "top_bits_of", { 32770, 1 }, 129, 0 },
    { "top_bits_of", { 42435, 2 }, 10, 0 },
    { "top_bits_of", { 2176, 2 }, 6, 0 },
    { "top_bits_of", { 42435, 3 }, 3, 0 },
    { "top_bits_of", { 32768, 3 }, 2, 0 },
    { "top_bits_of", { 128, 3 }, 1, 0 },
    { "first_space", { 5, 63 }, 5, 0 },
    { "first_space", { 40, 17 }, 17, 0 },
    { "first_space", { 63, 63 }, 63, 0 },
    { "first_space", { 16, 31 }, 16, 0 },
    { "first_space", { 64, 0 }, 0, TRAP_BOUNDS },
    { "mask_zeros", { 0, 0 }, 1616, 0 },
    { "mask_zeros", { 16, 0 }, 1104, 0 },
    { "mask_zeros", { 0x8000, 0 }, 15, 0 },
    { "mask_zeros", { 0, 1 }, 808, 0 },
    { "mask_zeros", { 0x80, 1 }, 601, 0 },
    { "mask_zeros", { 0x8000, 1 }, 403, 0 },
    { "call_add", { 10, 3, 5 }, 18, 0 },
    { "call_add", { 250, 9, 15 }, 18, 0 },
    { "call_spread", { 3, 4, 5 }, 1222, 0 },
    { "call_spread", { 200, 100, 15 }, 1111, 0 },
    { "call_mix", { 3, 4, 5 }, -299279552, 0 },
    { "call_mix", { 250, 9, 1 }, -299683988, 0 },
    { "call_leaf", { 3, 4, 5 }, 15, 0 },
    { "call_leaf", { 255, 255, 100 }, 613, 0 },
    { "call_after", { 5, 3 }, 6, 0 },
    { "call_after", { 255, 0 }, 0, 0 },
    { "compose", { 3, 4, 5 }, 34, 0 },
    { "compose", { 200, 100, 15 }, 111, 0 },
    { "call_rsum", { 3, 4, 5 }, 50, 0 },
    { "call_rsum", { 200, 30, 15 }, 218, 0 },
    { "call_pair", { 3, 4, 5 }, 5080, 0 },
    { "call_pair", { 200, 100, -2 }, -1686, 0 },
    { "dropped", { 5, 3 }, 10, 0 },
    { "dropped", { 200, 0 }, 144, 0 },
    { "call_slice", { 3, 8, 2 }, 4056, 0 },
    { "call_slice", { 250, 6, 0 }, 251301, 0 },
    { "call_slice", { 1, 3, 3 }, 0, TRAP_BOUNDS },
    { "call_eight", { 3, 4, 5 }, 4149, 0 },
    { "call_eight", { 255, 10, -200 }, -8, 0 },
    { "call_ignore", { 3, 4, 5 }, 15, 0 },
    { "call_ignore", { 1, 2, -7 }, -21, 0 },
    { "call_nested", { 10, 20, 5 }, 33, 0 },
    { "call_nested", { 250, 9, 15 }, 6, 0 },
    { "call_weigh", { 3, 4, 5 }, 9050403, 0 },
    { "call_weigh", { 250, -2, 7 }, 5070050, 0 },
};

const size_t NVECTOR_RESULTS = sizeof VECTOR_RESULTS / sizeof VECTOR_RESULTS[0];

const char VECTOR_GRID[] =
    "fn g_u8(a: int, b: int, op: int, k: int) -> u8 {\n"
    "    e := u8.[0, 1, 2, 3, 7, 8, 15, 16, 31, 63, 64, 100, 127, 128, 200, 255]\n"
    "    x: u8x16\n"
    "    y: u8x16\n"
    "    for i in 0..16 {\n"
    "        x[i] = e[(a + i) & 15]\n"
    "        y[i] = e[(b + 3 * i) & 15]\n"
    "    }\n"
    "    s := u32((a * 7 + b) & 7)\n"
    "    w := e[b & 15]\n"
    "    r := switch op {\n"
    "        0 => x +% y\n"
    "        1 => x -% y\n"
    "        2 => x *% y\n"
    "        3 => x & y\n"
    "        4 => x | y\n"
    "        5 => x ^ y\n"
    "        6 => ~x\n"
    "        7 => -%x\n"
    "        8 => x == y\n"
    "        9 => x != y\n"
    "        10 => x < y\n"
    "        11 => x <= y\n"
    "        12 => x > y\n"
    "        13 => x >= y\n"
    "        14 => x << s\n"
    "        15 => x >> s\n"
    "        16 => x >> 3\n"
    "        17 => x +% 5\n"
    "        18 => x | w\n"
    "        _ => w -% x\n"
    "    }\n"
    "    return r[k]\n"
    "}\n"
    "fn g_i8(a: int, b: int, op: int, k: int) -> i8 {\n"
    "    e := i8.[-128, -127, -100, -64, -16, -2, -1, 0, 1, 2, 7, 15, 16, 64, 100, 127]\n"
    "    x: i8x16\n"
    "    y: i8x16\n"
    "    for i in 0..16 {\n"
    "        x[i] = e[(a + i) & 15]\n"
    "        y[i] = e[(b + 3 * i) & 15]\n"
    "    }\n"
    "    s := u32((a * 7 + b) & 7)\n"
    "    w := e[b & 15]\n"
    "    r := switch op {\n"
    "        0 => x +% y\n"
    "        1 => x -% y\n"
    "        2 => x *% y\n"
    "        3 => x & y\n"
    "        4 => x | y\n"
    "        5 => x ^ y\n"
    "        6 => ~x\n"
    "        7 => -%x\n"
    "        8 => x == y\n"
    "        9 => x != y\n"
    "        10 => x < y\n"
    "        11 => x <= y\n"
    "        12 => x > y\n"
    "        13 => x >= y\n"
    "        14 => x << s\n"
    "        15 => x >> s\n"
    "        16 => x >> 3\n"
    "        17 => x +% 5\n"
    "        18 => x | w\n"
    "        _ => w -% x\n"
    "    }\n"
    "    return r[k]\n"
    "}\n"
    "fn g_u16(a: int, b: int, op: int, k: int) -> u16 {\n"
    "    e := u16.[\n"
    "        0, 1, 2, 3, 255, 256, 1000, 4095, 12345, 32767, 32768, 32769, 40000, 65000, 65534,\n"
    "        65535,\n"
    "    ]\n"
    "    x: u16x8\n"
    "    y: u16x8\n"
    "    for i in 0..8 {\n"
    "        x[i] = e[(a + i) & 15]\n"
    "        y[i] = e[(b + 3 * i) & 15]\n"
    "    }\n"
    "    s := u32((a * 7 + b) & 15)\n"
    "    w := e[b & 15]\n"
    "    r := switch op {\n"
    "        0 => x +% y\n"
    "        1 => x -% y\n"
    "        2 => x *% y\n"
    "        3 => x & y\n"
    "        4 => x | y\n"
    "        5 => x ^ y\n"
    "        6 => ~x\n"
    "        7 => -%x\n"
    "        8 => x == y\n"
    "        9 => x != y\n"
    "        10 => x < y\n"
    "        11 => x <= y\n"
    "        12 => x > y\n"
    "        13 => x >= y\n"
    "        14 => x << s\n"
    "        15 => x >> s\n"
    "        16 => x >> 3\n"
    "        17 => x +% 5\n"
    "        18 => x | w\n"
    "        _ => w -% x\n"
    "    }\n"
    "    return r[k]\n"
    "}\n"
    "fn g_i16(a: int, b: int, op: int, k: int) -> i16 {\n"
    "    e := i16.[\n"
    "        -32768, -32767, -1000, -256, -255, -2, -1, 0, 1, 2, 255, 256, 1000, 30000, 32766,\n"
    "        32767,\n"
    "    ]\n"
    "    x: i16x8\n"
    "    y: i16x8\n"
    "    for i in 0..8 {\n"
    "        x[i] = e[(a + i) & 15]\n"
    "        y[i] = e[(b + 3 * i) & 15]\n"
    "    }\n"
    "    s := u32((a * 7 + b) & 15)\n"
    "    w := e[b & 15]\n"
    "    r := switch op {\n"
    "        0 => x +% y\n"
    "        1 => x -% y\n"
    "        2 => x *% y\n"
    "        3 => x & y\n"
    "        4 => x | y\n"
    "        5 => x ^ y\n"
    "        6 => ~x\n"
    "        7 => -%x\n"
    "        8 => x == y\n"
    "        9 => x != y\n"
    "        10 => x < y\n"
    "        11 => x <= y\n"
    "        12 => x > y\n"
    "        13 => x >= y\n"
    "        14 => x << s\n"
    "        15 => x >> s\n"
    "        16 => x >> 3\n"
    "        17 => x +% 5\n"
    "        18 => x | w\n"
    "        _ => w -% x\n"
    "    }\n"
    "    return r[k]\n"
    "}\n"
    "fn g_u32(a: int, b: int, op: int, k: int) -> u32 {\n"
    "    e := u32.[\n"
    "        0, 1, 2, 3, 7, 65535, 65536, 1000000, 0x1000000, 0x75BCD15, 0x7FFFFFFF, 0x80000000,\n"
    "        0x80000001, 0xEE6B2800, 0xFFFFFFFE, 0xFFFFFFFF,\n"
    "    ]\n"
    "    x: u32x4\n"
    "    y: u32x4\n"
    "    for i in 0..4 {\n"
    "        x[i] = e[(a + i) & 15]\n"
    "        y[i] = e[(b + 3 * i) & 15]\n"
    "    }\n"
    "    s := u32((a * 7 + b) & 31)\n"
    "    w := e[b & 15]\n"
    "    r := switch op {\n"
    "        0 => x +% y\n"
    "        1 => x -% y\n"
    "        2 => x *% y\n"
    "        3 => x & y\n"
    "        4 => x | y\n"
    "        5 => x ^ y\n"
    "        6 => ~x\n"
    "        7 => -%x\n"
    "        8 => x == y\n"
    "        9 => x != y\n"
    "        10 => x < y\n"
    "        11 => x <= y\n"
    "        12 => x > y\n"
    "        13 => x >= y\n"
    "        14 => x << s\n"
    "        15 => x >> s\n"
    "        16 => x >> 3\n"
    "        17 => x +% 5\n"
    "        18 => x | w\n"
    "        _ => w -% x\n"
    "    }\n"
    "    return r[k]\n"
    "}\n"
    "fn g_i32(a: int, b: int, op: int, k: int) -> i32 {\n"
    "    e := i32.[\n"
    "        -0x80000000, -0x7FFFFFFF, -1000000, -65536, -7, -2, -1, 0, 1, 2, 7, 65535, 65536,\n"
    "        1000000, 0x7FFFFFFE, 0x7FFFFFFF,\n"
    "    ]\n"
    "    x: i32x4\n"
    "    y: i32x4\n"
    "    for i in 0..4 {\n"
    "        x[i] = e[(a + i) & 15]\n"
    "        y[i] = e[(b + 3 * i) & 15]\n"
    "    }\n"
    "    s := u32((a * 7 + b) & 31)\n"
    "    w := e[b & 15]\n"
    "    r := switch op {\n"
    "        0 => x +% y\n"
    "        1 => x -% y\n"
    "        2 => x *% y\n"
    "        3 => x & y\n"
    "        4 => x | y\n"
    "        5 => x ^ y\n"
    "        6 => ~x\n"
    "        7 => -%x\n"
    "        8 => x == y\n"
    "        9 => x != y\n"
    "        10 => x < y\n"
    "        11 => x <= y\n"
    "        12 => x > y\n"
    "        13 => x >= y\n"
    "        14 => x << s\n"
    "        15 => x >> s\n"
    "        16 => x >> 3\n"
    "        17 => x +% 5\n"
    "        18 => x | w\n"
    "        _ => w -% x\n"
    "    }\n"
    "    return r[k]\n"
    "}\n"
    "fn g_u64(a: int, b: int, op: int, k: int) -> u64 {\n"
    "    e := u64.[\n"
    "        0, 1, 2, 3, 7, 255, 65536, 0xFFFFFFFF, 0x100000000, 0x3B9ACA07, 0x123456789ABCDEF0,\n"
    "        0x7FFFFFFFFFFFFFFF, 0x8000000000000000, 0x8000000000000001, 0xFFFFFFFFFFFFFFFE,\n"
    "        0xFFFFFFFFFFFFFFFF,\n"
    "    ]\n"
    "    x: u64x2\n"
    "    y: u64x2\n"
    "    for i in 0..2 {\n"
    "        x[i] = e[(a + i) & 15]\n"
    "        y[i] = e[(b + 3 * i) & 15]\n"
    "    }\n"
    "    s := u32((a * 7 + b) & 63)\n"
    "    w := e[b & 15]\n"
    "    r := switch op {\n"
    "        0 => x +% y\n"
    "        1 => x -% y\n"
    "        2 => x *% y\n"
    "        3 => x & y\n"
    "        4 => x | y\n"
    "        5 => x ^ y\n"
    "        6 => ~x\n"
    "        7 => -%x\n"
    "        8 => x == y\n"
    "        9 => x != y\n"
    "        10 => x < y\n"
    "        11 => x <= y\n"
    "        12 => x > y\n"
    "        13 => x >= y\n"
    "        14 => x << s\n"
    "        15 => x >> s\n"
    "        16 => x >> 3\n"
    "        17 => x +% 5\n"
    "        18 => x | w\n"
    "        _ => w -% x\n"
    "    }\n"
    "    return r[k]\n"
    "}\n"
    "fn g_i64(a: int, b: int, op: int, k: int) -> i64 {\n"
    "    e := i64.[\n"
    "        -0x8000000000000000, -0x7FFFFFFFFFFFFFFF, -0x100000000, -0x3B9ACA07, -7, -2, -1, 0,\n"
    "        1, 2, 7, 0xFFFFFFFF, 0x100000000, 0x3B9ACA07, 0x7FFFFFFFFFFFFFFE,\n"
    "        0x7FFFFFFFFFFFFFFF,\n"
    "    ]\n"
    "    x: i64x2\n"
    "    y: i64x2\n"
    "    for i in 0..2 {\n"
    "        x[i] = e[(a + i) & 15]\n"
    "        y[i] = e[(b + 3 * i) & 15]\n"
    "    }\n"
    "    s := u32((a * 7 + b) & 63)\n"
    "    w := e[b & 15]\n"
    "    r := switch op {\n"
    "        0 => x +% y\n"
    "        1 => x -% y\n"
    "        2 => x *% y\n"
    "        3 => x & y\n"
    "        4 => x | y\n"
    "        5 => x ^ y\n"
    "        6 => ~x\n"
    "        7 => -%x\n"
    "        8 => x == y\n"
    "        9 => x != y\n"
    "        10 => x < y\n"
    "        11 => x <= y\n"
    "        12 => x > y\n"
    "        13 => x >= y\n"
    "        14 => x << s\n"
    "        15 => x >> s\n"
    "        16 => x >> 3\n"
    "        17 => x +% 5\n"
    "        18 => x | w\n"
    "        _ => w -% x\n"
    "    }\n"
    "    return r[k]\n"
    "}\n";

static const uint8_t EDGE_U8[16] = {
    0, 1, 2, 3, 7, 8, 15, 16, 31, 63, 64, 100, 127, 128, 200, 255,
};
static const int8_t EDGE_I8[16] = {
    -128, -127, -100, -64, -16, -2, -1, 0, 1, 2, 7, 15, 16, 64, 100, 127,
};
static const uint16_t EDGE_U16[16] = {
    0, 1, 2, 3, 255, 256, 1000, 4095, 12345, 32767, 32768, 32769, 40000, 65000, 65534, 65535,
};
static const int16_t EDGE_I16[16] = {
    -32768, -32767, -1000, -256, -255, -2, -1, 0, 1, 2, 255, 256, 1000, 30000, 32766, 32767,
};
static const uint32_t EDGE_U32[16] = {
    0, 1, 2, 3, 7, 65535, 65536, 1000000, 0x1000000, 0x75BCD15, 0x7FFFFFFF, 0x80000000, 0x80000001,
    0xEE6B2800, 0xFFFFFFFE, 0xFFFFFFFF,
};
static const int32_t EDGE_I32[16] = {
    INT32_MIN, -0x7FFFFFFF, -1000000, -65536, -7, -2, -1, 0, 1, 2, 7, 65535, 65536, 1000000,
    0x7FFFFFFE, 0x7FFFFFFF,
};
static const uint64_t EDGE_U64[16] = {
    0, 1, 2, 3, 7, 255, 65536, 0xFFFFFFFF, 0x100000000, 0x3B9ACA07, 0x123456789ABCDEF0,
    0x7FFFFFFFFFFFFFFF, 0x8000000000000000, 0x8000000000000001, 0xFFFFFFFFFFFFFFFE,
    0xFFFFFFFFFFFFFFFF,
};
static const int64_t EDGE_I64[16] = {
    INT64_MIN, -0x7FFFFFFFFFFFFFFF, -0x100000000, -0x3B9ACA07, -7, -2, -1, 0, 1, 2, 7, 0xFFFFFFFF,
    0x100000000, 0x3B9ACA07, 0x7FFFFFFFFFFFFFFE, 0x7FFFFFFFFFFFFFFF,
};

#define VECTOR_ORACLE(name, S, U, N, edge)                                                          \
    static u64 name(int64_t a, int64_t b, int64_t op, int64_t k)                                    \
    {                                                                                               \
        typedef S SV __attribute__((vector_size(16)));                                              \
        typedef U UV __attribute__((vector_size(16)));                                              \
        SV x;                                                                                       \
        SV y;                                                                                       \
        for (int64_t i = 0; i < N; i++) {                                                           \
            x[i] = edge[(a + i) & 15];                                                              \
            y[i] = edge[(b + 3 * i) & 15];                                                          \
        }                                                                                           \
        u32 s = (u32)((a * 7 + b) & (int64_t)(sizeof(S) * 8 - 1));                                  \
        S w = edge[b & 15];                                                                         \
                                                                                                    \
        UV ux = (UV)x;                                                                              \
        UV uy = (UV)y;                                                                              \
        SV all[20] = {                                                                              \
            (SV)(ux + uy), (SV)(ux - uy), (SV)(ux * uy), x & y,         x | y,                      \
            x ^ y,         ~x,            (SV)(-ux),     (SV)(x == y),  (SV)(x != y),               \
            (SV)(x < y),   (SV)(x <= y),  (SV)(x > y),   (SV)(x >= y),  (SV)(ux << s),              \
            x >> s,        x >> 3,        (SV)(ux + 5),  x | w,         (SV)((U)w - ux),            \
        };                                                                                          \
        return (u64)all[op][k];                                                                     \
    }

VECTOR_ORACLE(oracle_u8, uint8_t, uint8_t, 16, EDGE_U8)
VECTOR_ORACLE(oracle_i8, int8_t, uint8_t, 16, EDGE_I8)
VECTOR_ORACLE(oracle_u16, uint16_t, uint16_t, 8, EDGE_U16)
VECTOR_ORACLE(oracle_i16, int16_t, uint16_t, 8, EDGE_I16)
VECTOR_ORACLE(oracle_u32, uint32_t, uint32_t, 4, EDGE_U32)
VECTOR_ORACLE(oracle_i32, int32_t, uint32_t, 4, EDGE_I32)
VECTOR_ORACLE(oracle_u64, uint64_t, uint64_t, 2, EDGE_U64)
VECTOR_ORACLE(oracle_i64, int64_t, uint64_t, 2, EDGE_I64)

const GridFunction VECTOR_GRID_FUNCTIONS[] = {
    { "g_u8", 16, oracle_u8 },
    { "g_i8", 16, oracle_i8 },
    { "g_u16", 8, oracle_u16 },
    { "g_i16", 8, oracle_i16 },
    { "g_u32", 4, oracle_u32 },
    { "g_i32", 4, oracle_i32 },
    { "g_u64", 2, oracle_u64 },
    { "g_i64", 2, oracle_i64 },
};

const size_t NVECTOR_GRID_FUNCTIONS = sizeof VECTOR_GRID_FUNCTIONS / sizeof VECTOR_GRID_FUNCTIONS[0];
