# Syntax and semantics to adopt or settle now

Research note, 2026-11-10. Question: which syntax and semantics from other languages should chibi-v2 adopt or settle now, and in what order? The order is the cost of deciding late, once real games and the self-hosted compiler exist.

**Scope.** Other sheets cover unions; casts; `sizeof`, `alignof`, `offsetof` and `typeof`; `++` and `--`, the ternary operator, character literals, `goto`, varargs and bitfields; methods, generics, compile-time code and reflection; tasks; and the C import and `export fn` spellings. Number literals and modules are now settled ([decisions.md](../decisions.md)). This note mentions those topics only where a recommendation depends on them, and section 1.3 collects the dependencies in one table.

**Sources.** Primary sources were read on 2026-11-09 and 2026-11-10:
- the Zig 0.15.2 language reference and release notes;
- the Odin overview and FAQ;
- the C3 documentation and blog;
- the Carbon design documents at commit `19f1bf94`;
- the Zen C reference at commit `60115e02`;
- the Go specification, the Rust reference and book;
- the Swift, Kotlin, Hare and Mojo documentation.

Jai has no public documentation. Every Jai fact here comes from the community book *The Way to Jai* in `~/Personal/jai-refs` and is marked "(community source)". Facts that were not re-read in this session are marked "(unverified)" and listed in section 6. Old Chibi's own decisions are cited from `~/Personal/chibi/chibi/docs/language-friction.md` ("old Chibi friction"). They show the owner's earlier choices, but they don't bind chibi-v2.

**How the ranking works.** A choice is expensive to change late for three reasons, worst first:
1. **Silent.** Changing it alters what existing code does, with no compile error. No tool can find the affected lines. Wrapping overflow that later traps is the standard example.
2. **Structural.** The IR, the calling convention or the runtime has to change.
3. **Wide.** It touches most lines, even if a tool could rewrite them.

One asymmetry decides many recommendations below. A strict rule can usually be relaxed later without breaking code, but a loose rule can't be tightened without breaking the code that relied on it. The owner used this argument for old Chibi's overflow rule on 2026-10-20: "Trapping can be relaxed later without breaking code; wrapping could not be tightened later without breaking code that relied on it" (old Chibi friction, "Integer overflow wraps silently"). So where the strict choice costs little day to day, this note picks it now. Where it costs a lot day to day, as `const` pointers do, the note says so and leaves the call explicit.

## 1. Summary

### 1.1 The top ten

1. **Overflow (2.1).** Integer overflow traps in checked builds and wraps in fast builds, never UB, and `+%` wraps on purpose, because a later change is silent and every arithmetic operation in the IR depends on the rule.
2. **Integer and literal types (2.2).** `int` is 64-bit on every target and is the type of lengths, literals are untyped, and `x := 1.5` is an `f32`, because changing a width or a default later silently changes results and struct layouts.
3. **One contract for every backend (2.3).** Evaluation runs left to right, there is no type-based aliasing or FMA contraction, and struct `==` compares fields, because whatever chibi leaves to C becomes a silent difference between its backends.
4. **Errors (2.4).** A built-in `E!T` works with `try`, `catch` and `errdefer`, and out-of-memory aborts, because the choice fixes the return shape of every fallible function in the IR and the C ABI.
5. **Pointers (2.5).** `*T` is never nil, `?*T` may be, and `[*]T` points into C arrays, because tightening later needs flow analysis of every pointer, and wasm doesn't trap on nil.
6. **Initialization (2.6).** Memory starts zeroed unless declared `undefined`, declarations apply field defaults, and globals are built at compile time, because a later change is silent and hot reload needs the rule.
7. **Calling convention (2.7).** Every function uses the C ABI, and the context lives in a thread-local instead of a hidden parameter, because this is the ABI of every call and every C callback.
8. **Parameters (2.8).** Parameters are immutable copies that the compiler passes by reference only when no alias is possible, because Zig's hidden pass-by-reference produced silent aliasing bugs.
9. **Safety modes (2.9).** Bounds checks stay on in every build unless a block opts out, and no failed check becomes an optimizer assumption, because the release behavior of a bug is silent.
10. **Strings (2.10).** `string` is a read-only pointer and length, literals end in NUL, and pointers have no `const` qualifier, because every API signature and the C mapping depend on these.

Section 2 also covers ranks 11 to 15, which are mutability and shadowing, tuples, function values, vector math, and exhaustive `switch`. They cost less to change, mostly because the compiler finds the affected lines. The syntax in section 3 follows from these choices. Most of it is cheap to settle now and only moderately expensive later.

### 1.2 Terms

| Term | Meaning |
|---|---|
| Undefined behavior (UB) | C's name for an operation the standard gives no meaning, such as signed overflow. The optimizer may assume it never happens, so it may delete or reorder code around it. |
| Illegal behavior | Zig's name for the same idea. "Safety-checked" illegal behavior panics in safe builds; "unchecked" illegal behavior is UB. |
| Trap | Stop the program at the faulty operation with an error and, in debug builds, a stack trace. |
| Wrap, saturate | Two answers to overflow. Wrapping keeps the low bits, so a `u8` 255 + 1 is 0. Saturating clamps, so it stays 255. |
| IR | Intermediate representation: the compiler's internal form of the program after checking. chibi plans one typed, flat IR for the interpreter and every backend. |
| ABI, calling convention | The rules for which registers and stack slots carry arguments and results, and how structs are laid out. "The C ABI" is the platform's rules for C. |
| Sentinel | A special value that marks an end or an absence: the NUL byte at the end of a C string, or handle 0 meaning "none". |
| Slice | A pointer plus a length. |
| Optional | A type that holds a value or nothing (`?T`). |
| Error union | A type that holds a value or an error code (`E!T` in Zig). |
| Aliasing | Two names that reach the same memory, such as a pointer parameter and a global. |
| Type-based aliasing ("strict aliasing") | C's rule that pointers to different types never point at the same bytes, which lets the optimizer reorder their loads and stores. |
| FMA, contraction | A fused multiply-add computes `a*b + c` with one rounding instead of two. "Contraction" is a compiler fusing them on its own, which changes the last bit of results. |
| Zero value | The value of a type whose bytes are all zero. |
| Definite initialization | A compile-time analysis that proves each variable is written before it is read (Rust, Swift). |
| Shadowing | An inner declaration that reuses the name of an outer one, hiding it. |
| Flow-sensitive narrowing | The checker learning from `if p != nil { … }` that `p` is non-nil inside the branch (Kotlin, TypeScript). |
| Swizzle | Selecting or reordering vector components by name: `v.yx`, `color.rgb`. |
| SIMD | One instruction that works on several lanes at once, such as four `f32` additions. |
| Distinct type | A new type with the same representation as an old one that doesn't mix with it, such as a handle that is a `u32` underneath. |
| Closure, capture | A function value that remembers local variables of the function that created it. |
| Exhaustive switch | A `switch` that must handle every possible value, or say explicitly that it doesn't. |
| Untyped constant | A literal such as `1` or `1.5` that gets a concrete type only where it is used. |
| Thread-local storage (TLS) | A variable with one copy per thread. |
| Safe point | A moment where hot reload may swap code and migrate data, such as the frame boundary ([metaprogramming-reload-coroutines.md](metaprogramming-reload-coroutines.md), section 6). |

### 1.3 What this note assumes from the other sheets

| Recommendation here | Depends on | What the other sheet must decide |
|---|---|---|
| `??` and `.?` rather than postfix `?` (3.1) | ternary | Whether `c ? a : b` exists. If it does, a later `?.` token would make `c ? .red : .blue` ambiguous unless spaces decide, as in Swift. |
| `undefined` as a keyword, not `---` (3.3) | `++` and `--` | Whether `--` becomes a token |
| `+%` without `+%=` (3.4) | lexer hypothesis F7 | Whether three-byte operators stay ([front-end-optimizations.md](front-end-optimizations.md)) |
| No type-based aliasing; reinterpretation through a bit cast (2.3) | casts | The spelling of a bit cast, and implicit widening |
| Built-in `?T` and `E!T` (2.4, 2.5) | unions | Whether tagged unions exist, and whether optionals are a special case of them |
| Exhaustive `switch` (2.15) | unions | The same rule for switches over tagged unions |
| Tuples and error unions usable as type arguments (2.4, 2.12) | generics, tasks | A task's result type |
| Operator overloading later (2.14) | methods | How methods are declared |
| A thread-local context saved at task switches (2.7) | tasks | Stackless frames and the scheduler |
| Function values that point at reload stubs (2.13) | hot reload (H8, I4) | Stub layout |
| C pointer mapping, `c_long`, `_Nonnull` (2.5) | C import | The generator's rules and its override file |
| Arbitrary-width integers only as storage (2.2) | bitfields | Their declaration syntax |
| `for b in s` yields `u8` (2.10) | character literals | The type of `'a'` |
| `@` for attributes (3.8) | `sizeof` | Whether builtins also use `@`, as Zig's `@sizeOf` does |
| No shadowing of imported names (2.11) | modules (settled) | `import "std/fmt"` binds `fmt`, so a local named `fmt` is an error |

### 1.4 Where this note departs from Zen C

Zen C is the language closest to the owner's taste, so its choices deserve a direct comparison. The sources are the [Zen C reference](https://github.com/zenc-lang/docs/tree/60115e029d02e9cea4c9f721da5c82cd7b257de5/reference) and the [README](https://github.com/Zuhaitz-dev/zenc-lang/blob/main/README.md).

| Area | Zen C | This note | Why |
|---|---|---|---|
| `int` | 32 bits, `int32_t` | 64 bits | Lengths are `int`, and results must match on wasm and desktop |
| Strings | `string` is `char*`, a C string | pointer and length | Length and slicing in constant time; old Chibi's `strlen` costs (2.10) |
| Lambdas | capture by value by default, `[&]` by reference | no capture | Design A has no collector; captured references would dangle |
| Errors | `Result` with postfix `res?` | `E!T` with `try` | A storable type with a C layout; postfix `?` could collide with a `?:` ternary |
| Absent values | `??`, `??=` and `?.` on pointers that may be NULL | `?T`, with `??` now and `?.` later | The ternary dependency in 1.3 |
| Attributes | `@inline`, `@packed`, passed through to C | `@name` | Same choice |
| Tests | `test "name" { }` | `@test fn name()` | No new keyword; tests get stable names |
| Labels | `outer: loop`, `break outer` | the same form | Same choice |
| Operator overloading | methods named `add`, `eq` and so on | not now | Built-in array math covers vectors |
| Pipe `\|>`, `guard`, `unless` | yes | no | Second ways to write calls and `if` |
| Arbitrary-width integers | `iN`, `uN` through C23 | storage only | Each width adds backend cases |
| Read-only data | a `const` type qualifier | no qualifiers; `string` is read-only | Daily cost of `const` (2.10) |

## 2. Semantics to settle now

### 2.1 Integer overflow, division and shifts (rank 1)

**What it is.** An integer type holds a fixed range, such as 0 to 255 for a `u8`. When `a + b` falls outside the range, the language must pick a result. It can wrap, trap or saturate. It can also leave the result undefined, as C does for signed integers. Zig gives each choice its own operator:

```zig
const a: u8 = 255;
const w = a +% 1; // 0: wraps, never checked
const s = a +| 1; // 255: saturates
const t = a + 1;  // Illegal Behavior: panics in Debug and ReleaseSafe builds
```

Division and shifts face the same question at their edges. These are a zero divisor, the most negative value divided by -1, whose result doesn't fit, and a shift by at least the bit width.

**Who has it.**

| Language | `+` on overflow | Wrap on purpose | Division by zero | Shift by ≥ width |
|---|---|---|---|---|
| C | signed: UB; unsigned: wraps | — | UB | UB |
| Zig | Illegal Behavior: a panic in Debug and ReleaseSafe, unchecked in ReleaseFast and ReleaseSmall ([Integer Overflow](https://ziglang.org/documentation/0.15.2/#Integer-Overflow), [Build Mode](https://ziglang.org/documentation/0.15.2/#Build-Mode)) | `+% -% *%`; saturating `+\| -\| *\| <<\|` ([operators](https://ziglang.org/documentation/0.15.2/#Table-of-Operators)) | Illegal Behavior ([Division by Zero](https://ziglang.org/documentation/0.15.2/#Division-by-Zero)) | The amount's type must have log2 as many bits as the operand, so it can't be too large ([operators](https://ziglang.org/documentation/0.15.2/#Table-of-Operators)) |
| Rust | panics in debug builds; "two's complement wrapping" in release builds ([book](https://doc.rust-lang.org/book/ch03-02-data-types.html#integer-overflow)) | `wrapping_add` and related methods | panics | panics in debug builds ([reference](https://doc.rust-lang.org/reference/expressions/operator-expr.html#overflow)) |
| Swift | "by default Swift reports an error" ([Advanced Operators](https://github.com/swiftlang/swift-book/blob/16a2a4caa408c86fc85bde1e1dfff4745b6e454c/TSPL.docc/LanguageGuide/AdvancedOperators.md)) | `&+ &- &*` | (unverified) | (unverified) |
| Carbon | signed overflow is a programming error. A development build catches it, a performance build assumes it away, and a hardened build gives no UB. Unsigned "wraps around" ([arithmetic.md](https://github.com/carbon-language/carbon-lang/blob/19f1bf94dbb9b13f0595db32ce82e0a4ad19f782/docs/design/expressions/arithmetic.md#overflow-and-other-error-conditions)) | none | treated like signed overflow | a TODO |
| Go | wraps, "deterministically defined"; "A compiler may not optimize code under the assumption that overflow does not occur" ([spec](https://go.dev/ref/spec#Integer_overflow)) | everything wraps | run-time panic | "no upper limit on the shift count": the result is as if shifted one bit at a time ([spec](https://go.dev/ref/spec#Integer_operators)) |
| Odin | wraps; "Overflow **does not** cause a runtime panic" ([overview](https://odin-lang.org/docs/overview/#integer-overflow)) | everything wraps | (unverified) | `x << y` gives 0 when `y` is at least the width ([overview](https://odin-lang.org/docs/overview/#integer-operators)) |
| C3 | wraps; signed overflow is never UB ([guide for C programmers](https://c3-lang.org/c-to-c3/a-guide-for-c-programmers/), via search excerpt) | saturating operations ([features](https://c3-lang.org/faq/allfeatures/)) | (unverified) | (unverified) |
| Jai (community source) | "math bounds checks are not yet implemented" as of January 2026; casts are checked | — | run-time panic | (unverified) |
| Old Chibi | "overflow fails the call ... on every backend", and `T(wrap: expr)` wraps | `T(wrap: …)` | — | shifts reject a count "greater than or equal to the operand width" |

**Why it matters for chibi.**
- Hashes and random number generators need wrapping, while counters, sizes and indices are buggy when they wrap.
- The decision log says the C backend must avoid C's undefined behaviour, so it can't pass signed overflow through to clang.
- Lockstep games and replays need one result on x86-64, arm64 and wasm.
- Checks cost the most in hot integer loops. Regehr measured signed-overflow checking on SPEC CINT 2006 at 11.8% slower overall, from negligible to about 40% (HMMER). Code grew less than 1%, and "most of the slowdown comes from integer overflow checks interfering with loop optimizations"; HMMER's hot loop stopped vectorizing ([Regehr 2016](https://blog.regehr.org/archives/1384)). Float math, which dominates game math, has no overflow checks.

**Cost of deciding late.** This is the most expensive kind of late change, because it is silent. If chibi wraps now and traps later, every hash that relied on wrapping fails at run time, and no compile error points at it. If chibi traps now and wraps later, nothing breaks. In the IR, every add, subtract, multiply, negate, divide and shift needs a checked and an unchecked form. The compile-time interpreter (H9) and both backends must agree on both. The C translator ([c-corpus.md](c-corpus.md)) must map C's unsigned arithmetic, which wraps, to chibi's wrapping operators wherever the wrap is intended.

**Options.**

| Option | Precedent | A bug in a shipped build | What the C backend emits |
|---|---|---|---|
| A. Always wrap | Go, Odin, C3 | a wrong value, no stop | unsigned arithmetic or `-fwrapv` |
| B. Always trap | Swift, old Chibi | stops with an error | checked builtins such as `__builtin_add_overflow` |
| C. Trap in checked builds, wrap in fast builds | Rust | a wrong value, but the same on every machine | B in checked builds, A in fast builds |
| D. Trap in checked builds, undefined in fast builds | Zig ReleaseFast, Carbon's performance build | anything | plain C signed arithmetic, so clang may exploit it |

**Recommendation.** Option C, with B available by shipping the checked release build. Overflow is treated as a bug, under three rules.
- Debug and release builds trap.
- A release-fast build wraps. That is defined, so the C backend never emits signed overflow, and a buggy program gives the same wrong answer on every machine.
- `+%`, `-%` and `*%` wrap in every build and never trap, so code that means to wrap says so (section 3.4).

D is rejected, because it contradicts the C backend decision and lets the behavior of a bug change with the optimizer. Go's specification forbids that kind of optimization outright. B and C differ only in programs that already have a bug, so whether games ship checked builds can wait.

Division and shifts follow the same reasoning.
- **Division truncates toward zero, and `%` takes the sign of the dividend**, as in C, Go and [Carbon](https://github.com/carbon-language/carbon-lang/blob/19f1bf94dbb9b13f0595db32ce82e0a4ad19f782/docs/design/expressions/arithmetic.md#integer-types).
- **A zero divisor and the most negative value divided by -1 both trap in every build.** This is about portability as much as safety. arm64's divide instruction returns 0 for a zero divisor, while x86-64's faults on both cases (unverified, from the architecture manuals), so without the check one program behaves differently on each CPU. Rust also keeps the -1 check when overflow checks are off "for legacy reasons" ([reference](https://doc.rust-lang.org/reference/expressions/operator-expr.html#overflow)).
- **A shift count at least as large as the width traps in checked builds and is masked to the width in fast builds.** Masking is what arm64 and x86-64 do in hardware for 32- and 64-bit shifts, so it costs nothing there (unverified). 8- and 16-bit operands need an explicit mask. Old Chibi rejected such shifts in every mode, which is option B for shifts.
- Floored division and modulo (Odin's `%%`, Zig's `@mod`) can be library functions later.

**Own sheet?** Yes. One "numeric semantics" sheet should cover sections 2.1, 2.2, 2.3 and 2.9, since they share the IR operations, the interpreter and the C backend flags. Measure there what checked release builds cost on the C corpus and on the idiomatic seed, in cycles per element of the hottest integer loops.

### 2.2 Integer types, lengths and literals (rank 2)

**What it is.** Three linked questions. How wide is `int`? Are lengths and indices signed or unsigned? And what type does a literal such as `1` or `1.5` have before it meets a variable? Go treats literals as untyped constants with exact values, typed only where used:

```go
const big = 1 << 100    // fine: constants are exact
var f float32 = big >> 98 // 4, converted where it is used
x := 1.5                // float64, the default type of a float constant
```

**Who has it.**

| Language | `int` | Lengths and indices | Literals | `x := 1.5` |
|---|---|---|---|---|
| Go | 32 or 64 bits ([spec](https://go.dev/ref/spec#Numeric_types)) | `int`, signed | untyped; "Represent integer constants with at least 256 bits" ([spec](https://go.dev/ref/spec#Constants)) | `float64` |
| Odin | the "natural" register size, at least a pointer ([overview](https://odin-lang.org/docs/overview/#basic-types)). The `wasm64p32` target exists to give wasm a 64-bit `int` with 32-bit pointers ([FAQ](https://odin-lang.org/docs/faq/#what-architectures-does-odin-support)) | `int`, signed | untyped | (unverified; probably `f64`) |
| Zig | no `int`; `isize`, `usize` | `usize`, unsigned | `comptime_int` with no size limit; `comptime_float` at `f128` precision ([Integer Literals](https://ziglang.org/documentation/0.15.2/#Integer-Literals), [Float Literals](https://ziglang.org/documentation/0.15.2/#Float-Literals)) | a runtime variable needs a type |
| Jai (community source) | "int is an alias for s64" | `s64`, signed | integer literals default to `s64` | `float32`: "`float` defaults to `float32`" |
| C3 | 32 bits | changed from unsigned `usz` to signed `sz` in 0.8.0, May 2026 ([blog](https://c3-lang.org/blog/unsigned-sizes-a-five-year-mistake/)) | — | (unverified) |
| Zen C | 32 bits: "`int` is guaranteed to be 32-bit signed on all architectures" ([primitive types](https://github.com/zenc-lang/docs/blob/60115e029d02e9cea4c9f721da5c82cd7b257de5/reference/02-primitive-types.md)) | `usize` | not documented | not documented |
| Lobster | "64-bit on all platforms" ([reference](https://aardappel.github.io/lobster/language_reference.html)) | — | — | — |
| Rust | none | `usize`, unsigned | inferred (unverified) | `f64` (unverified) |

C3's post explains its reversal. Unsigned counters break countdown loops, and mixed comparisons go wrong. Also, "unsigned overflow often produces a quite plausible number, just the wrong one." C3 then dropped implicit signed-to-unsigned conversion too ([blog](https://c3-lang.org/blog/unsigned-sizes-a-five-year-mistake/)).

**Why it matters for chibi.**
- Most loops are `for i in 0..len(a)`, and `len(a) - 1` must not wrap to a huge number when `a` is empty.
- If `int` were pointer-sized, it would be 32 bits on wasm32. The same game could then overflow in its web build and not on the desktop, which is why Odin added `wasm64p32`.
- Untyped constants let `v.x * 0.5` work for an `f32` field without a suffix.
- The default float type decides whether `speed := 1.5` followed by `pos.x += speed` compiles when `pos.x` is an `f32`.

**Cost of deciding late.** The change would be both silent and wide. A different width of `int` moves overflow points and resizes every struct with an `int` field, and because structs use C layout, files and GPU buffers change too. A different default float type changes the precision of every inferred float local. A different signedness touches every loop and length computation.

**Decided 2026-11-11** as recommended below, with one change: there is no `uint`, because it would only be a second name for `u64`. See [../decisions.md](../decisions.md).

**Recommendation.**
- **Types.** `int` and `uint` are 64-bit on every target, as in Jai and Lobster. Sized types are `i8` to `i64`, `u8` to `u64`, `f32` and `f64`, and `bool` is one byte. A pointer-sized `uintptr` exists only for address arithmetic and C. C's `int`, `long` and `size_t` come in through the C import under target-dependent names, because `long` differs between Windows and Unix; the C import sheet owns that mapping.
- **Lengths, indices and counts are `int`,** as in Go, Odin, Jai and now C3.
- **No C promotion.** `u8 + u8` is a `u8`, and it overflows at 8 bits (2.1). Whether a `u8` widens implicitly to `int` is the casts sheet's decision.
- **Literals are untyped constants.** Their arithmetic is exact, as in Go and Odin, and they convert where they are used. A constant that doesn't fit its target is a compile error. The checker needs a small big-integer type for this. A fast path for values that fit in 64 bits keeps it cheap.
- **`x := 1` is an `int`, and `x := 1.5` is an `f32`, as in Jai.** This is a judgment call. Game code is mostly `f32`, and so are GPU and SIMD code. With an `f64` default (Go, Odin), every inferred float local needs an annotation or a conversion at the first `f32` it meets. Code that needs `f64`, such as a time accumulator, says so.
- **Arbitrary widths such as Zig's `u3` are not arithmetic types for now.** They belong to the bitfields sheet as storage.

**Own sheet?** Part of the numeric semantics sheet.

### 2.3 One defined-behavior contract for every backend (rank 3)

**What it is.** A set of rules that give each program one meaning, so the interpreter, the development backend and the C backend compute the same result. C leaves four things open that matter here:
- **Evaluation order.** In `f(g(), h())`, C may call `h` first.
- **Type-based aliasing.** C lets the optimizer assume that a `float*` and an `int*` never point at the same bytes. Reading a float's bits through an integer pointer is UB.
- **Floating-point contraction.** clang fuses `a*b + c` within one statement by default (`-ffp-contract=on`) ([clang](https://clang.llvm.org/docs/UsersManual.html#cmdoption-ffp-contract)), so the last bit of a result depends on the compiler.
- **Struct equality.** C has none. chibi's parser already accepts `p == Point.{}`, and C layout puts padding bytes between fields.

**Who has it.**

| Rule | Go | Zig | Rust | Others |
|---|---|---|---|---|
| Evaluation order | calls run left to right, but `x := []int{a, f()}` "may be `[1, 2]` or `[2, 2]`" ([spec](https://go.dev/ref/spec#Order_of_evaluation)) | left to right in practice, but the language reference doesn't say so; a compiler developer wrote in 2024 that side effects happen in source order and that he expects the spec to adopt it ([Ziggit](https://ziggit.dev/t/does-zig-guarantee-left-to-right-order-of-evaluation/2684)). A literal is written into its destination field by field, so `arr = .{ arr[1], arr[0] }` doesn't swap; the reference's own test for it says "So this fails!" ([Result Locations](https://ziglang.org/documentation/master/#Result-Locations)) | left to right (unverified) | Java: left to right (JLS §15.7, unverified). Odin: not stated in the [overview](https://odin-lang.org/docs/overview/) or [FAQ](https://odin-lang.org/docs/faq). Jai: no public spec |
| FMA contraction | allowed: "An implementation may combine multiple floating-point operations into a single fused operation, possibly across statements"; an explicit conversion prevents it ([spec](https://go.dev/ref/spec#Floating-point_operators)) | `Strict` by default. `@setFloatMode(.optimized)` per block is "equivalent to `-ffast-math`" ([setFloatMode](https://ziglang.org/documentation/0.15.2/#setFloatMode)) | not allowed: strict IEEE semantics preclude "turning `a*b + c` into FMA operations" ([RFC 3514](https://rust-lang.github.io/rfcs/3514-float-semantics.html)) | clang's C default fuses within a statement |
| Struct `==` | field by field, in source order, stopping at the first difference ([spec](https://go.dev/ref/spec#Comparison_operators)) | (unverified: not allowed on structs) | through a derived trait | Odin: comparable when every field is; structs marked `#simple` compare "as if with `memcmp`" ([overview](https://odin-lang.org/docs/overview/#comparison-operators)) |

**Why it matters for chibi.**
- The C backend is the first release backend. Whatever chibi leaves open becomes whatever clang does, which can differ from the development backend.
- The differential test that every engine must pass ([metaprogramming-reload-coroutines.md](metaprogramming-reload-coroutines.md), section 6) would then fail on correct programs.
- Lockstep multiplayer and replays need bit-identical float results across machines. Old Chibi's engineering rules already forbid FMA contraction and fast-math in gameplay math.

**Cost of deciding late.** The change would be silent, because a later change to evaluation order, aliasing or contraction alters results without an error, sometimes by one bit.

**Decided in part 2026-11-19** (option A): an array or struct in a literal, and a struct argument, is read where it is written, left to right, as a slice already was; checker step 3d.7 builds it. The rest of the recommendation below is still open. See [../decisions.md](../decisions.md).

**Recommendation.**
- **Order.** Every operand and argument is evaluated left to right. An assignment evaluates its target's subexpressions before the value. The C backend writes calls with side effects into temporaries in that order, which costs nothing after optimization (my analysis).
- **Aliasing.** chibi has no type-based aliasing rule, and reinterpreting bytes goes through an explicit bit cast (casts sheet). The C backend compiles with `-fno-strict-aliasing`, or emits `memcpy` for each reinterpretation.
- **Floats.** Floats are IEEE 754 binary32 and binary64, rounding to nearest even. Contraction and fast-math are off, and a fused multiply-add is an explicit builtin. The C backend passes `-ffp-contract=off` and never `-ffast-math`. NaN payload bits are not part of the contract, which Rust's RFC 3514 also leaves open. Compile-time evaluation follows the same rules, which the target-layout interpreter (H9) makes natural. One exception is accepted. Untyped float constants are exact (2.2), as in Go, so `const X := 0.1 + 0.2` can differ in its last bit from the same sum computed in `f32` at run time.
- **No fast-math mode, global or per function** (measured 2026-11-11 on an Apple M4 Pro with Apple clang 21, in a scratch program). `-ffast-math` turned a hand-written NaN guard, `x != x ? 0 : x`, into a bare `ret`, and it deleted Kahan summation's correction: 10⁷ × `0.1f` summed to 1,005,958.6 instead of 1,000,000.0. Apple's `math.h` warns that fast-math "allows the compiler to optimize away checks like x != x, which might lead to things like isnan(NaN) returning false". Its speed came from reordering a sum: 0.50 → 0.039 ns per element on 16 KB in L1. A sum over 32 accumulators in a fixed order ran at 0.022 ns per element with no flag, and it gives the same result on every machine. Zig's per-scope `@setFloatMode(.optimized)` still lets the optimizer "Assume the arguments and result are not NaN", with an undefined result if one appears. Box2D's author found fast-math gave "no significant performance benefit" on MSVC, and advises "never use this setting" ([Determinism](https://box2d.org/posts/2024/08/determinism/)). GCC adds that at link time it "may include libraries or startup files that change the default FPU control word" ([GCC](https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html)); in 2022, 49 PyPI packages shipped such libraries, setting flush-to-zero for every process that loaded them ([moyix](https://moyix.blogspot.com/2022/09/someones-been-messing-with-my-subnormals.html)). The standard library should give fixed-order sums and dot products instead.
- **Struct `==`.** It compares fields in order. Float fields compare as floats, so NaN is unequal to itself and -0 equals +0. Padding is never compared. The compiler may use `memcmp` for a type with no padding and no float fields, which is what Odin's `#simple` asks for.
- **`undefined`.** Reading memory declared `undefined` (2.6) before writing it is illegal behavior. This is the one place chibi deliberately hands the C backend an indeterminate value. Debug builds fill such memory with a pattern.
- **Later.** Platform math libraries (`sinf`, `expf`) differ between systems. A chibi math library with its own implementations can come later (section 4).

**Own sheet?** Part of the numeric semantics sheet, which should end in a one-page C backend checklist:
- `-fwrapv` or unsigned arithmetic;
- `-fno-strict-aliasing`;
- `-ffp-contract=off`;
- calls spilled to temporaries in order;
- explicit division and shift checks.

### 2.4 Errors (rank 4)

**What it is.** This is how a function reports that it failed. The main designs are these.
- exceptions, which unwind the stack to a handler;
- multiple return values with the error last (Go, Odin);
- an error union, a value-or-error-code type with operators that propagate it (Zig, C3);
- a generic `Result` type built on tagged unions (Rust, Hare, Zen C).

```zig
// Zig: `!Image` is an error union, and `try` returns the error to the caller.
fn load(path: []const u8) !Image {
    const bytes = try readFile(path);
    errdefer freeBytes(bytes); // runs only if this function returns an error
    return try decode(bytes);
}
```

```odin
// Odin: the last result is the error, and or_return returns it when it is set.
load :: proc(path: string) -> (img: Image, err: Error) {
    bytes := read_file(path) or_return
    return decode(bytes)
}
```

**Who has it.**

| Language | Declare | Propagate | Handle | Cleanup on error | Error value |
|---|---|---|---|---|---|
| Zig | `E!T`, or `!T` with an inferred set ([Inferred Error Sets](https://ziglang.org/documentation/0.15.2/#Inferred-Error-Sets)) | `try` | `catch`, `catch \|err\|` | `errdefer` ([errdefer](https://ziglang.org/documentation/0.15.2/#errdefer)) | each error name "across the entire compilation gets assigned an unsigned integer greater than 0"; the type "defaults to a u16" ([Error Set Type](https://ziglang.org/documentation/0.15.2/#Error-Set-Type)) |
| C3 | `T?`, an optional that holds a value or a fault ([optionals](https://c3-lang.org/language-common/optionals-essential/)) | postfix `!` | `??`, `if (catch err = f())`; `!!` forces | `defer catch`, `defer try` ([features](https://c3-lang.org/faq/allfeatures/)) | a fault, "guaranteed to be a pointer sized value" at the C boundary |
| Odin | multiple results, error last ([overview](https://odin-lang.org/docs/overview/#multiple-results)) | `or_return` | `or_else`, `or_break`, `or_continue` ([overview](https://odin-lang.org/docs/overview/#or_return-operator)) | `defer` | any type |
| Go | `(T, error)` | `if err != nil { return … }` | — | `defer`, scoped to the function | an interface |
| Rust | `Result<T, E>` | postfix `?` | `match`, `unwrap_or` | `Drop` | any type |
| Hare | a tagged union `(T \| error)` | postfix `?` ([tutorial](https://harelang.org/tutorials/introduction/#propagating-errors)) | `match`; postfix `!` asserts that no error happened | `defer` | any type |
| Zen C | `Result` with `Ok` and `Err` | postfix `res?` ([operators](https://github.com/zenc-lang/docs/blob/60115e029d02e9cea4c9f721da5c82cd7b257de5/reference/06-operators.md)) | not documented | `defer` | not documented |
| Jai (community source) | multiple results; `#must` forbids ignoring them | — | — | `defer` | any type |

Odin's FAQ puts its stance plainly: "Odin uses plain error handling through the use of multiple return values" ([FAQ](https://odin-lang.org/docs/faq/#why-does-odin-not-have-exceptions)). Zig's reference warns that functions with inferred error sets "become generic", and that "inferred error sets are incompatible with recursion".

**Why it matters for chibi.**
- **Tasks and generics need a result they can store.** A task that loads an image keeps "an image or an error" in its frame. A job system's generic `Future(T)` takes it as a type argument. Multiple return values are syntax, not a type, so they can't be stored or passed as a type argument. A first-class error type can.
- **Parallel checking and hot reload need signatures that don't depend on bodies.** With inferred error sets, a function's type depends on its body. Checking a caller then waits for the callee's body, and editing a body can change a signature, which turns a one-function reload into a re-check of every caller.
- **Stored errors must keep their meaning.** Zig numbers error names per compilation, so an error kept in retained state could change meaning after a reload. That is my inference from the numbering rule. Enum values declared in source keep their numbers.
- **C interop.** An `export fn` that can fail needs a C shape, and `struct { T value; E code; }` is one.
- **Out-of-memory decides how many functions can fail.** Zig's allocators return `error.OutOfMemory` (unverified), so nearly every function that allocates is fallible, and `try` is everywhere. Rust's standard allocation aborts on failure by default (unverified), and old Chibi's rule was "Out of memory aborts; that is deliberate." If chibi aborts on out-of-memory, far fewer functions can fail.

**Cost of deciding late.** The change would be structural and wide, because it fixes the return shape of every fallible function in the IR and in the C ABI. Every call site of those functions changes with it, and so do the `defer` variants. Most of the fallout would be compile errors, not silent changes, so a tool could guide the rewrite, but nearly every file would change.

**Options.**
- **A. Multiple results with an operator (Odin).** This needs one mechanism and no new type. But the value can be read even when the error is set, and the pair can't be stored as one value.
- **B. A built-in error union with the error type written in the signature (Zig, C3).** It is laid out as a C struct `{T value; E code}`, where code 0 means success, which matches Zig numbering its errors from 1. Because it is a type, it can be stored, used with generics and passed to C.
- **C. A library `Result(T, E)` over tagged unions (Rust, Hare, Zen C).** This depends on the unions and generics sheets. It gets no compact layout for free, and using it needs pattern matching.

**Partly superseded (2026-11-10).** The owner chose error sets inferred for every function with a body, private or `pub`, and Zig-style `catch` (`catch err => …`); see [../decisions.md](../decisions.md). The rule below about an ordinary `enum` with no inferred sets no longer holds, and error-only cleanup is spelled `defer catch` instead of `errdefer`. Details go through a diagnostics parameter, as recommended below, and error return traces run in debug builds. Rank 4 is settled.

**Recommendation.** Choose option B, with the following rules.
- The error type is an ordinary `enum` whose zero means "no error", and it is written in the signature. There are no inferred sets.
- `try` propagates, `catch` handles, and `errdefer` runs only when the function returns an error. A variant that runs only on success, like C3's `defer try`, can wait until real code needs it.
- Out-of-memory aborts in the default allocators.
- Details such as a file name or line number go through an explicit diagnostics out-parameter, so the error code fits in a register.
- Exceptions are rejected (section 5).
- Multiple return values become tuples (2.12), so there is one way to return two things and one way to fail.

**How often one `try` covers several calls (measured 2026-11-11).** In 11 files of Zig 0.15.2's standard library (`zig/Parse.zig`, `zig/AstGen.zig`, `http/Client.zig`, `json/static.zig`, `Build/Step/Run.zig`, `Build/Step/Compile.zig`, `process/Child.zig`, `tar.zig`, `zip.zig`, `fs/Dir.zig`, `crypto/tls/Client.zig`), 2,721 statements contain `try`, and 74 of them (2.7%) contain more than one. A typical case is `try zig_args.append(try std.fmt.allocPrint(…))`. Swift's rule, one `try` per expression, therefore saves one word in about 3 of 100 statements that can fail. Swift's rationale gives the choice to the writer: "Other developers may be content with knowing that something within a statement can throw" ([ErrorHandlingRationale.md](https://github.com/swiftlang/swift/blob/main/docs/ErrorHandlingRationale.md)). Swift marks expressions, never blocks. Its `do { … } catch { … }` handles errors for a block, but each throwing expression inside it still needs its own `try`. In the same files, 23 of 2,856 uses of `try` (about 0.8%) wrap a chain in parentheses, as in `(try p.expectExpr()).toOptional()`, which is the cost of `try` binding like a prefix operator. The owner chose one `try` per call on 2026-11-11 ([../decisions.md](../decisions.md)).

**Own sheet?** Yes, together with optionals (2.5) and the tokens in 3.1. Outside the excluded sheets, this is the largest open decision that combines syntax and semantics.

### 2.5 Pointers: nullability and kinds (rank 5)

**What it is.** Two questions. Can a pointer be null? And does one pointer type serve both "one object" and "the start of an array"? C answers yes to both. Zig keeps the cases apart:

```zig
var x: i32 = 1;
const p: *i32 = &x;      // one item: never null; no p[1] and no p + 1
const q: ?*i32 = null;   // optional pointer: the same size, null is address 0
const m: [*]u8 = buf.ptr; // many items: m[i] and m + n are allowed
```

**Who has it.**

| Language | One item | Array | Null | Unannotated C pointers |
|---|---|---|---|---|
| C | `T*` | `T*` | allowed | — |
| Zig | `*T`, no indexing or arithmetic ([Pointers](https://ziglang.org/documentation/0.15.2/#Pointers)) | `[*]T`; `[*:0]T` ends at a sentinel | only `?*T`; "The null of the optional is guaranteed to be address 0" ([Optionals](https://ziglang.org/documentation/0.15.2/#Optionals)) | `[*c]T`, "to be avoided whenever possible". It exists because "it is ambiguous whether pointers should be translated as single-item pointers (`*T`) or many-item pointers (`[*]T`)" ([C Pointers](https://ziglang.org/documentation/0.15.2/#C-Pointers)) |
| Odin | `^T`, which does "not support pointer arithmetic" ([overview](https://odin-lang.org/docs/overview/#pointers)) | `[^]T`: indexing without bounds checks, and slicing ([overview](https://odin-lang.org/docs/overview/#multi-pointers)) | `nil` is the zero value | hand-written bindings |
| Carbon | pointers "cannot be null and they cannot be indexed or have pointer arithmetic performed on them" ([values.md](https://github.com/carbon-language/carbon-lang/blob/19f1bf94dbb9b13f0595db32ce82e0a4ad19f782/docs/design/values.md#pointers)) | slices, still to be designed | "Optional pointers are expected to serve nullable use cases" | C++ interop, still being designed |
| Hare | "Pointers in Hare, by default, cannot be “null”"; a `nullable *T` must be tested before use ([tutorial](https://harelang.org/tutorials/introduction/#pointer-types-in-depth)) | slices | `nullable *T` | — |
| Go, Jai, C3 | one nullable kind | Go: no arithmetic; C3: C's arithmetic (unverified); Jai: arithmetic (community source) | allowed | — |
| Swift, Kotlin | non-null references | — | `T?` | a third kind. Swift used implicitly unwrapped optionals, "a valuable tool for importing Objective-C APIs where the nullability of a parameter or return type is unspecified" ([SE-0054](https://github.com/swiftlang/swift-evolution/blob/de9bd49899f3386e63c7c2c88d8d73ed9d22ef38/proposals/0054-abolish-iuo.md)). Kotlin has platform types, where "`T!` means "`T` or `T?`"" ([Kotlin](https://kotlinlang.org/docs/java-interop.html#null-safety-and-platform-types)) |

**Why it matters for chibi.**
- **On wasm, a null dereference doesn't trap.** Emscripten's docs say: "By default, with Emscripten, dereferencing a null pointer does not immediately cause a segmentation fault ... as 0 is just a normal address in a WebAssembly memory" ([Emscripten](https://emscripten.org/docs/debugging/Sanitizers.html#catching-null-dereference)). Without a language rule, a nil dereference on the web build silently reads or corrupts memory.
- **Design A makes non-null cheap.** Long-lived references are handles, where 0 means none. Raw pointers are local and short-lived, so most come from `&x` and are never nil.
- **Self-hosting needs array pointers.** The lexer walks `const char *` cursors ([c-corpus.md](c-corpus.md), section 6.1), which need indexing and arithmetic.
- **Non-null languages added a third pointer kind for foreign code without annotations.** Zig has `[*c]T`, Swift had implicitly unwrapped optionals, and Kotlin has platform types. chibi avoids it only if its header generator decides the kind of each pointer.

**Cost of deciding late.** Tightening later means finding every pointer that might be nil, which takes flow analysis of all existing code. Relaxing later breaks nothing. The split between one-item and array pointers touches every binding the generator writes.

**Recommendation.**
- **`*T` is never nil and has no indexing or arithmetic.** `?*T` may be nil and is the same size, and `*T` converts to `?*T` implicitly. Dereferencing a `?*T` without unwrapping it is a compile error.
- **`[*]T` points into an array.** It indexes without bounds checks and supports `p + n`. A slice `[]T` is the checked form, and `p[a..b]` turns an array pointer into a slice.
- **A type that contains a `*T` has no zero value,** so declaring one without an initializer is a compile error (2.6). The checker keeps one bit per type for this. A container that grows fills new slots with `undefined` and writes them before reading, as Zig's containers do.
- **Header mapping.** The generator maps:
  - pointers to structs and opaque types to `?*T`;
  - pointers to numbers to `?[*]T`;
  - `char *` to `cstring` (2.10);
  - pointers marked with clang's `_Nonnull` to a non-optional pointer.

  The bindings configuration can override any single declaration. Passing a chibi pointer to C is free, because `*T` converts to `?*T`. Only pointers that come back from C need unwrapping, and those really can be NULL, since `SDL_CreateWindow` returns NULL when it fails.
- **Unwrapping** uses an explicit binding (`if w := maybe_window { … }`), a trapping `.?`, or a default with `??` (3.1). chibi does not do flow-sensitive narrowing, because the checker would need a dataflow pass over every function.
- **`nil` is the empty value of every optional,** and of nothing else. It is already a keyword. Slices and strings are not optional, so their zero value is simply empty.
- **Optional values `?T` use the same syntax,** laid out as `{T value; bool present}`. If the unions sheet adds tagged unions, `?T` can still stay built in, for the null-pointer form and the C layout.

**C's `void *` and chibi's `*void` (added 2026-11-11, open).** chibi has a `void` type of size 0, whose value is `.{}` ([../decisions.md](../decisions.md), the `try` row). C's `void *` points to bytes of unknown size, and the header mapping above doesn't cover it yet.
- **A. `*void` is C's untyped pointer** (C, C3). C3's `void*` is the exception to explicit pointer casts: "any type may implicitly convert *to* or *from*" it ([conversions](https://c3-lang.org/language-rules/conversion/)). Translated C keeps its spelling. But `void` then means two things, and generic code that builds `*T` with `T = void` gets an untyped pointer instead of a pointer to a zero-size value. C3 met this in 0.6.4, when it had to "Disallow casting a `void*` to `any`" because the type inside became "`void`, which is a nonsense value" ([0.6.4](https://c3-lang.org/blog/new-contract-syntax-with-064/)).
- **B. A separate untyped pointer type; `*void` is an ordinary pointer to a zero-size value** (Zig, Rust, Odin, Go). Zig's `anyopaque` stands for C's `void`: "void has a known size of 0 bytes, and anyopaque has an unknown, but non-zero, size" (language reference 0.15.2). Rust's `*mut c_void` "is equivalent to C's `void*`", which "is _not_ the same as C's `void` return type, which is Rust's `()` type" ([c_void](https://doc.rust-lang.org/std/ffi/enum.c_void.html)). Odin has `rawptr`, a "raw pointer type", next to `uintptr`; Go's `unsafe.Pointer` "represents a pointer to an arbitrary type" ([unsafe](https://pkg.go.dev/unsafe)). `void` keeps one meaning and generic code stays regular, but a C programmer who writes `*void` gets something else.
- **C. B, and `*void` written in source is an error that names the untyped pointer type,** as `const Value := i32 | string` is an error that points to `union`. Generic code is unaffected, because `*T` with `T = void` isn't written as `*void`.
- **D. C, spelled with a star: `*opaque`** (Hare; Zig's `*anyopaque` and Rust's `*mut c_void` have the same shape). `opaque` is a type of unknown size, so it can only be pointed to. The type still reads as a pointer, `void` keeps one meaning, and the same word can declare C's incomplete structs, as Zig's `opaque {}` does. Hare splits C's `void` the way chibi does: it "provides void as a zero-size type, and opaque as an undefined-size opaque type" ([types::c](https://docs.harelang.org/types/c/)). Other spellings: Jai keeps `*void` ([The Way to Jai, chapter 10](https://github.com/Ivo-Balbaert/The_Way_to_Jai/blob/main/book/10A_What_are_pointers.md), a community source), Swift imports `void *` as `UnsafeMutableRawPointer` and `const void *` as `UnsafeRawPointer` ([HowSwiftImportsCAPIs.md](https://github.com/swiftlang/swift/blob/main/docs/HowSwiftImportsCAPIs.md)), and Pascal has an untyped `Pointer`.

**Converting back (added 2026-11-11).** The owner suggested implicit conversion in both directions.
- Implicit both ways: C, where a `void` pointer converts "to and from any pointer to object type" ([cppreference](https://en.cppreference.com/w/c/language/conversion)); C3 (above); Free Pascal, whose untyped pointer "is assignment compatible with all pointer types" ([Reference Guide 12.7](https://www.freepascal.org/docs-html/ref/refse87.html)).
- Implicit only into the untyped pointer: C++, which needs `static_cast` back ([cppreference](https://en.cppreference.com/w/cpp/language/implicit_conversion)); Odin, whose list of implicit conversions has `^T -> rawptr` and no reverse ([overview](https://odin-lang.org/docs/overview/)); Zig, where std's `ArenaAllocator` stores itself with `.ptr = self` and gets itself back with `@ptrCast(@alignCast(ctx))` ([arena_allocator.zig](https://github.com/ziglang/zig/blob/0.15.2/lib/std/heap/arena_allocator.zig)).
- Explicit both ways: Rust, whose coercions turn `&mut T` into `*mut T` but never reach `*mut c_void` ([reference](https://doc.rust-lang.org/reference/type-coercions.html)), and Go, with `unsafe.Pointer(p)` and `(*T)(p)`.
- In chibi the explicit form is `bitcast(*Player, p)`, and the Casts row already checks alignment there in debug and release builds. An implicit conversion back would keep that check but hide it: a branch that can trap, with nothing in the source to search for.

**Recommendation (not decided; revised 2026-11-11 after the owner asked to keep the `*` visible).** D. C's `void *` imports as `?*opaque`. Any pointer converts to `*opaque` implicitly, because only type information is lost, and converting back is `bitcast(*T, p)`, as C++, Odin and Zig require and C3 doesn't. Indexing and arithmetic on a `*opaque` are errors, and a written `*void` is an error that names `*opaque`. This replaces the earlier recommendation of C spelled `rawptr`.

**The owner's preference (2026-11-11): A, spelled `*void` as in Jai.** Not decided; it is settled when calls into C are built. A version of A that keeps generic code working:
- `*void` stays a pointer to the zero-size `void`, as in Zig, so dereferencing it reads and writes nothing, and a set built as `Map(K, void)` still compiles. On top of that, any pointer converts to `*void` implicitly, as in C.
- The cost that remains: the compiler can't tell a pointer to a zero-size value from a pointer to unknown bytes. A generic `fn clear(p: *$T)` called with a pointer from C deduces `T = void` and silently clears nothing. Zig, Rust, Odin, Go and Hare avoid this with a separate type, and C3 hit the same confusion in 0.6.4 (option A above).

**Sidestepping it while keeping `*void` (added 2026-11-11).** Later the same day the owner wrote: "I'm convinced a separate type is necessary unless there's a way to sidestep this issue". One way works: two types inside the compiler, only one of which can be written.
- A written `*void` is C's untyped pointer. It can't be dereferenced, and a generic can't deduce `T` from it; that is an error that says to `bitcast` first.
- A pointer to a zero-size value, which only generic code creates (`*V` with `V = void`), is a different type that has no spelling. Dereferencing it reads and writes nothing, so `Map(K, void)` still works.

That removes the silent bug, at three costs:
- `*T` with `T = void` is no longer the type that `*void` names, so a generic instance stops meaning what the same code means written out by hand.
- Error messages and reflection need a name for a type nobody can write.
- None of the languages surveyed here does it, so there is no experience to borrow.

The languages that keep C's spelling show what happens otherwise. C has no generics. C++ keeps `void*` and keeps `void` "an incomplete type that cannot be completed" ([cppreference](https://en.cppreference.com/w/cpp/language/types)), so generic code special-cases `void`. The Regular Void proposal ([P0146R1](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2016/p0146r1.html), 2016) cites `std::promise<void>`, whose `set_value` takes no value, and needs an overload for it. C3 hit the confusion in 0.6.4. Jai's behaviour is not verified.

**When the alignment check traps (measured 2026-11-11, Apple M4 Pro, Apple clang 21).** Only when an address isn't a multiple of the target type's alignment. A round trip such as `&player` to `*void` and back to `*Player` never traps, because the address came from a `*Player`. Reinterpreting bytes at an offset does: `bitcast(*u32, &bytes[1])`.
- **C:** creating the misaligned pointer is already undefined behaviour (C11 6.3.2.3p7), and nothing checks it. In `/tmp/align-demo/align.c`, a `void *` at `buf + 1` converted implicitly to `uint32_t *` loaded correctly with `ldr`, and so did an atomic `ldapr` at `buf + 1`. The same atomic load at `buf + 13`, which crosses a 16-byte boundary, died with SIGBUS (exit 138). `-fsanitize=alignment` reported the load, "load of misaligned address … which requires 4 byte alignment", at the line that reads through the pointer, not at the conversion.
- **C3:** safe mode checks the access, not the cast. Its 0.7.7 notes mark `*b_ref = 2` through a pointer to a packed field as an "Error at runtime in safe mode, unaligned access", and add `typedef IntAlign1 = int @align(1)` for pointers that may legally be under-aligned ([0.7.7](https://c3-lang.org/blog/c3-language-at-0-7-7-vector-abi%2C-riscv-improvements-and-more/)). C3's implicit `void*` conversion therefore never traps itself.
- **Zig:** `@alignCast` adds "a pointer alignment safety check" at the cast, in its safe build modes (language reference 0.15.2).
- **Rust:** since 1.70, builds with debug assertions check each dereference of a raw pointer and panic with "misaligned pointer dereference" ([#98112](https://github.com/rust-lang/rust/pull/98112)).
- **chibi (Casts row):** checks at the cast, in debug and release builds. That is the cheaper place: a pointer can only become misaligned through a conversion, so one check per conversion replaces one per access. Today every chibi conversion that can trap is written (`u8(total)`, `bitcast`), and implicit conversions never trap. An implicit `*void` to `*T` would be the first implicit conversion with a check; C3 and Rust avoid that by checking at the access, which costs a check per access instead.

**Own sheet?** Yes, in the sheet on errors and optionals.

### 2.6 Initialization (rank 6)

**What it is.** This is what a variable holds when its declaration gives no value. Some languages fill it with zero bytes and offer a way to skip that; others require a value:

```odin
x: int                 // 0
buf: [4096]u8 = ---    // left uninitialized, for speed
```

**Who has it.**

| Language | Default | Opt out | Field defaults | Globals |
|---|---|---|---|---|
| Go | zero value | — | — | initialized at run time in dependency order, plus `init` functions (unverified) |
| Odin | "Variables are initialized to zero by default unless specified otherwise" ([overview](https://odin-lang.org/docs/overview/#zero-values)) | `---` | none | `@(init)` procedures run at startup ([attributes](https://odin-lang.org/docs/overview/#attributes)) |
| Jai (community source) | zero | `---`, also per field | applied on declaration | (unverified) |
| C3 | "Unlike C, C3 local variables are zero-initialized by default" ([variables](https://c3-lang.org/language-fundamentals/variables/)) | `@noinit`. Reading such a variable before writing it "will trap or be initialized to a specific value" in safe builds and "is undefined behaviour in "fast" builds" | — | zeroed (search excerpt) |
| Zig | a value is required | `undefined` means "the value could be anything". Debug and ReleaseSafe builds write `0xaa`, which is "only an implementation feature, not a language semantic" ([undefined](https://ziglang.org/documentation/0.15.2/#undefined)) | used by struct literals ([Default Field Values](https://ziglang.org/documentation/0.15.2/#Default-Field-Values)) | "The initialization value of container level variables is implicitly comptime" ([Container Level Variables](https://ziglang.org/documentation/0.15.2/#Container-Level-Variables)) |
| Hare | "you must provide the initial value with an _initializer_" ([tutorial](https://harelang.org/tutorials/introduction/#using-const--let-to-define-variables)) | — | — | — |
| Carbon | objects may be "unformed"; the design is still open | — | — | — |

For C3's zeroing, a search excerpt of its documentation gives three reasons (unverified). It "avoids a whole class of vulnerabilities". Mandatory initialization "adds a lot of extra boilerplate". And because there is an opt-out, the change "comes at no performance loss".

**Why it matters for chibi.**
- Old Chibi's rule "Make each record's all-zero value valid and empty" and design A's handle 0 meaning "none" both assume zeroing.
- chibi already has field defaults (`struct Point { x: int, y: int = 0 }`), so it must say whether a declaration applies them.
- Hot reload needs a value for every field that a new layout adds.
- Globals decide whether a program has hidden startup code and an initialization order.

**Cost of deciding late.** The change would be silent, because switching between zero, garbage and defaults changes what existing code reads.

**Recommendation.**
- **Zero by default.** Every variable, field and array element starts as zero, unless it has an initializer or a field default.
- **`undefined` opts out,** as in `buf: [4096]u8 = undefined`. Reading it before writing it is illegal behavior, and debug builds fill it with a byte pattern.
- **Field defaults.** They are compile-time constants, and a declaration without an initializer applies them, as Jai does. The compiler marks each type as all-zero, cleared with `memset`, or as having defaults, copied from a constant template.
  - Raw allocation returns zero bytes, not defaults; a typed `new` applies them.
  - When a reload adds a field, live objects get its default.
- **No zero value with a non-null pointer.** A type that contains a `*T` (2.5) has none, so it must be initialized.
- **Globals.** Their initializers run at compile time, as in Zig, and the resulting bytes go into the image. There is no startup code and no initialization order; setup that needs the running program goes in `main`.
- **Globals across a reload.** A global keeps its current value. A new global starts with its compile-time bytes, and an edited initializer does not run again. That rule needs an explicit way to reset a global, which belongs to the hot reload design.
- **Spelling.** Use a keyword, `undefined`, rather than `---`. `---` would be a three-byte operator, which hypothesis F7 proposes to drop from the lexer, and the `++`/`--` sheet may claim `--`.

**Own sheet?** No for locals and fields; this note is enough. The rules for globals and reload belong to the hot reload design.

### 2.7 Calling convention and the implicit context (rank 7)

**What it is.** Odin and Jai give every function a hidden `context` argument. It holds the current allocator, a temporary arena, a logger and user data, so library code can allocate without an allocator parameter. Zig passes an `Allocator` explicitly, and its 0.15.1 release notes extend the idea to I/O: "All code that performs I/O will need access to an `Io` instance" ([release notes](https://ziglang.org/download/0.15.1/release-notes.html#IO-as-an-Interface)).

```odin
// Odin: every Odin-convention call receives `context` by pointer.
context.allocator = arena_allocator
data := make([]u8, 1024) // allocates from context.allocator
```

**Who has it.**

| Language | Where the context lives | C callbacks |
|---|---|---|
| Odin | "implicitly passed by pointer" to every Odin-convention procedure ([overview](https://odin-lang.org/docs/overview/#implicit-context-system)). Its fields include an allocator, a temporary allocator, a logger, a random generator and user data ([core.odin](https://github.com/odin-lang/Odin/blob/85afcb386c389aafcec4f2e33d27ff8b42579822/base/runtime/core.odin#L527-L539)) | a `"c"` procedure must assign `context = runtime.default_context()` before using it ([overview](https://odin-lang.org/docs/overview/#explicit-context-definition)) |
| Jai (community source) | "the memory allocator is implicitly passed to all procs (unless otherwise specified with **#c_call**)"; `push_context` replaces it for a block | `#c_call` procedures have none |
| Zig | none; `Allocator` and `Io` parameters | nothing to set up |
| Go | none; `context.Context` parameters by convention (unverified) | — |

**Why it matters for chibi.**
- Hypothesis I2 says a call between chibi and C costs no more than a call between C functions. Its plan for failure already lists "an implicit context" among the parts of the runtime to check.
- SDL calls back into chibi from its own threads, such as the audio thread, where an Odin-style context doesn't exist.
- Tasks move between threads, so whatever holds the context must move with them.
- For old Chibi, the owner asked for "an implicit thread-local context struct" and agreed to start with Odin's `context.temp_allocator`. These were old Chibi directions from 2026-10, recorded in session notes, not in the repository.

**Cost of deciding late.** The change would be structural, because it sets the calling convention of every function and shapes the development backend, every C callback and the task switch.

**Options.**
- **A. A hidden parameter (Odin, Jai).** It costs a register on every call and a setup step in every C callback. The context follows a task automatically, because it is an argument.
- **B. A thread-local pointer to a context struct.** Calls cost nothing extra. Code pays one thread-local load where it uses the context, which is at allocation and logging sites, far rarer than calls. Each thread starts with a default context, so C callbacks just work. The task scheduler saves and restores one pointer when it resumes a task on another thread.
- **C. Explicit parameters only (Zig).** Allocation shows in every signature, which old Chibi's rule "it hides no allocation" favors, but logging and scratch memory become noisy.

**Recommendation.**
- Every chibi function uses the platform C calling convention. Any function can then be handed to C as a callback without an adapter; development builds still route the call through the function's reload stub (I4).
- The context is option B, a thread-local pointer that task switches save and restore.
- An allocation that outlives its call takes the arena explicitly, as in C. The context's temporary arena serves scratch work within a frame.
- Measure the cost of a context read from chibi's own backend on macOS arm64 and Windows x64. Operating systems implement thread-local storage differently, and this note didn't check what it costs on each (unverified).

**Own sheet?** A short one, with that measurement and a C callback test on SDL's audio thread.

**Decided 2026-11-20:** the context travels as a hidden first argument in every backend (decisions.md, the Language row of 2026-11-18), not in a register that C preserves. Step 3d.9 built it on arm64 the same day ([checker-and-ir.md](checker-and-ir.md) section 8).

**Prior art for option A, gathered 2026-11-19,** after the owner chose it (decisions.md, the Language row of 2026-11-18):
- **Jai,** from the community book *The Way to Jai* ([ch. 25](https://github.com/Ivo-Balbaert/The_Way_to_Jai/blob/main/book/25A_Context.md), [ch. 29](https://github.com/Ivo-Balbaert/The_Way_to_Jai/blob/main/book/29A_Interacting_with_C.md), [ch. 31](https://github.com/Ivo-Balbaert/The_Way_to_Jai/blob/main/book/31A_Working_with_threads.md)). The context holds the thread index, the allocator, a logger, temporary storage, a stack trace and an assertion handler, and any module adds a field with `#add_context name := value`. `push_context c { … }` replaces the context for a block and everything the block calls, and `push_allocator(a)` swaps the allocator until the scope ends. `#no_context` marks a procedure that doesn't use the context. Calling Jai code from a `#c_call` procedure is a compile error until that procedure writes `push_context`, and the runtime's C `main` pushes a global `first_thread_context`. Each thread has its own context. None of these sources says which register carries it.
- **Odin.** The context is local to each scope, so a change never reaches the caller. Odin calls this copy-on-write and documents that it can't update the context inside a loop ([overview](https://odin-lang.org/docs/overview/#implicit-context-system)). `"contextless"` opts a procedure out.
- **A register that C preserves.** Go keeps the current goroutine in R28 on arm64 and R14 on amd64, and on amd64 restores it from thread-local storage when other code calls in ([abi-internal.md](https://github.com/golang/go/blob/go1.23.0/src/cmd/compile/abi-internal.md)). OCaml keeps its domain state pointer in x28 on arm64 and r14 on amd64 ([arm64 proc.ml](https://github.com/ocaml/ocaml/blob/trunk/asmcomp/arm64/proc.ml), [amd64 proc.ml](https://github.com/ocaml/ocaml/blob/trunk/asmcomp/amd64/proc.ml)). HotSpot keeps the current thread in r28 on arm64 ([assembler_aarch64.hpp](https://github.com/openjdk/jdk/blob/master/src/hotspot/cpu/aarch64/assembler_aarch64.hpp)). LLVM passes Swift's async context in x22, "an otherwise callee saved register so that it will be preserved for normal function calls" ([AArch64CallingConvention.td](https://github.com/llvm/llvm-project/blob/main/llvm/lib/Target/AArch64/AArch64CallingConvention.td)).
- **Koka.** Its generated C passes the context as a `_ctx` parameter, and code entered from C reads the same pointer from a thread-local ([kklib.h](https://github.com/koka-lang/koka/blob/master/kklib/include/kklib.h)).
- **What it means for callbacks.** C restores x28 before it returns, but may use x28 while it runs, so a callback can't find its caller's context there. Jai and Odin build a default context in the callback; Go and Koka read the thread's context from a thread-local. Giving a callback the context of the chibi code that called into C costs a thread-local store before each call into C.

### 2.8 Parameters and aliasing (rank 8)

**What it is.** When a function takes a large struct by value, does it get a copy? If parameters are immutable, a compiler may pass a pointer to the caller's original instead of copying, which is faster. But then a write to the original during the call shows through the "copy". Zig had exactly this bug:

```zig
fn inner(state: *State, thing: Thing) void {
    state.thing.value = 0; // writes the caller's original...
    // ...and thing.value is now 0 too, because thing was passed by reference
}
inner(&state, state.thing);
```

**Who has it.**

| Language | Parameters | How large values are passed |
|---|---|---|
| C | mutable copies | by a pointer to a copy that the caller makes, per the platform ABI |
| Zig | "parameters are immutable", and "Zig may choose to copy and pass by value, or pass by reference" ([Pass-by-value Parameters](https://ziglang.org/documentation/0.15.2/#Pass-by-value-Parameters)) | The aliasing above is [issue #5973](https://github.com/ziglang/zig/issues/5973), "eliminate hidden pass-by-reference footguns". The team's 2024 conclusion: "PRO in its current form will cease to exist", and parameters become references only when the optimizer proves the function pure ([Ziggit, quoting the issue](https://ziggit.dev/t/plan-to-address-attack-of-the-killer-features-parameter-reference-optimization-aliasing-issue-just-dropped/6199)). Forum users reported in May 2026 that 0.16 copies such arguments with `memcpy` and that the reference's wording is out of date ([Ziggit](https://ziggit.dev/t/pass-by-value-semantics/15710)) |
| Odin | "all parameters are immutable in Odin"; `x := x` makes a mutable copy ([overview](https://odin-lang.org/docs/overview/#parameters)). The FAQ says a pointer passed in place of a copy "does not matter from a user perspective as the parameter value is immutable" ([FAQ](https://odin-lang.org/docs/faq)) | by pointer when that's cheaper. The default convention passes parameters over 16 bytes by reference, "subject to change", and a December 2025 test saw a 32-byte struct passed as a pointer to the original with no copy ([forum](https://forum.odin-lang.org/t/does-dereferencing-a-pointer-copy-the-struct-when-passing-to-a-procedure-in-odin/1421)). A write to the original during the call would then show through |
| Jai | no public spec. A community guide: arguments over 8 bytes, and every struct, are "passed maybe by reference", read-only like a C++ `const&`, and "in practice things pretty much always work as if they are copied" ([guide](https://sjoerdev.github.io/posts/jai-guide/)) | maybe by reference (unverified) |
| Carbon | value bindings, which may be a copy or a pointer. "any mutation to the object or its storage ends the lifetime of the value binding", and this is "_not_ intended to ever become "undefined behavior"". The rule is marked experimental ([values.md](https://github.com/carbon-language/carbon-lang/blob/19f1bf94dbb9b13f0595db32ce82e0a4ad19f782/docs/design/values.md#value-expressions)) | either |
| Mojo | "By default, all arguments use the default convention of an immutable read-only reference". "Small values like `Int`, `Float`, and `SIMD` are always passed in machine registers". A `mut` argument "can't receive any other references to the same value" ([ownership](https://mojolang.org/docs/manual/values/ownership)) | by reference, checked for exclusive access |
| Swift | `inout`, with `&` at the call site (unverified) | — |

Old Chibi paid the cost from the other side. When its analysis couldn't prove that a call left the caller's storage alone, it copied every argument. A single store took stage one compiling itself from 25 to over 60 seconds (old Chibi friction, "Ordinary writes make calls copy their arguments").

**Why it matters for chibi.**
- Game code passes transforms, matrices and other large structs by value.
- Aliasing bugs appear only when two arguments reach the same memory, so tests rarely catch them.
- `export fn` must follow the C ABI either way.

**Cost of deciding late.** The change would be silent wherever aliasing exists, and structural for the ABI.

**Decided 2026-11-19** as recommended below: parameters behave as copies, and a large struct goes by pointer to a copy that the caller skips when nothing the callee can reach is able to write the original. Checker step 3d.8 built it the same day ([checker-and-ir.md](checker-and-ir.md) section 8). See [../decisions.md](../decisions.md).

**Recommendation.**
- **Copy semantics.** Parameters are immutable bindings with copy semantics, so the callee always sees the values as they were at the call.
- **How large structs travel.** They go by pointer, as the C ABI already does. The caller copies first, unless it proves that nothing the callee can reach writes the original during the call; this is the rule Zig adopted.
- **Writes are marked.** A function that mutates an argument takes a `*T`, and the caller writes `&x`, so the call site shows the write. chibi needs no `ref` or `inout` parameters.
- **Results** follow the same rule, so they behave as if returned into a temporary.
- **Measure** what the copies cost in debug builds of real code.

**Own sheet?** No.

### 2.9 Safety model and build modes (rank 9)

**What it is.** This is about which mistakes are checked at run time, in which builds, and what a failed check does. Zig calls a mistake that it can detect at run time "safety-checked Illegal Behavior". It panics in Debug and ReleaseSafe builds. In ReleaseFast and ReleaseSmall there are no checks, and "anything can happen" ([Illegal Behavior](https://ziglang.org/documentation/0.15.2/#Illegal-Behavior), [Build Mode](https://ziglang.org/documentation/0.15.2/#Build-Mode)).

**Who has it.**

| Language | Modes | Bounds checks | A failed `assert` in fast builds |
|---|---|---|---|
| Zig | Debug and ReleaseSafe check; ReleaseFast and ReleaseSmall don't. `@setRuntimeSafety` works per scope | safety checks | In ReleaseFast "the optimizer uses the assumption that `unreachable` code will never be hit" ([unreachable](https://ziglang.org/documentation/0.15.2/#unreachable)). Doc comments say "assume" for unchecked and "assert" for checked illegal behavior ([Doc Comment Guidance](https://ziglang.org/documentation/0.15.2/#Doc-Comment-Guidance)) |
| C3 | Safe Mode (`-O0`, `-O1`) and Unchecked Mode (`-O2` and up) ([debugging](https://c3-lang.org/misc-advanced/debugging/)) | Safe Mode only | the condition "is assumed to always be `true`"; `@assert_always` keeps the check. A violated contract lets the compiler "optimize as if every contract holds" ([contracts](https://c3-lang.org/language-common/contracts/), search excerpt) |
| Carbon | development, performance and hardened builds ([arithmetic.md](https://github.com/carbon-language/carbon-lang/blob/19f1bf94dbb9b13f0595db32ce82e0a4ad19f782/docs/design/expressions/arithmetic.md#overflow-and-other-error-conditions)) | design open | — |
| Odin | one mode | `#no_bounds_check` and `#bounds_check` per statement, block or procedure; `-no-bounds-check` for a whole build ([directives](https://odin-lang.org/docs/overview/#directives)) | (unverified) |
| Jai (community source) | build options | on; `#no_abc` per procedure | `assert` compiles out with `ENABLE_ASSERT=false` |
| Go, Rust | — | always on | Rust's `debug_assert!` compiles out (unverified) |

**Why it matters for chibi.**
- Players run release builds. The release behavior of a bug decides whether a player sees a crash report or a corrupted save.
- The C backend can't give an out-of-bounds access a defined meaning. Without a check, the access is C undefined behavior.

**Cost of deciding late.** The change would be silent, because the same bug would behave differently after it.

**Recommendation.**
- **Three modes.** Debug and release builds run every check. Release-fast builds turn overflow checks into wrapping (2.1) and keep every other check.
- **Bounds checks are on in every mode.** A block opts out with an attribute (`@no_bounds_check`), as Odin and Jai allow, so unchecked code is visible in source and in review.
- **No mode turns a failed check into an assumption.** `assert` checks in debug and release builds and is removed, not assumed, in release-fast builds. `unreachable` traps in every mode. An explicit `assume` can come later for measured hot spots (section 4).
- **Unwrapping traps.** Unwrapping a nil optional or an error with `.?` traps in every mode.

**Own sheet?** Part of the numeric semantics sheet.

### 2.10 Strings and read-only data (rank 10)

**What it is.** A string design makes four choices. It represents a string either as a pointer and a length or as a pointer to bytes that end in NUL. It says whether the bytes can change through the string, and what type a literal has. And it says how a string reaches C, which expects a NUL-terminated `char *`.

```zig
// Zig: a literal points at N bytes plus a NUL, and coerces to both forms.
const s = "hello";              // *const [5:0]u8
const view: []const u8 = s;     // a slice: pointer and length
const c: [*:0]const u8 = s;     // a C string, at no cost
```

**Who has it.**

| Language | Type | Writable through it? | Literal | C string | `for` over a string |
|---|---|---|---|---|---|
| Go | `string` | no; `a[x]` "may not be assigned to" ([spec](https://go.dev/ref/spec#Index_expressions)) | untyped constant | (unverified) | runes (unverified) |
| Odin | `string` | no; strings are immutable | untyped constant | `cstring`; converting one to `string` is O(n) ([overview](https://odin-lang.org/docs/overview/#cstring-type)) | runes, decoded from UTF-8 ([overview](https://odin-lang.org/docs/overview/#range-based-for-loop)) |
| Zig | `[]const u8` | no, through `const` | "const pointers to null-terminated arrays of u8" ([Slices](https://ziglang.org/documentation/0.15.2/#Slices)) | `[*:0]const u8` | bytes |
| Jai (community source) | `string`, a count and a data pointer | (unverified) | — | (unverified) | — |
| Zen C | `string` is `char*`, "C-string (null-terminated)" ([primitive types](https://github.com/zenc-lang/docs/blob/60115e029d02e9cea4c9f721da5c82cd7b257de5/reference/02-primitive-types.md)); the standard library adds a heap `String` | — | — | the same type | — |
| Rust | `&str`, UTF-8 validated | no | `&'static str` | a separate `CStr` type (unverified) | `chars()` |
| Old Chibi, 2026-10-21 | strings "are immutable, carry their byte length and may hold arbitrary bytes, including NUL. UTF-8 validation and decoding are explicit operations" | | | | |

On qualifiers, C3 lists that "Type qualifiers are all removed, including `const`, `restrict`, and `volatile`" ([features](https://c3-lang.org/faq/allfeatures/)). Odin, Go and Jai never had them. Zig has `*const T` and `[]const T`, Rust has `&T` and `&mut T`, and Carbon has `const`-qualified types ([values.md](https://github.com/carbon-language/carbon-lang/blob/19f1bf94dbb9b13f0595db32ce82e0a4ad19f782/docs/design/values.md#const-qualified-types)).

**Why it matters for chibi.**
- **Old Chibi measured the cost of NUL-terminated strings.** Each byte read ran `strlen`, so scanning 1 MiB byte by byte took 19.9 s, against 0.07 s through a byte array. Slicing also measured and copied, so compiling a file grew quadratically with its size (old Chibi friction, "Strings have no indexing or slicing syntax" and "Strings are NUL-terminated and carry no length").
- **C still wants NUL-terminated strings**, and most strings passed to SDL are literals.
- **`const` is paid on every signature.** Generic code must also abstract over it, so a function exists in a `const` version and a mutable one.

**Cost of deciding late.** The change would be wide, because every API that takes text depends on the string type and on how strings convert to C.

**Recommendation.**
- **`string` is a built-in read-only view, made of a pointer and an `int` length.**
  - `s[i]` is a `u8` that can't be assigned.
  - `s[a..b]` is another view, with no copy.
  - Converting `[]u8` to `string` is free, because it is a view; converting `string` to `[]u8` copies.
- **Literals are untyped string constants.** They are stored with a trailing NUL in read-only data, so a literal converts to `cstring` at no cost. A runtime string converts through an explicit copy into an arena.
- **`cstring` is a NUL-terminated pointer to `u8`,** like Odin's `cstring` and Zig's `[*:0]u8`.
- **`for b in s` yields bytes.** UTF-8 decoding is a library iterator, which keeps the cost visible.
- **No `const` qualifiers on pointers or slices.** `string` covers the common read-only case, and C's `const T*` imports as `*T` or `[*]T`. This departs from the asymmetry rule on purpose, because `const` costs something on every signature every day. The price is that adding `const` later would touch every signature, so if the owner wants read-only views beyond strings, they must be decided now.
- **No operators that allocate.** Concatenation is a function that takes an arena; Go's `+`, by contrast, "creates a new string" ([spec](https://go.dev/ref/spec#String_concatenation)).

**Own sheet?** No; this note is enough.

### 2.11 Mutability and shadowing (rank 11)

**What it is.** Two questions. Can a local be reassigned? And may an inner declaration reuse an outer name? The parser thread postponed "fixed locals", runtime values that can't be reassigned after their initialization (Zig's runtime `const`, Rust's `let`). chibi already has mutable `x := v` and compile-time `const X := v`. Zig forbids shadowing:

```zig
const x: i32 = 1;
{
    const x: i32 = 2; // error: Zig never lets an identifier hide another
}
```

**Who has it.**

| Language | Locals | Parameters | Shadowing |
|---|---|---|---|
| Zig | `var` and `const` | immutable | "Identifiers are never allowed to "hide" other identifiers by using the same name", though "you can, however, use the same name if the scopes are separate" ([Shadowing](https://ziglang.org/documentation/0.15.2/#Shadowing)) |
| Odin | mutable | immutable ([overview](https://odin-lang.org/docs/overview/#parameters)) | "Declarations have to be _unique_ within a scope"; the `x := x` idiom shadows a parameter |
| Jai (community source) | mutable | (unverified) | allowed in an inner block; the book advises "use it only when it is really necessary" |
| Zen C | `let` is mutable; a `const` qualifier makes a variable read-only ([variables](https://github.com/zenc-lang/docs/blob/60115e029d02e9cea4c9f721da5c82cd7b257de5/reference/01-variables-constants.md)) | — | not documented |
| Rust, Swift, Kotlin | `let` or `val` by default (unverified) | immutable (unverified) | Rust allows it even in the same scope (unverified) |

**Why it matters for chibi.**
- Hot reload maps a suspended coroutine's saved locals to the new code "by (function, yield point) and saved-local name" ([memory-models.md](memory-models.md), section 7). With shadowing, two live locals can share a name. Without it, a name identifies one local along any chain of scopes.
- Section 2.8 relies on parameters being immutable.

**Cost of deciding late.** The cost is moderate. Forbidding shadowing later means compile errors at each site and a renaming pass. Allowing it later breaks nothing. Fixed locals can be added later, because adding them changes no existing code.

**Recommendation.**
- **No shadowing.** A local may not reuse the name of a local, parameter or package-level name visible at that point, as in Zig. Sibling scopes may reuse names. Because `import "std/fmt"` binds `fmt`, a local named `fmt` in that file is an error.
- **Parameters are immutable.** To change one, copy it into a local with another name.
- **Locals stay mutable, and `const` stays compile-time.** Fixed locals can wait. If they come, give them a separate keyword rather than letting `const` hold runtime values, because chibi's compile-time features lean on `const` meaning "known at compile time".

**Own sheet?** No.

### 2.12 Multiple returns, tuples and destructuring (rank 12)

**What it is.** Some languages let a function return two values and give syntax to unpack them.

```odin
divmod :: proc(a, b: int) -> (int, int) { return a / b, a % b }
q, r := divmod(7, 2)
```

**Who has it.**
- **Go and Odin** have multiple results, and Odin allows named results ([overview](https://odin-lang.org/docs/overview/#named-results)).
- **Zig** returns anonymous structs, "referred to as "tuples"", and "Tuples can be destructured" ([Tuples](https://ziglang.org/documentation/0.15.2/#Tuples), [Destructuring Tuples](https://ziglang.org/documentation/0.15.2/#Destructuring-Tuples)).
- **Jai** has multiple results, named results with defaults, and `#must` (community source).
- **Rust and Swift** have tuples.
- **Old Chibi** asked for `a, b := F()` and `a, b = b, a` (old Chibi friction, "Nothing destructures results").

**Why it matters for chibi.** Error unions (2.4) and tasks need result types that can be stored. Generics take types, not syntax, so a pair of results must be a type to be a type argument.

**Cost of deciding late.** The cost is medium. If the IR treats multiple results as a special call form, turning them into types later changes the IR and the C ABI of every multi-result function.

**Recommendation.**
- **Tuples are anonymous structs with C layout.** `(int, bool)` is a type, and the existing `.{ x, true }` is its literal.
- **Destructuring.** `a, b := f()` declares, and `a, b = b, a` swaps.
- **Multiple results are tuple results.** `return a, b` is short for `return .{ a, b }`. In the C ABI a tuple is a struct, which comes back in two registers when it fits in 16 bytes.

There is one mechanism, a type, rather than a type plus a special multi-value call.

**Own sheet?** In the sheet on errors and optionals.

### 2.13 Function values and closures (rank 13)

**What it is.** The question is whether an inline `fn(x) => e` may use the local variables of the function around it. If it may, the function value must carry those variables with it, which makes it a closure.

```odin
// Odin: allowed, because the literal uses only its parameter.
double := proc(x: int) -> int { return x * 2 }
```

**Who has it.**
- **Odin** "only has non-capturing lambda procedures", because capturing needs "some form of automatic memory management" ([FAQ](https://odin-lang.org/docs/faq/#does-odin-have-closures)).
- **Jai** rejects a capture with the error "Closures are not supported" (community source).
- **C3's** lambdas "do not capture any state" ([features](https://c3-lang.org/faq/allfeatures/)).
- **Zig** has no closures (unverified).
- **Zen C's** lambdas "can capture their environment". They capture by value by default and by reference with `[&]`, and `fn*` is a raw C function pointer ([functions](https://github.com/zenc-lang/docs/blob/60115e029d02e9cea4c9f721da5c82cd7b257de5/reference/04-functions-lambdas.md)).
- **Go, Swift and Rust** have capturing closures, which a collector, reference counting and ownership manage respectively.

**Why it matters for chibi.**
- Design A has no collector. A captured reference into a stack frame dangles once the frame returns, and one into a coroutine frame dangles across a reload.
- Function values stored in data must survive a reload ([metaprogramming-reload-coroutines.md](metaprogramming-reload-coroutines.md), section 6, item 8; hypothesis I4).
- C callbacks need a plain code pointer.

**Cost of deciding late.** The cost is low if function values are thin now. Adding closures later means a second, fat type, and existing code keeps working. Making function values fat now would cost every value 16 bytes and every C callback an adapter.

**Recommendation.**
- **`fn(...)` values are one pointer and compatible with C.** In development builds, the pointer is a stable stub that jumps through the function's reload slot (I4), so a stored function value survives a reload.
- **No capture.** A `fn` literal may use globals, constants and its own parameters, but never the runtime locals of the function around it; that is a compile error.
- **Closures later, if ever,** as a separate type with an explicit by-value capture list.

**Own sheet?** No. Revisit with the tasks sheet, which may want closures for spawning tasks.

### 2.14 Vector math for games (rank 14)

**What it is.** Games need vector arithmetic. Languages provide it either as built-in arithmetic on small arrays (Odin) or through user-defined operators (Jai, C3, Carbon).

```odin
a := [3]f32{1, 2, 3}
b := [3]f32{4, 5, 6}
c := a + b           // {5, 7, 9}: element by element
d := a.zyx           // {3, 2, 1}: a swizzle
```

**Who has it.**

| Language | Arrays | Swizzles | SIMD type | Matrices | User operators |
|---|---|---|---|---|---|
| Odin | element-wise on fixed arrays ([overview](https://odin-lang.org/docs/overview/#array-programming)) | `xyzw` and `rgba` "on any array with length <= 4" ([overview](https://odin-lang.org/docs/overview/#swizzle-operations)) | `#simd`; lanes can't be read with `.x` ([overview](https://odin-lang.org/docs/overview/#simd-vector-types)) | a `matrix` type whose `*` multiplies matrices ([overview](https://odin-lang.org/docs/overview/#matrix-type)) | none; "Operator overloading is very easily abused" ([FAQ](https://odin-lang.org/docs/faq/#why-does-odin-not-have-operator-overloading)) |
| Zig | — | none; `@shuffle` and `@select` instead | `@Vector`, element-wise ([Vectors](https://ziglang.org/documentation/0.15.2/#Vectors)) | — | none; its overview lists operator overloading as hidden control flow ([overview](https://ziglang.org/learn/overview/#small-simple-language)) |
| C3 | — | "Swizzling for arrays and vectors" | vector types | — | for arithmetic, bit operators and equality, since 0.7.1 ([features](https://c3-lang.org/faq/allfeatures/)) |
| Jai (community source) | — | (unverified) | — | in the `Math` module | `operator +`, and so on |
| Carbon | — | — | — | — | `x + y` rewrites to a call on the `AddWith` interface ([arithmetic.md](https://github.com/carbon-language/carbon-lang/blob/19f1bf94dbb9b13f0595db32ce82e0a4ad19f782/docs/design/expressions/arithmetic.md#extensibility)) |
| Zen C | — | not documented | `std/simd.zc` | — | methods named `add`, `eq` and so on ([operators](https://github.com/zenc-lang/docs/blob/60115e029d02e9cea4c9f721da5c82cd7b257de5/reference/06-operators.md)) |
| Lobster | structs of numbers: `int2 { 1, 2 } * int2 { 4, 5 }` gives `int2 { 4, 10 }` ([reference](https://aardappel.github.io/lobster/language_reference.html)) | — | — | — | — |

For old Chibi, the owner agreed to "Odin-style array programming and swizzles, plus `#soa`" (session notes, 2026-10-20). chibi-v2 already has multi-dimensional arrays such as `[4, 4]f32`, indexed `m[i, j]`.

**Why it matters for chibi.**
- Cute Framework passes small vector structs such as `CF_V2 {float x, y}` by value, and GPU buffers hold arrays of them.
- Self-hosting needs SIMD types for the lexer. Hypothesis T8 lists "SIMD vector types and the intrinsics the lexer uses" among its conditions.

**Cost of deciding late.** The cost is medium, and partly silent. Array arithmetic is purely additive. But what `*` means on a `[4, 4]f32`, element by element or a matrix product, must be fixed before code uses it, because changing it later is silent.

**Recommendation.**
- **Arrays.** `+`, `-`, `*` and `/` work element by element on fixed arrays of numbers with the same shape, and broadcast a scalar (`v * 2.0`).
- **Swizzles.** `.xyzw` and `.rgba` work on arrays of length 4 or less. Reading comes first; writing a swizzle (`v.xy = p`) can come later.
- **`*` is element-wise for every shape, including `[4, 4]f32`.** The matrix product, dot and cross products are named functions, as HLSL does with `mul` (unverified). This keeps one rule, with no silent difference between 1-D and 2-D arrays. Odin's separate `matrix` type is the alternative.
- **A SIMD vector type, separate from arrays,** gives guaranteed registers, lane masks and the intrinsics the lexer needs.
- **Operator overloading can wait** until the methods sheet. If it comes, Carbon's model, where an operator rewrites to a method call, fits chibi best. Adding it later is additive.

**Own sheet?** Yes. A "vector math and SIMD" sheet should also cover the SIMD intrinsics that self-hosting needs.

### 2.15 Exhaustive switch and open enums (rank 15)

**What it is.** The question is whether a `switch` over an enum must handle every member, and whether an enum may hold a value that has no name. chibi already has `_` arms and no fallthrough. Zig requires full coverage:

```zig
switch (color) {
    .red => …,
    .green => …,
    // error: .blue not handled, unless an `else` arm is present
}
```

**Who has it.**
- **Zig's** reference says "When a `switch` expression does not have an `else` clause, it must exhaustively list all the possible values", and "A non-exhaustive enum can be created by adding a trailing `_` field" ([Exhaustive Switching](https://ziglang.org/documentation/0.15.2/#Exhaustive-Switching), [Non-exhaustive enum](https://ziglang.org/documentation/0.15.2/#Non-exhaustive-enum)).
- **Odin** requires `#partial switch` to leave enum or union cases out ([overview](https://odin-lang.org/docs/overview/#partial-switch)).
- **Rust** requires exhaustive matches (unverified).
- **C and Go** don't check.

**Why it matters for chibi.** When an edit adds an enum member, possibly during a hot-reload session, the compiler should point at every `switch` that needs a new arm. C enums are open sets, because SDL flags and error codes can hold any integer.

**Cost of deciding late.** The cost is moderate. Requiring exhaustiveness later breaks every incomplete `switch`, and each one needs a decision by hand. Relaxing it later breaks nothing.

**Recommendation.**
- **Exhaustive by default.** A `switch` over a chibi enum, and later over a tagged union, covers every member or has a `_` arm.
- **C enums are open.** Enums imported from C are open, so switches over them need `_`.
- **Conversions.** Converting an integer to a chibi enum checks the range; the casts sheet owns this.
- **Reload.** A stored value whose member an edit deleted is the hot reload design's problem, and that design must map the value by name or trap.

**Own sheet?** No.

## 3. Syntax to settle now

These choices are cheaper than the semantics above, because the compiler finds every use when a spelling changes. They are worth settling now because the semantics in section 2 need a spelling, and because several of them interact with the lexer's two-byte operator table and with the line-break rules.

| Item | Cost of deciding late | Own sheet? |
|---|---|---|
| 3.1 Tokens for optionals and errors | Mechanical but wide, because every fallible call and every optional use changes | In the errors and optionals sheet |
| 3.2 Pointer kinds | Mechanical, but it touches every pointer type in the generated bindings | No |
| 3.3 `undefined` | Low; a rename | No |
| 3.4 Wrapping operators | Low to add, but until they exist, hashing code can't be written under the trapping rule of 2.1 | No |
| 3.5 Iterating by reference | Low to add; loops written meanwhile index by hand | No |
| 3.6 Slicing with ranges | Low to add; slicing calls written meanwhile need rewriting | No |
| 3.7 Labels | Low, because the form is additive | No |
| 3.8 Attributes | Medium. Changing the sigil later touches every annotated declaration, and any keyword added meanwhile breaks identifiers with that name | No |
| 3.9 Tests | Low to medium. A `test` keyword added later would break every identifier named `test` | No |
| 3.10 Destructuring | Low, because the form is additive | No |

### 3.1 Tokens for optionals and errors

**Who has it.**

| Meaning | Swift | Kotlin | Zig | C3 | Rust | Hare | Zen C | Odin |
|---|---|---|---|---|---|---|---|---|
| Optional type | `T?` | `T?` | `?T` | `T?` | `Option<T>` | `nullable *T` | `Option` | `Maybe(T)` ([overview](https://odin-lang.org/docs/overview/#maybet)) |
| Fallible result | `throws` | — | `E!T` | `T?`, carrying a fault | `Result<T, E>` | `(T \| E)` | `Result` | multiple results |
| Propagate | `try` | — | `try` | postfix `!` | postfix `?` | postfix `?` | postfix `?` | `or_return` |
| Value or trap | postfix `!` | `!!` | `.?`, `catch unreachable` | `!!` | `.unwrap()` | postfix `!` | — | `.?` (whether it traps is unverified) |
| Value or default | `??` | `?:` | `orelse`, `catch` | `??` | `.unwrap_or()` | — | `??`, for pointers | `or_else` ([overview](https://odin-lang.org/docs/overview/#or_else-expression)) |
| Optional chaining | `?.` | `?.` | — | — | — | — | `?.`, for pointers | — |

The Kotlin cells for errors are empty because Kotlin uses exceptions. C3 has changed its spelling more than once; for example, 0.7.9 changed the suffix that turns a fault into an empty optional from `?` to `~`, so older C3 posts show other forms (search excerpt of [C3's optional syntax post](https://c3-lang.org/blog/optional-syntax/)). C3's postfix `!` means the opposite of Swift's and Hare's, because it propagates instead of trapping. The Swift, Kotlin and Rust cells come from general knowledge of those languages, not from pages re-read this session (unverified).

**Why it matters for chibi.**
- chibi's types are prefix (`*T`, `[]T`, `[N]T`), so the optional marker belongs in front.
- Prefix `!` already means "not", and a `!` at the start of a line starts a new statement, so `a ⏎ !b` parses as two statements.
- `.*` already dereferences, so a postfix that starts with `.` continues across line breaks, as leading-dot chains do.

**Recommendation.**
- **`?T` and `?*T`, prefix,** as in Zig. Read left to right, `?*T` is an optional pointer and `*?T` is a pointer to an optional. Old Chibi used a suffix `T?`, but its types were not prefix.
- **`E!T` for an error union, with `E` always written.**
- **`try f()` propagates.** A prefix keyword shows at the start of the line where control can leave, and it avoids postfix `?`, which would collide with a `c ? a : b` ternary.
- **`x.?` yields the value or traps,** for both `?T` and `E!T`, as Zig's `.?` does; Odin spells its unwrap `.?` too. It matches `.*`, needs no new line-break rule, and can't be confused with prefix `!`. The owner approved Swift's postfix `!` for old Chibi (session notes, 2026-10-30). A postfix `!` would also work, but it needs one more rule, that it sit on the same line as its operand, because of the line-start rule above.
- **`a ?? b` gives a default** for an optional or an error union. It is a two-byte token, so it fits the lexer table.
- **`catch` handles an error value.** Its exact form is left to the errors sheet, because chibi has no capture syntax like Zig's `|err|`.
- **`if w := maybe { … }` unwraps into a binding.** The parser accepts a declaration in the condition of `if`.
- **`?.` comes later** (section 4), after the ternary sheet decides on `?:`. If both exist, `c ? .red : .blue` would lex `?.` as one token unless spaces decide, as in Swift.

**Own sheet?** In the sheet on errors and optionals.

### 3.2 Pointer kinds

**Recommendation.** Use `?*T` (3.1), `[*]T` for array pointers as in Zig, and a named `cstring` type, as Odin does, rather than Zig's general sentinel syntax `[*:0]u8`. `[*]` is free in today's grammar, since a `*` after `[` in a type currently fails to parse. Adding `?` as a type start is one more case in `parse_composite_type`. Section 2.5 gives the reasons.

### 3.3 `undefined`

**Recommendation.** `x: T = undefined` uses a keyword rather than Odin's and Jai's `---`. A keyword costs nothing in the lexer, because keywords are interned in advance. `---` would be a three-byte operator, which conflicts with F7, and it would overlap any `--` token the `++`/`--` sheet adds.

### 3.4 Wrapping operators

**Recommendation.** Adopt `+%`, `-%` and `*%`, as in Zig. Each is a two-byte operator that fits the lexer's `kind2` table, and none of them collides with valid code today, since `%` can't start an operand. Leave out the compound forms such as `+%=`. They are three-byte operators, which conflict with F7, and wrapping updates are rare enough to spell out, as in `h = h *% PRIME`. Saturating operators (`+|`) can come later.

**Measured 2026-11-10.** chibi-v2's tests were built with clang's `-fsanitize=unsigned-integer-overflow`, with the vendored xxHash excluded. The build reported 16 places where unsigned arithmetic wrapped:
- 10 in the lexer's bitmask code: seven negations such as `x & -x` and `-(u64)flag`, plus carries through `+` and `-`;
- 1 in `arena.h`, where `top - n` wraps before a bounds check;
- 4 in the tests' hash and random-number functions, `splitmix64` and `fnv1a`.

Negation was the most common case. A unary `-%x`, which Zig has, or named bit functions such as `lowest_bit(x)` therefore belong with the binary operators. A run only reports code that the tests reached, so 16 is a lower bound.

### 3.5 Iterating by reference

**Who has it.**
- **Odin** iterates by reference with `for &value in some_array`; otherwise "The iterated values are _copies_ and cannot be written to" ([overview](https://odin-lang.org/docs/overview/#range-based-for-loop)).
- **Zig** uses `for (&items) |*value|` ([for](https://ziglang.org/documentation/0.15.2/#for)).
- **Jai** uses `for *elem: arr` (community source).
- **C3's** `foreach` may take the element "by value or reference" ([features](https://c3-lang.org/faq/allfeatures/)).

**Recommendation.** `for &x in items` gives `x` the type `*T`. It reads like taking an address, and a `&` after `for` is free in today's grammar. Updating every entity in place is the most common loop in data-oriented game code, so the form should exist before that code does.

### 3.6 Slicing with ranges

**Who has it.**
- **Zig** writes `a[start..end]` with an exclusive end, and `a[start..]` ([Slices](https://ziglang.org/documentation/0.15.2/#Slices)).
- **C3** has both `[start..inclusive_end]` and `[start:length]` ([features](https://c3-lang.org/faq/allfeatures/)).
- **Hare** slices "with the `..` operator" ([tutorial](https://harelang.org/tutorials/introduction/#arrays-and-slices-continued)).
- **Go and Odin** write `a[lo:hi]` (unverified).

**Recommendation.** Reuse the settled ranges, as in `a[i..j]`, `a[i..=j]`, `a[i..]` and `a[..j]`. The Slices row of [../decisions.md](../decisions.md) settled this form.

### 3.7 Labels for `break` and `continue`

**Who has it.**
- **Zig** writes `outer: while (true)` and `break :outer` ([Labeled while](https://ziglang.org/documentation/0.15.2/#Labeled-while)).
- **Odin** writes `break loop`, and labeled blocks work with `break` ([overview](https://odin-lang.org/docs/overview/#break-statement)).
- **Zen C** writes `outer: loop { … break outer; }` ([control flow](https://github.com/zenc-lang/docs/blob/60115e029d02e9cea4c9f721da5c82cd7b257de5/reference/05-control-flow.md)).
- **Jai** has no labels. `break i` leaves the loop whose iteration variable is `i`, and a `while` loop names its condition (community source).

**Recommendation.** Use `outer: for x in items { … break outer }`, the Odin, Zig and Zen C form, also for `while`, `switch` and plain blocks. The parser already tests the second token of a statement for `:`, and one more check of the third token separates a label from a declaration such as `x: T`, because a label's third token is `for`, `while`, `switch` or `{`. Jai's loop-variable form needs no new syntax, but it can't name a `while` loop or a block. Labeled blocks that yield a value can come later, together with the ternary sheet. If the `goto` sheet keeps `goto`, its labels and these must not differ.

### 3.8 Attributes

**Who has it.**
- **Odin** writes `@(require_results)`, `@(test)` and `@(private="file")` for declarations, and `#no_bounds_check`, `#force_inline` and `#partial` for directives ([attributes](https://odin-lang.org/docs/overview/#attributes), [directives](https://odin-lang.org/docs/overview/#directives)).
- **C3** writes attributes "in the form `@name(...)`" ([features](https://c3-lang.org/faq/allfeatures/)).
- **Zen C** writes `@inline`, `@packed` and `@align(N)`, and passes them through to C attributes ([advanced](https://github.com/zenc-lang/docs/blob/60115e029d02e9cea4c9f721da5c82cd7b257de5/reference/12-advanced.md)).
- **Hare** marks tests with `@test` ([tutorial](https://harelang.org/tutorials/introduction/#testing-your-code)).
- **Jai** uses `#` directives (`#no_abc`, `#must`, `#c_call`) and `@` notes that metaprograms read (community source).
- **Zig** spells such things as keywords (`export`, `callconv`, `align`, `linksection`) and uses `@` for builtins.

**Recommendation.** Use one form, `@name` or `@name(args)`, before a declaration or a block, for everything the parser doesn't need to know, such as `@test`, `@inline`, `@no_bounds_check`, `@align(16)`, `@link_name("…")`, `@deprecated`. Keywords stay for what changes parsing or visibility (`pub`, `export`, `const`).
- `@` is already a token. `#` is not lexed today, and no table needs it.
- New attributes then never need new keywords. A new keyword would break any program that already uses that name as an identifier.
- If user-defined attributes are allowed, as Jai's notes are, compile-time reflection can read them. Tests and serialization can then be library code.
- If the `sizeof` sheet adopts Zig-style `@sizeof(T)` builtins, position tells the two uses apart, because an attribute comes before a declaration and a builtin is a call inside an expression.

### 3.9 Tests

**Who has it.**
- **Zig** declares `test "name" { … }` ([Zig reference](https://ziglang.org/documentation/0.15.2/)).
- **Zen C** uses the same form ([README](https://github.com/Zuhaitz-dev/zenc-lang/blob/main/README.md)).
- **Odin** marks a procedure `@(test)`. **C3** says "Tests are indicated by `@test`" and they "are usually not compiled" ([features](https://c3-lang.org/faq/allfeatures/)). **Hare** has `@test` functions.
- **Jai** has no test syntax. Tests are procedures tagged with a note such as `@Test`, which a metaprogram gathers and runs (community source).

**Recommendation.** Mark tests as `@test fn parses_hex() { … }`. chibi is its own build system, so the build program can find `@test` functions through reflection, as Jai's metaprogram does. This needs no new keyword. A test also gets an identifier name, which works as a stable ID for caching and for hot reload, can be filtered on, and can be called like any function. Zig's string-named test blocks have none of these properties.

### 3.10 Destructuring and tuple syntax

**Recommendation.** Write `fn divmod(a: int, b: int) -> (int, int)`, with `return q, r`, `q, r := divmod(7, 2)` and `a, b = b, a` (2.12). The statement parser already tests the second token for `:=` and `:`, and destructuring adds `,` to that test. `return` keeps taking its values from its own line.

## 4. Worth adopting later

Each of these can be added later without breaking code that exists by then.

| Feature | Who has it | Why it can wait | What would bring it in |
|---|---|---|---|
| Arrays indexed by an enum, `[Color]f32` | Odin ([overview](https://odin-lang.org/docs/overview/#enumerated-array)) | `[N]T` already parses with any expression as `N`, so only the checker changes | the first table keyed by an enum |
| `bit_set[Enum]` | Odin ([overview](https://odin-lang.org/docs/overview/#bit-sets)) | a library type once compile-time code and enum reflection exist | flag sets in SDL and Cute Framework bindings |
| Struct-of-arrays storage (`#soa`, `MultiArrayList`) | Odin ([overview](https://odin-lang.org/docs/overview/#soa-data-types)), Zig's standard library | a type that compile-time code builds; the comptime sheet owns it, and the owner wants it | the first ECS-style storage |
| `distinct` types | Odin ([overview](https://odin-lang.org/docs/overview/#distinct-types)), Go's defined types, Zig's non-exhaustive enums used as handles | additive, provided `type A = B` means a plain alias now. Settle that now, since C's `typedef`s import as aliases | IDs and units that must not mix |
| Operator overloading | C3 since 0.7.1, Carbon, Jai, Zen C | additive once methods exist; built-in array math covers vectors first | quaternions, fixed-point math, big integers |
| Explicit overload sets | Odin `proc{…}` ([overview](https://odin-lang.org/docs/overview/#explicit-procedure-overloading)) | additive | a family of functions that generics can't express |
| Contracts, `@require` and `@ensure` | C3 ([contracts](https://c3-lang.org/language-common/contracts/)), D (unverified) | attributes; checked in debug and release builds, never assumed | public library APIs |
| Saturating operators `+\|` | Zig, C3 | two-byte tokens | color math, audio mixing |
| Named results | Odin, Go, Jai | additive on tuples | — |
| Blocks that yield a value; `switch` as an expression | Zig ([Blocks](https://ziglang.org/documentation/0.15.2/#Blocks)), Rust (unverified) | additive; tied to the ternary sheet | — |
| Optional chaining `?.` | Swift, Kotlin, Zen C | a two-byte token; see the ternary dependency in 1.3 | long optional chains |
| Raw and multi-line strings | Zig's `\\` lines ([Multiline String Literals](https://ziglang.org/documentation/0.15.2/#Multiline-String-Literals)), Odin's backticks, Jai's `#string` | additive. Zig's form keeps lexing within a line, which suits the 64-byte block lexer; delimited raw strings need state across blocks ([front-end-optimizations.md](front-end-optimizations.md)) | embedded shaders and text |
| Closures with an explicit capture list | Zen C's `[=]` and `[&]`; C++ (unverified) | a second, fat function type | spawning tasks, callbacks with state |
| Struct embedding | Odin's `using` on fields ([overview](https://odin-lang.org/docs/overview/#using-statement)), Go's embedding (unverified) | changes field lookup and reflection, but adds to the language rather than changing it | composing components |
| A deterministic math library | — | a library | lockstep networking |
| Floored division and modulo | Odin `%%` ([overview](https://odin-lang.org/docs/overview/#integer-operators)), Zig `@mod` | library functions | — |
| `assume(cond)` | Zig's "assume" convention ([Doc Comment Guidance](https://ziglang.org/documentation/0.15.2/#Doc-Comment-Guidance)) | opt-in UB, added only where needed | a measured hot spot |

## 5. Considered and rejected

| Feature | Who has it | Why not for chibi |
|---|---|---|
| Exceptions that unwind the stack | C++, Java, C# | Hidden control flow, which Zig's overview names as such ([overview](https://ziglang.org/learn/overview/#small-simple-language)). C callers can't unwind through chibi frames, and a suspended task's frame isn't on any stack an unwinder walks (my analysis) |
| Inferred error sets | Zig | A signature then depends on its body, which serializes checking and makes body edits change types (2.4) |
| Error codes numbered per compilation | Zig | Stored errors can change meaning after a rebuild or reload (2.4) |
| UB on overflow in fast builds | Zig ReleaseFast, Carbon's performance build | Conflicts with a C backend free of UB, and with determinism (2.1) |
| Failed asserts and contracts as optimizer assumptions | Zig's `unreachable`, C3's `assert` and contracts | A wrong assert then becomes UB (2.9) |
| Implicit function overloading | Jai (community source), C++, Swift | `export fn` names must not be mangled, and hot reload identifies a function by its path. Overload resolution adds a search to each call in the checker, and Odin's docs note that implicit overloading complicates scoping |
| `using` statements and `usingnamespace` | Zig removed `usingnamespace` in 0.15; Odin needs `#+feature using-stmt` for the statement form | Zig's notes say it "added distance between the "expected" definition of a declaration and its "actual" definition", was "problematic for tooling", and that "removal of this feature makes Incremental Compilation fundamentally simpler" ([0.15.1 notes](https://ziglang.org/download/0.15.1/release-notes.html#usingnamespace-Removed)) |
| Flow-sensitive narrowing | Kotlin, TypeScript; C3 unwraps after a `catch` that leaves the scope | A dataflow pass in the checker; an explicit binding does the job (2.5) |
| Closures that capture implicitly | Go, Swift, Zen C | Need a collector or reference counting under design A, or they dangle (2.13) |
| A hidden context parameter | Odin, Jai | Costs every call, and needs setup in every C callback (2.7) |
| Unsigned lengths | Zig, Rust, C | Underflow bugs and casts everywhere; C3 reverted after five years (2.2) |
| A pointer-sized `int` | Go, Odin, Swift | Results differ between wasm32 and desktop (2.2) |
| C's integer promotion | C | `u8 + u8` stays a `u8` (2.2) |
| General arbitrary-width integers | Zig's `u3`, C23's `_BitInt`, Zen C's `iN` | Each width adds cases to every backend; widths serve as bitfield storage only |
| FMA contraction or fast-math by default | Go, clang's C | Determinism (2.3) |
| Runes from `for` over a string | Go, Odin | Hidden UTF-8 decoding in every iteration (2.10) |
| String interpolation | Zen C | Needs lexer state across 64-byte blocks and allocates. Checking format strings at compile time does the job ([comptime-generics-reflection.md](comptime-generics-reflection.md), C9) |
| Operators that allocate | Go's string `+` | Hide allocation (2.10) |
| `const` pointers and slices | C, Zig, Rust, Carbon | A daily cost on every signature; `string` covers the main case. This is the one place where the note picks the looser rule on purpose (2.10) |
| A user-visible third pointer kind for C | Zig's `[*c]T`, Swift's implicitly unwrapped optionals, Kotlin's platform types | The generator chooses `?*T` or `?[*]T`, and an override file fixes exceptions (2.5) |
| Unused locals as errors | Zig since 0.9.0, which says it "will never have a "sloppy mode" flag" ([0.9.0 notes](https://ziglang.org/download/0.9.0/release-notes.html), search excerpt); Go (unverified) | Commenting out one line would stop an Edit-and-Continue session; use warnings instead |
| `defer` scoped to the whole function | Go (unverified) | chibi already decided block scope |
| `guard`, `unless` and the pipe `\|>` | Zen C ([control flow](https://github.com/zenc-lang/docs/blob/60115e029d02e9cea4c9f721da5c82cd7b257de5/reference/05-control-flow.md), [operators](https://github.com/zenc-lang/docs/blob/60115e029d02e9cea4c9f721da5c82cd7b257de5/reference/06-operators.md)), Swift's `guard` | Second ways to write `if` and calls |
| Linear types and borrow checking | Austral ("A _linear type_ is a type whose values can only be used once", [spec](https://austral-lang.org/spec/spec.html)), Rust | Checker analysis that costs compile time ([memory-models.md](memory-models.md), section 3) |
| Destructors that run implicitly (RAII) | C++, Zen C's `Drop` | Hidden calls at every scope exit; this depends on the memory model ([memory-models.md](memory-models.md), question 9) |
| Properties: getters that look like fields | D, C#, Swift | Zig's overview names D's `@property` functions as hidden control flow |

## 6. Not verified

**Jai.** Every Jai fact comes from *The Way to Jai* (`~/Personal/jai-refs/The_Way_to_Jai`, commit `19cb4b7a`, 2026-06-11), chapters 05A, 06A, 07A, 09A, 15A, 17A, 18A, 24A, 25A and 37A. Those chapters are the source for zero initialization and `---`, for `float` being `float32` and `int` being `s64`, and for the implicit context and `#c_call`. They are also the source for `break` with a loop variable, `for *`, `#no_abc`, `#must`, the error "Closures are not supported", operator overloading, and tests gathered through notes.

**Zig's version.** Zig facts come from the 0.15.2 reference and the 0.15.1 release notes. Zig 0.17 exists ([front-end-optimizations.md](front-end-optimizations.md) cites 0.17.1), and this note didn't check what changed since 0.15, including whether the parameter-reference plan from issue #5973 has shipped.

**Read only through search excerpts.** C3's rules that signed overflow wraps and that globals are zeroed were read this way, because the "Changes From C" page returned 404 when fetched. The same holds for C3's reasons for zero initialization, its contracts page, its 0.7.9 change to optional syntax, and Zig 0.9.0's errors for unused locals.

**Not re-read this session.** These came from general knowledge of the languages rather than from pages read for this note:
- Swift's shifts, `inout` parameters, the call-site `&` and its optional operators, and Kotlin's `?:` and `!!`.
- Rust's release-mode shifts, evaluation order, default literal types, `debug_assert!`, `CStr` and exhaustive `match`, and its abort on allocation failure.
- Java's evaluation order (JLS §15.7).
- Go's rune iteration, `init` functions, function-scoped `defer`, slice syntax, `context.Context` and struct embedding.
- Odin's default float type, its slice syntax, how it passes large parameters, and whether `.?` traps.
- Zig's evaluation order, whether `==` works on structs, its lack of closures, and `error.OutOfMemory`.
- C3's pointer arithmetic, its `String` and `ZString` types, and its shift and division rules.
- HLSL's `mul`, GLSL's `*` on matrices, D's contracts and unit tests, and C++ capture lists.

**Hardware and operating systems.** This note did not re-read the architecture manuals for three claims. One is that arm64's divide instruction returns 0 for a zero divisor while x86-64's `idiv` faults on a zero divisor and on the most negative value divided by -1. The others are how each CPU masks shift counts, and what a thread-local read costs on macOS and Windows.

**My analysis, not measured.** Four claims are reasoning rather than measurement. Ordered temporaries cost nothing after optimization. An exception unwinder can't see suspended task frames. Zig's error numbering can change the meaning of stored errors across rebuilds. And the context reads in option B of 2.7 are rarer than calls.

**Not found.** This note found no design for Carbon's error handling, no Zen C rules for integer overflow, zero initialization or shadowing, and no Austral rules for arithmetic, which fall in a part of its specification this note didn't reach.

**The owner's earlier directions.** Three come from session notes, not from a repository. They are the postfix `!` approved for old Chibi on 2026-10-30, the thread-local context, and the array-programming direction from 2026-10-20. The old Chibi decisions on overflow, shifts and strings are in `~/Personal/chibi/chibi/docs/language-friction.md`.
