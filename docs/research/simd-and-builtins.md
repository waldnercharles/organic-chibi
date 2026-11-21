# SIMD types and the builtins that self-hosting needs

Written 2026-11-21 by the research thread. Item 4 of the self-hosting order (the Compiler row of 2026-11-16) is SIMD types and the bit-scan and overflow builtins. Step 3i.1 built the bit counts on 2026-11-21 ([checker-and-ir.md](checker-and-ir.md) section 8), and steps 3i.2 and 3i.3 built the overflow checks and vectors the same day in the forms this sheet recommends, while the owner decides. This sheet covers the rest: the vector type the lexer needs, its masks and table lookups, and the overflow checks. [adopt-or-settle.md](adopt-or-settle.md) 2.14 asked for it: "A 'vector math and SIMD' sheet should also cover the SIMD intrinsics that self-hosting needs." Small game vectors such as `[3]f32` with element-wise arithmetic and swizzles are arrays, and stay with 2.14.

## 1. What the compiler's C uses

Measured in `src/` at c657314, before step 3i.1 added one call of each bit count to the checker.

| C construct | Uses | Files | chibi |
|---|---|---|---|
| `__builtin_ctzll`, `__builtin_ctz` | 31 | arm64.c 15, lexer.c 8, exact.c 5, simd.h 3 | `trailing_zeros`, step 3i.1 |
| `__builtin_popcount` | 10 | lexer.c 8, arm64.c 2 | `count_ones`, step 3i.1 |
| `__builtin_clzll` | 3 | exact.c 2, arm64.c 1 | `leading_zeros`, step 3i.1 |
| `__builtin_add_overflow`, `_sub_`, `_mul_` | 6 | exact.c 3, on `int64_t`; lexer.c 3, on `u64` | section 4 |
| `u8x16` through `src/simd.h` | 24 type uses, 39 helper calls | lexer.c | sections 2 and 3 |
| `u32x4` | 1 | lexer.c, `write_starts` | sections 2 and 3 |
| `__builtin_expect` (`LIKELY`, `UNLIKELY`) | 2 macros | common.h | dropped by the translator |
| `__builtin___clear_cache` | 1 | arm64.c | an `extern fn` for compiler-rt's and libgcc's `__clear_cache` |

`NOINLINE`, `always_inline` and `__builtin_expect` change only speed, so the translator can drop them until the attribute design (adopt-or-settle 3.8) is decided.

`exact.c` checks overflow to stay on its fast path for small integers: `if (!__builtin_mul_overflow(u, v, &r)) { return exact_int(e, r); }`. The lexer uses an addition's carry out of 64-bit bitsets, as in `first_on_line`:

```c
u64 sum;
bool over = __builtin_add_overflow(~starts, line_end, &sum);
over |= __builtin_add_overflow(sum, carry->line_break, &sum);
```

The lexer calls `simd_eq` 16 times, `simd_lookup16` 5, `simd_in_range` 3, `simd_load` 3, `simd_mask16` 3, `simd_first16` 3, `simd_mask64` 2, and `simd_any_bits`, `simd_load64` and `simd_next64` once each. Its vector constants are 16-byte tables written as literals, such as `ws_char` in `is_ws`. `simd.h` has three paths: NEON, SSE, and a generic one in GCC's vector extensions. The generic path needs only these:
- the types `u8x16` and `u32x4`, and literals of them;
- lane-wise `&`, `|`, `~`, wrapping `+` and `-`, and `>>` by a constant;
- `==` and unsigned `<=` that give a lane of all ones where they hold and all zeros elsewhere;
- a scalar on one side standing for every lane, as in `v | 0x20` and `v - lo`;
- reading and writing one lane, `v[i]`;
- unaligned 16-byte loads and stores.

The generic path builds `simd_lookup16` and `simd_mask16` from loops over the 16 lanes. The SSE path uses `pshufb` and `pmovmskb` for them. The NEON path uses `tbl`, and makes a mask with four bits per lane through `shrn`, because NEON has no `pmovmskb`; `simd_first16` divides the trailing zeros by four on NEON. The NEON path also loads 64 bytes deinterleaved with `vld4q_u8` and packs four masks with `vsriq_n_u8`.

So self-hosting needs a vector type with lane-wise operators, plus two operations that no operator gives: a table lookup and a lane mask. The NEON and SSE tricks beyond those can wait: the translated lexer can run the generic path's four plain loads first.

## 2. The vector type

| Language | Type | Comparisons give | Source |
|---|---|---|---|
| Zig | `@Vector(16, u8)` | `@Vector(16, bool)` | [Vectors](https://ziglang.org/documentation/0.15.2/#Vectors) |
| Odin | `#simd[16]u8`, "where N is a power of two"; `core:simd` names 48 aliases such as `u8x16 :: #simd[16]u8` | `lanes_eq` and the other `lanes_*` procedures give a mask of unsigned integer lanes | [overview](https://odin-lang.org/docs/overview/#simd-vector-types), [core:simd](https://pkg.odin-lang.org/core/simd) |
| Rust (nightly `core::simd`) | `Simd<u8, 16>`, with aliases such as `u8x16` | a separate `Mask` type | [vector.rs](https://doc.rust-lang.org/src/core/portable-simd/crates/core_simd/src/vector.rs.html) |
| C3 | `char[<16>]`: "declared similar to an array but uses `[<>]` rather than `[]`" | `.comp_eq(other)` gives a boolean vector | [vectors](https://c3-lang.org/language-common/vectors/) |
| GCC and Clang C | `typedef u8 u8x16 __attribute__((vector_size(16)))`, as `src/simd.h` does | the same width, with lanes of all ones or zeros | `src/simd.h` |

Options:
- **A. Predeclared names for 128-bit vectors:** `u8x16`, `i8x16`, `u16x8`, `i16x8`, `u32x4`, `i32x4`, `u64x2`, `i64x2`, `f32x4` and `f64x2`. These are the names `src/simd.h` already uses, and the aliases that Odin's `core:simd` and Rust's `core::simd` give their vector types. The parser needs no change, because they are names like `u8`. Wider names, such as `u8x32` for AVX2, come with a target that has them. The lexer's `is_ws` would read, with section 3's table lookup:
  ```
  ws := u8x16.{ 32, 0, 0, 0, 0, 0, 0, 0, 0, 9, 10, 0, 0, 13, 0, 0 }
  hit := lookup(ws, v & 15) == v
  ```
- **B. A built-in generic type,** `Simd(u8, 16)`, as Zig's `@Vector(16, u8)` and Rust's `Simd<u8, 16>`. Any lane count works, and code can be generic over it once generics exist. It needs `parse_type` to accept a call-shaped type, which sheet 6's `List(i32)` also needs, so it waits for generics.
- **C. A new type form,** C3's `[<16>]u8` or Odin's `#simd[16]u8`. It is general like B without waiting for generics, but it adds a third bracket form beside `[16]u8` and `[*]u8`, and `#` is not lexed today.
- **D. Arrays as vectors:** lane-wise operators on `[16]u8`, as Odin's array programming does for small arrays. The Matrices row of 2026-11-10 decided that `==` on arrays gives one `bool`, so lane masks would need functions, and the backend keeps arrays in memory rather than in registers.

**Recommendation: A now.** It covers the lexer with no new syntax, and the names read as the compiler's C already does. When generics come, B can define A's names as aliases, as Rust does, so code written with A doesn't change.

## 3. Lanes, masks and the two operations

These follow from A or B, and they are rules the research thread would build and leave open to review rather than questions:
- **A comparison gives the same vector type, with lanes of all ones or all zeros,** as GCC's vector extensions, Odin's `lanes_*` procedures, SSE and NEON do. The lexer ANDs a mask with a table value, `simd_eq(next, v) & BEFORE_SAME` in `pair_firsts`, which a separate mask type, as Zig's and Rust's are, would turn into a conversion.
- **A literal lists the lanes in braces,** as a struct's fields: `u8x16.{ 32, 0, ... }`. `u8x16.[...]` would be an array of vectors, as `int.[1, 2]` is an array of `int`.
- **A scalar on one side stands for every lane:** `v | 0x20`, `v -% lo`.
- **`+`, `-` and `*` on integer lanes follow rank 1, as scalars do:** they trap on overflow in checked builds, lane by lane, and `+%`, `-%` and `*%` wrap. `simd_in_range` depends on wrapping, so its translation uses `-%`. Checking every lane costs a compare and a reduction across lanes in checked builds.
- **`v[i]` reads or writes one lane,** with the same bounds rule as an array.
- **Loads and stores go through a `[*]u8`:** a builtin that reads 16 bytes at `p` into a `u8x16`, and one that writes them. A conversion between a vector and a `[16]u8` of the same size also works, through `bitcast`, which needs no new builtin.
- **A table lookup:** lane `i` gets `table[index[i]]`, and 0 where an index is 16 or more. That is NEON's `tbl`, and one saturating add before SSE's `pshufb`. The lexer's indexes are always under 16, so either rule works for it.
- **A lane mask:** one bit per lane, from each lane's top bit, in a `u16` for 16 lanes, as SSE's `pmovmskb`. NEON needs several instructions for it. The lexer's 64-byte bitsets need exactly one bit per byte, so the four-bits-per-lane trick can't be the definition; the backend can still use it where code only takes the first set lane.

The table lookup and the lane mask are builtins, so their names follow the spelling question on builtins that was asked on 2026-11-21.

## 4. The overflow checks

| Language | Form | Source |
|---|---|---|
| GCC and Clang C | `bool __builtin_add_overflow(a, b, &r)` | [Integer Overflow Builtins](https://gcc.gnu.org/onlinedocs/gcc-7.3.0/gcc/Integer-Overflow-Builtins.html) |
| C23 | `bool ckd_add(&r, a, b)` in `<stdckdint.h>` | the C23 standard |
| Zig | `@addWithOverflow(a, b)` gives `struct { @TypeOf(a, b), u1 }`, a tuple; it took a pointer before 0.11 | [ziglings 064, 0.11](https://git.koehr.ing/n/ziglings/raw/tag/v0.11.0/exercises/064_builtins.zig) |
| Odin | `intrinsics.overflow_add(lhs, rhs: $T) -> (T, bool)` | [base:intrinsics](https://pkg.odin-lang.org/base/intrinsics) |
| Swift | `addingReportingOverflow(_:) -> (partialValue: Self, overflow: Bool)` | [Swift docs](https://developer.apple.com/documentation/swift/fixedwidthinteger/addingreportingoverflow(_:)) |
| Go | `bits.Add64(x, y, carry uint64) (sum, carryOut uint64)` | [math/bits](https://pkg.go.dev/math/bits#Add64) |

Every language that has multiple results returns two here. chibi's rank 12, multiple results and tuples, is still open, so the options are:
- **A. C's form:** `add_overflow(a, b, &r) -> bool`. The translator maps it one to one. It takes the address of `r`, so the development backend keeps `r` in memory, and chibi has no other out-parameter builtin.
- **B. A test beside the wrapping operators:** `add_overflows(a, b) -> bool`, `sub_overflows` and `mul_overflows`, with `a +% b` for the result. It needs nothing undecided, and the development backend keeps both in registers. Code names the operands twice, and the translator must copy an operand with side effects to a local first. Measured with clang `-O2` for arm64: clang shares the multiply between a multiply test and `*%`, but keeps an addition's test as a separate `cmn`, one instruction more than C's form. An unsigned carry needs no builtin: after `sum := x +% y`, clang turns `sum < x` into the single `adds`, so the lexer's three carries become `sum := x +% y` and `carry := sum < x`.
- **C. A two-field struct now:** `r := add_overflow(a, b)`, then `r.value` and `r.overflow`, which rank 12's tuples could destructure later. It needs a struct-valued builtin in the checker and both backends now.
- **D. Wait for rank 12** and return a tuple, as Zig, Odin and Swift do. Self-hosting would then wait for rank 12, which the 2026-11-16 order puts after it.

**Recommendation: B.** It needs only what is decided, it is cheap to build now, and rank 12 can add a two-result form beside it later. Its cost is the extra `cmn` on a signed addition or subtraction test in release builds, which chibi's own backends could remove by merging the test with the operation beside it. A gives the best code now, but it would be chibi's only builtin that writes through a pointer. C needs anonymous struct types first, and D waits for rank 12.

## 5. Questions for you

1. The vector type: A, predeclared names such as `u8x16` now and aliases of a generic type later (recommended); B, a generic `Simd(u8, 16)` that waits for generics; C, a new form such as `[<16>]u8`; or D, arrays?
2. The overflow checks: B, `add_overflows(a, b)` beside `a +% b` (recommended); A, C's pointer form; C, a two-field struct now; or D, wait for tuples?
3. Section 3's rules are the research thread's defaults. Tell me if any of them should be a question instead, the comparison result above all.
