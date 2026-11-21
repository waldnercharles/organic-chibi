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
