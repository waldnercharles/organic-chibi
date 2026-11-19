# Compile-time execution, generics, reflection, hot reload and tasks in fast compilers

Research date: 2026-11-09. Sources are primary unless marked. Code references are pinned to a tag or commit.

## Summary

- Every compiler studied runs compile-time code by interpreting a typed IR inside the type checker: Zig interprets ZIR in `Sema`, Carbon executes SemIR, rustc interprets MIR, D walks its AST. None JIT-compiles. Jai's bytecode interpreter is reported only in talks (unverified).
- The value representation decides the speed of compile-time code. Zig measured 189 s for a 100M-iteration comptime loop against 6 s in CPython, using 14 GiB, because comptime values go through the interned pool. D's AST interpreter allocates AST nodes per expression.
- Compile-time memoization and incremental compilation conflict. Zig turns comptime call memoization off under `-fincremental` because callers do not record the dependency.
- The C-speed languages (Zig, Odin, C3, Rust, and reportedly Jai) monomorphize generics. They deduplicate instances by interning (generic declaration, compile-time arguments). Go, .NET and Swift share code through dictionaries and pay at run time. Carbon type-checks a generic once, keeps one body, and coalesces identical specifics when it lowers to LLVM IR.
- Compile-time reflection costs nothing at run time. Runtime type tables cost binary size per emitted type. Odin's `typeid` is a hash of the type's canonical name, so it is stable across rebuilds but does not change when the layout changes.
- Every working hot reload system keeps state outside the code, calls through an indirection, applies changes at a safe point, and keeps old code alive while frames still use it. None of the native systems (Visual Studio EnC, Live++, Handmade Hero, Karl Zylinski's Odin template) migrate struct layouts by themselves. Erlang (`code_change`) and Common Lisp (instance update on class redefinition) do.
- Suspended stackful tasks hold raw return addresses into old code, so they can only finish in old code. A stackless frame is a record holding a state index, so the compiler can map it to new code. Roslyn does this for C# async methods.
- For chibi-v2, own the coroutine lowering in your IR. Emit stable IDs for types, fields, functions and suspension points. Feed one IR to the interpreter and to both backends.

## How to read this

- "Measured" means the number appears in the source together with its workload. Hardware is given when the source states it.
- "Unverified" means I found only a talk title, a secondary summary, or no primary source.
- Zig moved its repository to Codeberg in November 2025, and the latest release is 0.17.1 (2026-11-02). Code line references use the GitHub tag `0.15.2`, which still exists at <https://github.com/ziglang/zig>. Release-note claims use 0.16.0 and 0.17.0.

---

## 1. Compile-time execution

### 1.1 Comparison

| Language | What runs at compile time | Engine | Memory model | Limits | Speed evidence |
|---|---|---|---|---|---|
| Zig | Any function in a `comptime` context; generic instantiation; type construction | `Sema` interprets ZIR while it produces AIR ([Sema.zig L1-L6](https://github.com/ziglang/zig/blob/0.15.2/src/Sema.zig#L1-L6), [analyzeBodyInner L1118](https://github.com/ziglang/zig/blob/0.15.2/src/Sema.zig#L1118-L1135)) | Mutable comptime allocations held per `Sema` ([L164-L191](https://github.com/ziglang/zig/blob/0.15.2/src/Sema.zig#L164-L191)); results interned in `InternPool`; pointers are a base plus a byte offset for well-defined layouts ([PR #19630](https://github.com/ziglang/zig/pull/19630)) | Backward-branch quota 1000 by default ([L193](https://github.com/ziglang/zig/blob/0.15.2/src/Sema.zig#L193), [langref](https://github.com/ziglang/zig/blob/0.15.2/doc/langref.html.in#L4028-L4036)); no calls to extern functions ([L7674-L7685](https://github.com/ziglang/zig/blob/0.15.2/src/Sema.zig#L7674-L7685)); comptime-var pointers may not become runtime-known or sit in a global's value ([0.12.0 notes, "Comptime Memory Changes"](https://ziglang.org/download/0.12.0/release-notes.html)) | Slow; see 1.3 |
| Jai | `#run` runs any procedure; a user metaprogram drives the compiler through a message loop | Bytecode interpreter (Blow, [2014 demo](https://www.youtube.com/watch?v=UTqZNujQOlA)) | Unverified | Unverified | Unverified. The 2014 demo shows a game running at compile time. |
| D | Functions called where a constant is needed (`enum`, `static if`, template arguments) ([spec](https://dlang.org/spec/function.html#interpretation)) | AST-walking `Interpreter : Visitor` ([dinterpret.d L1691](https://github.com/dlang/dmd/blob/v2.114.0/compiler/src/dmd/dinterpret.d#L1691)) | Values are AST literal nodes in a region that is released after each top-level evaluation ([L67-L127](https://github.com/dlang/dmd/blob/v2.114.0/compiler/src/dmd/dinterpret.d#L67-L127)) | No mutable globals, no inline asm, no non-portable casts ([spec](https://dlang.org/spec/function.html#interpretation)); recursion limit 1000 ([L255](https://github.com/dlang/dmd/blob/v2.114.0/compiler/src/dmd/dinterpret.d#L255)) | The spec says CTFE "can take considerably longer than executing it at run time" |
| Odin | Constant expressions, `when`, `#assert`, `#load`; no general execution | Constant folding in the checker | n/a | By design ([gingerBill, 2018](https://github.com/odin-lang/Odin/issues/64#issuecomment-430345024)) | n/a |
| C3 | Macros with `$if`, `$for`, `$foreach`, `$switch`, compile-time variables ([macros](https://c3-lang.org/generic-programming/macros/), [compile time](https://c3-lang.org/generic-programming/compiletime/)) | Macro expansion plus constant folding; the docs do not describe running ordinary functions | Compile-time variables, local scope only | Recursion limited by the `macro-recursion-depth` setting; mutable compile-time variables are not allowed at global scope | Not documented |
| Carbon | `eval fn` and `musteval fn` ([functions.md L644-L646](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/docs/design/functions.md#L644-L646)) | `FunctionExecContext` "sequentially interprets instructions" of SemIR ([eval.cpp L3516-L3519](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/check/eval.cpp#L3516-L3519), [L3847-L3851](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/check/eval.cpp#L3847-L3851)) | Locals map instruction IDs to `ConstantId` ([L62-L75](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/check/eval.cpp#L62-L75)); side effects and mutable state come "eventually" | Experimental toolchain | Not measured |
| Rust (reference) | `const` items, `const fn` | Interpreter over MIR, "a virtual machine for executing MIR" ([dev guide](https://rustc-dev-guide.rust-lang.org/const-eval/interpret.html)) | Each allocation is a byte array; pointers are (`AllocId`, offset) with relocations kept beside the bytes | Query-cached: "Future evaluations of the same constants will not actually invoke the interpreter" | Not measured |

None of these engines JIT-compiles. D tried a bytecode engine (newCTFE, see 1.2), and an LLVM back end for it was only "almost working" in 2016 ([D blog](https://blog.dlang.org/archive/2017/04/10/the-new-ctfe-engine/)).

### 1.2 Notes per language

**Zig.**
- Comptime is not a separate pass. `Sema` evaluates comptime blocks while it type-checks, and the results become interned values.
- Container-level declarations are "order-independent and lazily analyzed" ([langref L991](https://github.com/ziglang/zig/blob/0.15.2/doc/langref.html.in#L991)). Zig 0.16 also resolves fields lazily: a type used only as a namespace has its fields resolved only when its size or a field type is needed ([0.16.0 notes, "Lazy Field Analysis"](https://ziglang.org/download/0.16.0/release-notes.html)).
- Generics are comptime functions: "Compile-time parameters is how Zig implements generics. It is compile-time duck typing" ([langref L3863](https://github.com/ziglang/zig/blob/0.15.2/doc/langref.html.in#L3860-L3890)).
- Memoized comptime calls are keyed by function and argument values in the `InternPool`. Memoization is turned off when `zcu.comp.incremental` is set ([Sema.zig L7707-L7745](https://github.com/ziglang/zig/blob/0.15.2/src/Sema.zig#L7707-L7745)). The source comment reads: "comptime call memoization is currently not supported under incremental compilation since dependencies are not marked on callers".
- Andrew Kelley's diagnosis of the slowness: "we need to model comptime-mutable memory as actual mutable memory, rather than tossing everything into the garbage-collected InternPool" ([#4055 comment, 2023-07-23](https://github.com/ziglang/zig/issues/4055#issuecomment-1646701374)). Issue #4055 sets the goal as roughly CPython speed. It is still open.

**Jai (unverified).**
- [Demo: Base language, compile-time execution (2014)](https://www.youtube.com/watch?v=UTqZNujQOlA) runs a Space Invaders game at compile time with `#run` on a bytecode interpreter.
- [Jai Demo and Design Explanation (LambdaConf 2025 keynote)](https://www.youtube.com/watch?v=IdpD5QIVOKQ) describes workspaces and a user-replaceable metaprogram that controls compilation.
- Community docs name the API: `compiler_create_workspace`, `compiler_begin_intercept`, `compiler_wait_for_message` with messages such as `.TYPECHECKED` and `.COMPLETE`. I could not check these names against Thekla's own documentation. The compiler was still in closed beta in 2026 ([Wikipedia, used only for status](https://en.wikipedia.org/wiki/Jai_(programming_language))).

**D.**
- The CTFE engine author describes it: "It's an AST interpreter … every expression encountered will allocate one or more AST nodes. Within a tight loop, the interpreter can easily generate over `100_000_000` nodes and eat a few gigabytes of RAM" ([Koch, D blog 2017](https://blog.dlang.org/archive/2017/04/10/the-new-ctfe-engine/)).
- His bytecode replacement, newCTFE, was never finished. Koch wrote in 2020: "newCTFE in the end, has a lot less benefit than I first assumed" and "I would probably never have started newCTFE, and would have tried to fix the template system itself" ([forum, 2020-08-04](https://forum.dlang.org/thread/zuqlmmmchxloumtmgjjh@forum.dlang.org)).
- DMD v2.114.0 still uses the AST `Interpreter`.

**Odin.**
- `when` conditions must be constant expressions. The compiler checks only the branch whose condition is true, and `when` opens no new scope ([overview](https://odin-lang.org/docs/overview/)).
- gingerBill rejects compile-time execution for three reasons: determinism; the VM "can also run external code", which "complicates the compiler a lot"; and making "the VM can act like the target/host machine" is hard ([#64, 2018](https://github.com/odin-lang/Odin/issues/64#issuecomment-430345024)).
- In 2025 he wrote: "every problem I came across, I find a better solution that could be solved with a different language construct or idiom" ([If Odin Had Macros](https://www.gingerbill.org/article/2025/07/31/if-odin-had-macros/)).

**C3.**
- Macros take unevaluated `#expr` arguments. Compile-time evaluation at the top level is limited to `@feat`, `@if` and a few other forms, while function bodies get the full feature set ([macros](https://c3-lang.org/generic-programming/macros/)).
- The docs say "the macro will be completely compile time executed" only when all of its inputs are constant ([compile time](https://c3-lang.org/generic-programming/compiletime/)).

**Carbon.**
- Checked generics are type-checked once at definition. Templates exist for C++ interop and are "not yet implemented" in the toolchain ([check README L518-L526](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/docs/check/README.md#L518-L526)).
- Compile-time functions run by interpreting SemIR, the same IR that is later lowered to LLVM IR.

### 1.3 Measured numbers

| Number | Workload | Version | Hardware | Source |
|---|---|---|---|---|
| 189 s and 14 GiB in Zig comptime; 6 s in CPython | `for (0..100000000)` loop that assigns `x = i` | Zig master, July 2023 | Not stated | [andrewrk, #4055](https://github.com/ziglang/zig/issues/4055#issuecomment-1646701374) |
| 56 min 20 s at comptime; 2.187 s at run time; memory rose from about 150 MiB to 520 MiB | Build an offsets array over an `@embedFile` of 333,333 words | 0.12.0 | Not stated (user report, closed as a duplicate of #4055) | [#20163](https://github.com/ziglang/zig/issues/20163) |
| 75 s and 3 GB, then the quota error | `std.sort` of 10,000 items at comptime, stage1 compiler | 0.5.0 | Windows | [#4055 opening post](https://github.com/ziglang/zig/issues/4055) |
| "roughly 10x" faster CTFE (a claim, not a measurement) | newCTFE | 2017 | n/a | Unverified; from a search summary of Koch's forum posts |

### 1.4 Costs

| | Compile time | Run time |
|---|---|---|
| Interpreter on the checker's IR (Zig, Carbon, rustc) | Proportional to the instructions executed times the cost per instruction. The value representation sets that cost: interned values (Zig) or AST nodes (D) cost allocation and hashing per operation. | None. Results are baked into the binary's data. |
| Separate build-stage program (Odin's position, Zig's `build.zig`) | One normal compile and run of the metaprogram | None |
| Memoization | Saves repeated work, but is only safe when every memoized result records what it depends on (Zig disables it under incremental) | None |

---

## 2. Generics

### 2.1 Strategies

| Strategy | Who | Compile time | Code size | Run time | Source |
|---|---|---|---|---|---|
| Full monomorphization | Zig, Odin, C3, Rust; Jai (unverified) | Each instance is analyzed and code-generated again | One copy per instance; Rust's guide names "compile time" and "binary size" as the costs | Best: full inlining and specialization | [rustc dev guide](https://rustc-dev-guide.rust-lang.org/backend/monomorph.html) |
| Monomorphize, then coalesce identical specifics during lowering | Carbon | Generic bodies are checked once; one SemIR body per generic, with per-specific values computed on demand | Duplicate LLVM functions are merged by fingerprint | Same as monomorphization | [coalesce_generic_lowering.md](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/docs/coalesce_generic_lowering.md), [generics overview](https://github.com/carbon-language/carbon-lang/blob/trunk/docs/design/generics/overview.md) |
| GC-shape stenciling with dictionaries | Go 1.18+ | One instance per shape. Types share a shape if they have the same underlying type, or are both pointers. | Fewer copies | Dictionary loads; method calls through type parameters are not resolved at compile time, which blocks inlining; conservative escape analysis "could lead to more heap allocation" | [Go 1.18 dictionaries](https://go.googlesource.com/proposal/+/refs/heads/master/design/generics-implementation-dictionaries-go1.18.md), [GC shape doc](https://go.googlesource.com/proposal/+/refs/heads/master/design/generics-implementation-gcshape.md) |
| Shared canonical code with generic dictionaries | .NET (reference-type instantiations) | Less JIT time | One canonical copy | Extra loads through the dictionary | [Shared Generics Design](https://github.com/dotnet/runtime/blob/afa9a930b827ca0aa5debd693a4731229838516c/docs/design/coreclr/botr/shared-generics.md) |
| Witness tables by default, with specialization as an optimization | Swift | Generic bodies compiled once | Smaller | Indirect calls unless specialized | [Implementing Swift Generics, LLVM 2017](https://www.youtube.com/watch?v=ctS8FzqcRug) (talk; details not checked against a transcript) |

Rust deleted its partial "polymorphization" pass in 2024. The reason given: it broke the invariant that MIR bodies stay fully monomorphized after monomorphization, and nobody used the flag ([compiler-team #810](https://github.com/rust-lang/compiler-team/issues/810), [PR #133883](https://github.com/rust-lang/rust/pull/133883)).

### 2.2 Caching and deduplication of instances

| Compiler | Instance key | Lookup | Across units |
|---|---|---|---|
| Zig | Function instance: `generic_owner` plus `comptime_args` ([Sema.zig L7549-L7557](https://github.com/ziglang/zig/blob/0.15.2/src/Sema.zig#L7549-L7557)). Type: ZIR instruction plus captured values ([InternPool.zig L2164-L2198](https://github.com/ziglang/zig/blob/0.15.2/src/InternPool.zig#L2164-L2198)) | Hash map in the `InternPool` | One compilation unit per build |
| Odin | Per base procedure: a `gen_procs` list with `lookups` | **Linear scan** of `lookups`, comparing a hash first ([check_expr.cpp L847-L858](https://github.com/odin-lang/Odin/blob/dev-2026-11a/src/check_expr.cpp#L847-L858)) | One unit |
| Carbon | `(function_id, specific_id)` | IDs | Coalescing happens at lowering |
| Go | Shape instantiation plus a dictionary named by its fully qualified type arguments | Hash table in the compiler | The linker deduplicates by name |
| Rust | Mono item | Query system | `share_generics` is on at opt-level 0, 1, `s` and `z`, and off at 2 and 3 ([config.rs L1520-L1528](https://github.com/rust-lang/rust/blob/1.99.1/compiler/rustc_session/src/config.rs#L1520-L1528)) |
| C3 | Generic module parameters. All generics in the same parameterized module section are instantiated together ([generics](https://c3-lang.org/generic-programming/generics/)). | Not documented | Not documented |

### 2.3 Costs

- Compile time grows with (number of instances) × (body size). The Go GC-shape doc warns that "the number of instantiations could be exponential in the number of type parameters" ([gcshape](https://go.googlesource.com/proposal/+/refs/heads/master/design/generics-implementation-gcshape.md)).
- In Zig, Jai and Odin, each instance's body is checked again because there are no declared constraints. Carbon checks a generic once against its constraints.
- Jai's 2025 keynote reportedly shows a report of polymorphic procedure counts and their code-size cost (unverified; from a summary of [the keynote](https://www.youtube.com/watch?v=IdpD5QIVOKQ)).

---

## 3. Reflection

### 3.1 Comparison

| Language | Compile time | Run time | Opt-out | Source |
|---|---|---|---|---|
| Zig | `@typeInfo(comptime T: type) std.builtin.Type`. Fields and declarations are listed in source order. | None built in; reflection data reaches run time only if comptime code copies it into runtime values | Implicit: unused comptime data is not emitted | [langref L5785-L5800](https://github.com/ziglang/zig/blob/0.15.2/doc/langref.html.in#L5785-L5800) |
| Odin | Constant `typeid`, `size_of` and others | `type_info_of(T)` returns `^Type_Info` (size, align, flags, id, and a variant union; structs carry arrays of types, names, offsets, usings and tags). `any` = data pointer + `typeid`. | `-no-rtti`. The compiler fills `type_table` with "only the ones that are needed (not all types)". | [core.odin L153-L171, L244-L284](https://github.com/odin-lang/Odin/blob/dev-2026-11a/base/runtime/core.odin#L244-L284) |
| C3 | Full type information through `::` (`Foo::members`, `Foo::size`), `$Typeof`, `$Typefrom` | `typeid` the size of an `iptr`; "run time `typeid`s currently have a much smaller set of available properties" | n/a | [reflection](https://c3-lang.org/generic-programming/reflection/) |
| Jai | `type_info()`, `#run` code can inspect types | A table of `Type_Info` records in the binary's data segment | Unverified | [Demo: Run-Time (and Compile-Time) Type Information](https://www.youtube.com/watch?v=JoNkttD_MUs) (unverified) |
| Carbon | Not designed yet. GitHub code search for "reflection" finds only toolchain-internal C++ helpers, nothing under `docs/design` | n/a | n/a | Unverified |

### 3.2 Type identity across builds (Odin)

- An Odin `typeid` is a 64-bit hash of a canonical string for the type ([llvm_backend_type.cpp L78-L90](https://github.com/odin-lang/Odin/blob/dev-2026-11a/src/llvm_backend_type.cpp#L78-L90), [name_canonicalization.cpp L586-L606](https://github.com/odin-lang/Odin/blob/dev-2026-11a/src/name_canonicalization.cpp#L586-L606)).
- For a named type, the hash input is only the canonical name, such as `pkg::name` ([L1098-L1104](https://github.com/odin-lang/Odin/blob/dev-2026-11a/src/name_canonicalization.cpp#L1098-L1104); rules in [name_canonicalization.hpp L1-L22](https://github.com/odin-lang/Odin/blob/dev-2026-11a/src/name_canonicalization.hpp#L1-L22)).
- The runtime finds a `Type_Info` from a `typeid` by open addressing on `id % len(type_table)` ([core.odin L813-L825](https://github.com/odin-lang/Odin/blob/dev-2026-11a/base/runtime/core.odin#L813-L825)).
- What follows for hot reload: the `typeid` is stable across rebuilds, but it cannot tell you that the layout changed. You need a separate layout hash for that.

### 3.3 Costs

- Compile-time reflection costs evaluation time only.
- Runtime type info costs data size proportional to (emitted types) × (fields), plus names. Odin keeps the cost down by emitting only types that are reached, and offers `-no-rtti`.
- Hot reload: when a type changes, its runtime record changes too. Old records still referenced by old code or by `any` values must stay valid until those references are gone. A layout hash (absent in Odin) lets the runtime detect the change.
- Zig's incremental compiler recreates a changed container type "at a new index" and invalidates the units that depend on it ([InternPool.zig L20-L72](https://github.com/ziglang/zig/blob/0.15.2/src/InternPool.zig#L20-L72)).

---

## 4. Hot reload and Edit and Continue

### 4.1 Comparison

| System | Patch unit | Indirection | Where state lives | Layout changes | Old code | When it applies |
|---|---|---|---|---|---|---|
| Zig incremental + in-place binary patching | One declaration | Calls go through a GOT cell. The linker pads symbols so they can grow, and moves one that outgrows its slot ([Konka 2022](https://www.jakubkonka.com/2022/03/16/hcs-zig.html)) | In the process; untouched | Not handled (not discussed) | Not discussed | Whenever the compiler writes into the process with `task_for_pid`/`mach_vm_write` (macOS proof of concept, [PR #14986](https://github.com/ziglang/zig/pull/14986), merged 2023). Issue [#68 "hot code swapping"](https://github.com/ziglang/zig/issues/68) is still open. |
| Handmade Hero (days 21–23) | The whole game DLL | Function pointers taken from the DLL after each load | One memory block owned by the platform layer and passed to the game | Viewer asks "Will this break when you make changes to the game_state structure?" (Casey's answer unverified) | Unloaded | Checked once per frame by comparing DLL write times ([day 21](https://guide.handmadehero.org/code/day021/), [day 22](https://guide.handmadehero.org/code/day022/), [day 23](https://guide.handmadehero.org/code/day023/)) |
| Odin template (Karl Zylinski) | The whole game DLL | `Game_API` struct of procedures filled with `dynlib.initialize_symbols` | `game_memory()` pointer handed to the new DLL through `hot_reloaded(mem)` | If `memory_size()` differs, it does a full reset instead | Old DLLs stay loaded because "The DLL can contain stored info such as string literals" | Top of the frame loop ([main_hot_reload.odin L147-L185](https://github.com/karl-zylinski/odin-raylib-hot-reload-game-template/blob/901bb85274b76272fce317a5b7b136d592b77ea6/source/main_hot_reload/main_hot_reload.odin#L147-L185)) |
| Live++ | Recompiled `.obj` files, linked into a patch module | Old functions jump to new ones. "functions patched using Live++ do not cause any additional overhead at all" with incremental linking; other builds use a short jump plus a relative jump. | Original globals: Live++ will "redirect all accesses in hot-reloaded code to always make them refer to the original instance" | You migrate them yourself: serialize in `PrePatchHook`, re-create in `PostPatchHook`; "objects created on the stack cannot be migrated" | "functions currently on the stack need to be re-entered before any of their code changes can be observed" | A "synchronized agent" so that "functions won't be patched mid-frame" ([docs](https://liveplusplus.tech/docs/documentation.html)) |
| Visual Studio C++ Hot Reload / EnC | Function | Incremental linking (`/INCREMENTAL`, `/ZI`) | In the process | Unsupported: "Changes to a data type that affect the layout of an object" and "Most changes to global or static data" | Edits to functions on the stack are allowed within limits. I infer this from the unsupported item "Adding variables that require a constructor at a point before the instruction pointer". | Break mode, or on file save ([Supported Code Changes (C++)](https://learn.microsoft.com/en-us/visualstudio/debugger/supported-code-changes-cpp)) |
| .NET (Roslyn) EnC | Method | Runtime remaps methods | Managed heap | Fields can be added, but interface edits and new virtual members are rejected ([EnC Supported Edits](https://github.com/dotnet/roslyn/blob/a1e73126c63a0527f357eae4a46d3b202136cc4b/docs/wiki/EnC-Supported-Edits.md)) | Active statements are mapped | Supports adding and modifying `await` and `yield` in methods that are already suspended (see 5.3) |
| Erlang | Module | A fully qualified call `m:f()` always goes to the current version | Process state | `code_change(OldVsn, State, Extra)` returns the new state ([gen_server](https://www.erlang.org/doc/apps/stdlib/gen_server.html)) | At most two versions. Loading a third purges the oldest, and "any processes lingering in it are terminated" ([code loading](https://www.erlang.org/doc/system/code_loading.html)) | The release handler suspends the processes, loads code, calls `code_change`, then resumes ([appup](https://www.erlang.org/doc/apps/sasl/appup.html)) |
| Common Lisp (CLOS), as a design reference | Class | n/a | Instances keep their identity | Slots present in both versions keep their values; new slots are added and dropped slots discarded; the update happens "no later than the next time a slot of that instance is read or written" ([CLHS 4.3.6](https://www.lispworks.com/documentation/HyperSpec/Body/04_cf.htm)) | n/a | Lazily |
| Jai DLL reload | No primary source found | | | | | Unverified |

Practitioner limits, from [Zylinski, "Hot Reload Gameplay Code" (2023)](https://zylinski.se/posts/hot-reload-gameplay-code/):
- Procedure pointers stored in state break when the DLL that holds the procedures is unloaded.
- Globals reset on reload.
- Layout changes can "lead to weird behavior or in worst case a crash".

### 4.2 What the compiler and runtime must provide

| Requirement | Why | Precedent |
|---|---|---|
| **Function indirection** through a stable slot (table index or GOT cell), or patchable prologues | Callers, including old code, reach new code without being rewritten | Zig GOT; Live++ jumps; HMH/Odin function pointers re-fetched per load |
| **Stable function identity in data** | Function values stored in retained state must survive a reload. Raw addresses break when the old code is freed. | Zylinski's procedure-pointer limit; Erlang fully qualified calls |
| **Globals bound to existing instances** | Never re-run initializers or `main` | Live++ redirects to the original instance |
| **Safe points** | Apply a patch where no frame of the changed code holds state the patch cannot map. The usual point is the frame boundary, or a task yield. | Live++ synchronized agent; Erlang suspend and resume; HMH and Zylinski check at the top of the frame loop |
| **Keep old code alive** until no frame can return into it | Return addresses on stacks and in fibers point into old code | Erlang keeps two versions; Zylinski keeps old DLLs; Live++ waits for functions to be re-entered |
| **Stable layouts or migration** | Layout changes are where every native tool stops | Live++ hooks with serialize and re-create; Erlang `code_change`; CLOS keeps slots with the same name |
| **Layout descriptions at run time** (type, field IDs, offsets, layout hash) | Automatic migration needs old and new layouts plus a way to find every live instance | Odin RTTI has offsets and names, but no layout hash |
| **Reachable roots for retained state** | Migration must find every instance and every reference to it. Handles or indices need no pointer fix-up; raw pointers into migrated records do. | HMH's single memory block makes the roots trivial |
| **Fast, function-granular code generation** | Edit latency | Zig 0.16: 14 s full build, then 65 ms to rebuild after a one-line change with the new ELF linker (see 4.3) |
| **Executable memory on macOS arm64** | In-process patching needs `MAP_JIT`. Under Hardened Runtime the app needs `com.apple.security.cs.allow-jit` and "can only create one memory region with the MAP_JIT flag set". Instruction caches "aren't coherent with data caches", so call `sys_icache_invalidate`. | [Apple](https://developer.apple.com/documentation/apple-silicon/porting-just-in-time-compilers-to-apple-silicon). Konka's out-of-process writes needed debugger entitlements ([part 2](https://www.jakubkonka.com/2022/03/22/hcs-zig-part-two.html)). |

### 4.3 Measured numbers

| Number | Workload | Version | Hardware | Source |
|---|---|---|---|---|
| 14 s first build; rebuilds after a one-line function change: 194 ms with the old linker, 65 ms with the new ELF linker, 62 ms with no linking | Building the Zig compiler with `-fincremental` | 0.16.0 | Not stated | [0.16.0 notes, "Incremental Compilation"](https://ziglang.org/download/0.16.0/release-notes.html) |
| 75 s → 20 s | Building the Zig compiler; x86 backend made the debug default | June 2025 | Not stated | [Zig devlog 2025-06-08](https://ziglang.org/devlog/2025/) |
| 5–50% faster wall clock; 30% faster for Hello World | Parallel self-hosted code generation | June 2025 | Not stated | [Zig devlog 2025-06-14](https://ziglang.org/devlog/2025/) |

Status:
- Incremental compilation is still opt-in in 0.17.0 and works "for most projects targeting `x86_64-linux`" ([0.17.0 notes](https://ziglang.org/download/0.17.0/release-notes.html)).
- The MachO linker is not yet incremental ([#21165](https://github.com/ziglang/zig/issues/21165)), and the aarch64 backend is not finished. So the M4 dev machine gets no Zig incremental native output today.

### 4.4 How Zig tracks what to redo

- The `InternPool` keeps one dependency table per kind of thing a unit can depend on: a declaration's source hash, a declaration's value, a declaration's type, interned values, the set of names in a namespace, one name in a namespace, embedded files and ZON files ([InternPool.zig L20-L72](https://github.com/ziglang/zig/blob/0.15.2/src/InternPool.zig#L20-L72)).
- `Zcu` keeps the sets `potentially_outdated`, `outdated` and `outdated_ready` ([Zcu.zig L249-L259](https://github.com/ziglang/zig/blob/0.15.2/src/Zcu.zig#L249-L259)).
- This is the dependency granularity that Edit and Continue needs. Comptime memoization had to be switched off because it did not take part in it.

---

## 5. Coroutines and tasks

### 5.1 Comparison

| System | Kind | Frame | Compiler needs | Runtime needs | Source |
|---|---|---|---|---|---|
| C++20 | Stackless | Heap allocation through `operator new` unless elided | Split into ramp, resume and destroy functions; "suspend points must store the index of the active suspend", and resume/destroy "switch over that index" | Allocator; the promise type | [LLVM Coroutines](https://llvm.org/docs/Coroutines.html), [[dcl.fct.def.coroutine]/9](https://eel.is/c++draft/dcl.fct.def.coroutine) |
| C++ HALO | Elision | The frame becomes an `alloca` in the caller | Must inline the ramp, which "requires the coroutine to be defined in the same translation unit"; fails when the coroutine object escapes | None | [P0981R0](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0981r0.html) |
| Zig stage1 async (removed) | Stackless, "colourless" | `@Frame(f)` holds the resume index, return value, a pointer to the return value, the error trace and spilled locals | Frame size of every callee; function pointers to async functions were never handled correctly | None | [andrewrk on #6025](https://github.com/ziglang/zig/issues/6025#issuecomment-1744116798) |
| Zig 0.16+ `std.Io` | Library interface. `Io.Threaded` is complete; `Io.Evented` is "userspace stack switching … stackful coroutines", experimental | Fiber stack | Nothing from the language | Stack switching, an event loop | [0.16.0 notes, "std.Io Interface"](https://ziglang.org/download/0.16.0/release-notes.html) |
| Zig proposal #23446 | Stackless primitives: `@asyncFrameSize`, `@asyncInit`, `@asyncResume`, `@asyncSuspend` | A caller-provided byte buffer with a comptime-known size | Restricted function types (#23367), so that the frame size of an indirect callee is known | None | [#23446](https://github.com/ziglang/zig/issues/23446), [#23367](https://github.com/ziglang/zig/issues/23367) |
| Lua | Asymmetric, "full" (stackful within Lua) | Each thread "has an independent execution stack" | None | Yields across C frames need `lua_yieldk` continuations, because Lua yields with `longjmp` | [Lua 5.4 §2.6, §4.5, `lua_newthread`](https://www.lua.org/manual/5.4/manual.html), [de Moura & Ierusalimschy 2009](https://doi.org/10.1145/1462166.1462167) |
| Kotlin | Stackless, CPS | One state-machine object per invocation of a suspending function that makes suspending calls in non-tail position. It holds a `label` field and spilled locals, and is reused across that function's suspensions. | CPS transform: an extra `Continuation` parameter, and `COROUTINE_SUSPENDED` returned as a marker | Dispatchers (library) | [KEEP-0164 L868-L907, L1036-L1048](https://github.com/Kotlin/KEEP/blob/e06f0b96231226a3b2ed64416dc02c3649b4d3d6/proposals/KEEP-0164-coroutines.md#L868-L907) |
| C# | Stackless | A struct state machine with `<>1__state` and hoisted locals; "if the async method completes synchronously, this state machine will never have left the stack" | `MoveNext` rewrite | `Task` and builder types | [Toub, 2023](https://devblogs.microsoft.com/dotnet/how-async-await-really-works/) |
| Swift | Stackless, async frames chained | The callee asks the caller for its async frame size up front; only values live across a suspension go in the context | LLVM `coro.id.async` lowering | Task allocator | [McCall & Schwaighofer, LLVM 2021](https://llvm.org/devmtg/2021-11/slides/2021-AsyncFunctionsInSwift.pdf); [Groff: "You can't relocate stack pointers and still be interoperable with C"](https://forums.swift.org/t/why-stackless-async-await-for-swift/52785) |
| Go | Stackful, M:N | 2048-byte initial stack ([stack.go L78](https://github.com/golang/go/blob/go1.26.0/src/runtime/stack.go#L78)) that grows by copying ([`copystack` L898](https://github.com/golang/go/blob/go1.26.0/src/runtime/stack.go#L898-L900)) | Stack maps to move stacks; for asynchronous preemption, "stack and register maps for nearly every instruction" | Scheduler, signal-based preemption | [Non-cooperative preemption design](https://go.googlesource.com/proposal/+/refs/heads/master/design/24543-non-cooperative-preemption.md) |
| Naughty Dog (PS4) | Stackful fibers for jobs | "160 fibers (128 x 64 KiB stack, 32 x 512 KiB stack)" | None | 6 worker threads "locked to a CPU core", 3 job queues, "No job stealing", `WaitForCounter` parks the fiber on a wait list; "~800-1000 jobs per frame". On fiber-safe TLS, the slides say "Currently not supported by Clang"; the workaround was a separate `.cpp` file for TLS access. | [Gyrling, GDC 2015 slides](https://media.gdcvault.com/gdc2015/presentations/Gyrling_Christian_Parallelizing_The_Naughty.pdf), [vault](https://www.gdcvault.com/play/1022186/Parallelizing-the-Naughty-Dog-Engine) |
| Odin | None in the language | n/a | n/a | `core:thread`, `core:sync`. gingerBill: goroutines "require automatic memory management and a huge runtime" | [HN, 2022](https://news.ycombinator.com/item?id=32630574) |
| Jai | No primary source found | | | | Unverified |

### 5.2 Why Zig removed async

- 0.11.0 shipped without async because rebuilding it first required compiler rework: the InternPool and a rework of generic calls ([Zig news, 2023-07-19](https://ziglang.org/news/0.11.0-postponed-again/)).
- Andrew Kelley called stage1 async "of experimental quality". Runtime function pointers to async functions "caused all kinds of terrible problems" ([#6025](https://github.com/ziglang/zig/issues/6025#issuecomment-1744092649)).
- In July 2025 the commit [abfed17](https://github.com/ziglang/zig/commit/abfed1708862716b7b54ce8ea349ce7215a9fab4) removed the `async` and `await` keywords. #6025 was closed with "Stackless coroutines may return to Zig, but if they do, it will be in a different form" ([mlugg](https://github.com/ziglang/zig/issues/6025#issuecomment-3072857526)).
- Current plan: concurrency is an `Io` interface in the standard library. #23446 argues that stackless coroutines are still needed for targets without stack switching, such as Wasm, because "all of the suspend/resume machinery is lowered by the compiler to synchronous constructs".

### 5.3 Suspended frames across a code swap

| Frame kind | What is saved | Can it move to new code? |
|---|---|---|
| Stackful (fibers, Go, Lua, `Io.Evented`) | Raw return addresses, spilled registers, and the stack layout of every frame on the stack | Only if the old code stays mapped until the task finishes. Moving the task needs a per-frame map from old PC and stack slots to new ones for every frame on the stack. |
| Stackless state machine (C#, Kotlin, C++, Swift) | A record: state index and hoisted locals | Yes, if the compiler maps old states and slots to new ones. Roslyn writes a "State machine suspension points" map (syntax offset and await ID → state number) and a slot map for hoisted locals keyed by syntax offset into the PDB ([EditAndContinueMethodDebugInformation.cs L52-L67, L349](https://github.com/dotnet/roslyn/blob/020ca07a9007d66a777ed9121241b240f593f9b4/src/Compilers/Core/Portable/Emit/EditAndContinueMethodDebugInformation.cs#L52-L67), [debug info GUIDs L18-L20](https://github.com/dotnet/roslyn/blob/020ca07a9007d66a777ed9121241b240f593f9b4/src/Dependencies/CodeAnalysis.Debugging/PortableCustomDebugInfoKinds.cs#L18-L20)). This is what lets EnC edit `await` in running async methods. |
| LLVM-lowered coroutines | The frame layout is chosen inside LLVM's coroutine passes from def-use analysis ("which objects need to be kept alive across suspend points") | The front end does not choose the layout, so it cannot emit a migration map for it (my inference). LLVM also states "Compatibility across LLVM releases is not guaranteed". |

Erlang shows a third policy: a process keeps running old code until it makes a fully qualified call, then switches. For tasks this means: resume a task in the old version until it reaches a yield that dispatches through the function table, then continue in the new version.

---

## 6. Architecture constraints for chibi-v2

Ordered by how early each must be decided:

1. **One typed, flat IR is the only input to the compile-time interpreter, the debug backend and the release backend.**
   - Compile-time execution needs an executable IR before code generation, so the IR must be interpretable.
   - All engines must pass one differential test.
   - Precedent: Zig ZIR/AIR, Carbon SemIR, rustc MIR.
2. **Compile-time memory is plain mutable bytes in target layout, separate from interned constants.**
   - Pointers are (allocation, offset).
   - Interning happens only when a value escapes into a type or into the binary.
   - This is Zig's own diagnosis of its 30× gap against CPython, and how Miri works.
   - Estimate (not measured): to keep 1M lines/s with comptime-heavy code, the interpreter must run on the order of 10⁸ simple operations per second. Zig's 2023 number is about 5×10⁵ loop iterations per second.
3. **Decide what compile-time code may touch.**
   - C calls and I/O at compile time need FFI from the interpreter and a host/target layout model. gingerBill names this as the main complexity.
   - Pure compile-time code (Zig, D) needs neither, but needs a separate build program for build tasks.
4. **Dependency tracking and memoization are one design.**
   - Each comptime result and each generic instance records what it read: a declaration's value or type, a namespace's names, a file.
   - Only then can it be cached across incremental builds and Edit and Continue. Zig had to turn memoization off.
   - Determinism (source order, no hash order, no clocks) makes cached and clean builds agree.
5. **Stable IDs derived from source identity, not creation order.**
   - Types: canonical path plus a layout hash.
   - Fields: name or declared ID.
   - Functions: canonical path.
   - Suspension points: source offset or an explicit label.
   - Old and new builds must be comparable. Odin's name hash is the stable half; the layout hash is the missing half.
6. **Generic instances are interned by (generic declaration, comptime argument IDs) with O(1) lookup.**
   - Odin's linear scan per base procedure breaks the growth rule.
   - Optionally coalesce identical lowered bodies (Carbon) to bound code size.
7. **Runtime type info is emitted from the same type table, only for reachable types, and is always present in development builds.**
   - Hot reload migration needs field offsets, field IDs and layout hashes for every retained type.
8. **Development builds call every function through a stable slot (an index into a table).**
   - Function values stored in data are slot indices, not addresses.
   - Release builds call directly.
9. **The code memory manager keeps each code version until no stack, fiber or task can return into it.**
   - Use epoch or reference counting per version (Erlang's two-version rule, Zylinski's kept DLLs).
   - On macOS arm64 this means `MAP_JIT`, one region under Hardened Runtime, and `sys_icache_invalidate`. Alternatively, `dlopen` one module per patch.
10. **Safe points are explicit.**
    - The runtime applies a patch only at a frame boundary or at task yields.
    - Job threads must be quiescent at that point (the Naughty Dog frame-centric design gives such a point).
11. **Retained state is reachable from declared roots with full type info.**
    - The runtime migrates changed layouts by field ID: keep matching fields, zero new ones (CLOS semantics), and run an optional user hook (Erlang `code_change`, Live++ hooks).
    - Use index or handle links rather than raw pointers, so migration does not need to fix up pointers. The pending memory-model decision decides whether the runtime can find every pointer at all.
12. **Coroutines are lowered by the front end into state-machine records in the IR, not by LLVM's coroutine passes.**
    - Each frame is an ordinary record type with runtime type info.
    - Resume states carry stable suspension-point IDs, and spilled locals carry slot IDs. Hot reload can then map suspended frames the way Roslyn does.
    - When the map fails, use a fallback: finish the task in old code, or restart it.
    - A frame size known at compile time needs known callees. Indirect calls to suspending functions need restricted function types (Zig #23367) or frames allocated by the callee.
13. **A fast, function-granular debug backend is on the edit loop; LLVM is used only for release.**
    - Zig's own backend cut its self-build from 75 s to 20 s and makes incremental rebuilds take tens of milliseconds.
    - Padding and per-function placement allow in-place patching.

## 7. Open questions for the owner

1. **Compile-time power.** Pure computation only (Zig, D), or I/O and C calls into Cute Framework and SDL at compile time (Jai, unverified)? Are build scripts written in the language?
2. **Compile-time engine.** Interpreter only, or a later option to compile comptime functions with the debug backend and call them in-process? What throughput target, in operations per second?
3. **Generic checking.** Duck-typed per instance (Zig, Jai, Odin), or constraints checked once at definition (Carbon)? This decides whether compile time grows with instances × body size.
4. **Runtime reflection in shipped builds.** Full (names, fields), IDs only, or off (`-no-rtti`)?
5. **Memory model (pending).** Can the runtime find every reference to a record whose layout changed? If not, must retained state be restricted to handle-linked plain records?
6. **Reload unit.** Patch functions in-process (Zig, Live++), or reload the whole game module (HMH, Zylinski)? Must it work without a debugger, and on which of macOS, Windows and Linux?
7. **Layout-change policy.** Automatic migration by field name, with zero-init for new fields and a user hook? How are renamed fields and fields whose type changed handled?
8. **Suspended tasks whose own function changed.** Migrate through a state map, finish in old code, or restart?
9. **Task model.** Stackless (migratable, portable to Wasm) or stackful fibers (simpler to compile, cannot migrate)? Do tasks run on job threads in parallel, or cooperatively on one thread?
10. **Function values in retained state.** Allowed? If so, they must be stable slot IDs.
11. **Targets.** Must hot reload or JIT work on platforms that forbid writable code (iOS, consoles)? Is WebAssembly a target? Wasm rules out stackful tasks.

## 8. Unverified items

- All Jai specifics: the bytecode interpreter, the message loop API names, the type table in the data segment, and the polymorphism report. I could not read video transcripts, and Thekla publishes no documentation.
- Jai hot reload and Jai or Odin coroutine designs beyond gingerBill's statement: no primary source found.
- Casey Muratori's answers to the Day 23 questions about layout changes and stored function pointers. Only the question timestamps were readable.
- The newCTFE "roughly 10x" claim.
- The current status of Zig's macOS hot code swapping after the 2025 linker rewrite.
- Carbon reflection plans.

## Code and docs to read first

1. [Zig `Sema.zig` L7707-L7745](https://github.com/ziglang/zig/blob/0.15.2/src/Sema.zig#L7707-L7745) and [L164-L193](https://github.com/ziglang/zig/blob/0.15.2/src/Sema.zig#L164-L193): how comptime calls are memoized and why memoization is off under incremental; how comptime allocations are stored.
2. [Zig #4055, andrewrk's comment](https://github.com/ziglang/zig/issues/4055#issuecomment-1646701374): the measured cost of keeping comptime values in an interned pool, and the planned fix.
3. [Zig `InternPool.zig` L20-L72 and L2164-L2198](https://github.com/ziglang/zig/blob/0.15.2/src/InternPool.zig#L20-L72) and [`Zcu.zig` L249-L259](https://github.com/ziglang/zig/blob/0.15.2/src/Zcu.zig#L249-L259): the dependency kinds for incremental compilation, and type identity as (ZIR instruction, captures).
4. [Carbon `eval.cpp` L3510-L3530 and L3845-L3870](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/check/eval.cpp#L3510-L3530) and [coalesce_generic_lowering.md](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/docs/coalesce_generic_lowering.md): executing typed IR at compile time; one generic body with per-specific values; deduplication at lowering.
5. [Roslyn EnC Supported Edits](https://github.com/dotnet/roslyn/blob/a1e73126c63a0527f357eae4a46d3b202136cc4b/docs/wiki/EnC-Supported-Edits.md) and [`EditAndContinueMethodDebugInformation.cs`](https://github.com/dotnet/roslyn/blob/020ca07a9007d66a777ed9121241b240f593f9b4/src/Compilers/Core/Portable/Emit/EditAndContinueMethodDebugInformation.cs#L52-L67): how suspended state machines are mapped to edited code.
6. [Live++ documentation](https://liveplusplus.tech/docs/documentation.html), sections "Functions on the stack", "Mutable data" and the pre-/post-patch hooks: the practical limits of native function patching.
7. [Karl Zylinski's `main_hot_reload.odin` L147-L185](https://github.com/karl-zylinski/odin-raylib-hot-reload-game-template/blob/901bb85274b76272fce317a5b7b136d592b77ea6/source/main_hot_reload/main_hot_reload.odin#L147-L185): the smallest working DLL reload loop. Old DLLs are kept, and a size change forces a restart.
8. [Erlang code loading](https://www.erlang.org/doc/system/code_loading.html) and [appup `update`](https://www.erlang.org/doc/apps/sasl/appup.html): the two-version rule; the suspend, `code_change`, resume sequence.
9. [CLHS 4.3.6](https://www.lispworks.com/documentation/HyperSpec/Body/04_cf.htm): semantics for keeping instance identity and slot values when a type's layout changes.
10. [LLVM Coroutines](https://llvm.org/docs/Coroutines.html) and [P0981R0](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0981r0.html): switched-resume lowering, frame spilling, and when heap elision fails.
11. [Zig #23446](https://github.com/ziglang/zig/issues/23446) and [#23367](https://github.com/ziglang/zig/issues/23367): minimal stackless primitives with comptime-known frame sizes; why indirect calls block them.
12. [Gyrling GDC 2015 slides](https://media.gdcvault.com/gdc2015/presentations/Gyrling_Christian_Parallelizing_The_Naughty.pdf): fiber counts, stack sizes, counters, and fiber/TLS pitfalls in a shipped engine.
13. [Go 1.18 dictionaries design](https://go.googlesource.com/proposal/+/refs/heads/master/design/generics-implementation-dictionaries-go1.18.md): the middle ground between monomorphization and boxing, and its run-time costs.
14. [Odin `core.odin` L244-L284](https://github.com/odin-lang/Odin/blob/dev-2026-11a/base/runtime/core.odin#L244-L284) and [`name_canonicalization.cpp` L586-L606](https://github.com/odin-lang/Odin/blob/dev-2026-11a/src/name_canonicalization.cpp#L586-L606): runtime type info layout, and type IDs that are stable across builds.
15. [Go `stack.go` L898](https://github.com/golang/go/blob/go1.26.0/src/runtime/stack.go#L898-L900) and the [preemption design](https://go.googlesource.com/proposal/+/refs/heads/master/design/24543-non-cooperative-preemption.md): what growable stacks and safe points require from the compiler.
16. [Zig 0.16.0 release notes](https://ziglang.org/download/0.16.0/release-notes.html), sections "std.Io Interface" and "Incremental Compilation": current state of Zig's tasks and incremental rebuilds, with timings.
