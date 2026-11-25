# A C-derived corpus for chibi-v2, and a path to self-hosting

Research date: 2026-11-09. Sources are primary: official docs, the projects' own code at a pinned commit, and posts or talks by their authors. Every number marked "measured" is my own count of files fetched with `gh api` at the commits in section 3. Anything I could not check is marked **unverified**.

## Summary

1. Go 1.5 is the precedent that fits. Go wrote a translator for its own C, not for C in general, kept a 3,289-line rule file, edited the C first so it would translate, and accepted the result only when the compiler's output was bit-identical. The compiler moved in February 2015, 14 months after the plan.
2. No general C translator keeps C's structure. Zig, c2rust and cxgo translate preprocessed code and try to recover macros afterwards. None documents X-macros. Zig's translate-c drops every function that contains a `goto`. Expect per-codebase rules.
3. Use clang as the front end. Its JSON AST has types, implicit conversions and macro locations, and `clang -E -dD` gives macro bodies. Emit each declaration once, from the file that spells it, so header lines count once. tcc's 859k lines/s counted 1.95M lines for 77k unique ones.
4. A permissive mix of 21 codebases with no duplicated files has about 1.63M physical lines and 1.17M non-blank, non-comment lines (measured, partly sampled). SDL3 and SQLite are the bulk. Real game code (Taisei, TIC-80, raylib examples) is about 230k lines. Keep the GPL id Software games (about 527k lines) out of the repository.
5. The C in the mix has 35.1 bytes per line and 7.1 C tokens per code line (measured). Translated chibi should have about 35 bytes and 6.4 tokens per code line, shorter than old Chibi's 46.5 bytes and 9 tokens. Lexing and parsing then cost about 220 cycles per line, not 292.
6. chibi-v2's parser accepts one identifier as a type and has no declarations for types, and no `for`, `switch`, `goto`, cast, `sizeof`, ternary, `++`, char literal or hex literal. Its precedence for `<<`, `&` and `|` differs from C's, so the translator must re-insert parentheses. Section 4 lists 28 C constructs, 26 of them undecided, and compares Odin, Zig, C3, Go and Jai for each.
7. Translated C has no comptime, generics or reflection, but it imitates them with macros: X-macro tables, macro-unrolled tables, `stb_ds` containers, field-offset tables and Taisei's task macros. Count and lift those, add a hand-written idiomatic seed, and compare cycles per line for each phase.
8. Self-hosting by translation is realistic if chibi-v2's C follows a few rules from now on. `++` appears only as a statement. Macros appear only as X-macro tables and constants. `#if` wraps only whole declarations. Integer conversions are written as explicit casts. CI translates `src/` on every change.

## Terms

| Term | Meaning here |
|---|---|
| Front end | The part of a compiler that reads source text and builds a tree of it, with types attached. |
| AST | Abstract syntax tree: one node per construct, such as a call or an `if`. |
| Translation unit (TU) | One `.c` file after the preprocessor has pasted in every header it includes. |
| Preprocessor | The step before C parsing that handles `#include`, `#define` and `#if` by rewriting text. |
| Object-like macro | `#define NAME body`. Each use of `NAME` is replaced by `body`. |
| Function-like macro | `#define NAME(a, b) body`. Uses look like calls, but they are text substitution. |
| X-macro | One macro holds a table of rows, written as calls to its parameters. Other code expands it with different parameters to generate an enum, a name table or a count from the same rows. `CHIBI_TOKENS` is one. |
| Spelling location / expansion location | Where a token is written, and where the macro that produced it was used. They differ inside macro expansions. |
| Lowering | Rewriting one construct into simpler ones. An example is turning `a[i++] = x` into `a[i] = x` followed by `i += 1`. |
| Re-sugaring | Finding a macro's expansion in the expanded AST and printing the macro's name instead. |
| Differential test | Run two implementations on the same input and compare their outputs bit for bit. |
| Self-hosting | The compiler is written in the language it compiles. |
| Bootstrap | The older compiler that builds the first self-hosted one. For Go this was Go 1.4. |

---

## 1. Precedents

### 1.1 Go 1.5: c2go

| Question | What happened | Source |
|---|---|---|
| Goal | Translate *their* C. The talk says: "_our_ C code, not _all_ C code". Special cases and annotations in the C were allowed, with 99%+ of code translated automatically. | [c2go.slide L113-123](https://github.com/golang/website/blob/eaf4e1aa02a736d035f64e9a0449cace03bc407f/_content/talks/2014/c2go.slide#L113-L123) |
| Why it was feasible | The design doc says macros, unions and bit fields were rare in the compilers, deliberately, and pointer arithmetic was rare too. | [go13compiler.md L36](https://github.com/golang/proposal/blob/83ee2aaf9fdcc1a1cf4bb4d20216fc979635cf04/design/go13compiler.md#L36) |
| C edited first | `#define` constants became enums, and function-like macros got prototypes in `/*c2go*/` comments ([ebce794](https://github.com/golang/go/commit/ebce79446dad)). `?:` and assignments in the middle of expressions were removed ([3be0a0e](https://github.com/golang/go/commit/3be0a0ef6fb8), [e82003e](https://github.com/golang/go/commit/e82003e750ee)). Each cleanup commit kept output bit-identical ([5c87cf7](https://github.com/golang/go/commit/5c87cf760821)). | commits |
| Preprocessor | None. The lexer drops every directive except `#include`, and headers are parsed once. | [cc/lex.go L346-359](https://github.com/rsc/c2go/blob/520c22818a0844fb1aefa35f8ff24e3c1fc8f66a/cc/lex.go#L346-L359), [L109-170](https://github.com/rsc/c2go/blob/520c22818a0844fb1aefa35f8ff24e3c1fc8f66a/cc/lex.go#L109-L170) |
| Parser | Its own yacc grammar, descended from Cox's 2006 tool. | [cc/cc.y L5-8](https://github.com/rsc/c2go/blob/520c22818a0844fb1aefa35f8ff24e3c1fc8f66a/cc/cc.y#L5-L8) |
| Side effects | `*p++`, embedded assignments and `?:` are hoisted into separate statements. Hoisting out of a conditionally evaluated operand is only detected, not handled. | [syntax.go L395-556](https://github.com/rsc/c2go/blob/520c22818a0844fb1aefa35f8ff24e3c1fc8f66a/syntax.go#L395-L556) |
| `goto` | The compiler had 1,032 `goto` statements and 241 labels. The tool handled the trivial ones, and people rewrote the rest by hand. | [c2go.slide L329-345](https://github.com/golang/website/blob/eaf4e1aa02a736d035f64e9a0449cace03bc407f/_content/talks/2014/c2go.slide#L329-L345) |
| `switch` | The tool drops trailing `break` and inserts explicit `fallthrough`. | [syntax.go L305-366](https://github.com/rsc/c2go/blob/520c22818a0844fb1aefa35f8ff24e3c1fc8f66a/syntax.go#L305-L366) |
| Unions | The lexer rewrites `union` as `struct`, which bloated those structs. | [cc/lex.go L480-481](https://github.com/rsc/c2go/blob/520c22818a0844fb1aefa35f8ff24e3c1fc8f66a/cc/lex.go#L480-L481), [gogo.slide](https://github.com/golang/website/blob/eaf4e1aa02a736d035f64e9a0449cace03bc407f/_content/talks/2015/gogo.slide#L191) |
| Specialization | `c2go.cfg` has 3,289 lines: 364 text-diff blocks, 70 hand-written function replacements, and rules for `bool`, `slice` (pointer, length and capacity fields become one Go slice), rename, export and delete. The include path was hard-coded to rsc's machine. | [c2go.cfg](https://github.com/rsc/c2go/blob/520c22818a0844fb1aefa35f8ff24e3c1fc8f66a/c2go.cfg), [config.go L95-219](https://github.com/rsc/c2go/blob/520c22818a0844fb1aefa35f8ff24e3c1fc8f66a/config.go#L95-L219), [cc/lex.go L478-496](https://github.com/rsc/c2go/blob/520c22818a0844fb1aefa35f8ff24e3c1fc8f66a/cc/lex.go#L478-L496) |
| Not general | The tool's doc comment says it is not a general-purpose tool and will not convert your program. | [main.go L9-15](https://github.com/rsc/c2go/blob/520c22818a0844fb1aefa35f8ff24e3c1fc8f66a/main.go#L9-L15) |
| Size | Over 60,000 lines of C in 2013 and 80,000+ by 2014. The compiler commit added 88,448 lines across 119 files. | [go13compiler.md L60](https://github.com/golang/proposal/blob/83ee2aaf9fdcc1a1cf4bb4d20216fc979635cf04/design/go13compiler.md#L60), [8c195bd](https://github.com/golang/go/commit/8c195bdf120e) |
| Hand work | The runtime (Go 1.4) was converted mostly by hand with machine help. The assemblers were finished by hand, and later rewritten in under 4,000 lines. The `grind` tool moved declarations and removed dead code. | [gogo.slide L93-101, L243-246](https://github.com/golang/website/blob/eaf4e1aa02a736d035f64e9a0449cace03bc407f/_content/talks/2015/gogo.slide#L93-L101), [382b44e](https://github.com/golang/go/commit/382b44eb7c43) |
| Test | Success meant bit-identical output: translate, compile, run, compare the generated code, repeat. `toolstash -cmp` compares object files. | [gogo.slide L107-128](https://github.com/golang/website/blob/eaf4e1aa02a736d035f64e9a0449cace03bc407f/_content/talks/2015/gogo.slide#L107-L128), [toolstash](https://github.com/golang/tools/blob/588ada3c4d4d6d24ab1758e86bd2768beba5946d/cmd/toolstash/main.go#L42-L66) |
| Timeline | Design doc December 2013. Talk April 2014, still a prototype. C annotations July 2014. liblink translated 19 January 2015, compiler 13 February, C compiler deleted 23 February, linker 27 February. Go 1.5 shipped 19 August 2015. | [go13compiler.md](https://github.com/golang/proposal/blob/83ee2aaf9fdcc1a1cf4bb4d20216fc979635cf04/design/go13compiler.md), [merge b986f3e](https://github.com/golang/go/commit/b986f3e3b544), [release history](https://go.dev/doc/devel/release) |
| Speed | The first output ran about 10× slower, and most of that was recovered. Go 1.5 builds were about 2× slower. The causes were C-style loops, escape analysis, interfaces in place of varargs, unions turned into structs, and GC. | [gogo.slide L181-212](https://github.com/golang/website/blob/eaf4e1aa02a736d035f64e9a0449cace03bc407f/_content/talks/2015/gogo.slide#L181-L212), [Go 1.5 notes](https://go.dev/doc/go1.5) |
| Bugs | The release notes say the translation found previously unknown bugs. | [Go 1.5 notes](https://go.dev/doc/go1.5) |
| Bootstrap | Go 1.4 or later builds Go 1.5, which then rebuilds itself. | [go15bootstrap.md L22-44](https://github.com/golang/proposal/blob/83ee2aaf9fdcc1a1cf4bb4d20216fc979635cf04/design/go15bootstrap.md#L22-L44) |

### 1.2 Zig translate-c and Aro

| Topic | Fact | Source |
|---|---|---|
| Front end | Aro is a C compiler written in Zig (MIT; C23 complete, GNU extensions partial). Translate-c switched from libclang to Aro in PR #24497, merged 2025-09-25. The PR deleted 5,940 lines of C++. | [Aro README](https://github.com/Vexu/arocc/blob/a068a88be3c78e81fef28c1d7da21143d8d3f779/README.md), [#24497](https://github.com/ziglang/zig/pull/24497), [#16268](https://github.com/ziglang/zig/issues/16268) |
| Why | To stop linking LLVM and Clang into the `zig` binary. | [#16270](https://github.com/ziglang/zig/issues/16270) |
| Status | `@cImport` was deprecated in 0.16.0 and removed in 0.17.0. Translation now lives in a separate package with its own release cadence. | [0.17.0 notes](https://ziglang.org/download/0.17.0/release-notes.html), [translate-c README](https://codeberg.org/ziglang/translate-c/src/commit/d6832bfb2c026a982c461a36c236acf5b42f7979/README.md#L3-L54) |
| Macros | It translates expanded code, then re-tokenizes and parses each macro body. Function-like macros become `inline fn` with `anytype` parameters, and object-like macros become constants. Failures become `@compileError`. A hard-coded table matches common idioms. | [Translator.zig L4400-4500](https://codeberg.org/ziglang/translate-c/src/commit/d6832bfb2c026a982c461a36c236acf5b42f7979/src/Translator.zig#L4400-L4500), [MacroTranslator.zig L28-129](https://codeberg.org/ziglang/translate-c/src/commit/d6832bfb2c026a982c461a36c236acf5b42f7979/src/MacroTranslator.zig#L28-L129), [PatternList.zig](https://codeberg.org/ziglang/translate-c/src/commit/d6832bfb2c026a982c461a36c236acf5b42f7979/src/PatternList.zig#L16-L80) |
| Headers | Everything in the TU is translated, including system headers. Lazy analysis hides failures until something uses them. | [langref 0.15.2](https://github.com/ziglang/zig/blob/e4cbd752c8c05f131051f8c873cff7823177d7d3/doc/langref.html.in#L6928-L6933) |
| Unsupported constructs | A record with a bitfield becomes an opaque type. A function with `goto` fails with a "TODO goto" and is demoted to an `extern` declaration, so its body is lost. Variadic definitions are demoted the same way. Fallthrough is handled by duplicating statements. | [Translator.zig](https://codeberg.org/ziglang/translate-c/src/commit/d6832bfb2c026a982c461a36c236acf5b42f7979/src/Translator.zig) |
| Comments | Not kept; the output is printed from a new Zig AST (my inference from the code). | same |

### 1.3 c2rust

| Topic | Fact | Source |
|---|---|---|
| Front end | A clang tool exports the AST as CBOR. It needs `compile_commands.json`. | [README L189-216](https://github.com/immunant/c2rust/blob/10c776738db3f6b202dd1cade85fcdd3c1ada6f8/README.md#L189-L216), [AstExporter.cpp](https://github.com/immunant/c2rust/blob/10c776738db3f6b202dd1cade85fcdd3c1ada6f8/c2rust-ast-exporter/src/AstExporter.cpp#L743-L754) |
| Macros | Object-like macros are rebuilt from all their expansions. If every expansion has a compatible type and no side effects, the macro becomes a `const`; otherwise it stays expanded. Function-like macro support does not work yet. | [macros.rs L50-118, L247-268](https://github.com/immunant/c2rust/blob/10c776738db3f6b202dd1cade85fcdd3c1ada6f8/c2rust-transpile/src/translator/macros.rs#L50-L118), [v0.21 post](https://immunant.com/blog/2025/10/c2rust_release/) |
| Headers | Each `.c` file is translated together with every header it includes, which produces many duplicate declarations. A refactoring pass merges them afterwards. | [header merging post](https://immunant.com/blog/2019/12/header_merging/), [README L233-240](https://github.com/immunant/c2rust/blob/10c776738db3f6b202dd1cade85fcdd3c1ada6f8/README.md#L233-L240) |
| `goto`, `switch` | A control-flow graph plus Emscripten's "Relooper" algorithm, which rebuilds loops and ifs from arbitrary jumps. | [cfg/mod.rs L1-17](https://github.com/immunant/c2rust/blob/10c776738db3f6b202dd1cade85fcdd3c1ada6f8/c2rust-transpile/src/cfg/mod.rs#L1-L17) |
| Cleanup | The README calls the output unsafe and unidiomatic, a first step. Comment preservation is experimental. Output goes through rustfmt, so line structure is lost. | [README L23-25](https://github.com/immunant/c2rust/blob/10c776738db3f6b202dd1cade85fcdd3c1ada6f8/README.md#L23-L25), [known-limitations.md](https://github.com/immunant/c2rust/blob/10c776738db3f6b202dd1cade85fcdd3c1ada6f8/docs/known-limitations.md#L6-L29) |
| Real ports | ioquake3's game, UI, client and GL1 renderer ran "after a couple of days", with hand fixes. No line counts are given. | [Quake 3 post](https://immunant.com/blog/2020/01/quake3/) |

### 1.4 c2nim and cxgo

| Tool | Front end | Macros | Headers | Unsupported | Source |
|---|---|---|---|---|---|
| c2nim (C to Nim) | Its own lexer and parser, with the preprocessor in the parser; meant for headers | Object-like to `const`, function-like to `template`, which the manual admits is inexact. `#if` blocks become `when`, so every branch is kept | Not followed; `#include "x.h"` becomes `import x` | `goto` becomes a labeled `break`; fallthrough is silently dropped (my inference from the code). Output is meant to be edited by hand | [manual](https://github.com/nim-lang/c2nim/blob/f063b884131d062f5da367fa4e112129aa87e5e7/doc/c2nim.rst#L16-L116), [cparser.nim](https://github.com/nim-lang/c2nim/blob/f063b884131d062f5da367fa4e112129aa87e5e7/cparser.nim#L4012-L4018) |
| cxgo (C to Go) | modernc.org/cc v3, written in Go, needs no clang | Constants are substituted at their uses, which the docs list as a bug. Object-like macros from the current file also become `const`. Function-like macros are skipped | Emits only the current file and its paired `.h` | `goto` stays `goto`, or the function is flattened into a state machine. Pointer arithmetic becomes `unsafe.Add`. All comments are dropped | [README](https://github.com/gotranspile/cxgo/blob/e682671cd3c9b33e82e1a0796c7477059aa14178/README.md#L10-L84), [quirks.md](https://github.com/gotranspile/cxgo/blob/e682671cd3c9b33e82e1a0796c7477059aa14178/docs/quirks.md#L7-L33), [config.md](https://github.com/gotranspile/cxgo/blob/e682671cd3c9b33e82e1a0796c7477059aa14178/docs/config.md#L1-L329) |

### 1.5 What carries over to chibi-v2

- **Specialize.** Every tool that worked at scale had per-codebase rules: c2go's config, cxgo's `cxgo.yml`, c2nim's option files. Plan a small rule file per corpus codebase.
- **Translate expanded code, then re-sugar.** That is the only way to get correct semantics for code you don't control. Keep the macro's name only when the macro's whole expansion maps to one AST node.
- **Change your own C, not the tool.** Go edited the compiler's C until translation was mechanical. Do the same for chibi-v2.
- **Prove it with outputs.** Go's test was bit-identical compiler output. chibi-v2's bench already computes token and node checksums (`checksum`, `node_checksum` and `fingerprint` in `bench/bench.c`).
- **Expect cleanup and a slower first result.** Go's first output was about 10× slower. Translated code is C-shaped.
- **No tool handles X-macros,** and only c2go and c2nim keep comments in place. chibi-v2 needs both.

---

## 2. The C front end

### 2.1 Options

| Option | What it gives | What it lacks | Fit |
|---|---|---|---|
| clang JSON AST, `clang -Xclang -ast-dump=json -fsyntax-only` | The full typed AST. Implicit conversions appear with a `castKind` ([JSONNodeDumper.cpp L1437](https://github.com/llvm/llvm-project/blob/llvmorg-21.1.0/clang/lib/AST/JSONNodeDumper.cpp#L1437)). Every location has spelling and expansion parts, an `isMacroArgExpansion` flag, and `includedFrom` ([L258-331](https://github.com/llvm/llvm-project/blob/llvmorg-21.1.0/clang/lib/AST/JSONNodeDumper.cpp#L258-L331)). Readable as text. sokol's binding generator uses it ([gen_ir.py L124-133](https://github.com/floooh/sokol/blob/f3a42297dfe64edb390deaa5883728a558499eb5/bindgen/gen_ir.py#L124-L133)) | No macro definitions. The format may change between major releases ([D60910](https://reviews.llvm.org/D60910)). Locations omit the file and line when they match the previous location, so the dump must be read in order ([L283-288](https://github.com/llvm/llvm-project/blob/llvmorg-21.1.0/clang/lib/AST/JSONNodeDumper.cpp#L283-L288)). Dumps are large (**size not measured**) | Best for a translation that keeps semantics. Pin the clang version; Homebrew LLVM 23.1.1 is installed on this machine |
| libclang (stable C API) | Callable from C. Gives macro definition and expansion cursors with `CXTranslationUnit_DetailedPreprocessingRecord`, `clang_Cursor_isMacroFunctionLike`, `clang_tokenize`, and `clang_Location_isFromMainFile` ([Index.h](https://github.com/llvm/llvm-project/blob/llvmorg-21.1.0/clang/include/clang-c/Index.h#L696), [CXSourceLocation.h L96](https://github.com/llvm/llvm-project/blob/llvmorg-21.1.0/clang/include/clang-c/CXSourceLocation.h#L96)). Operator kinds only since LLVM 17 ([Index.h L6896, L6946](https://github.com/llvm/llvm-project/blob/llvmorg-21.1.0/clang/include/clang-c/Index.h#L6896)) | Implicit casts, designated initializers, `offsetof` and `va_arg` all appear as `CXCursor_UnexposedExpr` ([CXCursor.cpp L313-341](https://github.com/llvm/llvm-project/blob/llvmorg-21.1.0/clang/tools/libclang/CXCursor.cpp#L313-L341)). Those are the nodes a faithful translation needs most | Good for macros, weak for semantics. Apple's Command Line Tools ship the `.dylib` but not the headers; Homebrew ships both |
| Aro | A full C front end with its own preprocessor. Zig's translate-c reads macro bodies from it | Written in Zig, so a C tool would need a Zig build. GNU extensions are partial ([README](https://github.com/Vexu/arocc/blob/a068a88be3c78e81fef28c1d7da21143d8d3f779/README.md)). I found no C API (**unverified**) | Only if the translator is written in Zig |
| tree-sitter-c | A C library (MIT) that parses C *without* preprocessing. `#if` and `#define` are nodes, so every branch survives ([grammar.js L97-175](https://github.com/tree-sitter/tree-sitter-c/blob/b780e47fc780ddc8da13afa35a3f4ed5c157823d/grammar.js#L97-L175)) | No types. Macro bodies are opaque text ([L165](https://github.com/tree-sitter/tree-sitter-c/blob/b780e47fc780ddc8da13afa35a3f4ed5c157823d/grammar.js#L165)). Type-versus-expression ambiguity is guessed ([L38-43](https://github.com/tree-sitter/tree-sitter-c/blob/b780e47fc780ddc8da13afa35a3f4ed5c157823d/grammar.js#L38-L43)) | Covers every platform's code with no headers, but only for a corpus that needs to parse and never type-check |
| Own C parser | Full control and no dependency. Keeps macros as written (c2go, c2nim) | Real third-party C needs the preprocessor, GNU extensions and system headers. Years of work | Only for chibi-v2's own C, if it follows conventions |

**Recommendation.** Use clang for the corpus and for self-hosting: the JSON AST for code, `clang -E -dD` for macro definitions in order with their file and line, and the raw source for comments. A cheaper tree-sitter-c path is possible for a parse-only corpus, but it would be a second translator with different output. Use it only if T3 fails.

### 2.2 Macros

Translating from the expanded AST keeps the semantics but loses names and structure. Keeping macros as written needs heuristics, and each heuristic can be wrong. The translator should do the first and then re-sugar where it is safe.

**Object-like constants.** These come from chibi-v2's own headers:

| C | chibi | Decision needed |
|---|---|---|
| `#define PARSER_MAX_DEPTH 256` | `const PARSER_MAX_DEPTH := 256` | none; this parses today (`parse_const`) |
| `#define SLOW 0x80` | `const SLOW := 128`, or a hex literal | hex literals (`number_end` in `src/lexer.c` stops at the `x`) |
| `#define LEXER_MAX_SOURCE ((size_t)INT32_MAX - LEXER_SOURCE_PADDING)` | `const LEXER_MAX_SOURCE: «usize» = «cast»(INT32_MAX) - LEXER_SOURCE_PADDING` | integer type names, cast syntax |
| `enum { NKINDS = 0 CHIBI_TOKENS(CHIBI_COUNT, …) }` | `const NKINDS := «count of TOKEN_ROWS»` | compile-time length of a constant array |

`«…»` marks syntax the owner has not chosen.

**Function-like macros.** chibi-v2 has 47. Lua 5.4 has 649 in 31,708 lines; for example, `setobj` in [lobject.h L118-121](https://github.com/lua/lua/blob/v5.4.9/lobject.h#L118-L121) is a statement block. Rules, in order:

1. If the body parses as one expression and every expansion site has the same types, emit an inline function. This is Zig's rule plus c2rust's type unification.
2. If the parameter types differ between sites, as with `MAX(a, b)` or chibi-v2's `simd_eq` (which uses `__typeof__`), emit a generic function. That waits on generics.
3. If the body is `{ … }` or `do { … } while (0)` and the arguments are lvalues, emit a function that takes pointers.
4. Attribute wrappers (`ALWAYS_INLINE`, `NOINLINE`, `NORETURN`, `LIKELY`) map through a fixed table to chibi attributes or intrinsics.
5. Anything else is expanded at each use. The expanded lines are counted separately (section 2.4), and the macro goes in a report for a per-codebase rule.

**X-macros.** `CHIBI_TOKENS` has 91 rows and 12 expansion sites; `CHIBI_NODES` has 81 rows and 11 sites. Each site passes one macro per row kind. `CHIBI_TOKEN_NAME` builds the `TokenKind` enum, `CHIBI_COUNT` counts rows, and `KIND2_OP1` fills a lookup table. tcc uses the same pattern: [tcctok.h](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tcctok.h#L3-L12) is expanded into a keyword string in [tccpp.c L64-68](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tccpp.c#L64-L68). There are two ways to translate them:

- **Expand.** Write out every expansion. This is mechanical and parses with almost no new syntax. But the single table of rows is gone, and by my count the 172 rows plus 23 use lines become about 980 generated entries, five times as many lines. Those lines must not count as source lines.
- **Lift to compile-time code.** Emit the rows once as a constant array of records. Each expansion site becomes a compile-time expression over that array:

```
// Placeholder syntax. «…» marks an owner decision.
«type TokenRow = struct» { kind: RowKind, name: «ident», text: «str», c0: u8, c1: u8, c2: u8 }

const TOKEN_ROWS := «[]TokenRow» {
    { .kind = .other, .name = TK_EOF,  .text = "EOF" },
    { .kind = .kw,    .name = TK_IF,   .text = "if" },
    { .kind = .op2,   .name = TK_SHL,  .c0 = «'<'», .c1 = «'<'» },
    // … one line per C row, 91 in all
}

«enum TokenKind, built at compile time from TOKEN_ROWS[i].name»
const NKINDS := «len»(TOKEN_ROWS)
const keywords := «compile-time map over TOKEN_ROWS where kind == .kw: row.text»
```

The translator recognizes the pattern exactly. A table macro's body is a sequence of calls to its own parameters, and every use passes macros whose bodies are a single row template. Each template shape, such as "name followed by a comma inside an enum", "`+1`" or "`[token] = value,`", maps to one compile-time form. This depends on the open decisions on compile-time reach in the [research README](README.md), in particular "May it generate declarations?". Until those are made, expand, and count the expanded lines separately.

Table-building macros are the same problem without rows. `bit_positions[256][8]` in `src/lexer.c` is built by `BIT_ROW64` → `BIT_ROW16` → `BIT_ROW4` → `BIT_ROW` → `BIT_POS`, which yields 2,048 constants. In chibi it is a compile-time function of the index. The translator cannot discover that function by itself, so this needs a per-file rule.

### 2.3 Headers

- **Emit a declaration only from the file that spells it.** In the JSON, a declaration whose location carries `includedFrom` comes from a header. Skip it in that TU.
- **Translate each project header once, as its own unit.** Run clang on the header as the main file. Its declarations become one chibi file.
- **Header-only libraries** (stb, sokol, miniaudio, cute) keep their implementation behind `#define X_IMPLEMENTATION`. Translate one TU that defines the flag and includes the header, and treat the header as the owned file.
- **Drop prototypes.** If chibi lets declarations appear in any order (README point 1), a prototype adds nothing. Removing prototypes removes C lines that have no chibi line, so record them in the line map.
- **Never emit system headers.** Calls into libc or the OS go through a fixed map to foreign declarations.
- **Why it matters.** tcc's 859k lines/s compiled 1,950,947 lines, of which 76,936 were unique ([bellard.org/tcc](https://bellard.org/tcc/); [fast-compilers.md](fast-compilers.md) section 2.7). c2rust's per-file translation duplicates header declarations the same way ([post](https://immunant.com/blog/2019/12/header_merging/)).

### 2.4 Keeping line counts honest

1. Count the lines of the emitted chibi files, each file once. Never count C lines, or lines per TU.
2. Report four numbers per codebase: physical lines, non-blank lines, non-blank non-comment lines, and bytes. Add the token count from chibi-v2's own lexer. Any lines/s claim can then be converted to the others.
3. Keep C's line structure. Each statement goes on its source line. Hoisted statements stay on the same line, separated by `;`, which `parse_statements` already accepts. For example, `emit(p, NODE_FIELD, p->pos++)` becomes `at := p.pos; p.pos += 1; emit(p, NODE_FIELD, at)` on one line, assuming `.` reaches through pointers.
4. Write a line map (chibi line → C file:line, plus a reason flag: source, hoisted, expanded or generated). Count expanded and generated lines separately.
5. Exclude data-only lines (lines of literals and commas) from code lines. Shader blobs make up 14.5% of sokol's lines.
6. Exclude generated files (SDL's `SDL_blit_auto.c`, dynapi), vendored copies, and duplicate trees (section 3.4).
7. Report each codebase's ratio of chibi lines to C lines (T2).

---

## 3. Candidate codebases

### 3.1 Method

- Sizes come from `gh api repos/OWNER/REPO/git/trees/SHA?recursive=1` (blob sizes), and lines from each selected file fetched with `gh api …/contents/PATH?ref=SHA` to stdout. Nothing was cloned or built.
- SDL3 (1 file in 3), Taisei (1 in 3) and Quake 2, Quake III and DOOM (1 in 2) were sampled. Their line totals are bytes ÷ the sample's bytes per line, marked `~`.
- Tokens come from a regex tokenizer over C with comments removed. It counts identifiers, numbers, strings, character literals and punctuators on code lines, which excludes blank, comment-only and preprocessor lines. It is an approximation of a C lexer.
- "Data %" is the share of lines that hold only literals and commas.
- The `#define` and `#if` columns count directive lines.

### 3.2 Measurements

| Codebase (selection) | Files | Lines | Non-blank, non-comment | Bytes/line | C tokens per code line | Comment lines | `#define` obj | `#define` fn | `#if` | `goto` | Data % |
|---|---|---|---|---|---|---|---|---|---|---|---|
| chibi-v2 `src/` (ae4d366 + uncommitted) | 13 | 3,005 | 2,555 | 28.7 | 6.94 | 1% | 27 | 47 | 14 | 0 | 0.2 |
| Old Chibi C, `src/` at `1363ff20` | 251 | 66,032 | 60,956 | 38.3 | 7.05 | 5% | 558 | 448 | 242 | 136 | 0.2 |
| Lua 5.4.9, top-level `.c`/`.h` | 63 | 31,708 | 21,232 | 29.1 | 7.43 | 18% | 627 | 649 | 233 | 39 | 0.1 |
| tcc, top level + `lib/` | 59 | 74,124 | 61,622 | 31.5 | 6.68 | 8% | 3,807 | 409 | 1,218 | 556 | 0.3 |
| SQLite 3.55.0 `src/` core (no tests, Tcl, shell, Windows) | 97 | 173,489 | 111,351 | 35.0 | 7.08 | 30% | 1,724 | 671 | 2,704 | 871 | 0.3 |
| SDL3 `src/` first-party (966 files) | 322 | ~380,700 | ~288,900 | 36.1 | 6.23 | 10% | ~3,180 | ~1,150 | ~4,740 | ~605 | 0.3 |
| raylib `src/` + `src/platforms` | 23 | 59,738 | 40,653 | 42.2 | 9.28 | 16% | 720 | 68 | 1,087 | 0 | 0.6 |
| raylib examples (deduplicated) | 226 | 38,441 | 20,564 | 42.7 | 9.24 | 28% | 311 | 4 | 64 | 0 | 0.1 |
| Quake (WinQuake only) | 195 | 98,380 | 66,023 | 22.1 | 6.28 | 14% | 1,741 | 61 | 325 | 93 | 0.1 |
| Quake 2 (no `ctf`, Win32 only; 194 files) | 97 | ~122,200 | ~87,200 | 25.2 | 6.78 | 13% | ~7,320 | ~57 | ~193 | ~78 | 0.6 |
| Quake III Arena (`code/` engine and game; 303 files) | 151 | ~244,800 | ~161,800 | 27.8 | 7.02 | 20% | ~3,540 | ~234 | ~1,070 | ~24 | 0.4 |
| DOOM `linuxdoom-1.10` (124 files) | 62 | ~61,500 | ~38,100 | 20.1 | 4.62 | 20% | ~630 | ~20 | ~250 | ~17 | 0.4 |
| Box2D v3 `src/` + `include/` | 81 | 47,335 | 33,064 | 31.0 | 6.93 | 13% | 188 | 146 | 292 | 0 | 0.0 |
| Chipmunk2D `src/` + `include/` | 63 | 14,697 | 9,369 | 31.3 | 7.74 | 18% | 106 | 24 | 95 | 2 | 0.2 |
| Cute Framework C parts (`include/`, `libraries/cute`) | 64 | 92,268 | 58,204 | 39.4 | 9.95 | 26% | 795 | 692 | 814 | 17 | 0.1 |
| miniaudio `miniaudio.h`/`.c` | 2 | 96,951 | 72,956 | 42.9 | 7.15 | 10% | 1,245 | 416 | 1,781 | 137 | 0.2 |
| Nuklear `src/` | 48 | 31,349 | 23,005 | 37.7 | 8.36 | 18% | 154 | 110 | 214 | 16 | 0.0 |
| sokol headers + `util/` | 22 | 93,229 | 72,100 | 48.0 | 11.99 | 14% | 1,139 | 226 | 1,723 | 19 | 14.5 |
| stb top level | 19 | 38,581 | 26,330 | 37.1 | 9.44 | 21% | 390 | 306 | 674 | 57 | 0.6 |
| flecs `src/` + `include/` | 287 | 149,786 | 112,615 | 29.9 | 5.94 | 11% | 761 | 759 | 1,325 | 703 | 0.0 |
| Taisei `src/` (690 files) | 230 | ~119,700 | ~87,300 | 27.3 | 7.03 | 8% | ~600 | ~920 | ~390 | ~143 | 0.0 |
| TIC-80 first-party `src/` | 121 | 72,906 | 55,825 | 30.5 | 7.21 | 6% | 308 | 221 | 601 | 37 | 0.0 |
| cglm | 235 | 47,113 | 24,683 | 24.5 | 6.76 | 36% | 507 | 168 | 856 | 0 | 0.0 |
| Wren VM | 22 | 12,437 | 7,765 | 31.4 | 6.54 | 21% | 79 | 96 | 57 | 2 | 0.0 |
| PhysicsFS `src/` | 40 | 30,585 | 19,741 | 30.4 | 6.40 | 20% | 381 | 161 | 308 | 21 | 0.1 |
| libtcod `src/libtcod` | 105 | 29,765 | 20,387 | 36.5 | 7.55 | 26% | 370 | 51 | 309 | 0 | 0.1 |

The SQLite amalgamation has about 238K lines, or 145K without blank lines and comments, as of 2021 ([sqlite.org](https://sqlite.org/amalgamation.html)). It also contains extensions and generated parser code, so the core `src/` count above is smaller. Use the core or the amalgamation, never both.

Commits read: Lua [`v5.4.9`](https://github.com/lua/lua/tree/v5.4.9), tcc [`aade7baf`](https://github.com/TinyCC/tinycc/tree/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e), SQLite [`8d769aeb`](https://github.com/sqlite/sqlite/tree/8d769aeb61d058fdd7f568ceac2ab39e73b4574a), SDL [`8b6ae5d7`](https://github.com/libsdl-org/SDL/tree/8b6ae5d72496c04723fd3016ea7a5c138a156422), raylib [`bbd4af6b`](https://github.com/raysan5/raylib/tree/bbd4af6bee1765b7384931fd40a1b79e539fbef4), Quake [`bf4ac424`](https://github.com/id-Software/Quake/tree/bf4ac424ce754894ac8f1dae6a3981954bc9852d), Quake 2 [`372afde4`](https://github.com/id-Software/Quake-2/tree/372afde46e7defc9dd2d719a1732b8ace1fa096e), Quake III [`dbe4ddb1`](https://github.com/id-Software/Quake-III-Arena/tree/dbe4ddb10315479fc00086f08e25d968b4b43c49), DOOM [`a77dfb96`](https://github.com/id-Software/DOOM/tree/a77dfb96cb91780ca334d0d4cfd86957558007e0), Box2D [`dd8bd3fb`](https://github.com/erincatto/box2d/tree/dd8bd3fb525f0f33f1079f8ca3c5770783c44a53), Chipmunk2D [`f2f3d662`](https://github.com/slembcke/Chipmunk2D/tree/f2f3d66220b8bb1568b8402c6d194d73d4e477d1), Cute Framework [`6b00978a`](https://github.com/RandyGaul/cute_framework/tree/6b00978ac4a453a9a62b7e93e7d8508a2320f317), miniaudio [`877ea4a7`](https://github.com/mackron/miniaudio/tree/877ea4a7c2601c7b0f2a44e83dfac42e6470fea6), Nuklear [`bab18497`](https://github.com/Immediate-Mode-UI/Nuklear/tree/bab18497df988c171fd0d82baad798e1e0e34259), sokol [`f3a42297`](https://github.com/floooh/sokol/tree/f3a42297dfe64edb390deaa5883728a558499eb5), stb [`4e9b368b`](https://github.com/nothings/stb/tree/4e9b368bfc071e97206ea5c6adba68dd0cbf8897), flecs [`68790666`](https://github.com/SanderMertens/flecs/tree/68790666a02b1dc679cdc9bc2d19fc285ba4fe1b), Taisei [`baf029b0`](https://github.com/taisei-project/taisei/tree/baf029b05b625f5543543b8d96ecf38ae8e7f1b1), TIC-80 [`8194166a`](https://github.com/nesbox/TIC-80/tree/8194166ab64906565d9e1a352dd6706cf99b834e), cglm [`b9b570b7`](https://github.com/recp/cglm/tree/b9b570b78d58ceecc704f7a4e19b84dc45937ef4), Wren [`99d2f0b8`](https://github.com/wren-lang/wren/tree/99d2f0b8fc2686134b32b18166e037639f7e9f2c), PhysicsFS [`c30aa908`](https://github.com/icculus/physfs/tree/c30aa9086d4caf7548ee76655376aa7aa9098ffc), libtcod [`85948d32`](https://github.com/libtcod/libtcod/tree/85948d32c033f5dce7957af2728250fdee2d3e22). Old Chibi C was read locally with `git ls-tree`/`cat-file` on `origin/codex/resume-native-jobs-20261104`.

### 3.3 License, game closeness and macro use

Game closeness runs from 5, a game itself, down to 1, unrelated to games. "Commit" says whether a translated copy can sit in the owner's repository. This is not legal advice.

| Codebase | License | Commit a translated copy? | Game | How it uses macros |
|---|---|---|---|---|
| chibi-v2 | owner's | yes | 2 | X-macro tables (`CHIBI_TOKENS` with 5 row kinds, `CHIBI_NODES` with 7), table builders (`BIT_ROW64`), attribute wrappers |
| Old Chibi C | owner's | yes | 2 | 144 `#undef`s, mostly X-macro expanders; 136 `goto` |
| Lua | MIT ([lua.h](https://github.com/lua/lua/blob/v5.4.9/lua.h#L500-L518)) | yes, keep the notice | 3 | The most function-like macros per line (`setobj`, `cast_int`); configuration in `luaconf.h` |
| tcc | LGPL-2.1 ([COPYING](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/COPYING)) | possible as a separate LGPL directory with source; better kept out | 2 | X-macro token and opcode tables; `elf.h` is 3,325 lines of glibc `#define`s; the most `goto`s per line |
| SQLite | public domain ([copyright](https://sqlite.org/copyright.html)) | yes | 1 | Feature switches (`SQLITE_OMIT_*`, 2,704 `#if`); coverage macros; opcodes generated by Tcl scripts |
| SDL3 | zlib ([LICENSE.txt](https://github.com/libsdl-org/SDL/blob/8b6ae5d72496c04723fd3016ea7a5c138a156422/LICENSE.txt)) | yes; zlib requires altered copies to be marked as altered | 3 | Platform and backend `#if`s; a generated dynapi X-macro; a 20-line license comment in each of 966 files (about 19k lines) |
| raylib + examples | zlib ([LICENSE](https://github.com/raysan5/raylib/blob/bbd4af6bee1765b7384931fd40a1b79e539fbef4/LICENSE)) | yes, mark as altered | 4 / 5 | `SUPPORT_*` feature flags; colors are compound-literal macros ([raylib.h L181](https://github.com/raysan5/raylib/blob/bbd4af6bee1765b7384931fd40a1b79e539fbef4/src/raylib.h#L181)); `1.0f` suffixes |
| Quake 1, 2, III; DOOM | GPL-2.0 ([Quake gnu.txt](https://github.com/id-Software/Quake/blob/bf4ac424ce754894ac8f1dae6a3981954bc9852d/gnu.txt)) | a translated copy is a modified work under GPL; keep it out of the repo and translate locally | 5 | Mostly constants; Quake 2 has 7k `#define`s, mostly monster animation frame numbers; save-game field tables built with `FOFS` |
| Box2D v3 | MIT | yes | 4 | Few: asserts and SIMD selection |
| Chipmunk2D | MIT | yes | 4 | Few |
| Cute C parts | zlib or Unlicense ([LICENSE](https://github.com/RandyGaul/cute_framework/blob/6b00978ac4a453a9a62b7e93e7d8508a2320f317/LICENSE)) | yes | 4 | Single-header `*_IMPLEMENTATION` guards; platform `#if`s. **The framework itself is C++** (39 `.cpp` files in `src/`; its README says so); only these headers are C |
| miniaudio | Unlicense or MIT-0 | yes | 4 | 1,781 backend `#if`s, so one platform configuration sees a fraction |
| Nuklear | MIT or public domain | yes | 4 | Few; `nuklear.h` is a generated amalgamation of `src/` |
| sokol | zlib | yes, mark as altered | 4 | Backend `#if`s (Metal, D3D11, GL, WebGPU, Vulkan); shader bytecode as data |
| stb | MIT or public domain | yes | 4 | Implementation guards; `stb_ds` builds generic containers from macros |
| flecs | MIT | yes | 4 | API macros (`ECS_STRUCT` for reflection metadata); 703 `goto`s for error exits |
| Taisei | MIT for code ([COPYING.txt](https://github.com/taisei-project/taisei/blob/baf029b05b625f5543543b8d96ecf38ae8e7f1b1/COPYING.txt)) | yes; assets have other licenses, so take code only | 5 | A coroutine DSL (`TASK`, `INVOKE_TASK` in [taskdsl.h](https://github.com/taisei-project/taisei/blob/baf029b05b625f5543543b8d96ecf38ae8e7f1b1/src/coroutine/taskdsl.h#L177-L227)); the most designated initializers (69 per 1,000 lines) |
| TIC-80 | MIT | yes | 5 | Moderate; a fantasy console runtime plus its editors |
| cglm | MIT | yes | 4 | SIMD `#if` paths; 36% comment lines |
| Wren | MIT | yes | 3 | Opcode X-macro |
| PhysicsFS | zlib | yes | 4 | Platform `#if`s |
| libtcod | BSD-3-Clause | yes | 4 | Few |

### 3.4 Duplication traps

These repositories contain the same code twice. Count one copy.

| Repository | Duplicate | Keep |
|---|---|---|
| SQLite | the amalgamation versus `src/` | one of them |
| Nuklear | `nuklear.h` versus `src/` | `src/` |
| flecs | `distr/flecs.c` versus `src/` | `src/` |
| miniaudio | `extras/miniaudio_split` versus `miniaudio.h` | `miniaudio.h` |
| raylib | `src/external` (vendored GLFW, miniaudio, stb); `raygui.h` copied into 7 example folders | `src/` and one copy of each example header |
| SDL3 | `src/hidapi`, `src/libm`, `src/video/khronos`, `stb_image.h`, `miniz.h`, dlmalloc, shader-blob headers, `SDL_blit_auto.c` | first-party code only |
| Quake | `QW/` is a near-copy of `WinQuake/` | `WinQuake/` |
| Quake 2 | `ctf/` is a near-copy of `game/`; per-platform folders share files | `game/` and one platform |
| Quake III | `bspc/` copies botlib and q3map code; `lcc/` has per-OS headers | `code/` without `bspc` and `jpeg-6` |
| TIC-80 | `src/ext`, `vendor/`, shader blobs in `src/system/sokol` | first-party `src/` |
| Several | stb, sokol, miniaudio and SDL headers vendored into other projects | the upstream copy |

### 3.5 Recommended mix

These are all permissive or the owner's own code, with no duplicated files.

| Group | Codebases | Physical lines | Non-blank, non-comment |
|---|---|---|---|
| Game code | Taisei, TIC-80, raylib examples | ~231k | ~164k |
| Game libraries | raylib, sokol, Box2D, Chipmunk2D, flecs, Cute C parts, stb, Nuklear, miniaudio, cglm, PhysicsFS, libtcod | ~731k | ~513k |
| Platform and scripting | SDL3, Lua, Wren | ~425k | ~318k |
| Systems | SQLite core | 173k | 111k |
| Owner's code | old Chibi C, chibi-v2 | 69k | 64k |
| **Total** (2,334 files, 57.9 MB) | | **~1,630k** | **~1,170k** |

- Non-blank, non-comment lines include 97k preprocessor lines (5.9%), which mostly vanish in translation, and 16k data-only lines. Under the strictest count, code lines that are not preprocessor or data lines, the mix has about 1.06M. That still clears 1M.
- Headroom, if T3 shows that clang on macOS cannot reach enough of it: SQLite's FTS3, FTS5, R-Tree and session extensions (about 76k lines, estimated from their 2.7 MB), tcc (74k, if LGPL is acceptable), or GPL games kept outside the repo.
- The mix is mostly library code. Only Taisei, TIC-80 and the raylib examples are games. T5 tests whether that matters.

### 3.6 What translated lines will look like

| Measure | C mix (measured) | Translated chibi (estimate) | Old Chibi (from [hypotheses.md](../hypotheses.md)) | Old Chibi compiler source (measured) |
|---|---|---|---|---|
| Bytes per physical line | 35.1 | about 34–35 | 46.5 | 50.4 |
| Tokens per code line | 7.10 C tokens | about 6.4 | 9 | not measured |
| Comment-only lines | 15.7% | same | n/a | 7.6% |
| Blank lines | 12.7% | same | n/a | 1.9% |

- These exclude the sokol shader-blob files.
- The estimate drops the `;` at the end of statements (0.55 per code line) and the parentheses around `if`, `while` and `switch` conditions (0.2 per code line). It adds one token for each `++` or `--` that becomes `+= 1`.
- **Effect on the budget:** at the measured 6.276 cycles per byte for lexing and parsing ([hypotheses.md](../hypotheses.md)), 35 bytes per line costs about 220 cycles per line, against 292 at old Chibi's line length. H2 gets more margin, if H1 holds.
- The repository does not record bytes per line or tokens per line for the generated programs. `make bench` should print them, so H1 can compare line shapes too.

---

## 4. Syntax decisions the translator needs

> **Update, 2026-11-09: mostly settled.** Since this note was written, the owner and the parser thread have settled pointer, slice and array types, `struct`, `enum`, `for` with ranges, `switch`, struct and array literals, constants and functions. [decisions.md](../decisions.md) lists them. The table below shows what the other languages chose. Still open for translation: unions, casts and `sizeof`, `++` and `--`, the ternary operator, character literals, `goto`, varargs and bitfields. Number literals (hex, digit separators) were in progress.

### 4.1 What chibi-v2 parses today

Read from `src/parser.c` and `src/lexer.c`:

- **Statements:** `{ }`, `if`/`else if`/`else`, `while`, `return`, `break`, `continue`, `defer`, `const name := e` and `const name: T = e`, `fn name(params) -> T { … }` or `=> e`, `x := e`, `x: T` and `x: T = e`. Also assignment with `=` and the 10 compound operators, and expression statements (`parse_statement`).
- **Expressions:** names, int, float, string, `true`, `false`, `nil`; prefix `- ! ~ &`; postfix `.field`, `[i]`, `(args)` with named arguments `.x = v`, and dereference `.*`; `fn(x) => e` literals; `and`, `or` (`parse_operand`, `parse_postfix`, `parse_binary`). `&&` and `||` give an error that suggests `and` and `or` (`fail`).
- **Types:** one identifier only (`parse_type`).
- **Precedence:** shifts and `&` bind like `*`, and `|` and `^` like `+`. So `a << b + c` parses as `(a << b) + c` and `a & b == c` as `(a & b) == c` (`test/parser_test.c` lines 142–143). C means `a << (b + c)` and `a & (b == c)`. The translator must print from the tree and add parentheses wherever the two precedence tables disagree.
- **Rejected:** chained comparisons, chained assignments, and assignments inside expressions.
- **Statement ends:** a newline ends a statement, and a `(` or `[` at the start of a line does not continue the previous expression (`starts_line` checks).
- **Lexer gaps:** no `++` or `--` token (`++` lexes as two `+`), no character literal (`'` lexes as an unknown operator), no hex, octal or binary literal, no suffix, no `/* */` comment. Only JSON-style escapes `\" \\ \/ \b \f \n \r \t \uXXXX` are accepted (`escape_len`), so C's `\0`, `\x41`, `\033`, `\'` and `\a` are errors. `..` is two dots.
- **Keywords that are lexed but not parsed:** `for switch case default goto type struct union enum package import pub is in as sizeof alignof offsetof typeof cast yield`, and the tokens `? :: .. @ $ =>`.

In the tables below, "C use" is per 1,000 code lines of the recommended mix (measured; sokol blob files excluded). "Not measured" means my regex counter cannot tell the construct apart. Jai has no public primary docs, so every Jai cell comes from the community wiki and is **unverified**.

### 4.2 Types and declarations

| Construct | C use | chibi-v2 today | Odin | Zig 0.15.2 | C3 | Go | Jai (unverified) |
|---|---|---|---|---|---|---|---|
| Pointer type, member access through a pointer | `->` 227 | `&x`, `p.*`; no pointer type | `^T`, `p^`; `.` auto-dereferences ([doc](https://odin-lang.org/docs/overview/#pointers)) | `*T`, `p.*` ([doc](https://ziglang.org/documentation/0.15.2/#Pointers)) | `T*`, `*p`; `.` dereferences one level, no `->` ([doc](https://c3-lang.org/c-to-c3/a-guide-for-c-programmers/#member-access-using-even-for-pointers)) | `*T`, `*p`; selectors auto-dereference ([spec](https://go.dev/ref/spec#Pointer_types)) | `*T`, `p.*` ([wiki](https://github.com/Jai-Community/Jai-Community-Library/wiki/Getting-Started)) |
| Pointer arithmetic | not measured | none | not on `^T`; multi-pointer `[^]T` ([doc](https://odin-lang.org/docs/overview/#multi-pointers)) | `[*]T ± n` ([doc](https://ziglang.org/documentation/0.15.2/#Pointers)) | as in C ([spec](https://c3-lang.org/implementation-details/specification/#pointer-types)) | none; `unsafe.Add` ([FAQ](https://go.dev/doc/faq#no_pointer_arithmetic), [spec](https://go.dev/ref/spec#Package_unsafe)) | allowed |
| Arrays and slices | not measured | `a[i]` only | `[N]T`, `[]T` ([doc](https://odin-lang.org/docs/overview/#fixed-arrays)) | `[N]T`, `[]T`, no decay ([doc](https://ziglang.org/documentation/0.15.2/#Type-Coercion-Slices-Arrays-and-Pointers)) | `int[4]`, `int[]`, no decay ([spec](https://c3-lang.org/implementation-details/specification/#array-types)) | `[N]T`, `[]T` ([spec](https://go.dev/ref/spec#Array_types)) | `[N]T`, `[]T`, `[..]T` |
| `struct` | 13.2 (includes uses as a type) | keyword only | `N :: struct {…}` ([doc](https://odin-lang.org/docs/overview/#structs)) | `const N = struct {…}`; `extern struct` for C layout ([doc](https://ziglang.org/documentation/0.15.2/#extern-struct)) | `struct N {…}` ([doc](https://c3-lang.org/c-to-c3/a-guide-for-c-programmers/#struct-enum-and-union-declarations)) | `type N struct {…}` ([spec](https://go.dev/ref/spec#Struct_types)) | `N :: struct {…}` |
| `union` | 0.3 | keyword only | tagged `union`; C-style `#raw_union` ([doc](https://odin-lang.org/docs/overview/#unions)) | `extern union`, tagged `union(enum)` ([doc](https://ziglang.org/documentation/0.15.2/#Tagged-union)) | C union, no tagged union ([spec](https://c3-lang.org/implementation-details/specification/#union-types)) | none ([FAQ](https://go.dev/doc/faq#unions)) | `union` |
| `enum` | 1.7 | keyword only | `enum u8 {…}`; explicit conversion ([doc](https://odin-lang.org/docs/overview/#enumerations)) | `enum(u8)`, `@intFromEnum` ([doc](https://ziglang.org/documentation/0.15.2/#enum)) | ordinals only; `constset` for valued ([doc](https://c3-lang.org/language-common/enums/)) | `iota` constants ([spec](https://go.dev/ref/spec#Iota)) | `enum s16 {…}` |
| Bitfields | about 0.2 | none | `bit_field` ([doc](https://odin-lang.org/docs/overview/#bit-fields)) | `packed struct` with `u3` fields ([doc](https://ziglang.org/documentation/0.15.2/#packed-struct)) | `bitstruct` ([spec](https://c3-lang.org/implementation-details/specification/#bitstruct-types)) | none ([cgo](https://pkg.go.dev/cmd/cgo)) | none |
| Function pointer types | 2.5 | none (`fn` literals exist) | `proc(x: int) -> bool` ([doc](https://odin-lang.org/docs/overview/#procedure-type)) | `*const fn (i8) i8` ([doc](https://ziglang.org/documentation/0.15.2/#Functions)) | `alias Cb = fn void(int)` ([spec](https://c3-lang.org/implementation-details/specification/#function-types-and-function-pointers)) | `func(int) int` ([spec](https://go.dev/ref/spec#Function_types)) | `(int,int)->int` |

### 4.3 Control flow

| Construct | C use | chibi-v2 today | Odin | Zig 0.15.2 | C3 | Go | Jai (unverified) |
|---|---|---|---|---|---|---|---|
| `switch`, fallthrough | `switch` 2.4, `case` 18.2 | keywords only | implicit break, `fallthrough` ([doc](https://odin-lang.org/docs/overview/#switch-statement)) | no fallthrough; ranges; exhaustive; `continue :sw v` re-dispatch ([doc](https://ziglang.org/documentation/0.15.2/#Labeled-switch)) | implicit break; empty cases fall through; `nextcase` ([spec](https://c3-lang.org/implementation-details/specification/#switch-statement)) | implicit break, `fallthrough` ([spec](https://go.dev/ref/spec#Expression_switches)) | `if x == { case …; #through; }` |
| `goto`, labels | `goto` 2.7 | keyword only | none; labeled `break`/`continue` ([grammar](https://odin-lang.org/spec/grammar/#keywords)) | none; labeled blocks and loops ([doc](https://ziglang.org/documentation/0.15.2/#Blocks)) | removed; labeled `break`/`nextcase`, `defer` ([doc](https://c3-lang.org/c-to-c3/a-guide-for-c-programmers/#goto-removed)) | `goto`, cannot skip declarations or enter blocks ([spec](https://go.dev/ref/spec#Goto_statements)) | none |
| `do`/`while` | 0.5 | none | none ([FAQ](https://odin-lang.org/docs/faq/#why-does-odin-not-have-a-while-statement)) | none ([doc](https://ziglang.org/documentation/0.15.2/#while)) | `do {…} while (c);` ([spec](https://c3-lang.org/implementation-details/specification/#while-and-do-statements)) | none ([spec](https://go.dev/ref/spec#For_statements)) | not documented |
| C `for (init; cond; post)` | 13.5 | keyword only | `for i := 0; i < n; i += 1` ([doc](https://odin-lang.org/docs/overview/#basic-for-loop)) | none; `while (c) : (i += 1)` ([doc](https://ziglang.org/documentation/0.15.2/#while)) | C form ([spec](https://c3-lang.org/implementation-details/specification/#for-statement)) | `for i := 0; i < n; i++` ([spec](https://go.dev/ref/spec#For_clause)) | `for 0..n` |

Go's compiler had 1,032 `goto` statements, and the translator handled only the trivial ones. Zig drops every function that contains one. The mix has about 2,800 `goto`s, more than half of them in flecs (703) and SQLite (871).

### 4.4 Operators and expressions

| Construct | C use | chibi-v2 today | Odin | Zig 0.15.2 | C3 | Go | Jai (unverified) |
|---|---|---|---|---|---|---|---|
| `++`, `--` | 19.4 | none | none ([FAQ](https://odin-lang.org/docs/faq/#why-are-there-no--or----operators)) | none; `++` is concatenation ([doc](https://ziglang.org/documentation/0.15.2/#Table-of-Operators)) | prefix and postfix expressions ([spec](https://c3-lang.org/implementation-details/specification/#postfix-and-)) | statements only ([spec](https://go.dev/ref/spec#IncDec_statements)) | none |
| Ternary `?:` | 8.0 | `?` lexed only | `x if c else y` and `c ? x : y` ([doc](https://odin-lang.org/docs/overview/#ternary-operators)) | `if (c) a else b` ([doc](https://ziglang.org/documentation/0.15.2/#if)) | `c ? a : b` ([spec](https://c3-lang.org/implementation-details/specification/#ternary-expression)) | none ([FAQ](https://go.dev/doc/faq#Does_Go_have_a_ternary_form)) | `ifx c a else b` |
| Comma operator | not measured | none | none ([grammar](https://odin-lang.org/spec/grammar/)) | none ([doc](https://ziglang.org/documentation/0.15.2/#Table-of-Operators)) | removed ([FAQ](https://c3-lang.org/faq/allfeatures/)) | none; tuple assignment ([spec](https://go.dev/ref/spec#Assignment_statements)) | not documented |
| Assignment as an expression | 1.4 inside `if`/`while` (heuristic) | statement only; chaining rejected | statement ([grammar](https://odin-lang.org/spec/grammar/)) | statement ([grammar](https://ziglang.org/documentation/0.15.2/#Grammar)) | expression ([spec](https://c3-lang.org/implementation-details/specification/#assignment-expressions)) | statement ([spec](https://go.dev/ref/spec#Assignment_statements)) | not documented |
| Casts and implicit conversions | not measured | `cast`, `as` lexed only | `T(x)`, `cast(T)x`, `transmute`; no implicit numeric conversion ([doc](https://odin-lang.org/docs/overview/#type-conversion)) | `@intCast`, `@as`, `@bitCast`; only safe widening is implicit ([doc](https://ziglang.org/documentation/0.15.2/#Explicit-Casts)) | `(T)x`; C-like promotion ([doc](https://c3-lang.org/language-rules/conversion/)) | `T(x)`; operands must be identical types ([spec](https://go.dev/ref/spec#Conversions)) | `cast(T) x`, `xx x` |
| `sizeof`, `alignof`, `offsetof` | `sizeof` 9.3 | keywords only | `size_of(T)` … ([doc](https://odin-lang.org/docs/overview/#built-in-procedures)) | `@sizeOf`, `@offsetOf` ([doc](https://ziglang.org/documentation/0.15.2/#sizeOf)) | `T::size`, `@sizeof(e)` ([doc](https://c3-lang.org/language-overview/types/)) | `unsafe.Sizeof` … ([spec](https://go.dev/ref/spec#Package_unsafe)) | `size_of(T)` |
| Varargs: define, and call C's | `...` 0.3; `printf`-style calls not measured | `..` lexed only | `..int`; `#c_vararg` for C ([doc](https://odin-lang.org/docs/overview/#implementing-c-variadic-procedures)) | no native varargs; `extern` `...` ([doc](https://ziglang.org/documentation/0.15.2/#C-Variadic-Functions)) | `int... args`; C `...` in `extern` only ([spec](https://c3-lang.org/implementation-details/specification/#parameters)) | `...T`; cgo cannot call C varargs ([spec](https://go.dev/ref/spec#Function_types), [cgo](https://pkg.go.dev/cmd/cgo)) | `..int` |
| Bitwise and shift precedence | n/a | decided, Go-like (section 4.1) | like Go ([doc](https://odin-lang.org/docs/overview/#operator-precedence)) | shifts below `+`, bitwise above comparisons ([doc](https://ziglang.org/documentation/0.15.2/#Precedence)) | own table; mixed bitwise operators need parentheses ([doc](https://c3-lang.org/language-rules/precedence/)) | `& << >>` with `*`, `\| ^` with `+` ([spec](https://go.dev/ref/spec#Operator_precedence)) | not found |

### 4.5 Literals and initializers

| Construct | C use | chibi-v2 today | Odin | Zig 0.15.2 | C3 | Go | Jai (unverified) |
|---|---|---|---|---|---|---|---|
| Character literals, `'\0'` | 7.3 | none | `'a'` is a rune; `\0` is rejected, since octal escapes need 3 digits ([tokenizer.cpp L646-648](https://github.com/odin-lang/Odin/blob/85afcb386c389aafcec4f2e33d27ff8b42579822/src/tokenizer.cpp#L646-L648)) | `'a'` is `comptime_int`; no `\0`, use `\x00` ([doc](https://ziglang.org/documentation/0.15.2/#Escape-Sequences)) | `'a'` is `char`; `\0` accepted ([spec](https://c3-lang.org/implementation-details/specification/#character-literals)) | rune; `\0` invalid, `\x00` valid ([spec](https://go.dev/ref/spec#Rune_literals)) | `#char "1"` |
| Hex, octal, binary, suffixes | hex 33.8, suffixed 14.6 | decimal only | `0x 0o 0b _`; no suffixes ([doc](https://odin-lang.org/docs/overview/#numbers)) | `0x 0o 0b _`; no suffixes ([doc](https://ziglang.org/documentation/0.15.2/#Integer-Literals)) | `0x 0o 0b _`; `u`, `l`, `f` suffixes ([spec](https://c3-lang.org/implementation-details/specification/#integer-literals)) | `0x 0o 0b _`, legacy `0600` ([spec](https://go.dev/ref/spec#Integer_literals)) | `0x 0b _` |
| Compound literal `(T){…}` | not measured | none | `T{1, 4}`, inferred `{3, 4}` ([doc](https://odin-lang.org/docs/overview/#struct-literals)) | `T{ .x = 1 }`, `.{…}` ([doc](https://ziglang.org/documentation/0.15.2/#struct)) | `(Foo){…}` or inferred `{…}` ([doc](https://c3-lang.org/language-fundamentals/expressions/#compound-literals)) | `T{…}` ([spec](https://go.dev/ref/spec#Composite_literals)) | `Vec3.{…}` |
| Designated initializers | 12.1 | `.x = v` in call arguments only (`parse_args`) | `{z = 1}`; array keys and ranges ([doc](https://odin-lang.org/docs/overview/#fixed-arrays)) | `.{ .x = 1 }`; no array index designators found ([doc](https://ziglang.org/documentation/0.15.2/#struct)) | `.x =`, `[3] =`, `[0..128] =` ([spec](https://c3-lang.org/implementation-details/specification/#compound-literals)) | `field: v`, `index: v` ([spec](https://go.dev/ref/spec#Composite_literals)) | `.{x = 1}` |
| Null pointer | not measured | `nil` (decided) | `nil` | `null`, optionals only ([doc](https://ziglang.org/documentation/0.15.2/#Optional-Pointers)) | `null` | `nil` | `null` |

### 4.6 Linkage and the preprocessor

| Construct | C use | chibi-v2 today | Odin | Zig 0.15.2 | C3 | Go | Jai (unverified) |
|---|---|---|---|---|---|---|---|
| `static` (file scope, static locals), `extern` | `static` 26.5, `extern` 3.4 | `pub` lexed only | public by default; `@(private="file")`, `@(static)`, `foreign import` ([doc](https://odin-lang.org/docs/attributes/#static), [foreign](https://odin-lang.org/docs/overview/#foreign-system)) | private unless `pub`; static locals via a local struct; `extern fn` ([doc](https://ziglang.org/documentation/0.15.2/#Static-Local-Variables)) | `@local`, `@private`, `static` locals, `extern fn` ([spec](https://c3-lang.org/implementation-details/specification/#visibility)) | capitalized names export; cgo ([spec](https://go.dev/ref/spec#Exported_identifiers)) | `#scope_file`, `#foreign` |
| `const`, `volatile` qualifiers | `const` 47.2, `volatile` 0.4 | `const` is a binding keyword | none; `volatile_load` ([doc](https://odin-lang.org/docs/overview/#intrinsics)) | `*const T`, `*volatile T` ([doc](https://ziglang.org/documentation/0.15.2/#volatile)) | none; `@volatile_load` ([doc](https://c3-lang.org/language-overview/types/)) | none ([spec](https://go.dev/ref/spec#Constant_declarations)) | none |
| `#if` | 17.3 directives | none | `when`, build tags ([doc](https://odin-lang.org/docs/overview/#build-tags)) | `if` on compile-time values ([doc](https://ziglang.org/documentation/0.15.2/#Introducing-the-Compile-Time-Concept)) | `$if`, `@feat` ([spec](https://c3-lang.org/implementation-details/specification/#top-level-conditional-compilation)) | `//go:build` ([cmd/go](https://pkg.go.dev/cmd/go#hdr-Build_constraints)) | `#if` |
| `#include` | about 13,800 directives in the mix | `package`, `import` lexed only | see [packages.md](packages.md) | | | | |

### 4.7 Also needed, not compared here

`typedef` (type aliases; the keyword `type` is lexed); several declarators on one line (`int a, b;`); anonymous struct and union members; flexible array members; `static_assert`; `inline`; attributes (`__attribute__`, `[[maybe_unused]]`); GNU builtins (`__builtin_ctzll`, `__builtin_add_overflow`); SIMD vector types (`vector_size`) and NEON/SSE intrinsics; GNU range designators (`[0 ... 255] =`, used in `src/lexer.c`); statement expressions; adjacent string literals that C joins; C escapes in strings; `/* */` comments; unary `+`; labels; the entry point (`main`, `argc`, `argv`).

Five semantic rules decide whether translated code computes the same thing: integer promotion, signed overflow, `char` signedness, `long` width (LP64 versus LLP64), and C struct layout for interop.

### 4.8 Working before the decisions

While a construct is undecided, the translator can emit it as a call to a reserved name. Examples: `c_ternary(c, a, b)`, `c_cast(T, x)`, `c_sizeof(T)`, `c_switch(…)`. These already parse. The corpus then lexes and parses today, and each decision replaces one placeholder rule. The cost is a known bias: calls have different token counts than the final syntax. Record how many placeholder calls each codebase has.

---

## 5. Representativeness limits

### 5.1 What translated C lacks

Translated C has no comptime, generics or reflection. It also lacks the other features the owner expects to shape chibi code: tasks and coroutines, `defer` (C uses `goto cleanup`), slices (C passes a pointer and a length), tagged unions (C pairs an enum with a union), packages, named arguments and closures. These features cost most in the checker, the compile-time interpreter and generic instantiation (H5, H9, H10), which are exactly the phases a C corpus cannot exercise.

### 5.2 The C already imitates them

| Feature in chibi | What the C does instead | Example |
|---|---|---|
| Compile-time tables | X-macros | `CHIBI_TOKENS`; [tcctok.h](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tcctok.h#L3-L12) |
| Compile-time functions | Macro-unrolled tables | `bit_positions` via `BIT_ROW64` in `src/lexer.c` |
| Generics | Type-generic macros | [stb_ds.h L543](https://github.com/nothings/stb/blob/4e9b368bfc071e97206ea5c6adba68dd0cbf8897/stb_ds.h#L543) (`arrput`); Lua's `cast` |
| Reflection | Hand-written field-offset tables | Quake 2 save games ([g_save.c L27-28](https://github.com/id-Software/Quake-2/blob/372afde46e7defc9dd2d719a1732b8ace1fa096e/game/g_save.c#L27-L28)); flecs `ECS_STRUCT` ([meta_c.h L46](https://github.com/SanderMertens/flecs/blob/68790666a02b1dc679cdc9bc2d19fc285ba4fe1b/include/flecs/addons/meta_c.h#L46)) |
| Tasks and coroutines | Coroutine macros on top of a C coroutine library | Taisei `TASK`, `INVOKE_TASK` ([taskdsl.h](https://github.com/taisei-project/taisei/blob/baf029b05b625f5543543b8d96ecf38ae8e7f1b1/src/coroutine/taskdsl.h#L177-L227)) |
| `defer` | `goto` to a cleanup label | flecs (703 `goto`s), SQLite (871) |
| Slices | Pointer, length and capacity fields | c2go's `slice` rule ([config.go](https://github.com/rsc/c2go/blob/520c22818a0844fb1aefa35f8ff24e3c1fc8f66a/config.go#L95-L219)) |

### 5.3 Proposals

1. **Count the imitations.** The translator's macro classifier already sees them. Report per 1,000 lines: X-macro tables, unrolled tables, type-generic macros, field-offset tables, coroutine macros, cleanup `goto`s and pointer-length pairs. These counts estimate how often idiomatic chibi would use each feature.
2. **Lift them, where a rule is cheap and opt-in per codebase.** X-macro tables become compile-time arrays (section 2.2), cleanup `goto`s become `defer`, and named pointer-length pairs become slices. The result is C-shaped code with idiomatic islands. Report lifted lines separately.
3. **Write an idiomatic seed.** Hand-port at least 20,000 lines, written the way the owner wants chibi used, with comptime, generics, reflection and tasks. Good sources:
   - Taisei's stage scripts (game logic written as tasks);
   - a handful of raylib examples;
   - one Box2D or flecs module;
   - the old Chibi compiler's own Chibi code. Its `tools/compiler` has 88,716 lines at 50.4 bytes per line (measured), uses tagged unions, slices and packages, and could be translated from old Chibi syntax with a much simpler tool.
4. **Calibrate the generator.** Extend `test/programs.c` to emit comptime, generic and reflection constructs at the densities measured in the seed.

### 5.4 Measuring closeness

- **Shape vector** per corpus:
  - bytes per line and tokens per line;
  - the identifier length histogram;
  - the node kind histogram per 1,000 lines;
  - the nesting depth histogram;
  - statements per function;
  - comment and blank shares.
- **Distance:** compare node kind histograms with the Jensen–Shannon divergence. It is 0 when two distributions match and 1 when they share nothing. Also list the kinds whose rates differ by more than 2×.
- **The deciding measure:** cycles per line in each phase (lexing, parsing, checking, comptime, backend), on the corpus against the seed. This is H1's test, extended to every phase (T6).

---

## 6. Self-hosting path

### 6.1 Can chibi-v2 do what Go did?

Yes, and the job is smaller. chibi-v2's `src/` is 3,005 lines, against Go's 80,000+, and it has no `goto`, unions or bitfields (measured). The translator built for the corpus can translate it, once chibi has the constructs chibi-v2 uses:

- pointers and pointer arithmetic (the lexer walks `const char *` cursors);
- structs, enums and fixed arrays;
- `switch`;
- SIMD vector types and intrinsics (`src/simd.h`);
- bit-scan and overflow builtins;
- compile-time tables (the X-macros and `[0 ... 255]` designators).

Go's lessons apply directly. Edit the C until translation is mechanical, prove it with bit-identical output, keep the C compiler as the bootstrap, and expect slower code at first.

### 6.2 Conventions for chibi-v2's C, starting now

| Convention | Why | `src/` today (measured) |
|---|---|---|
| `++` and `--` only as whole statements | Of the languages compared, only C3 has them as expressions. c2go had to hoist them | about 27 lines (by grep) use them inside expressions, for example `emit(p, NODE_FIELD, p->pos++)` in `parse_postfix` |
| No assignment inside expressions | Go removed these from its C before translating | 0 |
| `?:` only if chibi gets an expression form | Go removed `?:` from its C | 19 |
| Macros only as (a) X-macro tables of one shape, (b) constants, (c) the attribute wrappers in `common.h` | The translator can lift exactly these shapes. Go turned `#define` constants into enums | 47 function-like macros. Most are X-macro expanders; `simd_eq` (uses `__typeof__`) and the table builders `BIT_ROW*`, `KEY_MASK`, `LOW_BYTES` and `MASK64*` need rules or rewrites |
| `#if` only around whole declarations | Lets the translator map `#if` to the chosen compile-time conditional | true today (SIMD variants, `CHIBI_LEXER_BULK`) |
| Every narrowing or sign change written as a cast; build with `-Wconversion -Wsign-conversion` | If chibi has no implicit numeric conversions (Odin, Go, Zig), translation must insert them. The warnings list them | not measured |
| Pointer arithmetic through `base + offset` with `u32` offsets | Maps to indexing whether or not chibi has many-item pointers | the lexer mixes both |
| No `goto`, unions, bitfields or `static` locals; no fallthrough except stacked `case` labels | These are the constructs every precedent handles worst | true today |
| One declarator per declaration | Mechanical mapping to `x: T` | mostly true |
| GNU extensions only behind wrappers in one header | One translation rule per wrapper. Range designators stay in table-building code | `src/simd.h` already does this; `__builtin_*` calls are spread across `lexer.c` |
| Comments on their own line or at the end of a line | c2go lost comments inside struct and enum bodies | true today |

The runtime cost of these conventions is zero. `p->pos++` as a separate statement compiles to the same instructions.

### 6.3 Bootstrap and checks

1. **CI translation:** translate `src/` on every change, and lex and parse the output with chibi-v2. Any drift from the conventions then shows up the day it happens.
2. **Differential test:** once chibi can run code, compare the bench `fingerprint()` of the C build with the translated build on the same generated programs. They must be identical. This is chibi-v2's `toolstash -cmp`.
3. **Bootstrap:** keep the C compiler as stage 0, like Go 1.4. The chibi compiler must stay buildable by it until a release of the chibi compiler takes over.
4. **The translator itself:** if it is written in C under the same conventions, it can later translate itself.

### 6.4 What chibi-v2's C needs, measured again on 2026-11-21

`src/` is now 20,055 lines in 24 files. Counted from clang 18's JSON AST of every `.c` file, with `-std=c23 -march=x86-64-v2`, over the declarations spelled in `src/` (each header's declarations once), with `vendor/` left out:

| Construct | Uses | Handling |
|---|---|---|
| `?:` written in the C | 645 | waits on sheet 4b ([syntax-options-effects.md](syntax-options-effects.md)); 152 more come from `assert` |
| String literals | 484 | waits on rank 10 ([adopt-or-settle.md](adopt-or-settle.md) 2.10); 313 more come from macros such as the name tables, and 294 from `assert` |
| Character literals | 239 | waits on sheet 4c; 28 more come from macros |
| `++` and `--` | 359 as statements, 160 as values | sheet 4a recommends none: the translator writes `x += 1` and hoists the 160 |
| C's three-part `for` | 245 | lowered to `while`, with `continue` leaving a labeled block so that the step still runs |
| `switch` | 31, with 496 `case` labels | stacked labels become one arm; a `break` that ends an arm disappears |
| `sizeof` and `alignof` | 163 and 13 | `size_of(T)` and `align_of(T)`, plain names as the builtin spelling card recommends |
| Pointer conversions | 46 written, 430 implicit, mostly from `void *` | `bitcast(T, x)`, decided on 2026-11-10 and built in step 3k.1 |
| Pointer and integer conversions | 25 | `bitcast`, as above |
| `NULL` | 68 lines | optional pointers `?*T` and `nil`, decided on 2026-11-10 and built in step 3e.1 |
| `assert` | 98 | a call to a translated function, or nothing when `NDEBUG` is defined |
| Attributes | `noinline` 106, `always_inline` 12, `unused` 10 | `noinline` and `always_inline` become `@noinline` and `@inline` (steps 3k.10 and 3t.11; the owner chose them on 2026-11-23), and `noinline` with `cold` becomes `@cold` (steps 3k.29 and 3t.18); `unused` is dropped |
| `static` locals | 12 | moved to package globals |
| Assignment or compound assignment used as a value, comma operator | 10, 6, 4 | hoisted |
| `do while` | 4 | `while true` that breaks at the end |
| A C variadic function and `va_arg` | 1 and 15 (`put_line` in `src/c_source.c`) | edited in the C, unless sheet 4e gives chibi variadic functions |
| `__int128` | 2 (`src/exact.c`) | edited in the C |
| An untagged union | 1 (`Exact` in `src/exact.h`) | unions are decided (sheet 1) and not built; editing the C is cheaper |
| A function pointer constant | 1 (`signal(SIGPIPE, SIG_IGN)` in `src/driver.c`) | edited in the C |
| Calls into C | 42 functions: stdio, memory, `qsort`, `mmap`, `dlopen`, `popen`, `strtod`, `XXH64` and others | `extern fn` declarations made from clang's prototypes; `FILE` becomes an empty struct used through pointers, and `struct stat` for `fstat` takes clang's layout |
| Builtins | the bit counts, `__builtin_expect`, the overflow checks, `__builtin_signbit`, `__builtin_isfinite` | chibi's builtins where they exist; `__builtin_expect` is dropped |
| SIMD | `src/simd.h` | translated with `CHIBI_SIMD_GENERIC`, whose vector code maps to chibi's vectors, `lookup` and `top_bits` |

Implicit integer conversions number 6,610 outside initializers and 131,648 inside them, almost all in generated tables; the translator writes `T(x)` wherever option K of the casts row doesn't convert by itself. `->` (4,360 uses) becomes `.`, which reads through a pointer. There is no `goto`, bitfield or `long double`.

The three waiting decisions decide how the translated code reads more than whether it can be written: without them, the translator would hoist 645 conditionals into `if` statements with temporaries, store 484 strings as constant byte arrays, and write 239 characters as numbers.

---

## 7. Recommended translator architecture

```
corpus.cfg per codebase: files, defines, include dirs, target, renames,
                         macro rules, lifts, excluded ranges
        │
        ▼
clang (pinned)  ──►  JSON AST per TU        (-Xclang -ast-dump=json -fsyntax-only)
                ──►  macro list per TU      (clang -E -dD: definitions with file:line)
source text     ──►  comments and line starts
        │
        ▼
1. Load      Stream the JSON into flat arrays: kind, type ID, first child, next sibling, source offset.
             Keep only declarations spelled in the TU's own file.
2. Own       Give each declaration exactly one output file. Drop prototypes. Deduplicate across TUs by (file, offset).
3. Macros    Classify each definition (constant, inline fn, generic, statement, table, flag, other).
             Re-sugar an expansion when its whole range is one AST node.
4. Lower     C semantics into chibi constructs: hoist side effects, explicit fallthrough, goto policy,
             re-parenthesize for chibi precedence, insert explicit conversions, placeholder calls for open decisions.
5. Emit      chibi text on the C line numbers, comments in place, plus a line map (source, hoisted, expanded, generated).
6. Check     Lex and parse the output with chibi-v2. Count lines, bytes and tokens. Later: type-check and differential runs.
```

- **One translator, specialized by configuration,** like c2go and cxgo. Do not aim for a general tool.
- **JSON AST rather than libclang** for code, because libclang hides implicit casts and designators. Use `-dD` text rather than libclang for macros, so there is one dependency: the `clang` binary.
- **Write it in C?** Writing it in C keeps one language, lets it share chibi-v2's arena and table style, and lets it translate itself later. This is the owner's call (question 4).
- **Stream the JSON.** Locations omit fields that repeat, so the reader must walk the dump in order.
- **Counters are part of the output:** per codebase, per construct and per rule. They feed T2–T4 and section 5.3.

---

## 8. Phased plan

| Phase | Work | Needs | Tests |
|---|---|---|---|
| 0 | Answer questions 1–4. Make `make bench` print bytes per line and tokens per line for the generated programs | owner | — |
| 1 | **Lexer corpus.** Rewrite the mix token by token: `/* */` to `//`; C escapes to chibi escapes; hex and suffixed literals to decimal; `&&`, `\|\|` and `->` to `and`, `or` and `.`; drop `;` and preprocessor lines. Run the lexer benchmark | nothing from the parser | first half of H1; T1 later |
| 2 | **Front-end spike on Lua 5.4** (31,708 lines): load, own, line map, macro list. Emit only what the parser accepts today, and placeholder calls for the rest | clang 23 | T2 and T4 on one codebase |
| 3 | **Core syntax decisions** (section 4), then grow the parser until it accepts all of Lua's output, then the whole mix | decisions | second half of H1; T2, T3, T4, T5 |
| 4 | **Semantic fidelity:** explicit conversions and C layout. The corpus type-checks as the checker grows | checker | H5 on real code |
| 5 | **Idioms:** count and lift imitations, write the seed, calibrate the generator | comptime, generics and reflection syntax | T6 |
| 6 | **Self-hosting:** translate `src/` in CI from now on; compare fingerprints once chibi runs code | backend | T7 |

Phase 1 needs no syntax decisions and gives real-code numbers for the lexer within days. Phase 6's CI step can start as soon as phase 2 exists, because translating and parsing `src/` needs no backend.

---

## 9. New hypotheses

These follow the format of [hypotheses.md](../hypotheses.md). The thresholds are proposals.

| ID | Claim | Needs | Status |
|---|---|---|---|
| T1 | A token-level rewrite of C predicts the lexer cost of full translation | phases 1 and 3 | untested |
| T2 | Translation keeps line counts honest | the translator | untested |
| T3 | clang on macOS covers at least 1M lines of the mix | the translator | untested |
| T4 | Most macro uses keep their names | the translator | untested |
| T5 | Game code and library code cost the same per byte to lex and parse | the corpus | untested |
| T6 | Translated C predicts idiomatic chibi in every phase | the seed and the checker | untested |
| T7 | chibi-v2's own C translates mechanically | a backend | untested |

### T1. A token-level rewrite of C predicts the lexer cost of full translation

- **Claim:** the lexer's cycles per byte on the token-level rewrite (phase 1) are within 5% of its cycles per byte on the fully translated corpus (phase 3), for the same C files.
- **Why we think so:** the lexer's cost depends on byte classes: identifier lengths, whitespace runs, comments, strings and numbers. The rewrite keeps all of these and changes only punctuation and a few keywords.
- **Test:** run the lexer benchmark on both corpora, built from the same selection.
- **Passes if:** the two results differ by at most 5%.
- **If it fails:** quote lexer results only from the translated corpus, and treat the phase 1 numbers as a rough guide.
- **Status:** untested.

### T2. Translation keeps line counts honest

- **Claim:** for every codebase, the translated corpus has 0.85 to 1.05 times as many physical lines as the selected C files. Lines produced by macro expansion, hoisting or generation are under 2% of the output.
- **Why we think so:** the translator keeps C's line structure and puts hoisted statements on their source line. Preprocessor lines (5.9% of the mix) and prototypes disappear, and little else changes.
- **Test:** sum the line map's flags per codebase.
- **Passes if:** every codebase's ratio is in range, and generated lines are under 2%.
- **If it fails:** find the construct that adds or removes the lines, and report lines/s on both the C count and the chibi count.
- **Status:** untested.

### T3. clang on macOS covers at least 1M lines of the mix

- **Claim:** translating each codebase in one macOS arm64 configuration covers at least 1,000,000 of the mix's 1,424,060 non-blank lines.
- **Why we think so:** portable codebases (SQLite, Lua, flecs, Box2D, stb, cglm) compile fully. Windows, Linux and Android backends, and Objective-C files, do not compile on macOS, so SDL3, miniaudio, sokol and raylib lose part of their code. The size of that loss is unknown.
- **Test:** per codebase, divide the translated lines by the selected lines.
- **Passes if:** the sum is at least 1M non-blank lines.
- **If it fails:**
  - add a Linux configuration through a sysroot;
  - add the headroom codebases (section 3.5);
  - or, for the parse-only corpus, translate the missing files with tree-sitter-c.
- **Status:** untested.

### T4. Most macro uses keep their names

- **Claim:** at least 90% of the uses of object-like macros, and at least 70% of the uses of function-like macros, appear in the output under the macro's name rather than expanded.
- **Why we think so:**
  - The mix defines twice as many object-like macros as function-like ones: 14,164 against 7,329 (measured).
  - c2rust rebuilds constants from their expansions.
  - Zig parses macro bodies as expressions.
- **Test:** the translator counts re-sugared and expanded uses.
- **Passes if:** both thresholds hold over the whole mix.
- **If it fails:** list the macro shapes that failed, and add rules for the most common. Count expanded lines as generated (T2).
- **Status:** untested.

### T5. Game code and library code cost the same per byte to lex and parse

- **Claim:** cycles per byte for lexing and parsing on the game subset (Taisei, TIC-80, raylib examples) are within 20% of those on the rest of the mix.
- **Why we think so:** the two subsets have similar line shapes (measured). The game subset has 30.9 bytes per line and 7.38 C tokens per code line; the rest has 35.8 and 7.06. The game subset has fewer comment lines (10.6% against 16.6%) and more blank lines (18.6% against 11.7%).
- **Test:** run the benchmark on the two subsets separately.
- **Passes if:** the difference is at most 20% for lexing and for parsing.
- **If it fails:**
  - weight the corpus toward game code;
  - report each subset separately;
  - add the GPL games as a local, uncommitted check set.
- **Status:** untested.

### T6. Translated C predicts idiomatic chibi in every phase

- **Claim:** cycles per line on the translated corpus are within 20% of those on a hand-written idiomatic seed of at least 20,000 lines. This holds separately for lexing, parsing and checking.
- **Why it matters:** the seed uses comptime, generics and reflection, which translated C lacks. If costs differ beyond H1's threshold, tuning on translated C targets the wrong code.
- **Test:** write the seed (section 5.3) and measure each phase on both corpora.
- **Passes if:** each phase is within 20%.
- **If it fails:** benchmark a weighted mix of the seed and lifted C, and calibrate `test/programs.c` to the seed.
- **Status:** untested. It needs the seed and the checker.

### T7. chibi-v2's own C translates mechanically

- **Claim:** at least 99% of the lines in `src/` translate with no per-file rule. The translated lexer and parser produce the same bench fingerprints as the C build.
- **Why we think so:** Go aimed for 99%+ automatic translation of a much larger compiler and got it by editing the C first. chibi-v2's C is small and has no `goto`, unions or bitfields.
- **Test:** translate `src/` in CI. When chibi can run code, compare `fingerprint()` on the generated programs.
- **Passes if:** at least 99% of lines need no rule, and the fingerprints are identical.
- **If it fails:** change the C to follow section 6.2 rather than adding special cases to the translator.
- **Status:** untested. It needs a backend.

---

## 10. Questions for the owner

Ordered by what they block.

1. **What must the corpus do first:** only lex and parse (H1), or also type-check and run (H5, H6, self-hosting)? This decides the front end and the order of phases.
2. **Which line count is the metric?** For the mix, the candidates give 1.63M physical, 1.42M non-blank, 1.17M non-blank non-comment, or 1.06M code lines without preprocessor and data lines. Do generated and expanded lines count? This decides how much code the corpus needs.
3. **May translated third-party code live in the repository?** Permissive licenses only? tcc under LGPL? The GPL id games? If not, the corpus is generated locally from pinned commits and ignored by git.
   - **Answered 2026-11-09:** chibi-v2 is GPL-3.0-only, and the owner accepts GPL code in the corpus. The id Software games may be committed as test data.
4. **What language is the translator written in:** C, a scripting language, or eventually chibi? This blocks the first line of translator code.
5. **The core syntax decisions** (section 4), in this order:
   - pointer and array types, and member access through pointers;
   - `struct` and `enum` declarations;
   - `for`, then `switch` and fallthrough;
   - casts and `sizeof`;
   - `++`;
   - the ternary;
   - character and hex literals, and C escapes;
   - `goto`.

   Are placeholder calls acceptable meanwhile?
6. **Implicit numeric conversions:** C-like (C3), or explicit only (Odin, Go, Zig)? This decides the semantic translation and the `-Wconversion` convention.
7. **Adopt the section 6.2 conventions in chibi-v2's C now?** The cost is small today and grows with the code.
8. **Compile-time reach, generics and reflection** ([README](README.md) decisions 4–6). These block idiom lifting and the seed.
9. **Pin clang 23.1.1,** the Homebrew version installed here, for the JSON format?

---

## 11. Not verified

- **Jai:** every Jai cell is from the community wiki. No public primary source exists.
- **Extrapolated totals:**
  - SDL3, Taisei, Quake 2, Quake III and DOOM line totals are extrapolated by bytes from 1-in-3 or 1-in-2 samples.
  - The headroom from SQLite's extensions is estimated from blob sizes.
- **Approximate counts:**
  - Token counts come from a regex tokenizer, not a C front end.
  - Bitfield, label, designator and assignment-in-condition counts are heuristics.
  - Casts, comma operators, pointer arithmetic and `printf`-style calls were not measured.
- **Not measured because nothing could be run:**
  - clang's coverage of each codebase on macOS;
  - JSON dump sizes;
  - the bytes per line of the generated programs.
- **Translated line shape:** the bytes and tokens per line of translated chibi are estimates from C token counts, not measurements.
- **Licenses:** the license column restates the license files. It is not legal advice.
- **Inferences, not stated by the projects:**
  - Zig's handling of X-macros, inferred from translate-c's code;
  - c2nim dropping fallthrough, inferred from its code;
  - Aro having no C API.
- **Missing numbers:**
  - c2rust's ports publish no line counts or measured cleanup effort.
  - cxgo's use on real games is unconfirmed.
