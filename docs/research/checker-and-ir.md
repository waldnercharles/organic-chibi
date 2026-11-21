# Checker and IR design

Written 2026-11-12 by the research thread for the owner, before any checker code (the Process rows of [../decisions.md](../decisions.md)). It covers:
- what the checker reads and produces (sections 1–2);
- how checking runs on many threads (3);
- types, constants and literals (4);
- the typed IR (5);
- two speed features for interpreters, guaranteed tail calls and computed `goto` (6);
- what the IR must leave room for (7);
- the first implementation step (8);
- the questions to answer (9).

Each section shows what other compilers do, lays out the options with their costs, and recommends one. Numbers measured on the owner's M4 Pro say so. Other numbers are estimates or come from the cited source.

Sources are primary and pinned:

| Sections | Pins |
|---|---|
| 1–4 | Carbon `ca18985`, Zig 0.17.1, Odin `85afcb3`, Go 1.27.4, typescript-go `89d5d5b`, c3c `ff06cba` |
| 5 and 7 | Zig 0.17.1 (`be248cc1`), TPDE `efe4cc77` and arXiv 2505.22610v1, V8 `ec65c608`, Wasmtime 50.0.1, LLVM 23.1.5, Slang v2026.22, wgpu 30.0.1, Roslyn `122e185`, Rust 1.99.1 |
| 6 | LLVM 23.1.5, the GCC 15.1 and 16.1 manuals, Microsoft Learn, Zig 0.17.1, Rust `29a5d066`, CPython 3.14.0 and 3.15.0, Wasmtime 50.0.1, Odin `dev-2026-11`, C3 0.8.6 |

Three research notes written for this sheet hold the line-level references: [checkers.md](checker-and-ir-sources/checkers.md), [ir.md](checker-and-ir-sources/ir.md) and [dispatch.md](checker-and-ir-sources/dispatch.md).

## 0. The pipeline

```
source files
  │  lex and parse, one job per file                  built: src/lexer.c, src/parser.c
  ▼
tokens + postorder nodes + list of top-level nodes    the list is a proposed parser addition (section 1)
  │  merge the per-thread name tables                 section 2.1
  ▼
one id per name across all files
  │  declare, one job per file                        section 3
  ▼
declarations in package scopes
  │  check: declaration jobs first, then one job      sections 3 and 4
  │  per function body; dependencies claimed on demand
  ▼
typed IR per function + diagnostics                   section 5
  │  error sets over the call graph                   decided (the Inference row); H13
  ▼
  │  lower, one job per function                      section 5
  ▼
bytecode for the interpreter | arm64 or x86-64 for development builds | C for release builds | SPIR-V for GPU code
```

Only the first step exists. Everything below it is design.

## 1. What the checker reads

For each file, the parser writes two arrays with one entry per node: a kind (1 byte) and a token index (4 bytes). Nodes are in *postorder*: children come before their parent, and nothing links a parent to its children. A list such as a block or a call starts with a *marker* node and ends with the list's own node. Here is a function and the nodes the parser writes for it, from a dump of the current parser:

```
fn sum(n: int) -> int {
    total := 0
    for i in 0..n {
        if i % 2 == 0 {
            total += i
        } else {
            total -= 1
        }
    }
    return total
}
```

```
FN_START fn
  NAME int  PARAM_TYPE n:
  NAME int  FN_RETURN ->
FN_BODY {                         ← the signature is complete: bind n, know the return type
BLOCK_START {
  INT 0  VAR_INIT total
  FOR_START for
    INT 0  NAME n  RANGE ..
  FOR_VALUE i                     ← the range is complete: bind i, open the loop
  BLOCK_START {
    IF_START if
      NAME i  INT 2  MOD %  INT 0  EQ ==
    IF_THEN {                     ← the condition is complete: open the first branch
    BLOCK_START {  NAME total  VALUE +=  NAME i  ADD_ASSIGN +=  BLOCK }
    ELSE else                     ← open the second branch
    BLOCK_START {  NAME total  VALUE -=  INT 1  SUB_ASSIGN -=  BLOCK }
    IF if                         ← the two branches join
  BLOCK }
  FOR in                          ← the loop ends
  NAME total  RETURN_VALUE return
BLOCK }
FN sum
```

**The markers arrive exactly when the checker needs context.** `FN_BODY` comes after the signature and before the body. `VALUE` comes after an assignment's left side and before its right side, so in `x: Point = .{ 1, 2 }` the declared type is known before the literal starts. `CALL_ARGS` comes after the callee and before the arguments, so each argument's parameter type is known before the argument. `IF_THEN`, `ELSE`, `WHILE_BODY` and `CASE_BODY` mark where control flow splits. `AND_LEFT`, `OR_LEFT`, `OR_ELSE_LEFT` and `CATCH_LEFT` come after the left operand of `and`, `or`, `??` and `catch`, which is where the right side's conditional block starts.

On the generated test programs, the parser writes 6.3 nodes per line, of which 1.2 are markers (measured on 400 generated files, 162,511 lines). Names are 29% of all nodes, calls 6% and field accesses 7%.

**What one pass costs before any type rule.** [experiments/check-floor](../../experiments/check-floor/README.md) walks those nodes once and does a checker's bookkeeping:
- an operand stack;
- popping children by the parser's arity tables;
- looking names up by id;
- restoring names when a block ends;
- writing a 13-byte IR record per node.

It does no type checking. It costs 24 cycles per node, or 149 cycles per line (measured, median of 5 runs). That is about what the parser costs (135 cycles per line) and 5% of H5's budget of 3,000 cycles per line for checking. It writes 5.1 IR records per line. The loop is untuned, so this is an upper bound for the bookkeeping, not a floor.

### Options

- **A. One forward pass over each function's nodes, with a value stack and no tree.** The checker reads each node once, pushes a typed value for each operand, pops operands when it reaches their operator, and writes IR as it goes. Carbon's checker works this way: it treats the parse nodes "as a byte code input that the check step interprets" ([check/README.md](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/docs/check/README.md#L57-L80); [fast-compilers.md](fast-compilers.md) section 3.4).
  - How Carbon does it: each stack entry is 8 bytes, a node and an id whose meaning follows from the node's kind (`check/node_stack.h`). A parent with a fixed number of children pops that many. A list's first node pushes a placeholder, and the parent pops back to it. Lists are moved to side stacks so that "we never need to search through the stack" (check/README.md). chibi's checker can get the same effect by recording the stack height at each start marker; the closing node then takes everything above that height.
  - Pros: no extra pass or memory, since the parser's output is the input; postorder is also evaluation order, so IR comes out in the order it runs; the markers give context where it is needed.
  - Cons: three things don't fit a single forward pass.
    - `return .{ 0, 0 }` has no marker before its value, so the literal doesn't know its type when it starts. The same holds for `a == .red` and for `.{ … }` passed to an overloaded function. Section 4.3 handles these with values whose type is filled in later.
    - A `defer` body runs at every exit from its block, so the checker reads its nodes again at each exit. It is the only place a node is read twice.
    - Finding the top-level declarations needs a scan of every node (see the parser additions below).
- **B. Build a tree with child links first.** Zig's AST node is 13 bytes: a tag, a main token and 8 bytes of child indices ([fast-compilers.md](fast-compilers.md) section 6). The checker can then look at a parent before its children, so a literal always knows its type from its parent.
  - Pros: any traversal order.
  - Cons: 8 or more bytes per node for the child links, and a slower walk. Measured on 2026-11-12 ([check-floor](../../experiments/check-floor/README.md), "Against a tree"):
    - The same bookkeeping, as a recursive walk over a tree, costs about 200 cycles per line, against 155 for one pass over the postorder nodes. Both write identical IR. The tree layout made no difference: linked children or Zig-like arrays of child indices.
    - Building the tree in its own pass costs another 123–129 cycles per line. A parser that builds the tree as it parses pays less, but still writes the child links.
    - In total, a tree costs 45–180 cycles per line more, 1.5–6% of H5's checking budget, before any type rule.
    - The walk is slower because it makes one call per node and must read a child's index before it can visit the child. The postorder pass is one flat loop over two arrays.
- **C. Option A, plus two small parser additions.**
  - *A list of top-level nodes.* Today the checker would have to replay the parser's stack over every node to find where each top-level declaration starts and ends, as `parser_dump` does. The parser knows this for free when it finishes a top-level statement. One `u32` per declaration is enough. Carbon's parser records similar ranges for the function bodies its checker must skip and come back to (`DeferredDefinition`, `parse/tree.h`).
  - *Builtin names with fixed ids.* `lexer_init` already interns the keywords first, so every lexer gives them ids 1 to `NKEYWORDS` ([src/lexer.c](../../src/lexer.c), `lexer_init`). Interning the builtin type names (`void`, `bool`, `int`, `i8` … `u64`, `f32`, `f64`, `uintptr`) and the predeclared functions right after them gives those names the same ids in every file. The checker then recognizes a builtin type by a range check, with no lookup. A predeclared function, such as `size_of` if sheets 3 and 8 are accepted, is found after locals and package declarations (section 2.2). `bitcast` is not one: it is a keyword whose first argument is a type (the Expressions row of decisions.md), so it already has a keyword id. This sheet first listed `len` among the predeclared functions, with a package's own `len` overloads as further candidates (parser thread, 2026-11-12). Section 4.4 now recommends a `.len` field instead.

**Why Zig and Odin build trees anyway** (the owner asked, 2026-11-12):
- **Zig passes context from parent to child.** AstGen walks the tree top-down with a `ResultInfo` that carries the expected type, so `.{ … }` knows its field types and `.red` its enum before they are lowered ([checkers.md](checker-and-ir-sources/checkers.md) 2.4). AstGen then writes ZIR, a flat list of instructions per file, and Sema reads ZIR in order. So Zig's type checking also reads a flat form; Zig builds that form through a tree. ZIR is cached on disk, so an unchanged file skips parsing and AstGen on the next build ([fast-compilers.md](fast-compilers.md) section 3.2). Sema reads the same ZIR body again for every generic instance and compile-time call. In chibi, the markers and provisional literals of section 4.3 replace `ResultInfo`.
- **Odin spends about 90% of its compile time in LLVM** ([newsletter, July 2023](https://odin-lang.org/news/newsletter-2023-07/)), so the shape of its front end was never its bottleneck. Like Go, it gives an untyped constant its final type by walking back down the expression's tree ([checkers.md](checker-and-ir-sources/checkers.md) 4.2), and that needs child links. Its checker became parallel only in October 2026 (PR #7701).
- **Carbon chose the postorder pass for speed.** It measures 985,000 lines per second through checking, on one core, on generated declarations (PR #4408).

**Recommendation: C.** The owner adopted it on 2026-11-12, and the parser thread built both additions the same day. They are parser API changes, so they went through the parser thread (the file-split row of decisions.md). What was built:
- **The list.** `parser_parse` has its own top-level loop and records the node count after each top-level statement in `Nodes.top_ends`, `Parser.top_count` entries in all. Statement i spans nodes `[top_ends[i-1], top_ends[i])`, with 0 before the first, and its root is node `top_ends[i] - 1`. The shared statement loop for blocks gained no branch. [check-floor](../../experiments/check-floor/README.md) confirms on 400 generated files that the entries match the roots its own postorder pass derives.
- **Builtin names.** `CHIBI_BUILTIN_NAMES(TYPE, ATTRIBUTE)` in `src/lexer.h` lists `void`, `bool`, `int`, `i8` … `i64`, `u8` … `u64`, `f32`, `f64`, `uintptr` and `error`, then the attribute name `tail`. Their ids (enum `BuiltinName`) start right after `NAME_LAST_KEYWORD`, which equals `NKEYWORDS`, and `lexer_init` and the reference lexer intern them from the same X-macro. A builtin type is `id - (NAME_LAST_KEYWORD + 1) < NBUILTIN_TYPES`. The keyword test stays `id <= NKEYWORDS`, so tokens pay nothing. The name merge of section 2.1 maps every id up to `NFIXED_NAMES` to itself. Measured by the parser thread: parser cycles within noise, lexer cycles 0.7% lower.
- **File scope.** The parser thread has asked the owner whether file scope accepts only declarations. Go and Zig reject `if`, `for` and bare expressions there while parsing. Until the owner decides, the list records every top-level statement, including `if`, `for` and bare expressions. With the rule, every entry is a declaration.

## 2. Names

### 2.1 One id for each name, across files

Each `Lexer` owns its own name table ([src/lexer.h](../../src/lexer.h), `Lexer.names`), and the bench runs one lexer per thread. So `count` in two files lexed by two threads gets two different ids. The checker compares names by id, so it needs one id per name across the whole program.

- **A. One table shared by all lexer threads.** Zig's InternPool does this for types and values: each thread appends to its own part, and the hash maps are split into shards with lock-free reads ([fast-compilers.md](fast-compilers.md) section 3.2).
  - Pros: ids are final as soon as a file is lexed.
  - Cons: interning is in the lexer's hottest path, and that path writes to the table even on a hit (`names_move_home` moves a found short name to its home slot). Sharing it needs atomics there. The cost is unmeasured, and the lexer belongs to the parser thread.
- **B. Keep a table per thread, then merge them after parsing.** One thread inserts every name of every table into a global table and records a map from local to global id. Each file's `tokens.ids` is then rewritten through its thread's map, in parallel.
  - Pros: the lexer doesn't change.
  - Cons: the merge is a serial step. Measured on 2026-11-12 with 14 per-thread tables from generated programs, 2.2M lines and 88,000–98,000 distinct names: 4.4–6.2 ms, or 40–58 ns per name per table, about 8–11 cycles per line on one thread. That is within H4's serial budget of 400 cycles per line. Real code repeats more names across files, so each table holds more of them, and the merge costs more (H15). If it grows, the merge splits into shards by hash bits, one thread per shard. The rewrite touches 2.7 identifier tokens per line on the generated programs, in parallel.
- **C. Keep per-thread ids and translate at every use.** No rewrite, but one more dependent load on every name the checker reads, and every later phase must remember to translate.

**Recommendation: B, with builtin names given fixed ids (section 1, option C).** The owner agreed on 2026-11-12: "Makes sense to me. And it's easy to revert."

### 2.2 Looking a name up

Inside a function body, about 1.8 names per line need a lookup (29% of 6.3 nodes). They resolve in this order: a local, a declaration in the package, an imported package, a builtin.

- **Locals: an array indexed by name id.** Each thread keeps one array with an entry per global name, holding the local's index plus one (0 means none). Declaring a local writes its entry. Leaving a scope restores the entries it changed, from an undo list. A lookup is one load, with no hashing and no string compare. Carbon's checker does the same, for "constant-time lookup of names, regardless of how many scopes" lie between (`check/lexical_lookup.h` at `ca18985`). At 100,000 names the array takes 400 KB per thread. Only the entries in use are touched.
- **Package declarations: a chain per name.** One global array, indexed by name id, points to every declaration with that name, in any package. A lookup walks that chain to the entry for the current package. Most names have one entry, so this is usually one load, and the chain also gives "`load` is declared in package `fmt`" errors for free. The declare step builds the chains in parallel, by atomically pushing onto each chain's head.
- **Builtins: a range check on the id,** given section 1's fixed ids. That settles a builtin type at once, and a predeclared function once no local or package declaration has its name.

What the others do:

| Compiler | Locals | Package or module names | Cost of one lookup |
|---|---|---|---|
| Carbon | array indexed by name id; each entry is a short stack of results | a hash map per scope, keyed by name id | one load for a local; one probe per enclosing class or namespace |
| Zig (AstGen) | a linked list of scopes, walked outward, comparing name ids; the walk continues past a match to reject shadowing | a hash map per namespace | one compare per enclosing scope |
| Odin | a hash map per scope, keyed by interned name | the same; package scopes become read-only after collection, so lookups take no lock | one probe per enclosing scope |
| Go (types2) | a `map[string]Object` per scope, walked outward | the same, with the predeclared names last | one string-hash probe per enclosing scope |
| C3 | an array of the live locals, scanned from the top | a hash table per unit, then per module | one compare per live local |

**Shadowing decides how much of this is needed.** Zig forbids it: "Identifiers are never allowed to "hide" other identifiers by using the same name" ([language reference 0.15.2](https://ziglang.org/documentation/0.15.2/), Shadowing), though separate scopes may reuse a name. Go allows it, even for predeclared names such as `int`, which live in the outermost "universe block": "An identifier declared in a block may be redeclared in an inner block" ([spec](https://go.dev/ref/spec#Declarations_and_scope)). Rust's `let` shadowing is idiomatic ([The Rust Book, 3.1](https://doc.rust-lang.org/book/ch03-01-variables-and-mutability.html#shadowing)).

- **A. Forbid shadowing, as Zig does.** A local can't reuse the name of a local, parameter, package declaration or builtin that is in scope.
  - Pros: a name means one thing within a function, which catches a class of bugs. Leaving a scope only clears entries, with no old values to restore. Lifting the rule later breaks no code.
  - Cons:
    - The Rust idiom `x := parse(x)` needs a new name.
    - UFCS (the Methods row) favours short package-level function names such as `len`, `area` and `scale`. Under A, no function in that package may have a local of the same name, so `len := v.len()` is an error. Zig users hit this all the time, because a Zig file is a struct whose functions and imports are all in scope: `fn format(self: Post, format: Format)` fails with "function parameter shadows declaration of 'format'" ([openmymind.net](https://www.openmymind.net/One-Method-Declaration-Two-Annoyances/)). Code generators that emit Zig rename their locals to avoid it.
    - C lets a local hide a file-level name, so the translator from C would have to rename every such local.
- **B. Allow shadowing, as Go and Rust do.**
  - Pros: familiar.
  - Cons: the undo list must keep old values, which costs little. Forbidding shadowing later breaks code.
- **C. Forbid a local from hiding another local or parameter of the same function, and allow it to reuse any other name** (D, C# and Java). Builtin type names such as `int`, `u8` and `error` stay reserved, as Zig reserves its primitive types.
  - D's spec: "local symbol declarations cannot shadow (hide) other local symbol declarations in the same function" ([statements](https://dlang.org/spec/statement.html), Scope Statements). C# reports the same case as error CS0136, and Java has the same rule. All three let a local reuse the name of a module-level or class-level declaration.
  - Pros:
    - It keeps A's protection inside a function, which is where the confusing case lives: an inner `x` that hides an outer `x`.
    - `len := v.len()` works, whatever the package declares.
    - It costs the checker what A costs. Locals never hide locals, so leaving a scope still only clears entries, and a package declaration lives in its own chain (above), not in the locals array.
    - Moving to B later breaks no code.
  - Cons: a local can hide a package-level variable or constant, so `SCALE` inside a function may not be the package's `SCALE`. A call to a function hidden this way fails with a type error rather than a shadowing error.

The parser thread first proposed letting locals reuse builtin names, and this sheet then recommended A with an exception for predeclared function names such as `len` (2026-11-12). Neither fixed the UFCS case above, and section 4.4 drops `len` as a predeclared function.

**Recommendation: C.** The owner agreed on 2026-11-12.

## 3. The order of checking, on many threads

### 3.1 What must be known before what

Declarations may appear in any order, so checking one declaration can need another first. What it needs is usually only part of the other declaration, so each declaration is checked in *phases*:

| Declaration | Phases, in order | Who needs the phase |
|---|---|---|
| `struct`, `enum`, `union` | identity: a type id exists, with no fields yet | `*Node` inside `Node`, or two structs that point to each other |
| | layout: field types, size and alignment | a field or local of this type, `size_of`, a literal |
| `fn` | signature: parameter and return types | every call |
| | body: the IR, and the return type when `->` is left out | a call to a private function that leaves out `->` (the Inference row) |
| `const` | value | any use |
| global variable | type, then initial value | any use |
| `import` | the package it names | `fmt.print` |

Splitting identity from layout is what lets `struct Node { next: ?*Node }` and two structs that point to each other check without a cycle. Only a real cycle, such as a struct that contains itself by value, is an error. Zig splits declarations the same way. Its units of analysis include a declaration's type (`nav_ty`), its value (`nav_val`), a type's layout (`type_layout`) and a function body (`func`), so code that uses a signature never waits for a body (`InternPool.zig` `AnalUnit`, Zig 0.17.1).

### 3.2 Options

- **A. Check on demand, claiming each phase.** The work list holds every function body, batched by file, on work-stealing queues (the parallel-strategy row of decisions.md). A job that needs another declaration's phase calls `need`:
  ```c
  static inline void
  need(Checker *c, u32 decl, u8 phase)
  {
      if (atomic_load_explicit(&c->decls[decl].done, memory_order_acquire) >= phase) {
          return;
      }
      need_slow(c, decl, phase);
  }
  ```
  The common case is one load and one compare. `need_slow` claims the declaration with a compare-and-swap and checks the phase itself, on its own stack. If another thread holds the claim, it waits for that thread. If this thread holds the claim further up its own stack, the declarations depend on each other, which is reported as a cycle. This is point 1 of [README.md](README.md) and how the Inference row expects return types to be inferred.
  - Precedent: Zig analyzes each unit on demand, on the stack of whoever needs it, but on one thread. Its semantic analysis is still serial in 0.17.1, and the June 2025 devlog calls parallelizing it "more distant future". Odin uses exactly this claim as the backstop of its parallel checker (option B).
  - Pros: no fixed order to get wrong; work starts at once; a declaration nobody needs early is checked by whichever job reaches it.
  - Cons: a thread that waits does nothing else until the claim is released. Section 3.3 covers detecting two threads that wait on each other.
- **B. Signatures first, then bodies.** Every declaration other than a function body is checked before any body.
  - Go checks one package on one thread. It visits package-level declarations in source order and checks each dependency first, on demand, marking declarations in progress to find cycles (`objDecl`). It queues every function body until all package-level declarations are done (`check.later`).
  - Odin's checker went parallel on 2026-10-02 (PR #7701, "Multithread The Entire Semantic Checker"). It builds a graph of which declarations each declaration's header names, skipping bodies, groups mutually dependent declarations with Tarjan's algorithm, and checks each group once the groups it needs are done. A thread claims a declaration with a compare-and-swap that stores its own number. A thread that finds another owner records what it waits for and follows that chain; if the chain comes back to itself, it reports "Illegal declaration cycle". Bodies are checked after all declarations. `odin check` on Odin's `examples/all` went from 820 ms to 337 ms on 32 threads.
  - Pros: bodies never wait on each other. A dependency graph keeps waits rare.
  - Cons: two decided features make one body need another. A private function without `->` needs a callee's body for its return type, and compile-time code makes a constant need a function's body. Both need option A's on-demand claim anyway. The graph costs an extra walk over every declaration's header.
- **C. A fixed number of checkers that duplicate shared work.** TypeScript 7 runs 4 checkers by default, each with its own caches and its own share of the files. It first assigned files round-robin, then by import-graph partitioning (PR #4313, August 2026), which users reported as "roughly 10% faster". The checkers "may end up duplicating some common work" ([7.0](https://devblogs.microsoft.com/typescript/announcing-typescript-7-0/)).
  - Pros: no waiting at all.
  - Cons: duplicated results must still end up identical. TypeScript sorts its types and symbols by content so every checker sees the same order. A struct's type id or a constant's value can't exist twice in chibi, so duplicates would need deduplication afterwards, which is option A's claim in another form.

**Which is fastest.** The owner wants the fastest option that loses no functionality (2026-11-12). All three options do the same checking work. They differ in time spent waiting and in work done twice:
- **B with a barrier** between declarations and bodies leaves threads idle while the last declarations finish. It also can't handle the two decided features above without A's claim. All it saves is `need`'s check, one load and one compare per use of another declaration, well under a cycle per line.
- **A alone** starts bodies at once. At the start, many bodies may need the same signature, such as a common allocator's, and every thread but one waits for it.
- **C** never waits but repeats work, and chibi would still have to deduplicate its results.

**Recommendation: A, run in B's order, with no barrier.** The owner adopted it on 2026-11-12. Every dependency goes through `need`, but each work list holds the declaration phases (layouts, constants, signatures) before the bodies:
- A thread that runs out of declaration jobs takes bodies at once.
- A body that needs a declaration still in progress checks it itself, or waits for the thread that holds it.
- Bodies then almost always find their signatures done, so `need` stays on its one-load path.
- Nothing is lost, because inferred return types and compile-time code still claim what they need.

This is Odin's design without its dependency graph, which costs an extra walk over every declaration's header. H14 measures the waits and decides whether the graph is worth adding; its fallback already describes a call graph built from the parser's call nodes. If waits do show up, a job that finds another thread's claim could also be set aside and resumed later instead of spinning. A postorder pass keeps its state in data (section 1): its position, its value stack and its undo list. Setting a job aside also needs the declarations it is checking on demand kept in that data, not in nested C calls. A recursive tree walk keeps that state in C stack frames, which can't be set aside.

### 3.3 Waiting, cycles and determinism

- **Waiting.** A thread that finds a claim held by another thread spins with a pause instruction until the phase is done. Odin sleeps on a futex instead, and doesn't run other work while it waits. Most phases are one declaration's signature or layout, so waits are short. A wait for a whole body happens only with inferred return types, which H14 estimates at under 3% of checking time.
- **Cycles across threads.** Before it waits, a thread records which declaration it waits for. It then follows the chain: the owner of that declaration, what the owner waits for, and so on. If the chain returns to itself, the declarations on the chain depend on each other. A thread only waits for something it needs, and only holds claims on what is on its own stack, so a cycle in the waits is always a real cycle between declarations, such as two private functions without `->` that call each other. The chain has at most as many links as there are threads.
- **Determinism.** Diagnostics are collected per thread and sorted by file and position before printing, so their order doesn't depend on timing. Sorting fixes the order of messages but not their content, and the others show where content leaks:
  - Odin's parallel checker produces identical code at any thread count, but its PR says "error messages may differ between runs".
  - Zig's dependency-loop report still carries "TODO: sort the dependency loops somehow" (`Zcu.zig`).
  - rustc's parallel front end is still not on by default. Its stabilization proposal (compiler-team MCP #1005) lists the leaks:
    - diagnostics in a different order;
    - internal ids in messages;
    - notes attached to whichever diagnostic came "first";
    - cycle reports that depend on which thread found the cycle.

  So chibi's messages never contain an internal id. A cycle is reported starting from its declaration that comes first in the source, naming all of its members. Anonymous things are numbered by source position. Type ids are given out in whatever order threads create types, so they differ between runs and must never reach output. Anything written out, such as type information in the binary or the order of symbols, is sorted by source identity (README point 6; hypothesis C5). The test suite should also run with shuffled job order and several thread counts, as Odin's `-internal-shuffle-global-entities` does.
- **No rule may depend on which use is checked first.** Zig can't parallelize its semantic analysis partly because a generic instance inherits the compile-time step budget of whichever caller instantiated it first (issue #22410). chibi's step budget for compile-time code (H9) must therefore belong to each evaluation, never to a caller.
- **What gets checked.** Whether code that nothing calls is checked is still open (README, decision 8). It changes only which jobs go on the list. Zig registers every declaration but analyzes only what its roots reach (`scanNamespace` and `ensureNavResolved`). Go and Odin check every declaration.

## 4. Types, constants and literals

### 4.1 Types

A type is a `u32` id, and 0 means no type. The builtin types have fixed ids, so `int` is a constant in the checker's code. Equal ids mean equal types, so the checker compares types with one compare.

- **Named types** (`struct`, `enum`, `union`) get their id from their declaration's identity phase, never by hashing.
- **Built types** (`*T`, `?T`, `[]T`, `[N]T`, `E!T` and function types) are *interned*: built once, then found by hashing their parts. Zig's InternPool does this across threads, with a per-thread append-only part and the thread's number in the top bits of the index, and hash maps split into shards with lock-free reads ([fast-compilers.md](fast-compilers.md) section 3.2). rustc uses 32 locked shards. Carbon canonicalizes types in its `CanonicalValueStore`, on one thread per file. Odin doesn't intern: it compares types by structure, recursively, which makes every identity test a walk. TypeScript 7 keeps private type tables per checker.
- **A shortcut for the common built types:** each type record keeps the ids of its pointer, optional and slice types once they exist, written once with a compare-and-swap. `*Point` is then one load after the first use, with no hashing. Odin caches pointer types on their element type the same way, so that threads agree on one `^T` (`cached_pointer`, `types.cpp`).

**Recommendation:** fixed ids for builtins, declaration-owned ids for named types, and Zig's scheme with the cached shortcuts for built types.

### 4.2 Untyped constants

Constants are untyped and exact at compile time, and take a type where they're used (the integer-types row). So `const MAX_HP := 100` is a number that fits a `u8` or an `f32`, and `1 << 40` is fine until it lands in an `i32`.

What the others store for such constants:

| Compiler | Integers | Floats | How small values stay fast |
|---|---|---|---|
| Go | 64 bits, else unlimited, rejected above 512 bits | exact fractions while both parts stay under 4,096 bits, then 512-bit floats | a 64-bit path that checks for overflow first |
| Zig | unlimited | `comptime_float` is an f128, not exact | values that fit 32 bits live inside the interned item, and arithmetic tries 64 bits first |
| Odin | always unlimited (libtommath) | exact fractions since 2026-09-29, then f64 above 65,536-bit parts | none, beyond allocating from an arena |
| C3 | a fixed 128 bits; a literal takes the first of `int`, `uint`, `long`, `ulong`, `int128`, `uint128` that holds it | the widest float type | no allocation at all |
| Carbon | small values inside the id, large ones in a table | — | most integers need no table access |

Sources: `go/constant/value.go` and `types2/const.go`, `InternPool.zig` and `Sema/arith.zig`, `exact_value.cpp` and `big_int.cpp`, c3c `compiler_internal.h` at `ff06cba`, Carbon `toolchain/base/int.h`.

Integers:
- **I1. Unlimited** (Zig, Odin; Go up to 512 bits). Exact for any program, but it needs a big-integer library, in C now and in chibi later.
- **I2. 128 bits, with an error beyond** (C3's arithmetic). chibi's widest integer type is 64 bits, so 128 bits hold every value of every chibi type, plus room for intermediate results such as `(1 << 100) >> 90`. clang's `__int128` does the arithmetic, with no library and no allocation.
- **I3. 64 bits checked, with unlimited values after an overflow** (Go, Zig). Fast for the common case, and exact, but it needs the library too.

Floats:
- **F1. Exact fractions up to a size limit** (Go, Odin). `0.1 + 0.2 == 0.3` holds at compile time, and every constant rounds once, into its final type. It needs the big-integer library, which I3 already brings.
- **F2. f128** (Zig). Apple's clang rejects `__float128` on arm64 ("not supported on this target") and its `long double` is an f64, so chibi would carry its own software f128, with a correctly rounded decimal parser for it. Zig carries the same in its compiler-rt. Rounded into an `f32` or `f64`, its results matched exact arithmetic in every case measured below, but comparisons between constants still behave like floats: `0.1 + 0.2 == 0.3` is false in f128 too.
- **F3. f64, keeping a plain literal's text.** A plain literal such as `0.1` is read straight into its final type, so it rounds once. A folded expression rounds at every step, as in C: `0.1 + 0.2` is `0.30000000000000004`, where F1 gives `0.3`.

Measured with [experiments/constant-rounding](../../experiments/constant-rounding/rounding.py): 100,000 random expressions of two or three literals shaped like game constants (integers, one to three decimal places, and long values such as π), with `+`, `-`, `*` and `/`. Compared with exact arithmetic rounded once:

| Folded in | Into `f32` | Into `f64` |
|---|---|---|
| f64 (F3) | 0 differ | 27,144 differ (27%) |
| f128 (F2) | 0 differ | 0 differ |

F3 can still differ in an `f32`, but only when its f64 result lands on an `f32` halfway point, about once in 2^29 values.

Cost: in every option, an integer within 64 bits and a float literal used directly take the fast path step 1 already has; the library runs only after a 64-bit overflow or to fold float constants. Of the 32,559 lines of Chibi tracked in the old repository, 233 hold a float literal and 2 do arithmetic between float literals, so folding is rare. A fraction operation on numbers of one to three 64-bit words is a few multiplications and a gcd, well under a microsecond, so even a thousand per million lines cost under a millisecond.

**Recommendation: I3 and F1,** capped as Go caps them: integers up to 512 bits, fractions while each part stays under 4,096 bits, and an error beyond either, which can relax later without breaking code. The decided rule says constant values are exact, and only F1 makes float constants exact. F1 and I3 share one big-integer library, about 600 to 900 lines of C, ported to chibi when the compiler hosts itself, as Go, Zig and Odin each carry their own. F2 needs that library and a software f128 as well, and is still not exact. I2 and F3 need no library, but F3 gives the C result for a quarter of folded `f64` constants, and moving from F3 to F1 later would change those values. The owner adopted I3 and F1 on 2026-11-13.

### 4.3 Literals that take their type from context

`.{ … }`, `.[ … ]`, `.red` and `nil` have no type of their own (the Literals row of the Syntax table). Untyped constants such as `100` take theirs from where they are used.

Compilers type such values in two ways:
- **From the parent, top-down.** Zig's AstGen walks a tree and hands each child its *result type*. So `.{ … }` inside `x: T = …` builds a `T` directly, and `.red` is looked up in the result type (`ResultInfo`, `structInitExpr` and `decl_literal` in `AstGen.zig`). Without a result type it builds an anonymous value that Sema converts later, so Zig uses both ways.
- **Bottom-up, converted where the value is used.**
  - Carbon gives a struct literal a structural type of its own. It converts the literal where it is used, building it straight into its destination (`Convert`, `convert.cpp`).
  - C3 makes an untyped list that its casting rules convert later.
  - Go and Odin record untyped expressions in a side table. When the final type is known they push it back down the expression's children (`updateExprType`), which needs child links.

In chibi's postorder stream the parent comes last, but section 1's markers give the context early in most places:

| Where the literal is | What gives its type | When the checker knows it |
|---|---|---|
| `x: Point = .{ 1, 2 }` | the declared type | at `VALUE`, before the literal |
| `p = .{ 1, 2 }` | the left side's type | at `VALUE` |
| `draw(.{ 1, 2 })` | the parameter's type | at `CALL_ARGS`, unless `draw` is overloaded |
| `Line.{ .from = .{ 0, 0 } }` | the field's type | at the field's name |
| `return .{ 0, 0 }` | the function's return type | at `RETURN_VALUE`, after the literal |
| `c == .red` | the other operand's type | at `==`, after the literal |
| `x := .{ 1, 2 }` | nothing | never, so it is an error |

- **A. Expected types only.** The checker keeps a stack of expected types, set at markers, and a literal without one is an error. Then `return .{ 0, 0 }` and `c == .red` need a written type or a new parser marker.
- **B. Provisional values only** (Carbon). Every literal becomes a provisional value, converted where it is used.
- **C. Both.** The checker uses the expected type when a marker gave one. Otherwise the literal stays provisional until the node that uses it fixes the type. A literal still provisional at the end of its statement is an error that suggests the typed form, `Point.{ … }`.

**Recommendation: C.** In the IR recommended in section 5, a provisional literal's elements are already instructions or constant operands. Fixing the type later appends one instruction that builds the value from them, plus any conversions, and patches nothing.

Cost: where a marker gives the type, C runs exactly A's code and writes the same instructions. Otherwise the literal is one stack entry until the operator that uses it, which already inspects its operand's type, so a provisional type is one more case in a check that runs anyway. B, by contrast, interns a structural type for every literal and writes a conversion that a later pass must remove. Predicted cost of C over A: under 1% of checking time; step 2 measures it. The owner adopted C on 2026-11-13.

One rule keeps this a single pass: a value that isn't constant never stays untyped past its operator. In `1 << n` with a runtime `n`, the `1` takes the expected type if a marker gave one, and `int` otherwise. Go instead types such a `1` from the context of the whole expression, which is the case that forces its walk back down the tree.

### 4.4 Lengths: a field or a predeclared function

No decision says how code gets the length of an array, slice or string. The integer row of decisions.md writes `len(a) - 1`, and says that `len` returns `i32` in GPU code. Those examples follow Go and Odin, and they led the parser thread and this sheet to treat `len` as a predeclared function. The owner asked what `len` is for (2026-11-12).

| Language | Spelling | What it is |
|---|---|---|
| Go, Odin | `len(a)` | a predeclared function |
| Hare | `len(a)` | a keyword ([specification](https://harelang.org/specification.pdf) 6.7.24) |
| Nim | `len(a)` or `a.len` | an ordinary procedure; UFCS gives both spellings |
| Zig, C3 | `a.len` | a field of every array and slice; strings are slices |
| Jai | `a.count` | a field of arrays and strings |
| Rust | `a.len()` | a method |

- **A. A read-only field, `a.len`, on arrays, slices and strings** (Zig, C3; Jai's `.count`). On a fixed array it is a constant.
  - Pros:
    - Reading a length is a load, and it looks like one.
    - No name enters any scope. Shadowing (section 2.2) needs no exception for it, and a package's own `len` functions never mix with a builtin.
    - User containers that keep a `len` field read the same as slices.
    - One spelling. A predeclared function plus UFCS gives two, `len(a)` and `a.len()`.
  - Cons:
    - A type that computes its length, such as a linked list, offers a function, so code writes `list.len()` but `slice.len`. Zig has the same split, with `map.count()` beside `slice.len`.
    - Odin's `len(Enum)`, the number of an enum's values, needs another spelling, which the reflection sheet (8) can give.
    - The integer row's wording changes from `len(a)` to `a.len`.
- **B. A predeclared function, `len(a)`** (Go, Odin, Hare).
  - Pros: one spelling for every built-in container. Packages may add overloads for their own types (the Methods row), so `len(list)` and `len(slice)` read alike.
  - Cons: `len` is a name in every scope. A local named `len` then needs a shadowing exception, and each call mixes the builtin with the package's own `len` overloads (section 1).

**Recommendation: A.** The field is read-only, and `a[..n]` shortens a slice; Zig instead lets code assign `s.len`. No predeclared functions are left until sheets 3 and 8 settle type queries such as `size_of`. `bitcast` stays a keyword (the Expressions row).

The owner agreed on 2026-11-12. They asked whether `list.len()` could just be `list.len` when the length is computed ahead of time, and whether `.len` is reserved for everything.
- **`.len` is not reserved.** `len` is only the name of the built-in field on arrays, slices and strings. A struct may declare its own `len` field, a package may declare `len` functions, and a local may be named `len`.
- **A type that stores its length reads like a slice.** A growable list that keeps a `len` field, updated on every append, is read as `list.len`.
- **Only a type that computes its length on request needs `()`.** Examples are a linked list that counts its nodes, a ring buffer that subtracts two indices, and a string's count of code points. The parentheses in `ring.len()` show that code runs.
- **A type can't offer both.** A dot call that could mean a field or a function is already an error (the Methods row).
- **Properties are the alternative, and this sheet doesn't recommend them.** C#, Swift and Kotlin let `x.len` run a function, which hides a call behind a field read. Zig, Odin, Jai, C3 and Go have no properties.

## 5. The typed IR

The checker writes one IR per function, and five consumers read it:
1. **The compile-time interpreter.** The IR is lowered to bytecode in H9's style, with byte offsets into a frame and the type in each opcode (the GPU output row of decisions.md says the bytecode is a lower level made from the IR).
2. **The development backend.** One pass per function to arm64 or x86-64, with calls through hot-reload slots (the development-builds row).
3. **The C backend** for release builds (the release-builds row).
4. **The GPU writers,** SPIR-V first. They need structured control flow with join points, typed values and field indices (the GPU output row).
5. **Coroutine lowering,** later (section 7).

All five read a function from start to end, once. None of them optimizes. Release builds leave optimization to clang, and an optimizing backend of chibi's own is a long-term goal that would build its own SSA form (the no-C-compiler row).

### 5.1 Three shapes, shown on `sum`

All three listings below are hand-written for this sheet. Locals live in numbered slots in A and B, and `end` is a hidden local that holds the loop's bound.

**A. Explicit operands** (Zig's AIR, Carbon's SemIR). Each instruction names its operands, either an earlier instruction (`%3`) or a constant (`0`). Control flow is nested in program order.

```
locals: n: int (parameter), total: int, i: int, end: int

%0  = load int n
      store int end, %0
      store int total, 0
      store int i, 0
      loop
%1  =   load int i
%2  =   load int end
%3  =   lt int %1, %2
        if %3
%4  =     load int i
%5  =     rem int %4, 2
%6  =     eq int %5, 0
          if %6
%7  =       load int total
%8  =       load int i
%9  =       add int %7, %8
            store int total, %9
          else
%10 =       load int total
%11 =       sub int %10, 1
            store int total, %11
          end
%12 =     load int i
%13 =     add int %12, 1
          store int i, %13
          repeat
        end
      end
%14 = load int total
      ret int %14
```

**B. Stack form** (WebAssembly). Operands are not named: each instruction pops its operands from a value stack and pushes its result. Constants are instructions.

```
locals: n: int (parameter), total: int, i: int, end: int

      get n  set end  const int 0  set total  const int 0  set i
      loop
        get i  get end  lt int
        if
          get i  const int 2  rem int  const int 0  eq int
          if
            get total  get i  add int  set total
          else
            get total  const int 1  sub int  set total
          end
          get i  const int 1  add int  set i
          repeat
        end
      end
      get total  ret
```

**C. SSA with basic blocks** (LLVM, Cranelift; TPDE reads this form). Locals disappear. Every value is assigned once, and a block's parameters receive the values that change between iterations.

```
entry(n):
    jump head(0, 0)
head(total, i):
    c = lt i, n
    br c, body, exit
body:
    r = rem i, 2
    e = eq r, 0
    br e, then, other
then:
    t1 = add total, i
    jump latch(t1)
other:
    t2 = sub total, 1
    jump latch(t2)
latch(t):
    i2 = add i, 1
    jump head(t, i2)
exit:
    ret total
```

How each shape suits the checker and the five consumers:

| | A. explicit operands | B. stack form | C. SSA with blocks |
|---|---|---|---|
| The checker writes it | Appends instructions. A constant whose type is settled later (section 4.3) is an operand, so nothing has to be patched. | Order matters, so a constant typed later must be written as a placeholder and patched, and a conversion can't be added after a value that is already below others on the stack. | Must build SSA while it checks, with block parameters at every join. |
| Interpreter bytecode | One bytecode instruction per IR instruction; each value gets a frame slot. | Stack positions become frame slots. | Block parameters become copies on each edge. |
| One-pass native backend | Keeps a location per value, freed at the value's one use. | Keeps a value stack. V8's Liftoff, SpiderMonkey's baseline compiler and Wasmtime's Winch compile WebAssembly this way, in one pass with no IR and no liveness pass. | Needs a liveness pass first. TPDE spends 12% of its time on layout, loops and liveness. Compiling WebAssembly through Cranelift's SSA IR, TPDE was 1.74× slower to compile than Winch, and 37% of its time went into translating WebAssembly into that IR. Winch compiles straight from WebAssembly. |
| C backend | One C temporary per value. | One C temporary per stack position. | One C variable per value, joined with `goto`. |
| GPU writers | Each value maps to one SPIR-V id. | The writer keeps a stack of SPIR-V ids. | The writer must rebuild the structure (shader-backends.md section 4). |
| Values live at a suspension point | Values defined before it and used after it. | The stack at that point. | Needs liveness. |
| Size of `sum` | 22 instructions, plus 8 that give structure (`loop`, `if`, `else`, `end`, `repeat`) | 28 instructions, plus the same 8 | 13 instructions in 7 blocks |

What the others chose:
- **Zig's AIR is shape A.** It is SSA for temporaries, `alloc`, `load` and `store` for variables, and no phis: a `block`'s value arrives through the `br` that leaves it. An operand is a `u32` whose top bit says whether it names an instruction or an interned constant. One AIR feeds Zig's C, x86_64, aarch64, LLVM and SPIR-V backends. A per-backend pass rewrites the instructions a backend can't emit into simpler ones (`Air/Legalize.zig`, 0.17.1).
- **WebAssembly is shape B.** Its structure was chosen for compact code and validation in one pass (Titzer, OOPSLA 2022). The single-pass compilers in the table translate it to code about 1.1–2× slower than their optimizing tiers.
- **Cranelift and TPDE read shape C.** Cranelift uses block parameters instead of phis; TPDE needs phis. TPDE's code is 1.14× faster than Winch's.
- **Odin has no IR of its own.** Its LLVM backend walks the checked syntax tree, and about 90% of its compile time is LLVM ([fast-compilers.md](fast-compilers.md) section 3.11).
- **V8 replaced its Sea of Nodes graph with a plain list of blocks (Turboshaft, 2025).** That halved compile time. The graph had about 3× more L1 data-cache misses, because nodes made during lowering sat far apart in memory ([V8 blog](https://v8.dev/blog/leaving-the-sea-of-nodes)).

Bytes per instruction, for scale:

| Format | Bytes |
|---|---|
| WebAssembly | 1 for the opcode, plus variable-length immediates; operands are implicit |
| Lua 5 bytecode | 4 |
| H9's bytecode | 8 |
| Zig AIR | 9, plus 4 for the instruction's place in a body list |
| this sheet's check-floor records | 13 |
| Carbon SemIR | 16 |
| Cranelift CLIF | 16, plus a 16-byte node for its place in the layout |

### 5.2 Choices every shape needs

1. **Structure in program order.** WebAssembly writes `block`, `loop`, `if`, `else` and `end` inline, in the order the code runs. Zig's AIR instead keeps each body as a list of instruction indices in a side array, which costs 4 more bytes per instruction. In ZIR, which has the same layout, those lists made up about 25% of the ZIR for `Sema.zig` (Zig issue 16039). Inline order lets every consumer read a function front to back with a small stack of open blocks, and the checker writes it in the order it reads nodes.
2. **Locals in numbered slots, not SSA values.**
   - The development backend keeps them at fixed frame offsets, which makes frames easy to map after a hot reload and easy for a debugger to read ([fast-compilers.md](fast-compilers.md) section 7).
   - clang promotes them to registers in release builds.
   - SPIR-V has function-local variables that its drivers promote the same way.
   - A coroutine's frame is its locals plus the values live at a suspension point.
3. **Every value is used exactly once.** A value needed twice, such as the address in `a[i] += 1`, goes into a hidden local. A consumer can then free a value's register or slot at its one use. That is the information Zig computes in a separate pass, its Liveness, at 4 bits per instruction ([fast-compilers.md](fast-compilers.md) section 3.2). Here no pass is needed. This is the *single-use rule* that section 7 refers to.
4. **Constants are operands, not instructions.** As in AIR, an operand is a `u32` whose top bit says whether it names an instruction or an interned constant. CLIF and Umbra IR spend an instruction on each constant.
5. **Every instruction records its result type,** so no consumer works types out again.
6. **Size.**
   - An instruction is a kind (1 byte), a type (4 bytes) and two operands (4 bytes each): 13 bytes, as in check-floor. Calls, literals and switches keep longer operand lists in a side array.
   - At 5.1 records per line, that is about 66 bytes per line (H17).
   - A function's IR lives in its job's arena and is dropped after lowering, unless compile-time code may call the function.
7. **Source positions in a side table,** only for instructions that can trap or call.

### 5.3 Recommendation

**A, written in program order, with the single-use rule.** The owner adopted it on 2026-11-12.
- It reads like Zig's AIR and is laid out like WebAssembly.
- Every consumer reads it in one pass without a liveness pass.
- The checker never patches it.

B would need patching wherever section 4.3 settles a type late, which is common in chibi because constants and literals take their types from context. C would make the checker build SSA and make the GPU writers rebuild the structure that the checker had. It would also buy optimizations that release builds already get from clang.

The evidence for it:
- **One nested IR already feeds every kind of backend chibi plans.** Zig's AIR, with no phis, drives its C, native, LLVM and SPIR-V backends.
- **Single-pass consumers don't need a liveness pass when values die at their use.** Liftoff frees registers by counting uses. V8 also tried a pre-pass over loops, and its source records that the pass brought no significant gain at baseline code quality (`liftoff-compiler.cc`).
- **Producing SSA has a measured cost:** when TPDE compiled WebAssembly through Cranelift's SSA IR, translating into that IR took 37% of the time (section 5.5).
- **Explicit operands keep random access** for the GPU writers, the C writer and the lowering to bytecode, which shape B gives up.

The one hard case for any structured IR is a coroutine that resumes in the middle of a loop, which section 7 covers.

### 5.4 Bytecode for the interpreter

The interpreter doesn't read the IR directly. The first time compile-time code calls a function, its IR is lowered to bytecode in H9's style, and the bytecode is cached:
- each local and each value gets a frame offset, and a value's slot is free again after its one use;
- structure becomes jumps (`JUMP`, `JUMP_UNLESS`, and `LOOP_IF` for back edges, where the step budget is checked);
- the type picks the opcode, as in `ADD_I64`.

Why bytecode with frame offsets rather than interpreting the IR or a stack form:
- **Register code runs fewer instructions.** Shi, Gregg, Beatty and Ertl turned the JVM's stack code into register code and executed 47% fewer VM instructions. The code was 25% larger, and it ran in 32% less time with switch dispatch and 26% less with threaded dispatch (VEE 2005).
- **Lua 5.0** made the same change; its `sum` loop went from 1.23 s to 0.54 s (J.UCS 2005).
- **wasmi** went from stack code to register code in 0.32 and ran CoreMark 1.7–2.2× faster. Its 2.0 moved to accumulator registers with tail-call dispatch, and ran about 2.2× faster than 1.0 on an M2 Pro (wasmi blog, 2024 and 2026).
- **Almost every fast WebAssembly interpreter rewrites its input.** Wizard is the exception, and builds a side table instead. Rewriting costs about 10× less time than a baseline compiler (Titzer, OOPSLA 2022).

Two later options:
- **Superinstructions.** H9's most common pair, `LT_I64` then `LOOP_IF`, could become one instruction. Ertl and Gregg removed nearly all dispatch mispredictions this way, combined with replicated instructions, for speedups up to 4.55× over threaded code (TOPLAS 2007).
- **Sharing the frame layout with the development backend.** V8's Sparkplug compiles Ignition bytecode and keeps the interpreter's frames unchanged, so debuggers and switches between tiers work as they are (V8 blog, 2021). If chibi's development backend used the bytecode's frame offsets, compile-time code could move between the interpreter and native code, and hot reload would map one layout instead of two.

### 5.5 What SSA buys, and who pays for it

The owner asked whether SSA has benefits over an AIR-like IR, why LLVM and Cranelift chose it, and what each approach costs (2026-11-12).

**The two differ only in locals.** Shape A is already SSA for temporaries: every instruction defines its result once, and nothing changes it afterwards. What A doesn't do is turn locals into SSA values.
- In listing A of section 5.1, `total` is one slot: `store` writes it and `load` reads it.
- In listing C, every assignment to `total` makes a new value (`t1`, `t2`). Where control flow joins, a block parameter (`latch(t)`) receives whichever value arrived. LLVM uses a phi instruction for the same job.

Turning slots into SSA values is called *promotion*. It needs to know where control flow joins and which assignment reaches each use.

**Why optimizers want SSA.** SSA was designed for optimization (Cytron, Ferrante, Rosen, Wegman and Zadeck, TOPLAS 1991). Because each value has exactly one definition:
- "Which value is this?" has one answer, the instruction that defines it. With slots, the answer depends on which `store` ran last on the path taken, and finding it takes a data-flow analysis of the whole function.
- Each value's uses can be listed directly. Constant propagation, dead-code removal and common-subexpression elimination follow those links, instead of revisiting every block until nothing changes.
- Values never change, so an optimizer can move, merge or delete instructions without checking for writes in between.
- Register allocation is easier: the interference graph of an SSA program is chordal, so it can be colored optimally in polynomial time (Hack, Grund and Goos, CC 2006).

LLVM, GCC (since 4.0), Cranelift, Go (since 1.7), V8's optimizing tier and HotSpot's server compiler all optimize on SSA. Go's SSA back end cut the CPU time of Go's benchmark programs by 5–35% ([Go 1.7 release notes](https://go.dev/doc/go1.7)).

**What SSA costs.**
- **Building it.** The checker would track every local's current value in every block. It would add a parameter at each join where the values differ, and fix up a loop's header once the loop body has been read. Cranelift's frontend builds SSA this way as code is emitted, with Braun et al.'s algorithm (CC 2013), which also removes the parameters that turn out to be unneeded.
- **Undoing it.** Machine code has no block parameters, so every backend turns them back into copies on the incoming edges. That can need new blocks on some edges, and a careful order for copies that swap values.
- **Liveness.** A one-pass backend must know where each value dies. TPDE reads SSA and spends 12% of its time on a pass for block layout, loops and liveness. Shape A's single-use rule gives this for free (section 5.2).
- **Debugging, hot reload and coroutines.** After promotion a local has no fixed home, because its value moves between registers and copies. A debugger then needs extra tracking, and shows "optimized out" where it has none. Hot reload and coroutine frames need a stable home for each local (section 7). Roslyn keeps every local in the frame in debug builds for this reason ([ir.md](checker-and-ir-sources/ir.md) sections 8 and 9).
- **Structure.** The GPU writers need structured control flow, which a graph of blocks and jumps drops (the GPU output row of decisions.md).

**Who builds SSA, and when.**
- **Clang doesn't.** It gives each local a stack slot (an `alloca`) and leaves promotion to LLVM's `mem2reg` pass, which runs only when optimizing. LLVM's tutorial tells front ends there is "no need for your front-end to build SSA form", and says Clang works this way ([Kaleidoscope, chapter 7](https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/LangImpl07.html)).
- **Zig's AIR doesn't.** A runtime `var` becomes `alloc`, `load` and `store`, and only the LLVM backend promotes it ([ir.md](checker-and-ir-sources/ir.md) section 1.4).
- **Go builds SSA in every build, because it always optimizes.** With optimizations off (`-N`), it still builds SSA for temporaries but keeps every named variable in memory: `canSSA` returns false under `-N` (`cmd/compile/internal/ssagen/ssa.go`, Go 1.25.0). That is shape A's split.
- **Cranelift builds SSA in its frontend,** because its optimizations and its register allocator work on SSA.
- **Single-pass first tiers skip it.** V8's Liftoff, SpiderMonkey's baseline compiler and Wasmtime's Winch compile WebAssembly with no IR at all. TPDE compiled WebAssembly through Cranelift's SSA IR instead:
  - it was 1.74× slower to compile than Winch;
  - translating into that IR took 37% of its time;
  - its code ran 1.14× faster than Winch's ([ir.md](checker-and-ir-sources/ir.md) section 4.4).

**What shape A gives up.** A one-pass backend reads and writes slots in memory, so development builds run slower than optimized ones, as every compiler's unoptimized builds do. Some of that comes back without SSA:
- Liftoff can keep WebAssembly locals in registers between loops, and spills them before each loop. V8 tried a pre-pass to keep loop-invariant locals in registers, and found no significant gain at baseline code quality ([ir.md](checker-and-ir-sources/ir.md) section 11.1).
- TPDE gives values used across the innermost loop a fixed callee-saved register. Shape A marks loops, so a backend can do the same for a loop's busiest slots.

**What this means for chibi.** None of the five consumers in section 5 optimizes. SSA built by the checker would cost time in every build, help none of them, and make debugging, hot reload, coroutines and the GPU writers harder. Release builds still get SSA optimization: clang promotes shape A's slots just as it promotes Clang's own `alloca`s. If chibi later gets its own optimizing backend (the no-C-compiler row), that backend adds a promotion pass over shape A, run only in optimized builds, as `mem2reg` is. Moving the other way later, from SSA in the checker back to slots, would mean rewriting the checker and every backend.

### 5.6 Values where control flow joins

The owner asked whether `a and b` has a better form than the hidden local that step 2 uses (2026-11-14). The same question returns in step 3 for `??`, `catch` with a fallback value and `break label value`: each produces one of two values, depending on which path ran.

**How common `and` and `or` are.** The old Chibi's C sources (932,000 lines) use `&&` or `||` 25,948 times, about once every 36 lines. At least 54% of the lines that contain one start with `if`, `while` or `for`. The step-2 benchmark corpus has one every 12 lines.

**A. A hidden local, as built in step 2.** Store `a` in a `bool` slot, `if` on it, store `b` inside the branch, then load the slot.
- `and` takes 6 instructions and `or` takes 7, plus a 12-byte slot: 90 and 103 bytes.
- A one-pass backend keeps the slot in the frame, so the `bool` goes to memory and back twice.
- Go does this in value context, and says so in a comment (`cmd/compile/internal/ssagen/ssa.go` L3302, Go 1.25.0). Go then builds SSA, so its variable becomes a phi. chibi's backends don't promote slots (section 5.5), so in development builds the memory traffic stays.

**B. An `if` or `block` that carries a value,** as WebAssembly's `if (result T)` and Zig do. `if bool %0` opens an `if` whose result is a `bool`. `else %2` ends the first arm with its value, and `end false` ends the second arm; the `end` is the result:

```
    if bool %0
%2 = load bool b
    else %2
%4 = end false
```

`a or b` is `if bool %0`, `else true`, then `b`'s code, then `end` with `b`'s value.
- Both take 3 instructions and one constant, and no slot: 55 bytes.
- A one-pass backend picks one register for the result at the `if` and moves each arm's value into it, as Liftoff and Winch do for WebAssembly's typed blocks. The C backend assigns a temporary in each arm, as it would for A. The SPIR-V writer emits `OpPhi` in the merge block, which is how glslang lowers `&&` and `||` (`createShortCircuit` in `SPIRV/GlslangToSpv.cpp`).
- Zig's AstGen emits `bool_br_and` or `bool_br_or`. The right-hand side's body ends with a `break` to that instruction, so the instruction is the value (`boolBinOp`, `lib/std/zig/AstGen.zig` L6151, 0.17.1). In AIR, a `block`'s value arrives through `br` ([ir.md](checker-and-ir-sources/ir.md) section 1.2).
- The same form serves step 3's `??`, `catch` with a value and `break label value`, as a `block` with a result type, left by an `exit` or an `end` that carries a value. With A, each needs its own hidden local.
- Costs: `else`, `end` and `exit` gain an operand. An `end` with a type is a value, so the rule "value kinds are those below `call`" gains an exception. Every consumer must merge the arms' values, which every WebAssembly compiler already does.

**C. Branches instead of a value, in conditions only,** as Clang (`EmitBranchOnBoolExpr`), Go (`condBranch`, ssa.go L4118) and GCC do. When `a and b` is the condition of an `if` or `while`, no `bool` is made: `if a { if b { … } }`.
- With an `else`, or with `or`, the code that runs when the condition fails is reached from two places. In a structured IR that needs `block`s opened before `a` is read, and `exit`s that leave two blocks at once. The GPU writers turn those back into flags or merge ladders ([ir.md](checker-and-ir-sources/ir.md) section 7.5).
- The checker must know the condition's shape before it reads `a`, so `logical_left` would depend on its parent through a new marker.
- It saves a one-pass backend about three machine instructions per `and` in a condition, one of them a jump: it branches on the comparison instead of setting a register, merging and testing it.

**Measured on 2026-11-14,** at the owner's request ([experiments/logical-lowering](../../experiments/logical-lowering/README.md)):
- **The checker's time doesn't separate A and B:** 245–248 against 245–254 cycles per line on the step-2 corpus, and 135–143 against 131–141 on a corpus with an `and` or `or` every 3 lines. B executes about 170 fewer instructions for each operator, and its IR is 3.7% and 17% smaller on the two corpora.
- **Machine code for `if x < n and y < m`,** written as a one-pass backend would emit it, with locals in frame slots. When the branches are predictable, A takes 4.2 cycles, B 2.95 and C 2.97. When they are unpredictable, A takes 24.6–26.3, B 20.7–22.3 and C 21.0–21.4.

**Result: B.** It is at least as fast as A in every measurement, and smaller. C was not faster than B on the M4 Pro, and would cost parser support, a conditional `exit` and multi-level exits for the GPU writers. `src/check.c` uses B since 2026-11-14, and the owner confirmed it the same day ("Okay, let's go with B"). The experiment keeps A's checker, so the comparison can be rerun.

## 6. Fast interpreters: guaranteed tail calls and computed `goto`

The owner wants fast interpreters, and wants speed features considered early (the fast-interpreters row of decisions.md). An interpreter spends much of its time on *dispatch*, getting from one instruction's code to the next. There are three ways to write it in C:

- **switch:** a loop around a `switch` on the opcode. Every instruction goes back through one shared indirect jump.
- **threaded:** each instruction's code ends with its own jump to the next, `goto *targets[op]`. This is *computed goto*, a GCC and clang extension, also called labels as values. Each jump site gets its own entry in the branch predictor.
- **tail calls:** each instruction is its own function and ends by calling the next instruction's function. The call is a *guaranteed tail call*: the compiler must turn it into a jump, so the stack doesn't grow. In clang that is `[[clang::musttail]] return next(...)`.

### 6.1 Measured on the owner's M4 Pro

On 2026-11-12, H9's interpreter got a third and a fourth dispatch loop ([experiments/h9-interpreter](../../experiments/h9-interpreter/README.md), section "Tail-call dispatch"):
- **tail:** each handler is marked `preserve_none`, a clang calling convention that keeps more values in registers;
- **tail+slot:** handlers are reached through a second, writable table, the way a development build would call through hot-reload slots.

The table shows cycles per operation with Apple clang 21. Each range covers 3 runs of 5 repetitions, with other sessions running.

| Program | switch | threaded | tail | tail+slot |
|---|---|---|---|---|
| count: add, compare, loop | 3.01–3.14 | 2.48–2.57 | 2.35–2.46 | 2.81–2.95 |
| calls: a call and return per iteration | 3.10–3.62 | 3.13–4.86 | 3.02–4.10 | 3.88–4.27 |
| table: byte stores and loads | 2.71–2.86 | 2.42–2.56 | 2.34–2.40 | 2.68–2.87 |
| fib: deep recursion | 5.65–6.34 | 4.29–4.78 | 3.59–3.94 | 3.92–4.18 |

- **Tail calls were fastest on three of four programs.** Comparing the middles of the ranges, they took about 5% fewer cycles than threaded dispatch on the loops and about 17% fewer on `fib`. Against switch dispatch they took 15–37% fewer.
- **`calls` moves with code layout, whatever the dispatch.** Adding unrelated functions to the binary changed none of the threaded loop's source or instruction count. It still moved that loop's `calls` row from 3.5 to 4.6–4.9 cycles per operation. A one-function interpreter shares one register allocation and one code layout across every instruction, so unrelated changes reach it.
- **The calling convention matters as much as the tail call.** Without `preserve_none`, the same tail-call handlers took about 8–20% more cycles per operation (one noisy set of runs).
- **The guarantee holds without optimization.** Built with `-O0`, the tail-call interpreter ran 40 million chained dispatches on an 8 MB stack. The same code without `musttail` overflowed the stack. A debug build of chibi doesn't optimize, so only a guaranteed tail call works there.
- **Slots cost 0.1–0.6 cycles per operation,** for one more dependent load.

These loops are small enough for the branch predictor to learn, so they are a best case for every style (H9).

### 6.2 Guaranteed tail calls

Here is a handler of an interpreter written in chibi, with the spelling this section recommends:

```
fn op_add(m: *Machine, pc: int) -> Status {
    i := m.code[pc]
    m.regs[i.dst] = m.regs[i.a] + m.regs[i.b]
    @tail return handlers[m.code[pc + 1].op](m, pc + 1)
}
```

`@tail` means: this function's frame is finished, and the call takes its place. A plain `return handlers[…](m, pc + 1)` grows the stack by one frame per instruction, unless an optimizer happens to turn it into a jump. Debug builds don't optimize, so without a guarantee the interpreter overflows its stack (section 6.1).

**Why the call is marked.** A compiler could instead make every call in tail position a tail call, as Lua and Scheme do. Four reasons favour a mark:
- **A guarantee needs an error where it can't hold.** Some calls can't become jumps: the types differ, or a `defer` must still run. Without a mark, the compiler can only make such a call an ordinary call. The program compiles, then overflows its stack, perhaps only on large inputs, or only after someone adds a `defer`. With a mark, the checker rejects the line that asked.
- **It changes what the function means.** The caller's frame, and every local in it, is gone before the callee starts. The C proposal for tail calls says such calls "are not an optimization, and directly affect the program semantics in potentially observable ways" ([WG14 N3266](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n3266.htm), 2024).
- **Frames vanish from stack traces and debuggers.** A mark limits that to the calls that asked for it. JavaScript is the recent attempt at the implicit rule: ES2015 required it in strict-mode code, and only Safari shipped it. The proposal for an explicit `return continue` instead cited the risk of slowing existing web code, missing frames in developer tools and `error.stack`, and the value of recording "user intent" when an algorithm depends on tail calls ([tc39 proposal-ptc-syntax](https://github.com/tc39/proposal-ptc-syntax)). It stalled.
- **The C calling convention allows only some tail calls** ("Why signatures must match", below). An implicit rule would apply only where the signatures match, which the reader can't see at the call.

**What others do** (sources pinned in the introduction; details in [dispatch.md](checker-and-ir-sources/dispatch.md)):

| Language or compiler | Form | Signature rule | Status |
|---|---|---|---|
| Clang | `[[clang::musttail]] return f(x);` | same number of arguments, "similar" types, same calling convention; no varargs; every variable in scope trivially destructible | since Clang 13 (2021); holds "even when optimizations are disabled"; rules unchanged through November 2026 |
| GCC | the same attribute | no documented signature rule | since GCC 15 (2025); no promise at `-O0` |
| MSVC | `[[msvc::musttail]]` | the callee may not use more stack space | since 14.50 (November 2025); x64 only, and only with `/O2` |
| Zig | `return @call(.always_tail, f, .{ x })` | identical function types, calling convention included | LLVM and C backends only. Zig's own backends never implemented it, so since 0.15 it fails in default x86_64 Debug builds |
| Rust | `become f(x)` | identical signatures | experimental on nightly; RFC 3407 not yet accepted |
| Odin | `return #must_tail f(x)` | identical types; not the `"odin"` convention | added February 2026 |
| WebAssembly | `return_call f` | only the result types must match | part of Wasm 3.0 (2025); Chrome 112, Firefox 121, Safari 18.2 |
| Lua, Scheme | every call in tail position | none | required by the language |
| Kotlin, Scala | `tailrec`, `@tailrec` | calls to itself only | rewritten into a loop |
| Go | rejected in 2019 (proposal 22624) | — | reasons included `defer` and `runtime.Callers` |

**Why signatures must match.** Under the C calling convention the caller removes the stack arguments after a call. A function can therefore pass on only the stack argument area its own caller gave it. Identical signatures guarantee the area fits. LLVM drops the rule only for conventions where the callee removes its own arguments (`tailcc`), as Cranelift's `tail` convention and WebAssembly's `return_call` do (LLVM LangRef and CodeGenerator docs, 23.1.5).

**Who builds interpreters this way, and what it bought:**
- **CPython 3.14** ships an interpreter whose handlers end in `musttail` and use `preserve_none`.
  - The first claim was a 9–15% geometric mean. It was revised down to 3–5% once it turned out that LLVM 19 had slowed the baseline: it merged CPython's 332 computed-goto jumps into 3 (Nelson Elhage, 2025; fixed in LLVM 20.1.2).
  - In CPython 3.15, built with MSVC, the same design is 15–20% faster than the switch interpreter that MSVC builds used before.
- **upb, the protobuf parser.** Josh Haberman's tail-call design parses at 2.0–2.3 GB/s, 71% less time than before (2021). He credits small functions that each keep their state in registers, with slow paths moved out.
- **Deegen.** Haoran Xu's generated Lua interpreter uses LLVM's GHC convention, which has no callee-saved registers. It is 28–31% faster than LuaJIT's hand-written assembly interpreter (2022 post, 2024 paper), though much of that gain comes from inline caching rather than from dispatch.
- **A counterexample, zlib-rs.** Rust's RFC 3720 found a single-frame `loop` with `match` about 15% faster than a tail-call version, because the tail-call version kept reloading state that didn't fit in arguments. Tail calls suit a small machine state that fits in registers. The labeled switch of section 6.3 suits large state machines.

**Options for the spelling:**
- **A. A keyword in place of `return`: `become f(x)`.** Alef had it in 1995; its manual says functions that call each other with `become` "execute in constant stack space" ([reference manual](https://doc.cat-v.org/plan_9/2nd_edition/papers/alef/ref) 6.6.4). Rust uses it on nightly, and Go's rejected proposal used the same word.
  - Pros: one short statement that can't hide inside an expression.
  - Cons: one more keyword, and the owner dislikes the word (2026-11-12).
- **B. `return` followed by a keyword.** The C proposal writes `return goto f(x)`. N3266 proposes it for a Technical Specification, not the C standard, and N3582 revises it. The C++ draft D3939R0 uses the same form. JavaScript's stalled proposal wrote `return continue f(x)`.
  - Pros: it reads as a kind of return. `return continue` needs no new keyword.
  - Cons: the token audit removed `goto`, which would come back for this one use. `continue` already means "run the loop or the labeled switch again" (section 6.3).
- **C. A marker before `return`: `@tail return f(x)`.** Clang, GCC and MSVC put their attribute in this place, as in `[[clang::musttail]] return f(x);`. Odin writes `return #must_tail f(x)`.
  - Pros:
    - No keyword.
    - It follows adopt-or-settle 3.8, which puts an attribute before what it modifies, and whose attributes may already change meaning, as `@align(16)` does.
    - C code that uses `musttail` translates line for line.
  - Cons:
    - The marker waits for the attribute decision, since `@` isn't settled.
    - N3266 chose a statement over an attribute because in C an attribute can't change what a `return` means. chibi's attributes have no such rule, so this reason doesn't carry over.
- **D. A builtin call,** Zig's `return @call(.always_tail, f, .{ x })`.
  - Cons: it is an expression, so Zig accepts `return n * @call(.always_tail, f, …)` and returns the wrong value (Zig issue 18223).
- **E. No mark: every call in tail position is a tail call,** as in Lua and Scheme.
  - Pros: no syntax at all.
  - Cons: the four reasons above.

**Rules the checker enforces,** whatever the spelling:
1. The callee's type equals the caller's: parameters, result and calling convention, as in Zig, Odin, Rust and N3266. That also suits the C backend, because Clang's rule accepts identical types.
2. A pending `defer` or `defer catch` in any enclosing block is an error, as in N3266 and in Zig's original tail-call proposal (issue 694).
   - The other choice is to run the `defer`s before the jump, as Rust drops locals before a `become`. Then a `defer` that frees memory the arguments point to hands the callee a dangling pointer. Rust's borrow checker catches that, and chibi has nothing that would.
   - A `defer catch` has no answer at all: after the jump, nothing is left to run it if the callee fails.
   - Allowing `defer`s later breaks no code.
3. An argument that takes the address of a local, such as `&x` or a slice of a local array, is an error. Rust's borrow checker gives the same error. An address stored earlier can't be caught this way, and using it after the jump is illegal behaviour, as Clang and GCC define it. Zig miscompiles this case silently (issue 9703).
4. No varargs, and no tail calls in GPU code, where recursion is forbidden ([shader-languages.md](shader-languages.md) section 4).

**What each backend must do:**
- **Interpreter:** copy the arguments into the frame's parameter slots and jump to the callee's first instruction.
- **Development backend:** jump after the epilogue. It must do this from its first version and at its lowest optimization level. Zig's own backends never did, which is why the feature fails in Zig's default Debug builds.
- **C backend:** `[[clang::musttail]]` and, for handlers, `preserve_none` (Clang 19 or later). Release builds use clang (the release-builds row), so GCC's and MSVC's limits don't apply.
- **WebAssembly:** `return_call` or `return_call_indirect`. LLVM needs `-mtail-call`, which its default CPU doesn't enable.
- **GPU:** a compile error (rule 4).

**The calling convention.** Section 6.1 measured 8–20% more cycles per operation without `preserve_none`. CPython marks its handlers with it. Odin (2026) and Zig 0.17 let programs do the same, with `"preserve/none"` and with `x86_64_preserve_none` and `aarch64_preserve_none`. [adopt-or-settle.md](adopt-or-settle.md) 2.7 recommends the C convention for every chibi function, so that any function can be a C callback. A handler would need one opt-in exception. Every vendor still calls `preserve_none`'s rules unstable.

**Recommendation:**
- Adopt guaranteed tail calls, spelled `@tail return f(x)` (option C), with rules 1–4. The owner agreed to the spelling on 2026-11-12; the rules are still open (question 8). `@tail` is the first attribute, and the rest of the attribute design is open ([adopt-or-settle.md](adopt-or-settle.md) 3.8).
- Add an opt-in register-heavy convention for handlers, spelled once the attribute syntax is decided.
- Debug builds keep the guarantee, because an interpreter needs it to run at all. Their stack traces lose the frames that tail calls replaced, as Lua and Rust's RFC document.

### 6.3 Computed `goto` and the labeled switch

chibi already has a structured form of computed `goto` (the Labels row of decisions.md). It is Zig's labeled switch, where `continue name value` runs the switch again on a new value:

```
fn run(m: *Machine) -> Status {
    pc := 0
    dispatch: switch m.code[pc].op {
        .add => {
            i := m.code[pc]
            m.regs[i.dst] = m.regs[i.a] + m.regs[i.b]
            pc += 1
            continue dispatch m.code[pc].op
        }
        .jump => {
            pc = m.code[pc].dst
            continue dispatch m.code[pc].op
        }
        .ret => return .ok
    }
}
```

The same loop in GNU C with labels as values, the feature this section asks about:

```c
static void *targets[] = { &&op_add, &&op_jump, &&op_ret };
goto *targets[code[pc].op];
op_add:  /* ... */ pc += 1; goto *targets[code[pc].op];
op_jump: pc = code[pc].dst; goto *targets[code[pc].op];
op_ret:  return OK;
```

Both aim at the same machine code: each instruction ends with its own indirect jump through a shared table, so the branch predictor sees one jump per opcode instead of one for the whole loop.

**What others do:**
- **GCC and clang** support labels as values. MSVC doesn't, so CPython's and Lua's Windows builds fall back to a switch.
- **Zig** removed `goto` and added the labeled switch in 0.14 (proposal 8220), after LLVM merged a hand-duplicated dispatch back into one jump. Zig's tokenizer got 13% faster with it. The release notes promise only that the compiler "will attempt" one jump per `continue`.
  - Its LLVM backend emits one jump per site.
  - Its own x86_64 backend does too, but only for non-PIC ELF output with at most 1,024 dense cases. In Mach-O and PIC builds every `continue` goes back through one shared compare chain.
  - Its C backend uses one shared `switch`.
  - WebAssembly has no indirect jumps, so it uses `br_table`.
  
  On macOS every build is PIC, so a guarantee there needs a table of relative offsets, which GCC's manual describes for read-only label tables.
- **Rust** is experimenting with `#[loop_match]` and `#[const_continue]` on nightly (2025). They give direct jumps only when the next state is known at compile time. On the `autolink_email` benchmark of comrak, a Markdown parser, that ran 2.74× faster than the plain loop.
- **C3** removed `goto`. `nextcase value` accepts a runtime value, and since 0.6 a `@jump` attribute makes a switch a guaranteed jump table.

**No source-level form guarantees one jump per site in C.** LLVM 19 merged CPython's 332 dispatch jumps into 3, even with computed `goto` (section 6.2). The gain itself also shrinks as branch predictors improve:
- Rohou, Swamy and Seznec measured threaded dispatch on Python 3.3 at 10.1%, 4.2% and 2.8% faster on Nehalem, Sandy Bridge and Haswell, and concluded that predicting indirect branches "is no longer critical for interpreters" (CGO 2015).
- Elhage put the fair value on CPython at about 2%.
- On the M4 Pro, H9's threaded dispatch still took 16–25% fewer cycles than its switch on three small loops.

**Options:**
- **A. Add labels as values** (`&&label`, `goto *p`).
  - Pros: C's familiar tool, and the C translator could carry such code over directly.
  - Cons:
    - `goto` went out in the token audit, and would come back.
    - A label's address stored in data breaks hot reload, because the code moves. Making it safe needs an index into a table the compiler owns, which is what the labeled switch already is.
    - Any label can be reached from anywhere, so the IR can no longer be structured, and GPU code can't express it.
    - MSVC can't compile it in the C backend.
    - With pointer authentication on arm64e, signing every table entry "can eat up a lot of the benefit" (John McCall, Swift forums, 2024).
- **B. Keep the labeled switch and guarantee one jump per `continue`** in the backends chibi writes itself.
  - A `continue name value` whose value is known at compile time becomes a direct jump, as in Zig and Rust.
  - Any other `continue` becomes a bounds check plus its own indirect jump through the switch's one table. That is the code Zig's x86_64 backend emits.
  - The C backend asks for the same with computed `goto`, which works on clang, the release compiler. WebAssembly gets `br_table`, and GPU code gets `loop` and `switch`.
  - Pros: a guarantee no C compiler gives, nothing new to learn, no label addresses in data, and a structured IR.
  - Cons: code that relies on labels as values, such as some C interpreters, must be rewritten for the C translator.
- **C. Keep the labeled switch with no guarantee,** as Zig does. Nothing to promise, but a backend change can quietly merge the jumps again.

**Recommendation: B.** Tail calls (section 6.2) and the labeled switch cover different programs: small machine states in registers and large state machines in one frame. Labels as values add nothing to either.

### 6.4 What the IR keeps

- **A call marked as a tail call.** The checker has already enforced section 6.2's rules. Every backend must honour the mark or reject the target before code generation, so LLVM's "failed to perform tail call elimination" never reaches a user, as it does in Clang, Zig and Odin.
- **A labeled switch as its own structure,** with `continue name value` as a dispatch instruction that names it. Lowering it to "assign and jump back to the top" would lose the per-site jumps that section 6.3 promises. Zig's AIR has the same pair, `loop_switch_br` and `switch_dispatch` (0.14). Its SPIR-V backend lowers them to a loop around a switch on a variable, and its C backend to one shared `switch`. Per-site dispatch survives only in backends that keep the instruction until they emit code.
- **Each function's calling convention,** in the function's record, so the handler convention can be added without changing the IR.

## 7. What the IR must leave room for

None of these is built in the first step, but each constrains the IR now.

- **Coroutines** (wanted; the required-features row). They are lowered by chibi itself into state-machine records, not by LLVM ([metaprogramming-reload-coroutines.md](metaprogramming-reload-coroutines.md) section 6, point 12). The IR needs three things for that:
  - each suspension point as its own instruction, with an id taken from its source position, so hot reload can map a suspended frame onto new code;
  - locals as numbered slots, so the frame record is the locals plus whatever temporaries are live at a suspension point;
  - a cheap way to know which temporaries are live there. Section 5's single-use rule gives this without a liveness pass.

  **The hard case: resuming in the middle of a loop.** A suspension point inside a loop gives that loop a second entry, which a structured IR can't express. The others lower coroutines where jumps are free:
  - Rust in MIR;
  - C# in its typed tree, rewritten into `goto` form;
  - Swift through LLVM's coroutine passes;
  - Kotlin on JVM bytecode;
  - Zig's removed async through its own IR and LLVM.

  chibi has a way out: GPU code can't have coroutines ([shader-languages.md](shader-languages.md) section 4), and only the GPU writers need the structure. Coroutines can therefore be lowered on the way to the CPU backends, which all accept plain jumps. Bytecode and machine code have jumps, and C has `goto`. The coroutines sheet decides this.
- **Hot reload** (the Edit and Continue goal). Calls name a function by index, and lowering decides whether a call goes through a slot (development builds) or directly (release builds) ([metaprogramming-reload-coroutines.md](metaprogramming-reload-coroutines.md) section 6, point 8). Loop back edges and calls are the safe points where a patch can apply, and the IR marks both.

  Instruction numbers and frame offsets change with every edit, so neither can identify a local. C#'s Edit and Continue identifies each local by its declaration's offset from the start of the method body, and each suspension point by its offset plus an ordinal. Its slot allocator maps every new local to its old slot through those ids (`LocalDebugId`, `EncVariableSlotAllocator` in Roslyn). chibi needs the same kind of id for locals and suspension points, and it must survive into the bytecode, the frame layout and the coroutine lowering.
- **Generics and compile-time code** (sheets 6–8, open). The checker must be able to check one body several times with different compile-time arguments, so its state lives in a per-job record and never in globals. Instances are found by (declaration, argument ids) in an interned table, as section 6 of the same note requires.
- **GPU writers** (the GPU output row of decisions.md). The IR keeps structured control flow, typed values and field indices ([shader-backends.md](shader-backends.md) section 4). Byte offsets appear only when the IR is lowered to bytecode or machine code.
- **Error return traces and runtime traps** (the error-code row of decisions.md). Every call that can fail and every instruction that can trap needs a source position, kept in a side table rather than in every instruction.

## 8. The first step

**Status, 2026-11-12: step 1 is built** in `src/check.h`, `src/check.c` and `test/check_test.c`, after the owner decided questions 1–5. [experiments/check-decls](../../experiments/check-decls/README.md) measures it at 94 instructions per node and 116–159 cycles per line, depending on machine load, on 451,264 generated lines of declarations. What step 1 leaves out, each reported as "the checker doesn't support this yet":
- function bodies, global variables, imports and qualified names, statements at file scope, untyped parameters, floats, strings, bools, and anonymous unions;
- sizes and alignments, because the layout of `?T` and `E!T` is not decided; the layout phase only resolves member types and finds structs that contain themselves;
- error sets other than `error` in `E!T`;
- threads: the claims run on one thread.

**Status, 2026-11-13: step 2 is built,** after the owner decided questions 6 and 7.
- `src/exact.c` and `src/exact.h` hold constant values as question 6 decided. A value is 16 bytes: an `int64` inline, or limbs in a side array for larger integers and fractions. Every operation tries 64 bits first, and the slow path works on fixed stack buffers, so it never allocates beyond the stored result. `test/exact_test.c` checks it against Python's `Fraction`, IEEE rounding edge cases (ties, subnormals, overflow) and 20,000 random operations against `__int128`.
- `src/ir.h` holds the IR of section 5, and `check_dump_ir` prints it for tests.
- The checker covers locals, integer, float and `bool` arithmetic, comparisons, `and`, `or` and `!`, `if` and `else if`, `while`, `for` over a range, `break` and `continue`, `return`, calls with default arguments, `T(x)` conversions and return types inferred from the body. It reports a function that can reach its end without returning a value.
- [experiments/check-bodies](../../experiments/check-bodies/README.md) measures it at about 250 cycles per line, or 16 million lines per second on one thread, on 694,816 generated, well-typed lines. Since the `and`/`or` change of section 5.6 the IR takes 4.0 instructions and 69 bytes per line.

Choices made while building step 2, open to review:
- **Constants in the IR are not interned.** Each function appends its constants to its own array, and a constant operand indexes it. Interning costs a hash and a probe per constant, and the single-use rule means a constant is rarely repeated within an expression anyway.
- **A comparison records its operand type,** as in `lt int %1, %2`. Its result is always `bool`.
- **`a and b` and `a or b` use a hidden `bool` local:** store `a`, `if` on it, store `b` inside the branch, load the result. The IR has no value-carrying blocks. A constant `a` that decides the result puts `b` inside `if false`, so `b`'s calls still type-check but never run. Replaced on 2026-11-14 by an `if` that carries a value, after the measurements in section 5.6.
- **`for i in a..b`** stores `a` in `i` and `b` in a hidden local, then emits `loop`, an `if` on `i < b`, and a `block` around the body. `continue` exits the block, so the increment still runs. `..=` compares with `<=` and exits on `i == b` before incrementing, so `0..=255` on a `u8` never overflows.
- **Discarding a value takes `_ =`,** as in Zig (owner, 2026-11-14). `f()` alone is an error when `f` returns a value. `_ = e` emits `drop` for a runtime value and nothing for a constant. `_` is never bound, so it can name several parameters and a `for` variable, and reading it or declaring it with `:=` is an error.
- **`u8(x & 0xFF)`** emits `truncate`, with no range check, when the mask fits the target type. Any other narrowing emits `narrow`, which is checked.
- **Parameters and `for` variables are read-only,** as in Zig (owner, 2026-11-14). An assignment reads the kind of the node that declared the slot, so the rule stores nothing new. Checking speed didn't change: 251–257 cycles per line.
- **A function body writes its IR straight into the package's store,** reserved from the node count. A body checked out of order, for a call to a function with an inferred return type, goes through a separate buffer that is appended when the outer body finishes. Copying every body instead cost 13–15% of checking time.

Once the owner has answered section 9, the research thread writes the checker for a core subset, in new files:
- `src/check.h` and `src/check.c`: the checker;
- `src/ir.h`: the IR's kinds and records, shared with lowering;
- `test/check_test.c`: its tests.

Three things go through the parser thread: the list of top-level nodes, builtin names with fixed ids (section 1), and the two lines that register the test suite in `test/main.c`.

The work goes in this order, and each step is measured before the next starts (the Process row of decisions.md):
1. **Declarations, types and signatures for one package:** structs, enums, unions, constants and function signatures. Diagnostics for unknown names, duplicate names and declarations that depend on themselves.
2. **Function bodies, on one thread:** integers, floats and `bool`, locals, arithmetic and comparisons, `if`, `while`, `for` over a range, `return` and calls, written out as IR. An IR dump makes the IR testable.
2b. **A native backend for the step-2 IR,** moved ahead of step 3 by the owner on 2026-11-14: one pass to arm64, into executable memory, with calls through a table of function addresses. It measures cycles per IR instruction and lines per second from source to machine code, and runs the tests' programs. Predicted before building: about 40 cycles per IR instruction (25–60), about 3 arm64 instructions per IR instruction, and about 670 cycles per line for lexing, parsing, checking and code generation together, or 6 million lines per second on one core.
   **Built on 2026-11-14** as `src/arm64.c`, with `test/arm64_test.c` ([experiments/arm64-backend](../../experiments/arm64-backend/README.md)):
   - Code generation measures 53–56 cycles per IR instruction, with 1.62 arm64 instructions each. Lexing, parsing, checking and code generation together take 683 cycles per line, 5.8 million lines per second on one core. The first version took 74 cycles per IR instruction; a profile showed the overhead, and section "Why the first version missed" of the experiment lists the fixes.
   - The tests run the generated code. For `i8` and `u8`, every pair of operands in a large sample is checked against a C reference, including which check fails. For 32- and 64-bit integers, edge values are checked the same way. Control flow, calls, recursion, spilling, mixed integer and float arguments, and float-to-integer checks have their own tests. One test replaces a function's table entry while the program is loaded.
   - **Review and output speed, 2026-11-15.**
     - An independent review confirmed four bugs: `!void` returns, non-scalar types, constant `and`/`or` around a call, and seven-deep `and`/`or`. All are fixed, with tests that fail on the first commit.
     - Generated code now runs as fast as clang `-O0` on `fib`, `collatz` and `primes` (0.95–1.14×), and 1.1–3.5× slower than `-O2`.
     - A loop whose first instruction loads the slot that the instruction just before it stored ran at half speed on the M4 Pro, so the backend now puts a `nop` and 16-byte padding before each loop head.
     - Re-timed on the backend's own code, the value-carrying `if` of section 5.6 made two `and`/`or` loops 21% and 27% faster than the hidden local.
   - **Improvements, 2026-11-15**, after the owner asked for every improvement that doesn't belong to a later step:
     - Locals live in registers, chosen by use counts that the checker records: ×8 per enclosing loop, plus a call count per function (decisions.md, Compiler, 2026-11-15). Values live across a call move to callee-saved registers. Functions without calls or stack slots get no frame.
     - Division by constants uses shifts or magic-number multiplies; `x % 2^k == 0` and `x & c == 0` become `tst`; constants become immediates where arm64 has them; range checks take one compare.
     - Generated code: `sum` 1.08–1.20× clang `-O2` (2.5× before), `primes` 1.03–1.08×, `collatz` 1.84–1.86× (3.5×), `fib` 1.81–1.83× (1.9×); every function is now faster than `-O0`, or equal on `primes`.
     - 22% fewer arm64 instructions (1.26 per IR instruction), for 7–10% more code generation time: 55.5–57.2 cycles per IR instruction, and 667–692 cycles per line for all four phases, 5.7–6.1 million lines per second.
     - A differential test now runs random programs natively and on a reference interpreter of the IR (`test/ir_reference.c`). A second independent review confirmed two problems, both fixed: parameters from C callers with the upper bits set, and a quadratic list search.
     - Left for later steps: threads per function (step 4), reusing the code mapping across compiles (hot reload), extending narrow results of C calls and stack probes for large frames (step 3).
   - Choices open to review:
     - `x % y` with the most negative `x` and `y == -1` gives 0 instead of trapping, because the overflow row names only division.
     - A failed check runs `brk #kind`, with one such instruction per kind at the end of each function.
     - A local keeps one register for the whole function, or none. Liftoff in V8 tracks which locals sit in registers along the code and merges at joins, which places more locals but costs more per instruction.
3. **The rest of the subset:** pointers, arrays, slices, fields and literals, optionals (`?T`, `!!`, `??`, narrowing after `!= nil`), errors (`try`, `catch` and `defer catch`, with sets inferred afterwards), `switch`, labels and `defer`. Each part lands in the checker, the arm64 backend, the reference interpreter (`test/ir_reference.c`) and the random-program generator together, so the differential test keeps covering everything. Order, started 2026-11-15:
   - **3a. Labels: built 2026-11-15.** `name:` on `while`, `for` and blocks; `break name` and `continue name` from nested loops; a labeled block used as a value, left with `break name value`, which the IR carries as `exit @b T %v` and a typed `end`. The parser now marks a labeled block in expression position with its own nodes (`VALUE_LABEL_START`, `LABELED_VALUE`). Rules chosen while building, open to review:
     - A block used as a value takes its type from the declaration, assignment, `return`, call argument or `break` to a typed block it is the whole value of; anywhere else, it takes the type that every `break` value converts to, as Zig's peer type resolution does. The owner chose this on 2026-11-15 over the first version, where the first `break` value set the type (decisions.md). The IR keeps a `nop` before each `exit` whose value could widen, and the block's `end` turns it into a `convert` where needed. The generator now writes narrower `break` values and blocks of only untyped constants; 320,000 programs agree with the reference interpreter. Later the same day (owner): a function without `->` takes its type from its `return`s by the same rule, with the same `nop` before `ret`; the parser marks a value block that is a whole call argument (`NODE_LABELED_ITEM`) so it takes the parameter's type; and the generator writes both, with 360,000 more programs passing. Then (owner): a `break` to a block with a type passes that type into a block that is its whole value, and a block's value widens unless one of its `break` values is an operator's result (`Peers.result` keeps that bit); 200,000 more programs pass. Cost, measured on dense corpora: about 11 cycles per peer-typed `break` and 24 per inferred `return` in checking; no change on the timing corpus.
     - A block used as a value whose end can be reached is an error, which is the rule the Labels row proposes.
     - `break name value` to a loop or to a block used as a statement is an error, as is a plain `break name` to a block used as a value.
     - Measured: the random-program generator now writes labeled loops, labeled blocks and nested value blocks that break to outer ones; 480,000 programs agree with the reference interpreter. A typing bug the generator found is fixed: `(b: { … }) > x` used to take the declaration's type for `b`.
   - **3b. Enums and `switch`: built 2026-11-16.** Enum values (`Color.red`, `.red` where the enum is known), `==` and `!=`, conversions both ways (`Color(n)` checks membership), and `switch` as a statement and as a value, with ranges, several labels per arm, `_`, full coverage and labeled `break`. A value switch is a typed block whose arms `exit` with their values; an arm takes the switch's type. The IR `switch` carries a sorted case table; arm64 picks a jump table or a binary search; the reference interpreter scans the table. The generator writes enums, value switches over integers, `bool` and enums, switch statements with labels, and enum conversions that trap; 200,000 programs agree with the reference interpreter. Bugs found and fixed on the way: a signed widening into an enum (`i16` to a 64-bit enum) was zero-extended; a value block that is a whole switch arm didn't take the switch's type; and, from 3a, a `break` in unreachable code made the code after its block or loop reachable again, which gave false "missing return" errors. Choices open to review are in decisions.md (2026-11-16 rows): the default enum representation, `c: Color` needing a zero member, and the jump-table thresholds. `continue sw value` waits for question 9. Measured: on the timing corpus, which has no enums or switches, checking costs about 3.5 cycles per line more (+1.4%) with fewer instructions, because three small helpers gained a second caller and stopped being inlined into `walk`; marking them `ALWAYS_INLINE` and the cold switch code `NOINLINE` recovered about half. A switch with 7 cases and `_` costs about 3,900 cycles per function from source to machine code, against about 4,570 for the same `if` chain.
   - **3c. `defer`;** question 15 decided. `defer catch` comes with errors.
   - **3d. Pointers, structs, arrays, slices and literals;** questions 12, 13 and 14 decided. Options A and C of question 14 were timed on 2026-11-19, on x86-64; C was at least as fast on every kernel, so 3d builds C (question 14 below). It was built in nine parts: pointers, arrays, slices, structs, slices in memory, small structs passed by value, arrays and structs read left to right, large structs passed by value, then the hidden context.
     - **3d.1 Pointers to locals: built 2026-11-19.** `&x` on a local; `p.*` as a value and as a place (`p.* = v`, `p.* += v`, `&p.*`); `*T` parameters and results; `==` and `!=` on pointers; pointers to pointers. A pointee must be an integer, float, `bool`, enum or pointer until structs and arrays land. The IR gains `addr` (the address of a slot), `load-at` and `store-at`. The arm64 backend keeps a local whose address is taken in its stack slot and computes the address from `sp`. The reference interpreter loads and stores through a pointer with the pointee's size. The generator declares `qN := &vK`, reads and writes through those pointers and through `*T` parameters, with every compound operator, and passes `&vK` as an argument; 16,000 programs agree with the reference interpreter under qemu. Rules chosen while building, open to review:
       - `&` refuses a parameter or a `for` variable, because both are read-only (2026-11-14) and a pointer would let code write them. The error names which one it is; copying the value into a local first works.
       - `x: *T` with no value is an error, because `*T` is never nil (question 13). `?*T` comes with optionals in 3e.
       - In `p.* op= v`, the address is computed before `v` and the old value is read after `v`, the same order as for a local. The address goes into a hidden local, because every IR value is used once.
       - A local whose address is taken gets the weight `WEIGHT_ADDRESSED`, which the backend reads as "never in a register". A separate flag in each slot record made checking about 2.7% and code generation about 5.5% slower on the timing corpus, because the record grew from 16 to 20 bytes.
       - Measured on the timing corpus, which has no pointers, as x86-64 wall time pinned to one core (the M4 Pro is not available): medians of six runs put checking 1.6% and code generation 1.1% above the previous commit, inside the run-to-run spread.
     - **3d.2 Arrays: built 2026-11-19.** `[N]T` locals whose elements are integers, floats, `bool`, enums, pointers or arrays of those; `a: [N]T` zeroes it. Typed literals `T.[a, b]` and `[3]T.[.[…], .[…]]`, and anonymous `.[a, b]` where a declaration, an assignment or an outer literal gives the type. `a[i]` as a value and as a place, through a pointer (`p.*[i]`), with every compound operator; `&a[i]`, `&a` and `&m[i]` for a row; whole copies (`b := a`, `a = b`, `m[i] = row`); `.len`; `for x in a`, `for x, i in a` and `for _, i in a`, over a local, a row or `p.*`; `*[N]T` parameters. The IR gains `index` (an element's address, with the bounds check), `copy` and `zero`. The reference interpreter gives every slot memory and runs all three. The generator writes arrays of every element type with 1 to 6 elements and sometimes 9 to 4,100, two-dimensional arrays, literals, copies, fill loops, loops over arrays, element and array pointers, `*[N]T` parameters, indices that can trap, and a checksum of every array before an entry returns; 35,000 programs agree with the reference interpreter under qemu, and planted bugs in the bounds check, zeroing, element scaling and the tail of a long copy are each caught. Bug found on the way: the elements of a `for` variable that is an array could be written (`for r in m { r[0] = 1 }`); the checker now follows a place back to its local. Rules chosen while building, open to review:
       - Any integer type can index, as in Go and Odin. The index is sign- or zero-extended and compared unsigned, so a negative index traps. A constant index out of range is a compile-time error.
       - A literal must have exactly `N` elements, as in Zig; Go and Odin fill missing elements with zero.
       - A length must fit in `int`, and `.len` is an `int` constant. `p.*.len` and `m[i].len` still evaluate `p` and `i`, so `m[i].len` traps when `i` is out of range.
       - An element of a literal that is itself an array is read when the literal closes, after the elements to its right. Step 3d.7 changed this on 2026-11-19: it is read where it is written.
       - `for x in a` reads `a[i]` when step `i` starts, so the body sees its own writes to later elements. `x`, `i` and the elements of `x` are read-only. Each step checks the bounds again; dropping that check is a later optimization.
       - `copy` assumes its two operands are the same or don't overlap, which whole arrays and rows always are.
       - Arrays as parameters or results wait for rank 8 of the open decisions ([adopt-or-settle.md](adopt-or-settle.md) 2.8: whether parameters behave as copies, with large ones passed by pointer); `*[N]T` works now. `==` on arrays is not supported.
       - A literal's elements can be assigned (`u8.[1, 2][0] = 3` compiles and changes nothing visible). C allows it; Go and Odin reject it.
       - Measured on the timing corpus, which has no arrays: the code generated is the same instruction for instruction. Pinned x86-64 wall time put checking 2.5–4% above 3d.1 over three sets of runs, and lex + parse 0.7–3% above with no change to that code. Corrected during 3d.3: callgrind first counted the same instructions within 0.02%, but on 30 of the 200 files, which don't check without the rest, so that run never reached code generation. On the whole corpus, checking ran 2.5% more instructions than 3d.1 and code generation 0.9% more, so the wall time measured real work, not only layout. Part of the checking cost was each read of a name looking up its local's slot record twice, once to ask whether it is an array; 3d.3 looks it up once, which saves 0.7% of checking instructions.
     - **3d.3 Slices: built 2026-11-19.** `[]T` locals, parameters and results whose elements are integers, floats, `bool`, enums, pointers or arrays of those; `s: []T` is empty. Slicing with `a[lo..hi]`, `a[lo..=hi]`, `a[lo..]`, `a[..hi]` and `a[..=hi]` on arrays, rows, `p.*` and slices; `s[i]` as a value and as a place, with every compound operator; `.len`; `for v in s`, `for v, i in s` and `for _, i in s`; arrays converting to slices where a slice is expected. A slice is two IR values, a pointer and a length (question 14, option C): a local is two slots, `s` and `s.len`; a parameter takes two words; a call that returns a slice is a `call` followed by `second`, which names the length; and `ret` takes both. The IR gains `index-len` (an element's address, checked against a length that is a value), `slice` and `slice-from` (the new pointer, after the bounds checks) and `second`. The arm64 backend passes and returns a slice in two registers, as C passes `struct { T *ptr; int64_t len; }`, and a test calls compiled functions from C that way. The reference interpreter runs all four. The generator writes slices of every element type, taken from arrays, rows, pointers and literals, re-slices them in loops (`rest = rest[1..]`), passes them to helpers and returns them, with bounds that can trap; it never returns a slice of a local array. 45,000 programs agree with the reference interpreter under qemu. Of nine bugs planted in the new backend code, the tests caught seven at once, and an eighth (a result written to a register that a live value still named) after a test for it was added; the ninth guarded a case that can't occur, so that check is gone. Rules chosen while building, open to review:
       - A slice lives in two registers and never in memory yet: `[N][]T`, `&s`, slices of slices and a slice as the value of a labeled block are errors until step 3d.5 stores slices in memory.
       - An array converts to a slice of all its elements wherever a slice is expected: a typed declaration, an assignment, an argument or a `return`. Jai does the same; Go and Odin need `a[:]`, and Zig converts only `&a`. An anonymous literal such as `sum(.[a, b, 3])` gets a hidden array in the caller's frame. A `for` variable that is an array can't be sliced, because its elements are read-only (3d.2) and a slice could write them.
       - `s[i]` checks `0 ≤ i < len` and `a[lo..hi]` checks `0 ≤ lo ≤ hi ≤ len`, unsigned after sign or zero extension, so a negative bound traps. A constant bound is checked at compile time wherever the length is known: always for an array, and for a slice of an array with constant bounds in the same expression, so `x[1..5][9]` is an error. A constant bound must fit in 63 bits.
       - `a[lo..=hi]` is `a[lo..hi + 1]`, with `hi` widened to 64 bits first, so `x[0..=n - 1]` with `n = 0` is empty, and a `hi` that equals the largest `int` at run time traps as an overflow.
       - `s: []T` with no value has a null pointer and length 0, like Go's nil slice and Odin's zero value; Zig has no default. `==` on slices is an error, and there is no `.ptr` yet.
       - The read-only rule for parameters (2026-11-14) covers a slice's pointer and length, not its elements: `fn fill(s: []u8)` can write `s[i]`.
       - `for v in s` reads `s` once, when the loop starts, so assigning `s` in the body doesn't change the steps, and the steps need no bounds check. Each step reads its element when the step starts, as for arrays.
       - Nothing stops a function from returning a slice of its own local array, which then points into a dead frame, as in C, Zig and Odin.
       - Measured on the timing corpus, which has no slices: the machine code is byte for byte the same as 3d.2's. Callgrind on the whole corpus counts 0.3% more instructions in checking than 3d.2, with the fix above, and 1.0% more in code generation. The first version cost 4.2% more in checking, mostly register spills in the checker's call path, which grew when it learned to pass a slice as two words; a call whose signature has a slice now takes its own path, chosen by a flag on the declaration. Pinned x86-64 wall time, medians of 10 runs that alternate 3d.1, 3d.2 and 3d.3, puts checking 1.3% above 3d.2 and 2.0% above 3d.1, and code generation 2.2% above 3d.2 and 0.2% above 3d.1. In the same runs lex + parse, which did not change, spread over 1.6%, so these wall-time gaps are near the noise and the instruction counts are the better guide.
     - **3d.4 Structs: built 2026-11-19.** Struct locals whose fields are integers, floats, `bool`, enums, pointers, other structs, or arrays of those. A struct is laid out as C lays it out: fields in declaration order, each at the next multiple of its alignment, and the size rounded up to the largest alignment. `p: Point` zeroes every field and then stores the fields' defaults, nested ones included, with a loop for each array of structs whose element type has defaults; `= undefined` skips both. Typed literals `Point.{ 1, 2 }` and `Point.{ .y = 2 }`, and anonymous `.{ 1, 2 }` where a declaration, an assignment, an outer literal's field or an array literal gives the type. `s.x` as a value and as a place, with every compound operator; `&s.x`; `p.x` through a `*Point`; whole copies (`t := s`, `s = p.*`, `l.b = m.a`); arrays of structs, array fields, and `for` over an array of structs; `*Point` parameters. The IR gains `field`, a field's address: the struct's address plus the field's offset. The checker keeps each struct's alignment and field offsets in one list, so parameters and enum members store nothing more, and `field` names the field's entry in that list. The arm64 backend keeps structs in the frame as it keeps arrays, and `field` is one `add`; folding the offset into the load or store that follows is a later optimization. A test passes C structs to compiled functions by pointer and checks every field, so the layout is C's. The reference interpreter runs `field`. The generator declares up to three struct types per program with fields of every scalar type, arrays, earlier structs and arrays of those, and scalar defaults. It writes positional, named and empty literals, typed and anonymous, copies, arrays of structs and loops over them, pointers to structs, fields and elements, `*R` helper parameters, field reads and writes with every compound operator, and a checksum of every field before an entry returns. 21,000 programs agree with the reference interpreter under qemu. Four planted bugs are each caught: a field offset rounded down in the backend (by the differential test and the C struct test), a 2-byte field left unaligned in the layout (by the C struct test only, because the backend and the interpreter share the layout), a defaults loop that skipped the last element (by the IR test and a run test), and a dropped 4-byte tail in `copy` (by the differential test). Rules chosen while building, open to review:
       - Fields keep their declaration order and C's padding, as in C, Odin and Jai, so a struct passes to C unchanged. Zig reorders the fields of a plain struct to save padding and keeps C's order only for `extern struct`.
       - `p: Point` zeroes the struct and then applies the field defaults, as rank 6 recommends ([adopt-or-settle.md](adopt-or-settle.md) 2.6, still open). A field whose default is `undefined` stays zero for now.
       - A positional literal lists every field or none, and a named literal may leave fields out, which get their default or zero. Mixing the two forms is an error, as in Go, and so is naming a field twice. A field with no zero value (a pointer, an enum without a zero member, or a struct holding one) must be given unless it has a default; Zig asks that of every field without a default.
       - A literal evaluates its fields left to right, in source order, and stores them when it closes. A field that is itself a struct or an array is built in a hidden local and copied, as array literals are (3d.2); building it in place is a later optimization. Since 3d.7, a field given as an array or struct is copied before a later field's call can change it.
       - An anonymous literal takes its type from a typed declaration, an assignment, the field or element of an outer literal, or an array literal's element type. Without one it is an error that suggests writing the type.
       - `p.x` dereferences one pointer (the Pointers row), so `pp.x` with `pp: **Point` is an error. `.len` also reads through a `*[N]T`, but indexing doesn't: `pa[1]` is an error and `pa.*[1]` works.
       - `.x` on a number, `bool`, enum value or pointer now reports that the type has no member `x`; it used to report that the checker doesn't support it yet.
       - Still errors: structs passed or returned by value (3d.6 built them up to 16 bytes, and 3d.8 larger ones), fields of slice type (3d.5), `==` on structs (rank 3 recommends comparing fields), a field default that is itself a struct (it needs compile-time struct constants), and named arguments in calls.
       - Measured on the timing corpus, which has no structs: the machine code is byte for byte the same as 3d.3's. Callgrind on the whole corpus counts 0.07% more instructions in checking and 0.6% fewer in code generation. The checking cost is one more instruction in a test for `div` and `rem`, whose IR numbers moved when `field` was added, and a register spill in the loop over declarations. The saving in code generation is spread thinly over the backend's main loop, which clang compiled differently after the IR and type kinds were renumbered. The first version cost 0.40% in checking: a helper for typed declarations stopped being inlined when it gained a second caller, and field offsets were stored for every parameter and enum member, not only for struct fields.
     - **3d.5 Slices in memory: built 2026-11-19.** A slice in memory is 16 bytes, the pointer at offset 0 and the length at 8, aligned to 8: C's `struct { T *ptr; int64_t len; }`. Slice fields, arrays of slices (`[N][]T`), slices of slices (`[][]T`), `&s` on a slice local and `p.*` on a `*[]T`, each as a value and as a place, in literals (`[]T.[a, b]`, `Node.{ .kids = s }`) and in `for` loops whose variable is a slice. The IR gains `store-pair`, which stores a pointer and a length to an address; `load-at` of a slice type reads both words and names the length with `second`, as a call does. The arm64 backend reads with `ldp` and writes with `stp`, and stores `xzr` for a constant zero. `&s` keeps both of a slice local's slots in the frame, next to each other. The reference interpreter runs both. A C test passes structs that hold slices, and slices of slices, to compiled functions by pointer and checks every word; six more functions agree with the interpreter on a grid of inputs, traps included. The generator declares slice fields of every scalar type and writes them in literals, assignments, fill loops, `for` loops, `.len` and copies into locals. It writes arrays of slices, zeroed or from literals, slices of those (`z: [][]T = y`, `y[1..]`), element and whole-array copies, loops over rows and over elements, element reads and writes, `&s` and `&z[i]` with reads and writes through them, and a checksum of every slice field and array of slices. It stores a slice only into a place that can't outlive what the slice points to. 243,000 programs agree with the reference interpreter under qemu. Seven planted bugs are each caught. Swapping the two words in the backend's `ldp` or `stp`, or in the interpreter's pair load or `store-pair`, crashes the arm64 test run. Storing zero for a constant length of 1, and leaving the length slot out of the frame when `&s` is taken, each fail both the differential test and the C test. Reading a slice local where it is used instead of where it is named fails the IR test and a hand-checked C test; the differential test can't see it, because both sides run the same IR. Rules chosen while building, open to review:
       - A slice local is read where it is named, so in `f(s, g(&s))` the first argument is `s` as it was before `g` ran: strict left to right, as rank 3 recommends ([adopt-or-settle.md](adopt-or-settle.md) 2.3). A slice read only for `.len`, a slice parameter and a slice `for` variable are read where they are used, because nothing can change them in between; their IR is unchanged from 3d.3.
       - Arrays and structs in a literal are still read when the literal closes (3d.2, 3d.4), so `P.{ .a = arr, .b = g(&arr) }` stores `arr` as `g` left it, while the same literal with a slice stores the slice from before the call. The two rules should agree. Strict left to right, as rank 3 recommends, would copy each array or struct item into the literal before a later item can change it: the same copies, made earlier. Go leaves this order open: `x := []int{a, f()}` "may be `[1, 2]` or `[2, 2]`" ([spec](https://go.dev/ref/spec#Order_of_evaluation)). Structs passed by value (3d.6) raise the same question for arguments. Decided on 2026-11-19 for both, as option A (the Language row of that day): they are read where they are written, and step 3d.7 builds it.
       - A slice field counts as laid out whatever its element type is, because the slice is 16 bytes either way. So a struct can hold a slice of itself (`struct Node { kids: []Node }`) or of a struct declared after it.
       - `p.len` reads through a `*[]T`, but `p[i]` is an error and `p.*[i]` works, as for `*[N]T` (3d.4).
       - A compound assignment to a slice (`s += t`) is an error that names the operator. `==` on slices stays an error, and so does a slice as the value of a labeled block, which self-hosting doesn't need.
       - Measured on the timing corpus, which has no slices: the machine code is word for word the same as 3d.4's. Callgrind on the whole corpus counts 0.17% more instructions in checking and 0.01% fewer in code generation. In checking, most of the cost is one more argument on every read of a local, which tells the read whether `.len` follows; the rest is spread over `walk`, where clang allocated registers differently. The first version cost 0.28% in checking and 0.30% in code generation: every read also passed the file, and clang inlined the backend's pair load and store into `emit_function`. The file now comes from the body being checked, and both backend paths stay out of line.
     - **3d.6 Small structs by value: built 2026-11-19.** Struct parameters and results that fit in registers, passed as C passes them on arm64 (AAPCS64). A struct of one to four floats of the same type, nested structs and arrays included, travels in float registers, up to 32 bytes; any other struct of up to 16 bytes travels in one or two integer registers. A struct argument is a `load-at` of the struct's type, which the backend doesn't run on its own: it keeps the address and loads the registers at the call, with `ldp` for 16 bytes and float pairs, and with 4-, 2- and 1-byte pieces merged by `orr` for sizes such as 3, 7, 11 and 15, so it never reads past the struct's end. A callee stores a struct parameter to its frame slot on entry, if the body uses it. `call` gains a result slot in the caller's frame, printed `call P swap(%9) -> $2`; the callee loads the result into `x0` and `x1` or `v0` to `v3` from the address that `ret` names, and the caller stores those registers into the slot. `o := f(x)` uses `o` as the slot, so the result is not copied. The reference interpreter copies a struct parameter into the callee's frame and a result into the caller's slot. C tests pass 18 struct shapes from 1 to 32 bytes to compiled functions and back, odd sizes, mixed int and float fields, nested float structs and a struct holding a slice among them; pass 8 arguments that mix integers, floats and both kinds of struct; and replace five compiled functions with C ones that take and return structs, so compiled code calls C. Four functions agree with the interpreter on a grid of inputs. The generator works out each record's size, alignment and registers, makes 15% of records all `f32` or all `f64`, passes a record parameter by value half the time when it fits the registers left, returns a record from three helpers in ten when one fits, and uses calls that return records as arguments, as returned values, as declarations (`o := h(...)`) and for field reads (`h(x).f`). 156,000 programs agree with the interpreter under qemu. Twelve planted bugs: nine in the first version are each caught by both the C tests and the differential test (the shifts that merge or split an odd-sized struct, a float pair or an integer pair moved in swapped registers, a struct counted as one register, a result never stored, the interpreter copying a byte too few of a parameter, struct parameters read from register 0, and a returned struct loaded from register 1 up). Two in the search for the register of an argument computed right after a struct argument, counting a struct as one register and skipping the struct check, first crashed only the differential run; a C test of such calls (`tails`) now crashes too. The twelfth counts a struct's registers in the wrong class, which costs a move but gives the right answer, and no test sees it, because the tests check results, not code. A slice argument is never computed right after a struct argument, because struct arguments load at the call and slices are read before it, so the backend asserts that instead of handling it. Rules chosen while building, open to review:
       - A struct argument is read at the call, after every argument is evaluated, so `first(p, bump(&p))` passes `p` as `bump` left it. Arrays and structs in literals do the same (3d.2, 3d.4), and slices don't (3d.5). Strict left to right, as rank 3 recommends, would copy `p` before `bump` runs; the open question on literals decides both. Decided on 2026-11-19: `first` gets `p` from before `bump` ran (3d.7).
       - A call's struct result is a hidden local in the caller's frame, one per call: `f(x).y` reads it and `&f(x)` points into it until the caller returns. Assigning into it (`f(x).y = 5`, `f(x).x += 1`) is accepted and has no effect anyone can see. C, Go and Zig reject that; rejecting it is one check, and is the recommendation. Decided on 2026-11-19, as recommended: it is an error, "a call's result can't be assigned; copy it to a local to change it", and `o := f(x)` still gives a local that can be changed. The check runs on every assignment whose target is rooted in a hidden local, and its slow path is out of line. Callgrind on the timing corpus counts 0.32% fewer instructions in checking than 3d.7, all in `walk`, where clang allocated registers differently; with the slow path inline, the same check cost 0.06% more. The IR and the machine code are unchanged.
       - Struct parameters are read-only, as all parameters are: assigning to one or to its fields, or taking `&` of it or of a field, is an error that names it (`` `p` is a parameter, so it can't be assigned ``). Copy it to a local to change it.
       - A struct larger than 16 bytes, unless it holds one to four floats of one type, was an error until rank 8 decided how large structs travel; 3d.8 built it on 2026-11-19. Arrays by value are still errors; a struct can hold one.
       - A call that needs more than 8 integer or 8 float registers is a backend error. Rank 8 didn't need arguments on the stack: a struct over 16 bytes takes one integer register, for its address, and the compiler's own C needs at most 8 integer registers in a call (`assign_at`), so they wait until code needs them.
       - A function with an inferred return type may return a struct. A struct can't yet be the value of a labeled block (`b: { break b p }`).
       - `return .{ 1, 2 }` takes the declared return type; with an inferred return type it is an error that asks for the type, as other untyped literals do. The parser marks where a returned value starts, one more node for each `return` with a value. In an `=>` body the mark sits on the parameters' `)`, because the parser allows at most two nodes per token.
       - Measured on the timing corpus, which passes no struct by value: the machine code is word for word the same as 3d.5's. Callgrind on the whole corpus counts 0.11% fewer instructions in checking than 3d.5, 0.18% more in code generation, and 0.09% more in parsing, for the mark on each `return`. The first version cost 1.1% in checking and 2.2% in code generation: struct tests on every call and argument, which now run only on the compound path, and register allocation in the two big dispatch loops, where the struct cases in the backend's argument loop made clang keep the IR pointer on the stack in `emit_function` and reload it three times per IR instruction. The argument loop now runs out of line, and the search for an argument's register hands a call with a struct argument to a slow path. The checker's path for calls with no compound types dropped two checks that can't fail there, for an array or struct parameter and for a result too large to pass, because such a signature takes the compound path. That is why checking ends below 3d.5.
     - **3d.7 Arrays and structs read left to right: built 2026-11-19,** after the owner chose option A of the question that 3d.5 and 3d.6 left open (the Language row of 2026-11-19). An array or struct that is an item of a literal, or a struct argument, is read where it is written: `L.{ p, P.{ bump(&p), 0 } }` stores `p` as it was before `bump` ran, and `first(p, bump(&p))` passes it that way. The checker keeps a list of the array and struct places on its value stack that a literal or a call may copy later, each with its position: a local, a field, an element or `p.*`, but not a place that `.` follows, nor a local, field or `p.*` that `[` follows. Before a call, before the right side of `and` and `or`, and at the start of a value block or a `switch` used as a value, it reserves four `nop`s for each place on the list below that point, unless the place is rooted in a parameter or a `for` variable, which nothing can change. When the literal or the call takes the item, the `nop`s become a copy if a call ran or a value block started since they were reserved. Otherwise they stay `nop`s, and the item is copied when the literal closes, or loaded at the call, as before. The copy goes straight into the literal's field or element, so it adds no copy. A struct argument, and an item of a literal that leaves fields out, is copied into a hidden local instead, because the backend loads struct arguments at the call and the literal zeroes its fields after its items: one copy more, made only when a call or a value block comes between. Reserving the copy before `and`, `or` and `switch` makes it run whichever way the branch goes. Tests: 18 hand-checked functions run under qemu on a grid of inputs, with arrays, struct fields, named and zero-filled literals, `and`, `or`, value blocks, `switch`, `p.*`, elements, nested fields, loop variables and parameters, and an IR test pins each shape of copy. 45,000 programs agree with the reference interpreter under qemu, but the differential test runs the same IR on both sides, so it checks that the copies compile, not when they run; about 15 generated programs in 2,000 fill a copy. Of 12 planted bugs, the IR test alone catches the 2 that only add `nop`s or move a copy, and the hand-checked functions catch the other 10: 9 by their hand-computed results, and one, which leaves a field unwritten when a `switch` arm skips the copy, only when the same functions are compared with the reference interpreter, because the stack still held the right value from an earlier call. One bug was found by reading the code, not by a test: a named item moves one place down the stack, and it could land on an entry left by an index whose own index called a function, as in `Y.{ .n = m[0][k[0][f(&k)] & 1], .p = p, .m = g(&p) }`, and lose its copy. A function that fails without the fix is now one of the 18. Measured on the timing corpus, which has no array or struct items: the IR is line for line the same as 3d.6's, so the machine code is too. Callgrind counts 0.15% more instructions in checking and the same number in code generation. The test for pending copies at each call and at each `and` and `or` costs 0.14%, and keeping each function body's list apart from the body that asked for it costs 0.04%; the rest of `walk` got 0.02% cheaper. Rules chosen while building, open to review:
       - An array passed where a slice is expected is not copied, because the slice points at the array: `total(r, grow(&r))` sees `r` as `grow` left it, as Go does with `r[:]`. The same holds for an array given to a slice field of a literal.
       - An index reads its element when it is applied, after the index is computed, so `r[grow(&r)]` reads the element that `grow` left. Copying `r` before the call would cost a whole array to read one element. Go leaves this order unspecified: in its spec's example, "the order of those events compared to the evaluation and indexing of `x`" is not specified ([spec](https://go.dev/ref/spec#Order_of_evaluation)).
       - Only a call or a value block can write memory inside an expression today. A later construct that writes memory or branches inside an expression (`??`, `catch`, `try`, or `++` if it becomes an expression) must reserve the copies at its start, as `and` and `or` do. A global, when globals come, must join the list as a local does, and struct `==` must read its left side the same way.
       - `m[0][twice(&m)]` leaves four `nop`s unused, because the checker lists the row `m[0]` before it sees that an index follows. For a local it can look at the next token, so `r[grow(&r)]` reserves nothing.
     - **3d.8 Large structs by value: built 2026-11-19,** after the owner chose copies for rank 8 (the Language row of 2026-11-19). A struct over 16 bytes that isn't one to four floats of one type travels as C passes it on arm64: the caller passes the address of a copy in the next integer register, and a result comes back through memory the caller provides, whose address goes in `x8`. That memory is the slot 3d.6 gives each call, a hidden local or the local that `o := f(x)` declares, so the callee writes the result in place and nothing moves after the call. The caller skips the copy when nothing the callee can reach is able to write the original. The checker records each large argument's root as the second operand of its `load-at`, printed `load-at W %19, w`: the local it is read from, or none for `p.*`, a field of `p.*` or a slice element. It marks a local whose address is taken, wherever that happens: `&` of the local or of a field or element of it, slicing it, and passing it as a slice. The backend copies an argument only when its root is unknown or marked, and a mark anywhere in the function counts, before the call or after it. So a parameter (read-only, and never `&`'d), a `for` variable (already a copy), a hidden local (a call's result, a literal, a 3d.7 copy) and a named local whose address is never taken pass their own memory. The checker sizes a copy area for each function: the largest sum, over its calls, of the 8-byte-rounded sizes of the arguments rooted in a named local or in nothing, printed `copies: 48 bytes`. The backend puts it above the locals and asserts that no call uses more. The callee treats a large parameter as an 8-byte slot that holds its address, pinned in a register or kept in the frame like any other value, and `addr` of it gives that address. A function that returns a large struct stores `x8` in its frame on entry, and `ret` copies the value through it. The reference interpreter still copies every struct parameter on entry, so it checks the copies independently, and it writes a struct result straight into the caller's slot. Tests: C calls seven compiled functions that take and return large structs of 17 to 320 bytes (three ints, 17 bytes, five `f32`, five `f64`, a struct holding a slice, 72 and 320 bytes), and one that takes eight arguments of every kind. 21 hand-checked functions run under qemu on a grid of inputs and agree with the interpreter: aliasing through `&w`, `&o.w`, a slice of a field, an array passed as a slice and `p.*`, results assigned back to their argument, calls in loops, chains of large results, a 320-byte struct, a function that copies an argument and returns a large struct, three copied arguments in one call, and a parameter that register pressure keeps in the frame. Then four compiled functions are replaced with C ones, so compiled code passes large structs to C and gets one back. One of them writes 999 into its parameter, which shows which of 9 callers pass the original: a local, a field, an element, a `for` variable and a parameter do, and a local whose address is taken, before or after the call, a sliced array and `p.*` get a copy. An IR test pins the roots and the copy area. The generator now passes records of any size by value. A helper that takes a record by pointer often takes one of the same type by value too, and its calls then pass the same place to both, as in `h(&o, o)`, and arguments name a local, a field or an element more often. Lifting the size limit exposed an exponential blowup, where a record literal's fields called helpers whose record arguments were literals again; field values inside a call's arguments now use depth 0, and the largest of 90,000 programs is 28 KB. 120,000 programs agree with the interpreter under qemu. Of 13 planted bugs, the hand-checked functions catch 8 by their results: a copy skipped despite the mark, a copy skipped for `p.*`, a copy 8 bytes short, two copies at one offset, `&` of a field, a slice or a conversion to a slice that leaves the local unmarked, and the address of a parameter kept in the frame computed instead of loaded. Four crash the run: `x8` never set, `x8` saved inside the copy area, a pinned parameter read from the frame, and 16-byte structs sent by pointer, which C doesn't do. A copy area sized by the last call instead of the largest trips the backend's assert and the IR test. Four of the hand-checked functions were added after their bugs passed every test. The random programs catch none of the 13: only about 50 calls in 3,000 programs copy an argument, and fewer change the original during the call. Measured on the timing corpus, which passes no struct over 16 bytes: the IR and the machine code are byte for byte the same as before this step. Callgrind counts 0.02% more instructions in checking and 0.15% more in code generation. The first version cost 5.4% in code generation. Most of it was a test, in each of seven frame layout loops and for every slot, of whether the slot was a large parameter. The backend now finds those parameters once per function and, only when there are some, gives the layout loops a copy of the slot list that types them as 8-byte integers: 1.9%. Keeping the copy offset in the emitter, and setting `x8` in the out-of-line result paths: 1.6%. The rest was register allocation in `emit_function`. Inlining the large-parameter path of `addr`, with its own 8-byte load, brought it to 0.15%; moving all of `addr` out of line cost 4.3%, and calling the shared load helper from that path cost 2.8%, because clang then stopped inlining the helper where `emit_function` loads a local. Rules chosen while building, open to review:
       - The checker sizes the copy area before it knows which locals get marked, since a mark can come later in the function, so it reserves room for every argument rooted in a named local. The backend uses only the room the marked ones need. Unused room costs stack, not time.
       - Skipping a copy assumes that only chibi code runs during the call. AAPCS64 lets a callee write its copy, so when chibi calls C (a self-hosting step), every large argument to C must be copied. A C function that replaces a compiled chibi function, as in the tests, must not write a large parameter.
       - A parameter is passed on without a copy: it points at its caller's copy or at a local nobody marked, and neither can change while the calls run.
       - A large result lands where 3d.6 puts a struct result. `w = f(w)` still goes through a hidden local and a copy into `w`, and `return f(x)` copies `f`'s result through the function's own `x8`. Writing in place would skip both copies; it is a later optimization.
     - **3d.9 The hidden context: built 2026-11-20,** after the owner chose to pass it as a hidden first argument in every backend (the Language row of 2026-11-18). Every chibi function receives the context in `x0`, and its integer arguments start at `x1`: a scalar, both halves of a slice, a small struct's registers and a large struct's address all move up one register. Float arguments, and `x8` for a large result, stay where C puts them. Nothing reads the context yet, so the IR doesn't show it and the reference interpreter doesn't model it: each function passes on the context it received. A function without calls ignores it, so `x0` is one more free register for its locals. A function with calls leaves `x0` as it arrived for a call that no other call can precede: none earlier in the code and no loop around it. Before every other call it reloads the context with `ldr x0, [x29, #16]`, because the call before returned its result in `x0`. A function that reloads pushes a 32-byte frame record instead of 16 and stores `x0` in the extra half in its prologue, so the context sits at the same place in every frame, one instruction away however large the frame is. C that calls compiled code passes the context first, and a C function that replaces a compiled one, as in the tests, receives it first.
       - Tests: every call from C into compiled code in the arm64 test, 143 of them, passes a context, and the nine C functions that replace compiled ones in the struct tests count every call that arrives with another context. A new test replaces five compiled functions with C ones that check the context and take an `int`, a 24-byte struct by pointer with a result through `x8`, a slice, an `f64` with an `int`, and five `int`s with a 16-byte struct that fills `x7`. Eight compiled functions call them for inputs 0 to 19 and give the hand-computed results with the context intact: one call, two calls, a call as another call's argument, a call in a loop, a call in an `if` in a loop, calls in both arms of an `if`, a compiled function that calls two others, and four calls of different kinds in a row. C calls a compiled function with seven `int` parameters. The test also reads the machine code: how many reloads and stores each function has, the 32-byte frame record wherever there is a store, and no moves in a function without calls that takes seven parameters. Three programs that need an eighth integer register fail with the limit's message: eight `int` arguments, six `int`s and a 16-byte struct, and eight `int` parameters. Two older tests used eight integer parameters and now use seven. The tests catch all 22 of 22 planted bugs. Without the machine-code checks they still catch 20: 12 by a wrong result or a context that arrives changed, 4 by the limit messages, and 4 by crashing or hanging the run: a frame record pushed as 32 bytes and popped as 16, the reverse, a flag left set from the function before, which makes a function without a frame record store `x0` into its caller's frame, and a value after a struct argument computed one register off. The other 2 only cost instructions, a reload before a function's first call and seven moves in a function without calls, and only the machine-code checks catch them. Those checks also fail first for 12 of the 20, before the run crashes or a result goes wrong.
       - Measured on the timing corpus: 8.05% more instructions in the functions, counted from each entry to its return (3,605,381 against 3,336,863), and 7.08% more words in the code mapping, because the prologue's store fills a word the prologue already reserves. The added instructions are 232,307 reloads, 19,818 stores and 16,516 moves of a call's result from `x0` to `x1`, where the next call takes it as its first argument; before, both were `x0`. Of 249,386 calls, 17,079 need no reload, and 136 of the 19,954 functions with calls need no store. The IR is unchanged. Callgrind counts the same instructions in checking and 0.51% more in code generation, all of it in `emit_function`, which writes the added instructions. Timing the generated code needs the owner's Mac.
       - Rules chosen while building, open to review:
         - The context takes `x0`, so 7 integer arguments fit in registers instead of 8; floats keep 8. A call or a function that needs more is an error until stack arguments are built, as before.
         - The context is the first argument, as the owner chose, so a call's result moves from `x0` to `x1` when it goes straight to the next call as its first argument: the 16,516 moves above, 0.5% of the code. A context passed last would avoid them.
         - The context sits in the frame record, at `[x29, #16]`. A callee-saved register (`mov x19, x0` on entry, `mov x0, x19` before a call) costs the same instructions and takes a register from locals, and a slot at `[sp]` costs the same instructions too.
         - A function reloads before every call after its first one in the code, even when the earlier call is on a path that can't reach the later one, as in an `if` and its `else`: one pass doesn't know where paths join. Every call in a loop reloads.
         - A function without calls may overwrite `x0`. The step that lets code read the context must keep it in the functions that read it.
         - Calls into C will pass no context (the 2026-11-18 row). Until that step, a C function that replaces a compiled one receives the context as a chibi function does.
   - **3e. Optionals;** questions 14 and 16 decided.
   - **3f. Errors:** `E!T`, `try`, `catch`, `defer catch` and inferred sets; question 16 decided.
   - **3g. Globals and constant tables,** the first step after 3d in the self-hosting order below, built in two parts: constant tables (3g.1), then global variables (3g.2), after the owner chose that globals start at compile time (the Language row of 2026-11-19).
     - **3g.1 Constant tables: built 2026-11-19.** A top-level `const` can now be an array or a struct: `const squares := int.[0, 1, 4, 9]`, `const grid := [3]int.[.[1, 2, 3], .[4, 5, 6]]`, `const box := Box.{ .lo = .{ 1, 2 }, .hi = .{ 30, 40 } }`, or a typed form such as `const t: [3]u8 = .[7, 8, 9]`. Elements and fields must be constants: numbers, `bool`, enum values, names of constants, and other constant arrays and structs. A struct constant gets its field defaults and zero for the rest, as `p: Point` does in a body (3d.4). The checker writes each constant's bytes, laid out as in memory and aligned to the type, into one buffer for the package (`Ir.data`), and the constant's value is its offset there. In a function body, naming an array or struct constant emits `data`, the address of its bytes, printed `%0 = data *[8]int squares`. It is a place like `addr`, so indexing, fields, `.len`, `for` loops, copies into locals and arguments work as they do on locals. The arm64 backend puts the buffer in the same mapping as the code, between the function table and the code, and computes an address with `adrp` and `add`. The reference interpreter reads the checker's buffer. A constant struct over 16 bytes passes to a function without a copy (3d.8), because nothing can write it: its `load-at` root is `DATA_ROOT`, which the backend reads as a place that never changes. The declarations dump prints a constant array or struct as a literal: `const deep: Deep = .{ 9, 0.5, .{ 0, 0 }, .[-1, 2, -3], 200 }`.
       - Fixed while building: a nested anonymous literal at top level, such as `.lo = .{ 1, 2 }` inside `Box.{ … }`, couldn't find its type, because the checker resolved the outer struct only when its literal closed; it now resolves a literal's type when the literal opens. Top-level checking read the package's first file to decide whether a literal was an array, so a literal in another file could be misread; it now reads the file of the declaration it checks. Slicing a field of a parameter said "`a` is a loop variable"; it now names the parameter.
       - Tests: a program of 26 constants covers tables of every integer width, signed and unsigned, `f32`, `f64`, `bool` and enums, rows taken over in place, an alias (`const same := grid`), structs with defaults and nested literals, a 24-byte and an 8 KB struct passed by value, a 1.6 MB struct that puts the tables declared before it more than a megabyte from the code, and a 3-byte table that would misalign the next one. Under qemu, 32 functions give the hand-computed results on a grid of inputs and agree with the interpreter, and two more trap on indexes out of range. C calls a function that returns a 24-byte constant, and the test checks that 12 constants are aligned to their types. Checker tests cover the declarations dump, the IR, every new error and a two-file package. The tests catch all 17 planted bugs: `adrp` without its `add`, a wrong `adrp` immediate, the data copied 8 bytes off, field defaults or the zero fill skipped, a field at the wrong offset, rows taken over when they aren't adjacent, items stored before conversion, the literal's type resolved late, the first file's tokens, a copy of a constant argument in the checker or in the backend, unsigned printing, the wrong name in an error, `&` allowed on a constant, the IR printer reading the data root as a slot, and alignment. Two of them passed at first. The alignment check is new, and the backend now reads a slot's mark only for a real slot number: a backend that forgets the data root then copies the argument and trips the copy-area assert, instead of reading another function's slot. The random-program generator doesn't write constant tables yet.
       - Measured on the timing corpus, which has no constant tables: the IR and the machine code are byte for byte the same as 3d.8's. Callgrind counts 0.20% fewer instructions in checking and 0.03% more in code generation. The first version cost 1.24% in code generation, all of it register allocation in `emit_function` after the `data` case was added. Choosing the result register inside the out-of-line address helper, instead of at the case, removed it. Marking the helper `NOINLINE` alone, moving the emitter's new field to the end, or numbering `data` last changed nothing.
       - Found while testing and not fixed: `?int` as a value crashes the checker, with an assert at top level (`const t := ?int`) and a 16 GB allocation in a body (`x := ?int.[1, 2]`).
       - Rules chosen while building, open to review:
         - A constant array or struct is read-only. Assigning to it or to its elements or fields, `&` of it, and slicing it are errors that name it (`` `T` is a constant, so a slice can't point into it ``), until rank 10 decides read-only data. So a constant array can't be passed where a slice is expected, `const s: []u8 = .[1, 2]` is an error, and a slice field of a constant can only be empty. `t := squares` gives a local copy that can change.
         - Indexing a constant with a constant index isn't folded: `T[1]` in a body loads from the table, and at top level `T[1]`, `P.x` and `T.len` are still "not supported yet". In a body `T.len` is a constant, but the `data` it names stays in the IR as a dropped value.
         - A package's constants share one buffer of at most 256 MB, and a constant that would pass that is an error. An inner literal's bytes stay in the buffer after the outer literal copies them, except in an array of arrays or structs whose items are adjacent, which the array takes over in place. The waste is at most the size of the inner literals at each depth.
         - The buffer shares the code's mapping, which is writable on arm64 Linux. Read-only pages wait for rank 10.
         - Still errors: a field or parameter default that is an array or struct (`p: Point = .{ 1, 2 }`, `fn g(p: Point = P)`), `const` inside a function body, and a string inside a constant struct (`Str.{ .s = "hi" }`).
     - **3g.2 Global variables: built 2026-11-20.** A top-level `counter := 5`, `origin: Point` or `ratio: f64 = 1.5` in a library file declares a global. Its starting value must be a constant, as a `const`'s must until compile-time execution is built: numbers, `bool`, enum values, names of constants, and constant arrays and structs, so `copy := T` starts as a copy of the table `T`. A global without a value starts as a local does: zero bytes with its field defaults (3d.4). The checker writes the starting bytes of each global that has a value or field defaults into one image for the package (`Ir.globals`), laid out and aligned to its type, and the global's value in the declarations table is its offset there. A global with neither, or with `= undefined`, takes no bytes in the image: the checker counts it in a zeroed part, and once every declaration is checked, it places that part after the image. So `pad: [100000]int` adds nothing to the image. In a function body, naming a global emits `global`, its address, printed `%0 = global *int counter`. It is a place like `addr`, so assignment, compound assignment, fields, elements, `&`, slicing, `.len` and `for` work on a global as they do on a local. A scalar global is loaded where it is named, so `counter * 100 + incCounter()` reads `counter` before the call. A global array or struct read as an argument or a literal item is copied before a later item runs (3d.7), and a global struct over 16 bytes passed by value is always copied (3d.8), because the callee can write the global: `place_root` gives a global no root. The arm64 backend maps the image and the zeroed part together, in a read-write mapping of their own, and copies the image in. The kernel zeroes the rest, so those pages cost nothing until the program touches them. The backend puts a global's address in a register as a 64-bit constant, with `movz` and `movk`, because the mapping can land anywhere. The reference interpreter keeps its own copy of the image. The declarations dump prints a global's type and starting value, `corner: Point = .{ 3, 4 }`, and only the type of a zeroed one, `zeros: [4]int`. The errors for assigning to, or taking the address of, something that isn't a place now say "only a variable" instead of "only a local".
       - Tests: the checker tests cover the declarations dump of 13 globals, with inferred and given types, field defaults, `undefined`, a slice and a copy of a constant table, and every new error: a global read in a constant, in another global's starting value and in an array length; a pointer, an enum and a struct that have no zero value and no starting value; a constant array given to a slice; and the 2 GB limit, passed by one global, by the zeroed part, and by the image after the zeroed part, next to a global that still fits. They pin the IR of compound assignment, a field write, `&` of a field and a 24-byte global copied before a later argument's call, and check a package whose function reads globals declared in another file. The entry file still rejects globals, and a library's `x := 5` is now a global where it was an error. The arm64 test program declares 23 globals: `i8`, `u8`, `i16`, `u32`, `u64` and `int`, `f32`, `f64`, `bool`, an enum, structs with defaults, an array of them, a table, zeroed arrays, `undefined`, a 100,000-element array, a slice, a 32-byte struct, a copy of a constant table, and a 3-byte array that would misalign the zeroed global after it. Under qemu, 18 functions give the hand-computed results on a grid of inputs and agree with the interpreter, and four whose results depend on earlier calls agree with it when both start from a fresh build. C reads `after` straight from the mapping, writes `counter` and `corner` through pointers that compiled functions return and reads them back through compiled code, gets a 16-byte and a 32-byte global back by value, and sees bounds traps on a global table. The test checks that the 23 globals are aligned to their types and inside the mapping, that globals with a starting value are in the image and zeroed ones after it, and, in a second program, that two globals of size 0 have distinct addresses that aren't 0. The tests catch all 24 planted bugs: the address taken from the constants buffer or cut to 32 bits; the image not copied, or copied 8 bytes off; a mapping that covers only the image; zeroed globals not moved past the image, not marked, not sized or not aligned; image globals not aligned; field defaults skipped, or given to `undefined`; the padding in the image not zeroed; a starting table copied from the wrong place; a global array argument not copied before a later call; a scalar global loaded late; a global read at top level without an error; the image's size check removed; globals allowed in the entry file; the dump printing a zeroed global's value; the interpreter not copying the image; the backend skipping the copy of a global struct argument; a global's type not claimed before its layout is read; and a global of size 0 taking no byte. Three of them crash the run instead of failing a check: the 32-bit address, the short mapping, and the size-0 global. Two passed at first: zeroed globals placed without alignment, and the image's size check removed. The 3-byte array and the limit cases were added for them. The random-program generator doesn't write globals yet.
       - Measured on the timing corpus, which declares no globals: the IR and the machine code are byte for byte the same as 3g.1's. Callgrind counts 0.02% more instructions in checking, all of it in `check_decl` and `check_package`, and 0.01% fewer in code generation. Numbering `global` right after `data` first cost 0.08% in checking: it moved `div` and `rem` from 13 and 14 to 14 and 15, and clang then tests for that pair in `binary` with one more instruction. `global` now comes just before `call`, which leaves every kind before it at its old number and keeps the printer's rule that the kinds before `call` give a value. `global_address` is out of line and picks its own result register, as 3g.1 found `data_address` must.
       - Found while testing and not fixed: the arm64 test's trap handler jumps to the buffer of the last `call_i2`, even when the trap comes from a sanitizer check in C code after that call has returned. A sanitizer failure in the interpreter or the backend then crashes the run with SIGSEGV instead of naming the check. The size-0 bug above crashes this way.
       - Rules chosen while building, open to review:
         - Globals in the entry file are still "not supported yet", because what `name := value` means among the entry file's code is open ([top-level-code.md](top-level-code.md) 3). The arm64 tests now check their programs as library files, so the programs can declare globals.
         - A global with no value and no field defaults, or with `= undefined`, lives in the zeroed part, so `undefined` gives zero bytes and skips field defaults. The decision of 2026-11-10 says debug builds fill `undefined` memory with a byte pattern; there are no debug builds yet, and locals don't get the pattern either.
         - A starting value can't read another global (`copy := counter` is "expected a constant"), and can't be a string yet. No constant is a pointer, so a pointer global must be `= undefined`, which leaves it nil until something writes it, and a slice global starts empty. `?T` and union globals are "not supported yet", as those types are elsewhere. An array or struct literal given as a starting value goes into the constants buffer first, under its 256 MB limit, and its bytes stay there after the image copies them.
         - All globals together take at most 2 GB. The check keeps 16 bytes for the padding between the image and the zeroed part, so it can reject a package up to 16 bytes early.
         - A global of size 0 takes one byte, so its address is never 0 and differs from every other global's, as C++ does for empty objects. Without it, a package whose globals all had size 0 got no mapping, and `&g` was a nil `*T`.
         - Every global struct over 16 bytes passed by value is copied, and every global array or struct read as an earlier argument is copied before a later argument's call, because the checker doesn't know which functions write which globals.
         - A global's address takes up to four instructions on arm64, where a constant's takes two (`adrp` and `add`), because the globals mapping isn't near the code. Mapping it within 4 GB of the code would allow `adrp`. The C backend will leave global addresses to the linker.
         - `counter += 1` keeps the global's address in a hidden pointer slot, as `p.* += 1` keeps `p`. Computing the address again would skip a store and two loads.
         - A local with a global's name hides the global without a warning, as it hides a constant.
         - The zeroed part's offsets are final only after the last declaration is checked. That holds because every body is checked after every declaration; step 4's threads must keep that order, or place zeroed globals as they are declared.
   - **3h. Calling C and being called from C,** item 3 of the self-hosting order, built in five parts: calls into C (3h.1); stack arguments and C's variadic calls, which `fprintf` needs (3h.2); callbacks that C calls, such as `qsort`'s comparator (3h.3); `export fn` with C's convention (3h.4); and `[*]T`, the pointer into a C array that rank 5 brings with calls to C, with the entry point, because C's `main(argc, argv)` needs `[*]T` for `argv` (3h.5).
     - **3h.1 Calls into C: built 2026-11-20.** `extern fn labs(x: i64) -> i64` declares a C function: a signature with no body, at the top of a file, which `pub` may precede. It is spelled as sheet 10a recommends (option H), as Zig, C3 and Zen C spell it. Without `->` it returns nothing, as a `pub` or `export` function does. The parser reports "`extern` must come before `fn`" and "an `extern fn` has no body". The checker gives the declaration no body and sets a bit for C's convention in its function type, above the 32 bits that hold the result type, so code that reads the result type sees no change. A slice or an array in the signature is an error, "C has no `[]u8`, so an `extern fn` can't pass or return it", because C passes neither by value. A call to an `extern fn` emits `call-c`, a new IR kind after `call` with the same operands, printed `%1 = call-c int labs(%0)`. A struct over 16 bytes passed to C is always copied, even a local that nothing else writes and a constant table, because the C function may write its copy, and constant tables sit in read-only memory. The arm64 backend calls C with C's convention: no context and integer arguments from `x0`, so 8 integer and 8 float arguments fit in registers, and structs go as 3d.6 and 3d.8 pass them. A call into C never reloads the context. A chibi call after one reloads it, because C doesn't keep `x0`, and a function whose only calls go into C keeps no context and pushes the 16-byte frame record. A C result narrower than 64 bits goes into the form chibi keeps in registers, as parameters already do on entry, because C may leave the upper bits set: `sxtb`, `uxtb`, `sxth` or `uxth` below 32 bits, and `mov w, w` at 32. The first call to an `extern fn` that the backend compiles looks its name up with `dlsym` in the running process and writes the address into the function table, so a declaration that nothing calls is never looked up. The reference interpreter doesn't run `call-c`.
       - Tests: the parser tests cover `extern fn` with and without `pub`, and both errors: for `extern struct` and a nameless `extern fn`, and for a body after `{` or `=>`. The checker tests cover the declarations dump, `extern fn labs(x: int) -> int` and `pub extern fn quit() -> void`; the error for a slice, an array and an array result in one signature; an `extern fn` inside a body, which is "not supported yet"; and the IR of a call. The arm64 test program declares 16 C functions: `labs` and `memset` from libc, and 14 in the test file, which the Makefile now links with `-rdynamic` so that `dlsym` finds them. Under qemu, 23 compiled functions call them and give the hand-computed results, 15 of them for inputs 0 to 19: a C call before, after and inside chibi calls and loops; eight `int` arguments in `x0` to `x7`; `i8`, `f64`, `u16`, `f32`, `i32` and `bool` mixed, which C reads back from the registers; `u8`, `i8`, `u16`, `i16`, `u32`, `i32` and `bool` results from C functions that leave the upper bits set, returned as they are and used in arithmetic; a 16-byte struct in and out in registers; a 24-byte struct that C writes through its copy, from a local and twice from a constant table; a 24-byte result through `x8`; a `pub extern fn` without a result, in a loop; and `memset` on a local array. A C function replaces the compiled `h` and counts the calls that arrive without the context, including calls after a call into C. The test also reads the machine code: how many reloads and stores of the context six functions have, and no `mov x7` in the function that passes eight arguments. Three programs fail with the backend's messages: nine `int` arguments to C, seven `int`s and a 16-byte struct, and an `extern fn` that names no loaded function. The tests catch 25 of 28 planted bugs: 6 by a wrong result or a wrong value in C, 4 by the machine-code checks, 5 by the backend's messages, 7 by the checker and parser tests, and 3 by an assert or a crash. The assert checks that a call's kind matches its callee, which turned one of those crashes into a named failure. The other 3 cost time at most, and no test can see them: a name looked up again at every compiled call; an argument register one off, which costs a move, on a path that no call in the tests reaches; and a field address at offset 0 that takes over its register before a C call, which changes no instruction in the test program.
       - Measured on the timing corpus, which calls no C: the IR and the machine code are byte for byte the same as 3d.9's. Callgrind counts 0.36% more instructions in checking and 0.82% more in code generation. The new work on the corpus is a few tests at each call and each declaration; the rest is clang inlining and allocating registers differently in `walk`, in `int_fits`, which it no longer inlines, and in `emit_function`. A first version without the new IR kind, whose backend read the bit from the callee's type at each call, cost 0.13% in checking and 1.61% in code generation. One that dispatched `call` and `call-c` in two places cost 1.84% in code generation, because clang then moved `call` out of `emit_function`.
       - Rules chosen while building, open to review:
         - `extern fn` is sheet 10a's option H; question 4 of 10a is still the owner's.
         - The backend finds a C function by name in the running process, at the first call it compiles, and stops with "an `extern fn` names a C function that is not loaded" when the name isn't there. The message doesn't name the function, because the backend's errors are fixed strings so far. Nothing loads a library yet: that waits for 10a's questions on where libraries are named. Only Linux under qemu has run it; `dlopen(NULL)` on macOS should find the same names, but that is untested.
         - A C function's name is its chibi name, of at most 255 bytes. A C function named like a chibi keyword can't be declared yet (10a's escape).
         - Every struct over 16 bytes passed to C is copied: C's convention lets the callee write its copy, and the copy of a constant table keeps that write away from read-only memory.
         - A C result is trusted to hold a value of its type once extended: a `bool` is its low byte, which C's convention keeps 0 or 1, and a `*T` is trusted not to be nil. A C function that can return NULL can't be declared correctly until optionals (3e) give `?*T`.
         - An `extern fn` is allowed only at the top of a file; inside a body it is "not supported yet".
         - The C convention is a bit of the function type, so an `extern fn`'s type differs from a chibi function's with the same signature. 3h.3 spells the C kind `extern fn(...)` and converts a named chibi function to it; chibi function values wait for rank 13.
         - `call-c` is a kind of its own rather than a flag on `call`, so the backend chooses the convention without looking at the callee (the measurements above). It renumbers every kind after `call` by one.
         - The reference interpreter doesn't run `call-c`, so the differential test can't generate calls into C. The interpreter would need to call C functions by signature, as libffi does.
         - Stack arguments and variadic calls into C came in 3h.2.
     - **3h.2 Stack arguments and C's variadic calls: built 2026-11-20.** Arguments that don't fit in registers go on the stack as AAPCS64 places them, in all three directions: chibi to chibi, chibi to C and C to chibi. A chibi function takes 7 integer arguments in `x1` to `x7` after the context and a C function 8 from `x0`, and both take 8 float arguments. Past those, a scalar takes an 8-byte slot, a struct of up to 16 bytes goes whole in slots of its size rounded up to 8, and a larger struct goes as the pointer that 3d.8 passes. Once a struct doesn't fit in the registers of its class that are left, no later argument of that class uses a register (AAPCS64 rules C.3 and C.11), and a slice is never split between `x7` and the stack. The caller stores stack arguments at the bottom of its frame, below its locals. The checker records the size of that area in each function's IR, printed as `stack arguments: 72 bytes`, so the one-pass backend lays out the frame before it emits code. A function with stack parameters pushes the 32-byte frame record, as one that saves the context does, and reads them at `x29 + 32` and up; a struct that arrives in stack memory is copied into its slot. The backend's errors for more than 7 integer or 8 float parameters, and for calls into C that don't fit in registers, are gone. `extern fn printf(fmt: *u8, ..) -> i32` declares C's variadic tail with a bare `..`, as sheet 4e recommends (option E). The parser reports "only an `extern fn` can end with `..`" and "`..` must be the last parameter". The checker sets bit 33 of the function type beside the C bit, and the declarations dump prints `(fmt: *u8, ..)`. Arguments after the named ones get C's default promotions: an untyped integer becomes `i32` when it fits, else `i64`, else `u64`, as C types a literal, and an untyped float or any `f32` becomes `f64`. `bool`, narrow integers, enums and pointers pass as they are: the arm64 backend keeps narrow values extended to 32 bits, which is what C's promotion to `int` gives, and generated C would promote them itself. A struct, a slice or an enum literal is an error, "C's `..` takes numbers, `bool`, enums and pointers, not `P`", and too few named arguments give "wrong number of arguments: expected at least 1, found 0". In the IR, the variadic operands follow the named ones in `call-c`, and the callee's parameter count tells where they start: `%8 = call-c i32 printf(%0, %1, %6, %3, 1, 3000000000, 18446744073709551615, -1, 2.5, %7)`. On Linux, variadic arguments travel as named ones do. Apple's arm64 convention differs in two ways: every variadic argument goes on the stack in an 8-byte slot, and named arguments on the stack are packed at their own size and alignment, an HFA of three `f32`s in 12 bytes. `arm64_compile` takes a `Convention`, and `CONVENTION_HOST` picks Apple's on macOS, so the Linux tests compile Apple's layout too and read it back. The checker sends calls that need the stack or take `..` down the path that already handled structs and slices, renamed `call_general` with the flag `DECL_GENERAL_CALL`, and decides that once per declaration, so other calls do no new work.
       - Tests: the parser tests cover `..` after a parameter and alone, and both errors, each in two places. The checker tests cover the declarations dump; one program with seven errors: too few arguments, and a struct, a slice, an integer too large for `u64`, a type, an enum literal and a struct literal passed to `..`; the IR of a `printf` call with 72 bytes of stack arguments and two `f32` values widened to `f64`; a variadic call that uses a default; the IR of chibi calls with eight integers and a float, and with a slice after six integers; and the types of promoted constants (`i32`, `i32`, `i64`, `u64`, `f64`), read from the IR, because `i32` and `i64` pass in the same 8 bytes on arm64 and no run can tell them apart. Under qemu, two programs give hand-computed results for inputs 0 to 19: 28 functions with stack arguments and 5 with variadic calls, beside 9 C functions in the test file. They cover twelve arguments with an `i8`, a `u16`, an `i32` and a `bool` on the stack, from C, from chibi and into C; two 16-byte structs past the registers with an `int` after them, which rule C.11 sends to the stack; three `f32`s in a struct past the float registers with an `f32` and an `f64` after them (rule C.3); a slice that doesn't fit in `x7`; a 24-byte struct passed by pointer on the stack, from a local, from a constant table, beside a pointer to the same local, and to a C function that writes its copy; 33 integers and a 24-byte struct, so a by-pointer parameter sits past the 32 that the backend's mask covers; a function whose only stack arguments are structs; narrow stack arguments that a function keeps in callee-saved registers; and C functions that record the raw stack words. `snprintf` with 12 conversions matches C's output, a C function reads 20 variadic arguments in pairs with `va_arg`, and a variadic call passes a struct as a named argument. The same programs compiled with Apple's convention give the same results for chibi calls, and the recorded words show the packed layout and the variadic slots. The machine-code checks count the frame records and context stores of two functions with stack parameters. The two programs of 3h.1 that failed with too many arguments for C now compile, so those checks are gone. The tests catch all 35 planted bugs: 11 by the checker tests, 2 by the parser tests, 9 by a wrong result or a wrong value seen by C, 8 by an assert, 4 by a crash and 1 by the machine-code checks. The first run caught 26 of 32. Five of the misses got tests: promotion to `i64` where `i32` fits, a variadic call that passes a struct, Apple's 8-byte alignment of the first variadic argument after packed named ones, a function whose only stack arguments are structs, and a narrow stack argument loaded without its sign into a callee-saved register. The sixth was not a bug: a leaf function kept a slice's pointer out of `x7` when the slice went to the stack, for no reason, and removing that line saves two instructions and 16 bytes of frame in `slice_at_seven`.
       - Measured on the timing corpus, which has no stack arguments or variadic calls: the IR and the machine code are byte for byte the same as 3h.1's. Callgrind counts 0.46% more instructions in checking and 0.51% more in code generation. The first version tested each call for stack and variadic arguments on the common path and cost 3.06% in checking, nearly all of it in `walk`, where the call code is inlined; deciding once per declaration brought it to 0.46%. In code generation `pass_args` costs 10% more, because the argument cursor that the stack paths share lives in memory; passing and returning the cursor by value instead cost 1.3% more in all.
       - Rules chosen while building, open to review:
         - `..` is sheet 4e's option E; question 2 of 4e is still the owner's.
         - The promotions follow C's, so an untyped integer becomes `i32` when it fits, as C types the literal `7` as `int`. A computed `f32` is widened to `f64` here, although a named `f64` parameter refuses one without a conversion.
         - `..` takes numbers, `bool`, enums and pointers only. C can read a struct through `va_arg`, but `printf` and its family take none.
         - Only an `extern fn` can end with `..`. Chibi functions, function types and function literals can't; question 1 of 4e covers chibi's own variadics.
         - A default before `..` is used only when the call passes no variadic arguments.
         - The checker sizes the stack area with Linux's layout of the named arguments plus 8 bytes for each variadic argument, which covers Apple's layout too, so one IR serves both. On Apple a frame may reserve a few bytes that it doesn't use.
         - Chibi calls chibi with the host's C layout for stack arguments, so C and chibi callers share one rule, and the adapter for callbacks (3h.3) only has to add the context.
         - Apple's layout is tested only by C functions on Linux that record the raw stack words. Nothing has run on macOS.
         - A function with stack parameters always pushes the 32-byte frame record, so they sit at fixed offsets from `x29`.
         - A leaf function keeps parameters that arrive on the stack in frame slots, except a slice's pointer, which takes `x7` when the slice didn't fit there. Giving them free argument registers was tried and made code larger: a stack parameter used once took `x0`, and other values moved to callee-saved registers.
         - A by-pointer struct past the 32nd parameter is found by a slower check, because the backend's mask of by-pointer parameters has 32 bits.
         - `Convention` is a parameter of `arm64_compile`, not a build flag, so one build tests both layouts.
     - **3h.3 Callbacks that C calls: built 2026-11-21.** `extern fn(a: *int, b: *int) -> i32` is the type of a C function pointer: a function type with 3h.1's C bit, 8 bytes, never nil, compared with `==` and `!=`, and printed as written. `fn(...)` stays the type of a chibi function, whose values wait for rank 13. A C signature can't hold a chibi function type: "C can't call a chibi `fn(int) -> int`; write `extern fn(int) -> int`". As the callbacks row of 2026-11-20 decided, a named chibi function converts to an `extern fn` type wherever that type is expected: an argument, a declaration or assignment with a type, a field of a struct literal and a `return`, as in `qsort(p, 8, 8, ascending)`. The parameter and result types must match exactly, and a function whose result type comes from its body has its body checked first. An `extern fn` converts to its own address. The conversion emits `callback`, a new IR kind after `global`, printed `%1 = callback extern fn(*int, *int) -> i32 up`; operand a indexes the IR's callback list, which holds each converted function once, in the order of first use, and operand b is the function. The arm64 backend writes one adapter for each chibi function in that list, before the functions; an `extern fn` in the list needs none. The adapter takes C's convention: it moves the integer arguments up one register, loads the default context into `x0`, loads the function's entry from the function table and jumps to it. When the function takes no argument on the stack, the adapter ends with `br`, so the function returns straight to C: 6 instructions for one integer argument and 12 for seven. Otherwise it pushes a frame, copies the stack arguments into the layout the function expects, calls the function and returns: 19 instructions for eight integers, the last an `i16`. It extends no narrow integer, because a chibi function extends its parameters on entry, as it has for C callers since 3h.1. The default context is one pointer per program, in the slot after the function table, which the host sets with `native_set_context` and which is null until then. Because the adapter reads the function's entry at each call, `native_replace` reaches the pointers that C already holds. A function with over 4064 bytes of stack arguments can't be a callback: "a callback for C needs over 4064 bytes of stack arguments", which keeps every offset in the adapter within one instruction's field. The reference interpreter doesn't run `callback`.
       - Tests: the parser tests cover a field of an `extern fn` type with an `extern fn` parameter inside it, and the error "expected `fn` after `extern`". The checker tests cover the declarations dump with an `extern fn` parameter and field; one signature with two chibi function types, a slice and an array, which give the new message and 3h.1's; one body with eight errors: a function whose result type differs, a variadic C function, `h := one` and a chibi `fn` local, an `extern fn` local without a value, a struct with an `extern fn` field declared without a value and in a literal that leaves the field out, and `==` on a chibi function; the IR of two `qsort` calls, a local and a C function passed on, where the function whose result type comes from its body is declared after its use; and the callback list of a program that passes two functions twice each. Under qemu, the arm64 test program sorts eight numbers both ways with libc's `qsort` through two comparators, for inputs 0 to 19, and C calls 13 chibi functions through their adapters, from `qsort`, from a C function in the test file that calls a callback in a loop, and from the test through pointers that chibi functions return. They cover one integer; seven arguments of seven types, which C passes with junk in the upper bits; eight integers, the last an `i16` on the stack; six integers and a 16-byte struct that moves to the stack; a 24-byte struct, three `f32`s and an `int`; 19 arguments with an `int` after nine `f64`s and two narrow integers on the stack; HFAs of three and four `f32`s past the float registers; and 16- and 24-byte results, one with eight integer arguments. The function with eight arguments calls a C function that checks that the stack pointer is 16-byte aligned, which qemu doesn't check. Every use of a function gives the same pointer, `==` on two of them in chibi is true, and a C function passed on gives its own address. After `native_replace` swaps `tap` for a C function, the pointer handed out before reaches the new function with the default context. The same program compiled with Apple's layout gives the same results, with the two narrow integers packed in one slot. A function with 520 integer arguments gives the backend's message. The tests catch all 40 planted bugs: 17 by the checker tests, 2 by the parser tests, 10 by a wrong result, 2 by the backend's messages, 5 by a crash and 4 by a trap. The first run caught 34. The six misses got tests: a function whose result type comes from its body, used before it; `==` on a chibi function; an `extern fn` field left without a value; the stack pointer's alignment; an `int` after nine `f64`s, because with the other order, adapters that took floats for integers still gave the right results; and an HFA of four `f32`s as the last stack argument, where copying in 8-byte units overwrites the frame record.
       - Measured on the timing corpus, which has no callbacks: the IR and the machine code are byte for byte the same as 3h.2's. Callgrind counts 0.17% more instructions in checking and 0.11% more in code generation. The corpus runs almost none of the new code; the difference sits in `binary` (0.8 million more) and in the backend's `result` (0.5 million more, at its test for `call` and `call-c`, two of the kinds that `callback` renumbers).
       - Rules chosen while building, open to review:
         - `extern fn(...)` spells a C function pointer type, as `extern fn` spells a C declaration (3h.1) and as Rust writes `extern "C" fn(i32) -> i32`.
         - A function converts only where an `extern fn` type is expected, so `x := tap` stays an error until rank 13 decides chibi function values. A `break` value of a block that has a type, and a `switch` arm, don't convert one yet.
         - One adapter per function, so every use gives the same pointer and C can compare them.
         - The default context is one pointer per program that the host sets. What it points to waits for what the context holds (rank 7), and threads (step 4) may need one per thread.
         - The adapter extends no narrow integer and trusts the chibi function to do it on entry.
         - The adapter loads the function's entry at each call, one load more than a direct branch, so a replaced function reaches pointers that C already holds.
         - Calling through an `extern fn` value from chibi isn't built: `f(1)` gives "expected a function or a type".
         - An `extern fn` is never nil, so a C function that takes or returns NULL as a function pointer waits for optionals (3e) and `?extern fn`.
         - An `extern fn` type can't end with `..`.
         - A global of an `extern fn` type can't start as a function yet ("the checker doesn't support this yet"): its starting bytes are computed while compiling, and the adapter's address exists only once the backend places the code.
         - A callback takes at most 4064 bytes of stack arguments, about 500 integers.
         - The reference interpreter doesn't run `callback`, as it doesn't run `call-c`.
     - **3h.4 `export fn`: built 2026-11-21.** `export fn step(x: int) -> int` is a chibi function that C calls by its chibi name: the 2026-11-09 row decided the convention and the unmangled name, and sheet 10b's option A keeps the chibi name as the C name. C reaches it through the adapter that 3h.3 writes for callbacks, which supplies the default context, while chibi callers call the function itself and pass their context. Its signature follows an `extern fn`'s rules, with the message "C has no `[]u8`, so an `export fn` can't pass or return it", and a chibi function type in it gives 3h.3's message. The checker adds each exported function to the IR's callback list as it checks the declaration. Every declaration is checked before any body, so the exports come first in the list and `Ir.nexports` counts them; an export that a body also passes as a callback keeps its index, so it has one adapter and one address. The IR has no new kind. The arm64 backend keeps the adapters' addresses after compiling, and `native_export(n, i)` returns the address of the export at index `i`, which the host finds by name in the callback list. The C header that the 2026-11-09 row asks for comes with the C backend.
       - Tests: the checker tests cover an `export fn` with a slice parameter and a chibi function type, and one with a slice result, which give the two messages; and the callback list of a program with two exports and a function passed as a callback, where the exports come first and passing an export as a callback reuses its index. Under qemu, C calls five exports through `native_export` for inputs 0 to 19: an `i32` and a `u8` that C passes with junk in the upper bits, nine integers with two on the stack, a pointer that the export writes through, an `f64`, and an export that a chibi function also calls directly and passes to a C function that calls it in a loop. A chibi function returns that export as an `extern fn`, and C gets the same address from `native_export`. After `native_replace`, the export's address reaches the new function with the default context. The tests catch 11 of 12 planted bugs: 9 by the checker tests, 1 by a crash and 1 by a trap. The twelfth leaves the adapters' addresses unfreed in `native_free`, a leak that no test checks.
       - Measured on the timing corpus, which exports nothing: the IR and the machine code are byte for byte the same as 3h.3's. Callgrind counts 0.05% more instructions in checking and the same in code generation.
       - Rules chosen while building, open to review:
         - The chibi name is the C name (10b, question 1, option A). `export "c_name" fn` (option B) waits for an export that needs another name.
         - An exported signature follows an `extern fn`'s rules: no slices, arrays or chibi function types. Question 3 of 10b would map a slice to a `{ ptr, len }` struct in the header.
         - `export` applies only to functions (10b, question 4).
         - An export runs with the default context, as a callback does, so C can't choose it.
         - The JIT has no symbol table: the host finds an export in the IR's callback list and asks `native_export` for its address. A clash between an export's name and a C function's is left to the linker of a C build.
         - Exports come first in the callback list because every declaration is checked before any body, an order that step 4's threads must keep, as the zeroed globals of 3g.2 need.
     - **3h.5 `[*]T` and the entry point: built 2026-11-21.** `[*]T` points into an array, as rank 5 decided on 2026-11-15: `p[i]` reads or writes element `i` without a bounds check, `p + n` moves the pointer by `n` elements and `p += n` moves it in place, and `p[a..b]` makes a slice `[]T`, which checks its own indexes again. A slice's `.ptr` gives its `[*]T`, as in rank 5's `m: [*]u8 = buf.ptr`, and `&a` of an array `[N]T` converts to `[*]T` wherever that type is expected. `[*]T` is 8 bytes, compares with `==` and `!=`, is never nil and so has no zero value, prints as written, and passes to C and back as a pointer, through C's `..` too. `p.*` gives "`[*]int` points at many items, so write `p[0]`, not `p.*`". The parser reads `[*]` where a type starts; before, a `*` after `[` in a type failed to parse. The IR has no new kind. `p[i]` and `p + n` are `index` on a `[*]T` value, which the backend and the interpreter leave unchecked, because they check only an `index` on a pointer to an array. `p[a..b]` is `slice` with the length `INT64_MAX`, so it checks `0 <= a <= b` and nothing more. `.ptr` and the conversion are `index` by 0, which changes the type and costs no instruction when the value sits in a register. The entry point is `export fn main(argc: i32, argv: [*][*]u8) -> i32`, C's own signature, which a C runtime or a host calls with the default context; `argv[i]` is a `[*]u8`, and `argv[i][0..n]` makes it a slice.
       - Tests: the parser tests cover `[*]u8`, `[*][*]u8` and `?[*]*Node`, and the error after a bare `[*]`. The checker tests cover one body with 16 errors: a `[*]int` local without a value, `p[1..]`, `p.*`, `p - 1`, `p + 1.5`, `p + q`, `p < q`, `p.len`, `&a` of a `[3]int` to `[*]u8` and to `*int`, a `*int` to `[*]int`, a struct with a `[*]u8` field declared without a value, a `[*]int` to `*int`, `p[-1]`, `p + -1` and `p.ptr`; and the IR of indexing, `p + i`, `p += 1`, the three slicing forms, `.ptr` of a slice parameter and of a slice field reached through a pointer, the conversion from `&a`, `==`, and an `export fn main` that passes `argv[argc - 1]` to C's `strlen`. Under qemu, C calls 15 functions: indexing with negative indexes, an `i32` index and an `i32` value with junk in the upper bits, a 12-byte struct element, `p + n` with a `u32` offset and then an `i8` index, both with junk in the upper bits, a loop that walks a C string with `q += 1`, `[*]u8` passed to C, returned from it and passed through C's `..`, four bounds traps from slicing, two from indexing the slices it makes and an overflow trap from `..=`, `.ptr`, writes through a `[*]int` into a local array, `==`, and `main` with three, one and no arguments. A word count shows no conditional branch in `p[i]`. Five more functions run in both the backend and the reference interpreter on a grid of inputs, traps included. The tests catch all 32 planted bugs, one of them after a new test: 27 by the parser and checker tests, 3 by the machine-code tests, and 2 by crashes.
       - Measured on the timing corpus, which has no `[*]T`: the IR and the machine code are byte for byte the same as 3h.4's. Callgrind counts 0.10% more instructions in checking and the same in code generation. The difference sits in `binary`: two instructions for each of its 0.4 million calls, the test that sends `p + n` to its own path. The test compares the type's id first, because every `[*]T` is built while checking and so has an id past the fixed types; testing the type's kind first cost 0.25%. The conversion from `&a` costs nothing on the common path, because it sits in the function that reports a mismatch.
       - Rules chosen while building, open to review:
         - `p - n`, `p - q` and `<` aren't built: `p - 1` gives "`[*]u8` doesn't support `-`". C has all three. The C-to-chibi translator will show which ones translated code needs.
         - `p + n` takes any integer type, as an index does, and a constant `n` can't be negative, as a constant index can't be. `n + p` is an error.
         - Only a pointer to an array converts to `[*]T`. A `*T` doesn't, because rank 5 gives one-item pointers no indexing.
         - `p[a..]` is an error, "`[*]u8` has no length, so this slice needs an end".
         - An empty slice's `.ptr` can be address 0, because a slice's zero value is all zero bytes, so `.ptr` is the one way to get a `[*]T` that is nil. Once optionals exist (3e), `.ptr` could give `?[*]T`, which costs nothing where it goes to C, since rank 5's header mapping gives C's pointers to numbers the type `?[*]T`.
         - The entry point is an `export fn main` until top-level code, which the README lists after self-hosting. The JIT has no runtime to call it; the tests call it through `native_export`, and the C backend will emit it as C's `main`.
         - The differential test's generator doesn't make `[*]T`; the grid test above compares the backend with the interpreter instead.
   - **3i. SIMD types and builtins,** item 4 of the self-hosting order. The compiler's C counts trailing zeros 31 times, one bits 10 times and leading zeros 3 times, checks overflow 6 times with `__builtin_add_overflow` and its kin, and the lexer reads 16 bytes at a time through `src/simd.h`. Step 3i.1 builds the bit counts. The overflow checks need a way to return two results, which rank 12 leaves open, and the vectors need a type and a spelling, so both wait for the owner.
     - **3i.1 Bit counts: built 2026-11-21.** `trailing_zeros(x)`, `leading_zeros(x)` and `count_ones(x)` take any integer and give an `int`: the zero bits below the lowest one bit, the zero bits above the highest one bit within `x`'s own width, and the one bits. A zero gives the width, as C23's `stdc_trailing_zeros` and Rust's `trailing_zeros` do, so `trailing_zeros(u8(0))` is 8 and no input is undefined. A signed argument counts its two's complement bits, so `count_ones(i8(-1))` is 8. A constant argument folds at compile time, so `const SHIFT := trailing_zeros(ALIGN)` works, and an untyped constant becomes an `int` first. The three names are predeclared: a local, a field, an enum member or a package declaration with the same name takes the name's place, as the shadowing row of 2026-11-12 allows. The name without a call gives "`trailing_zeros` is a function, so it can't be used here", and any other argument gives "`count_ones` takes an integer, found `f32`". The IR has three new kinds, `trailing-zeros`, `leading-zeros` and `count-ones`, whose type is `int` and whose operand keeps its own type, as `convert`'s does. On arm64, `trailing_zeros` is `rbit` and `clz`, after an `orr` that sets the bit just past the width of an 8- or 16-bit type. `leading_zeros` is `clz`, followed by a `sub` for 8- and 16-bit types. `count_ones` moves the value to a vector register, counts each byte's bits with `cnt`, adds the bytes with `addv` and moves the sum back, because arm64 counts bits only in vector registers until FEAT_CSSC. Both zero-extend a signed 8- or 16-bit value first.
       - Tests: the checker tests fold constants at four widths (`leading_zeros(u8(1))` is 7, `trailing_zeros(i16(0))` is 16, `count_ones(i8(-1))` is 8, and `leading_zeros(u32(0)) + trailing_zeros(uintptr(1 << 63))` is 95) and cover the IR for `u64`, `i8`, `u16` and `i32` arguments; a package function named `trailing_zeros`, a local named `count_ones`, a field and an enum member that take the names' places; and one body with 16 errors: no argument, two arguments, an untyped float, a pointer, a slice, a struct, an `f32`, a `bool`, an enum, the name without a call, the name as an argument, a constant too large for `int`, a call that returns nothing, a type, an `int` stored in a `u8`, and an unused result. Under qemu, C calls one function for each of the eight integer types with four or five inputs each, among them zero, the top bit alone and all ones, and with junk above the narrow arguments. Word counts show one `rbit`, one `clz` and no conditional branch in a 64-bit `trailing_zeros`, one `cnt` and one `addv` in `count_ones`, and one `clz` and one `sub` in a 16-bit `leading_zeros`. Eight more functions, one for each type, run in both the backend and the reference interpreter on a grid of inputs. The tests catch all 24 planted bugs: 12 by the checker tests, 7 by the machine-code tests, 2 by the grid, and 3 by crashes, two of them asserts that name the cause.
       - Measured on the timing corpus, which calls none of the three: the IR and the machine code are byte for byte the same as 3h.5's. Callgrind counts 0.06% fewer instructions in checking and 0.11% fewer in code generation, from layout alone. A first version cost 0.47% and 0.72%: a new branch in `call` stopped clang from inlining `call_fn` into `walk`, and inlining `count_bits` into `emit_function` changed its register allocation. The builtins now go through the branch that reports "expected a function or a type", and `count_bits` stays out of line.
       - Rules chosen while building, open to review:
         - The spelling. The owner was asked on 2026-11-21, with plain names recommended; `@trailing_zeros` or a `bits` package would change only how the name resolves.
         - The names are Rust's and C23's without the `stdc_` prefix, rather than Odin's `count_trailing_zeros` or WGSL's `countTrailingZeros`.
         - The result is an `int`, as rank 2 makes counts, rather than the argument's type as in Odin, WGSL and Zig, so `w: u8 = trailing_zeros(x)` needs `u8(...)`.
         - `uintptr` counts as an integer; an enum or a `bool` needs a conversion first.
4. **Threads:** declaration jobs queued before one job per function body, with dependencies claimed on demand (section 3).
5. **Lowering to bytecode,** so the tests' programs run in the interpreter. Each later backend runs the same programs, which makes one differential test.

**Order, changed 2026-11-16 (owner):** self-hosting comes after 3b and 3d: globals and constant tables, calling C, SIMD and builtins, the C backend, the C-to-chibi translator and the bootstrap check (decisions.md). Then 3c, 3e, 3f, 4 and 5.

**Tests.** Pure transforms are tested with tables:
- source and the expected IR dump;
- source and the expected diagnostic, with its line, column and message, so every rejection has a test;
- programs that run in the interpreter and check their results.

**Benchmark.** The parser's generated programs are valid syntax but don't type-check, so the checker needs a generator of well-typed programs. The parser thread proposes where things live:
- the generator in `test/random/`, which the Makefile already collects for tests and the bench;
- the benchmark as a function in a new file in `bench/`, with no `main`, called from `bench/bench.c` by the parser thread as one more phase of `make bench`.

The benchmark reports:
- cycles per line on 1 and 14 threads (H5, H3);
- IR bytes per line (H17);
- the time spent waiting on claims held by other threads (H14).

## 9. Questions for the owner

Questions 1, 4 and 5 shape the checker's structure, so they came before any checker code; all three are decided. Questions 6, 7 and 9 come up in steps 2 and 3 of section 8, or wait for their own syntax decisions, and their recommended answers are cheap to change later.

Decided by the owner on 2026-11-12, and recorded in decisions.md:
- **1. What the checker reads** (section 1): one forward pass over the postorder nodes, plus two parser additions, a list of top-level nodes and builtin names with fixed ids.
- **2. One id per name** (section 2.1): per-thread name tables, merged after parsing.
- **3. Shadowing** (section 2.2): option C, the rule of D, C# and Java. A local may not hide another local or parameter of the same function, may reuse package and predeclared names, and builtin type names such as `int` stay reserved.
- **4. Order of checking** (section 3): each phase of a declaration claimed on demand, with declaration jobs queued before bodies and no barrier.
- **5. The IR's shape** (sections 5 and 5.5): explicit operands in program order, locals in slots, and every value used once.
- **8. Guaranteed tail calls** (section 6.2): written `@tail return f(x)`. Their rules are still open (question 8 below).
- **10. Lengths** (section 4.4): arrays, slices and strings have a read-only `.len` field, and there is no predeclared `len`.

Decided by the owner on 2026-11-15 ("All of the recommendations look great"), and recorded in decisions.md: questions 11 to 16 below, each as recommended.

Decided by the owner on 2026-11-14, from section 8's review list: discarding a value takes `_ =`, and parameters and `for` variables are read-only, both as in Zig.

Decided by the owner on 2026-11-13:
- **6. Untyped constants** (section 4.2): integers in 64 bits with a big-integer fallback up to 512 bits, and floats as exact fractions, both as in Go. The recommendation was revised on 2026-11-13 from 128-bit integers and f64 floats, after measuring how often f64 folding differs.
- **7. Literals that take their type from context** (section 4.3): option C, the expected type where a marker gives one, and a provisional value until the operator that uses it otherwise.

Open:
8. **Tail-call rules** (section 6.2). The callee's type equals the caller's; a pending `defer` or `defer catch`, the address of a local in the arguments, varargs and GPU code are errors; and an opt-in register-heavy convention for handlers, spelled once attributes are decided? Recommended: yes.
9. **Computed `goto`** (section 6.3). Leave out labels as values. Instead, chibi's own backends promise one indirect jump per `continue name value`, and a direct jump when the value is known at compile time. Recommended: yes.


**Step 3 questions (2026-11-15), all decided as recommended on 2026-11-15.** The language surface of step 3 is mostly decided; these are the choices of representation and lowering it still needs. Each is cheap to state now and expensive to change once three backends read it.

11. **`switch` in the IR.**
    - **A. A `switch` instruction** with its cases in the side array, each case a value or range and an arm. Zig's AIR (`switch_br`), LLVM (`switch`), WebAssembly (`br_table`) and SPIR-V (`OpSwitch`) all have one. Each backend picks a jump table, a binary search or compares. The labeled-switch dispatch that question 9 recommends (`continue sw value`) needs it: one indirect jump per site is only possible if the backend sees the whole switch.
    - **B. Nested `if`s.** No new instruction, but a backend can't build a jump table without recognizing the chain again, and the GPU writers lose `OpSwitch`.
    - Recommended: A.
12. **Pointer kinds** (adopt-or-settle.md 2.5, rank 5, still open). Recommended there, as in Zig: `*T` is never nil and has no indexing or arithmetic; `[*]T` points into an array, indexes without checks and supports `p + n`; `?*T` may be nil, is the same size, and nil is address 0. Odin has the same split (`^T` and `[^]T`); C, Go and C3 have one nullable kind. Step 3 can start with `*T` and `?*T` only, and add `[*]T` with calls to C.
13. **Bounds checks** (adopt-or-settle.md 2.9, rank 9, still open). Recommended there: indexing and slicing check bounds in every build, and a block opts out with an attribute, as Odin's `#no_bounds_check` does. Rust, Go and Swift always check; Zig checks only in its safe build modes.
    - Measured 2026-11-15 on the M4 Pro with clang (-O1 and -O2, vectorizing off): a sequential sum and a random gather over 4,096 `i64`s ran within ±5% of the unchecked loops, about 0.24 ns per element either way. The cost of checks is not the compare and branch but the vectorizing they can prevent in release builds, which the opt-out covers.
14. **Structs, arrays and slices in the IR.**
    - **A. In memory only.** A struct, array or slice local lives in a slot; the IR gets `addr` (of a slot), `field` and `index` (typed addresses that keep field numbers for the GPU writers), and `load`/`store` through an address. Calls pass them through a hidden pointer. This is WebAssembly's and Cranelift's model, and Rust's MIR places; it maps directly onto SPIR-V's access chains. Simple for every backend, but a slice's pointer and length are reloaded on every use.
    - **B. As values.** Aggregates are IR values with field reads and builds, as in LLVM's first-class aggregates and Zig's AIR. Each backend must keep values that need two or more registers.
    - **C. A, plus pairs.** Values of two scalars, a slice, `?T` and `E!T` for a scalar `T`, are values held in two registers; everything else is in memory. Rust lowers such types as a scalar pair, and Go's SSA backend splits structs of up to four fields into scalars.
    - Recommended: C, after prototyping A and C on a slice loop and timing both, as with `and`/`or`. Locals of a struct type whose address is never taken could later be split into scalar slots by the checker, so the register choice of 2026-11-15 applies to their fields.
    - **Timed on 2026-11-19, on x86-64** ([experiments/slice-lowering](../../experiments/slice-lowering/README.md)), with machine code written as a one-pass backend would emit it. Indexing a slice that doesn't change took 1.5 cycles per element in both forms. A loop that reads the first byte and drops it, as a lexer does, took 2.0–2.1 cycles per byte with C and 2.7–2.9 with A, which loads and stores both halves of the slice at every step. A call per byte that passes the remaining slice took 4.4–5.1 cycles with C and 5.0–5.5 with A. The owner's M4 Pro was not available; llvm-mca can't stand in for it, because LLVM 18 models Apple cores with the 2013 Cyclone core and doesn't model a store forwarded to a later load.
15. **`defer`.**
    - **A. Copied to each exit.** The deferred code is checked once into a side buffer and copied, with its values renumbered, at every exit from its scope. Zig stores a defer body once and analyzes it at each exit; Odin and C3 also expand defers at each exit. No run-time state, so it is the fastest; the code grows with exits times defers.
    - **B. One cleanup block per scope** that each exit jumps to, with a variable saying where to go after it, as C code does with `goto cleanup`. Smaller code, one extra branch and store per exit.
    - **C. A run-time list of deferred calls,** as Go did before 1.14; Go then moved simple cases to open-coded defers, which is A.
    - Recommended: A.
16. **Optionals and error unions in memory and in calls.**
    - `?*T` is the pointer, with nil at address 0 (Zig guarantees this; Rust's `Option<&T>` does the same).
    - `?T` for other `T` is `{ T value; bool present }` in C layout, as adopt-or-settle.md 2.5 proposes.
    - `E!T` is `{ T value; u16 code }`, with code 0 for success, as the error-code row proposes (Zig's error type defaults to `u16`).
    - In calls, `T` returns in `x0` or `d0` and the code in `w1`, so `try f()` is `cbnz w1, error_path` after the call. Swift returns its error in a dedicated register (`x21` on arm64); Rust returns a two-scalar `Result` in two registers.
    - Recommended: as listed.

The memory model stays open; step 3 needs no heap, only pointers to locals, arrays and slices of them.

## 10. Not verified

- **Jai.** Its checker, its message loop and how it waits on dependencies have no public primary source, so this sheet doesn't rely on Jai.
- **Go.** No published lines-per-second figure for its type checker was found.
- **TypeScript.** Which release first ships the import-graph partitioning of PR #4313 was not checked.
- **rustc.** Its module-level name tables were not read.
- **Real code.** Every per-line figure in this sheet comes from the parser's generated programs. H1 asks whether those predict real code, and is still untested.
- **Dispatch.** These were not confirmed in a primary source:
  - what Zig does with a `defer` pending at `.always_tail`;
  - whether GCC's and Clang's AArch64 `preserve_none` are the same convention;
  - which MSVC version added `__preserve_none`;
  - why Cranelift gave its `tail` convention callee-saved registers back in 2024;
  - whether C3's `@jump` gives each `nextcase` its own jump;
  - Metal's rules on recursion and function pointers;
  - whether WG14 has published the Technical Specification that N3266 and N3582 propose (`return goto f(x)`).
- **IR sizes.** No compiler publishes IR instructions per source line, and none publishes a backend's cost per IR instruction (TPDE, Cranelift, Liftoff).
- **This sheet's own measurements** ran while other sessions kept the load average at 10–13. The cycle counters are per process, but the ranges are wider than on an idle machine.
