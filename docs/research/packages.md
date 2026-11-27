# Packages and modules: Carbon, Zen C, C3, Zig, Odin, Go, Jai

Research date: 2026-11-09. Code links point at fixed commits or tags (listed under "Versions read"). Items marked **UNVERIFIED** rest on secondary sources or inference.

## Summary for chibi-v2

- The languages fall into two families. **Whole-program** compilers keep every package in one semantic graph: Zig, Odin, Jai, C3 and Zen C. **Separate** compilers check one unit at a time against interface data: Go and Carbon. Whole-program makes comptime and generics easy. Separate compilation gives caching and parallelism across the package graph.
- Go, C3, Odin and Zig make top-level order irrelevant in the same way. They first register every name, then resolve each declaration on demand with a three-state mark (not started, in progress, done). Carbon instead requires declaration before use, so that checking is one pass.
- Parallelism inside the checker is the main open problem. Zig's Sema is still single-threaded; its own numbers work out to about 36k lines/s cold. Odin made its checker fully parallel in October 2026: each entity is claimed atomically, and other threads wait on it.
- Go is the model for caching. It emits export data before codegen, starts dependents early, and keys rebuilds on the hash of the export data. Its cost is redundant work: every importing package instantiates generics again, which made Go 1.18 compiles about 15% slower.
- None of the seven languages has Edit and Continue that keeps state. Zig patches single functions through the GOT, experimentally. Odin swaps a whole DLL by convention. The others have nothing.
- **Best fit for chibi-v2:** package = directory; no cycles between packages; order-free inside a package; the whole program in one process; packages as the scheduling and cache unit, each with a hashed interface; one global table of generic instances; functions as the patch unit, behind indirection.

## Versions read

| Language | Source | Pin |
|---|---|---|
| Zig | github.com/ziglang/zig (frozen mirror; the project [moved to Codeberg](https://github.com/ziglang/zig)) | tag `0.15.2` for line numbers. Names checked at Codeberg tag `0.17.1` (`be248cc1`) |
| Carbon | github.com/carbon-language/carbon-lang | `ca1898506be64134c3a25a5381622667a25cf67f` (2026-11-06) |
| C3 | github.com/c3lang/c3c | `b2e1e99baaf2add3a40bd084823416a4437f3ec6` (2026-11-09; latest release `v0.8.5_1`) |
| Zen C | github.com/Zuhaitz-dev/zenc-lang; docs at zenc-lang/docs | `b3f550e938be3fd395e9ebf657669f89dbb3453b` (2026-10-25); docs `60115e02` |
| Odin | github.com/odin-lang/Odin; docs at odin-lang/odin-lang.org | `85afcb386c389aafcec4f2e33d27ff8b42579822` (2026-11-09); docs `1d272b93` |
| Go | github.com/golang/go | `6e61706b976dff01299814ed319bbea95722009e` (2026-11-09) |
| Jai | no public source | talks and Thekla posts only |

### What "Zen C" is

Zen C is [Zuhaitz-dev/zenc-lang](https://github.com/Zuhaitz-dev/zenc-lang/blob/b3f550e938be3fd395e9ebf657669f89dbb3453b/README.md), a "modern systems programming language" (about 4.3k stars). Its compiler, `zc`, is written in C and outputs readable GNU C / C11. The `zenc-lang` GitHub org holds its [docs](https://github.com/zenc-lang/docs), editor plugins and an empty RFC repo.

Other projects found:
- `zenc-lang-ce/zenc`, a fork of Zen C.
- connectFree's "Zen", a closed-source Zig derivative from about 2020 ([Zig statement](https://ziglang.org/news/statement-regarding-zen-programming-language/)). It is not called Zen C and is inactive.
- Unrelated projects (ZenCart, ZenCode).

Zen C is the only relevant systems language among them.

## Comparison tables

### Units, imports, visibility, cycles

| | Unit of compilation | Import syntax and resolution | Visibility | Package cycles |
|---|---|---|---|---|
| **Zig** | One `Zcu` holds all Zig code of a build. Files are structs. Modules are import-table scopes inside the Zcu | `@import("name")` looks up the module's `deps` table, filled by `-M`/`--dep` from build.zig. `@import("x.zig")` resolves relative to the file | `pub` or file-private. There is no module-private level | Allowed, for files and for modules |
| **Carbon** | Library = one API file plus impl files. The toolchain compiles per file | `import Pkg library "x";` in a header that needs only parsing. Every API file must be passed to the same process | Public by default in the API file. `private` = library-private. Impl-only names are effectively file-private | Forbidden |
| **C3** | Whole program, including the stdlib. A module is a namespace spread over files; it is also the unit of codegen | `import a::b;` imports sub-modules recursively. Name lookup only among parsed modules; files come from `project.json` | Public by default. `@private` = module-private, `@local` = file-private | Allowed (no check) |
| **Zen C** | Whole program becomes one C translation unit. Module = file | `import "path.zc" [as m]`, found by search path and parsed in place | No `pub`/`private`. `opaque` types only | Forbidden |
| **Odin** | Package = directory. Whole program | `import "core:fmt"` (collection) or a relative path | Public by default. `@(private)` = package, `@(private="file")` = file | Forbidden |
| **Go** | Package = directory. One `compile` process per package | Import path = module path + subdirectory. The compiler sees only direct deps' export data | Upper-case names are exported. `internal/` directories | Forbidden |
| **Jai** | Whole program per workspace (**UNVERIFIED**) | `#import "Module"` (search paths) and `#load "file"` (**UNVERIFIED**) | `#scope_file`, `#scope_module`, `#scope_export` (**UNVERIFIED**) | **UNVERIFIED** |

### Order, parallelism, generics, build

| | Top-level order | Parallel and incremental | Generics and comptime across units | Build description |
|---|---|---|---|---|
| **Zig** | Free. Lazy analysis on demand from roots | Parallel AstGen and codegen. Single-threaded Sema. Incremental only in a resident `--watch` process | One InternPool. Instances memoized by comptime args | `build.zig` (Zig program) + `build.zig.zon` |
| **Carbon** | Matters. Declare before use; single pass | Files checked in topological order, serially. No serialized IR | Checked generics. Specifics emitted `linkonce_odr` in every user file | `carbon compile/link/build`; Bazel rules. No package manager |
| **C3** | Free. Lockstep stages + memoized on-demand resolution | Single-threaded front end. Per-module LLVM optimize/emit in a thread pool. No cache | Instances live in the generic's defining module. `$` macros, no general CTFE | `project.json`, `.c3l` libraries |
| **Zen C** | Free for function bodies: bodies are deferred, then structs are sorted when emitting C | Single-threaded. One C compile. No cache | Monomorphized while parsing. A tree-walking `comptime` interpreter emits text | None; `//>` directives in source |
| **Odin** | Free. Dependency graph + SCCs; entities claimed atomically | Parallel parse and check; per-package LLVM modules. No incremental build | One global instance table. No general CTFE | None; `odin build <dir>` + flags |
| **Go** | Free (lazy `objDecl`). Init order from dependencies | Package DAG in parallel; export-first pipelining; content-addressed cache | Generic bodies in export data. Instantiated per importer, linker dedups | `go.mod`, `go.sum`, `go.work` |
| **Jai** | Free (**UNVERIFIED**) | Whole program, from source every build (**UNVERIFIED**) | `#run` arbitrary CTFE; `$T` polymorphs | A Jai metaprogram (`build.jai`) |

### Hot reload and memory

| | Unit replaced on change | What the compiler keeps in memory |
|---|---|---|
| **Zig** | One function or global (`updateFunc`/`updateNav`) patched through the GOT. Experimental; no safe points | Whole InternPool, source/AST/ZIR per file plus previous ZIR, dependency and reference graphs |
| **Carbon** | Not designed | Tokens, parse tree and SemIR of every input and Core file, plus `ImportRef`s per importer |
| **C3** | Not designed ([#1968](https://github.com/c3lang/c3c/issues/1968) is open) | ASTs of the whole program and stdlib, symbol tables per module and per file |
| **Zen C** | Not designed | Whole AST; types stored as strings |
| **Odin** | Whole DLL (`-build-mode:dll` + `core:dynlib`) | Whole program, in a permanent arena |
| **Go** | Nothing can be replaced (`plugin` cannot unload) | One package + its direct deps' export data |
| **Jai** | No statement found | Whole program (**UNVERIFIED**) |

## Zig

**Unit.**
- The header of `Zcu.zig` says all Zig source of a compilation is one `Zcu`, and each `Compilation` has zero or one ([Zcu.zig#L1-L6](https://github.com/ziglang/zig/blob/0.15.2/src/Zcu.zig#L1-L6)).
- `@import` returns a file as if it were `struct { FILE_CONTENTS }` ([docs](https://ziglang.org/documentation/0.15.2/#import)).
- A module is a root directory, a `deps` table and per-module options such as target and optimize mode ([Package/Module.zig#L3-L36](https://github.com/ziglang/zig/blob/0.15.2/src/Package/Module.zig#L3-L36)).
- A package is a build-system concept: a dependency in `build.zig.zon`, identified by content hash ([build.zig.zon.md](https://github.com/ziglang/zig/blob/0.15.2/doc/build.zig.zon.md)).

**Imports.**
- `doImport` special-cases `std`, `root` and `builtin`. Other names go through `importer.mod.deps`. A string ending in `.zig` or `.zon` is a path relative to the importing file ([PerThread.zig#L1967-L2012](https://github.com/ziglang/zig/blob/0.15.2/src/Zcu/PerThread.zig#L1967-L2012)).
- The module table comes from `-M<name>=<root>` and `--dep` ([main.zig#L1065-L1100](https://github.com/ziglang/zig/blob/0.15.2/src/main.zig#L1065-L1100)). The build system emits those flags from `b.addModule`/`b.createModule` and `Module.addImport` ([Build.zig#L904-L916](https://github.com/ziglang/zig/blob/0.15.2/lib/std/Build.zig#L904-L916), [Build/Module.zig#L325-L330](https://github.com/ziglang/zig/blob/0.15.2/lib/std/Build/Module.zig#L325-L330)).
- AstGen threads discover imports and deduplicate them by canonical path ([PerThread.zig#L1895-L1966](https://github.com/ziglang/zig/blob/0.15.2/src/Zcu/PerThread.zig#L1895-L1966)).

**Visibility.**
- A non-`pub` declaration is visible only in its own file: the check is `src_file == namespace.file_scope` ([Sema.zig#L6600-L6609](https://github.com/ziglang/zig/blob/0.15.2/src/Sema.zig#L6600-L6609)).
- A path import outside the module root is an error ([PerThread.zig#L2251-L2268](https://github.com/ziglang/zig/blob/0.15.2/src/Zcu/PerThread.zig#L2251-L2268)).
- A file may belong to only one module ([Zcu.zig#L144-L151](https://github.com/ziglang/zig/blob/0.15.2/src/Zcu.zig#L144-L151)).

**Order: lazy, demand-driven.**
- The docs say container-level declarations are "order-independent and lazily analyzed" ([docs](https://ziglang.org/documentation/0.15.2/#Container-Level-Variables)).
- `scanNamespace` creates a `Nav` for every declaration. It queues analysis only for `comptime` blocks, tests and `export`s; `const` and `var` get `want_analysis = false` ([PerThread.zig#L2742-L2782](https://github.com/ziglang/zig/blob/0.15.2/src/Zcu/PerThread.zig#L2742-L2782)).
- The roots are the main module and std ([Zcu.zig#L269-L276](https://github.com/ziglang/zig/blob/0.15.2/src/Zcu.zig#L269-L276)).
- When a declaration is used, `ensureNavResolved` records a reference, reports "dependency loop detected" if the unit is already in `analysis_in_progress`, and otherwise recurses into `ensureNavTypeUpToDate` or `ensureNavValUpToDate` ([Sema.zig#L31219-L31257](https://github.com/ziglang/zig/blob/0.15.2/src/Sema.zig#L31219-L31257), [PerThread.zig#L929-L1000](https://github.com/ziglang/zig/blob/0.15.2/src/Zcu/PerThread.zig#L929-L1000)).
- Analysis units (`AnalUnit`) are `comptime`, `nav_val`, `nav_ty`, `type`, `func` and `memoized_state` ([InternPool.zig#L393-L453](https://github.com/ziglang/zig/blob/0.15.2/src/InternPool.zig#L393-L453)). In 0.17, `type` was split into `type_layout` and `struct_defaults` ([Codeberg InternPool.zig](https://codeberg.org/ziglang/zig/src/tag/0.17.1/src/InternPool.zig), [devlog 2026-03-10](https://ziglang.org/devlog/2026/#2026-03-10)).
- Side effect: code that nothing references is never checked. Under incremental builds, errors from unreferenced units are dropped ([Compilation.zig#L4091-L4095](https://github.com/ziglang/zig/blob/0.15.2/src/Compilation.zig#L4091-L4095)).

**Cycles.**
- File import cycles are allowed; `Zcu.zig` and `Compilation.zig` import each other ([Zcu.zig#L21](https://github.com/ziglang/zig/blob/0.15.2/src/Zcu.zig#L21), [Compilation.zig#L36](https://github.com/ziglang/zig/blob/0.15.2/src/Compilation.zig#L36)).
- Module cycles are allowed; the 0.14 notes show mutual `addImport` ([0.14.0 notes](https://ziglang.org/download/0.14.0/release-notes.html#Creating-Artifacts-from-Existing-Modules)).

**Parallelism and caching.**
- AstGen runs one pool job per file ([Compilation.zig#L4837-L4876](https://github.com/ziglang/zig/blob/0.15.2/src/Compilation.zig#L4837-L4876)).
- Sema runs on the "(at least for now) single-threaded main work queue" ([Compilation.zig#L4603-L4607](https://github.com/ziglang/zig/blob/0.15.2/src/Compilation.zig#L4603-L4607)).
- Codegen from AIR to MIR runs on pool threads, while emit and link run on one thread. The devlog describes one Sema thread, many codegen threads and one link thread, and calls parallel Sema the "more distant future" ([Compilation.zig#L5083-L5084](https://github.com/ziglang/zig/blob/0.15.2/src/Compilation.zig#L5083-L5084), [devlog 2025-06-14](https://ziglang.org/devlog/2025/#2025-06-14)).
- **Cache modes** ([Compilation.zig#L1549-L1581](https://github.com/ziglang/zig/blob/0.15.2/src/Compilation.zig#L1549-L1581)):
  - `whole` hashes all inputs. Any edit means a full re-run.
  - `incremental` patches the previous binary.
- The only per-file reuse across processes is cached ZIR ([PerThread.zig#L99-L168](https://github.com/ziglang/zig/blob/0.15.2/src/Zcu/PerThread.zig#L99-L168)).
- Incremental state lives only in a running compiler: `saveState` writes a file, but the notes say loading it is not implemented ([0.14.0 notes](https://ziglang.org/download/0.14.0/release-notes.html#File-System-Watching)).
- In 0.17, `-fincremental --watch` works for "most projects targeting x86_64-linux". A Mach-O linker and incremental builds without `--watch` are future work ([0.17.0 notes](https://ziglang.org/download/0.17.0/release-notes.html#Incremental-Compilation)).
- **Invalidation:** `updateZirRefs` compares a source hash per declaration between the old and new ZIR and marks dependents outdated ([PerThread.zig#L497-L507](https://github.com/ziglang/zig/blob/0.15.2/src/Zcu/PerThread.zig#L497-L507), [Zcu.zig#L249-L259](https://github.com/ziglang/zig/blob/0.15.2/src/Zcu.zig#L249-L259)).

**Generics and comptime.**
- There is no export data: every module shares one `InternPool` ([Zcu.zig#L175](https://github.com/ziglang/zig/blob/0.15.2/src/Zcu.zig#L175)).
- `getFuncInstance` interns the generic owner plus the comptime args, so each instance exists once per build ([InternPool.zig#L9556-L9640](https://github.com/ziglang/zig/blob/0.15.2/src/InternPool.zig#L9556-L9640)).
- Comptime call memoization is turned off under incremental compilation ([Sema.zig#L7707-L7718](https://github.com/ziglang/zig/blob/0.15.2/src/Sema.zig#L7707-L7718)). The tracking issue says memoization matters for speed ([#21165](https://github.com/ziglang/zig/issues/21165)).

**Build.**
- `zig build` compiles `build.zig` together with `build_runner.zig` and runs the result. The result is a DAG of steps that run concurrently ([main.zig#L5185-L5191](https://github.com/ziglang/zig/blob/0.15.2/src/main.zig#L5185-L5191), [learn/build-system](https://ziglang.org/learn/build-system/)).
- 0.17 split the configurer and maker processes ([0.17.0 notes](https://ziglang.org/download/0.17.0/release-notes.html#Separate-the-Maker-Process-from-the-Configurer-Process)).

**Hot reload.**
- Issue [#68](https://github.com/ziglang/zig/issues/68), "hot code swapping", has been open since 2016.
- Andrew Kelley: incremental compilation and linking is "the same problem as hot code swapping" ([HN](https://news.ycombinator.com/item?id=30716177)).
- The compiler has a `hot_update` message, which leads to `hotCodeSwap` ([Client.zig#L22-L28](https://github.com/ziglang/zig/blob/0.15.2/lib/std/zig/Client.zig#L22-L28), [main.zig#L4174-L4195](https://github.com/ziglang/zig/blob/0.15.2/src/main.zig#L4174-L4195)).
- The linker attaches to the child process with ptrace ([link.zig#L571-L605](https://github.com/ziglang/zig/blob/0.15.2/src/link.zig#L571-L605)) and rewrites one function or global at a time ([link.zig#L714-L790](https://github.com/ziglang/zig/blob/0.15.2/src/link.zig#L714-L790)).
- Calls go through the GOT, so callers stay valid ([Jakub Konka, 2022](https://www.jakubkonka.com/2022/03/16/hcs-zig.html)).
- There are no safe points and no state or layout migration.

**Numbers.**

| What | Result | Source |
|---|---|---|
| Tokenize + parse + AstGen of std, 292k lines, 8 cores | 33 ms, about 8.9M lines/s | [0.8.0 notes](https://ziglang.org/download/0.8.0/release-notes.html#Whole-File-AST-Lowering) |
| Errors-only check of a half-million-line codebase | 14 s cold, 63 ms per edit | [0.14.0 notes](https://ziglang.org/download/0.14.0/release-notes.html#Incremental-Compilation) |
| Self-hosted x86 backend | compiler build 75 s → 20 s | [devlog 2025-06-08](https://ziglang.org/devlog/2025/#2025-06-08) |
| `-fincremental --watch` on the compiler | 36 s first build, then 228–288 ms per edit | [devlog 2026-05-30](https://ziglang.org/devlog/2026/#2026-05-30) |

The 14 s cold check works out to about 36k lines/s through Sema. That figure is our arithmetic; which codebase was measured is **UNVERIFIED**.

## Carbon

Shorthand for paths below:
- `README` = [docs/design/code_and_name_organization/README.md](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/docs/design/code_and_name_organization/README.md).
- `pNNNN` = the file in [proposals/](https://github.com/carbon-language/carbon-lang/tree/ca1898506be64134c3a25a5381622667a25cf67f/proposals).

**Unit.**
- A library has exactly one API file and any number of implementation files. A package is the unit of distribution ([README#L139-L167](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/docs/design/code_and_name_organization/README.md#L139-L167)).
- The current syntax comes from [p3927](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/proposals/p3927.md#L27-L41):
  - API file: `package Geometry library "Shapes";`
  - Implementation file: `impl package Geometry library "Shapes";`
- The toolchain turns each file into one `SemIR::File` and one LLVM module ([compile_driver.cpp#L49-L69](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/driver/compile_driver.cpp#L49-L69)).
- p0875 prefers file-at-a-time compilation to library-at-a-time, for build performance ([p0875#L262-L270](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/proposals/p0875.md#L262-L270)).

**Imports.**
- Imports go in a header right after the package declaration ([README#L282-L288](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/docs/design/code_and_name_organization/README.md#L282-L288)). The dependency graph is therefore known after parsing alone.
- A cross-package import introduces only the package name ([README#L204-L214](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/docs/design/code_and_name_organization/README.md#L204-L214)).
- There is no search path. `CheckParseTrees` maps (package, library) to an API unit among the files given to the process, and reports "imported API not found" for anything missing ([check.cpp#L214-L329](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/check/check.cpp#L214-L329)).
- Planned in [p6333](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/proposals/p6333.md#L139-L151) but not implemented: map library `a/b` to the file `<root>/a/b.carbon`.

**Visibility.**
- Declarations in an API file are public by default; `private` makes them library-private ([README#L456-L461](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/docs/design/code_and_name_organization/README.md#L456-L461)).
- Implementation files cannot import each other, so names that exist only in an implementation file are effectively file-private ([README#L436-L444](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/docs/design/code_and_name_organization/README.md#L436-L444)).
- `extern library "owner"` declares an entity without importing the library that owns it ([declaring_entities.md#L231-L288](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/docs/design/declaring_entities.md#L231-L288)).

**Order: information accumulation, single pass.**
- Name lookup finds only earlier names, so forward declarations are required ([information_accumulation.md#L46-L56](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/docs/project/principles/information_accumulation.md#L46-L56)).
- p0875 rejected a globally consistent view: a build that does not scale would be "an existential threat" ([p0875#L242-L250](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/proposals/p0875.md#L242-L250)).
- The checker treats the postorder parse-node array like bytecode. `ProcessNodeIds` is a `switch` on node kind ([check/README.md#L57-L79](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/docs/check/README.md#L57-L79), [check_unit.cpp#L380-L427](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/check/check_unit.cpp#L380-L427)).
- The only reordering: inline member function bodies are checked after their outermost class ([deferred_definition_worklist.h#L18-L20](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/check/deferred_definition_worklist.h#L18-L20)).

**Cycles.**
- Forbidden: "no cyclic library dependencies" ([generics/details.md#L4854-L4856](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/docs/design/generics/details.md#L4854-L4856)).
- Files left over after the topological sort get an error and are checked without the import that broke the cycle ([check.cpp#L559-L601](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/check/check.cpp#L559-L601)).

**Parallelism and caching.**
- **Design intent:** compiling a file needs the transitive closure of API files, so parallelism is bounded by the *volume* of that closure. The design therefore favors very small libraries ([README#L495-L507](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/docs/design/code_and_name_organization/README.md#L495-L507)).
- **What the code does:** files are checked in a Kahn-style topological order. This is a serial loop ([check.cpp#L455-L503](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/check/check.cpp#L455-L503)).
- Lex and parse are also plain loops ([compile_driver.cpp#L580-L686](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/driver/compile_driver.cpp#L580-L686)).
- There is no serialized SemIR. An implementation file imports the in-memory `SemIR::File` of its API file ([check_unit.cpp#L134-L193](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/check/check_unit.cpp#L134-L193)).
- Every invocation re-checks the prelude and Core; serializing them is future work ([p6333#L335-L359](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/proposals/p6333.md#L335-L359)).
- The current Bazel rules re-check the whole library for every file, and their comment calls this "a hack" ([defs.bzl#L171-L206](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/bazel/carbon_rules/defs.bzl#L171-L206)).
- **Targets** ([Chandler Carruth, C++Now 2023](https://chandlerc.blog/slides/2023-cppnow-compiler/index.html)):
  - lex and parse: 10M lines/s;
  - semantic analysis: 1M lines/s, which is 1 µs per line;
  - lowering: 0.1M lines/s, marked "Maybe…".
  - Whether these figures are per core is **UNVERIFIED**.

**Generics and comptime.**
- Generics are checked: the body is checked once, at the definition ([generics/goals.md#L274-L286](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/docs/design/generics/goals.md#L274-L286)).
- A specific (instance) needs the generic's definition to be visible. Otherwise the error is "use of undefined generic function" ([generic.cpp#L766-L788](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/check/generic.cpp#L766-L788)). So generic bodies used across libraries must live in API files. That is inferred from the code, not tested, so **UNVERIFIED**.
- Specifics are emitted with `linkonce_odr` in every file that uses them, and deduplicating them across files is a TODO ([file_context.cpp#L644-L672](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/lower/file_context.cpp#L644-L672)).
- `eval fn` and `musteval fn` run by interpreting SemIR during checking ([functions.md#L644-L645](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/docs/design/functions.md#L644-L645), [eval.cpp#L3847-L3894](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/check/eval.cpp#L3847-L3894)).

**Build.**
- p6333 settles on `carbon compile`, `link` and `build` subcommands, and on a path derived from the package and library names. It rules out a full build system or package manager ([p6333#L95-L198](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/proposals/p6333.md#L95-L198)).
- Package management is open ([README#L995-L1000](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/docs/design/code_and_name_organization/README.md#L995-L1000)).

**Hot reload.** Nothing in the design or the code.

## C3

All C3 code paths below are under [src/compiler/ at b2e1e99](https://github.com/c3lang/c3c/tree/b2e1e99baaf2add3a40bd084823416a4437f3ec6/src/compiler).

**Unit.**
- A file starts with `module x;`. One module may span many files ([docs: modules](https://c3-lang.org/language-fundamentals/modules/)).
- The compiler parses every input file, plus the entire stdlib, serially before analysis ([compiler.c#L385-L432](https://github.com/c3lang/c3c/blob/b2e1e99baaf2add3a40bd084823416a4437f3ec6/src/compiler/compiler.c#L385-L432)).
- The author describes modules as being for namespacing only ([Lernö 2023](https://c3.handmade.network/blog/p/8650-a_look_at_modules_in_general__in_the_context_of_c3)).

**Imports.**
- `import a::b;` also imports sub-modules recursively, unless written with `@norecurse` ([docs: modules](https://c3-lang.org/language-fundamentals/modules/)).
- Imports are not resolved by path. Lookup searches only modules that were already parsed ([sema_passes.c#L115-L142](https://github.com/c3lang/c3c/blob/b2e1e99baaf2add3a40bd084823416a4437f3ec6/src/compiler/sema_passes.c#L115-L142)).
- The compiler reads every file listed in `project.json`. Imports only control visibility ([Lernö](https://c3.handmade.network/blog/p/8982-why_doesn%27t_c3_do_import_aliasing)).

**Visibility.**
- Public by default. `@private` makes a symbol module-private and `@local` makes it file-private. `import x @public` exposes x's private symbols ([docs](https://c3-lang.org/language-fundamentals/modules/), [enums.h#L2009-L2011](https://github.com/c3lang/c3c/blob/b2e1e99baaf2add3a40bd084823416a4437f3ec6/src/compiler/enums.h#L2009-L2011)).

**Order: lockstep stages plus on-demand resolution.**
- There are 16 stages, from `ANALYSIS_MODULE_HIERARCHY` to `ANALYSIS_FINALIZE` ([enums.h#L515-L533](https://github.com/c3lang/c3c/blob/b2e1e99baaf2add3a40bd084823416a4437f3ec6/src/compiler/enums.h#L515-L533)). The driver advances every module one stage at a time ([semantic_analyser.c#L528-L582](https://github.com/c3lang/c3c/blob/b2e1e99baaf2add3a40bd084823416a4437f3ec6/src/compiler/semantic_analyser.c#L528-L582)).
- All top-level names are registered before any declaration is analysed ([sema_passes.c#L500-L512](https://github.com/c3lang/c3c/blob/b2e1e99baaf2add3a40bd084823416a4437f3ec6/src/compiler/sema_passes.c#L500-L512)).
- `sema_analyse_decl` memoizes with `RESOLVE_NOT_DONE/RUNNING/DONE`, and reaching a `RUNNING` declaration is a "Recursive definition" error ([sema_decls.c#L5760-L5785](https://github.com/c3lang/c3c/blob/b2e1e99baaf2add3a40bd084823416a4437f3ec6/src/compiler/sema_decls.c#L5760-L5785)).
- A reference to a type alias triggers its analysis ([sema_types.c#L213-L247](https://github.com/c3lang/c3c/blob/b2e1e99baaf2add3a40bd084823416a4437f3ec6/src/compiler/sema_types.c#L213-L247)).
- Function bodies are checked in `ANALYSIS_FUNCTIONS` ([sema_passes.c#L1043-L1064](https://github.com/c3lang/c3c/blob/b2e1e99baaf2add3a40bd084823416a4437f3ec6/src/compiler/sema_passes.c#L1043-L1064)).
- The docs warn that modules evaluate top-level conditional compilation in an order that "is not deterministic" ([docs: compiletime](https://c3-lang.org/generic-programming/compiletime/)).

**Cycles.**
- The import pass rejects only duplicate imports and self-imports ([sema_passes.c#L104-L135](https://github.com/c3lang/c3c/blob/b2e1e99baaf2add3a40bd084823416a4437f3ec6/src/compiler/sema_passes.c#L104-L135)).
- So module cycles work in practice. No document says they are allowed (**UNVERIFIED** as a stated rule).

**Parallelism and caching.**
- The front end is single-threaded over one global context ([compiler_internal.h#L2150-L2187](https://github.com/c3lang/c3c/blob/b2e1e99baaf2add3a40bd084823416a4437f3ec6/src/compiler/compiler_internal.h#L2150-L2187)).
- IR generation runs on the main thread, one LLVM module per C3 module. Only optimize and emit run as pool tasks ([llvm_codegen.c#L1138-L1175](https://github.com/c3lang/c3c/blob/b2e1e99baaf2add3a40bd084823416a4437f3ec6/src/compiler/llvm_codegen.c#L1138-L1175)).
- `--single-module` merges everything into one LLVM module, and is on by default at O3 and above ([builder.c#L196-L284](https://github.com/c3lang/c3c/blob/b2e1e99baaf2add3a40bd084823416a4437f3ec6/src/build/builder.c#L196-L284)).
- No incremental or object cache was found (**UNVERIFIED** beyond `compiler.c`, `builder.c` and `build_options.c`).
- The author says lexing, parsing and semantic analysis are "a fraction of the total compilation time" ([Lernö 2023](https://c3.handmade.network/blog/p/8650-a_look_at_modules_in_general__in_the_context_of_c3)).

**Generics and comptime.**
- Generic modules or declarations are written `module foo <Type>;` and used as `List{int}` ([docs: generics](https://c3-lang.org/generic-programming/generics/)).
- Instances are found by a linear scan over interned names. A miss copies the template and catches it up to the current stage ([sema_generics.c#L145-L300](https://github.com/c3lang/c3c/blob/b2e1e99baaf2add3a40bd084823416a4437f3ec6/src/compiler/sema_generics.c#L145-L300)).
- Each instance's unit is the generic's defining unit (`sema_generics.c#L148`), so instance code lands in the defining module's object file. That is an inference from the code (**UNVERIFIED**).
- Compile-time features: `$if`, `$for`, macros and `$exec` (runs an external program). There is no general compile-time execution of ordinary functions ([docs: compiletime](https://c3-lang.org/generic-programming/compiletime/)).

**Build.**
- `project.json` holds `sources`, `dependencies` (`.c3l` libraries), `dependency-search-paths`, `targets` and more ([project.c#L17-L87](https://github.com/c3lang/c3c/blob/b2e1e99baaf2add3a40bd084823416a4437f3ec6/src/build/project.c#L17-L87), [docs: project config](https://c3-lang.org/build-your-project/project-config/)).
- A library is a directory or an archive with a `manifest.json` ([docs: library packaging](https://c3-lang.org/misc-advanced/library-packaging/)).

**Hot reload.**
- Issue [#1968](https://github.com/c3lang/c3c/issues/1968), "Make hot reloading work", has been open since 2025. It notes that dynamic lookup tables would point to old code.

## Zen C

**Unit.**
- The root file and the files given on the command line form one AST. That AST becomes one `.c` file and one C compiler run ([driver.c#L165-L405](https://github.com/Zuhaitz-dev/zenc-lang/blob/b3f550e938be3fd395e9ebf657669f89dbb3453b/src/driver/driver.c#L165-L405)).

**Imports.**
- Forms: `import "x.zc"`, `import "x.zc" as m`, `import { A as B } from "x.zc"`, `export import`, and C headers ([stmt_import.c#L407-L889](https://github.com/Zuhaitz-dev/zenc-lang/blob/b3f550e938be3fd395e9ebf657669f89dbb3453b/src/parser/stmt/stmt_import.c#L407-L889)).
- The imported file is parsed in place, once.
- The search order is: relative to the importing file, then the current directory, then `-I` paths, then std ([utils.c#L239-L340](https://github.com/Zuhaitz-dev/zenc-lang/blob/b3f550e938be3fd395e9ebf657669f89dbb3453b/src/utils/utils.c#L239-L340)).

**Visibility.**
- There are no visibility keywords; PR [#136](https://github.com/Zuhaitz-dev/zenc-lang/pull/136), which added them, was closed.
- `opaque` types hide fields outside their defining file ([docs 03](https://github.com/zenc-lang/docs/blob/60115e029d02e9cea4c9f721da5c82cd7b257de5/reference/03-aggregate-types.md)).

**Order.**
- Non-generic function bodies are skipped while parsing; the lexer position is saved and `skip_braced_block` is called. The bodies are parsed later, once every declaration is known ([decl_function.c#L210-L243](https://github.com/Zuhaitz-dev/zenc-lang/blob/b3f550e938be3fd395e9ebf657669f89dbb3453b/src/parser/decl/decl_function.c#L210-L243), [core_program.c#L608-L621](https://github.com/Zuhaitz-dev/zenc-lang/blob/b3f550e938be3fd395e9ebf657669f89dbb3453b/src/parser/core/core_program.c#L608-L621)).
- When emitting C, structs are topologically sorted by comparing field type names as strings. A sweep is O(n²) and repeats up to n² times ([codegen_main.c#L14-L60](https://github.com/Zuhaitz-dev/zenc-lang/blob/b3f550e938be3fd395e9ebf657669f89dbb3453b/src/codegen/codegen_main.c#L14-L60), [L231-L343](https://github.com/Zuhaitz-dev/zenc-lang/blob/b3f550e938be3fd395e9ebf657669f89dbb3453b/src/codegen/codegen_main.c#L231-L343)).
- Prototypes for every function are emitted first.

**Cycles.** "Circular import detected" ([stmt_import.c#L667-L669](https://github.com/Zuhaitz-dev/zenc-lang/blob/b3f550e938be3fd395e9ebf657669f89dbb3453b/src/parser/stmt/stmt_import.c#L667-L669)).

**Parallelism and caching.** None found: one thread, one C translation unit, no cache.

**Generics and comptime.**
- Generics are monomorphized during parsing and deduplicated with linear `strcmp` lists ([utils_template_inst.c#L233-L432](https://github.com/Zuhaitz-dev/zenc-lang/blob/b3f550e938be3fd395e9ebf657669f89dbb3453b/src/parser/utils/utils_template_inst.c#L233-L432), [L774-L830](https://github.com/Zuhaitz-dev/zenc-lang/blob/b3f550e938be3fd395e9ebf657669f89dbb3453b/src/parser/utils/utils_template_inst.c#L774-L830)).
- `comptime { yield("...") }` runs a tree-walking interpreter with a limit of 1M steps. Its text output is parsed again ([comptime_interpreter.c#L8-L39](https://github.com/Zuhaitz-dev/zenc-lang/blob/b3f550e938be3fd395e9ebf657669f89dbb3453b/src/analysis/comptime_interpreter.c#L8-L39), [typecheck.c#L996-L1045](https://github.com/Zuhaitz-dev/zenc-lang/blob/b3f550e938be3fd395e9ebf657669f89dbb3453b/src/analysis/typecheck.c#L996-L1045)).

**Build.** There is no manifest: `zc build file.zc`, plus `//>` directives in source for link flags and similar ([docs 12](https://github.com/zenc-lang/docs/blob/60115e029d02e9cea4c9f721da5c82cd7b257de5/reference/12-advanced.md)).

**Hot reload.** Nothing for user programs.

**Takeaway.** Deferring bodies is the one idea worth taking: it makes declarations order-independent cheaply, and each deferred body is an independent job. Avoid the rest:
- one C translation unit;
- types stored as strings and compared with `strcmp`;
- the O(n³) struct sort.

## Odin

`ODOC` below is [odin-lang.org/content/docs at 1d272b93](https://github.com/odin-lang/odin-lang.org/tree/1d272b93ccbd9a7e23f21cdbc2a6b5d5b8e5daa4/content/docs).

**Unit.**
- "A package is a directory of Odin code files" ([overview](https://odin-lang.org/docs/overview/#packages)).
- The build is whole-program. `parse_packages` starts from the main package and `base:runtime` and adds packages as imports appear ([parser.cpp#L7791-L7861](https://github.com/odin-lang/Odin/blob/85afcb386c389aafcec4f2e33d27ff8b42579822/src/parser.cpp#L7791-L7861)).

**Imports.**
- `import "core:fmt"` names a collection; a path without a collection is relative to the file ([overview](https://odin-lang.org/docs/overview/#packages), [parser.cpp#L6797-L6930](https://github.com/odin-lang/Odin/blob/85afcb386c389aafcec4f2e33d27ff8b42579822/src/parser.cpp#L6797-L6930)).
- `-collection:name=path` adds a collection.

**Visibility.**
- Public by default. `@(private)` makes a declaration package-private and `@(private="file")` file-private. The `#+private` file tag applies to the whole file ([overview](https://odin-lang.org/docs/overview/#private), [ODOC/attributes.md#L486-L497](https://github.com/odin-lang/odin-lang.org/blob/1d272b93ccbd9a7e23f21cdbc2a6b5d5b8e5daa4/content/docs/attributes.md#L486-L497)).

**Order: parallel and demand-driven since PR #7701 (merged 2026-10-02).**
- Entity collection for a package starts as soon as its last file is parsed ([parser.cpp#L6494-L6497](https://github.com/odin-lang/Odin/blob/85afcb386c389aafcec4f2e33d27ff8b42579822/src/parser.cpp#L6494-L6497)).
- Global entities form a syntactic dependency graph. Iterative Tarjan splits it into strongly connected components (SCCs), which run on the thread pool once their dependencies finish ([checker_global.cpp#L1512-L1703](https://github.com/odin-lang/Odin/blob/85afcb386c389aafcec4f2e33d27ff8b42579822/src/checker_global.cpp#L1512-L1703)).
- `check_entity_decl` claims an entity with a compare-exchange on `checking_thread`, and other threads wait on a futex. If a thread ends up waiting on itself, it reports "Illegal declaration cycle" ([check_decl.cpp#L2217-L2245](https://github.com/odin-lang/Odin/blob/85afcb386c389aafcec4f2e33d27ff8b42579822/src/check_decl.cpp#L2217-L2245)).
- Procedure bodies are checked in parallel after the globals ([checker.cpp#L7358-L7403](https://github.com/odin-lang/Odin/blob/85afcb386c389aafcec4f2e33d27ff8b42579822/src/checker.cpp#L7358-L7403)).
- Results are re-sorted into source order, so output is deterministic ([checker.cpp#L8239-L8256](https://github.com/odin-lang/Odin/blob/85afcb386c389aafcec4f2e33d27ff8b42579822/src/checker.cpp#L8239-L8256)).

**Cycles.** "Cyclic importation" is an error ([checker.cpp#L6656-L6679](https://github.com/odin-lang/Odin/blob/85afcb386c389aafcec4f2e33d27ff8b42579822/src/checker.cpp#L6656-L6679)).

**Parallelism and caching.**
- Files are parsed one task per file ([parser.cpp#L6499-L6516](https://github.com/odin-lang/Odin/blob/85afcb386c389aafcec4f2e33d27ff8b42579822/src/parser.cpp#L6499-L6516)).
- `-use-separate-modules` gives one LLVM module per package, and it is required for threaded object generation ([llvm_backend_general.cpp#L418-L468](https://github.com/odin-lang/Odin/blob/85afcb386c389aafcec4f2e33d27ff8b42579822/src/llvm_backend_general.cpp#L418-L468), [llvm_backend.cpp#L3578-L3584](https://github.com/odin-lang/Odin/blob/85afcb386c389aafcec4f2e33d27ff8b42579822/src/llvm_backend.cpp#L3578-L3584)).
- There is no per-package incremental build. Issue [#497](https://github.com/odin-lang/Odin/issues/497) was closed without one.
- `-internal-cached` is an experimental all-or-nothing cache for the whole build ([cached.cpp#L261-L347](https://github.com/odin-lang/Odin/blob/85afcb386c389aafcec4f2e33d27ff8b42579822/src/cached.cpp#L261-L347)).

**Generics and comptime.**
- Parametric polymorphism (`$T`). Instances live in one table per generic procedure, behind a mutex, so each exists once per program ([check_expr.cpp#L829-L909](https://github.com/odin-lang/Odin/blob/85afcb386c389aafcec4f2e33d27ff8b42579822/src/check_expr.cpp#L829-L909)).
- There is no general compile-time execution. Ginger Bill called it "a huge complexity" ([#64](https://github.com/odin-lang/Odin/issues/64#issuecomment-430182251)).
- Compile-time features: `when`, `#config`, `#load` (embeds bytes).

**Build.** There is no build file: `odin build <dir>` plus flags ([ODOC/overview.md#L36-L43](https://github.com/odin-lang/odin-lang.org/blob/1d272b93ccbd9a7e23f21cdbc2a6b5d5b8e5daa4/content/docs/overview.md#L36-L43)).

**Dependencies** (added 2026-11-26).
- There is no package manager, by design. Ginger Bill separates packages, repositories, build systems and package managers, and objects only to the last: a package manager downloads a package's dependencies, "and its dependencies, and its dependencies", which he calls "the automation of dependency hell". His answer is "copying and vendoring each package manually, and fixing the specific versions down", and he credits a strong standard library, as in Go, with keeping dependency counts low ([Package Managers are Evil](https://www.gingerbill.org/article/2025/09/08/package-managers-are-evil/), 2025-09-08).
- `core:` is the standard library (`fmt`, `os`, `net`, `crypto`, `image`, `encoding`, `thread` and others). `vendor:` holds bindings that "are curated and maintained by the Odin team", with the binaries they need: SDL2 and SDL3, raylib, GLFW, OpenGL, Vulkan, Direct3D 11 and 12, Apple's frameworks, wgpu, box2d, miniaudio, stb, cgltf, ENet, Lua, microui, nanovg, curl, zlib and more ([vendor/README.md](https://github.com/odin-lang/Odin/blob/85afcb386c389aafcec4f2e33d27ff8b42579822/vendor/README.md)).
- On Windows the libraries ship prebuilt (`vendor/sdl3` holds `SDL3.dll` and `SDL3.lib`); elsewhere the binding links the system's library, `system:SDL3` ([sdl3__foreign.odin](https://github.com/odin-lang/Odin/blob/85afcb386c389aafcec4f2e33d27ff8b42579822/vendor/sdl3/sdl3__foreign.odin)).

**Hot reload.**
- `-build-mode:dll` plus `core:dynlib`. Loading the same library again "unloads the previous incarnation, allowing for straightforward hot reload support" ([lib.odin#L110-L124](https://github.com/odin-lang/Odin/blob/85afcb386c389aafcec4f2e33d27ff8b42579822/core/dynlib/lib.odin#L110-L124)).
- The replaced unit is the whole DLL. Keeping game state in the host is a convention; see the secondary pointer [Karl Zylinski](https://zylinski.se/posts/hot-reload-gameplay-code/).

**Numbers ([PR #7701](https://github.com/odin-lang/Odin/pull/7701), Windows, 32 threads).**
- `odin check examples/all`: 820 → 337 ms.
- A full demo build: 395 → 350 ms, because "LLVM code generation and linking are unchanged".

## Go

**Unit.**
- A package is a directory of files that share one package scope ([spec](https://go.dev/ref/spec#Packages)).
- `go build` runs one `compile` process per package. That process parses its files concurrently ([noder.go#L27-L77](https://github.com/golang/go/blob/6e61706b976dff01299814ed319bbea95722009e/src/cmd/compile/internal/noder/noder.go#L27-L77)).

**Imports.**
- An import path is the module path joined with the subdirectory ([ref/mod](https://go.dev/ref/mod#modules-packages-and-versions)).
- The compiler gets an `importcfg` that lists only *direct* dependencies, each mapped to its export file ([exec.go#L798-L811](https://github.com/golang/go/blob/6e61706b976dff01299814ed319bbea95722009e/src/cmd/go/internal/work/exec.go#L798-L811)).
- Export data is self-contained: it re-exports the transitive types a package uses ([unified.go#L162-L202](https://github.com/golang/go/blob/6e61706b976dff01299814ed319bbea95722009e/src/cmd/compile/internal/noder/unified.go#L162-L202)).

**Visibility.**
- Upper-case package-level names are exported ([spec](https://go.dev/ref/spec#Exported_identifiers)).
- Packages under an `internal/` directory can be imported only from within the tree above that directory ([pkg.go#L1470-L1478](https://github.com/golang/go/blob/6e61706b976dff01299814ed319bbea95722009e/src/cmd/go/internal/load/pkg.go#L1470-L1478)).

**Order: collect, then lazy `objDecl`.**
- `checkFiles` runs these phases in order: `collectObjects`, `packageObjects`, `processDelayed`, `initOrder` ([check.go#L422-L484](https://github.com/golang/go/blob/6e61706b976dff01299814ed319bbea95722009e/src/cmd/compile/internal/types2/check.go#L422-L484)).
- `collectObjects` puts every package-level object into the package scope first ([resolver.go#L199-L515](https://github.com/golang/go/blob/6e61706b976dff01299814ed319bbea95722009e/src/cmd/compile/internal/types2/resolver.go#L199-L515)).
- `objDecl` gives each object a color: white (not started), grey (on the `objPathIdx` stack) or black (done). Finding a grey object means a possible cycle ([decl.go#L72-L123](https://github.com/golang/go/blob/6e61706b976dff01299814ed319bbea95722009e/src/cmd/compile/internal/types2/decl.go#L72-L123)).
- A use of an untyped object calls `objDecl` on demand, but never for objects from another package, because that would race ([typexpr.go#L56-L72](https://github.com/golang/go/blob/6e61706b976dff01299814ed319bbea95722009e/src/cmd/compile/internal/types2/typexpr.go#L56-L72)).
- Function bodies wait until all package-level declarations are done ([decl.go#L703-L709](https://github.com/golang/go/blob/6e61706b976dff01299814ed319bbea95722009e/src/cmd/compile/internal/types2/decl.go#L703-L709)).
- Variable initialization order comes from dependency analysis ([spec](https://go.dev/ref/spec#Package_initialization), [initorder.go#L16-L60](https://github.com/golang/go/blob/6e61706b976dff01299814ed319bbea95722009e/src/cmd/compile/internal/types2/initorder.go#L16-L60)).

**Cycles.**
- Forbidden: "import cycle not allowed" ([pkg.go#L1440-L1455](https://github.com/golang/go/blob/6e61706b976dff01299814ed319bbea95722009e/src/cmd/go/internal/load/pkg.go#L1440-L1455)).
- Rationale: cycles force the compiler to handle larger sets of files at once and slow incremental builds ([Go at Google, 2012](https://go.dev/talks/2012/splash.article)).

**Parallelism and caching.**
- **Across packages.** The action graph runs `-p` workers, GOMAXPROCS by default ([exec.go#L205-L250](https://github.com/golang/go/blob/6e61706b976dff01299814ed319bbea95722009e/src/cmd/go/internal/work/exec.go#L205-L250)).
- **Pipelining.** `BuildExportAction` splits a compile into an export step and an object step, and dependents wait only for the export step ([action.go#L588-L616](https://github.com/golang/go/blob/6e61706b976dff01299814ed319bbea95722009e/src/cmd/go/internal/work/action.go#L588-L616)). The compiler signals that export data is ready through `-exportfd`, before backend codegen ([obj.go#L45-L67](https://github.com/golang/go/blob/6e61706b976dff01299814ed319bbea95722009e/src/cmd/compile/internal/gc/obj.go#L45-L67)).
- **Within a package.** The backend compiles functions on `-c` workers, longest first ([compile.go#L131-L199](https://github.com/golang/go/blob/6e61706b976dff01299814ed319bbea95722009e/src/cmd/compile/internal/gc/compile.go#L131-L199)). Type checking runs as a sequential pipeline of phases.
- **Cache.** A package's action ID hashes its sources plus the content IDs of its dependencies' export data. A dependent therefore rebuilds only when that export data changes ([buildid.go#L28-L62](https://github.com/golang/go/blob/6e61706b976dff01299814ed319bbea95722009e/src/cmd/go/internal/work/buildid.go#L28-L62), [cache.go#L28-L34](https://github.com/golang/go/blob/6e61706b976dff01299814ed319bbea95722009e/src/cmd/go/internal/cache/cache.go#L28-L34)).

**Generics and comptime.**
- Generic bodies always go into export data ([writer.go#L1153-L1232](https://github.com/golang/go/blob/6e61706b976dff01299814ed319bbea95722009e/src/cmd/compile/internal/noder/writer.go#L1153-L1232)).
- An importing package instantiates them itself and marks the instances `Dupok` ([reader.go#L829-L830](https://github.com/golang/go/blob/6e61706b976dff01299814ed319bbea95722009e/src/cmd/compile/internal/noder/reader.go#L829-L830)). The linker removes duplicates.
- Instances are stenciled by GC shape and take a dictionary argument ([design doc](https://github.com/golang/proposal/blob/master/design/generics-implementation-dictionaries-go1.18.md)).
- Go 1.18 compiles were "roughly 15% slower" ([go1.18](https://go.dev/doc/go1.18)).
- There is no compile-time execution beyond constants. `go generate` never runs automatically ([generate.go#L43-L44](https://github.com/golang/go/blob/6e61706b976dff01299814ed319bbea95722009e/src/cmd/go/internal/generate/generate.go#L43-L44)).

**Build.** `go.mod`, `go.sum` and optionally `go.work`. There is no build script ([ref/mod](https://go.dev/ref/mod#go-mod-file)).

**Hot reload.** None. A plugin "cannot be closed" ([plugin.go#L12-L64](https://github.com/golang/go/blob/6e61706b976dff01299814ed319bbea95722009e/src/plugin/plugin.go#L12-L64)).

## Jai

There is no public compiler source, and almost everything below is **UNVERIFIED** (from community primers):
- **Modules:** `#import "Module"` loads a module from search paths; `#load "file.jai"` pulls a file into the current scope.
- **Visibility:** the `#scope_*` directives.
- **Order:** declarations are order-independent, resolved by a multithreaded job system.
- **Build:** the whole program is compiled from source on every build.
- **Metaprogram:** `build.jai` uses the workspace and message API (`compiler_wait_for_message`, `add_build_string`).
- **Dependencies** (added 2026-11-26): there is no package manager. The compiler ships a `modules` folder with the modules "that most people would want", such as `Basic`, `File`, `Math`, `Simp` (2D rendering), `Window_Creation`, `Input`, `GetRect` (UI) and `Thread` ([Jai Community wiki, Modules and Libraries](https://github.com/Jai-Community/Jai-Community-Library/wiki/Modules-and-Libraries)). A community module is copied or symlinked into that folder ([onelivesleft/Metaprogram](https://github.com/onelivesleft/Metaprogram)).

Primary sources:
- Blow's demos show `#run` compile-time execution ([2014](https://www.youtube.com/watch?v=UTqZNujQOlA)) and polymorphic procedures ([2015](https://www.youtube.com/watch?v=BwqeFrlSpuI)).
- A 2018 post on Thekla's blog says the build program can pause at any compilation stage. It also says a whole build "can amount to compiling a single file" ([the-witness.net](http://the-witness.net/news/2018/03/testing-the-jai-compiler/)).

Not verified:
- Speed claims: 80k lines in about 1.5 s; 250k lines/s on the x64 backend; a goal of 1M lines/s (**UNVERIFIED**).
- No statement on hot reload was found.

**Relevance.** Jai's compile-time code can inject declarations through `add_build_string`. Then any unresolved name may depend on a metaprogram, and that blocks per-package caching unless injection is fenced to one package.

## What drives compile speed

**The budget.**
- 1M lines/s on 14 cores gives 14 core-µs per line for everything: lex, parse, check, codegen and link.
- Carbon's targets add up to about 11 µs per line (0.1 + 1 + 10), and lowering accounts for 10 of them. So the backend owns the budget, not the front end.
- Odin's PR #7701 shows the same: after the checker went parallel, LLVM and linking dominated.

**Redundant work.**

| Source | Who pays it | Avoided by |
|---|---|---|
| Re-checking imported interfaces from source | Carbon (re-checks Core and the prelude every run; Bazel re-checks the whole library per file); C3 (re-parses the stdlib every build) | Go-style export data; Zig/Odin whole program in one process |
| Re-instantiating generics per importer | Go (linker dedup), Carbon (`linkonce_odr`) | One global instance table (Zig, Odin) |
| Re-analysing everything after any edit | Zig `whole` cache mode, Odin, C3, Zen C | Go per-package cache; Zig resident incremental |
| Analysing unreachable code | Everyone except Zig | Zig's lazy roots, at the cost of missing errors in unused code |

**Parallelism.**

| Approach | Example | Limit |
|---|---|---|
| Package DAG, one process per package | Go | Depth of the dependency chain. Export-first pipelining shortens it |
| Files in topological order | Carbon (serial today) | Volume of the API closure |
| One checker, demand-driven, claim and wait | Odin (since 2026-10) | Contention on hot entities; error order needs re-sorting |
| One checker, demand-driven, single thread | Zig, C3, Zen C | One core |
| Parallel only after the front end (per-module LLVM) | C3, Odin, Zig codegen | The front end stays serial |

**Laziness.**
- Zig checks only what the roots reach. This cuts work for big libraries, but serializes the recursion.
- Go, Odin and C3 check everything and resolve order lazily.
- Laziness and parallelism conflict unless resolution can wait on another thread, as in Odin.

## Hot reload: which unit is replaced

| Language | Unit | Indirection | Safe points and state |
|---|---|---|---|
| Zig | Function or global | GOT, patched over ptrace | None. State stays in place; no layout migration |
| Odin | Whole DLL | Host calls through function pointers it loaded | The host keeps state, by convention |
| C3 | None yet | #1968 notes vtables pointing to old code | None |
| Go | None | none | none |
| Carbon, Zen C, Jai | None found | none | none |

For chibi-v2's Edit and Continue, the lessons are:
- Patch per function, as Zig does. A package or DLL is too coarse when a body changes.
- Every call that can cross a patch needs indirection: function tables, a GOT, or handles. Type metadata and vtables must also be in patchable tables.
- Generic instances need one owner each. Go and Carbon emit copies in every user, so one patch would have to find N copies.
- Nobody has defined safe points, coroutine-frame migration or layout migration. chibi-v2 must design these itself.
- Zig's units (`nav_val`, `func`, `type_layout`) plus a per-declaration source hash are a good model for deciding what changed. A changed `type_layout` is the signal that state migration is needed.

## What each design keeps in memory

| Design | Resident data | Growth bound |
|---|---|---|
| Separate compilation (Go) | One package + export data of its direct deps | Package size + its interface fan-in |
| Whole program, one pass (Odin, C3, Zen C) | All ASTs and typed entities until codegen | Whole program |
| Whole program, resident incremental (Zig `--watch`) | InternPool, ZIR and previous ZIR per file, dependency and reference graphs, for the life of the process | Whole program × (1 + dependency edges) |
| Per file, re-check imports (Carbon) | Tokens, parse tree and SemIR of every input; `ImportRef` copies per importer | Inputs × imported API volume |

Edit and Continue needs a resident process anyway. So the question for chibi-v2 is how compact the resident form is (flat arrays, indices, interned IDs), not whether to keep it.

## Recommendation for chibi-v2

| Option | Used by | Parallelism | Redundant work | Laziness | Ergonomics | Verdict |
|---|---|---|---|---|---|---|
| **Unit: whole program in one process; packages are scheduling and cache units with a hashed interface** | Odin (process) + Go (interface hash) | Parse and check in parallel across and within packages | None within a run. Unchanged packages are reused across runs through the interface hash | Optional | Users see packages; the compiler sees one graph | **Recommended** |
| Unit: one process per package, export data | Go | DAG depth limits it. Pipelining helps | Generics instantiated per importer; comptime bodies serialized | Per package | Simple mental model | Good for caching, weak for comptime and EnC |
| Unit: per file, API/impl files | Carbon | High in theory, serial in practice | Re-checks API closures | No | Two files per library, forward declarations | Not recommended |
| Unit: whole program, lazy from roots, serial Sema | Zig | AstGen and codegen only | Low | Yes | Errors in unused code are silent | Do not copy the serial Sema |
| **Package = directory** | Go, Odin | Discovery is trivial and parallel | — | — | One rule, no manifest needed per package | **Recommended** |
| Package declared in each file | C3 | Needs all files parsed before lookup | Parses everything listed | — | Flexible; harder to find code | No |
| File = module | Zig, Zen C | Fine-grained | — | — | Many tiny modules; visibility per file | No |
| **Order-free inside a package: register all names, then demand-driven claim and wait** | Odin (parallel), Go/C3 (serial 3-state) | Full, with atomics per entity | — | Resolution is lazy | Users never write forward declarations | **Recommended** |
| Declare before use (information accumulation) | Carbon | Single pass per file | — | — | Forward declarations everywhere | No (conflicts with order-free comptime) |
| Deferred function bodies (save lexer position) | Zen C | Each body is a job | — | — | Invisible | **Use as the body stage** |
| **No cycles between packages; cycles allowed within a package** | Go, Odin, Carbon (no cycles) | A DAG enables scheduling and caching | — | — | Shared types move to a lower package | **Recommended** |
| Cycles allowed between modules | Zig, C3 | No DAG to cache by | — | — | Most freedom | No |
| **Generic instances in one global table, keyed by (generic ID, type args)** | Zig, Odin | Table needs a sharded lock | Each instance compiled once | Instances made on demand | — | **Recommended** |
| Instances per importer, linker dedup | Go, Carbon | Fully parallel | N copies; Go 1.18 was 15% slower | — | — | No (also bad for EnC) |
| Instances in the defining module | C3 | — | One copy, but the library's object changes | — | — | No |
| **Comptime across packages: interpret the callee's IR from the in-memory graph; record each result as a dependency** | Zig (in process), Carbon (SemIR eval) | Comptime calls are jobs | Memoize per (function, args) | Yes | — | **Recommended**; give memoized calls their own dependency nodes (Zig had to turn memoization off under incremental) |
| Comptime injecting declarations into other packages | Jai | Blocks per-package caching | — | — | Powerful | Fence: only into the running package |
| **Build described in Chibi, run at comptime** | Zig, Jai | Step DAG | — | — | One language | **Recommended**, with a declarative dependency list (hash-pinned like `build.zig.zon`) |
| Declarative manifest only | C3, Go | — | — | — | Easy to tool | Acceptable alternative |
| **Hot reload unit: function behind an indirection table; layout changes go through a migration step** | Zig (function + GOT) | Recompile only dirty units | — | — | True Edit and Continue | **Recommended** |
| Hot reload unit: whole DLL | Odin | Rebuild the whole DLL | All code in it | — | Simple; state lives in the host by hand | Stopgap only |
| **Visibility: public with `pub`, private to package by default, optional file-private** | Go (package), Odin/C3 (file level) | — | — | — | Familiar | **Recommended**; interface = `pub` items, so the interface hash covers only them |

## Questions the owner must answer

1. **Package on disk.** Is a package exactly one directory, as in Go and Odin? Can one directory hold a package plus its tests?
2. **Cycles.** Are import cycles between packages forbidden? This report recommends yes.
3. **Laziness.** Should the compiler check every declaration in user packages and report errors in unused code (Go, Odin)? Or only what the roots reach (Zig)? A mixed answer is possible: eager for the user's own packages, lazy for dependencies.
4. **Generics model.** Are generic bodies checked once at the definition (Carbon) or checked per instance (Zig, Odin, C3, Jai)? With per-instance checking, a package's interface must carry generic bodies.
5. **Comptime reach.** Can compile-time code read files, call into other packages, or generate declarations? Can it generate them into other packages? Must it be deterministic, so its results can be cached?
6. **Reflection.** Is reflection data on imported types part of the package interface? Does a change to a private field invalidate the users of a reflecting function?
7. **What "full build" means.** Does the 1M lines/s target include codegen and link? Which backend: your own, LLVM, or C? The backend is most of the budget (Carbon's targets, Odin PR #7701).
8. **Cold or warm.** Must a clean machine reach 1M lines/s, or only a resident compiler that already has the stdlib loaded? Is a resident compiler daemon acceptable as the main development loop, as with Zig's `--watch`?
9. **Stdlib.** Is the stdlib compiled from source on every build (C3, Odin, Zig) or loaded from a prepared interface or cache?
10. **Edit and Continue scope.** Which edits must patch live: body-only changes, new functions, new globals, struct layout changes, edits to coroutine bodies with live frames? What happens to state when a layout changes?
11. **Instance ownership.** For hot reload, which unit owns a generic instance: the generic's package, the first user, or a global instance table?
12. **Build and dependencies.** Is the build described by a Chibi program run at compile time, or by a manifest? Are third-party packages pinned by content hash?
13. **Visibility default.** Private by default with `pub` (Zig, Go-like) or public by default (C3, Odin)? Is a file-private level needed?

## Code and docs to read first

1. **Odin [PR #7701](https://github.com/odin-lang/Odin/pull/7701), then [check_decl.cpp#L2217-L2245](https://github.com/odin-lang/Odin/blob/85afcb386c389aafcec4f2e33d27ff8b42579822/src/check_decl.cpp#L2217-L2245) and [checker_global.cpp#L1512-L1703](https://github.com/odin-lang/Odin/blob/85afcb386c389aafcec4f2e33d27ff8b42579822/src/checker_global.cpp#L1512-L1703).** Look for: claim and wait per entity, SCC scheduling, re-sorting results into source order, and the measured speedup. This is the closest existing design to a parallel order-free checker.
2. **Go [action.go#L588-L616](https://github.com/golang/go/blob/6e61706b976dff01299814ed319bbea95722009e/src/cmd/go/internal/work/action.go#L588-L616) and [buildid.go#L28-L62](https://github.com/golang/go/blob/6e61706b976dff01299814ed319bbea95722009e/src/cmd/go/internal/work/buildid.go#L28-L62).** Look for: export-first pipelining, and rebuild keys made from the export-data hash of dependencies.
3. **Go [decl.go#L72-L123](https://github.com/golang/go/blob/6e61706b976dff01299814ed319bbea95722009e/src/cmd/compile/internal/types2/decl.go#L72-L123) and [resolver.go#L645-L709](https://github.com/golang/go/blob/6e61706b976dff01299814ed319bbea95722009e/src/cmd/compile/internal/types2/resolver.go#L645-L709).** Look for: three-color lazy resolution, and the decision to never resolve another package's objects lazily.
4. **Zig [Sema.zig#L31219-L31257](https://github.com/ziglang/zig/blob/0.15.2/src/Sema.zig#L31219-L31257), [PerThread.zig#L2742-L2782](https://github.com/ziglang/zig/blob/0.15.2/src/Zcu/PerThread.zig#L2742-L2782) and [InternPool.zig#L393-L453](https://github.com/ziglang/zig/blob/0.15.2/src/InternPool.zig#L393-L453).** Look for: analysis units, lazy roots, and dependency kinds. These units are the right size for patch decisions.
5. **Zig [PerThread.zig#L410-L507](https://github.com/ziglang/zig/blob/0.15.2/src/Zcu/PerThread.zig#L410-L507) (`updateZirRefs`), then [Zcu.zig#L3168-L3250](https://github.com/ziglang/zig/blob/0.15.2/src/Zcu.zig#L3168-L3250).** Look for: per-declaration source hashes, outdated propagation, and recursion loops.
6. **Zig [issue #68](https://github.com/ziglang/zig/issues/68), [Konka's HCS post](https://www.jakubkonka.com/2022/03/16/hcs-zig.html) and [link.zig#L714-L790](https://github.com/ziglang/zig/blob/0.15.2/src/link.zig#L714-L790).** Look for: function-level patching through the GOT, and what is missing (safe points, state).
7. **Carbon [C++Now 2023 slides](https://chandlerc.blog/slides/2023-cppnow-compiler/index.html) and [README#L495-L507](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/docs/design/code_and_name_organization/README.md#L495-L507).** Look for: per-line budgets for each phase, and why the size of the interface closure bounds parallelism.
8. **Carbon [p0875](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/proposals/p0875.md) and [check.cpp#L455-L601](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/check/check.cpp#L455-L601).** Look for: the case against order-free code, and topological scheduling from parse-only import headers.
9. **Go [generics dictionaries design](https://github.com/golang/proposal/blob/master/design/generics-implementation-dictionaries-go1.18.md) and [unified.go#L162-L202](https://github.com/golang/go/blob/6e61706b976dff01299814ed319bbea95722009e/src/cmd/compile/internal/noder/unified.go#L162-L202).** Look for: what export data must carry so that generics and inlining work across packages.
10. **C3 [semantic_analyser.c#L528-L582](https://github.com/c3lang/c3c/blob/b2e1e99baaf2add3a40bd084823416a4437f3ec6/src/compiler/semantic_analyser.c#L528-L582) and [sema_generics.c#L145-L300](https://github.com/c3lang/c3c/blob/b2e1e99baaf2add3a40bd084823416a4437f3ec6/src/compiler/sema_generics.c#L145-L300).** Look for: stages run in lockstep, and where generic instances land.
11. **Zen C [decl_function.c#L210-L243](https://github.com/Zuhaitz-dev/zenc-lang/blob/b3f550e938be3fd395e9ebf657669f89dbb3453b/src/parser/decl/decl_function.c#L210-L243).** Look for: skipping bodies during parsing to get order independence cheaply.
12. **[Go at Google (2012)](https://go.dev/talks/2012/splash.article), sections 5 and 7.** Look for: why there are no import cycles, and the dependency fan-out numbers.
13. **Zig release notes on incremental compilation ([0.14](https://ziglang.org/download/0.14.0/release-notes.html#Incremental-Compilation), [0.17](https://ziglang.org/download/0.17.0/release-notes.html#Incremental-Compilation)).** Look for: measured update times and what is still missing.

## Unverified

- Everything about Jai except the existence of `#run` and polymorphic procedures, and the Thekla post's claims. That includes all speed numbers.
- Whether Carbon's C++Now targets are per core.
- That Carbon generic bodies used from another library must live in API files (inferred from `generic.cpp`).
- That C3 generic instances land in the defining module's object file (inferred from `instance->unit`).
- That C3 has no build cache. Only the driver files were searched.
- The source of Zig's "half-million line" figure, and so the 36k lines/s estimate.
- Zig line numbers at 0.17.1. Only the names of the cited functions were confirmed there.
- Whether Zig's `hotCodeSwap` works end to end in 0.17.1.
- gingerBill's 2023 statement that LLVM and the linker take 90% of build time (from a search snippet only).
