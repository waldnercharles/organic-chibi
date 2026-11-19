# Compile-time code generation, generics and reflection

Research date: 2026-11-09. Primary sources only, except where a row says "third party" or "UNVERIFIED". Code links are pinned to a tag or commit (see [Pinned sources](#pinned-sources)). This note goes deeper than [metaprogramming-reload-coroutines.md](metaprogramming-reload-coroutines.md), called "the earlier note" below, and links to it instead of repeating it.

## Summary

- **Engines.** Zig, Rust, Carbon, Nim 2.4 and Clang's new interpreter run compile-time code on a compiler IR. D, C3 and Clang's old evaluator walk or copy the syntax tree; Clang's tree walker was 6.6× slower than its bytecode on one allocation benchmark (M16).
- **The value representation decides the speed.** Zig pays a big-integer operation plus an intern for every comptime add. Nim rewrote its VM in October 2026 to keep values as bytes in the target's layout.
- **Generic cost is instances × the work each instance redoes.** Zig, Odin, C3, D, Nim and (reportedly) Jai re-check every instance; Odin and C3 copy the syntax tree to do it. Rust, Swift, Go and Carbon check a body once, and Carbon stores only a small table of values per instance.
- **Instance lookup** is one hash probe in Zig, Rust and Carbon, which key instances by interned IDs. Odin, Nim and C3 scan a list, and D hashes string arguments byte by byte.
- **Generated declarations break order-free, parallel checking:** D mixins, Jai, Nim macros and C3's new `$expand` all add names mid-check. Zig forbids them, and Swift makes each macro declare its names before anything expands.
- **Reflection.** Compile-time reflection is free at run time but generates code per type. Runtime tables cost bytes linear in the number of types, and gingerBill argued in September 2026 that they should be the default. One dynamic Go reflection call disables dead-method removal for the whole program.
- **X-macros are cheap.** C++26 reflection turns an enum into strings in 187.7 ms per translation unit against 26.9 ms for an X-macro, mostly because of `#include <meta>` (M17).
- **For chibi-v2:** interpret one typed IR on target-layout bytes; allow values, types, and declarations whose names are declared up front; key instances by interned stable IDs; emit one runtime type table for reached types; charge budgets per call, so analysis order can't change results.

## Contents

1. [Words used in this note](#1-words-used-in-this-note)
2. [Where each feature runs](#2-where-each-feature-runs)
3. [Language by language](#3-language-by-language): [Zig](#31-zig), [Jai](#32-jai-unverified), [Odin](#33-odin), [D](#34-d), [C++](#35-c), [Rust](#36-rust), [Nim](#37-nim), [C3](#38-c3), [Swift](#39-swift), [Go](#310-go), [Carbon](#311-carbon)
4. [Measured numbers](#4-measured-numbers)
5. [Design-space tables](#5-design-space-tables)
6. [`CHIBI_TOKENS` in six languages](#6-chibi_tokens-in-six-languages)
7. [Design options for chibi-v2](#7-design-options-for-chibi-v2)
8. [New testable hypotheses](#8-new-testable-hypotheses)
9. [Questions for the owner](#9-questions-for-the-owner)
10. [Not verified](#10-not-verified)
11. [Code and docs to read first](#11-code-and-docs-to-read-first)

## Pinned sources

| Project | Pin |
|---|---|
| Zig | GitHub tag [`0.15.2`](https://github.com/ziglang/zig/tree/0.15.2) for compiler code. Codeberg tag [`0.17.1`](https://codeberg.org/ziglang/zig/src/tag/0.17.1) for the langref and `std.lang`. Release notes 0.12.0 to 0.17.0. |
| Odin | tag [`dev-2026-11a`](https://github.com/odin-lang/Odin/tree/dev-2026-11a) (`9a69916`) |
| dmd and Phobos | [`v2.114.0`](https://github.com/dlang/dmd/tree/v2.114.0), still the newest tag on 2026-11-09 |
| LLVM/Clang | [`llvmorg-23.1.5`](https://github.com/llvm/llvm-project/tree/llvmorg-23.1.5) |
| GCC/libstdc++ | `releases/gcc-16.2.0` |
| WG21 papers | [P2996R13](https://isocpp.org/files/papers/P2996R13.html), P1306R5, [P3394R4](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3394r4.html), P3491R3, P3096R12, [P3294R2](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p3294r2.html) |
| Rust | [`1.99.1`](https://github.com/rust-lang/rust/tree/1.99.1); serde `v1.0.229`; cargo-llvm-lines `0.4.48` |
| Nim | [`v2.4.0`](https://github.com/nim-lang/Nim/tree/v2.4.0) (2026-10-27) |
| C3 | c3c master [`b2e1e99`](https://github.com/c3lang/c3c/tree/b2e1e99baaf2add3a40bd084823416a4437f3ec6) (2026-11-09, newer than release v0.8.5_1). Docs from c3-web [`ddff474`](https://github.com/c3lang/c3-web/tree/ddff47422f823e7dfd99966c99f5914bceb32c15). |
| Swift | [`swift-6.4.2-RELEASE`](https://github.com/swiftlang/swift/tree/swift-6.4.2-RELEASE); swift-evolution `de9bd49` |
| Go | [`go1.26.0`](https://github.com/golang/go/tree/go1.26.0) and `go1.27.3`; x/tools `v0.52.0`; proposal repo `83ee2aa` |
| Carbon | [`ca189850`](https://github.com/carbon-language/carbon-lang/tree/ca1898506be64134c3a25a5381622667a25cf67f) (2026-11-06) |

**How to read this note.**
- "M7" points to row 7 of the [measured numbers](#4-measured-numbers) table. A number appears there only if its source gives the workload. Claims without measurements stay in the text and are labelled as claims.
- "UNVERIFIED" marks anything I could not confirm in a primary source.
- "Inference" marks my own reading of code or docs.
- The tiny examples are mine, written in each language's documented syntax. None of them was compiled.

---

## 1. Words used in this note

| Term | Plain meaning | Tiny example |
|---|---|---|
| **Compile-time execution ("comptime")** | Running ordinary code while compiling, and putting the result into the program as a constant. | `const T = comptime build_table();` |
| **Code generation** | Compile-time code that produces program parts: types, functions, declarations, or source text that is then compiled. | D: `mixin("int x = 1;");` |
| **Generic** | A function or type written once with a type left open. The compiler fills the open type in for each use. | `fn max(T, a: T, b: T) T` |
| **Instance (instantiation)** | One filled-in copy of a generic, such as `max(i32, ...)`. Creating it is called instantiating. | `List(u8)` is an instance of `List` |
| **Monomorphization** | Making a separate machine-code copy of a generic for every instance. Fast code, but more compile time and bigger binaries. | `max_i32`, `max_f64` |
| **Dictionary passing / witness table** | Compiling one shared copy of a generic and passing a hidden table of functions and sizes that describes the actual type. Smaller code, slower calls. Swift calls the table a *witness table*. | `max(dict_for_i32, a, b)` |
| **Constraint (bound, trait, interface, concept)** | A declared list of what a generic's type must support, such as "has `<`". If the compiler checks the body against it once, it can skip re-checking every instance. | Rust `T: PartialOrd` |
| **Duck typing at compile time** | No declared constraint. Each instance's body is checked with the real type, and fails if an operation is missing. | Zig `anytype` |
| **Reflection** | Code that reads facts about types: field names, field types, sizes, enum names. *Compile-time reflection* reads them while compiling. *Runtime reflection* reads a table stored in the binary. | `@typeInfo(T)`, `type_info_of(T)` |
| **RTTI / type table** | Runtime type information: the table of type descriptions in the binary that runtime reflection reads. | Odin `Type_Info` |
| **Type ID** | A number that names a type. *Stable* means the same type gets the same number in every build. | Odin `typeid` |
| **AST** | Abstract syntax tree: the parser's tree of the source code. | `Add(Name(a), Int(1))` |
| **IR** | Intermediate representation: a simpler, flatter form of the program that the compiler builds after parsing. A *typed IR* has every type resolved. | Zig ZIR (untyped), AIR (typed) |
| **Bytecode / VM** | A compact instruction list, and the interpreter loop (virtual machine) that runs it. | Nim's compile-time VM |
| **JIT** | Just-in-time compilation: turning code into machine code while the program (here, the compiler) runs, then calling it. | none of the studied compilers does this for comptime |
| **Interning** | Storing each distinct value once in a table and referring to it by a small index. Comparing two interned values is one integer compare. | `"if"` → 5 |
| **Memoization** | Caching a function's result by its arguments, so the same call is not computed twice. | `fib(30)` stored after the first call |
| **Hygiene** | A macro's internal names can't clash with names at the place where the macro is used. | Rust `macro_rules!` |
| **String mixin** | Compile-time code builds source text as a string, and the compiler parses it as if it were written there. | D `mixin(s)` |
| **Proc macro / compiler plugin** | A separate compiled program that the compiler calls to rewrite code. | Rust `#[derive(Serialize)]` |
| **Post-monomorphization error** | An error found only when one particular instance is built, not when the generic is checked. | Rust `const { assert!(size_of::<T>() <= 8) }` |
| **COMDAT** | An object-file section that the linker keeps only once, even if many object files contain it. This is how C++ and D drop duplicate template instances. | `inline` functions in C++ |
| **SoA** | Struct of arrays: one array per field instead of one array of structs. | `xs[], ys[]` instead of `points[]` |

---

## 2. Where each feature runs

Each feature can run at a different stage of the compiler. The stage decides what the feature can see (text, names, types) and what its output can still change.

| Stage | What runs there | Who |
|---|---|---|
| Before parsing | External generators write source files | Go `go generate` + `stringer`, Odin's `core:rexcode` table generator, Zig `build.zig` steps |
| After parsing, before names and types | Macros that see only tokens or syntax and return new syntax | Rust proc macros and `macro_rules!`, Swift macros (their arguments are type-checked first), Nim `untyped` macros, C `#define` |
| During type checking | A compile-time interpreter, generic instantiation, compile-time reflection, and text that is parsed and checked on the spot | Zig `Sema`, D CTFE and mixins, Nim `typed` macros and the VM, C3 macros and `$for`, Odin and C++ constant evaluation, Carbon `TryEvalCall` |
| Around type checking | A user program receives a message after each compiler phase and can add code | Jai metaprogram (UNVERIFIED), the 2018 Thekla test-runner post |
| After checking, while lowering | Per-instance code is produced, shared or specialized | Rust mono item collector, Swift SIL specializer, Go shape stenciling, Carbon lowering with coalescing |
| At link | Duplicate instances are dropped by name | C++ and D COMDAT, Go `DUPOK` symbols |

The later a feature runs, the more it knows (types, layouts) and the less it can change (it can no longer add a name that earlier lookups needed).

---

## 3. Language by language

Each language gets the same four parts for each feature: the user's view, the compiler's view, the cost, and real code. A short "For chibi-v2" list ends each language.

### 3.1 Zig

**Shared machinery: the `InternPool`.** All three features run inside one interning table, so it is described first.
- Every type and every comptime value is an *item*: a 1-byte tag plus 32 bits of data. Tags and data are stored as separate arrays, and bigger payloads go into a `u32` array called `extra` ([InternPool.zig L4574-L4578](https://github.com/ziglang/zig/blob/0.15.2/src/InternPool.zig#L4574-L4578), [L4815-L4825](https://github.com/ziglang/zig/blob/0.15.2/src/InternPool.zig#L4815-L4825), [addExtra L10393](https://github.com/ziglang/zig/blob/0.15.2/src/InternPool.zig#L10393)).
- An item's index is a `u32`. The top bits hold the creating thread's ID and the low 30 bits the index in that thread's list ([L4785-L4835](https://github.com/ziglang/zig/blob/0.15.2/src/InternPool.zig#L4785-L4835)).
  - Each thread owns its own lists (`Local`), and only the owner writes them ([L1029-L1078](https://github.com/ziglang/zig/blob/0.15.2/src/InternPool.zig#L1029-L1078)).
  - The hash maps are split into 2^⌈log2(threads)⌉ shards ([L6838-L6842](https://github.com/ziglang/zig/blob/0.15.2/src/InternPool.zig#L6838-L6842)).
  - A lookup hashes the key with Wyhash and probes without a lock. On a hash match it decodes the stored item and compares it with the key. An insert takes the shard's mutex ([L7684-L7785](https://github.com/ziglang/zig/blob/0.15.2/src/InternPool.zig#L7684-L7785)).
- Type equality is one `u32` compare ([Type.zig L116-L122](https://github.com/ziglang/zig/blob/0.15.2/src/Type.zig#L116-L122)).
- **Semantic analysis is still one thread.** AstGen (source to ZIR) runs in parallel, then comes a single-threaded main work queue ([Compilation.zig L4604-L4607](https://github.com/ziglang/zig/blob/0.15.2/src/Compilation.zig#L4604-L4607)). Code generation can run on another thread ([0.14.0 notes](https://ziglang.org/download/0.14.0/release-notes.html#Multithreaded-Backend-Support)).
  - Making `Sema` parallel is a stated goal ([PR #20528](https://github.com/ziglang/zig/pull/20528)).
  - One blocker is open: a generic instance inherits the branch quota (the comptime step limit) of whichever caller is analyzed first. The result then depends on analysis order, which the issue says rules out parallel semantic analysis ([#22410](https://github.com/ziglang/zig/issues/22410), mlugg, 2025-01-05; cited in [Sema.zig L7564-L7567](https://github.com/ziglang/zig/blob/0.15.2/src/Sema.zig#L7564-L7567)).
  - I did not check whether 0.16 or 0.17 changed this. Their release notes talk about parallel backends, not `Sema`.

#### Comptime code generation

**User's view.**
```zig
const crc_table: [256]u32 = blk: {
    @setEvalBranchQuota(4096);
    var t: [256]u32 = undefined;
    for (&t, 0..) |*e, i| {
        var c: u32 = @intCast(i);
        for (0..8) |_| c = if (c & 1 != 0) 0xEDB88320 ^ (c >> 1) else c >> 1;
        e.* = c;
    }
    break :blk t;
};

fn sumFields(comptime T: type, v: T) u64 {
    var s: u64 = 0;
    inline for (@typeInfo(T).@"struct".fields) |f| s += @field(v, f.name);
    return s;
}
```
- `comptime` parameters and blocks run at compile time. `inline for` and `inline while` unroll, giving each iteration compile-time-known values ([langref L2738-L2775](https://github.com/ziglang/zig/blob/0.15.2/doc/langref.html.in#L2738-L2775)).

**Compiler's view.**
- **Where it runs.** AstGen turns `comptime {}` into a `block_comptime` instruction, and inline loops into `block_inline`, `repeat_inline` and `break_inline` ([Zir.zig L306-L350](https://github.com/ziglang/zig/blob/0.15.2/lib/std/zig/Zir.zig#L306-L350)). ZIR is an untyped IR, one per file, cached on disk.
- **What it runs on.** `Sema` walks ZIR and does two jobs with one function, `analyzeBodyInner` ([L1118](https://github.com/ziglang/zig/blob/0.15.2/src/Sema.zig#L1118)):
  - In runtime code it emits AIR, the typed IR that code generation reads.
  - In a comptime block it computes an interned value for each instruction instead.
  - `inline for` re-analyzes the loop body's ZIR once per iteration and emits the unrolled AIR. Each backward jump costs one unit of branch quota ([L1700-L1705](https://github.com/ziglang/zig/blob/0.15.2/src/Sema.zig#L1700-L1705)).
- **Cost of one operation.** A comptime integer add converts both operands to big integers in the `Sema` arena, adds them, and interns the result ([Sema/arith.zig L1383-L1415](https://github.com/ziglang/zig/blob/0.15.2/src/Sema/arith.zig#L1383-L1415)). So each operation is a big-integer operation plus a hash plus a table probe. That is why comptime is slow (earlier note §1.3).
- **Calls.** A call at comptime runs as an inline call inside the caller's `Sema`: it swaps in the callee's ZIR and maps the arguments ([L7482](https://github.com/ziglang/zig/blob/0.15.2/src/Sema.zig#L7482), [L7749-L7790](https://github.com/ziglang/zig/blob/0.15.2/src/Sema.zig#L7749-L7790)). A call whose return type exists only at compile time, such as `type`, is forced to comptime ([L7465-L7478](https://github.com/ziglang/zig/blob/0.15.2/src/Sema.zig#L7465-L7478)).
- **Cache.** The memo key is `memoized_call{func, arg_values}` ([InternPool.zig L2637-L2642](https://github.com/ziglang/zig/blob/0.15.2/src/InternPool.zig#L2637-L2642)).
  - A cache hit still charges the stored branch count to the caller's quota. If the hit would go over the quota, the call is run again so that the error points at the right place ([Sema.zig L7707-L7743](https://github.com/ziglang/zig/blob/0.15.2/src/Sema.zig#L7707-L7743)).
  - Memoization is off in incremental builds (earlier note §1.2).
- **Errors.**
  - `@compileError` fires only if the code that contains it is analyzed. Zig analyzes lazily, so unreferenced code is never checked ([langref L4627-L4637](https://github.com/ziglang/zig/blob/0.15.2/doc/langref.html.in#L4627-L4637)).
  - Every error inside an inline call gets one note per frame: `called at comptime here`, `called inline here`, or `generic function instantiated here` ([Sema.zig L2638-L2648](https://github.com/ziglang/zig/blob/0.15.2/src/Sema.zig#L2638-L2648)).
  - The "referenced by" trace is rebuilt from recorded references only when an error is printed ([Compilation.zig L4353-L4384](https://github.com/ziglang/zig/blob/0.15.2/src/Compilation.zig#L4353-L4384)). It shows 2 frames by default; `-freference-trace` raises that to 256 ([main.zig L1496-L1503](https://github.com/ziglang/zig/blob/0.15.2/src/main.zig#L1496-L1503)).
- **New declarations: none.**
  - `@Type` rejects a struct, enum, union or opaque with any declarations ([Sema.zig L20730-L20850](https://github.com/ziglang/zig/blob/0.15.2/src/Sema.zig#L20730-L20850)).
  - `usingnamespace` was removed in 0.15. The suggested replacement for mixins is a zero-size field plus `@fieldParentPtr` ([0.15.1 notes](https://ziglang.org/download/0.15.1/release-notes.html#usingnamespace-Removed)).
  - So comptime can make values and types, but no names. Every name in a Zig program is visible in its source.

**Cost.**
- Claim in the source: `std.meta.stringToEnum` stops using a comptime-built `StaticStringMap` above 100 fields. Above that it falls back to an unrolled `inline for`, because the map takes too long to compile ([meta.zig L17-L41](https://github.com/ziglang/zig/blob/0.15.2/lib/std/meta.zig#L17-L41)).
- Measured: the `InternPool` rework, M1–M4, and the comptime numbers in the earlier note §1.3.
- `zig build --time-report` reports `Sema` and codegen time per declaration ([0.15.1 notes](https://ziglang.org/download/0.15.1/release-notes.html#Web-Interface-and-Time-Report)).

**Real code.**
- **`std.fmt` checks format strings at compile time.** `Writer.print(comptime fmt, args: anytype)` raises the quota to `fmt.len * 1000`, then walks `fmt` with `inline while` ([Io/Writer.zig L593-L720](https://github.com/ziglang/zig/blob/0.15.2/lib/std/Io/Writer.zig#L593-L720)).
  - It calls `Placeholder.parse` on each `{...}` ([fmt.zig L89-L172](https://github.com/ziglang/zig/blob/0.15.2/lib/std/fmt.zig#L89-L172)).
  - It raises `@compileError` for a missing `{` or `}`, an unknown argument name, too few arguments, or unused arguments (Writer.zig L646-L718).
  - What it emits is straight-line code: a `writeAll` of each literal piece, and one `printValue` call per placeholder.
- **`StaticStringMap.initComptime`** sorts its keys by length at comptime ([static_string_map.zig L66-L100](https://github.com/ziglang/zig/blob/0.15.2/lib/std/static_string_map.zig#L66-L100)).
  - A lookup jumps to the first key of the right length and compares the keys of that length one by one. It does not hash ([L198-L218](https://github.com/ziglang/zig/blob/0.15.2/lib/std/static_string_map.zig#L198-L218)).
  - Zig's own tokenizer uses it for keywords ([tokenizer.zig L10-L63](https://github.com/ziglang/zig/blob/0.15.2/lib/std/zig/tokenizer.zig#L10-L63)). It lexes operators with a switch-based state machine, not with tables ([L395-L421](https://github.com/ziglang/zig/blob/0.15.2/lib/std/zig/tokenizer.zig#L395-L421)).

#### Generics

**User's view.**
```zig
fn Stack(comptime T: type, comptime cap: usize) type {
    return struct {
        items: [cap]T = undefined,
        len: usize = 0,
        const Self = @This();
        pub fn push(self: *Self, x: T) void { self.items[self.len] = x; self.len += 1; }
    };
}

fn max(a: anytype, b: @TypeOf(a)) @TypeOf(a) { return if (a > b) a else b; }

comptime { std.debug.assert(Stack(u8, 4) == Stack(u8, 4)); }
```
- A generic type is a function that returns a type. The langref calls this compile-time duck typing ([L3861-L3870](https://github.com/ziglang/zig/blob/0.15.2/doc/langref.html.in#L3861-L3870)).

**Compiler's view.**
- **Function instances.** A call to a generic function collects one comptime argument per parameter, or "none" for runtime parameters. `getFuncInstance` interns the key: the generic function, those arguments, and the function type ([Sema.zig L7520-L7557](https://github.com/ziglang/zig/blob/0.15.2/src/Sema.zig#L7520-L7557), [InternPool.zig L2853-L2876](https://github.com/ziglang/zig/blob/0.15.2/src/InternPool.zig#L2853-L2876)).
  - Each new instance's body is queued for its own analysis, AIR and machine code ([L7572-L7578](https://github.com/ziglang/zig/blob/0.15.2/src/Sema.zig#L7572-L7578)).
  - So `anytype` makes one instance per distinct argument type. A comptime string parameter, such as a format string, makes one instance per distinct string.
- **Type instances.** Since 0.12, a `struct {}` written inside a function is identified by its source instruction plus the values it captures from the surrounding scope ([0.12.0 notes, "Namespace Type Equivalence"](https://ziglang.org/download/0.12.0/release-notes.html#Namespace-Type-Equivalence)).
  - AstGen records which outer names the struct body uses ([Zir.zig L3390-L3411](https://github.com/ziglang/zig/blob/0.15.2/lib/std/zig/Zir.zig#L3390-L3411)). `Sema` reads their values ([getCaptures L2869-L2880](https://github.com/ziglang/zig/blob/0.15.2/src/Sema.zig#L2869-L2880)) and looks up the key `.declared{zir_index, captures}` ([L2987-L3010](https://github.com/ziglang/zig/blob/0.15.2/src/Sema.zig#L2987-L3010)).
  - Comparing two keys compares the instruction index, then the capture arrays as raw `u32` memory ([InternPool.zig L3115-L3131](https://github.com/ziglang/zig/blob/0.15.2/src/InternPool.zig#L3115-L3131)).
  - So the second `Stack(u8, 4)` finds the same type without memoizing the call. A parameter that the struct body never uses is not part of the key.
- **Types built with `@Type`.** Their key is (the `@Type` instruction, a 64-bit hash of the fields), and two keys are equal when the hashes are equal. The source carries a TODO to use a longer hash ([InternPool.zig L3133-L3137](https://github.com/ziglang/zig/blob/0.15.2/src/InternPool.zig#L3133-L3137), [Sema.zig L21460-L21522](https://github.com/ziglang/zig/blob/0.15.2/src/Sema.zig#L21460-L21522)). The same fields built at two different call sites give two different types.
- **Checking.** Every instance's body is checked separately. An error inside it carries the "generic function instantiated here" note.

**Cost.**
- **"Writergate" (0.15).** The generic `anytype` writer was replaced by a concrete `*std.Io.Writer` that holds its buffer in the interface. The stated reason: the generic interface spread into every struct that held one and forced every function using it to be generic too ([0.15.1 notes](https://ziglang.org/download/0.15.1/release-notes.html#Writergate), [PR #24329](https://github.com/ziglang/zig/pull/24329)). Measured in M6.
- The same notes removed `LinearFifo` as needlessly generic, and warn about generic code bloat ([Ring Buffers](https://ziglang.org/download/0.15.1/release-notes.html#Ring-Buffers)).
- 0.16 resolves a type's fields only when its size or a field's type is needed. Before that, using a type only as a namespace pulled in its fields, which could bloat binaries ([0.16.0 notes, "Lazy Field Analysis"](https://ziglang.org/download/0.16.0/release-notes.html)).

**Real code.**
- **`std.ArrayList(T)`** returns `array_list.Aligned(T, null)`, and `Aligned` turns an explicit natural alignment into `null` ([std.zig L48-L50](https://github.com/ziglang/zig/blob/0.15.2/lib/std/std.zig#L48-L50), [array_list.zig L600-L605](https://github.com/ziglang/zig/blob/0.15.2/lib/std/array_list.zig#L600-L605)). That makes both spellings one type: a hand-written canonical key.
- **`std.enums.EnumArray`, `EnumMap` and `EnumSet`** build on `EnumIndexer` ([enums.zig L1319-L1414](https://github.com/ziglang/zig/blob/0.15.2/lib/std/enums.zig#L1319-L1414)), which sorts the enum's fields at comptime.
  - `EnumArray.init` takes a struct built with `@Type` that has one required field per enum tag ([L34-L50](https://github.com/ziglang/zig/blob/0.15.2/lib/std/enums.zig#L34-L50)).
  - So leaving out a tag is a compile error, the same guarantee the X-macro gives.

#### Reflection

**User's view.**
```zig
fn dump(w: *std.Io.Writer, v: anytype) !void {
    inline for (@typeInfo(@TypeOf(v)).@"struct".fields) |f|
        try w.print("{s}={any} ", .{ f.name, @field(v, f.name) });
}
```
- **0.15.** `@typeInfo(T)` returns a `std.builtin.Type` value whose fields and declarations are in source order. `@Type` is its inverse ([langref L5750-L5800](https://github.com/ziglang/zig/blob/0.15.2/doc/langref.html.in#L5750-L5800)).
- **0.16** replaced `@Type` with `@Int`, `@Tuple`, `@Pointer`, `@Fn`, `@Struct`, `@Union`, `@Enum` and `@EnumLiteral` ([0.16.0 notes](https://ziglang.org/download/0.16.0/release-notes.html), proposal [#10710](https://github.com/ziglang/zig/issues/10710)).
  - None of them takes a declarations parameter.
  - There is no `@Float`, `@Array`, `@Opaque` or `@ErrorSet`, so error sets can no longer be built at comptime.
  - The current signature is `@Enum(TagInt, mode, field_names, field_values)` ([langref 0.17.1 L5902-L5910](https://codeberg.org/ziglang/zig/src/tag/0.17.1/doc/langref.html.in#L5902-L5910)).
- **0.17** moves `std.builtin` to `std.lang`. Struct info becomes struct-of-arrays: `field_names`, `field_types`, `field_attrs` ([0.17.0 notes](https://ziglang.org/download/0.17.0/release-notes.html), [lang.zig L750-L784](https://codeberg.org/ziglang/zig/src/tag/0.17.1/lib/std/lang.zig#L750-L784)).

**Compiler's view.**
- `zirTypeInfo` ([Sema.zig L17052](https://github.com/ziglang/zig/blob/0.15.2/src/Sema.zig#L17052)) builds the `Type` value out of interned aggregates. For a struct it first forces the layout ([L17611](https://github.com/ziglang/zig/blob/0.15.2/src/Sema.zig#L17611)). There is no cache: calling it again rebuilds the value, and the pool deduplicates it back to the same indices.
- **No runtime reflection is built in.**
  - `@tagName` on a value known at compile time becomes a string literal. On a runtime value, the LLVM backend creates one `__zig_tag_name_<T>` function per enum the first time it is needed: a switch that returns string constants ([llvm.zig L4462-L4540](https://github.com/ziglang/zig/blob/0.15.2/src/codegen/llvm.zig#L4462-L4540)).
  - `@errorName` uses one global table of every error name, emitted only if referenced ([L667-L720](https://github.com/ziglang/zig/blob/0.15.2/src/codegen/llvm.zig#L667-L720)).
  - In 0.16, pointers to comptime-only types became runtime types, so a `[]const StructField` can now reach runtime code ([0.16.0 notes](https://ziglang.org/download/0.16.0/release-notes.html)).

**Real code.**
- **`std.MultiArrayList(T)` builds a struct-of-arrays container from `@typeInfo`** ([multi_array_list.zig](https://github.com/ziglang/zig/blob/0.15.2/lib/std/multi_array_list.zig)).
  - It takes the element's fields with `meta.fields(T)`. `meta.FieldEnum(T)` builds an enum of the field names with `@Type` ([meta.zig L488-L530](https://github.com/ziglang/zig/blob/0.15.2/lib/std/meta.zig#L488-L530)).
  - At comptime it computes each field's size and alignment and sorts the fields by alignment, largest first (L169-L201).
  - At run time it makes one allocation of `capacity × Σ field sizes` bytes (L602-L606). `slice()` computes one start pointer per field (L219-L230), and `items(.x)` casts that pointer to the field's type (L83-L94).
- **`std.json`.**
  - `innerParse(comptime T, ...)` switches on `@typeInfo(T)` ([json/static.zig L215-L221](https://github.com/ziglang/zig/blob/0.15.2/lib/std/json/static.zig#L215-L221)).
  - For each key in the input it runs an unrolled `inline for` of `mem.eql(field.name, key)` ([L337-L384](https://github.com/ziglang/zig/blob/0.15.2/lib/std/json/static.zig#L337-L384)). That costs O(fields) string compares per key at run time, and O(fields) code per type.
  - A type can supply its own `jsonParse` instead (L330).

**For chibi-v2.**
- **Type identity.** Identifying a generic type by (declaration, captured values) makes it independent of memoization and call order. For reload, though, the declaration part must survive edits. Zig's ZIR index does not; Zig tracks instructions across updates with a `tracked_inst_map` per shard ([InternPool.zig L1531-L1610](https://github.com/ziglang/zig/blob/0.15.2/src/InternPool.zig#L1531-L1610)).
- **Cost to avoid.** A big-integer operation plus interning on every comptime operation is the cost [H9](../hypotheses.md) exists to avoid.
- **Limits.** Comptime step limits must not depend on which caller is analyzed first (#22410). Otherwise parallel checking changes results.

### 3.2 Jai (UNVERIFIED)

Jai is in closed beta. It has no public documentation, source or transcripts, and YouTube captions could not be fetched. Everything below except video titles and dates rests on community sources, and so is **UNVERIFIED**: the [Jai-Community wiki](https://github.com/Jai-Community/Jai-Community-Library/wiki/Metaprogramming) and *The Way to Jai* at commit [`19cb4b7`](https://github.com/Ivo-Balbaert/The_Way_to_Jai/tree/19cb4b7acb0de2798c769f9ad73313a4d15f4056).

**Primary videos** (titles and dates from YouTube; contents not checked; from Blow's [playlist](https://www.youtube.com/playlist?list=PLmV5I2fxaiCKfxMBrNsU1kgKJXD3PkyxO)):

| Date | Title |
|---|---|
| 2014-10-31 | [Demo: Base language, compile-time execution](https://www.youtube.com/watch?v=UTqZNujQOlA) |
| 2015-01-21 | [Data-Oriented Demo: SOA, composition](https://www.youtube.com/watch?v=ZHqFrNyLlpA) |
| 2015-02-11 | [Demo: Run-Time (and Compile-Time) Type Information](https://www.youtube.com/watch?v=JoNkttD_MUs) |
| 2015-04-01 | Polymorphic Procedures, [part 1](https://www.youtube.com/watch?v=BwqeFrlSpuI) and [part 2](https://www.youtube.com/watch?v=7Fsy2WaxLOY) |
| 2015-10-15 | [Demo: Self-browsing code, compiler message loop, workspaces](https://www.youtube.com/watch?v=OHZwYYW9koI) |
| 2016-03-03 | [Demo: Code Modification](https://www.youtube.com/watch?v=59lKAlb6cRg) |
| 2016-03-17 | [Demo: Structs with Parameters](https://www.youtube.com/watch?v=2IBr0XZOPsk) |
| 2017-07-22 | [Demo: Implicit Polymorphism, Stronger Type_Info, …](https://www.youtube.com/watch?v=MXNkQ5L4pLo) |
| 2019-08-17 | [Compiler Demo: Macros and Iteration](https://www.youtube.com/watch?v=QX46eLqq1ps) |
| 2025-07-09 | [LambdaConf keynote (May 2025)](https://www.youtube.com/watch?v=IdpD5QIVOKQ) |
| 2026-01-24 | [Wookash Podcast](https://www.youtube.com/watch?v=1blhmslxkWg&t=4820), chapters "Metaprogramming & Jai" and "How Metaprogramming Shaped OotSS" |

The one written source from Blow's studio is [Testing the Jai Compiler](http://the-witness.net/news/2018/03/testing-the-jai-compiler/) (Abner Coimbre, Thekla's blog, 2018-03-14). It describes a build program written in Jai that does this:
1. pauses the compile after type checking;
2. finds procedures tagged `@TestProcedure`;
3. adds `register_test` calls to the syntax tree before the bytecode stage runs.

#### Comptime code generation (UNVERIFIED)
```jai
TABLE :: #run build_table();          // runs at compile time; result baked into data
#insert #run make_code_string();      // compile a string returned by compile-time code
```
- `#run f()` reportedly runs in a bytecode interpreter that can call into DLLs ([The Way to Jai 26A L168](https://github.com/Ivo-Balbaert/The_Way_to_Jai/blob/19cb4b7acb0de2798c769f9ad73313a4d15f4056/book/26A_Metaprogramming.md#L168)). Globals it changes are reset in the output unless marked `#no_reset`.
- `#insert` takes a string or a `Code` value. `#code` produces a `Code` value, `compiler_get_nodes` turns it into a syntax tree, and `#expand` marks macros.
- **The metaprogram** ([example 30.11](https://github.com/Ivo-Balbaert/The_Way_to_Jai/blob/19cb4b7acb0de2798c769f9ad73313a4d15f4056/examples/30/30.11_using_notes.jai)):
  1. creates a workspace and calls `compiler_begin_intercept`;
  2. loops on `compiler_wait_for_message()`, receiving `.TYPECHECKED` messages (declarations plus their notes), `.PHASE` and `.COMPLETE`;
  3. at the phase `TYPECHECKED_ALL_WE_CAN`, generates code with `add_build_string`.
- `#placeholder X;` declares a name that a metaprogram will define later ([30.12](https://github.com/Ivo-Balbaert/The_Way_to_Jai/blob/19cb4b7acb0de2798c769f9ad73313a4d15f4056/examples/30/30.12_placeholder.jai)).
- Inference: for this to work, the type checker must park declarations that use unknown names, check "all it can", and only then report those names as errors.

#### Generics (UNVERIFIED)
```jai
max :: (a: $T, b: T) -> T { return ifx a > b then a else b; }
```
- `$T` marks the parameter that defines `T`. There is one instance per combination of type variables ([22A L324](https://github.com/Ivo-Balbaert/The_Way_to_Jai/blob/19cb4b7acb0de2798c769f9ad73313a4d15f4056/book/22A_Polymorphic_procedures.md#L324)).
- `#modify { ... }` runs at compile time before instantiation. It can reject the instance or **rewrite `T`**: for example, mapping every integer type to `s64` makes them all share one instance ([26.16](https://github.com/Ivo-Balbaert/The_Way_to_Jai/blob/19cb4b7acb0de2798c769f9ad73313a4d15f4056/examples/26/26.16_modify1.jai)). That gives the user control over the instance key.
- `#bake_arguments` fixes some arguments at compile time and makes a new procedure.

#### Reflection (UNVERIFIED)
- `Any` is `{type: *Type_Info, value_pointer: *void}`.
- Type info is stored in the binary for the types reached from `type_info()` calls, recursively ([16A L315-L333](https://github.com/Ivo-Balbaert/The_Way_to_Jai/blob/19cb4b7acb0de2798c769f9ad73313a4d15f4056/book/16A_Types_in_depth.md#L315-L333)).
- Opt-outs: `#type_info_none`, `#type_info_procedures_are_void_pointers` and `#type_info_no_size_complaint`. The last implies the compiler warns about table size.

**Cost.** No number could be verified. A polymorph report in the 2025 keynote is described only by an AI summary.

**For chibi-v2.**
- Jai's model, where the build is an ordinary program that receives compiler messages, is the most powerful design studied. It is also the hardest to make parallel and cacheable, because any unresolved name may be waiting on a metaprogram.
- `#modify` shows a cheap way to cap instance counts: let the generic map its arguments to a canonical key before instantiation.

### 3.3 Odin

#### Comptime code generation: none by design

**User's view.** Odin has compile-time constants and conditions, not compile-time execution.
```odin
when ODIN_OS == .Windows { /* only this branch is checked */ }
MAX :: #config(MAX, 64)                     // set with -define:MAX=128
DATA :: #load("table.bin")                  // file bytes as a constant []u8
#assert(len(Token_Kind) <= 256)
```
- `#config(NAME, default)` reads `-define:NAME=v`, which may be an integer, a bool or a string ([check_builtin.cpp L2981-L3010](https://github.com/odin-lang/Odin/blob/dev-2026-11a/src/check_builtin.cpp#L2981-L3010)).
- `#defined` works only inside procedures ([L2947-L2979](https://github.com/odin-lang/Odin/blob/dev-2026-11a/src/check_builtin.cpp#L2947-L2979)). `#exists(path)` gives a constant bool ([L2720-L2742](https://github.com/odin-lang/Odin/blob/dev-2026-11a/src/check_builtin.cpp#L2720-L2742)).
- `#load(path, T)` loads a file as a constant ([L2361-L2410](https://github.com/odin-lang/Odin/blob/dev-2026-11a/src/check_builtin.cpp#L2361-L2410)). There are also `#load_directory` and `#load_hash`.
- `len(EnumType)` is a constant ([L3288-L3297](https://github.com/odin-lang/Odin/blob/dev-2026-11a/src/check_builtin.cpp#L3288-L3297)).
- An enumerated-array literal such as `[Token_Kind]string{...}` must list every enum value unless it is marked `#partial` ([check_expr.cpp L12523-L12560](https://github.com/odin-lang/Odin/blob/dev-2026-11a/src/check_expr.cpp#L12523-L12560)). That gives the same completeness check as an X-macro.
- A `::` constant exists only at compile time and cannot be indexed with a variable. A table that is read at run time must be declared `@(rodata) X := ...` ([FAQ](https://odin-lang.org/docs/faq/#why-can-i-not-indexaddress-a-constant-with-a-variable)).

**Compiler's view.** Constant folding in the checker. There is no interpreter.

**What real Odin code does instead.**
- **Tables built at startup.** Odin's own tokenizer in `core:odin` builds its keyword lookup table at run time. `keyword_lut_init` loops between two sentinel enum members that mark the keyword range ([tokenizer.odin L35-L90](https://github.com/odin-lang/Odin/blob/dev-2026-11a/core/odin/tokenizer/tokenizer.odin#L35-L90), [token.odin L31-L166](https://github.com/odin-lang/Odin/blob/dev-2026-11a/core/odin/tokenizer/token.odin#L31-L166)).
- **A separate generator program.** `core:rexcode` has a generator run with `odin run tablegen`. It writes `.odin` files plus binary blobs, which the library loads with `@(rodata) ENCODE_FORMS := #load("tables/....bin", []Encoding)` ([tablegen/gen.odin L1-L20](https://github.com/odin-lang/Odin/blob/dev-2026-11a/core/rexcode/isa/mips/tablegen/gen.odin#L1-L20), [tables.odin L47-L51](https://github.com/odin-lang/Odin/blob/dev-2026-11a/core/rexcode/isa/mips/tables.odin#L47-L51)).

**gingerBill's reasons, over ten years.** The earlier note §1.2 covers his 2018 and July 2025 statements. Further primary statements:
1. **2016, [The Metaprogramming Dilemma](https://www.gingerbill.org/article/2016/12/01/the-metaprogramming-dilemma/).** Values computed at compile time could hold pointers into the interpreter's memory, which makes compile-time execution fragile. He asks whether metaprogramming belongs in an external tool, as with `go generate`.
2. **2018, [#64 comment](https://github.com/odin-lang/Odin/issues/64#issuecomment-430182251).** The main cost of compile-time execution is in understanding the program, not in implementing it. He calls parametric polymorphism deliberately limited and simple, and needed because Odin has no macros.
3. **2025-01, [X post](https://x.com/TheGingerBill/status/1876237419919573120)** (text read from a search index; the page itself did not load, so UNVERIFIED). Comptime sounds appealing but causes problems for the programmer, while a separate build stage can be debugged like any other program.
4. **2026-09, [CTTI is Exponential, RTTI is Linear](https://www.gingerbill.org/article/2026/09/02/ctti-is-exponential-rtti-is-linear/)** (CTTI is compile-time type information, RTTI runtime type information).
   - A runtime type table costs bytes linear in the number of types, plus one routine that is checked once.
   - Instantiating per type can cost O(N^K) for N types and K type arguments, in checking, code generation and binary size. In his words: "CTTI is an exponential cost everywhere in the worst-case".
   - His instance counts (1,365, 3,906 and 19,531) are computed upper bounds, not measurements. So are his examples of a 6-minute build and a 400 MiB binary.
   - He recommends runtime type information as the default. Instantiation should be kept for hot paths, or for small known sets of types.

#### Generics: parametric polymorphism

**User's view** ([overview](https://odin-lang.org/docs/overview/#parametric-polymorphism)).
```odin
my_new :: proc($T: typeid) -> ^T { return new(T) }
make_array :: proc($N: int, $val: f32) -> (r: [N]f32) { for &x in r { x = val }; return }
cross_2d :: proc(a, b: $T/[2]$E) -> E where intrinsics.type_is_numeric(E) { return a.x*b.y - a.y*b.x }
Table :: struct($Key, $Value: typeid) { keys: [dynamic]Key, values: [dynamic]Value }
```

**Compiler's view.**
- **Where.** Instances are created in the checker, while it checks a call in the caller's body: `check_call_arguments` calls `find_or_generate_polymorphic_procedure` ([check_expr.cpp L7621-L7633](https://github.com/odin-lang/Odin/blob/dev-2026-11a/src/check_expr.cpp#L7621-L7633), [L721-L1139](https://github.com/odin-lang/Odin/blob/dev-2026-11a/src/check_expr.cpp#L721-L1139)).
- **What it runs on.** The syntax tree.
  - The signature's tree is copied and checked with the `$` parameters bound ([L938-L945](https://github.com/odin-lang/Odin/blob/dev-2026-11a/src/check_expr.cpp#L938-L945)).
  - The whole body's tree is copied once for every new instance ([L982-L983](https://github.com/odin-lang/Odin/blob/dev-2026-11a/src/check_expr.cpp#L982-L983)). Memory therefore grows with body size × number of instances.
- **Cache.** Two levels per generic procedure, both linear scans ([checker.hpp L745-L768](https://github.com/odin-lang/Odin/blob/dev-2026-11a/src/checker.hpp#L745-L768)):
  1. `lookups`, keyed by an FNV hash of the call's operands: their types, constant values and modes. It is scanned under a shared lock ([L587-L700](https://github.com/odin-lang/Odin/blob/dev-2026-11a/src/check_expr.cpp#L587-L700), [L845-L876](https://github.com/odin-lang/Odin/blob/dev-2026-11a/src/check_expr.cpp#L845-L876)). A hit replays the dependencies recorded the first time. This level was added in [PR #7886](https://github.com/odin-lang/Odin/pull/7886) (M8).
  2. `procs`, keyed by a hash of the specialized signature, compared with `are_types_identical_unique_tuples` ([L964-L979](https://github.com/odin-lang/Odin/blob/dev-2026-11a/src/check_expr.cpp#L964-L979)).
  - A check that failed is not cached, so every later call reports the same errors again ([L888-L896](https://github.com/odin-lang/Odin/blob/dev-2026-11a/src/check_expr.cpp#L888-L896)).
  - Generic structs have their own cache, `gen_types`. It is scanned linearly with `are_types_identical` and does not hash ([check_type.cpp L623-L668](https://github.com/odin-lang/Odin/blob/dev-2026-11a/src/check_type.cpp#L623-L668)).
- **Parallel checking.** A generic body is never checked by itself ([check_decl.cpp L1700-L1701](https://github.com/odin-lang/Odin/blob/dev-2026-11a/src/check_decl.cpp#L1700-L1701)). Each instance body is queued as a thread-pool task the first time it is used, so instances are checked in parallel with everything else ([checker.cpp L2812-L2826](https://github.com/odin-lang/Odin/blob/dev-2026-11a/src/checker.cpp#L2812-L2826), [PR #7729](https://github.com/odin-lang/Odin/pull/7729)).
- **Errors.**
  - Each instance remembers its parent instance and its call site. An error inside an instance prints lines such as `main.odin(9:7) instantiated as 'add($T=bool)'`, up to 8 levels deep ([checker.cpp L6918-L6980](https://github.com/odin-lang/Odin/blob/dev-2026-11a/src/checker.cpp#L6918-L6980)).
  - A failed `where` clause prints the clause and the bound types, for example `E :: bool` ([check_expr.cpp L7818-L7880](https://github.com/odin-lang/Odin/blob/dev-2026-11a/src/check_expr.cpp#L7818-L7880)).
- **Type queries.** `base:intrinsics` provides 91 `type_*` procedures, such as `type_struct_field_count` ([intrinsics.odin L246](https://github.com/odin-lang/Odin/blob/dev-2026-11a/base/intrinsics/intrinsics.odin#L246)). Instances use them with `when` to pick code by type.

**Cost.** Measured: M8, the October 2026 checker memory work.
- Before that change, the checker allocated a new `^T` type on every use, and pointer types were 63–67% of all types.
- Every `Type` reserved space for every kind of type: 184 bytes. Now each takes a 40-byte header plus its own kind's data.
- Each instance's signature used to be checked twice.

#### `#soa`: a struct-of-arrays type built by the compiler

**User's view** ([overview, SOA](https://odin-lang.org/docs/overview/#soa-data-types), [demo.odin L1824-L1870](https://github.com/odin-lang/Odin/blob/dev-2026-11a/examples/demo/demo.odin#L1824-L1870)).
```odin
Vec3 :: struct { x, y, z: f32 }
v: #soa[1024]Vec3          // three arrays: x[1024], y[1024], z[1024]
v[3].y = 1                 // same as v.y[3]
d: #soa[dynamic]Vec3; append_soa(&d, Vec3{1, 2, 3})
```

**Compiler's view.**
- `make_soa_struct_internal` builds an ordinary struct type ([check_type.cpp L4747-L4900](https://github.com/odin-lang/Odin/blob/dev-2026-11a/src/check_type.cpp#L4747-L4900)):
  - a fixed array gets one `[N]F` field per element field;
  - a slice or dynamic array gets one `[^]F` pointer per field, plus a length, and for dynamic arrays a capacity and an allocator;
  - an element that is a small array, up to 4 values, gets fields named `x`, `y`, `z`, `w`.
- If the element type is not finished yet, `complete_soa_type` fills the fields in later ([L4637-L4736](https://github.com/odin-lang/Odin/blob/dev-2026-11a/src/check_type.cpp#L4637-L4736)).
- `v[i]` becomes a lazy address made of (base, index), so `v[i].x` compiles to `v.x[i]` ([llvm_backend_expr.cpp L5370-L5378](https://github.com/odin-lang/Odin/blob/dev-2026-11a/src/llvm_backend_expr.cpp#L5370-L5378)).
- Growing a dynamic SoA array walks `type_info_of(T)`'s fields at run time ([core_builtin_soa.odin L276-L317](https://github.com/odin-lang/Odin/blob/dev-2026-11a/base/runtime/core_builtin_soa.odin#L276-L317)).
  - Element stores used to walk the same table. They now use stores the compiler lowers ([L449-L456](https://github.com/odin-lang/Odin/blob/dev-2026-11a/base/runtime/core_builtin_soa.odin#L449-L456)).
  - That change made appends up to 11× faster (M9). It shows the run-time price of using reflection where the compiler knows the type.
- **Why it is a language feature.** gingerBill's April 2026 post [Blessed Syntax and Ergonomics](https://www.gingerbill.org/article/2026/04/29/blessed-syntax-and-ergonomics/) argues for built-in containers, `#soa` among them, over operator overloading:
  - indexing alone would need about 7 overloadable operations;
  - the compiler can optimize built-ins;
  - everyone shares one idiom.

  He gives no reason specific to `#soa`.

#### Reflection

**User's view.**
```odin
info := type_info_of(Vec3)                           // ^runtime.Type_Info
for name, i in reflect.struct_field_names(Vec3) { fmt.println(name) }
x: any = 3.5                                          // pointer + typeid
fmt.println(x)                                        // printed through the type table
```

**Compiler's view.**
- **Which types get table entries.** Converting a value to `any`, calling `type_info_of`, or looping over an enum type records that the current declaration depends on the type's info ([check_expr.cpp L1207-L1211](https://github.com/odin-lang/Odin/blob/dev-2026-11a/src/check_expr.cpp#L1207-L1211), [checker.cpp L2546-L2561](https://github.com/odin-lang/Odin/blob/dev-2026-11a/src/checker.cpp#L2546-L2561)).
  - After checking, only declarations the program actually reaches contribute.
  - Their types are added recursively, including fields and element types ([L2899-L2930](https://github.com/odin-lang/Odin/blob/dev-2026-11a/src/checker.cpp#L2899-L2930)).
- **`-no-rtti`** is allowed only on freestanding targets ([build_settings.cpp L1914-L1919](https://github.com/odin-lang/Odin/blob/dev-2026-11a/src/build_settings.cpp#L1914-L1919)).
  - It makes `type_info_of`, `typeid_of`, any use of `any`, and looping over an enum into errors ([check_builtin.cpp L3632-L3640](https://github.com/odin-lang/Odin/blob/dev-2026-11a/src/check_builtin.cpp#L3632-L3640), [checker.cpp L41-L51](https://github.com/odin-lang/Odin/blob/dev-2026-11a/src/checker.cpp#L41-L51), [check_stmt.cpp L1862-L1869](https://github.com/odin-lang/Odin/blob/dev-2026-11a/src/check_stmt.cpp#L1862-L1869)).
  - So `fmt` and `json` stop working. Inference: so does growing a `#soa[dynamic]` array.
- **Type IDs.** A `typeid` is a hash of the type's canonical name. It is stable across builds but does not change when the layout changes (earlier note §3.2).

**Real code.**
- `fmt.println` prints through `_fmt_value(fi, data, id, ...)` ([fmt.odin L3484-L3500](https://github.com/odin-lang/Odin/blob/dev-2026-11a/core/fmt/fmt.odin#L3484-L3500)).
- JSON marshalling switches on `type_info_of(v.id)` and reads struct tags with `reflect.struct_tag_get` ([json/marshal.odin L165-L184](https://github.com/odin-lang/Odin/blob/dev-2026-11a/core/encoding/json/marshal.odin#L165-L184)).
- `reflect.enum_string` scans the enum's values linearly ([reflect.odin L773-L788](https://github.com/odin-lang/Odin/blob/dev-2026-11a/core/reflect/reflect.odin#L773-L788)).

**Cost.** No primary source measures table size. A community forum post reports a 161 KiB `fmt.println` program against 21 KiB without `fmt`, but does not separate the type table from `fmt` itself (M10, third party).

**For chibi-v2.**
- **Without comptime**, tables are built at startup or by a generator program plus `#load`. Both work, but the generator is a second build step, and a table built at startup costs a little at every launch.
- **Per-instance copying of the syntax tree, and linear instance lookup,** are the two Odin choices that break the growth rule. The October 2026 fix (M8) shows the memory they cost.
- **Instances as independent parallel jobs (PR #7729)** fit chibi-v2's plan to claim declarations with atomic marks.

### 3.4 D

#### Comptime code generation: CTFE, string mixins, `static if`, `static foreach`

CTFE means compile-time function execution, D's name for comptime. The earlier note covers the engine: an AST-walking interpreter with a recursion limit.

**User's view.**
```d
string decl(string name) { return "int " ~ name ~ " = 0;"; }   // ordinary function
mixin(decl("count"));                                           // CTFE builds text; compiler parses it
static foreach (i; 0 .. 3) mixin("enum x", i, " = i;");         // declares x0, x1, x2
enum bytes = import("table.bin");                               // file contents; needs -J
```
- An `import("file")` expression yields the file's contents. It works only for files under a path given with `-J`, which the spec calls a security feature ([spec](https://dlang.org/spec/expression.html#import_expressions)).

**Compiler's view.**
- **String mixins.** `expressionsToString` runs semantic analysis and CTFE on each argument and joins the results ([expressionsem.d L1152-L1200](https://github.com/dlang/dmd/blob/v2.114.0/compiler/src/dmd/expressionsem.d#L1152-L1200)).
  - Templates instantiated only to build the string are never emitted as code (L1168-L1170).
  - A fresh parser then runs on the text: `parseDeclDefs` for declarations ([dsymbolsem.d L3892-L3918](https://github.com/dlang/dmd/blob/v2.114.0/compiler/src/dmd/dsymbolsem.d#L3892-L3918)), `parseExpression` for expressions ([expressionsem.d L9522-L9551](https://github.com/dlang/dmd/blob/v2.114.0/compiler/src/dmd/expressionsem.d#L9522-L9551)).
- **When the new names become visible.** When a scope registers its members, a mixin only records its scope ([dsymbolsem.d L6666-L6670](https://github.com/dlang/dmd/blob/v2.114.0/compiler/src/dmd/dsymbolsem.d#L6666-L6670)). The declarations inside it enter the symbol table only when the mixin itself is analyzed ([L3924-L3941](https://github.com/dlang/dmd/blob/v2.114.0/compiler/src/dmd/dsymbolsem.d#L3924-L3941)). The spec says the same: other declarations in the scope cannot refer forward to a mixin's contents ([spec](https://dlang.org/spec/module.html#mixin-declaration)).
- **`static foreach`** at declaration level expands once, lazily, with a guard against cycles. It copies the body once per element and adds the generated members afterwards ([dsymbolsem.d L9422-L9461](https://github.com/dlang/dmd/blob/v2.114.0/compiler/src/dmd/dsymbolsem.d#L9422-L9461)). Neither `static foreach` nor `static if` opens a new scope ([spec](https://dlang.org/spec/version.html#staticforeach)).
- **Errors.** Error locations inside mixins are mapped back to the source. `-mixin=<file>` writes all expanded text to a file, so error lines point into real text ([cli.d L720](https://github.com/dlang/dmd/blob/v2.114.0/compiler/src/dmd/cli.d#L720)).

#### Generics: templates

**User's view.**
```d
auto sum(R)(R r) if (isInputRange!R) { ElementType!R s = 0; foreach (x; r) s += x; return s; }
template isPtr(T) { enum isPtr = is(T == U*, U); }   // "eponymous": the template's value is its member
```
- The spec promises one instance per distinct argument list, even across modules ([spec](https://dlang.org/spec/template.html#common_instantiation)).
- A constraint (`if (...)`) is an ordinary compile-time boolean. `isInputRange` is a chain of `is(typeof(...))` tests, each a trial compile that is allowed to fail ([primitives.d L175-L180](https://github.com/dlang/phobos/blob/v2.114.0/std/range/primitives.d#L175-L180)).

**Compiler's view.**
- **Cache.** Each template declaration owns a hash table of its instances ([dtemplate.d L244](https://github.com/dlang/dmd/blob/v2.114.0/compiler/src/dmd/dtemplate.d#L244)).
  - The key is the enclosing scope plus a hash of the deduced arguments. Equality deep-compares the arguments ([templatesem.d L669-L711](https://github.com/dlang/dmd/blob/v2.114.0/compiler/src/dmd/templatesem.d#L669-L711), [L2410-L2470](https://github.com/dlang/dmd/blob/v2.114.0/compiler/src/dmd/templatesem.d#L2410-L2470)).
  - Value arguments are hashed by content: a string over all its bytes, an array element by element ([L600-L660](https://github.com/dlang/dmd/blob/v2.114.0/compiler/src/dmd/templatesem.d#L600-L660)). So looking up `format!"a long format string"` costs time proportional to the string's length.
  - Template recursion stops at depth 500 ([globals.d L332](https://github.com/dlang/dmd/blob/v2.114.0/compiler/src/dmd/globals.d#L332)).
  - dmd never frees instances. `-lowmem` turns on a collector that trades compile time for memory ([cli.d L637](https://github.com/dlang/dmd/blob/v2.114.0/compiler/src/dmd/cli.d#L637)), and `-vtemplates` prints instance counts.
- **Which object file gets an instance.** `needsCodegen` decides ([templatesem.d L3338-L3489](https://github.com/dlang/dmd/blob/v2.114.0/compiler/src/dmd/templatesem.d#L3338-L3489)).
  - By default it skips emitting an instance that a library module also instantiates, because that module's own compile emits it. `-allinst` emits everything.
  - Emitted instances are COMDAT ([glue/toobj.d L607-L613](https://github.com/dlang/dmd/blob/v2.114.0/compiler/src/dmd/glue/toobj.d#L607-L613)).
  - Only object code is deduplicated. Every dmd run still analyzes every instance it reaches.
- **Errors.**
  - `printInstantiationTrace` prints `instantiated from here` chains, folding recursion ([dtemplate.d L924-L1006](https://github.com/dlang/dmd/blob/v2.114.0/compiler/src/dmd/dtemplate.d#L924-L1006)).
  - Trial compiles such as `__traits(compiles, ...)` suppress ("gag") errors ([globals.d L340-L374](https://github.com/dlang/dmd/blob/v2.114.0/compiler/src/dmd/globals.d#L340-L374)). `-verrors=spec` shows them.
  - An instance that failed while gagged stays in the cache. A later ordinary use analyzes it again without the gag, so the user sees the errors ([templatesem.d L1031-L1036](https://github.com/dlang/dmd/blob/v2.114.0/compiler/src/dmd/templatesem.d#L1031-L1036)).

#### Reflection

**User's view.**
```d
struct Col { string name; }
struct Row { @Col("id") int id; }
static foreach (m; __traits(allMembers, Row))
    pragma(msg, m, " ", getUDAs!(__traits(getMember, Row, m), Col)[0].name);
```
- `__traits(allMembers, T)` returns the member names. The spec leaves their order undefined ([traits](https://dlang.org/spec/traits.html#allMembers)), so a table generated from it may change order between compiler versions.
- User-defined attributes (UDAs, such as `@Col("id")`) are read back with `__traits(getAttributes)` or Phobos `getUDAs` ([traits.d L8625-L8630](https://github.com/dlang/phobos/blob/v2.114.0/std/traits.d#L8625-L8630)).
- Runtime reflection is limited to `typeid` and `TypeInfo`. `-betterC` removes them but keeps all compile-time metaprogramming ([betterc](https://dlang.org/spec/betterc.html#consequences)).

**Real code (Phobos v2.114.0).**
- **`format!"..."(args)`** asserts at compile time that `checkFormatException!(fmt, Args)` is empty ([format/package.d L1384-L1394](https://github.com/dlang/phobos/blob/v2.114.0/std/format/package.d#L1384-L1394), [L735-L748](https://github.com/dlang/phobos/blob/v2.114.0/std/format/package.d#L735-L748)).
  - That template runs the *runtime* `formattedWrite` in CTFE, into a sink that discards output, with default argument values. If it throws, the message becomes the compile error.
  - So the whole formatter runs in the AST interpreter, once per (format string, argument types). Inference: the template cache memoizes it.
- **`std.bitmanip.bitfields`** builds accessor functions as source text with recursive templates; the user `mixin`s the result ([bitmanip.d L77-L311](https://github.com/dlang/phobos/blob/v2.114.0/std/bitmanip.d#L279-L311)).
- **`std.typecons.Tuple`** generates field-alias text with CTFE and `static foreach`, then mixes it in ([typecons.d L579-L649](https://github.com/dlang/phobos/blob/v2.114.0/std/typecons.d#L579-L649)).
- **`std.sumtype.SumType`** generates its union members with `static foreach` + `mixin`. `match` builds a dispatch table with CTFE whose size is the product of the member counts of the sum types it is given ([sumtype.d L293-L302](https://github.com/dlang/phobos/blob/v2.114.0/std/sumtype.d#L293-L302), [L2006-L2060](https://github.com/dlang/phobos/blob/v2.114.0/std/sumtype.d#L2006-L2060)).

**Cost.** Measured: M11–M14.
- Max Haughton, a dmd contributor, traced about 50,800 template instance analyses in one dmd build (M11).
- His advice is to make instances identical where possible: declare types at module scope, and pass function pointers instead of `alias` parameters.
- Teoh's 2018 measurement: one `regex("")` call costs about 2.8 s of compile time (M12).

**For chibi-v2.**
- **D is the precedent for comptime that produces source text.** Its costs: the text is parsed again, every instance is analyzed again in every compiler run, and names from mixins become visible late.
- **Hashing a value argument byte by byte** makes instance lookup cost grow with the value. Interning the value once gives an ID to hash instead.
- **D's formatter shows the reuse argument for comptime.** The same function runs at compile time and at run time, so validation logic is written once. The price is running a whole formatter in a slow interpreter.

### 3.5 C++

#### Generics: templates and concepts

**User's view.**
```cpp
template <std::totally_ordered T> T max(T a, T b) { return a < b ? b : a; }
```

**Compiler's view.**
- **Two-phase lookup.** Names that don't depend on `T` are bound where the template is defined. Names that do are bound at instantiation ([temp.res](https://eel.is/c++draft/temp.res)).
- **Every translation unit instantiates on its own** ([temp.inst](https://eel.is/c++draft/temp.inst)). A translation unit is one `.cpp` file plus its headers.
  - The Itanium ABI puts each instance in a COMDAT group keyed by its mangled name, and the linker keeps one copy ([Itanium ABI §5.2.6](https://itanium-cxx-abi.github.io/cxx-abi/abi.html#vague-itemplate)).
  - `extern template` tells a translation unit that the instance is defined elsewhere ([temp.explicit](https://eel.is/c++draft/temp.explicit)).
- **Clang's cache** is a per-template `FoldingSetVector`, keyed by a profile of the template arguments ([DeclTemplate.h L961](https://github.com/llvm/llvm-project/blob/llvmorg-23.1.5/clang/include/clang/AST/DeclTemplate.h#L961), L2272). It lives only for one translation unit, unless a precompiled header or module carries it.
- **Concepts are checked only at the use site, never at the definition.** Stroustrup calls this a deliberate omission: it eases migration and allows debug code inside templates ([P0557R0 §8.2](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2017/p0557r0.pdf)).
- **Errors.** Clang prints `in instantiation of ... requested here` chains, cut off at 10 entries by default ([DiagnosticOptions.h L93-L95](https://github.com/llvm/llvm-project/blob/llvmorg-23.1.5/clang/include/clang/Basic/DiagnosticOptions.h#L93-L95)).

#### Comptime: `constexpr`, `consteval`

**User's view.**
```cpp
consteval std::array<uint8_t, 256> make_table() { std::array<uint8_t, 256> t{}; /* ... */ return t; }
constexpr auto TABLE = make_table();     // consteval: must run at compile time
```

**Compiler's view.**
- **Limits.**
  - Clang: 1,048,576 evaluation steps (`-fconstexpr-steps`), 512 nested constexpr calls, 1,024 nested template instantiations ([LangOptions.def L384-L390](https://github.com/llvm/llvm-project/blob/llvmorg-23.1.5/clang/include/clang/Basic/LangOptions.def#L384-L390)).
  - GCC: 2^25 operations, 2^18 loop iterations, template depth 900 ([GCC docs](https://gcc.gnu.org/onlinedocs/gcc/C_002b_002b-Dialect-Options.html)).
- **Clang's default evaluator walks the syntax tree** ([ExprConstant.cpp](https://github.com/llvm/llvm-project/blob/llvmorg-23.1.5/clang/lib/AST/ExprConstant.cpp#L792), 23,072 lines).
  - It counts one step per statement ([L997-L1005](https://github.com/llvm/llvm-project/blob/llvmorg-23.1.5/clang/lib/AST/ExprConstant.cpp#L997-L1005)).
  - A constexpr variable's value is cached once per translation unit ([Decl.h L885-L922](https://github.com/llvm/llvm-project/blob/llvmorg-23.1.5/clang/include/clang/AST/Decl.h#L885-L922)).
- **Clang's bytecode interpreter** ([ConstantInterpreter.rst L11-L51](https://github.com/llvm/llvm-project/blob/llvmorg-23.1.5/clang/docs/ConstantInterpreter.rst#L11-L51)):
  - It compiles functions to stack-based, typed bytecode.
  - Top-level expressions are evaluated while they are compiled, because their bytecode would never be reused.
  - It is opt-in through `-fexperimental-new-constant-interpreter`. Since Clang 23, a build option can make it the default.
  - Integers of 16 bits or more take 24 bytes, so that pointer-to-integer casts can round-trip.
  - Measured against the tree walker: M15 and M16.
- **The C++ counterpart of Zig's format check.** `std::format` checks its format string at compile time through a `consteval` constructor on `basic_format_string` ([P2216R3](https://wg21.link/p2216r3), [format.fmt.string](https://eel.is/c++draft/format.fmt.string)).
  - libstdc++ runs each argument's `formatter<T>::parse` inside that constructor ([std/format L5203-L5252](https://github.com/gcc-mirror/gcc/blob/releases/gcc-16.2.0/libstdc%2B%2B-v3/include/std/format#L5203-L5252)). It is the same parser that runs at run time.
  - Inference: every call site is evaluated again, with no memoization.

#### Reflection and code generation: C++26

**Status.** Seven papers were adopted into C++26 at the June 2025 Sofia meeting ([Sutter's trip report](https://herbsutter.com/2025/06/21/trip-report-june-2025-iso-c-standards-meeting-sofia-bulgaria/)):

| Paper | Adds |
|---|---|
| P2996 | reflection |
| P3394 | annotations |
| P3293 | splicing bases |
| P3491 | `define_static_*` |
| P1306 | `template for` |
| P3096 | parameter reflection |
| P3560 | error handling |

Implementations exist in GCC 16 (`-std=c++26 -freflection`, [changes](https://gcc.gnu.org/gcc-16/changes.html)), Bloomberg's clang fork, and EDG ([P2996 §2.3](https://isocpp.org/files/papers/P2996R13.html#implementation-status)).

**User's view.**
```cpp
enum class Color { Red, Green };
template <typename E> constexpr std::string_view name(E e) {
    template for (constexpr auto r : std::define_static_array(std::meta::enumerators_of(^^E)))
        if (e == [:r:]) return std::meta::identifier_of(r);
    return "?";
}
```
- `^^E` gives a reflection: an opaque compile-time value of type `std::meta::info`.
- Functions such as `members_of`, `nonstatic_data_members_of`, `enumerators_of` (in declaration order) and `identifier_of` query it ([meta.syn](https://eel.is/c++draft/meta.syn)).
- `[: r :]` "splices" a reflection back into code: it turns the reflection into the entity it describes.
- `template for` (P1306) stamps out a copy of its body for each element, like an instantiation.
- Annotations `[[=value]]` attach compile-time values to declarations, including enumerators ([P3394R4](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3394r4.html)).

**How it generates code.**
- `define_aggregate(class, members)` only completes a class or union that has been declared but not defined ([meta.reflection.define.aggregate](https://eel.is/c++draft/meta.reflection.define.aggregate)).
- There is no function that defines an enum or adds enumerators, so **C++26 cannot build an enum from a table**.
- A declaration injected this way becomes visible immediately after the constant expression that produced it. Injecting across an instantiation boundary is ill-formed (P2996 §4.4.7–4.4.8).
- General code injection from token sequences ([P3294](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p3294r2.html)) targets C++29.
- Barry Revzin, a P2996 author, writes that he dislikes how C++26 generates types ([SoA post, 2025](https://brevzin.github.io/c++/2025/05/02/soa/)).

**Real code.** P2996's own examples:
- [enum to string (§3.6)](https://isocpp.org/files/papers/P2996R13.html#enum-to-string);
- [struct of arrays with `define_aggregate` (§3.10)](https://isocpp.org/files/papers/P2996R13.html#struct-to-struct-of-arrays);
- [command-line parsing (§3.7)](https://isocpp.org/files/papers/P2996R13.html#parsing-command-line-options);
- [a universal formatter (§3.12)](https://isocpp.org/files/papers/P2996R13.html#a-universal-formatter).

**Cost.** M17 is the most direct comparison with chibi-v2's X-macros. Turning an enum into strings costs, per translation unit:

| Variant | Time |
|---|---|
| X-macro | 26.9 ms |
| reflection | 187.7 ms |
| reflection with a precompiled header | 81.0 ms |

Including `<meta>` alone adds about 155 ms. After that, each enumerator costs 0.07 ms with reflection against 0.027 ms with the X-macro.

**For chibi-v2.**
- **Re-doing every instance in every translation unit** is a cost that a whole-program compiler like chibi-v2 avoids by design.
- **In M17 most of reflection's cost is the library** that implements it, not the reflection itself. chibi-v2's reflection should come from the compiler's own type table, not from a large library parsed in every build.
- **`define_aggregate` can only complete a class that was declared beforehand.** That is C++26's version of "declare the name first, generate the contents later" (see Swift).

### 3.6 Rust

#### Generics: checked once, monomorphized

**User's view.**
```rust
fn max<T: PartialOrd>(a: T, b: T) -> T { if a > b { a } else { b } }
fn small<T>() { const { assert!(std::mem::size_of::<T>() <= 8) } }   // can fail only per instance
```

**Compiler's view.**
- **Checked once.** A generic body is type-checked and borrow-checked once, against its bounds ([reference](https://doc.rust-lang.org/reference/trait-bounds.html)).
- **Instance key.** An `Instance` is a kind plus an interned list of generic arguments, so hashing and comparing it is cheap ([instance.rs L23-L36](https://github.com/rust-lang/rust/blob/1.99.1/compiler/rustc_middle/src/ty/instance.rs#L23-L36)). No per-instance copy of the MIR (rustc's typed IR) is ever made. Code generation and const evaluation substitute the arguments as they go.
- **The mono item collector** builds a graph of everything that needs machine code ([collector.rs L1-L125](https://github.com/rust-lang/rust/blob/1.99.1/compiler/rustc_monomorphize/src/collector.rs#L1-L125)).
  - Its roots are the public non-generic items. It then walks each item's MIR for calls, function references and drop glue (the generated cleanup code).
  - A cast to `dyn Trait` instantiates every method of that trait for the vtable.
  - Roots are walked in parallel. Shared state is a few locked sets ([L249-L258](https://github.com/rust-lang/rust/blob/1.99.1/compiler/rustc_monomorphize/src/collector.rs#L249-L258)).
  - **The result is sorted by a stable hash,** because the order of parallel discovery is not deterministic ([L1914-L1957](https://github.com/rust-lang/rust/blob/1.99.1/compiler/rustc_monomorphize/src/collector.rs#L1914-L1957)).
  - Constants are evaluated even inside dead code. Otherwise an optimized build could succeed where a debug build fails ([L156-L195](https://github.com/rust-lang/rust/blob/1.99.1/compiler/rustc_monomorphize/src/collector.rs#L156-L195)).
- **Errors found only in an instance** come from const evaluation or from the recursion limit.
  - The collector compares the error count before and after each item. If it grew, it adds `the above error was encountered while instantiating fn ...` ([L545-L567](https://github.com/rust-lang/rust/blob/1.99.1/compiler/rustc_monomorphize/src/collector.rs#L545-L567)).
  - There is no full chain of calls. The source calls this a minimal first version ([L393-L416](https://github.com/rust-lang/rust/blob/1.99.1/compiler/rustc_monomorphize/src/collector.rs#L393-L416)).
- **Codegen units.** Each source module gets two codegen units: one for stable non-generic code and one for "volatile" instances. Adding a call to a generic can create a new instance, and keeping instances apart means that doesn't invalidate the stable code ([partitioning.rs L1-L71](https://github.com/rust-lang/rust/blob/1.99.1/compiler/rustc_monomorphize/src/partitioning.rs#L1-L71)).
- **Across crates.** `upstream_monomorphizations` maps (definition, interned arguments) to the crate that already exports that instance ([queries.rs L1933-L1960](https://github.com/rust-lang/rust/blob/1.99.1/compiler/rustc_middle/src/queries.rs#L1933-L1960)).
  - Coherence (the orphan rule) allows at most one impl per (trait, types) in the whole crate graph ([reference](https://doc.rust-lang.org/reference/items/implementations.html)).
  - Inference: that is what makes an instance mean the same function in every crate, so it can be shared.

**Cost.** Measured: M18–M24.
- **Generic shell, non-generic core.** `RawVec::grow` was instantiated 108 times in webrender. It was split into a small generic wrapper around a non-generic core. That cut LLVM IR by up to 15% and made debug builds up to 5.7% faster (M18).
- **`cargo-llvm-lines`** ([README](https://github.com/dtolnay/cargo-llvm-lines/blob/0.4.48/README.md)) counts LLVM IR lines and copies per generic function. It charges each instance to the crate that uses it, not the crate that defines it.

#### Comptime: `const fn`

**User's view.**
```rust
const fn crc(i: u32) -> u32 { let mut c = i; let mut k = 0; while k < 8 { c = if c & 1 != 0 { 0xEDB88320 ^ (c >> 1) } else { c >> 1 }; k += 1; } c }
const TABLE: [u32; 256] = { let mut t = [0; 256]; let mut i = 0; while i < 256 { t[i] = crc(i as u32); i += 1; } t };
```

**Compiler's view.**
- The engine is rustc's MIR interpreter (earlier note §1.1). Allowed: arithmetic, loops, branches, calls to `const fn`, and limited borrows ([reference](https://doc.rust-lang.org/reference/const_eval.html)).
- Still unstable:
  - heap allocation at compile time (`const_heap`) ([intrinsics/mod.rs L2811-L2858](https://github.com/rust-lang/rust/blob/1.99.1/library/core/src/intrinsics/mod.rs#L2811-L2858));
  - trait methods in const code ([unstable.rs L481](https://github.com/rust-lang/rust/blob/1.99.1/compiler/rustc_feature/src/unstable.rs#L481));
  - const generics over expressions (`generic_const_exprs`).
- Integer const generics (`[T; N]`) have been stable since 1.51 ([accepted.rs L314](https://github.com/rust-lang/rust/blob/1.99.1/compiler/rustc_feature/src/accepted.rs#L314)).

#### Code generation: proc macros

**User's view.**
```rust
#[derive(Serialize)] struct Point { x: i32, y: i32 }
```

**Compiler's view.**
- **What a proc macro is.** A proc macro crate is compiled to a dynamic library, which rustc loads into its own process with `dlsym` ([host_dylib.rs L126-L147](https://github.com/rust-lang/rust/blob/1.99.1/compiler/rustc_metadata/src/host_dylib.rs#L126-L147)).
  - Calls go through a "bridge" that passes tokens in C-ABI buffers with integer handles, so a macro built by one compiler version can talk to another ([bridge/mod.rs L1-L7](https://github.com/rust-lang/rust/blob/1.99.1/library/proc_macro/src/bridge/mod.rs#L1-L7)).
  - The macro sees tokens only: no names, no types ([Nethercote, 2025](https://nnethercote.github.io/2025/08/16/speed-wins-when-fuzzing-rust-code-with-derive-arbitrary.html)).
  - It can do anything the compiler process can, including file I/O. A panic becomes a compile error; an infinite loop hangs rustc ([reference](https://doc.rust-lang.org/reference/procedural-macros.html)).
- **When its output becomes visible.** Expansion is a loop that runs until nothing changes ([dev guide](https://rustc-dev-guide.rust-lang.org/macro-expansion.html)). Each pass:
  1. resolves imports;
  2. finds macro calls whose macro path can now be resolved;
  3. expands them, parses the output, and gives the new items IDs;
  4. queues any macro calls inside the output.

  Only macro paths are resolved during this loop; all other names and all types come later. If the loop stops making progress, the remaining calls become errors.
- **`macro_rules!`** macros are hygienic, matched by pattern, and usable only after their definition in the text ([reference](https://doc.rust-lang.org/reference/macros-by-example.html)). Proc macros are not hygienic.
- **Errors.** A macro reports errors by panicking or by emitting `compile_error!`. Spans (source positions) carried on output tokens let later errors point at the user's code. serde uses `quote_spanned!`, so a field type without `Serialize` is reported at that field ([ser.rs L684](https://github.com/serde-rs/serde/blob/v1.0.229/serde_derive/src/ser.rs#L684)).
- **Caching and determinism.**
  - Caching derive output exists only behind `-Zcache-proc-macros`, which is labelled potentially unsound ([options.rs L2402-L2403](https://github.com/rust-lang/rust/blob/1.99.1/compiler/rustc_session/src/options.rs#L2402-L2403)).
  - The 2025 compiler performance survey names uncached derive expansion as one cause of slow incremental builds ([Rust blog](https://blog.rust-lang.org/2025/09/10/rust-compiler-performance-survey-2025-results/)).
  - dtolnay proposed compiling proc macros to sandboxed WebAssembly for determinism and isolation ([pre-RFC](https://internals.rust-lang.org/t/pre-rfc-sandboxed-deterministic-reproducible-efficient-wasm-compilation-of-proc-macros/19359)).

**Real code.**
- **serde's derive** emits an `impl Serialize` whose `serialize` is itself generic over the serializer ([ser.rs L13-L64](https://github.com/serde-rs/serde/blob/v1.0.229/serde_derive/src/ser.rs#L13-L64)).
  - A struct becomes `serialize_struct`, one `serialize_field` per field, then `end` ([L328-L366](https://github.com/serde-rs/serde/blob/v1.0.229/serde_derive/src/ser.rs#L328-L366)). Deserialize generates a visitor type ([de/struct_.rs L88-L183](https://github.com/serde-rs/serde/blob/v1.0.229/serde_derive/src/de/struct_.rs#L88-L183)).
  - So the cost comes twice: the macro generates code per type, then monomorphization copies it per (type, serializer) pair.
- **`format_args!`** is built into rustc.
  - It parses the format string during expansion. Errors point inside the string literal ([format.rs L275-L308](https://github.com/rust-lang/rust/blob/1.99.1/compiler/rustc_builtin_macros/src/format.rs#L275-L308)).
  - It expands to a dedicated syntax node that is lowered later ([L1130-L1154](https://github.com/rust-lang/rust/blob/1.99.1/compiler/rustc_builtin_macros/src/format.rs#L1130-L1154)). The language's most common compile-time check is a compiler built-in, not a user macro.
- **`#[derive(Debug)]`** uses a fixed helper for up to 5 fields and a slice-based helper above that, to keep generated code small ([debug.rs L92-L121](https://github.com/rust-lang/rust/blob/1.99.1/compiler/rustc_builtin_macros/src/deriving/debug.rs#L92-L121)).

**Cost.** Measured: M21–M24.
- `-Zmacro-stats` shows that `#[derive(Deserialize)]` on a two-field struct expands to 159 lines (M21).
- Bevy's `derive(Reflect)` turned 16k source lines into 27k generated lines (M23).

#### Reflection

- **`TypeId`** is a 128-bit hash. Its docs say the hash may change between Rust releases ([any.rs L604-L624](https://github.com/rust-lang/rust/blob/1.99.1/library/core/src/any.rs#L604-L624)).
  - The hash covers each definition's crate ID, which mixes in the crate name, the `-Cmetadata` flags and the compiler version ([def_id.rs L144-L204](https://github.com/rust-lang/rust/blob/1.99.1/compiler/rustc_span/src/def_id.rs#L144-L204)).
  - `type_name` is for diagnostics only, and its format is unspecified ([L877-L892](https://github.com/rust-lang/rust/blob/1.99.1/library/core/src/any.rs#L877-L892)).
- **Compile-time reflection is in progress** under Oli Scherer's [2026 project goal](https://rust-lang.github.io/rust-project-goals/2026/reflection-and-comptime.html).
  - The unstable `core::mem::type_info` describes a type's kind and its fields as name, `TypeId` and offset ([type_info.rs](https://github.com/rust-lang/rust/blob/1.99.1/library/core/src/mem/type_info.rs#L1-L120)).
  - Its entry point is marked `#[rustc_comptime]`: it can only be called at compile time.
  - The tracking issue lists an open question: reflection lets authors write errors that appear only in particular instances ([#146922](https://github.com/rust-lang/rust/issues/146922)).

**For chibi-v2.**
- **Two collector ideas transfer directly:**
  - discover instances in parallel, then sort them by a stable key so output is deterministic;
  - keep instance code separate from a module's own code, so a new instance doesn't invalidate code that didn't change.
- **The "generic shell, non-generic core" split** (M18) is a habit the language can encourage, and a compiler warning could point at candidates.
- **Token-level macros** cost compile time in two places: expansion, then checking the larger output. They also can't be cached safely unless they are pure.

### 3.7 Nim

#### Comptime code generation: macros, templates, the VM

**User's view.**
```nim
import std/macros
macro sumTo(n: static int): untyped =        # runs in the compile-time VM, returns syntax
  result = newLit(0)
  for i in 1..n: result = infix(result, "+", newLit(i))
template twice(body: untyped) = body; body   # substitution with hygiene
const table = static(computeTable())         # any procedure the VM can run
const version = staticExec("git describe")   # runs a process at compile time
```
- An `untyped` macro parameter skips name lookup and typing; a `typed` one is checked first ([manual L6371-L6392](https://github.com/nim-lang/Nim/blob/v2.4.0/doc/manual.md#L6371-L6392)).
- A `static` parameter makes each distinct value a separate instance ([L6973-L6993](https://github.com/nim-lang/Nim/blob/v2.4.0/doc/manual.md#L6973-L6993)).
- `staticRead` embeds a file. `staticExec` runs a process, optionally caching its output in nimcache keyed by the command and its input ([compilation.nim L166-L205](https://github.com/nim-lang/Nim/blob/v2.4.0/lib/system/compilation.nim#L166-L205)).
- C calls at compile time need a compiler built with libffi plus `--experimental:compiletimeFFI` ([vmgen.nim L844-L870](https://github.com/nim-lang/Nim/blob/v2.4.0/compiler/vmgen.nim#L844-L870)).

**Compiler's view.**
- **Where.** `semMacroExpr` runs the macro, then `semAfterMacroCall` type-checks the returned tree like hand-written code ([sem.nim L547-L575](https://github.com/nim-lang/Nim/blob/v2.4.0/compiler/sem.nim#L547-L575), [L632-L660](https://github.com/nim-lang/Nim/blob/v2.4.0/compiler/sem.nim#L632-L660)). The manual notes that this checks the region at least twice ([L6693-L6716](https://github.com/nim-lang/Nim/blob/v2.4.0/doc/manual.md#L6693-L6716)).
- **When new names become visible.** Declarations a macro generates exist only after its expansion point. Nim checks declarations in order unless `{.experimental: "codeReordering".}` is on ([experimental L283-L290](https://github.com/nim-lang/Nim/blob/v2.4.0/doc/manual_experimental.md#L283-L290)).
- **The VM since 2.4.** [PR #26323](https://github.com/nim-lang/Nim/pull/26323) (Araq, merged 2026-10-06) replaced the old VM, which stored values as compiler syntax nodes.
  - Values now sit in memory in the same layout as compiled programs.
  - Registers are 8-byte frame slots. Instructions are 64-bit words with a 16-bit opcode, and opcodes encode operand size and kind ([vmdef.nim L10-L76](https://github.com/nim-lang/Nim/blob/v2.4.0/compiler/vmdef.nim#L10-L76)).
  - The compiler's destructor-injection pass drives allocation and freeing; cycles leak.
  - Syntax nodes (`NimNode`) are reference-counted handles to compiler nodes ([vm.nim L2472-L2501](https://github.com/nim-lang/Nim/blob/v2.4.0/compiler/vm.nim#L2472-L2501)).
- **Cache.**
  - Bytecode is generated lazily per procedure and cached by symbol ID ([vmgen.nim L4037-L4141](https://github.com/nim-lang/Nim/blob/v2.4.0/compiler/vmgen.nim#L4037-L4141)).
  - Macro expansions are not memoized: every call runs again.
  - Selected library procedures (math, OS, times) run as native callbacks ([vmops.nim L49-L70](https://github.com/nim-lang/Nim/blob/v2.4.0/compiler/vmops.nim#L49-L70)).
- **Limits.** 10,000,000 backward jumps, counted per evaluation, and call depth 2,000 ([options.nim L761-L762](https://github.com/nim-lang/Nim/blob/v2.4.0/compiler/options.nim#L761-L762), [vm.nim L677-L685](https://github.com/nim-lang/Nim/blob/v2.4.0/compiler/vm.nim#L677-L685)). The error prints a VM stack trace.
- **Errors** carry `template/generic instantiation of X from here` traces ([msgs.nim L500-L512](https://github.com/nim-lang/Nim/blob/v2.4.0/compiler/msgs.nim#L500-L512)).

**Real code.**
- **`std/strformat`'s `fmt`** parses its format string at compile time. It parses each `{expr}` as Nim code and emits `formatValue` calls into a pre-sized string ([strformat.nim L645-L780](https://github.com/nim-lang/Nim/blob/v2.4.0/lib/pure/strformat.nim#L645-L780)).
- **`std/json`'s `%*`** walks an untyped tree ([json.nim L416-L445](https://github.com/nim-lang/Nim/blob/v2.4.0/lib/pure/json.nim#L416-L445)).

#### Generics and concepts

**User's view.**
```nim
proc max2[T](a, b: T): T = (if a < b: b else: a)
```

**Compiler's view.**
- **Partly checked at the definition.** A first pass over the generic binds some symbols at its definition. Overloaded names stay open until instantiation, controlled by `mixin` and `bind` ([semgnrc.nim L10-L16](https://github.com/nim-lang/Nim/blob/v2.4.0/compiler/semgnrc.nim#L10-L16), [manual L6207-L6260](https://github.com/nim-lang/Nim/blob/v2.4.0/doc/manual.md#L6207-L6260)). The body is type-checked per instance.
- **Cache.** `procInstCache` maps the generic's ID to a list of its instances ([modulegraphs.nim L63-L64](https://github.com/nim-lang/Nim/blob/v2.4.0/compiler/modulegraphs.nim#L63-L64)).
  - A lookup scans that list, comparing types structurally ([seminst.nim L78-L94](https://github.com/nim-lang/Nim/blob/v2.4.0/compiler/seminst.nim#L78-L94)).
  - Nesting deeper than 50 instantiations is an error.
- **Concepts.**
  - Old-style concepts match a type if a block of example expressions compiles for it ([experimental L899-L957](https://github.com/nim-lang/Nim/blob/v2.4.0/doc/manual_experimental.md#L899-L957)).
  - New-style concepts list signatures over `Self`. They are used only for overload resolution; checking a generic body against them is described as forthcoming ([manual L3108-L3156](https://github.com/nim-lang/Nim/blob/v2.4.0/doc/manual.md#L3108-L3156)).

#### Reflection
- **Compile time.** `getTypeImpl` and friends return syntax trees ([macros.nim](https://github.com/nim-lang/Nim/blob/v2.4.0/lib/core/macros.nim#L339)). `fieldPairs` unrolls a `for` loop over fields ([iterators.nim L309-L336](https://github.com/nim-lang/Nim/blob/v2.4.0/lib/system/iterators.nim#L309-L336)).
- **Enum names.** The compiler generates a `$` procedure for every enum: a `case` that returns string literals ([enumtostr.nim L7-L40](https://github.com/nim-lang/Nim/blob/v2.4.0/compiler/enumtostr.nim#L7-L40)).
- **Run time.** Runtime type info under ARC/ORC holds only what memory management and `of` checks need. It has no field names, and type names only with `-d:nimTypeNames` ([system.nim L1714-L1733](https://github.com/nim-lang/Nim/blob/v2.4.0/lib/system.nim#L1714-L1733)).

**Cost.** Measured: M33–M37.
- In Araq's own profile, routine-body checking is 63% of the compiler's front-end time (M34).
- Materializing large compile-time arrays in the new VM raised one project's front-end time from 71.5 s to 83 s, until a fix brought it back to 72.5 s (M35).
- In Nim 2.2, a linear constant-deduplication scan in the VM took 11.6% of CPU time on a large project (M36).

**For chibi-v2.**
- **Nim 2.4 is the newest precedent for H9's design:** an interpreter over memory in the target's layout, replacing an interpreter over compiler nodes.
- **The rewrite also shows that large comptime values are a memory and time risk.** M35 needed a follow-up fix.

### 3.8 C3

#### Comptime code generation: macros and `$` code

**User's view.**
```c3
macro void @swap(#a, #b) { var t = #a; #a = #b; #b = t; }   // #x: an unevaluated expression
macro long @fib(long $n)                                     // $x: a compile-time value
{
    $if $n <= 1: return $n; $else return @fib($n - 1) + @fib($n - 2); $endif
}
```
- `$x` is a compile-time value or type, and `#x` is an expression passed unevaluated. Macros that take `#` parameters must have names starting with `@` ([macros.md L209-L219](https://github.com/c3lang/c3-web/blob/ddff47422f823e7dfd99966c99f5914bceb32c15/docs/generic-programming/macros.md#L209-L219)).
- Compile-time variables exist only inside function and macro bodies ([compiletime.md L9-L16](https://github.com/c3lang/c3-web/blob/ddff47422f823e7dfd99966c99f5914bceb32c15/docs/generic-programming/compiletime.md#L117-L168)).
- An `@const` macro that folds to a constant can fill a global constant table. A test builds `const TrieNode[*] TRIE = build_trie(...)` this way ([ct_var_const_fold.c3t](https://github.com/c3lang/c3c/blob/b2e1e99baaf2add3a40bd084823416a4437f3ec6/test/test_suite/compile_time/ct_var_const_fold.c3t)).
- **Top level.** The docs say only `@feat`, `@if` and `$assert` are allowed at top level, so that declarations don't appear out of nowhere ([macros.md L197-L207](https://github.com/c3lang/c3-web/blob/ddff47422f823e7dfd99966c99f5914bceb32c15/docs/generic-programming/macros.md#L197-L207)).
  - But 0.8.0 (2026-05-14) added `$expand(string)`, which compiles any compile-time string as code, top-level functions included.
  - Lernö calls it inherently hostile to language servers and IDEs ([0.8.0 post L226-L244](https://github.com/c3lang/c3-web/blob/ddff47422f823e7dfd99966c99f5914bceb32c15/docs/blog/posts/c3_0_8_0_the_core_language_is_settling.md#L226-L244)).
- **`$exec(cmd, args, stdin)`** pastes a program's output into the source. It can also compile and run `.c3` scripts, and it requires `--trust=full` ([modules.md L447-L491](https://github.com/c3lang/c3-web/blob/ddff47422f823e7dfd99966c99f5914bceb32c15/docs/language-fundamentals/modules.md#L447-L491)). This is the only way to run ordinary C3 functions at compile time.

**Compiler's view.**
- **Macros are copied into every call.** The macro's parameters, body and contracts are copied, then the copy is analyzed in the caller's context. Nesting is limited to 64, and nothing is cached ([sema_expr.c L3261-L3292](https://github.com/c3lang/c3c/blob/b2e1e99baaf2add3a40bd084823416a4437f3ec6/src/compiler/sema_expr.c#L3261-L3292)).
- **`$for` unrolls; it does not interpret.**
  - Each iteration copies the body's syntax tree, analyzes the copy, and appends it ([sema_stmts.c L3194-L3262](https://github.com/c3lang/c3c/blob/b2e1e99baaf2add3a40bd084823416a4437f3ec6/src/compiler/sema_stmts.c#L3194-L3262)).
  - The limit is `--max-macro-iterations`, 1,048,575 by default ([build_options.c L218](https://github.com/c3lang/c3c/blob/b2e1e99baaf2add3a40bd084823416a4437f3ec6/src/build/build_options.c#L218)).
  - Compile-time values are constant syntax nodes. There is no VM.
- **Errors.** The chain of copies is printed as `Inlined from here.` for macros and `Instantiated here.` for generics ([semantic_analyser.c L649-L683](https://github.com/c3lang/c3c/blob/b2e1e99baaf2add3a40bd084823416a4437f3ec6/src/compiler/semantic_analyser.c#L649-L683)).
- **Order.** The docs say top-level conditional evaluation across modules is not deterministic ([compiletime.md L298-L308](https://github.com/c3lang/c3-web/blob/ddff47422f823e7dfd99966c99f5914bceb32c15/docs/generic-programming/compiletime.md#L298-L308)).
- **Lernö's 2022 rationale** ([The downsides of compile time evaluation](https://c3.handmade.network/blog/p/8590-the_downsides_of_compile_time_evaluation)):
  - mixing compile-time and run-time code hurts readability and tools;
  - creating arbitrary types should be limited to generics;
  - external generators are often the better choice.

#### Generics: generic modules

**User's view.**
```c3
module std::collections::list <Type>;     // the whole section is generic
struct Foo <Type> { Type a; }             // or one declaration
fn Type square(Type t) <Type> => t * t;
Foo{int} f;
```
- Generic declarations use `<Type>` and instances use `{int}`. Since 0.7.9, declarations that share parameter names form one group, and the whole group is instantiated together ([generics.md L106-L144](https://github.com/c3lang/c3-web/blob/ddff47422f823e7dfd99966c99f5914bceb32c15/docs/generic-programming/generics.md#L106-L144)).
- `@require` contracts on a generic section are checked once per instance ([L146-L193](https://github.com/c3lang/c3-web/blob/ddff47422f823e7dfd99966c99f5914bceb32c15/docs/generic-programming/generics.md#L146-L193)).

**Compiler's view** ([sema_generics.c](https://github.com/c3lang/c3c/blob/b2e1e99baaf2add3a40bd084823416a4437f3ec6/src/compiler/sema_generics.c)).
- **Key.** The canonical arguments are mangled into a string such as `$int$`, which is then interned ([L4-L40](https://github.com/c3lang/c3c/blob/b2e1e99baaf2add3a40bd084823416a4437f3ec6/src/compiler/sema_generics.c#L4-L40)).
- **Lookup.** A linear scan over the module's generic sections, then over their instances, comparing interned pointers ([L145-L163](https://github.com/c3lang/c3c/blob/b2e1e99baaf2add3a40bd084823416a4437f3ec6/src/compiler/sema_generics.c#L145-L163)).
- **Miss.** On a miss, the compiler ([L164-L299](https://github.com/c3lang/c3c/blob/b2e1e99baaf2add3a40bd084823416a4437f3ec6/src/compiler/sema_generics.c#L164-L299)):
  1. copies every declaration of every section in the group;
  2. checks the group's contracts;
  3. registers the copies;
  4. brings them up to the stage the module has reached.
- Inference: the generic itself is never analyzed, only its copies.

#### Reflection and contracts
- **Compile time.** Type properties use `::`, such as `Foo::size`, `Foo::members`, `Enum::names` and `Enum::values` ([reflection.md L16-L50](https://github.com/c3lang/c3-web/blob/ddff47422f823e7dfd99966c99f5914bceb32c15/docs/generic-programming/reflection.md#L16-L50)).
  - 0.8.0 removed the older `$xxxof` builtins ([releasenotes L537](https://github.com/c3lang/c3c/blob/b2e1e99baaf2add3a40bd084823416a4437f3ec6/releasenotes.md#L537-L538)), and `$reflect(expr)` exists.
  - `::methods` can differ depending on where in resolution it is used.
- **Run time.**
  - A `typeid` is pointer-sized. At run time it offers only `size`, `kind`, `parent`, `inner`, `names` (for enums) and `len` ([types.md L304-L331](https://github.com/c3lang/c3-web/blob/ddff47422f823e7dfd99966c99f5914bceb32c15/docs/language-overview/types.md#L304-L331)).
  - An `any` is a pointer plus a `typeid`. `printf` takes `any[]` and switches on `arg.type.kind` ([formatter_private.c3 L38-L60](https://github.com/c3lang/c3c/blob/b2e1e99baaf2add3a40bd084823416a4437f3ec6/lib/std/io/formatter_private.c3#L38-L60)). It prints enums through `arg.type.names`, so enum name tables end up in the binary.
- **Contracts.** `@require`, `@ensure` and `@pure` live in `<* *>` doc comments ([contracts.md](https://github.com/c3lang/c3-web/blob/ddff47422f823e7dfd99966c99f5914bceb32c15/docs/language-common/contracts.md)).
  - A violation that constant folding can detect should not compile. In safe mode, contracts become runtime asserts.
  - Macro contracts are copied into every call with the body.

**Cost.** No published numbers. `--stats` times generic instantiation ([build_options.c L86](https://github.com/c3lang/c3c/blob/b2e1e99baaf2add3a40bd084823416a4437f3ec6/src/build/build_options.c#L86)).

**For chibi-v2.**
- **Copy-and-analyze costs every iteration.** C3 unrolls by copying syntax trees, so a compile-time loop of N iterations analyzes the body N times. A 64 KiB table built this way costs 65,536 analyses.
- **The rule against generated declarations didn't hold.** C3's "no declarations from nowhere" rule gave way to `$expand` four years after Lernö argued for it. Users want generated declarations, so the design should say how they become visible, not only forbid them.

### 3.9 Swift

#### Generics: witness tables, with specialization as an optimization

**User's view.**
```swift
func drawAll<S: Shape>(_ xs: [S]) { for x in xs { x.draw() } }
```

**Why Swift compiles generics this way.** Slava Pestov's *Compiling Swift Generics* ([introduction.tex L7-L21](https://github.com/swiftlang/swift/blob/swift-6.4.2-RELEASE/docs/Generics/chapters/introduction.tex#L7-L21), [PDF](https://download.swift.org/docs/assets/generics.pdf)) lists the goals:
- check generics independently of their arguments;
- let shared libraries change without recompiling their users;
- store generic struct fields inline;
- pay the overhead only across module boundaries or where types are unknown.

**Compiler's view.**
- **Calling convention.** The caller passes hidden arguments: run-time type metadata for each type parameter (size, alignment, copy and destroy functions), and a witness table for each protocol requirement ([L114, L265-L288](https://github.com/swiftlang/swift/blob/swift-6.4.2-RELEASE/docs/Generics/chapters/introduction.tex#L265-L288)). Metadata for generic types is built lazily at run time and cached.
- **Type checking.** The "Requirement Machine" answers questions about generic signatures by rewriting strings ([basic-operation.tex L9-L30](https://github.com/swiftlang/swift/blob/swift-6.4.2-RELEASE/docs/Generics/chapters/basic-operation.tex#L9-L23)).
- **Specialization** is an optimizer pass on SIL, Swift's typed IR ([GenericSpecializer.cpp L13-L14](https://github.com/swiftlang/swift/blob/swift-6.4.2-RELEASE/lib/SILOptimizer/Transforms/GenericSpecializer.cpp#L13-L14)).
  - Specializations are found by their mangled name ([Generics.cpp L2111-L2138](https://github.com/swiftlang/swift/blob/swift-6.4.2-RELEASE/lib/SILOptimizer/Utils/Generics.cpp#L2111-L2138)).
  - The pass silently gives up on types nested deeper than 50 or wider than 2000 ([L77-L94](https://github.com/swiftlang/swift/blob/swift-6.4.2-RELEASE/lib/SILOptimizer/Utils/Generics.cpp#L77-L94)).
  - Because specialization is optional, there are no errors that appear only in instances, only slower code.
- **Across modules.** Generic bodies cross module boundaries only through `@inlinable`, which stores the SIL in the module ([compilation-model.tex L483-L517](https://github.com/swiftlang/swift/blob/swift-6.4.2-RELEASE/docs/Generics/chapters/compilation-model.tex#L483-L517)).
- **Embedded Swift.** It requires full specialization and emits metadata only when it is needed. `Mirror` is unavailable there ([embedded vision](https://github.com/swiftlang/swift-evolution/blob/de9bd49899f3386e63c7c2c88d8d73ed9d22ef38/visions/embedded-swift.md#L118-L126)).

#### Code generation: macros (SE-0382, SE-0389, SE-0397)

**User's view.**
```swift
@Observable class Model { var n = 0 }
```
`@Observable` is declared as an attached macro that names the members it adds ([Observable.swift L42-L46](https://github.com/swiftlang/swift/blob/swift-6.4.2-RELEASE/stdlib/public/Observation/Sources/Observation/Observable.swift#L42-L46)).

**Compiler's view.**
- **Order of work.** A macro's arguments are type-checked first. The macro then returns syntax, which is parsed and type-checked again ([SE-0382 L44-L74](https://github.com/swiftlang/swift-evolution/blob/de9bd49899f3386e63c7c2c88d8d73ed9d22ef38/proposals/0382-expression-macros.md#L44-L62)). The macro does not see the argument types.
- **Macros are separate programs.**
  - They are written with swift-syntax. The compiler starts each one once, reuses it, and on macOS runs it in a deny-by-default sandbox ([PluginRegistry.cpp L185-L217](https://github.com/swiftlang/swift/blob/swift-6.4.2-RELEASE/lib/AST/PluginRegistry.cpp#L185-L217), [Sandbox.cpp](https://github.com/swiftlang/swift/blob/swift-6.4.2-RELEASE/lib/Basic/Sandbox.cpp#L14-L41)).
  - Messages are length-prefixed JSON ([L312-L341](https://github.com/swiftlang/swift/blob/swift-6.4.2-RELEASE/lib/AST/PluginRegistry.cpp#L312-L341)).
- **New names are declared up front.**
  - An attached macro must declare the names it introduces: exact names, prefixes, or "arbitrary". Name lookup expands only the macros that could produce the name it wants ([SE-0389 L509](https://github.com/swiftlang/swift-evolution/blob/de9bd49899f3386e63c7c2c88d8d73ed9d22ef38/proposals/0389-attached-macros.md#L509)).
  - Every expansion sees the original declaration, so the order of expansion does not matter ([L623](https://github.com/swiftlang/swift-evolution/blob/de9bd49899f3386e63c7c2c88d8d73ed9d22ef38/proposals/0389-attached-macros.md#L623)).
  - Macros on top-level declarations may not use "arbitrary" names, because that invites cycles ([L595](https://github.com/swiftlang/swift-evolution/blob/de9bd49899f3386e63c7c2c88d8d73ed9d22ef38/proposals/0389-attached-macros.md#L595)).
- **Cost sources.** An Apple engineer lists them: building swift-syntax, starting plugin processes, serializing messages, and walking syntax trees ([forum 70443](https://forums.swift.org/t/swift-macros-build-time-overhead-concerns/70443)). Third-party measurements: M30, M31.

#### Reflection
- `Mirror` reads each type's field descriptors at run time and demangles the field types ([ReflectionMirror.cpp L420-L489](https://github.com/swiftlang/swift/blob/swift-6.4.2-RELEASE/stdlib/public/runtime/ReflectionMirror.cpp#L420-L489)).
- Field descriptors are optional metadata, but often cannot be stripped because other metadata refers to them. SwiftUI breaks silently without them ([SE-0379](https://github.com/swiftlang/swift-evolution/blob/de9bd49899f3386e63c7c2c88d8d73ed9d22ef38/proposals/0379-opt-in-reflection-metadata.md#L14-L50)). That proposal was returned for revision. Third-party size number: M32.

**For chibi-v2.**
- **Declaring generated names before expansion** is the cheapest known way to let generated declarations live with order-free, parallel name lookup. A lookup knows which generators to wait for, and every other lookup proceeds.
- **Dictionary passing in debug builds** would cut compile time and code size. The cost is a second calling convention and run-time metadata, a large surface for a small team.

### 3.10 Go

#### Comptime: none. `go generate` and `stringer` instead

**User's view.**
```go
//go:generate stringer -type=Op -trimprefix=O
type Op uint8
const ( OADD Op = iota; OSUB; OMUL )
```
- `go build` never runs `go generate` ([generate.go L43-L72](https://github.com/golang/go/blob/go1.26.0/src/cmd/go/internal/generate/generate.go#L43-L72)).
- **`stringer`** loads the package with full type information and reads each constant's value ([stringer.go L289-L295](https://github.com/golang/tools/blob/v0.52.0/cmd/stringer/stringer.go#L289-L295)).
  - It picks a layout: one contiguous run, up to 10 runs, or a map when the values are sparse ([L361-L381](https://github.com/golang/tools/blob/v0.52.0/cmd/stringer/stringer.go#L361-L381)).
  - It writes one string of all names, an index array of offsets, and a `String()` method that slices the string.
  - It also writes a function that indexes a one-element array with each constant's expected value: `_ = x[OADD-0]`. If anyone renumbers a constant, the generated file stops compiling ([golden_test.go L58-L81](https://github.com/golang/tools/blob/v0.52.0/cmd/stringer/golden_test.go#L58-L81)).
- The Go compiler names its own IR ops this way ([node.go L105](https://github.com/golang/go/blob/go1.26.0/src/cmd/compile/internal/ir/node.go#L105)). That is the same job as `kind_names[]` in `lexer.c`.

#### Generics: GC-shape stenciling with dictionaries

**User's view.**
```go
func Max[T cmp.Ordered](a, b T) T { if a > b { return a }; return b }
```

**Compiler's view.**
- **Checking.** A generic body may use only operations that every type allowed by its constraint supports ([design L75-L76](https://github.com/golang/proposal/blob/83ee2aaf9fdcc1a1cf4bb4d20216fc979635cf04/design/43651-type-parameters.md#L75-L76)). It is checked once. A failed instantiation is reported at the call site.
- **Shapes.** Instances that share a "shape" share one compiled body ([shapify, reader.go L891-L969](https://github.com/golang/go/blob/go1.26.0/src/cmd/compile/internal/noder/reader.go#L891-L969)).
  - A shape is the argument's underlying type. Pointers collapse into one shape only when the constraint has methods only.
  - A TODO in the source says shapes could be merged much further.
- **Dictionary.** Each instance gets a dictionary: an array of words holding ([L1505-L1538](https://github.com/golang/go/blob/go1.26.0/src/cmd/compile/internal/noder/reader.go#L1505-L1538)):
  1. method closures;
  2. sub-dictionaries;
  3. runtime type pointers;
  4. interface tables.
- **Naming and deduplication.** Instances are named `F[targs]` and dictionaries `.dict.F[int]`. Both are marked `DUPOK`: every package that needs one emits it, and the linker keeps one copy ([L804-L805](https://github.com/golang/go/blob/go1.26.0/src/cmd/compile/internal/noder/reader.go#L804-L805)).
- **Method calls on a type parameter go through the dictionary.** The compiler doesn't know the callee, so it can't inline it ([Randall on #50182](https://github.com/golang/go/issues/50182#issuecomment-998007712)).
  - Ian Lance Taylor's guidance is that using a type parameter is generally not faster than using an interface ([When To Use Generics, 2022](https://go.dev/blog/when-generics)).
  - An open 2026 issue asks to devirtualize these calls when the constraint lists a closed set of types ([#81377](https://github.com/golang/go/issues/81377)).

**Cost.** Measured: M25–M28.
- Go 1.18 compiled about 15% slower than 1.17 (M25). Dan Scales attributed most of that to the new type checker and front end (M26). Go 1.20 recovered it (M27).
- A user benchmark of methods called through a type parameter showed 7.6× slower calls and 14 allocations instead of 1, before fixes (M28).

#### Reflection
- **Type descriptors.** Every type used at run time gets a descriptor, `internal/abi.Type` ([abi/type.go L20-L45](https://github.com/golang/go/blob/go1.26.0/src/internal/abi/type.go#L20-L45)). It holds:
  - size, alignment, kind and name;
  - a 32-bit hash;
  - an equality function;
  - a garbage-collection mask.
- **Identity.** Descriptors are named symbols that the linker deduplicates. Type identity at run time is pointer equality. The hash is a truncated hash of the type's link name ([types/fmt.go L644-L650](https://github.com/golang/go/blob/go1.26.0/src/cmd/compile/internal/types/fmt.go#L644-L650)).
- **Dead-code removal.** The linker keeps a method if it is called directly, if it matches an interface method that is used, or if reflection could reach it ([deadcode.go L402-L475](https://github.com/golang/go/blob/go1.26.0/src/cmd/link/internal/ld/deadcode.go#L402-L475)).
  - If any reachable function calls `reflect.Value.Method`, or `MethodByName` with a name that isn't a constant, the linker gives up. It then keeps every exported method of every reachable type ([L141-L143](https://github.com/golang/go/blob/go1.26.0/src/cmd/link/internal/ld/deadcode.go#L141-L143)).
  - The Go 1.7 linker's method pruning had cut typical binaries by about 30% (M29).
- **Real code.** `encoding/json` caches its per-type encoders and field lists, keyed by `reflect.Type` ([encode.go L379, L1329](https://github.com/golang/go/blob/go1.26.0/src/encoding/json/encode.go#L379)).

**For chibi-v2.**
- **One body per shape** is a possible mode for debug builds, with full instances in release builds. It needs a differential test, because the two modes run different code.
- **One dynamic reflection call can cost the whole program its dead-code removal.** So by-name reflection should be compile-time only, or work only on names known at compile time.
- **`stringer`'s array-index check** is a neat way for generated code to catch stale inputs.

### 3.11 Carbon

#### Generics: checked generics and templates

**User's view** (syntax after [p7254](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/proposals/p007254-replace-and-with-keywords-and-contextual-defaults.md#L38-L87), which removed `:!`):
```carbon
fn Max[T: Ordered](a: T, b: T) -> T { return if a > b then a else b; }
fn F[template T: type](x: T) -> i32 { let n: i32 = x.n; return n; }   // template: checked per instance
```
- Deduced parameters (in `[]`) and parameters of types and interfaces are checked generics by default. The keywords `template`, `generic` and `runtime` override the default.

**Why.** Carbon's generics goals ([goals.md L255-L286](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/docs/design/generics/goals.md#L255-L286)):
- check a body once, which is less total work;
- report errors early, as a call that doesn't meet the function's contract;
- shorten builds;
- possibly use dynamic dispatch in development builds.

Unbounded families of types are excluded so that static dispatch always works ([L644-L679](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/docs/design/generics/goals.md#L644-L679)). Templates exist mainly for C++ interop ([templates.md](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/docs/design/templates.md#L29-L58)).

**Compiler's view.**
- **A `Generic` stores the generic once.** It holds the declaration plus two *eval blocks*: lists of the instructions whose values depend on the generic parameters ([generic.h L14-L56](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/sem_ir/generic.h#L14-L56)).
- **A `Specific` is one instance.** It holds the generic, the arguments, and a value block per eval block ([L69-L147](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/sem_ir/generic.h#L69-L147)).
  - Entry *i* of a value block is eval-block instruction *i*, evaluated for this instance.
  - **The body is never copied.**
- **Deduplication.** `SpecificStore::GetOrAdd` hashes (generic ID, argument block ID) into a set ([generic.cpp L13-L57](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/sem_ir/generic.cpp#L13-L57)). Argument blocks are stored canonically, so comparing arguments compares one ID. Lookup is one hash probe.
- **When work happens.** Making a specific evaluates its declaration part at once. Definition parts are evaluated by a worklist at the end of the file ([check/generic.cpp L707-L788](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/check/generic.cpp#L707-L788)).
- **Templates.** Operations that depend on a template parameter become "template actions" in the eval block, resolved per specific ([L305-L333](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/check/generic.cpp#L305-L333)).
  - Member access, calls and conversions already work. Some operators are still TODO ([unimplemented.carbon](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/check/testdata/generic/template/unimplemented.carbon#L15-L47)).
  - So the check README's "not yet implemented" is out of date.
- **Errors.** The primary error points into the template body, with a note `unable to monomorphize specific ...` at the use site ([eval.cpp L3439-L3513](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/check/eval.cpp#L3439-L3513)).
- **Lowering.** Specifics are lowered on demand. Each lowered specific gets two 64-bit fingerprints ([inst_fingerprinter.cpp](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/sem_ir/inst_fingerprinter.cpp#L76)).
  - Specifics of the same generic are then compared **pair by pair**, and identical ones are merged ([coalesce doc L91-L118](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/docs/coalesce_generic_lowering.md#L91-L118)).
  - Bucketing by hash is described but not implemented. The compile-time cost is not measured.

#### Comptime
- `eval` and `musteval` are keywords ([token_kind.def L193, L210](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/lex/token_kind.def#L193)).
- `TryEvalCall` runs a function's SemIR, keeping locals in a hash map from instruction to constant; a TODO suggests an array ([eval.cpp L3847-L3899](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/check/eval.cpp#L3847-L3899)).
- Errors carry `in call to ... here`.
- C++ `constexpr` functions are evaluated by Clang ([cpp/constant.h](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/check/cpp/constant.h#L35-L39)).

#### Reflection and metaprogramming
Only placeholders exist. The metaprogramming design is a placeholder page ([metaprogramming.md L18-L32](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/docs/design/metaprogramming.md#L18-L32)), and no reflection design was found.

**Real code.** The prelude uses interfaces and generic impls, for example `impl forall [T: Copy & Destroy, N: IntLiteral] array(T, N) as Iterate` ([iterate.carbon L12-L32](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/core/prelude/iterate.carbon#L12-L32)).

**Cost.** Not measured.
- Carbon's benchmark generator emits no interfaces or generics yet ([source_gen.h L21-L37](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/benchmarking/source_gen.h#L21-L37)).
- It runs without the core library ([compile_benchmark.cpp L121](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/benchmarking/compile_benchmark.cpp#L121)).
- So the lines-per-second numbers in [fast-compilers.md](fast-compilers.md) say nothing about generics.

**For chibi-v2.**
- **"One body plus a value block per instance"** is the cheapest per-instance representation found. Its memory grows with (instances × parameter-dependent instructions), not (instances × body).
- **It needs generics checked against constraints.** With duck typing, almost every instruction depends on `T`, and the value block becomes a full copy.
- **Coalescing identical lowered bodies** bounds code size without changing the language. Comparing every pair breaks the growth rule, so bucket by fingerprint.

---

## 4. Measured numbers

Only numbers whose source states the workload are listed. "Not stated" means the source gives no hardware. Rows marked "third party" were measured by users rather than the project's authors; they are kept because no primary number exists for that question. The Zig comptime-speed numbers (#4055, #20163) are in the earlier note §1.3 and are not repeated.

| ID | Number | Workload | Version | Hardware | Source |
|---|---|---|---|---|---|
| **Zig** | | | | | |
| M1 | 6.264 → 5.893 s; peak RSS 1,130 → 463 MiB | Building ghostty, before and after the `InternPool` branch | 2023-06 | not stated | [#15569 comment](https://github.com/ziglang/zig/pull/15569#issuecomment-1588358234) |
| M2 | groovebasin 2.581 → 2.860 s (slower); 2Pew 2.098 → 2.190 s; legend-of-swarkland 4.642 → 4.600 s | Same comparison, other projects | 2023-06 | not stated | [#15569](https://github.com/ziglang/zig/pull/15569#issuecomment-1588465487) |
| M3 | 4.924 → 4.063 s (−17.5%); the PR says this recovers about half the `InternPool` loss | Compiler, `-fno-emit-bin` | 2023-06 | not stated | [#16105](https://github.com/ziglang/zig/pull/16105) |
| M4 | 4.43 → 4.68 s (+5.7%); instructions −11% | Thread-safe `InternPool`, threading off, compiler `-fno-emit-bin` | 2024-07 | not stated | [#20528](https://github.com/ziglang/zig/pull/20528) |
| M5 | 12.8 → 8.57 s | Compiler self-build with codegen on a separate thread | 0.14.0 | "one computer" | [0.14.0 notes](https://ziglang.org/download/0.14.0/release-notes.html#Multithreaded-Backend-Support) |
| M6 | Compiler build 12.4 → 10.6 s; peak RSS 1.03 → 1.14 GB; groovebasin 1.86 → 2.09 s; ReleaseSmall compiler 13.6 → 13.3 MiB | Replacing generic `anytype` writers with a concrete `Io.Writer` ("Writergate") | 2025-07 | not stated | [#24329](https://github.com/ziglang/zig/pull/24329) |
| M7 | `.text` −72.8 KiB (−21.7%) | A kernel after replacing the old `std.fmt` (third party) | 2021 | not stated | [#9635](https://github.com/ziglang/zig/issues/9635) |
| **Odin** | | | | | |
| M8 | Types allocated 806k → 201k; type memory 141 → 17 MiB; peak 2,162 → 1,895 MiB; checker 135 → 122 ms (project B is similar) | "Project A", after per-kind type allocation, a pointer-type cache and the instance lookup cache | dev, 2026-10-11 | not stated | [PR #7886](https://github.com/odin-lang/Odin/pull/7886) |
| M9 | `#soa` append up to 11.05× faster (8 fields of 4 B); 1.92× (2 fields); batch append up to 10.27× | Run-time type-table walk replaced by compiler-lowered stores | dev, 2026-08 | i7-4770, Windows 10, LLVM 20.1, `-o:speed` | [PR #7196](https://github.com/odin-lang/Odin/pull/7196) |
| M10 | 161 KiB with `fmt.println`; 21 KiB without `fmt` | Hello world, `-o:size`; type table not isolated from `fmt` (third party) | 2024-12 | Linux | [forum](https://forum.odin-lang.org/t/why-are-odins-binaries-so-large-and-compilation-times-so-slow/542) |
| **D** | | | | | |
| M11 | 243 ms vs 159 ms, binary 69 KB smaller; `register` with alias parameters about 5 s vs about 1 s with function pointers; about 50,800 template instance analyses | 100 unittests with local vs module-level types; a registration template; tracing a dmd build | 2022; the trace used dmd 2.097.0 | "my system" | [Haughton, D blog](https://blog.dlang.org/archive/2022/03/24/reducing-template-compile-times/) |
| M12 | 3.113 s vs 0.285 s | `dmd -c` on a 4-line file with and without `regex("")` | early 2018 | not stated | [Teoh, forum](https://forum.dlang.org/post/mailman.3091.1517866806.9493.digitalmars-d@puremagic.com) |
| M13 | 1.24 s | dmd compiling Phobos, 88k lines | 2010 (version UNVERIFIED) | a laptop | [Alexandrescu, forum](https://forum.dlang.org/post/hpsps2$2lqp$1@digitalmars.com) |
| M14 | Check: dmd 4.5 / 7.1 µs per function, clang++ 17.4 / 28.6. Compile: dmd 6.8 / 10.1, clang++ 90.6 / 102.6. Each pair is without / with templates. (third party) | 40,000 generated functions | dmd 2.113.0, clang 23.1.1, 2026-09 | Ryzen AI 7 350 | [compiler-benchmark @dc7d33c](https://github.com/nordlow/compiler-benchmark/blob/dc7d33c70fbe1caaa7e17a6b0d031a6e744dfd27/README.md) |
| **C++** | | | | | |
| M15 | Syntax-tree evaluator 25.2 s vs bytecode 0.9 s (the author calls debug-build numbers meaningless) | A constexpr loop of 100k increments | Clang, 2024 | debug builds of Clang | [Bäder 2024](https://developers.redhat.com/articles/2024/10/21/new-constant-expression-interpreter-clang) |
| M16 | Summing an embedded sqlite3: 36.49 s vs 14.80 s. 10k × allocate/fill/free 1 KB: syntax tree 27.79 s, bytecode 4.24 s, GCC 14.3.1 43.83 s. 1 MiB allocation: 1,048 s vs 0.440 s (GCC 0.117 s) | Constant evaluation benchmarks, hyperfine, 30 runs | Clang release build, 2025 | not stated | [Bäder 2025](https://developers.redhat.com/articles/2025/10/15/clang-bytecode-interpreter-update) |
| M17 | Per translation unit, N=16: X-macro 26.9 ms; reflection 187.7 ms; with PCH 81.0 ms; with modules 403.1 ms. N=1024: 54.7 vs 255.0 ms. `<meta>` alone about +155 ms. Per enumerator about 0.027 vs 0.07 ms | Enum to string | GCC 16.1.1, 2026-05 | i9-13900K, pinned to one P-core | [Romeo](https://vittorioromeo.com/index/blog/refl_enum_to_string.html) |
| **Rust** | | | | | |
| M18 | `RawVec::grow` was 56,700 IR lines (5.6%) in 108 copies. After the split: debug builds up to 5.7% faster, optimized up to 1.7%, IR lines down up to 15% | webrender and rustc-perf | nightly, 2020-05 | rustc-perf server | [#72013](https://github.com/rust-lang/rust/pull/72013#issuecomment-627000894) |
| M19 | Instructions −0.0% (range −3.6% to +1.6%); max RSS +2.9%; binary size −0.5% (primary), −7.1% (secondary) | Sharing generics for `inline(never)` functions | 2024-11 | rustc-perf server | [#123244](https://github.com/rust-lang/rust/pull/123244#issuecomment-2495777745) |
| M20 | `windows` crate 3m48s → 2m55s; aws-sdk-ec2 4m07s → 2m04s; codegen share for `windows` 49% → 1% | `-Zhint-mostly-unused`: generate code only for items used | nightly, 2025-07 | not stated | [Triplett](https://blog.rust-lang.org/inside-rust/2025/07/15/call-for-testing-hint-mostly-unused/) |
| M21 | Two-field struct: Deserialize 159 lines (8,622 B); Serialize 23 lines; Debug 8 lines | `-Zmacro-stats` | nightly, 2025-06 | n/a | [Nethercote](https://nnethercote.github.io/2025/06/26/how-much-code-does-that-proc-macro-generate.html) |
| M22 | Expanded code more than tripled the source; about 20% faster compiles after trimming macros | An unnamed project of about 100k lines | 2025 | not stated | same post |
| M23 | 16k source lines → 27k generated (bevy_ui). After the fix: generated code −39%, `cargo check` −16%, peak memory −5% (bevy_window) | `derive(Reflect)` | 2025-12 | not stated | [Nethercote](https://nnethercote.github.io/2025/12/05/how-to-speed-up-the-rust-compiler-in-december-2025.html) |
| M24 | −75% (1,000 field-less enums); −30 to −40% (1,000 structs) | `derive(Arbitrary)` stress tests after shrinking its output | arbitrary 1.4.2, 2025-08 | not stated | [Nethercote](https://nnethercote.github.io/2025/08/16/speed-wins-when-fuzzing-rust-code-with-derive-arbitrary.html) |
| **Go** | | | | | |
| M25 | Compiler about 15% slower than 1.17 | Release notes | 1.18 | n/a | [go1.18.html L554-L559](https://github.com/golang/go/blob/go1.18/doc/go1.18.html#L554-L559) |
| M26 | 15–18% slower; front end about 4% of a run before vs about 14% with the new type checker ("rough", few inputs) | `-G=0` vs `-G=3` | 1.18 development | not stated | [Scales, #49569](https://github.com/golang/go/issues/49569) |
| M27 | Build speed up to 10% better, back in line with 1.17 | Release notes | 1.20 | n/a | [go1.20.html L338-L343](https://github.com/golang/go/blob/go1.20/doc/go1.20.html#L338-L343) |
| M28 | 55.26 ns, 1 alloc (interface) vs 421.3 ns, 14 allocs (type parameter); after fixes 34 ns vs 65 ns (third party) | `sort.Interface` methods called through a type parameter | 1.18beta1 | Ryzen 5 3600X | [#50182](https://github.com/golang/go/issues/50182), [follow-up](https://github.com/golang/go/issues/50182#issuecomment-1040507717) |
| M29 | Typical binaries about 30% smaller; hello world 2.3 → 1.6 MB | Linker method pruning | 1.7 | not stated | [Crawshaw 2016](https://go.dev/blog/go1.7-binary-size) |
| **Swift (all third party)** | | | | | |
| M30 | Clean builds: MemberwiseInit 429 s vs 226 s hand-written; Observation 154.9 vs 84.8 s | Macro stress test | 2024-03 | not stated | [forum 70443](https://forums.swift.org/t/swift-macros-build-time-overhead-concerns/70443) |
| M31 | Emit-module phase 40 s → 16 s | Prebuilt swift-syntax for macros | 6.1 | not stated | [forum 80202](https://forums.swift.org/t/preview-swift-syntax-prebuilts-for-macros/80202) |
| M32 | 32 MB → 30 MB without reflection metadata, then the app misbehaved | Vapor template, release build | 5.6.1 | not stated | [pitch thread](https://forums.swift.org/t/pitch-3-opt-in-reflection-metadata/58852) |
| **Nim** | | | | | |
| M33 | 216,251 lines in 12.641 s; peak 638.8 MiB (probably includes the C compile, UNVERIFIED) | Compiler self-build, `-d:release`, ORC, with the new VM | devel `d8310aa`, 2026-10 | CI runner | [bot on #26323](https://github.com/nim-lang/Nim/pull/26323) |
| M34 | 5,391 ms total; routine-body checking 3,409 ms (63%); C body generation 504 ms | `nim c --compileOnly compiler/nim.nim`, 244 modules | branch, 2026-09 | "32-core machine" | [Araq, #26199](https://github.com/nim-lang/Nim/issues/26199) |
| M35 | Front end 71.5 s → 83 s → 72.5 s; peak 6.33 → 4.34 GiB | Nimbus: the new VM materialized large arrays; then fixed | devel, 2026-10 | not stated | [#26367](https://github.com/nim-lang/Nim/pull/26367) |
| M36 | Linear constant deduplication in `vmgen` took 11.6% of CPU; build 3 min 15 s | Nimbus, `nim c -c` | 2.2.4 | Linux, VTune | [#25114](https://github.com/nim-lang/Nim/issues/25114) |
| M37 | Compile time 51 s vs run time 0.04 s | nim-regex tests run at compile time vs at run time (library author) | devel, 2021 | macOS | [nim-regex #104](https://github.com/nitely/nim-regex/issues/104) |

**What the numbers say together.**
- **Representation beats algorithm.** M15, M16 and M35 are all changes to how compile-time values are stored. Each moved time by an integer factor, in either direction.
- **Generated code is paid for twice:** once to generate it, then again to check and compile the larger output (M21–M24, M30).
- **Instance count matters more than instance size.** The large wins came from creating fewer instances, not from making each one cheaper: M6, M11, M18 and M20.

---

## 5. Design-space tables

### 5.1 Compile-time execution and code generation

| Language | What runs it | What it may access | What it may produce | Determinism and limits |
|---|---|---|---|---|
| Zig | `Sema` interprets untyped ZIR; every value is interned | `@embedFile`, `@import`; no extern calls, no other I/O | Values and types (`@Type`; `@Struct`, `@Enum` and others in 0.16). No declarations. | Deterministic. Branch quota 1,000 by default; an instance inherits its first caller's quota ([#22410](https://github.com/ziglang/zig/issues/22410)) |
| Jai (UNVERIFIED) | Bytecode interpreter; a user metaprogram | Anything, including DLL calls and files | Values, code strings, syntax trees, whole declarations, and edits to the tree before code generation | Up to the programmer |
| Odin | Constant folding in the checker | `#load`, `#exists`, `#config` | Constants only | Deterministic |
| D | Syntax-tree interpreter (CTFE) | `import("file")` under `-J` paths | Values, source text (mixins), template instances | Deterministic; template depth 500, CTFE recursion 1,000 |
| C++ | Syntax-tree evaluator, or Clang's opt-in bytecode | No I/O (only `#embed` in the preprocessor) | Values, template instances; C++26 can complete a declared class | Deterministic; step limits (Clang 2^20 steps, GCC 2^25 operations) |
| Rust | MIR interpreter for `const`; proc macros as native libraries inside rustc | `const`: nothing. Proc macros: anything the process can do | `const`: values. Macros: tokens that become any items | `const` is deterministic. Proc macros are not, so their caching is "potentially unsound" |
| Nim | Register VM over target-layout memory (2.4) | `staticRead`; `staticExec` runs processes; C calls with a special build | Values, and syntax trees that become any declarations | Loop limit 10^7 per evaluation; `staticExec` is not deterministic |
| C3 | Copying and unrolling syntax trees, plus constant folding | `$embed`, `$include`; `$exec` with `--trust=full` | Values, constant tables, declarations through `$expand` (0.8) | The docs say top-level evaluation order across modules is not deterministic |
| Swift | Macros as separate sandboxed processes | Sandboxed (deny by default) | Syntax that becomes declarations whose names were declared up front | Expansions are independent, so order doesn't matter |
| Go | Nothing in the compiler; `go generate` runs any program | Anything | Source files | Outside the build |
| Carbon | SemIR interpreter (`TryEvalCall`) | Nothing yet | Values; metaprogramming is a TODO | Deterministic |

### 5.2 Generics

| Language | Body checked | Code emitted | Instance key | Cache and lookup cost | Errors in instances |
|---|---|---|---|---|---|
| Zig | Per instance | One copy per instance | Functions: (generic, comptime arguments, function type). Types: (declaring instruction, captured values) | Sharded intern table; equality is one `u32` compare | `generic function instantiated here`, plus a reference trace |
| Jai (UNVERIFIED) | Per instance | One copy per instance; `#modify` can rewrite the key | UNVERIFIED | UNVERIFIED | UNVERIFIED |
| Odin | Per instance; the body's syntax tree is copied | One copy per instance | Hash of the call's operands; hash of the specialized signature | Per-generic lists, linear scan | `instantiated as 'f($T=...)'`, up to 8 deep |
| D | Per instance; constraints are trial compiles | One copy per instance, COMDAT; `needsCodegen` picks the object file | (enclosing scope, deduced arguments), hashed by content | Hash table per template; hashing a value costs its length | `instantiated from here`; trial-compile errors gagged |
| C++ | Per instance; concepts checked only at use | One copy per instance per TU, COMDAT | Template argument profile | Per template, per translation unit | `requested here`, capped at 10 |
| Rust | Once, against bounds | One copy per instance; shared across crates at low optimization | (instance kind, interned argument list) | Query cache; parallel collection, then a sort by stable hash | Only from const evaluation: `encountered while instantiating` |
| Nim | Per instance, after symbols are bound at the definition | One copy per instance | Generic symbol ID → list | One hash, then a linear scan with structural compares | `template/generic instantiation of X from here` |
| C3 | Per instance; the whole group is copied | One copy per instance | Mangled canonical arguments, interned | Linear scan over sections and instances | `Instantiated here.` |
| Swift | Once (Requirement Machine) | One shared body with metadata and witness tables; specialization optional | Mangled name, for specializations | Function lookup in the module | None; the specializer silently gives up |
| Go | Once (types2) | One body per shape, plus a dictionary | `F[targs]`, `.dict.F[...]` | Compiler table; the linker drops duplicates by name | At the call site |
| Carbon | Once (checked) or per instance (template) | One copy per instance; identical lowered bodies merged | (generic ID, canonical argument-block ID) | Hash set, one probe; merging compares pairs | `unable to monomorphize specific` |

### 5.3 Reflection

| Language | Compile time | Run time | Contents | Binary cost and opt-out | Type ID across builds |
|---|---|---|---|---|---|
| Zig | `@typeInfo`, `@field`, `@hasDecl`, `@FieldType` | None built in. Per-enum `@tagName` function on first use; one global error-name table if used | Fields, declarations, layout | Only what code copies to run time | No runtime ID |
| Jai (UNVERIFIED) | `type_info()` in `#run` code | `Type_Info` table for reached types; `Any` | UNVERIFIED | `#type_info_none` and others | UNVERIFIED |
| Odin | `type_*` intrinsics, `size_of`, constant `typeid` | `Type_Info` table, `typeid`, `any`, `core:reflect` | Names, types, offsets, tags, enum names and values | Only types the program reaches. `-no-rtti` only on freestanding targets, and it breaks `fmt` | Hash of the canonical name: stable, but blind to layout changes |
| D | `__traits`, UDAs, `.stringof` | `TypeInfo`, `typeid` | Members at compile time (order unspecified); little at run time | `-betterC` removes it | Not researched |
| C++26 | `std::meta` with annotations | `typeid` / `type_info`: name and identity only | Members, enumerators, annotations, parameters | `-fno-rtti` | Not researched |
| Rust | None stable; `core::mem::type_info` is unstable and compile-time only | `TypeId`, `type_name`, `Any` | Identity only | None | Varies between compiler releases (documented) |
| Nim | `getTypeImpl`, `typetraits`, `fieldPairs` | Minimal; no field names | Size, alignment, destructor; names with a flag | `-d:nimTypeNames` adds names | Not researched |
| C3 | `::members`, `::names`, `$reflect` | `typeid` (size, kind, parent, inner, enum names, len); `any` | Limited at run time | Not documented | Not documented |
| Swift | Macros see syntax only | `Mirror` over metadata and field descriptors | Fields, names, types | Flags exist, but stripping breaks SwiftUI (SE-0379) | Not researched |
| Go | None in the compiler (`go/types` in generators) | `reflect` over `abi.Type` | Everything: fields, methods, tags | Dynamic method lookup disables method dead-code removal | None: pointer identity within one binary |
| Carbon | Not designed | Not designed | n/a | n/a | n/a |

---

## 6. `CHIBI_TOKENS` in six languages

**The C original.** `src/lexer.h` declares one table, and every consumer picks the columns it needs by passing different macros:
```c
#define CHIBI_TOKENS(OTHER, KW, OP3, OP2, OP1)  \
    OTHER(TK_EOF, "EOF")                        \
    OTHER(TK_IDENT, "IDENT")                    \
    KW(TK_IF, "if")                             \
    KW(TK_ELSE, "else")                         \
    OP3(TK_SHLEQ, '<', '<', '=')                \
    OP2(TK_SHL, '<', '<')                       \
    OP1(TK_LT, '<')                             \
    OP1(TK_PLUS, '+')
```
From it, `lexer.h` and `lexer.c` generate:
- the `TokenKind` enum, `NKINDS` and `NKEYWORDS`;
- `keywords[]`, the text of each keyword;
- a `static_assert` that the keywords are consecutive kinds starting at `TK_IF`;
- `kind_names[]`;
- `kind_len[]`, the length of each operator;
- `kind2[2][256][256]`, the table indexed by a token's first two bytes.

`parser.c` does the same with `CHIBI_NODES` for prefix nodes, binary operators, arity, list starts, markers and labels.

The versions below use the eight tokens above. Each one indexes the two-byte table as `[first][second]`. The C code stores it transposed, so that one little-endian 16-bit load can index it. None of the versions was compiled; lines marked `UNSURE` are the ones most likely to need fixing.

### Zig (0.15; 0.16 change noted)

The table is an array of structs. Comptime code derives everything else from it, including the enum.
```zig
const std = @import("std");

const Class = enum { other, keyword, op };
const Spec = struct { name: [:0]const u8, text: []const u8, class: Class };

const specs = [_]Spec{
    .{ .name = "eof", .text = "EOF", .class = .other },
    .{ .name = "ident", .text = "IDENT", .class = .other },
    .{ .name = "kw_if", .text = "if", .class = .keyword },
    .{ .name = "kw_else", .text = "else", .class = .keyword },
    .{ .name = "shl_eq", .text = "<<=", .class = .op },
    .{ .name = "shl", .text = "<<", .class = .op },
    .{ .name = "lt", .text = "<", .class = .op },
    .{ .name = "plus", .text = "+", .class = .op },
};

pub const TokenKind = blk: {
    var fields: [specs.len]std.builtin.Type.EnumField = undefined;
    for (specs, 0..) |s, i| fields[i] = .{ .name = s.name, .value = i };
    break :blk @Type(.{ .@"enum" = .{
        .tag_type = u8,
        .fields = &fields,
        .decls = &.{},
        .is_exhaustive = true,
    } });
};

pub const nkinds = specs.len;

pub const kw_first = blk: {
    for (specs, 0..) |s, i| if (s.class == .keyword) break :blk i;
    @compileError("no keywords"); // UNSURE: whether this line is analyzed after the comptime break
};

pub const nkeywords = blk: {
    var n: usize = 0;
    for (specs) |s| n += @intFromBool(s.class == .keyword);
    break :blk n;
};

comptime {
    for (specs[kw_first..][0..nkeywords]) |s| {
        if (s.class != .keyword) @compileError("keywords must be consecutive: " ++ s.name);
    }
}

pub const kind_names: [nkinds][]const u8 = blk: {
    var t: [nkinds][]const u8 = undefined;
    for (&t, specs) |*o, s| o.* = s.text;
    break :blk t;
};

pub const kind_len: [nkinds]u8 = blk: {
    var t: [nkinds]u8 = undefined;
    for (&t, specs) |*o, s| o.* = if (s.class == .op) @intCast(s.text.len) else 0;
    break :blk t;
};

pub const kind2: [256][256]TokenKind = blk: {
    @setEvalBranchQuota(1 << 20);
    var t = [_][256]TokenKind{[_]TokenKind{.eof} ** 256} ** 256;
    for (specs, 0..) |s, i| {
        if (s.class == .op and s.text.len == 1) {
            for (&t[s.text[0]]) |*e| e.* = @enumFromInt(i);
        }
    }
    for (specs, 0..) |s, i| {
        if (s.class == .op and s.text.len >= 2) t[s.text[0]][s.text[1]] = @enumFromInt(i);
    }
    break :blk t;
};

pub const keywords = std.StaticStringMap(TokenKind).initComptime(blk: {
    var kvs: [nkeywords]struct { []const u8, TokenKind } = undefined;
    for (&kvs, kw_first..) |*kv, i| kv.* = .{ specs[i].text, @enumFromInt(i) };
    break :blk kvs;
});
```

**What it shows.**
- **One table, everything derived,** as in the C X-macro.
- **A 0.16 change.** The `TokenKind` block becomes `@Enum(u8, .exhaustive, &names, &values)`, built from two arrays filled in the same loop ([0.16.0 notes](https://ziglang.org/download/0.16.0/release-notes.html)).
- **No methods on the enum.** A built enum cannot carry declarations, so helpers live outside it.
- **The idiom std itself uses is the other way round.** It writes `enum(u8) { ... }` by hand and derives tables from it. `std.enums.EnumArray(TokenKind, []const u8).init(.{ ... })` then gives the X-macro's completeness check.
- **Cost (estimate, not measured).** Filling 65,536 entries runs at least 65,536 interned stores. At the 2023 rate in #4055 (about 1.9 µs per loop iteration), that is about 0.1 s. Zig's own tokenizer avoids such tables.

### Jai (UNVERIFIED syntax from community sources)

The enum is written by hand. Ordinary procedures build the tables, and `#run` runs them at compile time.
```jai
Token_Kind :: enum u8 {
    EOF; IDENT;
    IF; ELSE;                 // keywords: IF .. ELSE
    SHL_EQ; SHL; LT; PLUS;    // operators: SHL_EQ .. PLUS
}

SPELLING :: string.["EOF", "IDENT", "if", "else", "<<=", "<<", "<", "+"];

N_KINDS    :: #run type_info(Token_Kind).values.count;
N_KEYWORDS :: cast(int) Token_Kind.ELSE - cast(int) Token_Kind.IF + 1;
FIRST_OP   :: cast(int) Token_Kind.SHL_EQ;
#assert(SPELLING.count == N_KINDS);

build_kind2 :: () -> [256*256] u8 {
    t: [256*256] u8;
    for k: FIRST_OP .. N_KINDS-1 {
        s := SPELLING[k];
        if s.count == 1  for c1: 0..255  t[s[0]*256 + c1] = xx k;
    }
    for k: FIRST_OP .. N_KINDS-1 {
        s := SPELLING[k];
        if s.count >= 2  t[s[0]*256 + s[1]] = xx k;
    }
    return t;
}

KIND2 :: #run build_kind2();
```

**What it shows.**
- **Ordinary code, run at compile time.** The table builder is a normal procedure, and the result is baked into the binary's data.
- **Generating the enum itself** would need `#insert #run` with a string. Whether `#insert` works inside an enum body is UNVERIFIED.

### Odin

The enum is the source of truth. The tables are written as complete enumerated arrays or built at startup, because Odin cannot loop at compile time.
```odin
Token_Kind :: enum u8 {
	EOF, Ident,
	If, Else,                    // keywords: If ..= Else
	Shl_Eq, Shl, Lt, Plus,       // operators: Shl_Eq ..= Plus
}

N_KINDS    :: len(Token_Kind)
N_KEYWORDS :: int(Token_Kind.Else) - int(Token_Kind.If) + 1
#assert(N_KINDS <= 128)

@(rodata) TOKEN_TEXT := [Token_Kind]string{
	.EOF = "EOF", .Ident = "IDENT",
	.If = "if", .Else = "else",
	.Shl_Eq = "<<=", .Shl = "<<", .Lt = "<", .Plus = "+",
}

@(rodata) OP_LEN := #partial [Token_Kind]u8{.Shl_Eq = 3, .Shl = 2, .Lt = 1, .Plus = 1}

kind2: [256][256]Token_Kind

init_lexer_tables :: proc() {
	for k in u8(Token_Kind.Shl_Eq) ..= u8(Token_Kind.Plus) {
		s := TOKEN_TEXT[Token_Kind(k)]
		if len(s) == 1 { for &e in kind2[s[0]] { e = Token_Kind(k) } }
	}
	for k in u8(Token_Kind.Shl_Eq) ..= u8(Token_Kind.Plus) {
		s := TOKEN_TEXT[Token_Kind(k)]
		if len(s) >= 2 { kind2[s[0]][s[1]] = Token_Kind(k) }
	}
}
```

**What it shows.**
- **Completeness without comptime.** Leaving `#partial` off `TOKEN_TEXT` makes a missing kind a compile error, the X-macro's main safety property.
- **Derived tables are hand-written or built at startup.** `OP_LEN` repeats facts that `TOKEN_TEXT` already holds, so the two can drift apart. The 64 KiB table is filled when the program starts, as Odin's own tokenizer does.
- **The other route is a generator program.** `odin run tools/gen_tables` writes a `.bin` file that the lexer loads with `@(rodata) KIND2 := #load("kind2.bin", []u8)`, as `core:rexcode` does.
- **Run-time names** from `fmt` or `reflect.enum_string` are the identifiers (`If`), not the spellings (`if`).

### D

The table is an array of structs known at compile time. CTFE turns it into enum source text, and a mixin compiles that text.
```d
enum Shape : ubyte { other, kw, op }
struct Tok { string name; Shape shape; string text; }

enum Tok[] TOKENS = [
    Tok("EOF", Shape.other, "EOF"), Tok("IDENT", Shape.other, "IDENT"),
    Tok("IF", Shape.kw, "if"), Tok("ELSE", Shape.kw, "else"),
    Tok("SHLEQ", Shape.op, "<<="), Tok("SHL", Shape.op, "<<"),
    Tok("LT", Shape.op, "<"), Tok("PLUS", Shape.op, "+"),
];

string enumSource() {
    string s = "enum TokenKind : ubyte {";
    foreach (t; TOKENS) s ~= "TK_" ~ t.name ~ ",";
    return s ~ "}";
}
mixin(enumSource());

enum size_t NKINDS = TOKENS.length;
enum size_t FIRST_KW = () { foreach (i, t; TOKENS) if (t.shape == Shape.kw) return i; assert(0); }();
enum size_t NKEYWORDS = () { size_t n; foreach (t; TOKENS) if (t.shape == Shape.kw) ++n; return n; }();
static assert(FIRST_KW == TokenKind.TK_IF);
static assert(() {
    foreach (i, t; TOKENS)
        if ((t.shape == Shape.kw) != (i >= FIRST_KW && i < FIRST_KW + NKEYWORDS)) return false;
    return true;
}(), "keywords must be consecutive");

immutable string[NKINDS] KIND_NAMES = () { string[NKINDS] a; foreach (i, t; TOKENS) a[i] = t.text; return a; }();
immutable ubyte[NKINDS] KIND_LEN = () {
    ubyte[NKINDS] a;
    foreach (i, t; TOKENS) if (t.shape == Shape.op) a[i] = cast(ubyte) t.text.length;
    return a;
}();
immutable TokenKind[256][256] KIND2 = () {   // UNSURE: CTFE time and memory for 65,536 elements
    TokenKind[256][256] a;
    foreach (i, t; TOKENS) if (t.shape == Shape.op && t.text.length == 1) a[t.text[0]][] = cast(TokenKind) i;
    foreach (i, t; TOKENS) if (t.shape == Shape.op && t.text.length >= 2) a[t.text[0]][t.text[1]] = cast(TokenKind) i;
    return a;
}();
```

**What it shows.**
- **The closest match to the X-macro's shape.** One table generates the enum and every derived table.
- **Generated names arrive late.** `TokenKind` exists only after the mixin is analyzed, so nothing earlier in the module may refer to it by name.
- **Cost risk.** CTFE is a syntax-tree interpreter that allocates per expression (earlier note §1.2), so the 65,536-element table is the likely cost.

### C3 (current syntax at c3c `b2e1e99`)

An enum with associated values is the table. `@const` macros build the derived tables at compile time.
```c3
module chibi::token;

enum TokenClass : char { OTHER, KEYWORD, OP }

enum TokenKind : char (TokenClass cls, String text)
{
    EOF   { TokenClass.OTHER, "EOF" },     IDENT { TokenClass.OTHER, "IDENT" },
    IF    { TokenClass.KEYWORD, "if" },    ELSE  { TokenClass.KEYWORD, "else" },
    SHLEQ { TokenClass.OP, "<<=" },        SHL   { TokenClass.OP, "<<" },
    LT    { TokenClass.OP, "<" },          PLUS  { TokenClass.OP, "+" },
}

const usz NKINDS = TokenKind::len;

macro String[] @texts(TokenClass $c) @const
{
    String[] $out = {};
    $foreach $k : TokenKind::values:
        $if $k.cls == $c:
            $out +++= { $k.text };
        $endif
    $endforeach
    return $out;
}

const String[*] KEYWORD_TEXT = @texts(TokenClass.KEYWORD);
const usz NKEYWORDS = KEYWORD_TEXT.len;

macro char[65536] @kind2() @const
{
    char[65536] $t;
    $foreach $k : TokenKind::values:
        $if $k.cls == TokenClass.OP && $k.text.len == 1:
            $for var $c = 0; $c < 256; $c++:          // UNSURE: $for header syntax
                $t[(usz)$k.text[0] << 8 | $c] = (char)$k.ordinal;
            $endfor
        $endif
    $endforeach
    $foreach $k : TokenKind::values:
        $if $k.cls == TokenClass.OP && $k.text.len >= 2:
            $t[(usz)$k.text[0] << 8 | (usz)$k.text[1]] = (char)$k.ordinal;
        $endif
    $endforeach
    return $t;
}

const char[65536] KIND2 = @kind2();
```

**What it shows.**
- **No X-macro is needed.** `TokenKind::names` gives the name table, `k.text.len` the operator length, and `TokenKind::len` the count.
- **Unrolling has a cost.** `$foreach` and `$for` unroll by copying and re-analyzing the body. The row fill alone is 256 analyses per one-byte operator.

### C++26 (P2996, P1306, P3394, P3491)

C++26 cannot define an enum from a table, so the table moves onto the enum as annotations: the reverse of the X-macro.
```cpp
#include <array>
#include <cstdint>
#include <meta>
#include <string_view>

struct Kw { char text[8]; };
struct Op { char text[4]; };

enum class TokenKind : std::uint8_t {
    Eof, Ident,
    If [[=Kw{"if"}]], Else [[=Kw{"else"}]],
    ShlEq [[=Op{"<<="}]], Shl [[=Op{"<<"}]], Lt [[=Op{"<"}]], Plus [[=Op{"+"}]],
};

constexpr std::size_t NKINDS = std::meta::enumerators_of(^^TokenKind).size();

template <class A> consteval bool has(std::meta::info e) {
    return !std::meta::annotations_of_with_type(e, ^^A).empty();
}
template <class A> consteval A get(std::meta::info e) {        // UNSURE: may need constant_of() first
    return std::meta::extract<A>(std::meta::annotations_of_with_type(e, ^^A)[0]);
}

struct Tables {
    std::array<std::string_view, NKINDS> name{};
    std::array<std::uint8_t, NKINDS> op_len{};
    std::array<std::array<TokenKind, 256>, 256> kind2{};       // Eof = none
    std::size_t first_kw = NKINDS, nkw = 0;
    bool kw_consecutive = true;
};

consteval Tables build() {
    Tables t;
    template for (constexpr std::meta::info e :
                  std::define_static_array(std::meta::enumerators_of(^^TokenKind))) {
        constexpr std::size_t i = static_cast<std::size_t>([:e:]);
        t.name[i] = std::meta::identifier_of(e);
        if constexpr (has<Kw>(e)) {                              // UNSURE: discarded inside template for?
            if (t.first_kw == NKINDS) t.first_kw = i;
            t.kw_consecutive = t.kw_consecutive && i == t.first_kw + t.nkw;
            ++t.nkw;
        }
        if constexpr (has<Op>(e)) {
            constexpr Op op = get<Op>(e);
            std::string_view s = op.text;
            t.op_len[i] = s.size();
            if (s.size() == 1) for (auto& x : t.kind2[(unsigned char)s[0]]) x = [:e:];
        }
    }
    template for (constexpr std::meta::info e :
                  std::define_static_array(std::meta::enumerators_of(^^TokenKind))) {
        if constexpr (has<Op>(e)) {
            constexpr Op op = get<Op>(e);
            std::string_view s = op.text;
            if (s.size() >= 2) t.kind2[(unsigned char)s[0]][(unsigned char)s[1]] = [:e:];
        }
    }
    return t;
}

inline constexpr Tables TOK = build();
static_assert(TOK.first_kw == std::size_t(TokenKind::If) && TOK.kw_consecutive);
```

**What it shows.**
- **The names are the enumerator identifiers** (`If`). The spellings live in annotations.
- **The cost is paid per translation unit.** Every file that includes this header pays about 155 ms for `<meta>` (M17), then runs `build()` again. The 65,536-element table also counts against the constexpr step limits.

### Side by side

| | Where the one table lives | Can it generate the enum? | Builds the 64 KiB table at compile time? | Completeness check | Cost of the table (estimate unless noted) |
|---|---|---|---|---|---|
| C X-macro | Macro list | Yes (preprocessor) | Yes, with designated initializers that the compiler folds | `static_assert` | Negligible |
| Zig | Array of structs | Yes, with `@Type` / `@Enum`, but the enum can have no methods | Yes, as a comptime loop of interned stores | `comptime` block, or `EnumArray.init` | About 0.1 s at the 2023 comptime rate |
| Jai (UNVERIFIED) | Enum plus array | Only through `#insert` (UNVERIFIED) | Yes, `#run` | `#assert` | Unknown |
| Odin | Enum plus enumerated arrays | No | No: build at startup, or run a generator and `#load` its output | Enumerated-array literal without `#partial` | Startup loop; or zero, with a generator |
| D | Array of structs | Yes, string mixin | Yes, CTFE | `static assert` | CTFE memory risk |
| C3 | Enum with associated values | The enum *is* the table | Yes, `@const` macro, unrolled | `$assert` | 65,536+ copies of the loop body analyzed |
| C++26 | Enum plus annotations | No | Yes, `consteval` | `static_assert` | +155 ms per translation unit for `<meta>` (measured, M17), plus steps |

The comparison shows what chibi-v2 needs to replace its X-macros without losing anything:
- loops at compile time;
- the ability to build an enum, or a complete table keyed by an enum;
- values stored as plain bytes, so that a 64 KiB table costs milliseconds, not seconds.

---

## 7. Design options for chibi-v2

**The budget** ([hypotheses.md](../hypotheses.md#the-budget)):
- one core: 3,950 cycles per line for all phases;
- 14 cores: serial work at most 400 cycles per line ([H4](../hypotheses.md));
- checking: at most 3,000 cycles per line on one thread, and 10× faster on 14 ([H5](../hypotheses.md));
- backend: at most 1,000 cycles per line ([H6](../hypotheses.md)).

Each option below gives its effect on that budget, on hot reload (stable IDs, and what to invalidate when a dependency changes), and on parallel checking. Numbers marked "estimate" are my arithmetic, not measurements.

### 7.1 What runs compile-time code

| Option | Budget | Hot reload | Parallel checking | Precedent |
|---|---|---|---|---|
| **A1. Walk or copy the syntax tree** | Each operation allocates or copies nodes. On the same compiler and workload, Clang's tree walker was 6.6× slower than its bytecode on allocation (M16). C3 analyzes a loop body once per iteration. Fine for constants; a 64 KiB table costs seconds. | No IR shared with the backend, so a second engine has to agree with the first | Trees must be immutable, or copied per thread | D, C3, Clang's default evaluator |
| **A2. Interpret the typed flat IR, with values as bytes in the target layout, interning only values that escape into a type or the binary** | At [H9](../hypotheses.md)'s 10^8 operations per second, filling `kind2` (131,072 bytes, about 4 operations each) takes about 5 ms (estimate). Zig's interned values ran about 5×10^5 iterations per second. | A result is bytes plus the list of declarations and files it read. The memo key is (function's stable ID, argument bytes). | One interpreter state per thread. The memo table needs shards (Zig's `InternPool`) or per-thread tables merged in a fixed order. | Nim 2.4, rustc const eval, Clang's bytecode interpreter |
| **A3. Compile comptime functions with the debug backend and call them natively** | Fastest per operation, but every comptime function costs backend time. Host and target layouts differ when cross-compiling (wasm32 pointers are 4 bytes). | Same machinery as hot-reload patching | A crash in comptime code crashes the compiler unless isolated | None of the studied compilers, for general comptime |
| **A4. No comptime in the compiler: a generator program runs before the build** | Zero compiler cost. Generator time sits outside the 1M lines/s metric unless the metric counts it. | Generated files are ordinary sources, so reload works, but the generator must run again on every edit | None | Go `stringer`, Odin `core:rexcode`, Zig `build.zig` |

A2 is the only option that meets the budget and also serves the backend and hot reload. A4 stays useful for work that needs the outside world (see 7.3).

### 7.2 What compile-time code may produce

| Option | Budget | Hot reload | Parallel checking | Precedent |
|---|---|---|---|---|
| **B1. Values only** | Cheapest. The `TokenKind` enum must still be written by hand, as in Odin and C++26. | Values keyed by stable ID; easy | No effect on name lookup | Odin, C++ `constexpr`, Rust `const` |
| **B2. Values and types, but no named declarations** | One intern per created type | Types created at compile time need an identity that survives edits: (stable ID of the creating expression, IDs of the fields), never creation order. Zig's key uses its ZIR index, which needs `tracked_inst_map` to survive edits. | Types can be created on any thread if interning is thread-safe | Zig (`@Type`, `@Enum`), C++26 `define_aggregate` |
| **B3. B2, plus declarations whose names are declared before checking** | Names are registered in the declaration pass with every other name. The contents are generated when first needed. | A generated declaration's ID is (generator, name). It is invalidated when anything the generator read changes. | A lookup of name X waits only for the generator that declared X; no other lookup waits | Swift attached macros (SE-0389) |
| **B4. Any declarations, from text or syntax trees** | Generated code is parsed and checked again; M21–M24 show it is paid for twice | Names can appear and disappear between builds, so every failed lookup may need to run again | A failed lookup can't be reported until every generator in scope has run, so the checker must park lookups and retry (Jai's `TYPECHECKED_ALL_WE_CAN`) | D mixins, Nim macros, Jai, C3 `$expand`, Rust proc macros |

C3 shows that the pressure toward B4 is real: it moved from "no declarations from nowhere" to `$expand` in four years. B3 gives users generated declarations without giving up order-free, parallel lookup.

### 7.3 What compile-time code may touch

- **Pure computation.** Results can be memoized by (function ID, argument bytes, read set) and reused across builds and reloads. Every compiler that is deterministic by default starts here: Zig, D, Rust `const`, Carbon.
- **Reading files.** Record each file's content hash in the read set, as D's `-J` and Zig's `@embedFile` imply. Changing the file invalidates exactly the results that read it.
- **Running processes or calling C.** The result can't be cached unless the inputs are declared (Nim keys `staticExec` by command and input). The order of side effects depends on the order of checking. C3 gates this behind `--trust=full`, and gingerBill rejects it outright.
- **The cheapest rule** is pure computation plus declared file reads, with external tools run by the build program (A4).

### 7.4 How generics are checked

| Option | Budget | Hot reload | Parallel checking | Precedent |
|---|---|---|---|---|
| **G1. Per instance (compile-time duck typing)** | Checking costs Σ instances × body. Estimate: if generic bodies are 10% of lines and each has 5 instances on average, the checker does 1.4× the work (0.9 + 0.1 × 5). At H5's 3,000 cycles per line that adds 1,200 cycles per line, which runs in parallel. | An edit to a generic body invalidates all its instances. An edit to a type invalidates every instance that uses it. | Each instance is an independent job (Odin PR #7729) | Zig, Odin, C3, D, Nim, Jai |
| **G2. Once, against declared constraints** | The body is checked once. Each instance stores only its parameter-dependent values (Carbon's value blocks). The cost moves into the language: interfaces, impls and coherence rules. | An edit to a body re-checks one body and re-lowers its instances | Same as G1, with less work | Rust, Swift, Go, Carbon |
| **G3. G2 by default, G1 with a `template` keyword** | Two implementations to keep in step | Both | Both | Carbon |

G1 is what comptime-built types such as `MultiArrayList` need. A layout chosen by comptime code can't be checked against a constraint before the code runs.

### 7.5 How generic code is emitted

| Option | Budget | Hot reload | Parallel checking | Precedent |
|---|---|---|---|---|
| **E1. One copy per instance** | Backend cost and code size grow with instances, and H6's 1,000 cycles per line applies to instance lines too. M18 shows the usual fix: a small generic shell around a non-generic core. | Each instance is its own code block, with ID hash(generic's stable ID, argument type IDs), and is patched on its own | Per-function backend jobs | Everyone except Swift and Go |
| **E2. E1, plus merging identical lowered bodies** | One hash per lowered body. Bucket by hash; never compare all pairs, as Carbon does now. | Merged bodies share one slot, which must split if one instance's body later changes | Merge after lowering, in a fixed order | Carbon |
| **E3. One body per shape plus a dictionary, in debug builds only** | Fewer bodies, but slower calls and no inlining through the dictionary (Go #50182, M28) | Fewer blocks to patch | Needs a second calling convention and a differential test against E1 | Go; Carbon's stated goal for development builds |

### 7.6 How instances are identified

- **Key.** Intern (generic's stable ID, argument IDs). Arguments are type IDs or interned constant values, never raw bytes. The key is then one hash probe and one integer compare ([H10](../hypotheses.md)).
- **Avoid the slow lookups found:** Odin's, Nim's and C3's linear scans, and D's hashing of string arguments over every byte.
- **Canonicalize before lookup.** Zig's `ArrayList` maps natural alignment to `null`, Jai's `#modify` can rewrite `T`, and Haughton's D advice is to make instances identical (M11). Fewer distinct keys means fewer instances.
- **Fix the order.** Discover instances in parallel, then sort them by stable key before emitting, as rustc does. Output then does not depend on thread timing.
- **Make limits per call.** A compile-time step budget belongs to one evaluation and is never inherited from whichever caller came first (Zig #22410).

### 7.7 Reflection

| Option | Budget | Hot reload | Parallel checking | Precedent |
|---|---|---|---|---|
| **R1. Compile time only** | Zero bytes at run time, but code is generated per type, for example O(fields) compare code per type in Zig's JSON parser | Hot reload migration needs layouts at run time anyway ([H12](../hypotheses.md); earlier note §4.2) | None | Zig, D, C++26, Rust (planned) |
| **R2. A runtime table, for reached types only** | Bytes linear in types (gingerBill), and one routine serves every type | The table entries are exactly what migration needs: path hash, layout hash, field IDs, offsets | Emitted after checking, in stable-key order | Odin, Jai, C3, Go, Swift |
| **R3. Both, from the compiler's one type table** | Compile-time queries read the checker's records directly. The runtime table is a serialized subset. | Development builds always include layouts for retained types | Same as R2 | Odin comes closest (`type_*` intrinsics plus `Type_Info`) |

- **Type IDs.** Use hash(canonical path) for identity, plus a separate layout hash. Odin's path hash is stable but blind to layout. Rust's `TypeId` changes with the compiler version. Go has only pointer identity within one binary.
- **By-name dynamic lookups.** Keep them out of the language, or limit them to names known at compile time. One such call disables Go's method dead-code removal for the whole program.

### 7.8 A combination that fits the evidence

1. **A2:** one typed IR, interpreted with target-layout bytes, with budgets per call.
2. **B3** with **pure computation plus declared file reads.** External tools run from the build program.
3. **G1 for now.** Odin and Zig show it works for C-speed languages, and comptime-built types need it. Measure its amplification (C2) before considering G2.
4. **E1, with merging decided by measurement (C3).**
5. **Instances keyed by interned stable IDs,** discovered in parallel and sorted.
6. **R3:** one type table. Development builds always emit runtime layouts for retained types and `any`. Type IDs are path hashes plus layout hashes.

---

## 8. New testable hypotheses

These follow the format of [hypotheses.md](../hypotheses.md). The thresholds are proposals worked out from the budget and the evidence above.

| ID | Claim | Needs | Status |
|---|---|---|---|
| C1 | A comptime interpreter builds all lexer and parser tables in at most 10 ms | the H9 test IR | untested |
| C2 | Checking each generic instance separately re-checks at most 0.5 extra nodes per source node | generics, the corpus | untested |
| C3 | Merging identical instance bodies would save under 10% of debug machine code | generics, a backend | untested |
| C4 | Generated declarations whose names are declared up front keep checking 10× parallel | the checker, comptime | untested |
| C5 | Output and diagnostics don't depend on analysis order or thread count | the checker, comptime, generics | untested |
| C6 | Runtime type info for reached types costs at most 5% of a debug image | a backend | untested |
| C7 | Path-hash type IDs plus layout hashes detect every layout change and nothing else | the checker's type table | untested |
| C8 | After a one-function edit, at most 1% of comptime results and instances are redone | comptime, generics, incremental builds | untested |
| C9 | A format string can be checked at compile time with no code instance per string | comptime, the checker | untested |

### C1. A comptime interpreter builds all lexer and parser tables in at most 10 ms

- **Claim:** every table that `CHIBI_TOKENS` and `CHIBI_NODES` generate can be written as comptime code and run on the H9 interpreter in at most 10 ms on one core. That covers `kind2`, `kind_len`, `kind_names`, `keywords`, `prefix_nodes`, `binary_ops`, `assign_nodes`, `node_arity`, `list_starts`, `is_marker` and `node_labels`.
- **Why we think so:** `kind2` is 2 × 256 × 256 = 131,072 bytes. At about 4 IR operations per store and H9's 10^8 operations per second, that is about 5 ms. The other tables have fewer than 1,000 entries. At Zig's 2023 rate (#4055, about 1.9 µs per loop iteration) the same stores would take about 0.25 s. The value representation makes the difference.
- **Test:** write the table builders for the H9 test IR, run them, and compare the output bytes with the C tables. Report the time and the number of IR operations.
- **Passes if:** the total is at most 10 ms, and every byte matches.
- **If it fails:** count operations per store. Add a bulk fill operation to the IR. If that is not enough, compile comptime functions with the debug backend (option A3).
- **Status:** untested. Can test once the H9 test IR exists.

### C2. Checking each generic instance separately re-checks at most 0.5 extra nodes per source node

- **Claim:** with generics checked per instance (option G1), the checker analyzes at most 1.5 nodes per node of source on the corpus, counting every instance body.
- **Why we think so:** G1's cost is Σ instances × body size. Generic-heavy code can push that high: one dmd build ran about 50,800 instance analyses (M11), and `RawVec::grow` alone had 108 copies (M18). Game code written in Odin's style uses fewer generics, but nobody has measured it.
- **Test:** once generics exist, count the nodes checked, including instance bodies, and divide by the nodes in the source. List the 10 generics with the largest instances × body size.
- **Passes if:** the ratio is at most 1.5.
- **If it fails:** check the worst generics once against constraints (G2), or share bodies by shape in debug builds (E3). Add a compiler note that suggests splitting a generic shell from a non-generic core.
- **Status:** untested. Needs generics and the C-to-chibi corpus.

### C3. Merging identical instance bodies would save under 10% of debug machine code

- **Claim:** in a debug build of the corpus, lowered instance bodies that are identical make up under 10% of machine-code bytes. Identical means equal after callee IDs are made canonical.
- **Why we think so:** Carbon built merging but did not measure it. Rust's sharing of generics across crates changed binary size by −0.5% on primary benchmarks and −7.1% on secondary ones (M19), through a different mechanism.
- **Test:** hash each lowered instance body twice, as Carbon does: once over its own instructions and once including the IDs of the instances it calls. Count the bytes in bodies whose hashes repeat.
- **Passes if:** duplicates are under 10% of machine-code bytes. Then skip merging.
- **If it fails:** merge bodies by hash bucket (never pairwise) and give merged bodies one slot.
- **Status:** untested. Needs generics and a backend.

### C4. Generated declarations whose names are declared up front keep checking 10× parallel

- **Claim:** if every comptime generator declares the names it will produce before checking starts (option B3), the checker still runs at least 10× faster on 14 threads. That holds on a corpus where 5% of declarations are generated, and the speedup stays within 10% of the same corpus written by hand.
- **Why we think so:** under Swift's rule, name lookup expands only the generators that could produce the name it wants (SE-0389). D and Jai add names in the middle of checking, so they need forward-reference rules or lookups that park and retry.
- **Test:** generate a pair of corpora. In one, comptime code generates per-enum name tables and per-struct serializers. In the other, the same declarations are written out. Run the checker on 1 and 14 threads on both.
- **Passes if:** both speed up at least 10×, and the two speedups differ by at most 10%.
- **If it fails:** run generators in a separate phase per package before checking, or let generated names be visible only inside the generator's own package.
- **Status:** untested. Needs the checker and comptime.

### C5. Output and diagnostics don't depend on analysis order or thread count

- **Claim:** if comptime budgets are charged per evaluation, and instances and generated declarations are ordered by stable keys, then the compiler produces byte-identical output and diagnostics over 100 runs. Each run claims declarations in a different random order, on 1 to 14 threads.
- **Why we think so:** Zig's generic instances inherit the step budget of whichever caller is analyzed first, which makes results depend on order and blocks a parallel `Sema` (#22410). rustc sorts instances by stable hash after finding them in parallel. C3's docs admit that top-level evaluation order across modules is not deterministic.
- **Test:** seed the order in which threads claim declarations. Hash the output image and the diagnostic text for each run.
- **Passes if:** every hash is identical.
- **If it fails:** find the state that depends on order: memo entries, budgets, error deduplication, or instance numbering. Key it by stable ID.
- **Status:** untested. Needs the checker, comptime and generics.

### C6. Runtime type info for reached types costs at most 5% of a debug image

- **Claim:** a runtime type table costs at most 5% of the debug image. The table holds names, sizes, field offsets, field IDs and layout hashes, for every type reached through `any`, reflection or retained state.
- **Why we think so:** gingerBill argues the table grows linearly with the number of types, and Odin emits entries only for types the program reaches. No primary source measures the table alone; M10 mixes it with `fmt`. Hot reload needs the same data to migrate layouts (H12).
- **Test:** build the corpus with and without the table. Report the difference in image size, and the bytes per type and per field.
- **Passes if:** the table adds at most 5%.
- **If it fails:** move names into a separate section that only tools load, and keep IDs, offsets and layout hashes in the image.
- **Status:** untested. Needs a backend.

### C7. Path-hash type IDs plus layout hashes detect every layout change and nothing else

- **Claim:** take a type ID that is a hash of the type's canonical path, and a layout hash over field names, field type IDs, offsets, size and alignment, recursive through fields stored by value. Across a scripted series of edits, the ID never changes while the path stays the same, and the layout hash changes exactly when the layout changes.
- **Why we think so:** Odin's `typeid` is stable across builds but ignores layout (earlier note §3.2). Rust's `TypeId` changes with the compiler version. Go has no ID that survives a rebuild.
- **Test:** script 200 edits: rename, add, reorder and retype fields; change a nested type stored by value; edit method bodies; edit unrelated code; rename the type. Compare IDs and layout hashes before and after each edit.
- **Passes if:** there are no missed layout changes and no false alarms.
- **If it fails:** add whatever changed without being hashed to the layout hash.
- **Status:** untested. Needs the checker's type table.

### C8. After a one-function edit, at most 1% of comptime results and instances are redone

- **Claim:** if each memoized comptime call and each generic instance records the declarations, namespaces and files it read, then a one-function edit re-evaluates at most 1% of them on the corpus. The result is identical to a clean build.
- **Why we think so:** Zig turned memoization off under incremental builds because callers do not record what they depend on (earlier note §1.2). rustc keeps instance code apart from stable code (§3.6). Nim records body dependencies for incremental compilation when it generates VM code.
- **Test:** apply 100 one-function edits to the corpus. Count the comptime calls and instances evaluated again, and compare each output with a clean build.
- **Passes if:** on average at most 1% are redone, and every output matches the clean build.
- **If it fails:** invalidate per declaration instead, and measure what that costs.
- **Status:** untested. Needs comptime, generics and incremental builds.

### C9. A format string can be checked at compile time with no code instance per string

- **Claim:** a call such as `print("x={} y={}", x, y)` can be validated at compile time and lowered to a call that takes a table of the string's pieces. No function instance is created per distinct string, and checking costs at most 1,000 cycles per call site.
- **Why we think so:** Zig creates one function instance per distinct comptime format string (§3.1). D runs its whole runtime formatter in the CTFE interpreter, once per (string, argument types) (§3.4). Rust needs a compiler built-in and a dedicated syntax node to avoid both (§3.6).
- **Test:** make a corpus with 10,000 `print` calls using 1,000 distinct strings. Measure checking cycles per call site and count the code instances created.
- **Passes if:** checking costs at most 1,000 cycles per call site, and no instances are created per string.
- **If it fails:** make `print` a compiler built-in, as Rust does.
- **Status:** untested. Needs comptime and the checker.

---

## 9. Questions for the owner

Ordered by what they block. The first four decide how the checker and IR are built, the next thing after the parser.

1. **May compile-time code create named declarations?** If yes, must every generator declare its names before checking starts (B3, Swift's rule)? This decides whether the declaration pass can register every name before checking, and so whether lookups ever wait.
2. **Are generic bodies checked per instance or against declared constraints?** Per instance needs no interfaces and allows types built at compile time. Constraints need interfaces and impls, and check a body once. This decides whether instances copy bodies or store value blocks.
3. **May compile-time code create types (structs, enums, unions), and may those types have methods?** This decides the identity key of a created type. It also decides whether struct-of-arrays is a library (`MultiArrayList`) or a language feature (`#soa`).
4. **Is the compile-time engine the IR interpreter with target-layout bytes (A2)?** What happens when host and target layouts differ, for example 4-byte pointers on wasm32?
5. **What may compile-time code touch:** pure computation, declared file reads, running processes, C calls? This decides the cache key and whether results can be reused across builds and reloads.
6. **Does a shipped build carry runtime type info: full, IDs only, or none? Is `any` a built-in type?** This decides how `print` works and what the backend emits.
7. **Is a generator program that runs before the build acceptable** for jobs that need the outside world, so that comptime can stay pure?
8. **Should debug builds share generic code by shape** (E3) to save compile time, at the cost of a second calling convention?
9. **Is `print` checked at compile time, and is it a built-in or library code** (C9)?

---

## 10. Not verified

- **Jai:** every detail beyond video titles and dates. The sources are community docs and third-party timestamps. That includes the bytecode interpreter, the message loop API, `#modify`, `#bake_arguments`, the `Type_Info` opt-outs, and the polymorph report in the 2025 keynote.
- **gingerBill's January 2025 X post:** the text came from a search index, because the page did not load.
- **Zig:**
  - whether `Sema` is still single-threaded in 0.16 and 0.17; inferred from 0.15.2 and the release notes;
  - whether the self-hosted backends also emit tag-name functions lazily;
  - whether an error inside a generic body is reported once per instance;
  - hardware for every Zig number.
- **Odin:**
  - that `-no-rtti` breaks growing `#soa[dynamic]` arrays (inferred from code);
  - whether the range loop over enum values in the Odin rewrite compiles.
- **D:**
  - Walter Bright's 21.56 s → 12.19 s figure (Dr. Dobb's was unreachable);
  - the dmd version behind Alexandrescu's 2010 number;
  - that `format!` results are memoized through the template cache (inferred).
- **C++:**
  - whether `extract<T>` works directly on an annotation;
  - whether `if constexpr` discards inside `template for`;
  - upstream Clang support for P2996 at 23.1.5;
  - that format-string checks are not memoized across call sites (inferred).
- **Rust:**
  - that coherence is what makes sharing instances across crates sound (inferred);
  - dtolnay's before/after timing for precompiled `serde_derive` (only a screenshot).
- **Swift:** no primary numbers exist for the run-time cost of witness tables or the compile-time cost of specialization. All Swift numbers are third party.
- **Go:**
  - that every importing package re-instantiates the same generic (inferred from `DUPOK`);
  - that no type ID is stable across builds (inferred from code).
- **Nim:**
  - whether the manual's restrictions on compile-time code still hold after the VM rewrite;
  - whether M33 includes the C compile.
- **C3:**
  - which object file receives instance code;
  - whether `typeid` records are emitted only for reachable types;
  - the cost of large compile-time arrays;
  - the `$for` header syntax in the rewrite.
- **Carbon:** whether calls evaluated at compile time are memoized, and every talk-based performance claim.
- **Not researched:** type ID stability in D, C++, Nim and Swift.
- **Not compiled:** every code example in this note.

---

## 11. Code and docs to read first

1. **[Zig `Sema.zig` L2987-L3010](https://github.com/ziglang/zig/blob/0.15.2/src/Sema.zig#L2987-L3010), [`InternPool.zig` L3115-L3137](https://github.com/ziglang/zig/blob/0.15.2/src/InternPool.zig#L3115-L3137) and [`Zir.zig` L3390-L3411](https://github.com/ziglang/zig/blob/0.15.2/lib/std/zig/Zir.zig#L3390-L3411).** How a type returned by a generic function gets its identity from (declaration, captured values), and how built types are keyed by hash. This is the model for chibi-v2's generic types.
2. **[Zig `InternPool.zig` L1029-L1078, L4785-L4835 and L7684-L7785](https://github.com/ziglang/zig/blob/0.15.2/src/InternPool.zig#L7684-L7785).** A thread-safe intern table: per-thread lists, the thread ID in the index's top bits, sharded maps, lock-free probing. Read it before designing chibi-v2's type and instance tables.
3. **[Zig `multi_array_list.zig`](https://github.com/ziglang/zig/blob/0.15.2/lib/std/multi_array_list.zig) and [`Io/Writer.zig` L593-L720](https://github.com/ziglang/zig/blob/0.15.2/lib/std/Io/Writer.zig#L593-L720).** Two pieces of comptime code that real programs depend on: a struct-of-arrays type built from reflection, and format-string checking that emits straight-line code.
4. **[Zig #22410](https://github.com/ziglang/zig/issues/22410).** How one small rule, budgets inherited from the first caller, blocks a parallel checker.
5. **[Carbon `sem_ir/generic.h`](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/sem_ir/generic.h#L14-L147), [`sem_ir/generic.cpp` L13-L57](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/sem_ir/generic.cpp#L13-L57) and [`coalesce_generic_lowering.md`](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/docs/coalesce_generic_lowering.md).** One generic body plus a value block per instance, instances deduplicated by ID, and identical bodies merged at lowering.
6. **[Odin `check_expr.cpp` L721-L1139](https://github.com/odin-lang/Odin/blob/dev-2026-11a/src/check_expr.cpp#L721-L1139) and [`checker.cpp` L6918-L6980](https://github.com/odin-lang/Odin/blob/dev-2026-11a/src/checker.cpp#L6918-L6980).** Per-instance copying of the syntax tree, a two-level instance cache, and instantiation traces in errors: the simple design, with its costs visible. Then read [PR #7886](https://github.com/odin-lang/Odin/pull/7886) for what it cost in memory.
7. **[Odin `check_type.cpp` L4747-L4900](https://github.com/odin-lang/Odin/blob/dev-2026-11a/src/check_type.cpp#L4747-L4900).** `#soa` as a struct type the compiler builds. Compare it with item 3 to decide between a language feature and a comptime library.
8. **[rustc `collector.rs` L1-L195 and L1914-L1957](https://github.com/rust-lang/rust/blob/1.99.1/compiler/rustc_monomorphize/src/collector.rs#L1-L195) and [`partitioning.rs` L1-L71](https://github.com/rust-lang/rust/blob/1.99.1/compiler/rustc_monomorphize/src/partitioning.rs#L1-L71).** Finding instances in parallel and then sorting them, and keeping volatile instance code apart from stable code.
9. **[Nim `vmdef.nim` L10-L76](https://github.com/nim-lang/Nim/blob/v2.4.0/compiler/vmdef.nim#L10-L76) and [PR #26323](https://github.com/nim-lang/Nim/pull/26323).** A compile-time VM that keeps values in target layout: instruction format, register slots, memory management. It is the closest existing design to H9.
10. **[Clang `ConstantInterpreter.rst`](https://github.com/llvm/llvm-project/blob/llvmorg-23.1.5/clang/docs/ConstantInterpreter.rst#L11-L51) and [Bäder 2025](https://developers.redhat.com/articles/2025/10/15/clang-bytecode-interpreter-update).** Why Clang is replacing its tree-walking evaluator, with numbers from one compiler and one workload.
11. **[P2996R13 §3.6 and §3.10](https://isocpp.org/files/papers/P2996R13.html#enum-to-string) with [Romeo's measurements](https://vittorioromeo.com/index/blog/refl_enum_to_string.html).** Reflection-based enum names and struct-of-arrays in C++26, and what they cost next to an X-macro.
12. **[SE-0389](https://github.com/swiftlang/swift-evolution/blob/de9bd49899f3386e63c7c2c88d8d73ed9d22ef38/proposals/0389-attached-macros.md#L509-L623).** The declared-names rule for generated declarations (option B3).
13. **[dmd `templatesem.d` L600-L711 and L3338-L3489](https://github.com/dlang/dmd/blob/v2.114.0/compiler/src/dmd/templatesem.d#L600-L711) and [Phobos `format/package.d` L735-L748](https://github.com/dlang/phobos/blob/v2.114.0/std/format/package.d#L735-L748).** Hashing instance keys by content, choosing which object file emits an instance, and a formatter that runs at compile time.
14. **[Go `reader.go` L891-L969 and L1505-L1538](https://github.com/golang/go/blob/go1.26.0/src/cmd/compile/internal/noder/reader.go#L891-L969) and [`deadcode.go` L141-L143](https://github.com/golang/go/blob/go1.26.0/src/cmd/link/internal/ld/deadcode.go#L141-L143).** Shapes, dictionary layout, and how one reflection call disables dead-method removal.
15. **[Pestov, *Compiling Swift Generics*, introduction](https://github.com/swiftlang/swift/blob/swift-6.4.2-RELEASE/docs/Generics/chapters/introduction.tex#L7-L288).** Why a language would pay for dictionaries: separate compilation and library evolution. It is the clearest statement of the other side of the trade-off.
16. **[gingerBill, "CTTI is Exponential, RTTI is Linear"](https://www.gingerbill.org/article/2026/09/02/ctti-is-exponential-rtti-is-linear/) and [Lernö, "The downsides of compile time evaluation"](https://c3.handmade.network/blog/p/8590-the_downsides_of_compile_time_evaluation).** The two strongest arguments for keeping comptime small, from authors of C-speed languages. Read them last, against the evidence above.
