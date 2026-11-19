# Memory models for games, hot reload and coroutines

Research note, 2026-11-09. Question: which memory models work for games, and which fit a
language that compiles 1,000,000 lines per second, reloads code as Edit and Continue (state kept,
struct layouts may change) and has coroutines and tasks.

## Summary

1. The deciding question for chibi-v2 is not "GC or RC". It is: at a safe point, can the runtime find every live instance of a changed type and every stored reference to it? Typed pools addressed by generational handles answer this with no compiler analysis. Tracing GCs answer it with type maps, barriers and roots. Reference counting and raw arenas do not answer it.
2. **Goose** is [aardappel/goose](https://github.com/aardappel/goose), a new language by Wouter van Oortmerssen (author of Lobster). It has no heap: values live inline on compiler-assigned bump "data stacks" freed at scope exit, pools give type-safe slot reuse, threads share nothing, and coroutines are designed (not built) as fixed-size frame values. The price is whole-program root inference and specialization. Goose has no hot reload.
3. **Zen C** is [Zuhaitz-dev/zenc-lang](https://github.com/Zuhaitz-dev/zenc-lang) (formerly `z-libs/Zen-C`), a transpiler to C. Memory is manual: `defer`, move-by-default for types with destructors, and a `Drop` trait. It has no borrow checking, and source v0.5.0 removed the `autofree` keyword that its docs still show.
4. The best RC systems (Perceus in Koka, Lean 4, Lobster) remove most count operations with cheap per-function passes. They keep counts non-atomic and mark thread-shared objects with the sign bit. RC still pays per copy, leaks cycles without a cycle collector, and gives no help with layout migration.
5. Game GCs (Unreal, Unity, Lua, Luau, Go) all moved to incremental or concurrent marking. That needs write barriers on pointer stores and leaves an atomic phase that can pause for milliseconds.
6. For coroutines, the cheapest model is a stackless frame that is a plain value. This is the Goose design, and close to Rust futures and Swift's task allocator. Such frames work best when they hold handles, not pointers. RC frames keep objects alive and can leak through task cycles (Nim's async under ARC). Per-frame temp arenas dangle under frames that live across game frames.
7. Of the three candidate designs below, **A** (typed pools with generational handles, scoped arenas, and a "persistable type" rule) adds almost no compile time and gives the simplest reload migration.

---

## 1. What "Goose" and "ZenC" are

### Name search

| Name | Projects found | Chosen |
|---|---|---|
| Goose | [aardappel/goose](https://github.com/aardappel/goose): memory-safe systems language, "no heap allocator, garbage collector, or lifetime annotations". [aaif-goose/goose](https://github.com/aaif-goose/goose): AI agent. [pressly/goose](https://github.com/pressly/goose): database migrations. [goose-lang/goose](https://github.com/goose-lang/goose): converts a subset of Go to Rocq. | aardappel/goose |
| ZenC | [Zuhaitz-dev/zenc-lang](https://github.com/Zuhaitz-dev/zenc-lang) (`z-libs/Zen-C` and `zenc-lang/zenc` redirect here; site [zenc-lang.org](https://www.zenc-lang.org/)). [zenc-lang-ce/zenc](https://github.com/zenc-lang-ce/zenc): a "community edition" fork. | Zuhaitz-dev/zenc-lang |

### Goose (aardappel/goose @ `f4637bfb`, 2026-11-08)

Goose's repository was created 2026-08-30. Its README says the repository "was produced almost entirely with AI, from a human language design" by van Oortmerssen ([README L608-L632](https://github.com/aardappel/goose/blob/f4637bfb342fb70d42bea0474eab9e62a3c99d68/README.md#L608-L632)).

**Memory model.**
- A program has the native stack, static data and N data stacks. The compiler computes N. Each data stack is a virtual-address reservation with a bump pointer, and "there is no general-purpose heap and no `malloc`" ([spec §1.2](https://github.com/aardappel/goose/blob/f4637bfb342fb70d42bea0474eab9e62a3c99d68/docs/goose_spec.md#L64-L89)).
- Lifetimes on one stack are strictly nested. At most one resizable value is live per stack, and it is always on top, so growth is a pointer bump with no capacity check. "The compiler proves all of this statically" ([spec §1.3](https://github.com/aardappel/goose/blob/f4637bfb342fb70d42bea0474eab9e62a3c99d68/docs/goose_spec.md#L90-L117)).
- Freeing at scope exit is "a single store, no matter how deeply it is nested". There are no destructors and no RC ([README L34-L37](https://github.com/aardappel/goose/blob/f4637bfb342fb70d42bea0474eab9e62a3c99d68/README.md#L34-L37)).
- References survive growth because memory never moves ([README L106-L119](https://github.com/aardappel/goose/blob/f4637bfb342fb70d42bea0474eab9e62a3c99d68/README.md#L106-L119)). Each data stack reserves 2 GB of address space by default ([README L518-L522](https://github.com/aardappel/goose/blob/f4637bfb342fb70d42bea0474eab9e62a3c99d68/README.md#L518-L522)).
- Every reference has a static *root*. The compiler infers roots and **specializes functions per root**, so no annotations are needed ([README L121-L140](https://github.com/aardappel/goose/blob/f4637bfb342fb70d42bea0474eab9e62a3c99d68/README.md#L121-L140)).
- Relative references are 1-, 2- or 4-byte offsets. They make data position independent and loadable with one read plus a verification pass ([README L218-L259](https://github.com/aardappel/goose/blob/f4637bfb342fb70d42bea0474eab9e62a3c99d68/README.md#L218-L259)).
- When lifetimes don't nest, `slot_pool<T>` (a grow-only array plus a freelist) is used. A stale slot "reads a *different value of the correct type*". This is a logic bug, never memory corruption. The pool keeps no generations ([stdlib.md L688-L731](https://github.com/aardappel/goose/blob/f4637bfb342fb70d42bea0474eab9e62a3c99d68/docs/stdlib.md#L688-L731), [spec §9.4](https://github.com/aardappel/goose/blob/f4637bfb342fb70d42bea0474eab9e62a3c99d68/docs/goose_spec.md#L3993-L4006)). Goose's physics and UI libraries do use generational handles ("Every handle carries a generation") ([stdlib.md L2100-L2107](https://github.com/aardappel/goose/blob/f4637bfb342fb70d42bea0474eab9e62a3c99d68/docs/stdlib.md#L2100-L2107)).

**Compile model.** Goose is "a closed-world, whole-program compiler". Each function is compiled once per unique combination of argument types, reference roots, writability, destination stacks and static function values ([spec §10.1-10.3](https://github.com/aardappel/goose/blob/f4637bfb342fb70d42bea0474eab9e62a3c99d68/docs/goose_spec.md#L4227-L4286)). Recursive functions cannot keep growable data across a recursive call ([README L422-L438](https://github.com/aardappel/goose/blob/f4637bfb342fb70d42bea0474eab9e62a3c99d68/README.md#L422-L438)). I found no compile-speed numbers.

**Threads.** Threads share no memory. A `thread_fn` and everything it calls is compiled as a separate program with its own data stacks. Values cross threads by `memcpy` through typed queues and must be flat ([README L349-L368](https://github.com/aardappel/goose/blob/f4637bfb342fb70d42bea0474eab9e62a3c99d68/README.md#L349-L368), [spec §11.2](https://github.com/aardappel/goose/blob/f4637bfb342fb70d42bea0474eab9e62a3c99d68/docs/goose_spec.md#L4521-L4540)).

**Coroutines.** These are a research note only: "Nothing here is implemented" ([coroutines.md L52-L100](https://github.com/aardappel/goose/blob/f4637bfb342fb70d42bea0474eab9e62a3c99d68/docs/design/coroutines.md#L52-L100)).
- A suspended function can keep neither its native stack nor a data stack. So coroutines are stackless, and the frame is a fixed-size value. The recursion rule ("no nonfixed local live across a call into the cycle") becomes the yield rule ([§2.1-2.2](https://github.com/aardappel/goose/blob/f4637bfb342fb70d42bea0474eab9e62a3c99d68/docs/design/coroutines.md#L165-L208)).
- Frames copy, can be sent to another thread when flat, and serialize, "tied to the compiler version that laid the frame out" ([§4.9](https://github.com/aardappel/goose/blob/f4637bfb342fb70d42bea0474eab9e62a3c99d68/docs/design/coroutines.md#L813-L835)).
- Costs: `resume` is one direct call, one switch and reloads of the live locals. `yield` is the stores of the live locals plus a tag store. Code that doesn't use coroutines pays nothing ([§4.12](https://github.com/aardappel/goose/blob/f4637bfb342fb70d42bea0474eab9e62a3c99d68/docs/design/coroutines.md#L880-L952)).

**WebAssembly.** Platforms without address-space reservation ("wasm today") "fall back to index-based references and bounds-checked growth, with reduced performance" ([spec §10.4](https://github.com/aardappel/goose/blob/f4637bfb342fb70d42bea0474eab9e62a3c99d68/docs/goose_spec.md#L4287-L4298)).

**Hot reload.** I found none in the docs. Whole-program specialization means one edit can change the specializations of many callees.

**What to take.**
- Pools as the escape hatch from scoped lifetimes.
- Relative references and load-time verification of saved data.
- Share-nothing threads with flat messages.
- Coroutine frames as plain values that hold no data-stack memory across a yield.

**What to leave.**
- Per-root specialization and closed-world compiling, which conflict with fast incremental builds and hot reload.
- Reliance on virtual reservations, which wasm lacks.

### Zen C (Zuhaitz-dev/zenc-lang @ `b3f550e9`, v0.5.0)

Zen C "compiles to human-readable GNU C/C11" and offers "manual memory management with RAII capabilities" ([README L43-L45](https://github.com/Zuhaitz-dev/zenc-lang/blob/b3f550e938be3fd395e9ebf657669f89dbb3453b/README.md#L43-L45)).

**Memory model**, from the [memory chapter](https://github.com/zenc-lang/docs/blob/60115e029d02e9cea4c9f721da5c82cd7b257de5/reference/08-memory-management.md):
- `defer`.
- Types with destructors are "Resources" and move by default. Other types opt in to `Copy`.
- Borrowing is a raw pointer parameter (`r: Resource*`).
- `Drop` runs cleanup at scope end.
- The v0.5.0 changelog adds drops for by-value parameters and discarded results ([CHANGELOG L98-L102](https://github.com/Zuhaitz-dev/zenc-lang/blob/b3f550e938be3fd395e9ebf657669f89dbb3453b/CHANGELOG.md#L98-L102)).

**Docs vs source.**
- The docs still show `autofree`. The parser now rejects it: "'autofree' was removed; declare the variable normally and add 'defer free(x)'" ([parser_stmt.c L322-L337](https://github.com/Zuhaitz-dev/zenc-lang/blob/b3f550e938be3fd395e9ebf657669f89dbb3453b/src/parser/parser_stmt.c#L322-L337)).
- The docs say async is "Built on pthreads" ([concurrency chapter](https://github.com/zenc-lang/docs/blob/60115e029d02e9cea4c9f721da5c82cd7b257de5/reference/11-concurrency.md)). v0.5.0 changed this: "`async` functions compile to a state machine polled by `await` instead of a thread per call" ([CHANGELOG L227](https://github.com/Zuhaitz-dev/zenc-lang/blob/b3f550e938be3fd395e9ebf657669f89dbb3453b/CHANGELOG.md#L227)).
- **Update, 2026-11-10:** Zen C removed `async` and `await` from its `main` branch on 2026-10-24. Its parser now reports that `async` was removed. See [syntax-options-effects.md](syntax-options-effects.md), sheet 9.

**Compiler work.** The move check is a flow-sensitive pass inside type checking:
- Moved state is merged at branches, and loops get a second pass.
- State is a linked list keyed by `strdup`'d path strings and looked up with `strcmp` through parent scopes ([move_check.c L92-L180](https://github.com/Zuhaitz-dev/zenc-lang/blob/b3f550e938be3fd395e9ebf657669f89dbb3453b/src/analysis/move_check.c#L92-L180), [L298-L340](https://github.com/Zuhaitz-dev/zenc-lang/blob/b3f550e938be3fd395e9ebf657669f89dbb3453b/src/analysis/move_check.c#L298-L340)). Each lookup scans every entry in scope, so a function with many moved paths costs quadratic time.

**Standard library.** `std/arena.zc` is a fixed-capacity bump arena with `save` and `restore` marks ([arena.zc L1-L79](https://github.com/Zuhaitz-dev/zenc-lang/blob/b3f550e938be3fd395e9ebf657669f89dbb3453b/std/arena.zc#L1-L79)).

**What to take.** Move-by-default for owning types, checked by a forward pass. With dense local ids and bitsets instead of strings, that pass is linear.

**What to leave.** Zen C has no answer for references that outlive their target, no hot reload and no layout reflection.

---

## 2. Runtime cost by model

Cells without a link are my analysis of the mechanism in the cited sources, not measurements.

| Model | Per pointer assignment | Per call | Per frame | Pauses | Cycles |
|---|---|---|---|---|---|
| Manual + arenas (Odin, Jai, Zig, Zen C) | 0 | Odin: one implicit `context` pointer argument ([overview](https://github.com/odin-lang/odin-lang.org/blob/1d272b93ccbd9a7e23f21cdbc2a6b5d5b8e5daa4/content/docs/overview.md#L3666)). Zig: an explicit `Allocator` parameter only where needed ([langref](https://github.com/ziglang/zig/blob/738d2be9d6b6ef3ff3559130c05159ef53336224/doc/langref.html.in#L6293-L6295)) | Temp arena reset is O(1) | None | Programmer's problem |
| Generational handles + pools | 0 (copy an integer) | 0 | 0 | None | No ownership graph. Destroy is explicit, and stale handles are detected ([floooh](https://floooh.github.io/2018/06/17/handles-vs-pointers.html)) |
| Naive RC (`shared_ptr`, Swift ARC) | Increment the new target, decrement the old one with a free branch. Atomic in `shared_ptr` once any thread exists ([libstdc++ L136-L155](https://github.com/gcc-mirror/gcc/blob/4f0d0a2c472f5270b066b707aa280ee02e7c9a01/libstdc%2B%2B-v3/include/ext/atomicity.h#L136-L155)). Atomic in Swift ([RefCount.h](https://github.com/swiftlang/swift/blob/25983423c97fabeb2ee68995cfa191ca86417545/stdlib/public/SwiftShims/swift/shims/RefCount.h#L44-L98)) | An increment/decrement pair for owned arguments. Borrowed (`@guaranteed`) arguments skip it ([OSSA](https://github.com/swiftlang/swift/blob/25983423c97fabeb2ee68995cfa191ca86417545/docs/SIL/Ownership.md#L6-L23)) | 0, but dropping a big graph frees it all at once (Koka's drop "drop[s] the children recursively", [kklib.h L764](https://github.com/koka-lang/koka/blob/7e9c1e593a46b704c915832c2da702b7ec41f45f/kklib/include/kklib.h#L764-L775)) | Cascading frees | Leak ([Rust `Rc`](https://doc.rust-lang.org/std/rc/index.html), [Swift](https://github.com/swiftlang/swift-book/blob/16a2a4caa408c86fc85bde1e1dfff4745b6e454c/TSPL.docc/LanguageGuide/AutomaticReferenceCounting.md#L379-L381), [Nim ARC](https://nim-lang.org/docs/mm.html)) |
| Optimized RC (Perceus, Lean, Lobster, Nim ARC) | Same, but most operations are removed statically (Lobster: about 95% ([doc](https://aardappel.github.io/lobster/memory_management.html))). The fast path is one sign test, non-atomic ([kklib.h L733-L775](https://github.com/koka-lang/koka/blob/7e9c1e593a46b704c915832c2da702b7ec41f45f/kklib/include/kklib.h#L733-L775)) | Borrowed parameters cost 0 ([Lean](https://arxiv.org/abs/1908.05647)) | Same as naive RC | Cascading frees | Leak (ARC, Lobster: report at exit) or trial-deletion collector (ORC) ([ORC](https://nim-lang.org/blog/2020/12/08/introducing-orc.html)) |
| Generational references (Vale) | Copy pointer + generation | 0 | 0 | None | Single owner, so cycles are not a leak source |
| Data stacks (Goose) | 0 | Hidden stack arguments where shared ([spec §10.2](https://github.com/aardappel/goose/blob/f4637bfb342fb70d42bea0474eab9e62a3c99d68/docs/goose_spec.md#L4236-L4245)) | Scope exit is one store | None | No heap graph |
| Incremental tracing GC (Unreal, Unity, Lua, Luau, Go) | Write barrier while marking ([Unity](https://docs.unity.com/en-us/engine/current/manual/analysis/performance-memory/managed-memory/incremental-garbage-collection), [UE](https://dev.epicgames.com/documentation/en-us/unreal-engine/incremental-garbage-collection-in-unreal-engine), [Go](https://go.dev/doc/gc-guide)) | Safe-point polls; stack maps for precise roots ([Go 1.4](https://go.dev/doc/go1.4)) | A mark/sweep slice inside a budget | Atomic phase. Luau: "on the order of tens of milliseconds" before its remark change ([Luau](https://luau.org/performance)) | Collected |
| Generational moving GC (.NET) | Card-marking write barrier (not verified in a primary source) | JIT stack maps; all managed threads suspended ([fundamentals](https://learn.microsoft.com/en-us/dotnet/standard/garbage-collection/fundamentals)) | Gen0 collections | Gen2 is avoidable only through latency modes or a no-GC region ([latency](https://learn.microsoft.com/en-us/dotnet/standard/garbage-collection/latency), [TryStartNoGCRegion](https://learn.microsoft.com/en-us/dotnet/api/system.gc.trystartnogcregion)) | Collected |

Dereference cost:
- **Handle:** a bounds check plus a load and compare of the slot's generation ([floooh](https://floooh.github.io/2018/06/17/handles-vs-pointers.html)).
- **Vale generational reference:** a load and compare of the object's generation. Measured cost is in §6.

## 3. Compiler work by model

The budget: 1,000,000 lines per second on one core is about 1 µs per line, roughly 4,000 cycles at about 4 GHz. This is my estimate. Apple doesn't publish M4 clock speeds, so the clock is unverified. A memory-model pass that visits each IR instruction once, at 5-10 instructions per line and 20-50 cycles per visit, costs 100-500 cycles per line. That is 3-12% of the budget. Fixpoints over the call graph, region inference and per-root specialization cost a multiple of that, and their worst case grows with program shape.

| Model | What the compiler emits | Analysis needed | Fits the budget? |
|---|---|---|---|
| Manual + arenas | Nothing. Odin also passes `context` on every Odin-convention call ([overview L3666](https://github.com/odin-lang/odin-lang.org/blob/1d272b93ccbd9a7e23f21cdbc2a6b5d5b8e5daa4/content/docs/overview.md#L3666)) | None | Yes |
| Handles + pools | Generation-check code at the dereference site; reflection tables per type | None (library) | Yes |
| Move-by-default (Zen C, Rust moves) | Drop calls at scope ends, plus drop flags on conditional moves | Forward dataflow over moved state with merges at joins ([Zen C](https://github.com/Zuhaitz-dev/zenc-lang/blob/b3f550e938be3fd395e9ebf657669f89dbb3453b/src/analysis/move_check.c#L142-L180)) | Yes, with dense ids and bitsets |
| Naive RC | Increments and decrements at every copy and scope end; drop code per type | None | Yes for compile time; costly at run time |
| Perceus / Lean RC | Explicit `dup`/`drop` at last use, reuse tokens | Per-function liveness. Lean also infers borrow annotations with a heuristic ([abstract](https://arxiv.org/abs/1908.05647)) | Liveness: yes. Interprocedural borrow inference: risky |
| Swift ARC | Retains and releases in OSSA form ([Ownership.md](https://github.com/swiftlang/swift/blob/25983423c97fabeb2ee68995cfa191ca86417545/docs/SIL/Ownership.md#L6-L23)), then several optimizer passes such as loop hoisting ([ARCOptimization.md](https://github.com/swiftlang/swift/blob/25983423c97fabeb2ee68995cfa191ca86417545/docs/SIL/ARCOptimization.md#L407-L430)) | Ownership verification plus iterative dataflow | No |
| Lobster | Increments only where a second owner is needed | Ownership analysis during type checking ([doc](https://aardappel.github.io/lobster/memory_management.html)) | Likely yes |
| Rust ownership | Nothing at run time | MIR borrow check: region renumbering, move-path dataflow, region inference, borrows in scope, a second MIR walk ([rustc-dev-guide](https://rustc-dev-guide.rust-lang.org/borrow-check.html)) | No |
| Vale | A generation check per dereference; regions remove checks in `pure` code ([Vale](https://verdagon.dev/blog/generational-references)) | Region analysis for check removal | Checks: yes. Regions: unknown |
| Goose | Stack indices; hidden stack arguments | Whole-program root inference, specialization per root, stack assignment ([spec §10](https://github.com/aardappel/goose/blob/f4637bfb342fb70d42bea0474eab9e62a3c99d68/docs/goose_spec.md#L4227-L4286)) | No, and not incremental |
| Precise tracing GC | Write barriers; type maps; stack maps at every safe point ([Go 1.4](https://go.dev/doc/go1.4)) | Pointer liveness at each safe point | Barriers and type maps: yes. Stack maps: they add backend work at every call |
| Conservative tracing GC (Boehm, as Unity uses it) | Nothing for roots; barriers for incremental mode ([Unity](https://docs.unity.com/en-us/engine/current/manual/analysis/performance-memory/managed-memory/incremental-garbage-collection)) | None | Yes, but not on wasm (§8) and the GC can't move objects |

## 4. Per-model notes

### Reference counting

**Swift ARC.**
- "Reference counting applies only to instances of classes" ([TSPL L21](https://github.com/swiftlang/swift-book/blob/16a2a4caa408c86fc85bde1e1dfff4745b6e454c/TSPL.docc/LanguageGuide/AutomaticReferenceCounting.md#L21)).
- Strong, unowned and weak counts live in an atomic inline word. A side table is added when the first weak reference forms ([RefCount.h L44-L98](https://github.com/swiftlang/swift/blob/25983423c97fabeb2ee68995cfa191ca86417545/stdlib/public/SwiftShims/swift/shims/RefCount.h#L44-L98)).
- A compiler pass can make all counts non-atomic when the program is single-threaded, but the runtime's own retains stay atomic ([AssumeSingleThreaded.swift L12-L35](https://github.com/swiftlang/swift/blob/25983423c97fabeb2ee68995cfa191ca86417545/SwiftCompilerSources/Sources/Optimizer/FunctionPasses/AssumeSingleThreaded.swift#L12-L35)).
- Async frames come from "a task-local allocator that obeys a stack discipline". It needs no synchronization because at most one thread runs a task at once ([TaskAlloc.cpp L12-L18](https://github.com/swiftlang/swift/blob/25983423c97fabeb2ee68995cfa191ca86417545/stdlib/public/Concurrency/TaskAlloc.cpp#L12-L18)).
- Cycles need `weak` or `unowned` references ([TSPL L379-L381](https://github.com/swiftlang/swift-book/blob/16a2a4caa408c86fc85bde1e1dfff4745b6e454c/TSPL.docc/LanguageGuide/AutomaticReferenceCounting.md#L379-L381)).

**Perceus (Koka).**
- The compiler inserts precise `dup`/`drop`, so objects are freed right after last use. The docs call this "garbage-free" for cycle-free programs ([why.kk.md L431-L446](https://github.com/koka-lang/koka/blob/7e9c1e593a46b704c915832c2da702b7ec41f45f/doc/spec/why.kk.md#L431-L446)).
- Reuse analysis "pairs pattern matches with constructors of the same size and reuses them in-place" ([L251-L267](https://github.com/koka-lang/koka/blob/7e9c1e593a46b704c915832c2da702b7ec41f45f/doc/spec/why.kk.md#L251-L267)).
- It relies on language facts: "inductive data types cannot form cycles, and potential sharing across threads can be reliably determined" ([L222-L225](https://github.com/koka-lang/koka/blob/7e9c1e593a46b704c915832c2da702b7ec41f45f/doc/spec/why.kk.md#L222-L225)).
- The runtime encoding is: count 0 means unique, positive means thread-local, negative means thread-shared (atomic) or stuck. `dup` and `drop` test one sign in the fast path ([kklib.h L102-L116](https://github.com/koka-lang/koka/blob/7e9c1e593a46b704c915832c2da702b7ec41f45f/kklib/include/kklib.h#L102-L116), [L733-L775](https://github.com/koka-lang/koka/blob/7e9c1e593a46b704c915832c2da702b7ec41f45f/kklib/include/kklib.h#L733-L775)).
- Paper: [Perceus TR](https://www.microsoft.com/en-us/research/publication/perceus-garbage-free-reference-counting-with-reuse/).

**Lean 4.**
- The sign of `m_rc` means single-threaded (>0), multi-threaded (<0) or not counted (0, for persistent objects and compact regions). Counts that overflow become "sticky" and are never freed ([lean.h L149-L160](https://github.com/leanprover/lean4/blob/392f865691c407ff3500c90d4a52f4156edc9dbc/src/include/lean/lean.h#L149-L160)).
- Borrowed parameters plus a borrow-inference heuristic reduce count updates ([Counting Immutable Beans](https://arxiv.org/abs/1908.05647)).

**Nim ARC/ORC.**
- ARC is RC with move semantics and cursor inference: building a data structure "does not involve RC operations", and common traversals don't either. ORC adds a trial-deletion cycle collector that is called only for types that may be cyclic, with an `.acyclic` opt-out ([ORC post](https://nim-lang.org/blog/2020/12/08/introducing-orc.html)).
- ORC is the default. ARC and ORC are not atomic and are not thread-safe across threads; `atomicArc` and `yrc` are atomic variants.
- With `--mm:arc`, "the default async implementation leaks memory" ([mm](https://nim-lang.org/docs/mm.html)).

**Lobster.** Runtime RC plus a compile-time ownership analysis that "was able to remove around 95% of runtime reference count operations". Cycles are not collected; a cycle report at exit helps the programmer break them ([memory_management](https://aardappel.github.io/lobster/memory_management.html)).

**C++ `shared_ptr` (libstdc++).** Count updates go through `__exchange_and_add_dispatch` ([shared_ptr_base.h L150-L151](https://github.com/gcc-mirror/gcc/blob/4f0d0a2c472f5270b066b707aa280ee02e7c9a01/libstdc%2B%2B-v3/include/bits/shared_ptr_base.h#L150-L151)). That function uses a plain add only while `__libc_single_threaded` is true; after the first thread is created, every update is an atomic RMW ([atomicity.h L50-L61, L136-L155](https://github.com/gcc-mirror/gcc/blob/4f0d0a2c472f5270b066b707aa280ee02e7c9a01/libstdc%2B%2B-v3/include/ext/atomicity.h#L50-L61)).

**Biased RC (CPython free-threading).** The owning thread updates a local count non-atomically, and other threads use an atomic shared count. Immortal objects skip counting ([PEP 703](https://peps.python.org/pep-0703/)). Measured overhead is in §6.

### Generational references and regions (Vale)

- Each allocation has a generation, and a reference stores the pointer plus the generation it expects. Each dereference checks the two match.
- Later designs use random generations and allow ordinary `malloc`.
- `pure` functions and region "pre-checks" remove checks: the first regions prototype showed "no observable overhead" on the benchmark ([Ovadia, updated 2023](https://verdagon.dev/blog/generational-references)). Measured overhead is in §6.

### Ownership (Rust, as a reference point)

- No runtime cost for owned values. The cost is the MIR borrow checker ([rustc-dev-guide](https://rustc-dev-guide.rust-lang.org/borrow-check.html)).
- `Rc` "uses non-atomic reference counting" and is not `Send`. `Arc` is the atomic version. `Rc` cycles leak ([std::rc](https://doc.rust-lang.org/std/rc/index.html)).
- `async fn` state machines can point into themselves, so Rust added `Pin`. The alternative, "indirection and allocation for each layer of composition", was "deemed unacceptable" ([std::pin](https://doc.rust-lang.org/std/pin/index.html)).

### Arenas and allocator contexts

**Odin.**
- `context` is implicit, local to each scope, and passed by pointer to every Odin-convention call. `context.allocator` defaults to an OS heap allocator. `context.temp_allocator` is "a growing arena based allocator", "to be freed once per cycle/frame" with `free_all` ([overview L3665-L3728](https://github.com/odin-lang/odin-lang.org/blob/1d272b93ccbd9a7e23f21cdbc2a6b5d5b8e5daa4/content/docs/overview.md#L3665-L3728)).
- An `Allocator` is a procedure pointer plus data, so every allocation is an indirect call ([core.odin L456-L463](https://github.com/odin-lang/Odin/blob/85afcb386c389aafcec4f2e33d27ff8b42579822/base/runtime/core.odin#L456-L463)). The `Context` struct also carries a logger, an RNG and user data ([L527-L539](https://github.com/odin-lang/Odin/blob/85afcb386c389aafcec4f2e33d27ff8b42579822/base/runtime/core.odin#L527-L539)).

**Jai.** Jonathan Blow demonstrated temporary storage and the expandable context in 2017 ([demo](https://www.youtube.com/watch?v=SSVHWrYG974), [Q&A](https://www.youtube.com/watch?v=FCbUV8oSDgI)). Jai has no public documentation. Details such as the struct fields, overflow behavior and `push_context` semantics come from secondary sources and are **unverified**.

**Zig.**
- "The Zig language performs no memory management on behalf of the programmer". There is "no default allocator"; functions that allocate take an `Allocator` ([langref L6280-L6300](https://github.com/ziglang/zig/blob/738d2be9d6b6ef3ff3559130c05159ef53336224/doc/langref.html.in#L6280-L6300)).
- For "a video game main loop", the guide recommends `ArenaAllocator` freed at the end of the cycle, or `FixedBufferAllocator` when there is a known bound ([L6312-L6356](https://github.com/ziglang/zig/blob/738d2be9d6b6ef3ff3559130c05159ef53336224/doc/langref.html.in#L6312-L6356)).
- An `Allocator` is a pointer plus a vtable ([Allocator.zig L19-L22](https://github.com/ziglang/zig/blob/738d2be9d6b6ef3ff3559130c05159ef53336224/lib/std/mem/Allocator.zig#L19-L22)). The links are to the GitHub mirror; Zig has moved to Codeberg.

### Generational handles and slot maps

**Weissflog, "Handles are the better pointers" (2018).**
- Move memory management into systems that own arrays per type. Hand out only indices.
- A handle is index bits plus a per-slot generation counter that is bumped on release. A mismatch means a stale handle. A slot whose counter overflows is disabled.
- "Never store pointers"; use a pointer only briefly inside one block ([floooh](https://floooh.github.io/2018/06/17/handles-vs-pointers.html)).

**Bitsquid ID lookup table (2011).** IDs let the owning system "reorganize its objects in memory, delete them at will", and need no synchronization to keep a reference count ([Frykholm](http://bitsquid.blogspot.com/2011/09/managing-decoupling-part-4-id-lookup.html), [reprint](https://gamedev.net/tutorials/programming/general-and-gameplay-programming/managing-decoupling-part-4-the-id-lookup-table-r3062/)).

### Garbage collectors used in games

**Unreal Engine.**
- Mark-and-sweep over UObjects, starting from a root set. References are held through `UPROPERTY`. Clusters let "only the cluster itself" be checked, but a large cluster is deleted in one frame, "potentially causing a hitch" ([object handling](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-object-handling-in-unreal-engine)).
- Incremental reachability splits marking across frames with a soft time limit (the doc's example is 2 ms). It needs `TObjectPtr` assignments to act as a write barrier. It is experimental and "not fully thread safe" ([incremental GC](https://dev.epicgames.com/documentation/en-us/unreal-engine/incremental-garbage-collection-in-unreal-engine)).
- Other settings include incremental BeginDestroy, multithreaded destruction and parallel GC ([settings](https://dev.epicgames.com/documentation/en-us/unreal-engine/garbage-collection-settings-in-the-unreal-engine-project-settings)).

**Unity.**
- Unity uses Boehm-Demers-Weiser, which is non-compacting, in incremental mode by default. Unity "generates additional code (known as write barriers)", which "adds some overhead when changing references".
- Too many reference changes can keep marking from finishing; Unity then falls back to a full, non-incremental collection. Web builds don't support incremental GC.
- The stop-the-world mode "might last for hundreds of milliseconds" ([Unity 6.6 manual](https://docs.unity.com/en-us/engine/current/manual/analysis/performance-memory/managed-memory/incremental-garbage-collection)).
- Each coroutine creates an `IEnumerator` state machine object; starting coroutines often allocates ([coroutines](https://docs.unity.com/en-us/engine/current/manual/scripting/coroutines-section/coroutines)).

**Lua 5.4.**
- Incremental mode interleaves small mark-and-sweep steps. Defaults: pause 200%, step multiplier 100, step size 2^13 bytes.
- Generational mode runs minor collections over young objects and a "stop-the-world major collection" when memory stays high. Defaults: minor 20%, major 100%.
- Threads (coroutines) are collected objects ([manual §2.5](https://www.lua.org/manual/5.4/manual.html#2.5)).

**Roblox Luau.**
- Incremental mark, atomic and sweep phases.
- A PID pacer sets the GC rate from heap growth and the allocation rate. Fast-allocating scripts do "GC assists".
- A "remark" pass reduces the atomic step, which could pause "on the order of tens of milliseconds".
- "Writes to coroutine stacks don't use a write barrier, since that's prohibitively expensive". Coroutines are re-scanned instead.
- Objects of one size share 16 KB pages, so a paged sweeper runs ([performance](https://luau.org/performance)).

**Go.**
- Concurrent, non-moving mark-sweep. The GC takes 25% of CPU during marking, plus brief STW pauses at phase transitions, extra work on pointer writes during marking, and allocation assists ([GC guide](https://go.dev/doc/gc-guide)).
- Precise since 1.4. Goroutine stacks grow by copying, which requires knowing every pointer ([Go 1.4](https://go.dev/doc/go1.4)).
- The team rejected generational designs because "The write barrier was fast but it simply wasn't fast enough" ([Hudson, ISMM 2018](https://go.dev/blog/ismmkeynote)).

**.NET.**
- Generational (0, 1, 2 and the LOH) and compacting. Roots come from JIT stack info. Managed threads are suspended for a collection ([fundamentals](https://learn.microsoft.com/en-us/dotnet/standard/garbage-collection/fundamentals)).
- `SustainedLowLatency` suppresses foreground gen2 collections ([latency](https://learn.microsoft.com/en-us/dotnet/standard/garbage-collection/latency)). `TryStartNoGCRegion(totalSize)` reserves a budget during which no GC runs ([API](https://learn.microsoft.com/en-us/dotnet/api/system.gc.trystartnogcregion)).

## 5. Hot reload when live objects change layout

**What migration needs.** To keep state across a layout change, the runtime must:
1. Enumerate every live instance of each changed type.
2. Find or neutralize every stored reference to those instances.
3. Map old fields to new fields by name.
4. Do this at a safe point where no old code holds a raw pointer into an old layout.

**Prior art.**

| System | How it handles a changed layout |
|---|---|
| Unreal Live Coding | "Object Reinstancing" replaces live UCLASS/USTRUCT instances. Code holding pointers must refresh them in `ReloadReinstancingCompleteDelegate`. Without reinstancing, large structural changes "usually result in crashes" ([Live Coding](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-live-coding-to-recompile-unreal-engine-applications-at-runtime)). It relies on GC reflection; that the reference-replacement mechanism is GC reflection is my reading, unverified in source. |
| .NET Edit and Continue | Added fields "can't simply be glued onto existing object instances". Each object gets a lazily created side list hung off its SyncBlock, holding a dependent handle per added field ([encee.h L33-L42](https://github.com/dotnet/runtime/blob/8b9903ebf160fed45050edda447792363ce96dc0/src/coreclr/vm/encee.h#L33-L42), [L302-L325](https://github.com/dotnet/runtime/blob/8b9903ebf160fed45050edda447792363ce96dc0/src/coreclr/vm/encee.h#L302-L325)). Deleting or renaming members is unsupported ([VS docs](https://learn.microsoft.com/en-us/visualstudio/debugger/supported-code-changes-csharp)). |
| Live++ (C++) | The user writes pre/post-patch hooks that serialize, delete, re-create and restore objects. "Objects created on the stack cannot be migrated" ([docs](https://liveplusplus.tech/docs/documentation.html)). |
| Common Lisp (CLOS) | Instances are updated "no later than the next time a slot of that instance is read or written". Local slots in both versions keep their values; dropped slots are discarded ([CLHS 4.3.6](https://www.lispworks.com/documentation/HyperSpec/Body/04_cf.htm)). |
| Erlang/OTP | `code_change` returns the converted state during a release upgrade ([gen_server](https://www.erlang.org/doc/apps/stdlib/gen_server.html)). |
| Odin hot-reload template | Game state is one struct owned by the game DLL. If `memory_size()` changes, the template does a full reset instead of a reload. Old DLLs stay loaded because their string literals may still be referenced ([main_hot_reload.odin L147-L180](https://github.com/karl-zylinski/odin-raylib-hot-reload-game-template/blob/901bb85274b76272fce317a5b7b136d592b77ea6/source/main_hot_reload/main_hot_reload.odin#L147-L180)). Handmade Hero likewise keeps state in a platform-owned block ([day 21](https://guide.handmadehero.org/code/day021/)). |

**Fit per model.** This table is my analysis.

| Model | Find instances | Find references | Verdict |
|---|---|---|---|
| Typed pools + handles | The pool is the instance list | Handles are index + generation; they survive a re-layout unchanged | Best. Re-lay each changed pool's array; copy fields by name from reflection |
| Precise tracing GC | Heap walk with type maps | Same machinery as a compacting GC (forwarding) | Good, but you pay for barriers and stack maps every frame |
| Conservative GC (Boehm, Unity) | Heap walk, but some words are ambiguous | Can't rewrite ambiguous roots | Poor |
| RC (any) | Only if each type keeps a live list | Raw pointers anywhere, including C | Poor unless objects live in pools |
| Arenas | Untyped bytes | Raw pointers | Only empty or untyped arenas survive. Frame arenas are empty at a frame-boundary safe point |
| Goose data stacks | Inline variable-size values | Specialized code bakes in layouts | Not designed for it. `to_bytes` images are tied to a compiler version ([§4.9](https://github.com/aardappel/goose/blob/f4637bfb342fb70d42bea0474eab9e62a3c99d68/docs/design/coroutines.md#L829-L832)) |

## 6. Measured numbers (kept apart from claims)

| Source | Result | Hardware, version, workload |
|---|---|---|
| [Nim ORC post](https://nim-lang.org/blog/2020/12/08/introducing-orc.html) (2020-12-08) | With periodic `GC_fullCollect`: ORC 320.49 µs average and 6.24 ms max latency; M&S 65.31 ms and 204.79 ms. Without it: ORC 274.84 µs / 1.10 ms; M&S (memForSpeed) 1.49 ms / 46.41 ms. Max memory 137 MiB vs 333 MiB | Async HTTP server holding about 135 MB of JSON, `wrk`; hardware not stated |
| [Vale](https://verdagon.dev/blog/generational-references) | Unsafe 43.82 s; naive RC 54.90 s (+25.29%); generational references 48.57 s (+10.84%) | BenchmarkRL terrain generator; hardware and version not stated |
| [Lobster](https://aardappel.github.io/lobster/memory_management.html) | About 95% of runtime RC operations removed | Several dozen Lobster programs; no timings |
| [PEP 703](https://peps.python.org/pep-0703/) | Free-threaded CPython overhead: 6% on Skylake and 5% on Zen 3 single-threaded; 8% and 7% multi-threaded. "The largest contribution ... is biased reference counting" | pyperformance 1.0.6, against commit `018be4c` |
| [Go, Hudson](https://go.dev/blog/ismmkeynote) (2018) | Twitter production pauses: 300-400 ms, then 30-40 ms (Go 1.5), then 4-5 ms, then under 10 ms (1.6.3), then sub-millisecond (2017). Stated objective: 500 µs STW per cycle. Barrier cost "about 4-5%" is the talk's claim, not shown data | Pauses measured by Twitter; hardware not stated |
| [Unreal, Epic staff KB](https://forums.unrealengine.com/t/knowledge-base-garbage-collector-internals/501800) (2022) | About 600k UObjects: GC 18.11 ms without clusters, 11.47 ms with clusters. Reachability 17.5 ms → 0.85 ms, but MarkObjectsAsUnreachable 0.02 ms → 10.1 ms | Engine version and hardware not stated; the log shows 126 threads |
| [Luau](https://luau.org/performance) | Paged sweeper is 2-3x faster than a linked-list sweep and saves 16 bytes per object | No hardware stated |
| [Goose results](https://github.com/aardappel/goose/blob/f4637bfb342fb70d42bea0474eab9e62a3c99d68/bench/results.md#L38-L50) | Geometric mean over 16 benchmarks: 3.51x (MSVC) and 3.54x (clang) faster than idiomatic C++; 1.24x/1.26x faster than hand-optimized C++; 1.13x/1.19x faster than the best safe Rust; peak memory 1.95x/1.32x/1.25x lower ([README L412-L416](https://github.com/aardappel/goose/blob/f4637bfb342fb70d42bea0474eab9e62a3c99d68/README.md#L412-L416)) | Ryzen 9 9950X3D, Windows 11, MSVC 19.51, clang-cl 22.1.3, rustc 1.98.0, 2026-10-16. Run by the author |
| [Koka readme](https://github.com/koka-lang/koka/blob/7e9c1e593a46b704c915832c2da702b7ec41f45f/readme.md#L177-L230) | Results are only in a figure, which I did not read | AMD 3600XT, Ubuntu 20.04, Nov 2020; Koka 2.0.3 vs OCaml, GHC, Swift 5.3, Java 15 G1, gcc 9.3 |

## 7. Coroutines and tasks: who owns what a suspended frame holds

| Model | Where the frame lives | What it holds across a suspension | Risk |
|---|---|---|---|
| Stackless frame as a value (Goose design, Rust futures) | Wherever the program stores it, for example a pool ([Goose §4.8-4.9](https://github.com/aardappel/goose/blob/f4637bfb342fb70d42bea0474eab9e62a3c99d68/docs/design/coroutines.md#L793-L835)) | Locals live across the yield | References into the frame itself need `Pin` in Rust ([std::pin](https://doc.rust-lang.org/std/pin/index.html)). Goose forbids them (rule R4) |
| Swift tasks | Task-local stack allocator ([TaskAlloc.cpp](https://github.com/swiftlang/swift/blob/25983423c97fabeb2ee68995cfa191ca86417545/stdlib/public/Concurrency/TaskAlloc.cpp#L12-L18)) | Owned (+1) references | Retain cycles through captured `self` (inferred from ARC rules, not measured) |
| RC + async (Nim) | Heap | Strong references | `--mm:arc` async leaks; ORC's cycle collector is needed ([mm](https://nim-lang.org/docs/mm.html)) |
| Tracing GC (Lua, Luau, Unity, Go) | GC heap object or goroutine stack | Traced references | Unity allocates per start ([coroutines](https://docs.unity.com/en-us/engine/current/manual/scripting/coroutines-section/coroutines)). Luau re-scans coroutine stacks instead of using barriers ([Luau](https://luau.org/performance)). Go needs precise stack maps to scan and copy stacks ([Go 1.4](https://go.dev/doc/go1.4)) |
| Arenas | Anywhere | Pointers into a frame/temp arena | A temp arena reset every frame ([Odin](https://github.com/odin-lang/odin-lang.org/blob/1d272b93ccbd9a7e23f21cdbc2a6b5d5b8e5daa4/content/docs/overview.md#L3725-L3728)) leaves dangling pointers in any task that spans frames. Analysis, not measured |
| Handles | Pool | Integers | A stale handle is caught on resume by the generation check. Frames are flat and serializable |

Hot reload adds one more problem: a frame's saved-local layout and resume points depend on the code version. The options are to map frames by (function, yield point) and saved-local name, or to restart changed coroutines. Goose ties frame images to the compiler version ([§4.9](https://github.com/aardappel/goose/blob/f4637bfb342fb70d42bea0474eab9e62a3c99d68/docs/design/coroutines.md#L829-L832)). .NET Hot Reload supports edits to async methods but not changes to active statements ([VS docs](https://learn.microsoft.com/en-us/visualstudio/debugger/supported-code-changes-csharp)).

## 8. WebAssembly constraints

- Wasm code can't scan its own locals or value stack. The proposal for "Linear-Memory GC-Root Marking" calls this "the main obstacle" ([WebAssembly/design#1459](https://github.com/WebAssembly/design/issues/1459)). Most locals live on the hidden wasm stack, not the scannable shadow stack ([emscripten#17131](https://github.com/emscripten-core/emscripten/issues/17131)). A tracing GC in linear memory therefore needs pointers spilled to a shadow stack, or must collect only when no pointers are on the stack.
- Wasm has no address-space reservation. Goose's data stacks fall back to slower bounds-checked growth there ([spec §10.4](https://github.com/aardappel/goose/blob/f4637bfb342fb70d42bea0474eab9e62a3c99d68/docs/goose_spec.md#L4294-L4298)). Any design that relies on "reserve 2 GB, never move" needs a second path on wasm.
- Unity doesn't support incremental GC on the web platform ([Unity](https://docs.unity.com/en-us/engine/current/manual/analysis/performance-memory/managed-memory/incremental-garbage-collection)).

---

## 9. Candidate designs for chibi-v2

### A. Typed pools with generational handles, scoped arenas, and a "persistable type" rule

**Shape.**
- Values are plain, copied data. Dynamic arrays are plain buffers with no copy-on-write.
- `pool T` is reflected storage per type. `handle T` is a 32-bit index plus a 32-bit generation, and 0 means none.
- Arenas have explicit lifetimes: frame, level and program.
- Raw pointers `^T` are for local, transient use only.

**The rule.** A type is *persistable* if it contains no raw pointers. Pools, globals and coroutine frames may hold only persistable types. Values in these places reach each other through handles, or through offsets within one arena.

| Cost | Estimate |
|---|---|
| Compiler | One bit per type, computed once. A check at stores into pool, global and frame slots. Reflection tables are needed anyway for the reflection requirement. Under 1% of the per-line budget (estimate) |
| Per assignment / call | 0 / 0. The allocator is passed explicitly or kept in a thread-local; whether to have an Odin-style implicit `context` is open |
| Per dereference | Index → slot, one generation load, compare and branch, as in [floooh](https://floooh.github.io/2018/06/17/handles-vs-pointers.html) |
| Per frame | Frame arena reset, O(1). Destroy is a generation bump plus a freelist push |
| Pauses / cycles | None. The program owns teardown; leaks show up as pool counts |
| Hot reload | At the frame-boundary safe point there are no raw pointers live (by the rule) and the frame arena is empty. For each changed type, re-lay its pool by field name; handles remain valid. Long-lived arenas must hold only types whose layout didn't change, or be rebuilt |
| Coroutines | Stackless frames stored in a pool, persistable by the same rule (Goose R1/R2, but checked by type). Reload maps frames by yield point, or restarts them |
| Threads | Systems own pools. Handles are integers, so messages are a `memcpy`. No atomics on dereference |
| Gaps | A raw pointer into a pool can dangle within a frame if the pool grows. Fixes: reserve virtual memory where available (Goose-style; not on wasm), or forbid growth while pointers are live, or check in debug builds only. Use-after-free on handles is detected; dangling arena pointers are not, except by debug poisoning |

### B. Move-by-default ownership plus Perceus-style RC for opt-in sharing

**Shape.**
- Owning values (buffers, strings) move by default and drop at last use or scope end (Zen C semantics, Perceus placement).
- `rc T` is opt-in. Its count is non-atomic, with the sign bit marking thread-shared objects (as in [Koka](https://github.com/koka-lang/koka/blob/7e9c1e593a46b704c915832c2da702b7ec41f45f/kklib/include/kklib.h#L733-L775) and [Lean](https://github.com/leanprover/lean4/blob/392f865691c407ff3500c90d4a52f4156edc9dbc/src/include/lean/lean.h#L149-L160)).
- Parameters are borrowed by default, so there is no interprocedural inference.

| Cost | Estimate |
|---|---|
| Compiler | A forward move-check pass and a backward liveness pass per function over the IR, plus drop code per type. About 3-12% of the per-line budget (estimate from §3) |
| Per assignment | `rc` only: an increment, and a decrement with a free branch on the old value. Plain owned values: 0 |
| Per call | 0 with borrowed parameters |
| Per frame | 0. Large drops cascade unless frees go to a queue drained under a per-frame budget |
| Pauses / cycles | Cascading frees. Cycles leak; report them at exit as Lobster does, or provide `weak` |
| Hot reload | Weak on its own. Finding instances and references requires `rc` objects to live in per-type pools, which turns this into design A plus counts |
| Coroutines | Frames own references, which keeps targets alive (safe) but can leak through task cycles (Nim's async under ARC) |
| Threads | Non-atomic until an object is marked shared; atomic after that |

### C. A managed tier collected only at frame boundaries, plus manual core data

**Shape.** This follows Unreal's split.
- Gameplay objects marked `managed` live in per-type pages with precise type maps from reflection (Luau-style pages).
- Everything else follows design A.
- Collection runs only at safe points where no managed pointer is live in a native frame: the main-loop frame boundary and coroutine yields. Roots are then globals, pools and coroutine frames, all with type maps. **No stack maps are needed.**

| Cost | Estimate |
|---|---|
| Compiler | Type maps per type. A write barrier at stores of managed pointers, needed only for incremental marking. About 1-2% of the per-line budget (estimate) |
| Per assignment | A barrier on managed-pointer stores while marking. Go's talk claims about 4-5% typical overall; Unity documents "some overhead" |
| Per frame | One mark/sweep slice under a budget (Unreal's example is 2 ms; Unity uses VSync idle time) |
| Pauses / cycles | An atomic phase. Its size scales with the live managed heap unless it is incremental with a remark (Luau). Cycles are collected |
| Hot reload | Strong. The collector knows every object and every reference, so it can re-lay and forward objects (Unreal reinstancing, CLOS) |
| Coroutines | Frames are traced objects, so there are no leaks |
| Threads | The managed heap is single-threaded (Unreal's incremental mode is "not fully thread safe"); jobs use values and handles |
| Risk | The predecessor language's tracing collector slowed it down. This design limits collection to opted-in types and frame boundaries, and drops copy-on-write. On wasm, collecting only at frame boundaries avoids stack scanning (§8) |

## 10. Questions the owner must answer

1. Must use-after-free be impossible in release builds, or only detected in debug builds? This chooses between handle checks always on, Vale-style checks, and plain pointers.
2. May long-lived data reach other data only through handles or offsets, never raw pointers? If yes, design A works without a GC. If no, hot reload needs a tracing collector's reference knowledge (design C).
3. Which layout changes must keep state: only types in pools and globals, or also values on the native stack and inside arrays of other types? Live++ and CLOS both give up on stack objects.
4. Where are reload safe points: only at the frame boundary, or also inside running coroutines? Should changed coroutines be migrated by yield point, or restarted?
5. Are threads share-nothing with flat messages (Goose), or is there a shared heap with jobs? This decides whether any count or barrier must be atomic.
6. How important is wasm? It rules out stack scanning and makes "reserve, never move" storage a second code path.
7. Implicit allocator context (Odin passes one pointer per call), a thread-local, or explicit parameters (Zig)?
8. Must the C side (Cute Framework, SDL3) hold pointers into language-owned memory across frames? If so, those objects must never move, which rules out compaction and pool growth by reallocation.
9. Is per-object cleanup (`Drop`/RAII) wanted, or only `defer` plus explicit destroy?
10. What share of the 1M-lines-per-second budget may memory analysis take? This decides whether B's liveness pass is affordable.

## 11. Unverified or not found

- Jai details beyond the existence of the context and temporary storage (no public docs; only talk videos).
- Biased RC paper numbers for Swift ([PACT'18 PDF](https://iacoma.cs.uiuc.edu/iacoma-papers/pact18.pdf)); the PDF text was unreadable to my tools.
- The Perceus paper's tables, and the Koka benchmark figure values.
- Unity's 2018 incremental-GC blog numbers (the post now returns 404).
- The UE 4.11 release-note claim of a "9x reduction" in GC time (the blog returned 403; seen only quoted on forums).
- Unreal's default `gc.IncrementalReachabilityTimeLimit` (5 ms per a search snippet of the console-variable reference; not re-read).
- That Unreal reinstancing finds references through the same reflection the GC uses.
- .NET's card-marking barrier details. Primary-source pause times for .NET in games.
- Goose compile speed. Goose's coroutine "within 10%" figure, which comes from a community sketch I didn't inspect.
- The Apple M4 clock rate used in the budget estimate.

## Code and docs to read first

1. [Goose spec §1.2-1.3](https://github.com/aardappel/goose/blob/f4637bfb342fb70d42bea0474eab9e62a3c99d68/docs/goose_spec.md#L64-L117), [§9.2-9.4](https://github.com/aardappel/goose/blob/f4637bfb342fb70d42bea0474eab9e62a3c99d68/docs/goose_spec.md#L3786-L4006) and [§10](https://github.com/aardappel/goose/blob/f4637bfb342fb70d42bea0474eab9e62a3c99d68/docs/goose_spec.md#L4227-L4298): how a no-heap model stays safe, what still needs runtime checks, and what root specialization costs the compiler.
2. [Goose coroutines design §2 and §4.8-4.12](https://github.com/aardappel/goose/blob/f4637bfb342fb70d42bea0474eab9e62a3c99d68/docs/design/coroutines.md#L160-L952): frame-as-value lowering, the yield rule, and the cost table. This is the closest match to chibi-v2's needs.
3. [Weissflog, Handles are the better pointers](https://floooh.github.io/2018/06/17/handles-vs-pointers.html): handle bit layout, generation overflow, and the rule against stored pointers that design A depends on.
4. [Odin hot-reload template, main_hot_reload.odin L147-L180](https://github.com/karl-zylinski/odin-raylib-hot-reload-game-template/blob/901bb85274b76272fce317a5b7b136d592b77ea6/source/main_hot_reload/main_hot_reload.odin#L147-L180): what a manual-memory language does today when state layout changes (it restarts).
5. [CoreCLR encee.h L33-L42, L302-L360](https://github.com/dotnet/runtime/blob/8b9903ebf160fed45050edda447792363ce96dc0/src/coreclr/vm/encee.h#L302-L360): how a production runtime adds fields to live objects without moving them, and why it can't delete them.
6. [Unreal Live Coding reinstancing](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-live-coding-to-recompile-unreal-engine-applications-at-runtime) and [incremental GC](https://dev.epicgames.com/documentation/en-us/unreal-engine/incremental-garbage-collection-in-unreal-engine): reinstancing needs reflected references, and incremental marking needs a write barrier.
7. [Koka kklib.h L102-L116, L733-L775](https://github.com/koka-lang/koka/blob/7e9c1e593a46b704c915832c2da702b7ec41f45f/kklib/include/kklib.h#L733-L775) and [Lean lean.h L149-L160](https://github.com/leanprover/lean4/blob/392f865691c407ff3500c90d4a52f4156edc9dbc/src/include/lean/lean.h#L149-L160): the one-branch, non-atomic RC fast path, with the sign bit for thread-shared objects.
8. [Lobster memory management](https://aardappel.github.io/lobster/memory_management.html) and [Nim mm](https://nim-lang.org/docs/mm.html) with the [ORC post](https://nim-lang.org/blog/2020/12/08/introducing-orc.html): how much RC work cheap analyses remove, and what cycles and async cost.
9. [Luau performance, GC section](https://luau.org/performance): pacing, the remark step, coroutines without barriers, and paged sweeping. A good template if design C is chosen.
10. [Go ISMM keynote](https://go.dev/blog/ismmkeynote) and [GC guide](https://go.dev/doc/gc-guide): why write-barrier cost drove Go away from generational GC, and where latency comes from.
11. [Odin overview: context and allocators](https://github.com/odin-lang/odin-lang.org/blob/1d272b93ccbd9a7e23f21cdbc2a6b5d5b8e5daa4/content/docs/overview.md#L3665-L3728) and [Zig langref: Memory](https://github.com/ziglang/zig/blob/738d2be9d6b6ef3ff3559130c05159ef53336224/doc/langref.html.in#L6280-L6476): implicit context vs explicit allocator parameters, and the per-frame arena pattern.
12. [Vale generational references](https://verdagon.dev/blog/generational-references): check-per-dereference cost, and how regions remove the checks.
13. [Zen C move_check.c](https://github.com/Zuhaitz-dev/zenc-lang/blob/b3f550e938be3fd395e9ebf657669f89dbb3453b/src/analysis/move_check.c#L92-L340) and its [memory chapter](https://github.com/zenc-lang/docs/blob/60115e029d02e9cea4c9f721da5c82cd7b257de5/reference/08-memory-management.md): a minimal move checker. Look for the string-keyed state to avoid.
14. [WebAssembly/design#1459](https://github.com/WebAssembly/design/issues/1459): why wasm blocks stack scanning, which matters for any GC design.
