# Research notes

Written 2026-11-09 from primary sources: official docs, the projects' own code at pinned commits, and talks or posts by their authors. Each note lists what it could not verify.

| Note | Question |
|---|---|
| [fast-compilers.md](fast-compilers.md) | How the fastest compilers produce runnable code. tcc in depth, then Jai, Zig, Go, Carbon, Pascal/Delphi, QBE, MIR, TPDE, Cranelift, V, Odin, C3 and mold |
| [memory-models.md](memory-models.md) | Memory models for games, hot reload and coroutines. Goose, Zen C, reference counting, arenas, handles, and the collectors games use |
| [packages.md](packages.md) | Packages in Carbon, Zen C, C3 and Zig, contrasted with Odin, Go and Jai |
| [metaprogramming-reload-coroutines.md](metaprogramming-reload-coroutines.md) | How compilers implement compile-time execution, generics, reflection, hot reload and coroutines, and what each costs |
| [comptime-generics-reflection.md](comptime-generics-reflection.md) | Compile-time execution, generics and reflection in depth, across 11 languages, with `CHIBI_TOKENS` rewritten in six of them |
| [front-end-optimizations.md](front-end-optimizations.md) | A profile of chibi-v2's own lexer and parser, with ranked optimizations and the grammar choices that change their cost |
| [c-corpus.md](c-corpus.md) | Building a benchmark corpus by translating C into chibi, Go 1.5's c2go as the precedent, and the syntax decisions it needs |
| [syntax-options-data.md](syntax-options-data.md) | Option sheets 1–3: unions, casts and conversions, size and type queries. Sheet 1 was decided on 2026-11-11 and sheet 2 on 2026-11-10; see the Unions and Casts rows of [../decisions.md](../decisions.md) |
| [syntax-options-effects.md](syntax-options-effects.md) | Option sheets 4, 9 and 10: what translating C needs (`++`, ternary, character literals, `goto`, varargs, bitfields), tasks, and C interop details |
| [syntax-options-abstraction.md](syntax-options-abstraction.md) | Option sheets 5–8: methods, generics, compile-time code and reflection, and how they fit together. Sheet 5 was decided on 2026-11-11 (see the Methods row of [../decisions.md](../decisions.md)), and sheet 6 now also has to settle how generics interact with overloading |
| [adopt-or-settle.md](adopt-or-settle.md) | Ideas from other languages, ranked by the cost of deciding late: overflow, integer types, errors, pointers, initialization and more |
| [shader-languages.md](shader-languages.md) | Compiling chibi for the GPU: what shader languages have, what GPUs forbid, the changes to adopt-or-settle ranks 1–3, types narrower than 32 bits, and emitting SPIR-V for SDL3 and Cute Framework. Updated 2026-11-19 to match the decisions made since |
| [shader-backends.md](shader-backends.md) | How 16 compilers and engine pipelines produce GPU code, their published speeds, what the IR must keep for GPU writers, and whether shader-languages.md section 6 holds |
| [checker-and-ir.md](checker-and-ir.md) | The checker and IR design sheet: what the checker reads, names, checking on many threads, constants and literals, the typed IR, and guaranteed tail calls and computed `goto` for fast interpreters, with measurements on the owner's M4 Pro |

## The shape the four notes agree on

The points below are hypotheses until they are measured on chibi-v2. Testable versions, with the measurements that would confirm or reject them, are in [../hypotheses.md](../hypotheses.md).

1. **A data-oriented front end that runs in parallel.**
   - Lex and parse each file into flat arrays.
   - Register every top-level name first, so declarations can appear in any order.
   - Check declarations on demand, claiming each one with an atomic not-started / in-progress / done mark. Odin made its checker fully parallel this way in October 2026.
2. **One typed, flat IR per function.** It is the only input to the compile-time interpreter, the debug backend and any later release backend. Zig, Carbon and rustc all interpret their typed IR at compile time.
3. **Compile-time memory is plain bytes in the target layout.** Values are interned only when they escape into a type or into the binary. Zig's comptime is slow (189 s for a 100M-iteration loop in 2023) because every value is interned.
4. **The debug backend runs one pass per function, in the style of tcc and TPDE, on all cores.**
   - It writes an in-memory image, or its own Mach-O/ELF/PE output.
   - Each function is a code block that keeps its relocations.
   - Development builds call every function through a slot table, which is what makes hot reload possible.
   - LLVM is used only for release builds, if at all.
5. **Coroutines are stackless.** The front end lowers them into state-machine records in the IR. Each suspension point has a stable ID, so a suspended frame can be mapped onto new code after a reload.
6. **Stable IDs come from source identity, not creation order.**
   - Types are identified by path plus a layout hash.
   - Fields by name.
   - Functions by path.
   - Each comptime result and generic instance records what it read, so caching and reload invalidation are one mechanism. Zig had to turn off memoization under incremental builds because it lacked this.
7. **Memory design A:**
   - typed pools addressed by generational handles;
   - arenas scoped to a frame, a level or the whole program;
   - a "persistable" rule: pools, globals and coroutine frames may hold no raw pointers.

   This lets reload find and migrate every live object of a changed type, with almost no compiler analysis. A managed tier collected only at frame boundaries (design C) is the fallback if handles prove too restrictive.

## What the evidence says about 1M lines/s

- **No compiler has published a full build at 1M lines/s of code that generates instructions.**
  - tcc's 859k lines/s (Pentium 4, 2004) includes linking, but about 96% of those lines are repeated header declarations.
  - Carbon's front end reaches about 1M lines/s per core through type checking.
  - TPDE's backend is 12–19x faster than LLVM -O0.
  - Nobody has combined these with a fast linker and published the result.
- **Parallelism is the lever.** v2's lexer scaled 10.8x on 14 threads (1,356 to 14,673 MB/s, measured). If every phase scales like that, each core needs only about 93k lines/s, about 42,000 cycles per line at the 3.9 GHz the lexer benchmark implies. Single-threaded, the budget is about 3,900 cycles per line.
- **The risk is serial phases.** Zig's semantic analysis and Go's type checking are serial, and so is linking unless it is designed otherwise.

## Decisions, merged from the four notes and ordered by what they block

The owner's answers so far are in [../decisions.md](../decisions.md).

1. **The metric.**
   - Does it count unique physical lines?
   - Is it a cold or a warm build?
   - Does it include linking or reaching runnable code?
   - On which machine?
2. **Memory.**
   - May long-lived data (pools, globals, coroutine frames) refer to other data only through handles or offsets? That choice decides between design A and design C.
   - Must use-after-free be impossible in release builds, or only detected in debug builds?
   - Will C code (Cute Framework, SDL3) hold pointers into memory the language owns?
3. **Tasks.**
   - Stackless or stackful? Only stackless frames can be migrated, and only they work on wasm.
   - Do tasks run in parallel on job threads, or cooperatively on one thread?
   - When a suspended task's code changes, does it migrate, finish in the old code, or restart?
4. **Compile-time reach.**
   - Pure computation only, or also I/O and C calls?
   - May it generate declarations, and into other packages?
   - Is the build itself a program in the language?
5. **Generics.** Is each instance checked separately (Zig, Jai, Odin), or are constraints checked once at the definition (Carbon)?
6. **Reflection in shipped builds.** Full, IDs only, or off?
7. **Hot reload.**
   - Which edits must patch live: function bodies, new functions, layout changes, or coroutine bodies with live frames?
   - What is the policy for layout changes: match fields by name, zero new fields, and run a user hook?
   - On which platforms must it work?
8. **Packages.**
   - Is a package exactly one directory?
   - Are import cycles between packages forbidden?
   - Is unused code checked, or only what the roots reach?
   - Is the default visibility private, with `pub` to export?
   - Is the standard library compiled from source on every build, or loaded from a cache?
9. **Targets.** Is WebAssembly a target? Must the game run on platforms that forbid writable code?

## Next step

Updated 2026-11-11. The lexer and parser exist. Between them and an interpreter, nothing exists yet: name scopes, the checker and the typed IR. No open syntax question blocks a core subset (integers, floats, structs, arrays, pointers, functions, control flow, errors, optionals, unions).
1. **The checker and IR design sheet**, [checker-and-ir.md](checker-and-ir.md), written 2026-11-12, which the owner reads before any checker code. The research thread wrote it, and then writes the checker in new files (see the Process rows of [../decisions.md](../decisions.md)). Its section 9 lists the questions to answer first.
2. **Inputs to the IR**, because the owner wants speed features considered early: guaranteed tail calls and computed `goto`, for fast interpreters. Section 6 of the sheet covers both, with H9's interpreter measured under four dispatch styles.
3. **Then:** the checker on the core subset, the typed IR, and bytecode lowered from it for the interpreter (H9 tested that design on small loops).

The 2026-11-09 plan, kept for reference: read items 1–8 of the reading list in [fast-compilers.md](fast-compilers.md#9-code-to-read-first), the whole of tcc's design ending in `arm64-gen.c`, then build a spike:
- a per-function IR for integers, branches and calls;
- a one-pass arm64 emitter writing into `MAP_JIT` memory;
- calls through a slot table.

Measure cycles per IR instruction, and swap one slot while the program runs. That one spike tests the backend budget and the hot reload design together.

## Syntax option sheets: how they fit together (2026-11-10)

The four notes above were written in parallel, then checked against each other and against [../decisions.md](../decisions.md). They agree on these points:

- **Markers:** `$` marks compile-time code (sheet 7), `@` marks attributes such as `@test` and `@inline` ([adopt-or-settle.md](adopt-or-settle.md), section 3.8), and queries such as `size_of`, `type_of` and `type_info` are plain predeclared names (sheets 3 and 8).
- **`?` stays free for optionals.** Sheet 4 chooses `if c then a else b` over `c ? a : b`, so `?T`, `??` and `.?` (adopt-or-settle 3.1) don't collide with a ternary.
- **Calls:** every chibi function uses the C calling convention (sheet 10 and adopt-or-settle 2.7). The implicit context is a thread-local, saved when a task switches (sheet 9).
- **Labels:** both sheet 4 and adopt-or-settle 3.7 propose `outer: for` with `break outer`. The parser tells a label from the declaration `x: T` by the token after the colon.

These points need the owner's decision, because the notes either answer an open question or differ from an earlier note:

1. **`const` means compile-time only.** Decided 2026-11-10: a `const` is known at compile time and invariant.
2. **Opting out of zero initialization:** decided 2026-11-10: `x: T = undefined`, allowed only as an initializer. It was `---` for a few hours, until the owner switched back in the parser thread.
3. **Unwrapping an optional:** decided 2026-11-10 in the parser thread, after its survey: postfix `x!!` traps when empty, `a ?? b` and `x ??= y`, and a `!= nil` check narrows, as in Kotlin. `a?.b` and `a ?: b` are left out until real code needs them (see the Optionals row of decisions.md).
4. **Default float type:** decided 2026-11-10: `x := 1.5` is an `f32`.

Keywords and tokens proposed across the sheets:

| Change | Words or tokens | Source |
|---|---|---|
| Add | `then`, `extern`, `await`, `task` (only before `fn`), `undefined`. `bitcast` decided 2026-11-10 (sheet 2). The reserved `is` tests a tagged union's tag, as in `e is .key` (sheet 1, decided 2026-11-11). `catch` and `try` decided 2026-11-10: a named error is written `catch err => …`, and `try` goes before each call that can fail, as in Zig (decided 2026-11-11, replacing Swift's one `try` per expression). Error-only cleanup is `defer catch` (decided 2026-11-11), so `errdefer` isn't added | sheets 4, 9 and 10; adopt-or-settle |
| Remove | `as` and `cast`, decided 2026-11-10 (sheet 2). `type`, `sizeof`, `alignof`, `offsetof` and `typeof`, decided 2026-11-11 in the parser thread's token audit, until sheets 6–8 decide how code asks for sizes and types. `case`, `default`, `goto` and `yield`, decided later the same day; the coroutines sheet picks its own keyword. `::` and `?=` stay lexer tokens that the parser rejects with a hint, and `&&` and `\|\|` stay as tokens that only give hints | sheets 2, 3 and 4; token audit |
| New tokens | decided 2026-11-10: `+%`, `-%`, `*%`, `+%=`, `-%=`, `*%=` and prefix `-%`; postfix `!!`, `??` and `??=`; ranges `..` and `..=` (the `..<` token was removed on 2026-11-11, so `0..<n` fails at the `<`); `\|` combines error sets. `?.` and `?:` are left out until real code needs them. Still proposed: `[*]` | adopt-or-settle 2.1, 3.1 and 2.5 |
