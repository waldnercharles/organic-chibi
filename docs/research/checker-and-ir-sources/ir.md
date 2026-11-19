# B2: Prior art for chibi-v2's typed IR

Research for the checker and IR design sheet, written 2026-11-12. Sources are primary: project source at a pinned tag or commit, official documentation, papers, and posts by the authors. Anything that no such source confirms is marked **UNVERIFIED**. Line numbers refer to the pinned versions in the source list at the end. Wording from sources is paraphrased rather than quoted.

**Corrections to the "already known" list.**

- TPDE's "12–19×" is the per-architecture geometric mean on unoptimized LLVM-IR: 12.15× on x86-64 and 18.96× on AArch64. The abstract states the per-benchmark range as 8–24×. The paper's 14% time share belongs to the *LLVM preparation pass*, not to the IR adaptor; `fast-compilers.md` section 3.8 gives that 14% to the IR adaptor. ([arXiv 2505.22610v1](https://arxiv.org/html/2505.22610v1), section 5.)
- Confirmed: AIR is a 1-byte tag plus an 8-byte payload, asserted in `Air.zig`; Liveness keeps 4 bits per instruction; Slang puts the merge point on the branch instruction. Carbon's 16-byte instruction was not rechecked.

---

## 1. Zig AIR (tag 0.17.1, commit `be248cc1`)

### 1.1 Layout

- `Air.zig` keeps one AIR per function: an `instructions` `MultiArrayList` (one array of 1-byte tags, one of 8-byte payloads) plus an `extra: ArrayList(u32)` for larger payloads ([Air.zig L1-L25](https://codeberg.org/ziglang/zig/src/tag/0.17.1/src/Air.zig#L1-L25)).
- The 8-byte payload is a `union`, and a `comptime` assert pins `@sizeOf(Data) == 8` in builds without safety checks ([L1287-L1373](https://codeberg.org/ziglang/zig/src/tag/0.17.1/src/Air.zig#L1287-L1373)).
- A body is a list of `u32` instruction indices in `extra`: `Block` has a `body_len` with trailing indices ([L1378-L1381](https://codeberg.org/ziglang/zig/src/tag/0.17.1/src/Air.zig#L1378-L1381)). Nearly every instruction belongs to a body, so it costs another 4 bytes. ZIR has the same layout, and [issue #16039](https://github.com/ziglang/zig/issues/16039) measured these index lists at about 25% of `Sema.zig`'s ZIR.
- The compiler's AIR dump prints totals per function. In [Codeberg issue #36521](https://codeberg.org/ziglang/zig/issues/36521) (opened 2026-08-15), 12 instructions take 108 B, which is 9 bytes each. `--verbose-air` is listed among the debug options meant for compiler development ([main.zig L792-L797](https://codeberg.org/ziglang/zig/src/tag/0.17.1/src/main.zig#L792-L797)).
- `Air/Legalize.zig` rewrites AIR into a simpler subset chosen by each backend's feature set; for example, `scalarize_add` removes vector adds for a backend that can't emit them ([Legalize.zig L1-L40](https://codeberg.org/ziglang/zig/src/tag/0.17.1/src/Air/Legalize.zig#L1-L40)). One IR serves every backend. Backends that can do less ask for more lowering, rather than each having its own IR.

### 1.2 Control-flow instructions

AIR control flow is a tree of bodies. Every body ends with an instruction of type `noreturn`, so there is no fall-through and no merge block. Control reaches the code after a `block` only by a `br` that targets it. Source: doc comments at [Air.zig L335-L357 and L494-L529](https://codeberg.org/ziglang/zig/src/tag/0.17.1/src/Air.zig#L335-L357).

| Tag | Payload | Meaning |
|---|---|---|
| `block` | `ty_pl`, `Block` | Runs its body. Continues after itself only when a `br` targets it, and the `br` operand is the block's value. A `noreturn` block has no `br`s. |
| `loop` | `ty_pl`, `Block` | Its body runs forever and loops only through an explicit `repeat`. It always has at least one `repeat`, and its type is always `noreturn`. |
| `repeat` | `loop_inst` | Jumps back to the start of an enclosing `loop`. |
| `br` | `block_inst`, `operand` | Leaves any enclosing `block` with a value. It can leave several levels at once, as Wasm's `br` can. |
| `cond_br` | `pl_op`, `CondBr` | Condition plus a then-body and an else-body, each ending in `noreturn`, with packed branch hints ([L1396-L1413](https://codeberg.org/ziglang/zig/src/tag/0.17.1/src/Air.zig#L1396-L1413)). |
| `switch_br` | `pl_op`, `SwitchBr` | Operand plus cases. Each case has items, ranges and a body, followed by an else body ([L1415-L1430](https://codeberg.org/ziglang/zig/src/tag/0.17.1/src/Air.zig#L1415-L1430)). |
| `loop_switch_br` | `pl_op`, `SwitchBr` | A switch that can dispatch back to itself with a new operand. It is the AIR form of Zig's labeled `switch`. |
| `switch_dispatch` | `br` (`block_inst` is a `loop_switch_br`) | `continue :label value`. Dispatches again to the parent `loop_switch_br` with a new operand. |
| `try`, `try_cold`, `try_ptr` | `pl_op`, `Try` | Splits on an error union. The error body must return. |

Zig 0.14.0 added `repeat`, `loop_switch_br` and `switch_dispatch` for labeled switch. The [0.14.0 release notes](https://ziglang.org/download/0.14.0/release-notes.html) define a labeled switch as equivalent to a switch in a loop with a variable, and state the intended code: a `continue` whose operand is known at compile time becomes an unconditional branch to its case; a `continue` with a runtime operand may become its own branch at each site, ideally through one shared jump table.

### 1.3 `Air.Inst.Ref`

A `Ref` is a `u32`. The top bit set means an instruction index, and the top bit clear means an `InternPool` index. `none` is the one exception: it has the top bit set but refers to the InternPool ([L1094-L1283](https://codeberg.org/ziglang/zig/src/tag/0.17.1/src/Air.zig#L1094-L1283)). The first InternPool indices are named well-known types and values (`u8_type`, `bool_true`, and so on). An operand can therefore name a constant or a type directly, with no constant instruction. CLIF and Umbra IR do need one: CLIF has `iconst`, and Umbra IR makes constants separate instructions. In 0.17.1, `Inst.Index` values with the top bit set are also "target" indices, printed `%tN` ([L1061-L1091](https://codeberg.org/ziglang/zig/src/tag/0.17.1/src/Air.zig#L1061-L1091)); what they are for was not traced.

### 1.4 Locals, and no phis

- A runtime `var` becomes `alloc`, which takes a pointer type and reserves local stack memory, followed by `store` and `load` ([Air.zig L228-L230, L594-L621](https://codeberg.org/ziglang/zig/src/tag/0.17.1/src/Air.zig#L228-L230); Sema emits it at [Sema.zig L3361 and L3877](https://codeberg.org/ziglang/zig/src/tag/0.17.1/src/Sema.zig#L3340-L3362)).
- A `const` whose address isn't taken stays an SSA value. `dbg_var_val` takes the local's value as its operand, while `dbg_var_ptr` names the storage of a `var` or of an address-taken `const` ([L550-L558](https://codeberg.org/ziglang/zig/src/tag/0.17.1/src/Air.zig#L550-L558)).
- AIR has no phi instruction and no block parameters. The only merge value is a block's result, carried by `br` operands. The LLVM backend builds one `phi` per `block` from its list of `br`s ([llvm/FuncGen.zig L1340-L1380](https://codeberg.org/ziglang/zig/src/tag/0.17.1/src/codegen/llvm/FuncGen.zig#L1340-L1380)). The C backend assigns a result local before `goto zig_block_N` ([c.zig airBr](https://codeberg.org/ziglang/zig/src/tag/0.17.1/src/codegen/c.zig#L4383)).
- In short, AIR is SSA for temporaries and memory for `var`s. Promotion to registers happens only in LLVM.

### 1.5 `.always_tail` calls

- `@call(.always_tail, f, args)` becomes the tag `call_always_tail`, which differs from `call` only in that attribute ([Air.zig L380-L386](https://codeberg.org/ziglang/zig/src/tag/0.17.1/src/Air.zig#L374-L386)). Sema then emits a `ret` of the call's result ([Sema.zig `handleTailCall` L7428-L7445](https://codeberg.org/ziglang/zig/src/tag/0.17.1/src/Sema.zig#L7428-L7445)).
- Sema rejects the call unless the callee's function type equals the caller's. It also rejects it unless the backend supports tail calls for the target.
- `supportsTailCall` returns true only for the LLVM backend, depending on the target, and for the C backend. Every self-hosted backend, x86_64 included, returns false, so `.always_tail` is a compile error there ([target.zig L870-L879](https://codeberg.org/ziglang/zig/src/tag/0.17.1/src/target.zig#L870-L879)).
- The C backend recognizes the `ret` whose operand is a `call_always_tail` and prints `zig_always_tail return f(...)` ([c.zig L3426-L3428, L4098-L4099](https://codeberg.org/ziglang/zig/src/tag/0.17.1/src/codegen/c.zig#L4098-L4099)). `zig.h` maps that to `__attribute__((musttail))`. Where the C compiler lacks it, the macro expands to an undefined name, so compilation fails rather than silently losing the guarantee ([lib/zig.h L244-L248](https://codeberg.org/ziglang/zig/src/tag/0.17.1/lib/zig.h#L244-L248)).

### 1.6 How backends lower `switch_dispatch`

- **x86_64** ([CodeGen.zig L177380-L177445, L177717-L177858](https://codeberg.org/ziglang/zig/src/tag/0.17.1/src/codegen/x86_64/CodeGen.zig#L177758-L177858)):
  - `lowerSwitchBr` builds one jump table per switch, but only when:
    - the output is non-PIC ELF;
    - the switch has 4 to 256 prong items;
    - the value range is at most 1,024 entries;
    - at most 75% of the table is waste.
  - For a `loop_switch_br` with a table, **each `switch_dispatch` emits its own bounds check (if there is an `else`) and its own indirect `jmp [table + index*8]`** through the shared table.
  - Otherwise a dispatch is a `jmp` back to the switch's compare chain, which is one shared dispatch point. Mach-O and PIC builds always take this path.
- **C backend** ([c.zig L505-L507, L4383-L4422, L4716-L4727](https://codeberg.org/ziglang/zig/src/tag/0.17.1/src/codegen/c.zig#L4383-L4422)):
  - `loop_switch_br` copies its operand into a C local and puts a label `zig_switch_N_loop:` before one C `switch`. Each case also gets a label, `zig_switch_N_dispatch_K`.
  - A dispatch whose operand is known at compile time becomes `goto zig_switch_N_dispatch_K;`, a direct jump.
  - A runtime dispatch assigns the local and does `goto zig_switch_N_loop;`. The C code therefore has one shared dispatch; whether it is duplicated is up to the C compiler. The comment says this keeps the C output small and simple, at some cost in code quality.
- **LLVM backend** (not asked, but it is the clearest statement of intent): the comment at [FuncGen.zig L1763-L1774](https://codeberg.org/ziglang/zig/src/tag/0.17.1/src/codegen/llvm/FuncGen.zig#L1763-L1774) says labeled switch is meant to follow the direct-threading pattern. LLVM's own lowering did not share the jump table and added a compare before each dispatch, so Zig builds the table of `blockaddress` constants by hand. Each dispatch site then emits its own bounds check and `indirectbr` ([L1405-L1492](https://codeberg.org/ziglang/zig/src/tag/0.17.1/src/codegen/llvm/FuncGen.zig#L1405-L1492)). Limits: at most 1,024 entries, unsigned operands only, and not on Wasm.
- **SPIR-V:** see 7.5.

### 1.7 Liveness

- Liveness is a separate analysis with 4 bits per instruction: one bit for "unreferenced" and three bits for "operand dies here". A sparse `special` table holds per-branch death lists for `cond_br`, `switch_br`, `loop_switch_br`, `block` and `try`, plus extra bits for calls with more than three operands ([Air/Liveness.zig L24-L45](https://codeberg.org/ziglang/zig/src/tag/0.17.1/src/Air/Liveness.zig#L24-L45)).
- It walks the instructions backwards twice:
  - `loop_analysis` records, for each `loop` and `loop_switch_br`, the outer blocks it breaks to and the operands it uses from outside;
  - `main_analysis` then sets the bits ([L72-L90](https://codeberg.org/ziglang/zig/src/tag/0.17.1/src/Air/Liveness.zig#L72-L90)).
- Because AIR is structured, no dominator tree and no loop detection are needed.

### 1.8 Measurements

No published figure was found for AIR instructions per source line or per function. The searches turned up ZIR size statistics (issue #16039) but no AIR counts per line. The per-function dump in 1.1 could be summed over a corpus to measure it.

---

## 2. WebAssembly as an IR

### 2.1 Structured control flow

The WebAssembly spec, version 3.0 dated 2026-11-04 on the page ([instructions](https://webassembly.github.io/spec/core/syntax/instructions.html)), defines it this way:

- `block`, `loop` and `if` open labelled constructs.
- `br`, `br_if` and `br_table` name a label by relative nesting depth.
- A branch to a `block` or `if` resumes after the construct's end. A branch to a `loop` jumps backward to the loop's start.
- Branches can only go outward. The spec compares them to C's `break` and `continue`.
- `br_table` is the indirect branch: it indexes a list of labels and has a default.

Titzer gives the reason for this design: structured constructs were chosen on purpose, for compact code and validation in one pass, whereas a bytecode with jumps typically needs more bytes and two passes to verify ([arXiv 2205.01183 §2](https://arxiv.org/abs/2205.01183)).

### 2.2 Why fast interpreters translate Wasm: Titzer, OOPSLA 2022

Titzer, "A fast in-place interpreter for WebAssembly", *Proc. ACM Program. Lang.* 6(OOPSLA2), article 148, 2022; preprint [arXiv 2205.01183](https://arxiv.org/abs/2205.01183). The interpreter is in the Wizard engine; the benchmark suite is PolyBenchC.

- **The problem:** a Wasm branch names a construct by depth, so an interpreter can't quickly find the target offset (the start of a loop or the end of a block). It also can't tell how many values to pop. The interpreters Titzer surveyed (JSC, Chakra, wasm3, WAMR-fast) rewrite Wasm into an internal format. WAMR's classic interpreter does run in place, but finds branch targets through a runtime cache, and on a miss it may rescan the function (§4.4.4).
- **The fix:** the validator already computes this information, so it emits a per-function side table. Each entry is the tuple ⟨Δip, Δstp, valcnt, popcnt⟩, and only branches need entries. The interpreter keeps a side-table pointer next to its instruction pointer.

Numbers:

| Measure | Result |
|---|---|
| Side-table space | About 30% of the original bytecode, measured with entries packed to 2 bytes where possible (not the default). |
| Space of the other tiers | Rewriting interpreters and compilers typically take 2×–4× the bytecode's space. Among rewriters, WAMR takes up to 4× and JSC's interpreter about 1×. |
| Translation time | Building the side table is about 10× cheaper than the cheapest rewriting interpreter. Baseline compilers cost 10× more than rewriting interpreters. |
| Speed | wasm3 was the fastest interpreter on every benchmark. WAMR-fast was 1.5–1.7× slower than wasm3, and the others 2–3× slower. Wizard was nearly equal to WAMR-fast on the 4 shortest benchmarks. |
| Interpreter tuning | Threaded dispatch was on average 14% faster (at most 29%). 4-byte dispatch-table entries were fastest. Removing value tags gave about 8% (at most 15%). Best against worst tuning: 20%–60%. |
| Against JITs | wasm3 is about 10× slower than V8 TurboFan on long benchmarks. Liftoff and SpiderMonkey's baseline were about 2× slower than TurboFan, and JSC's BBQ about 3×. |

Titzer also notes that compilers don't turn a loop around a switch into threaded dispatch. The C alternatives, computed `goto` and tail-calling handlers, produce several irreducible loops and spill interpreter state ([PDF §3, footnote 13](https://arxiv.org/pdf/2205.01183)).

### 2.3 Wasm3

Source: [docs/Interpreter.md @ `28ecb9af`](https://github.com/wasm3/wasm3/blob/28ecb9af6d2040e474a70f7cb7f43666740141fb/docs/Interpreter.md).

- A compile pass turns Wasm into "operations". Each is a C function with one fixed signature `(pc, sp, mem, r0, fp0)`, and it ends with a call to the next operation, which the C compiler turns into a tail call. There is no central dispatch loop.
- `r0` and `fp0` act as top-of-stack registers. One opcode maps to up to 3 operations depending on its operands and commutativity, so it spends space to save time.
- Blocks are flattened into direct jumps. Loops (and `try_table`) take a native frame.
- The author's measurement on a 4 GHz Intel iMac: roughly 4–15× slower than compiled code.
- Titzer adds that wasm3 rewrites into a register machine, merges constants, and does peephole fusion, with word-sized operands.

### 2.4 wasmi: register machine in 0.32, accumulators in 2.0

- **v0.32** ([Freyler, 2024-05-28](https://wasmi-labs.github.io/blog/posts/wasmi-v0.32/)):
  - A new register-based bytecode replaced the stack-based IR. The example sequence `local.get 0; local.get 1; i32.add; local.set 0` becomes the single instruction `0 <- i32.add 0 1`, so 4 executed instructions become 1.
  - CoreMark against v0.31: 1.7× on an i7-14700K, 1.8× on an M2 Pro, 1.9× on a Threadripper 3990X and 2.2× on an EPYC 7763.
  - Costs: translation became more expensive, above all for the new register bytecode; recursive Fibonacci didn't improve; and the design suits compute-heavy work better than call-heavy work.
  - Startup improved through lazy translation and validation; the post claims gains of several orders of magnitude without giving a ratio.
- **v2.0** ([Freyler, 2026-09-01](https://wasmi-labs.github.io/blog/posts/wasmi-v2.0/)):
  - The engine moved to *accumulator registers* (`ireg`, `freg32`, `freg64`) that hold results implicitly. 1.0's `i32_add_ssi 1 0 10` becomes `i32_add_rsi 0 10` followed by `u64_copy_sr 1`, with fused forms for common `add`/`load` followed by `local.set`.
  - Dispatch became direct-threaded: handlers tail-call each other through Rust's unstable `become`. Indirect threading is 10–15% slower but makes the IR smaller. A switch loop remains as the fallback.
  - Execute geometric mean: about 2.2× faster than 1.0 on an M2 Pro. Startup stayed about the same.
  - This is the same shape as V8 Ignition (3.3): a register file plus an accumulator.

---

## 3. Register versus stack bytecode

### 3.1 Shi, Gregg, Beatty and Ertl, "Virtual Machine Showdown"

- **VEE 2005** ([PDF](https://www.usenix.org/legacy/events/vee05/full_papers/p153-yunhe.pdf)), JVM, Pentium 4:
  - Translating stack code to register code, with copy propagation and constant hoisting, removes on average more than 47% of executed VM instructions (47.21%) and 43.47% of static ones.
  - Register code is 25.05% larger. That costs only 1.07 extra real machine loads per VM instruction removed.
  - Time: 32.3% less with `switch` dispatch and about 26.5% less with threaded dispatch.
- **TACO 2008** (Shi, Casey, Ertl, Gregg; [author preprint](https://www.complang.tuwien.ac.at/andi/tuonly/SkriptVMShowdown.pdf)), a full register JVM on Cacao 0.95:
  - More than 46% fewer executed instructions; bytecode 26% larger.
  - On AMD64: a 1.48× speedup with switch dispatch and still 1.15× with inline-threaded dispatch.

### 3.2 Lua 5.0 (Ierusalimschy, de Figueiredo and Celes, J.UCS 2005)

Source: [jucs05.pdf](https://www.lua.org/doc/jucs05.pdf).

- The paper calls it the first register-based VM in wide use. Lua's compiler has no IR: it emits VM code while parsing.
- Instructions are 32 bits: a 6-bit opcode, an 8-bit `A`, and 9-bit `B` and `C`, which can combine into an 18-bit `Bx` or `sBx`. There are 35 opcodes.
- Most instructions are three-address. `RK(x)` operands select a register or a constant, so `a = a + 1` is one `ADD`.
- Rationale: a register machine avoids push and pop and needs fewer instructions. It decodes cheaply because the operands sit in one machine word. Stack VMs use 1–2 bytes per instruction, but need more instructions, so total code is only slightly larger.
- Benchmarks on a Pentium IV with gcc 3.3, Lua 4.0 (stack) against Lua 5.0 (register):
  - `sum` loop: 1.23 s → 0.54 s (44%);
  - `fibo`: 72%;
  - `ack`: 86%.

### 3.3 V8 Ignition: registers plus an accumulator

- Sources: the [blog post](https://v8.dev/blog/ignition-interpreter) (McIlroy, 2016-08-23) and the [design document "Ignition: V8 Interpreter"](https://docs.google.com/document/d/11T2CRex9hXxoJwbYqVQ32yIPMh0uouUZLdyrtmMoL44) (rmcilroy, oth; updated 2016-03-22).
- Explicit register operands plus an accumulator that is the implicit input and output of many bytecodes. The accumulator makes bytecodes smaller, because they need no register operand for it.
- Registers are slots in the frame's register file, with sizes fixed when the bytecode is generated.
- Operands are 8 bits by default. `Wide` and `ExtraWide` prefix bytecodes widen them to 16 or 32 bits; the prefixes cost under 1% on Octane-2.1.
- Handlers are generated with TurboFan's macro-assembler. Each handler dispatches by tail-calling the next one, making the interpreter indirect-threaded.
- Bytecode is 25%–50% the size of the baseline machine code it replaced. Early field experiments showed about 5% less memory per tab.
- The design doc's goal was about 2× slower than full-codegen on peak benchmarks.

### 3.4 Superinstructions and replication (Ertl and Gregg)

Ertl and Gregg, PLDI 2003; extended as Casey, Ertl and Gregg, TOPLAS 29(6), 2007 ([author preprint](https://www.scss.tcd.ie/David.Gregg/papers/toplas05.pdf)).

- Branch target buffers predict interpreter dispatch only 2%–50% of the time.
- *Replication*: several copies of a VM instruction, so each copy gets its own dispatch branch.
- *Superinstructions*: sequences combined into one VM instruction. Static ones are chosen when the interpreter is built; dynamic ones are made at run time.
- Together these remove nearly all dispatch mispredictions. Speedups reach 4.55× over efficient threaded-code interpreters and 1.34× over dynamic superinstructions alone, on Gforth and a JVM.

### 3.5 Quickening and specialization: CPython 3.11, PEP 659

- [PEP 659](https://peps.python.org/pep-0659/) (Shannon, 2021):
  - Quickening replaces generic instructions with specialized ones in place.
  - Adaptive instructions keep counters and periodically try to specialize.
  - A specialized instruction de-optimizes by writing back the adaptive opcode.
  - Inline caches live in the bytecode array itself.
  - Expected speedups: 10%–60%, with up to 50% seen in experiments.
  - Memory: about 6 bytes per instruction in 3.11, against 2 bytes for cold code and about 7.8 bytes for hot code in 3.10.
- [What's New in Python 3.11](https://docs.python.org/3.11/whatsnew/3.11.html):
  - 25% faster than 3.10 on average on pyperformance (GCC, Ubuntu), and 10–60% depending on workload.
  - Maximum gain per operation: binary operations 10%, subscript 10–25%, calls 20%, method loads 10–20%.

---

## 4. TPDE (paper arXiv 2505.22610v1; code `tpde2/tpde` @ `efe4cc77`, 2026-11-11)

Authors: Schwarz, Kamm and Engelke (TU Munich). The arXiv page doesn't name a venue.

### 4.1 What the input IR must provide

From [docs/tpde/adaptor.md](https://github.com/tpde2/tpde/blob/efe4cc7759e4bf99146d6ad9834636a7bcc0f1f6/docs/tpde/adaptor.md) and the paper §3:

- SSA form for instruction results. Memory need not be SSA, so mutable variables may stay in stack slots.
- A CFG of basic blocks with control flow only at block ends. Each block lists its instructions.
- Single-entry functions and blocks.
- Multiple results per instruction are allowed.
- Values merge through **phi nodes**. Direct support for block arguments is planned. Block arguments can create multi-edges, which TPDE doesn't support, so the CLIF adaptor inserts empty blocks.
- Successors only; TPDE doesn't need predecessor lists.
- Values are untyped apart from size and register bank. Each value needs a per-function number usable as an array index.
- **Block order is free:** the first block must be the entry, and the rest may come in any consistent order.

### 4.2 Analysis pass, run once before code generation

- **Layout:** blocks in reverse post-order, except that each loop is placed together.
- **Loops:** Wei et al.'s algorithm, chosen because it handles **irreducible** loops; the whole function is wrapped in one loop. Worst case O(N·E), average O(N+E).
- **Liveness:** Kohn et al.'s algorithm, linear in the instruction count. Each value gets a coarse interval: first block, last block, and whether it ends inside or at the end of the last block. The interval is extended across back edges so a value stays live for the whole loop. Uses are counted as well.
- The paper's reason: SSA allows a coarser, and therefore faster, liveness analysis.

### 4.3 The IR adaptor interface

The adaptor is a C++ template parameter checked by the `IRAdaptor` concept ([IRAdaptor.hpp L79-L290](https://github.com/tpde2/tpde/blob/efe4cc7759e4bf99146d6ad9834636a7bcc0f1f6/tpde/include/tpde/IRAdaptor.hpp#L79-L290)), so calls into it inline. It must provide:

- `IRValueRef`, `IRBlockRef` and `IRFuncRef`, each at most 8 bytes;
- function lists, linkage, arguments (including byval and sret), static allocas, and the entry block;
- iterators over blocks, successors, instructions, phis and operands;
- a local index for each value;
- 64 bits of auxiliary storage per block, which the adaptor holds for the compiler;
- per-part size and register bank for each value, phi incoming values, and constants' data;
- the optional hooks `prepare` and `finalize`.

### 4.4 Code generation and costs

- One pass handles instruction selection, register allocation and encoding.
- Allocation is greedy and local. Registers are picked lowest-number first and evicted round-robin. Values used in several blocks of the innermost loop may get a fixed callee-saved register.
- No register state is kept per block. At a branch to a block with several predecessors, or one that isn't the fall-through, all live values without a fixed register are spilled.
- The prologue is patched at the end, once the frame size is known.
- Time inside TPDE: code generation 49%, LLVM preparation pass 14%, analysis 12%.
- **No cost per instruction or throughput figure is published.**
- On SPEC CPU2017, against LLVM -O0:
  - compile time 8–24× faster (geometric mean 12.15× on x86-64, 18.96× on AArch64);
  - run time within ±9%;
  - code size larger by 43% (x86-64) and 49% (AArch64).
  - Against -O1, the code is 1.54× slower on x86-64 and 1.77× on AArch64.
- On Wasm through CLIF:
  - TPDE compiles 4.27× faster than Cranelift and 2.68× faster than Cranelift with its fast allocator, but **1.74× slower than Winch**. 37% of the time goes to translating Wasm into CLIF, which builds SSA that TPDE doesn't need.
  - TPDE's code is 1.14× faster than Winch's and 1.31× faster than Cranelift-fastalloc's, but 1.64× slower than Cranelift with its default allocator.
- On Umbra IR, TPDE matches Umbra's own DirectEmit backend in compile time and code quality, in 3.6k lines against 11k.

---

## 5. V8 leaves the Sea of Nodes for Turboshaft

Source: [Mercadier, "Land ahoy: leaving the Sea of Nodes", 2025-03-25](https://v8.dev/blog/leaving-the-sea-of-nodes).

Reasons:

- Managing effect and control chains by hand is error-prone.
- In JavaScript most nodes sit on those chains anyway: SoN amounts to a CFG in which only the pure nodes float.
- The graphs are hard to read.
- The scheduler is complex and can undo or duplicate work.
- No visit order is good, so peephole passes and state tracking are costly, and load elimination bailed out on large graphs.
- Nodes created during lowering sit far apart in memory, which hurts the cache.

Numbers:

- Compile time halved against SoN.
- The new load elimination is up to 190× faster.
- SoN had about 3× more L1 data-cache misses on average, and up to 7× in some phases. The author estimates this costs up to 5% of compile time and calls the figure hand-wavy.
- TurboFan's JavaScript backend and the Wasm pipeline now use Turboshaft.

Turboshaft's storage, from source (`v8/v8` @ `ec65c608`):

- Operations sit in an append-only `OperationBuffer` of 8-byte slots ([graph.h L33-L40](https://github.com/v8/v8/blob/ec65c60809a94d58ecde81f0f45a86a5288d2789/src/compiler/turboshaft/graph.h#L33-L40)).
- An operation takes at least 2 slots (16 B) and can be longer.
- `OpIndex` is a byte offset into the buffer, and ids are the offset divided by 16 ([index.h L64-L110](https://github.com/v8/v8/blob/ec65c60809a94d58ecde81f0f45a86a5288d2789/src/compiler/turboshaft/index.h#L64-L110)).

---

## 6. Cranelift CLIF (Wasmtime v50.0.1)

- **SSA with block parameters:** CLIF has no phi instructions and uses block parameters instead. A branch passes arguments, and the entry block's parameters are the function's parameters ([docs/ir.md L104-L130](https://github.com/bytecodealliance/wasmtime/blob/v50.0.1/cranelift/docs/ir.md#L104-L130)). `cranelift_frontend` builds SSA from mutable variables. Address-taken locals use stack slots with `stack_load` and `stack_store`.
- **Size:** two tests assert `size_of::<InstructionData>() == 16`. Formats that need more go out of line in a `Box` ([instructions.rs L1184-L1189, L1220-L1228](https://github.com/bytecodealliance/wasmtime/blob/v50.0.1/cranelift/codegen/src/ir/instructions.rs#L1184-L1228)).
- Block and instruction order is kept in doubly linked lists. Each instruction has an `InstNode` holding block, prev, next and seq, 4 × `u32` = 16 B ([layout.rs L601-L607](https://github.com/bytecodealliance/wasmtime/blob/v50.0.1/cranelift/codegen/src/ir/layout.rs#L601-L607)). An instruction thus takes at least 32 B before its result lists and values (my sum).
- **Tail calls:** `return_call` and `return_call_indirect` are block terminators, documented as direct and indirect tail calls. Caller and callee must use the same calling convention, and it must support tail calls ([meta instructions.rs L235-L275](https://github.com/bytecodealliance/wasmtime/blob/v50.0.1/cranelift/codegen/meta/src/shared/instructions.rs#L235-L275)). Only `CallConv::Tail` does, and it isn't ABI-stable ([call_conv.rs L17-L18, L91-L96](https://github.com/bytecodealliance/wasmtime/blob/v50.0.1/cranelift/codegen/src/isa/call_conv.rs#L91-L96)).
- **Compile speed:** **no figure per instruction or per second was found.** Published comparisons are all relative:
  - Engelke and Schwarz, CGO 2024 ([PDF](https://home.cit.tum.de/~engelke/pubs/2403-cgo.pdf)), on query compilation: Cranelift compiles only 20–35% faster than unoptimized LLVM, and a single-pass compiler is 16× faster than Cranelift with similar code.
  - The TPDE numbers in §4.4.
  - The Winch RFC's 15–20× (§11.3).

---

## 7. Structured IRs that also serve CPU backends

### 7.1 MLIR `scf` to `cf` (llvmorg-23.1.5)

- **Lowering** ([SCFToControlFlow.cpp L43-L117](https://github.com/llvm/llvm-project/blob/llvmorg-23.1.5/mlir/lib/Conversion/SCFToControlFlow/SCFToControlFlow.cpp#L43-L117)):
  - `scf.for` becomes an init block, a condition block whose block arguments are the induction variable and the loop-carried values, the body blocks, and a continuation block.
  - `scf.if` becomes a conditional branch to the then and else blocks, which join in a continuation block.
  - Each subgraph keeps a single entry and a single exit.
- **Values:** `scf` ops return values through `scf.yield` and the operands of `scf.condition`, not through phis.
- **Gives up:** `scf` has no `break` and no `continue`. Unstructured flow lives in the `cf` dialect, or inside `scf.execute_region`, which permits several blocks within an SCF op ([SCF dialect](https://mlir.llvm.org/docs/Dialects/SCFDialect/)).
- **The reverse direction:** `transformCFGToSCF` implements Bahmann et al., "Perfect Reconstructability of Control Flow from Demand Dependence Graphs" (TACO 2015). It lifts **any** single-entry CFG, irreducible ones included, into do-while loops plus multi-way branch regions. It does this by routing edges through a *multiplexer block* that dispatches on a block argument ([CFGToSCF.cpp L9-L95](https://github.com/llvm/llvm-project/blob/llvmorg-23.1.5/mlir/lib/Transforms/Utils/CFGToSCF.cpp#L9-L95)).

### 7.2 Slang IR (v2026.22)

Source: [docs/design/ir.md](https://github.com/shader-slang/slang/blob/v2026.22/docs/design/ir.md).

- **Block parameters as in SIL**, instead of phis:
  - The first N instructions of a block are `IRParam`s, and branches pass the arguments.
  - Critical edges are banned, so only branches to join points carry arguments (L109-L131).
  - The doc notes that block parameters suit an interpreter better than phis.
- **Merge points:** a structured conditional branch carries its join point as an operand. The operand isn't a CFG edge. SPIR-V instead puts merges in a separate instruction before the branch, which can drift away from it (L160-L182).
- **Gives up:**
  - No structured `continue` information. Code from a continue clause may be emitted twice, as `fxc` does.
  - Multi-level `break` must not appear.
  - Every pass must preserve the structure.
  - A Relooper-style restructuring is listed as future work (L184-L192).
- **CPU targets:** Slang can generate C++ source and binaries, through a downstream C/C++ compiler or `slang-llvm` ([docs/cpu-target.md](https://github.com/shader-slang/slang/blob/v2026.22/docs/cpu-target.md)).

### 7.3 Naga (wgpu v30.0.1)

Source: [naga/src/ir/mod.rs](https://github.com/gfx-rs/wgpu/blob/v30.0.1/naga/src/ir/mod.rs).

- **Two layers:** pure `Expression`s live in an arena and form a DAG. `Statement`s form a tree with side effects and structured control flow, and `Emit(range)` sets when expressions are evaluated (L1-L31).
- **Control statements** (around L1997-L2110):
  - `Block`, `If { accept, reject }` and `Switch` (with fall-through cases);
  - `Loop { body, continuing, break_if }`;
  - `Break`, which leaves only the *innermost* loop or switch;
  - `Continue`, `Return`, `Kill`.
- **No phis:** the docs state that Naga IR has no phi instructions. Values that cross control flow go through a `LocalVariable`.
- **Gives up:** labeled and multi-level breaks, `goto`, irreducible flow, and computed jumps.
- **Serves:** SPIR-V, MSL, HLSL, GLSL and WGSL. It has no CPU machine-code backend.

### 7.4 SPIR-V merge instructions (unified spec 1.6 revision 8)

Source: [SPIRV.html](https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html).

- **Placement:**
  - `OpSelectionMerge` must immediately precede `OpBranchConditional` or `OpSwitch`.
  - `OpLoopMerge` must immediately precede `OpBranch` or `OpBranchConditional`.
  - Each is the second-to-last instruction of its *header block*. `OpLoopMerge` names both a merge block and a *continue target*.
- **Rules (§2.11):**
  - A merge block belongs to one header only.
  - A header strictly dominates its merge.
  - Every back edge goes to a loop header, and each loop header has exactly one back edge.
  - Constructs nest, and are entered only through their header.
  - An exit must be one of the listed forms: break from a selection, break from the innermost loop, enter the continue construct, next iteration, a case-to-case branch, or break from the innermost switch.
  - Hence, the spec notes, the CFG formed by the structured edges is reducible.
- **Validation:** under "Validation Rules for Shader Capabilities", loops must have `OpLoopMerge` and selections must have `OpSelectionMerge` (§2.16.2). This rule is listed for Shader capabilities only, so it doesn't bind Kernel (OpenCL) modules. That reading comes from the section's placement, not from an explicit sentence.
- **Gives up:** irreducible flow, multi-level breaks (only innermost loop and switch exits), and computed jumps. `OpSwitch` takes literal cases, and core SPIR-V has no indirect branch. Values merge through `OpPhi`, or through `Function`-storage variables.

### 7.5 How Zig's SPIR-V backend uses AIR's structure (0.17.1)

Source: [codegen/spirv/CodeGen.zig](https://codeberg.org/ziglang/zig/src/tag/0.17.1/src/codegen/spirv/CodeGen.zig).

- **Always structured.** The function body goes through `genStructuredBody(.selection, …)` (L220), and the code has no unstructured path.
- **Selection bodies:** an AIR body can't exit a SPIR-V selection early. Each early exit (a `br` to an outer block) instead returns a "next block" id, a `u32` merged with `OpPhi` (`structuredNextBlock`, L7115-L7128). The backend then emits a **merge ladder**: nested `OpSelectionMerge`s, one per pending merge (`Block` union, L114-L147; L7159-L7260).
- **`loop`:** emits a header block with `OpLoopMerge` (merge and continue labels). Its body is generated with loop-exit semantics, so `br` out of the loop branches to the merge label (L7425-L7461).
- **`loop_switch_br`:** stores the operand in a `Function` variable, then emits `OpLoopMerge` around an `OpSelectionMerge` and `OpSwitch` on the loaded variable (around L8225-L8455).
- **`switch_dispatch`:** stores the new operand and "breaks" with a sentinel next-block id. At the switch merge, an `OpPhi` and a comparison decide between another iteration and an exit (L8459-L8469).
- **Cost of this mapping:** multi-level breaks become merge ladders with a phi and compares, and per-site dispatch is lost; every dispatch becomes "store, then loop again". AIR has no irreducible flow, so the backend never needs a restructuring algorithm.

### 7.6 Summary: what each structured IR gives up

| IR | Merge representation | Multi-level break | Irreducible CFG | Computed jump | CPU backends fed |
|---|---|---|---|---|---|
| Zig AIR | End of an enclosing `block`; value via `br` operand | yes (`br` to any enclosing block) | no, except inside `loop_switch_br`, a dispatch loop that can encode any state machine | no; `switch_dispatch` is the closest | C, x86_64, aarch64, LLVM, and SPIR-V via merge ladders |
| Wasm | Block end, typed by the block signature | yes (`br n`) | no | `br_table` only | Liftoff, Rabaldr, Winch, Cranelift, interpreters |
| MLIR `scf` | `scf.yield` results | no break or continue | no; lift handles it through `cf` | no | everything, after lowering to `cf` |
| Slang | join operand on the branch; block parameters | banned | no | no | C++, CUDA, `slang-llvm` |
| Naga | none; `LocalVariable`s | no (innermost only) | no | no | none (shader languages only) |
| SPIR-V Shader | `OpSelectionMerge`, `OpLoopMerge` and `OpPhi` | innermost loop and switch only | no | no | none (Kernel is a different profile) |

---

## 8. Coroutine lowering in the front end, and the IR level of each

| Language | Level | What goes in the frame | Source |
|---|---|---|---|
| C# (Roslyn) | Bound tree (the typed AST), after `LocalRewriter`, `AsyncExceptionHandlerRewriter` and `ClosureConversion`, then `IteratorRewriter` and `AsyncRewriter`, before IL is emitted ([MethodCompiler.cs L1512-L1616](https://github.com/dotnet/roslyn/blob/122e185e4e42d31cf1c1547652de387a612a5157/src/Compilers/CSharp/Portable/Compiler/MethodCompiler.cs#L1512-L1616)) | Release: only variables a dataflow walk finds live across an `await` or `yield`. Debug: all user locals, parameters and long-lived temporaries ([IteratorAndAsyncCaptureWalker.cs L16-L29, L123-L151](https://github.com/dotnet/roslyn/blob/122e185e4e42d31cf1c1547652de387a612a5157/src/Compilers/CSharp/Portable/Lowering/StateMachineRewriter/IteratorAndAsyncCaptureWalker.cs#L16-L151)) | Roslyn source |
| C#, "runtime async" (.NET 11 preview) | Roslyn emits no state machine. It marks the method `MethodImplOptions.Async` and lowers `await` to `AsyncHelpers.Await` calls; the **JIT** builds the state machine (the `RuntimeAsyncRewriter` branch at MethodCompiler.cs L1608-L1616) | Chosen by the runtime | [runtime-async spec](https://github.com/dotnet/runtime/blob/main/docs/design/specs/runtime-async.md), [Roslyn design](https://github.com/dotnet/roslyn/blob/main/docs/compilers/CSharp/Runtime%20Async%20Design.md). Its EnC support is an open TODO |
| Kotlin/JVM | Split: an IR lowering adds the `Continuation` parameter and classes ([AddContinuationLowering.kt L48](https://github.com/JetBrains/kotlin/blob/v2.4.21/compiler/ir/backend.jvm/lower/src/org/jetbrains/kotlin/backend/jvm/lower/AddContinuationLowering.kt#L48)). The **state machine itself is built on JVM bytecode**: `CoroutineTransformerMethodVisitor` works on an ASM `MethodNode`, runs `FixStack` and collects suspension points ([L18-L106](https://github.com/JetBrains/kotlin/blob/v2.4.21/compiler/backend/src/org/jetbrains/kotlin/codegen/coroutines/CoroutineTransformerMethodVisitor.kt#L18-L106)) | A `label` field plus fields for locals shared between states, one class per suspending lambda ([KEEP coroutines.md](https://github.com/Kotlin/KEEP/blob/master/proposals/coroutines.md)) | Kotlin source. Kotlin/JS builds it in IR (`StateMachineBuilder.kt` exists there) |
| Zig stage1 (≤ 0.10, removed) | Its own analyzed IR (`Stage1Air`) marks which expression scopes must spill across suspend points ([analyze.cpp ~L6790-L6870](https://codeberg.org/ziglang/zig/src/tag/0.10.1/src/stage1/analyze.cpp#L6790-L6870)). Zig's own LLVM codegen builds the frame struct with a resume-index field and an LLVM `switch` on it at entry, adding one case per suspend point ([codegen.cpp L283, L2786, L9540-L9561](https://codeberg.org/ziglang/zig/src/tag/0.10.1/src/stage1/codegen.cpp#L9540-L9561)). No LLVM coroutine intrinsics: "coro" appears 0 times in codegen.cpp | The frame struct holds `@fn_ptr`, `@resume_index`, `@awaiter`, result pointers, `@result`, stack-trace fields, the parameters, then **every alloca**: named locals and spill slots alike ([analyze.cpp L6892-L6960](https://codeberg.org/ziglang/zig/src/tag/0.10.1/src/stage1/analyze.cpp#L6892-L6960)) | Zig 0.10.1 source |
| Swift | SIL keeps `async` functions whole. IRGen emits `llvm.coro.id.async` and `llvm.coro.suspend.async` ([GenCall.cpp L257, L5161](https://github.com/swiftlang/swift/blob/swift-6.4.2-RELEASE/lib/IRGen/GenCall.cpp#L5161)). LLVM's CoroSplit splits the function into a ramp and **one resume function per suspend point** ([Coroutines.md L173-L252](https://github.com/llvm/llvm-project/blob/llvmorg-23.1.5/llvm/docs/Coroutines.md#L173-L252)) | Values live across a suspend sit at the tail of the caller-allocated "async context". The context size comes from an "async function pointer" struct, which lowering updates | LLVM docs and Swift source |
| Rust | **MIR**, after drop elaboration ([coroutine/mod.rs L1-L52](https://github.com/rust-lang/rust/blob/1.99.1/compiler/rustc_mir_transform/src/coroutine/mod.rs#L1-L52)) | Upvars, a `state: u32` (0 unresumed, 1 returned, 2 poisoned), then the MIR locals live across a suspension. Storage-liveness dataflow lets locals that never conflict share space (`storage_conflicts`, [layout.rs L64-L66](https://github.com/rust-lang/rust/blob/1.99.1/compiler/rustc_mir_transform/src/coroutine/layout.rs#L64-L66)). Two generated functions, `resume`/`poll` and the drop glue, each switch on the state | rustc source |

Zig's removal. The [0.12.0 notes](https://ziglang.org/download/0.12.0/release-notes.html) say async regressed in 0.11.0 and list four unsolved problems:

- LLVM can't optimize async functions;
- debuggers can't debug them;
- cancellation;
- async function pointers make the stack size unknowable.

The [0.15.1 notes](https://ziglang.org/download/0.15.1/release-notes.html) removed the `async` and `await` keywords and `@frameSize`, and moved async into the standard library's `Io` interface. `suspend`, `resume` and related machinery may return under the proposal "stackless coroutines as low-level primitives".

---

## 9. Edit and Continue: does the IR level matter?

Yes, in one specific way. Roslyn ties every emitted local to source syntax, not to an IR position:

- A `LocalDebugId` is the declarator's "syntax offset", its textual distance from the start of the method body, plus an ordinal when one declarator defines several variables ([LocalDebugId.cs L11-L26](https://github.com/dotnet/roslyn/blob/122e185e4e42d31cf1c1547652de387a612a5157/src/Compilers/Core/Portable/CodeGen/LocalDebugId.cs#L11-L26)).
- State-machine states are keyed by syntax offset plus an `AwaitDebugId`. That id separates the several awaits one syntax node produces, such as the two in `await foreach` ([AwaitDebugId.cs L7-L13](https://github.com/dotnet/roslyn/blob/122e185e4e42d31cf1c1547652de387a612a5157/src/Compilers/Core/Portable/CodeGen/AwaitDebugId.cs#L7-L13); [StateMachineStateDebugInfo.cs L19-L33](https://github.com/dotnet/roslyn/blob/122e185e4e42d31cf1c1547652de387a612a5157/src/Compilers/Core/Portable/CodeGen/StateMachineStateDebugInfo.cs#L19-L33)).

The previous generation's maps come from the baseline's debug information (`EditAndContinueMethodDebugInformation`). On an edit, `EncVariableSlotAllocator` maps each new local to its old slot through the syntax map. Without a syntax map the method's source is unchanged, so the ids match. Lambdas and closures are keyed by syntax offset relative to the top-level method ([EncVariableSlotAllocator.cs L82-L126](https://github.com/dotnet/roslyn/blob/122e185e4e42d31cf1c1547652de387a612a5157/src/Compilers/Core/Portable/Emit/EditAndContinue/EncVariableSlotAllocator.cs#L82-L126)). The allocator is passed into the iterator and async rewriters (§8). Roslyn therefore assigns slots and states while lowering the bound tree, where syntax is still at hand. In Debug builds it hoists *all* user locals into async frames, which keeps them inspectable and mappable.

For chibi-v2 this means the following:

- Instruction indices and frame offsets change with every edit, so they can't be identities. Each named local, closure and suspend point needs an id derived from syntax, such as an offset within the function or a declaration ordinal. That id has to survive into whichever IR the slot-assigning consumer reads: the interpreter's bytecode, the debug backend's frame layout, and the coroutine lowering.
- Machine code itself can be patched per function through the slot table, wherever the IR sits. Live state is what needs the ids: native frames still on the stack, which can finish in old code, and stackless coroutine frames, which can only be remapped if both their state numbers and their slots have stable ids.

---

## 10. Odin and Jai

- **Odin (`dev-2026-11a`):** no IR of its own. The LLVM backend walks the checked AST, through `lb_build_stmt(lbProcedure *p, Ast *node)` ([llvm_backend_stmt.cpp L3405](https://github.com/odin-lang/Odin/blob/dev-2026-11a/src/llvm_backend_stmt.cpp#L3405)) and `lb_build_expr(…, Ast *expr)` ([llvm_backend_expr.cpp L4632](https://github.com/odin-lang/Odin/blob/dev-2026-11a/src/llvm_backend_expr.cpp#L4632)), and builds LLVM IR through the C API. History, from the `src/` listings at those tags:
  - `v0.13.0` had its own `ir.cpp`, `ir_opt.cpp` and `ir_print.cpp` next to `llvm_backend.cpp`;
  - `dev-2023-10` and `dev-2024-06` had an experimental Tilde backend (`tilde_*.cpp`);
  - neither is present at `dev-2026-11a`.
- **Jai:** **UNVERIFIED.** No public compiler source or specification exists. Community documentation in the owner's `~/Personal/jai-refs` (JCL wiki) says:
  - compile-time code (`#run`) runs on bytecode;
  - `#dump` prints a function's bytecode and its basic blocks;
  - there is an `enable_bytecode_inliner` build option;
  - there are two backends, a fast x64 one without optimization and an LLVM one;
  - the 0.0.045 changelog claims 250,000 lines/s with x64.

  Whether the x64 and LLVM backends read that bytecode or a separate IR could not be confirmed from a primary source.

---

## 11. Single-pass baseline compilers over a stack-form IR

### 11.1 V8 Liftoff

- **Design** ([blog, Backes, 2018-08-20, V8 6.9](https://v8.dev/blog/liftoff); [docs](https://v8.dev/docs/wasm-compilation-pipeline)): one pass over the function body that decodes, validates and emits code. It builds no IR, which saves the time and memory an IR would cost.
- **Abstract stack** (source @ `ec65c608`):
  - Each entry of the compile-time "virtual stack" is a `LiftoffVarState`: `kStack`, `kRegister` or `kIntConst`, plus a spill offset ([liftoff-varstate.h L13-L33](https://github.com/v8/v8/blob/ec65c60809a94d58ecde81f0f45a86a5288d2789/src/wasm/baseline/liftoff-varstate.h#L13-L33)).
  - Constants emit nothing until they are used.
  - Wasm locals are the first entries of this state.
  - Registers carry a use count, because a register can appear in several stack entries (a local, then its `local.get`). A register is freed when its count reaches zero ([liftoff-assembler.h L158, L316-L335](https://github.com/v8/v8/blob/ec65c60809a94d58ecde81f0f45a86a5288d2789/src/wasm/baseline/liftoff-assembler.h#L316-L335)). Use counts are what frees registers; no liveness pass runs.
- **Merges** (`MergeIntoNewState`, [liftoff-assembler.cc L152-L230](https://github.com/v8/v8/blob/ec65c60809a94d58ecde81f0f45a86a5288d2789/src/wasm/baseline/liftoff-assembler.cc#L152-L230)):
  - The target state is built from the first edge that reaches it: locals, then a stack prefix, then the merge values.
  - Constants and duplicate registers are removed from the merge region, so later edges can move into it.
  - With more than one merge value, the merge region is spilled entirely, to avoid a parallel-move hazard.
  - Each later edge emits moves to match this state.
- **Loops** ([liftoff-compiler.cc L1857-L1869](https://github.com/v8/v8/blob/ec65c60809a94d58ecde81f0f45a86a5288d2789/src/wasm/baseline/liftoff-compiler.cc#L1857-L1869)): before a loop, Liftoff spills all locals and the loop arguments. The comment records that V8 tried pre-analysing the function to avoid reloading loop-invariant locals on every iteration, found no significant gain, and judged a pre-pass not worth it.
- **Speed:** tens of megabytes per second, per the docs, with no hardware given. The 2018 blog shows charts only.
- **Code quality** (2018, tier-up off): about 50% slower than TurboFan on Unity benchmarks on an HP Z840, 70% slower on a MacBook Pro, and 18–54% slower on PSPDFKit. Titzer (2022) measured about 2× slower than TurboFan on PolyBench.

### 11.2 SpiderMonkey's baseline Wasm compiler (RabaldrMonkey)

Source: `mozilla-firefox/firefox` @ `5fb56bef`.

- **Value stack:** `Stk` entries are typed `Mem*` (spilled), `Local*` (a *deferred* read of a local slot), `Register*` or `Const*`, for i32, i64, f32, f64, v128 and ref ([WasmBCStk.h L28-L76](https://github.com/mozilla-firefox/firefox/blob/5fb56bef1f2d93bf32a9452c4442a6360bc6e3be/js/src/wasm/WasmBCStk.h#L28-L76)). The compiler keeps this stack itself because it must scan it to spill ([WasmBCClass.h L108](https://github.com/mozilla-firefox/firefox/blob/5fb56bef1f2d93bf32a9452c4442a6360bc6e3be/js/src/wasm/WasmBCClass.h#L108)).
- **Control:** a `Control` node records the labels and stack heights at join points ([WasmBCClass.h L63](https://github.com/mozilla-firefox/firefox/blob/5fb56bef1f2d93bf32a9452c4442a6360bc6e3be/js/src/wasm/WasmBCClass.h#L63)). For blocks, loops and ifs, the policy is ([WasmBaselineCompile.cpp L4172-L4183](https://github.com/mozilla-firefox/firefox/blob/5fb56bef1f2d93bf32a9452c4442a6360bc6e3be/js/src/wasm/WasmBaselineCompile.cpp#L4172-L4183)):
  - **sync**, spilling the value stack, *before entering* the block;
  - so every exit may assume that no register is live, except one;
  - that one is the exit value, which goes in a designated join register for its type.
  - My reading: this is simpler than Liftoff's merge, at the price of more spills.
- Tier-up is driven by a hotness counter that is decremented on entry and on each loop iteration, two instructions on Intel (file header).
- **Numbers** ([Clark, Mozilla Hacks, 2018-01-17](https://hacks.mozilla.org/2018/01/making-webassembly-even-faster-firefoxs-new-streaming-and-tiering-compiler/)):
  - The baseline compiler is 10–15× faster than the optimizing compiler, and its code only 2× slower.
  - 30–60 MB of Wasm per second on desktop and 8 MB/s on an average phone. These figures describe the streaming and tiered pipeline, not the baseline compiler alone, and give no hardware.

### 11.3 Wasmtime's Winch (v50.0.1)

- **Design** ([RFC](https://github.com/bytecodealliance/rfcs/blob/main/accepted/wasmtime-baseline-compilation.md), Cabrera and Fallin):
  - Single pass with no IR.
  - A value stack tagged as local, register, constant or memory offset.
  - Registers are allocated in the same pass, and locals and registers spill when none are free *and at control-flow points*.
  - Limited constant rematerialization.
- **Source:**
  - `Val` is `I32`, `I64`, `F32`, `F64` or `V128` constants, `Reg`, `Local` or `Memory` ([stack.rs L87-L106](https://github.com/bytecodealliance/wasmtime/blob/v50.0.1/winch/codegen/src/stack.rs#L87-L106)).
  - Entering a block spills live registers and locals (L417). Leaving one spills again (L1018).
  - Multi-value results are placed following SpiderMonkey's approach ([control.rs L852-L874](https://github.com/bytecodealliance/wasmtime/blob/v50.0.1/winch/codegen/src/codegen/control.rs#L852-L874)).
- **Speed:**
  - The RFC's rough Sightglass measurements, comparing Cranelift, Liftoff and RabaldrMonkey: a baseline compiler compiles 15–20× faster, with code 1.1–1.5× slower.
  - The TPDE paper (§4.4): Winch compiles 1.74× faster than TPDE-on-CLIF, which itself is 4.27× faster than Cranelift. Multiplied, that is about 7.4× faster than Cranelift in that setup (my product). TPDE's code is 1.14× faster than Winch's.

### 11.4 What the three have in common

- No liveness pass: a Wasm operand is consumed when it is popped, and use counts cover duplicates (Liftoff).
- Lazy materialization: all three defer constants, and Rabaldr and Winch also defer local reads through `Local` stack entries.
- At control flow, all three force a canonical state. Rabaldr and Winch spill everything; Liftoff makes the first edge canonical and spills all locals before a loop.
- What it costs: code about 1.1–2× slower than the optimizing tier (§11.1–11.3). These sources don't measure how much of that comes from merges.

A related design, V8's Sparkplug ([Swirski, 2021-05-27](https://v8.dev/blog/sparkplug)), is a non-optimizing JavaScript compiler that reads **Ignition bytecode** directly:

- It deliberately keeps frames identical to the interpreter's, so debuggers, the profiler, exceptions and OSR work unchanged.
- It makes two passes, one to find loops and one to emit code, and only local peepholes are possible.
- Speedometer improved 5–10%.

---

## Options for chibi-v2's IR

The options below are drawn from the evidence above. The owner hasn't decided any of them.

**Structured or CFG.**

- **Nested (AIR-like):** nested bodies, `block` with a result, `loop` plus `repeat`, `br` to any enclosing block, `cond_br` and `switch` with bodies, and a dispatch loop for labeled `switch` (§1.2).
  - It is already proven to feed C, x86_64, LLVM and SPIR-V.
  - Layout follows source order, loops are explicit, and liveness needs only a loop pre-pass (§1.7). TPDE's whole analysis pass (layout, loop detection and liveness) takes 12% of its time; structure removes the layout and loop-detection parts.
  - The SPIR-V writer builds merge ladders for multi-level breaks (§7.5).
  - chibi has no `goto` after the token audit, so ordinary source can't produce irreducible flow. Labeled `switch` with `continue name value` is the one construct that can, and the dispatch construct covers it.
- **Annotated CFG (Slang-like, merges on branches):** more general, but every pass must preserve the merges, multi-level breaks are banned, and a one-pass backend needs ordering and loop analysis (§4.2, §7.2).
- **Plain CFG (CLIF, TPDE input):** shader writers would need the CFG-to-SCF lift from §7.1.
- Nested fits the decision that GPU writers read structure. Its one hard case is coroutines (see below).

**SSA values or local slots.**

- AIR, Naga and Wasm all avoid phis: AIR uses block results, Naga uses `LocalVariable`s, and Wasm uses typed block results. Block parameters (CLIF, Slang) and phis (TPDE) appear where an optimizer or LLVM-IR is the input.
- One mix fits all four consumers and Edit and Continue: SSA for expression temporaries, plus slots for named mutable locals and address-taken values (`alloc`, `load` and `store`, as in AIR).
  - Slots give each local a stable identity for the debugger and EnC. Roslyn in Debug hoists all user locals into async frames, which keeps them inspectable and mappable (§8, §9).
  - Slots also become `Function` variables in SPIR-V and plain C variables in the C backend.
  - The release path goes through C or LLVM, which promote slots to registers anyway.

**Encoding and byte budget.**

| Format | Bytes per instruction |
|---|---|
| Wasm | 1-byte opcode plus LEB immediates; operands implicit |
| Lua | 4 |
| H9 bytecode | 8 |
| Zig AIR | 1 + 8, plus 4 for body membership, plus `extra` |
| Carbon SemIR | 16 |
| Turboshaft | at least 16, variable |
| CLIF | 16, plus a 16-byte layout node, plus values |
| Umbra IR | variable length, references are offsets into the buffer ([CGO 2024](https://home.cit.tum.de/~engelke/pubs/2403-cgo.pdf) §III) |
| CPython 3.11 | about 6, including inline caches |

- Bytes are not the binding cost. At k IR instructions per line and b bytes each, the IR traffic is k·b bytes per line; k = 10 and b = 16 gives 160 MB/s at 1M lines/s. k is unmeasured, and no compiler publishes it (§1.8). H6's real limit is cycles per instruction in each consumer.
- Two encoding ideas worth keeping:
  - AIR's `u32` reference with a tag bit for an interned constant versus an instruction (§1.3);
  - types interned and named by index.

**One IR, or IR plus bytecode.**

The evidence favours the existing decision: one typed IR, plus a bytecode lowered from it for the interpreter.

- Every fast Wasm interpreter except Wizard rewrites its input. Rewriting typically takes up to 2–4× the bytecode's space, and it is about 10× cheaper than baseline compilation (§2.2).
- Register form executes 46–47% fewer instructions than stack form, and a register JVM ran 1.15–1.48× faster (§3.1). Lua's `sum` loop took 44% of the time after the switch (§3.2).
- The step from typed IR to frame-offset bytecode is one linear pass.
- Sparkplug's frame sharing (§11.4) is an option to weigh: give the debug backend the bytecode's frame layout. Moves between interpreter and native code, and hot reload, then need no frame translation.

**Tail calls and per-site switch dispatch.**

- **Guaranteed tail calls must be their own instruction**, checked in the checker:
  - Zig requires an identical function type (§1.5).
  - CLIF requires one calling convention that supports tail calls (§6).
  - As a **terminator** (CLIF's `return_call`), nothing needs to pattern-match a call followed by `ret`. Zig's C backend has to look for exactly that pair.
  - Every backend must implement it or reject it. Zig's self-hosted backends reject it today. The C backend needs `musttail`, which Zig's `zig.h` refuses to emulate.
  - Through the hot-reload slot table, a tail call is an indirect jump.
- **Per-site dispatch must stay its own instruction** that names the switch, for example `dispatch(switch_inst, value)`, never "store, then jump to the head". Backends can then choose:
  - one indirect jump per site through a shared table (Zig x86_64 and LLVM);
  - a direct jump when the value is a constant (Zig's C backend);
  - a loop around a switch on a variable (SPIR-V, and Zig's C backend for runtime values).
  - Once lowered to the shared form, the per-site version can't be recovered.
- The interpreter gets the same property from threaded or tail-call dispatch. H9 found threaded dispatch 16–25% cheaper per operation than `switch` on three of its four programs. On 2026-11-12, tail-call dispatch (`musttail` with `preserve_none`) took about 5% fewer cycles than threaded on the two loops and about 17% fewer on `fib`. Its `calls` row depends on code layout (`~/Personal/chibi-v2/experiments/h9-interpreter/README.md`).
- **Computed `goto`** exists in none of the structured IRs surveyed (§7.6). Adding it would force a CFG IR and cut off the SPIR-V writers. Titzer also notes it yields irreducible loops in C interpreters.

**What coroutine lowering needs.**

The precedents lower at the typed tree (Roslyn), MIR (Rust), LLVM IR (Swift) and JVM bytecode (Kotlin), and .NET is moving it into the JIT (§8). Lowering in the front end leaves ordinary IR for all four consumers. It needs:

- **Suspend points** as explicit instructions with stable ids (§9).
- **Live values across suspends.** Either a liveness pass, as Rust's dataflow and storage overlap do, or "all locals in the frame" for debug builds, as Roslyn in Debug and Zig's stage1 frames do.
- **A known frame size per coroutine, also through function pointers.** Swift stores the size in an async function pointer struct; Zig dropped async partly because function pointers made the size unknown.
- **A way to resume into the middle of a loop.**
  - That gives the loop a second entry, which is irreducible in a CFG.
  - Rust and Roslyn lower to CFG and `goto` forms where this is free. A structured IR has to express it some other way: through the dispatch-loop construct, by re-dispatching at each enclosing loop header, or by lowering coroutines into a CFG-shaped subset.
  - This is the main argument against pure structure, and it needs a decision.

**Stack form (Wasm-like) or explicit operand references (AIR, SemIR) when every consumer is single-pass.**

| | Stack-form typed IR | Explicit references |
|---|---|---|
| Size | Smallest: operands are implicit | 9–16 B per instruction, plus bodies |
| Liveness for a one-pass backend | Free: popping consumes, and use counts cover `local.get` copies (§11.1) | Needs tomb bits: AIR's separate pass, or bits the checker writes as it emits |
| A value used twice | Needs a local (`local.tee`, `local.get`) | Direct |
| Code at merges | Forced canonical state; Rabaldr and Winch spill everything | A backend can see uses and pick a state; TPDE still spills at joins |
| SPIR-V and GPU writers | One abstract-interpretation pass to give values SSA ids, plus `OpPhi` or variables at merges | Instruction index is the SSA id |
| C writer | Stack positions become temporaries | One temporary per instruction |
| Interpreter | Must be rewritten into register form anyway (§2.2, §3.1) | One linear pass assigns frame offsets |
| Coroutine liveness | Operand-stack values live across a suspend must be spilled (Kotlin's `FixStack`, §8) | Ordinary liveness |
| Validation | Single pass, as Wasm's is | Single pass over references |

Liftoff's experience bears on the liveness row: V8 found a pre-pass over loops not worth its complexity at baseline code quality (§11.1).

A middle option, from my analysis rather than any source: emit explicit references in post-order, and have the checker write an "operand dies here" bit as it emits each instruction, in AIR's 4-bit Liveness format. Expression temporaries have exactly one use by construction, so their bits are known at once. Only named values used more than once need a last-use computation, or can become slots. One-pass consumers then get stack-form liveness for free, while the SPIR-V and C writers, the interpreter lowering and coroutine lowering keep random access.

---

## Not verified or not found

- **Not found, no published figure:**
  - AIR instructions per source line or per function;
  - cost per instruction for TPDE, Cranelift or Liftoff;
  - absolute compile throughput for Cranelift;
  - Liftoff and Rabaldr speeds with hardware and version. Liftoff's docs say tens of MB/s; Mozilla's 30–60 MB/s covers the whole pipeline.
- **UNVERIFIED:** Jai's IR structure (all claims come from the community wiki).
- **Inferred, not stated by the spec:** SPIR-V Kernel modules are exempt from the structure rule, read from where that rule is placed.
- **Not checked:** the purpose of AIR's "target" `Inst.Index`.
- **Unconfirmed:**
  - whether `--verbose-air` works in release builds of the Zig compiler;
  - Roslyn's dispatch behaviour inside `try` blocks, which is why it isn't described here;
  - the Wasm 3.0 spec date shown on the page (2026-11-04).

## Pinned sources

| Project | Version |
|---|---|
| Zig | Codeberg tag `0.17.1`, commit `be248cc1fdc3cc654f9ec8ba68514bcb93b204fe`; stage1 from tag `0.10.1`; release notes for 0.5.0, 0.10.0, 0.12.0, 0.14.0 and 0.15.1 |
| TPDE | `tpde2/tpde` @ `efe4cc7759e4bf99146d6ad9834636a7bcc0f1f6`; arXiv 2505.22610v1 |
| V8 | `v8/v8` @ `ec65c60809a94d58ecde81f0f45a86a5288d2789`; v8.dev posts as dated above |
| Wasmtime, Cranelift and Winch | tag `v50.0.1` |
| LLVM and MLIR | tag `llvmorg-23.1.5` |
| Slang | tag `v2026.22` |
| Naga | wgpu tag `v30.0.1` |
| SpiderMonkey | `mozilla-firefox/firefox` @ `5fb56bef1f2d93bf32a9452c4442a6360bc6e3be` |
| Roslyn | `dotnet/roslyn` @ `122e185e4e42d31cf1c1547652de387a612a5157` |
| Kotlin | tag `v2.4.21` |
| Swift | tag `swift-6.4.2-RELEASE` |
| Rust | tag `1.99.1` |
| Odin | tags `dev-2026-11a`, `v0.13.0`, `dev-2023-10` and `dev-2024-06` |
| wasm3 | `28ecb9af6d2040e474a70f7cb7f43666740141fb` |
| wasmi | blog posts for v0.32 and v2.0; crate tag `v2.0.0` |
| SPIR-V | unified spec 1.6 revision 8 |
| WebAssembly | core spec page, 3.0 |
| Papers | Titzer, PACMPL 6(OOPSLA2) 2022; Shi et al., VEE 2005 and TACO 2008; Ierusalimschy et al., J.UCS 2005; Casey, Ertl and Gregg, TOPLAS 2007; Engelke and Schwarz, CGO 2024 |
