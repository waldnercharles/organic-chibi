# Fast compilers: how the fastest real compilers produce runnable code

Research for chibi-v2, 2026-11-09. Primary sources only. Code links are pinned to a commit.

## Summary

1. Only one compiler has a primary-source measurement near 1M lines/s for a full build that includes linking. That compiler is tcc 0.9.22: 859,000 lines/s on a 2.4 GHz Pentium 4, single-threaded, with header-heavy C ([bellard.org/tcc](https://bellard.org/tcc/)). It has no IR. It parses straight into machine code and has its own linker.
2. LLVM is the wall. C3 and Odin report that LLVM plus the linker take 90–98% of a debug build. Zig's self-hosted x86 backend cut hello-world from 918 ms to 275 ms.
3. The front end fits the budget. Carbon measures about 1 µs per line through type checking on one core. chibi-v2's lexer takes about 135 cycles per line, about 3% of the one-core cost of 1M lines/s (about 4,000 cycles per line at an assumed 4 GHz).
4. Inside IR-based backends, register allocation is the cost. Single-pass per-function backends are 6x to 19x faster and produce -O0-class code: TPDE (12–19x faster than LLVM -O0) and Cranelift's Fastalloc (register allocation about 6x faster than its default allocator).
5. Real parallel designs have the same shape: parse per file in parallel, check mostly serially, generate code per function in parallel, and link in one deterministic ordered step (Zig, Go).
6. Compile-time execution, generics and hot reload all need a re-enterable unit per function: a compact typed IR per function, plus a code atom per function that keeps its relocations. Strict tcc single pass cannot provide these.
7. A shape that fits all the evidence: a data-oriented front end, then a compact per-function IR, then a tcc/TPDE-style one-pass emitter per function running on all cores, then your own Mach-O/ELF/PE writer or in-memory image with call indirection.

---

## Contents

1. [The budget on an Apple M4](#1-the-budget-on-an-apple-m4)
2. [tcc in depth](#2-tcc-tinycc-in-depth)
3. [Other compilers](#3-other-compilers)
4. [Comparison table](#4-comparison-table)
5. [Which designs reached about 1M lines/s](#5-which-designs-reached-about-1m-liness-for-a-full-build)
6. [Cost per stage](#6-cost-per-stage)
7. [Single pass versus IR for chibi-v2](#7-single-pass-versus-ir-for-chibi-v2-interactions-only)
8. [Not verified](#8-not-verified)
9. [Code to read first](#9-code-to-read-first)

**Pinned sources.** Every code link below points at these commits.

| Project | Commit or tag | Home |
|---|---|---|
| tcc | `aade7baf` (mob, 2026-11-09) on the [GitHub mirror](https://github.com/TinyCC/tinycc), which describes itself as an "Unofficial mirror of mob" | [repo.or.cz/tinycc.git](https://repo.or.cz/tinycc.git) |
| Zig | tag 0.15.2 (`e4cbd752`), plus Codeberg 0.17.1 | [ziglang/zig](https://github.com/ziglang/zig), [codeberg](https://codeberg.org/ziglang/zig) |
| Go | go1.27.3 (`2aae1000`) | [golang/go](https://github.com/golang/go) |
| Carbon | `ca189850` | [carbon-lang](https://github.com/carbon-language/carbon-lang) |
| Wasmtime/Cranelift | `3e0ee3fb`; regalloc2 `4a233442` | [wasmtime](https://github.com/bytecodealliance/wasmtime) |
| TPDE | `9779acf4` | [tpde2/tpde](https://github.com/tpde2/tpde) |
| MIR | `dc2c763f` | [vnmakarov/mir](https://github.com/vnmakarov/mir) |
| QBE | `180d2eb4` | [c9x.me/git/qbe.git](https://c9x.me/git/qbe.git) |
| Others | listed inline | |

---

## 1. The budget on an Apple M4

These are derived numbers. The clock is an assumption: Apple does not publish M4 clock speeds.

| Quantity | Value | How it is derived |
|---|---|---|
| 1M lines/s on one performance core | about 4,000 cycles per line | at an assumed ~4 GHz |
| 1M lines/s on 10 performance cores, perfect scaling | about 40,000 core-cycles per line | 10 × 4,000 |
| chibi-v2 lexer today | about 135 cycles per line, 9 tokens per line | owner's measurement |
| tcc 0.9.22, whole pipeline including link | about 2,800 cycles per line | 2.4 GHz ÷ 859k lines/s ([bellard.org/tcc](https://bellard.org/tcc/)) |
| Carbon lex + parse + check, one core | about 1.0 µs per line | from [PR #4408](https://github.com/carbon-language/carbon-lang/pull/4408), 16,384-line row; hardware not stated |

**What this means.**
- A tcc-like pipeline already fits inside the one-core budget, even on 2004 hardware.
- A Carbon-like checker alone uses about one core's whole budget. It needs parallelism, or a cheaper check, to leave room for code generation and linking.

---

## 2. tcc (TinyCC) in depth

**Code location.** Base URL: `https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/`. Each link below uses it.

**The main source is tcc's own manual.** The developer chapter in [`tcc-doc.texi` L1500-L1980](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tcc-doc.texi#L1500-L1980) is short and accurate. Read it first.

### 2.1 Pipeline

- **Stages in one call.** `tcc_compile` takes a global lock, opens the file, then runs `preprocess_start`, `tccgen_compile` and `tccelf_end_file` ([libtcc.c L801-L844](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/libtcc.c#L801-L844)).
- **No token array.** The parser pulls one token at a time with `next()` ([tccpp.c L3470](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tccpp.c#L3470)). Identifiers are interned in a 16,384-bucket hash table into `TokenSym` records ([tcc.h L431-L445](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tcc.h#L431-L445)).
- **One pass, three exceptions.** The manual says the parser "does only one pass", except for these cases ([tcc-doc.texi L1531-L1547](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tcc-doc.texi#L1531-L1547)):
  - It counts the elements of arrays that have an initializer but no size.
  - It reverses argument order on some targets.
  - It records each `inline` function body as a token string and parses it again where the function is used.
- **No IR.** "The TCC code generator directly generates linked binary code in one pass… Optimization is only done at the expression level. No intermediate representation of expression is kept except the current values stored in the value stack" ([tcc-doc.texi L1744-L1760](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tcc-doc.texi#L1744-L1760)).
- **Name lookup costs one pointer load.**
  - Each `TokenSym` points directly to the innermost visible `Sym` for that name (`sym_identifier`, `sym_struct`, …).
  - Shadowing chains through `prev_tok`. Leaving a scope pops the chain ([tccgen.c L720-L734](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tccgen.c#L720-L734), [L745-L763](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tccgen.c#L745-L763)).
  - So resolving a name never hashes. The lexer already produced the interned id.
- **Symbols come from a free list.** `Sym` records are pooled in 8 KB blocks ([tccgen.c L629-L660](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tccgen.c#L629-L660)).
- **Code goes straight into a section buffer.** `ind` is the current offset in `cur_text_section` ([tcc-doc.texi L1680-L1700](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tcc-doc.texi#L1680-L1700)).
- **Per-function driver.** `gen_function` does the following ([tccgen.c L8662-L8737](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tccgen.c#L8662-L8737)):
  - sets `ind`;
  - emits the prologue;
  - parses the body with `block(0)`, which emits code as it parses;
  - patches the return jumps;
  - emits the epilogue;
  - writes the symbol size.

### 2.2 The value stack: `SValue`, `vtop`, `vset`/`vpush`, `gv`

**The stack.**
- `vstack` is a fixed array of 512 `SValue`s, and `vtop` points at its top entry ([tccgen.c L48-L50](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tccgen.c#L48-L50), [tcc.h L426](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tcc.h#L426)).
- The stack holds only the operands of the expression being parsed, so it stays in L1 cache.

**The `SValue` record** ([tcc.h L482-L496](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tcc.h#L482-L496)) has these fields:
- `type`: a `CType` holding an int plus a `Sym*` reference.
- `r`: where the value lives. The low 6 bits are a register number or one of the location codes below; the high bits are flags.
- `r2`: the second register of a two-word value.
- A union of `CValue c` (a constant) and `{jtrue, jfalse}`, which are the heads of pending jump lists.
- A union of `Sym *sym` and `{cmp_op, cmp_r}`.

**Location codes and flags** ([tcc.h L1026-L1034](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tcc.h#L1026-L1034)):

| Code | The value is… |
|---|---|
| `VT_CONST` | a constant in `c` |
| `VT_LOCAL` | at a stack-frame offset |
| `VT_LLOCAL` | a spilled lvalue |
| `VT_CMP` | in the CPU flags |
| `VT_JMP`, `VT_JMPI` | the result of pending jumps |
| `VT_LVAL` (flag) | an address that must be dereferenced to get the value |
| `VT_SYM` (flag) | relative to a symbol, so it needs a relocation |

The manual calls `VT_LVAL` the key to understanding tcc ([tcc-doc.texi L1762-L1866](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tcc-doc.texi#L1762-L1866)).

**Pushing values.**
- `vsetc` bumps `vtop` and fills the entry ([tccgen.c L888-L899](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tccgen.c#L888-L899)).
- It first calls `vcheck_cmp` ([L864-L886](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tccgen.c#L864-L886)). If the old top is a flags value, `vcheck_cmp` materializes it into a register, because the next instruction may clobber the flags.
- `vset` and `vpush` are thin wrappers ([L931-L970](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tccgen.c#L931-L970)).
- `vswap`, `vrotb` and `vrott` reorder the top entries ([L901-L1028](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tccgen.c#L901-L1028)).

**Code is emitted only when a value is forced.**
- Parsing `x + 1` pushes an lvalue (`VT_LOCAL|VT_LVAL`) and a constant (`VT_CONST`). Then `gen_op('+')` runs.
- `gen_opic` folds the operation when both sides are constant ([tccgen.c L2363-L2549](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tccgen.c#L2363-L2549)). It also does these cheap rewrites:
  - turns multiplication or division by a power of two into a shift;
  - drops operations that do nothing, such as `x*1`;
  - folds `symbol + constant` and `local + constant` into the address.
- Only when an operand has to live in a register does `gv(rc)` emit a load ([tccgen.c L1847-L1998](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tccgen.c#L1847-L1998)). The manual calls `gv` "the most important function of the code generator" ([tcc-doc.texi L1868-L1898](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tcc-doc.texi#L1868-L1898)).
- `gv2` loads the top two entries ([L2001-L2026](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tccgen.c#L2001-L2026)).

**This deferral is tcc's whole instruction selector.** The backend sees each operand's location (constant, local, symbol or register) and picks the instruction form to match. For example:
- arm64 `arm64_gen_opic` emits `add` with a 12-bit immediate, shifted or unshifted, before it falls back to a register form ([arm64-gen.c L1725-L1802](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/arm64-gen.c#L1725-L1802)).
- x86-64 `gen_opi` uses the `imm8` form when the constant fits ([x86_64-gen.c L1671-L1700](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/x86_64-gen.c#L1671-L1700)).

**Comparisons and `&&`/`||` never produce a 0/1 value unless one is needed.**
- A comparison leaves `VT_CMP` with the condition code in `cmp_op` ([tccgen.c L1031-L1037](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tccgen.c#L1031-L1037)).
- `gvtst` turns a value into a conditional jump and appends that jump to a list ([L1059-L1097](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tccgen.c#L1059-L1097)).
- `vset_VT_JMP` builds a 0/1 value only when one is actually needed ([L1040-L1056](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tccgen.c#L1040-L1056)).

**Dead code is suppressed with a flag, not removed by a pass.**
- `nocode_wanted` is a bitfield ([tccgen.c L52-L68](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tccgen.c#L52-L68)). One bit is set after an unconditional jump, and a 16-bit counter tracks `sizeof`/`typeof` operands.
- The emitters return early when the flag is set ([arm64-gen.c L115-L124](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/arm64-gen.c#L115-L124)).
- Code turns back on at the next label (`gsym`, `gind`) ([tccgen.c L170-L201](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tccgen.c#L170-L201)).
- So `if (0) {...}` costs parsing only.

### 2.3 Register allocation

**Registers hold only expression temporaries.** Named locals always live in the stack frame at a fixed offset (`VT_LOCAL`). The register sets are:
- x86-64: 25 entries, of which rax, rcx and rdx are in `RC_INT`. rsi, rdi and r8–r11 serve only as fixed classes for argument passing, and xmm0–xmm5 are `RC_FLOAT` ([x86_64-gen.c L127-L161](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/x86_64-gen.c#L127-L161)).
- arm64: 28 entries. x0–x15 are `RC_INT`, x16 and x17 are reserved for the generator, x18 is reserved on macOS and Windows, x30 is special, and v0–v7 are float ([arm64-gen.c L14-L30](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/arm64-gen.c#L14-L30), [L63-L92](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/arm64-gen.c#L63-L92)).

**`get_reg(rc)`** ([tccgen.c L1490-L1527](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tccgen.c#L1490-L1527)):
- It returns the first register of the class that no value-stack entry uses. That is a linear scan over at most 28 registers times the stack depth.
- If none is free, it spills the register of the oldest entry, scanning from the bottom of the stack, so the operands of the current operation are never taken.

**Spilling.**
- `save_reg_upstack` stores the register to a stack temporary and rewrites every entry that referred to it as `VT_LOCAL|VT_LVAL` or `VT_LLOCAL` ([L1409-L1466](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tccgen.c#L1409-L1466)).
- Temporaries are reused from a pool of 8 slots, tracked with a bitmask ([L1531-L1571](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tccgen.c#L1531-L1571), [L104](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tccgen.c#L104)).

**Calls flush everything.** `gfunc_call` starts with `save_regs(nb_args + 1)`, which spills every register still live on the value stack ([arm64-gen.c L1025-L1037](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/arm64-gen.c#L1025-L1037), [tccgen.c L1394-L1400](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tccgen.c#L1394-L1400)). Since no register survives a call, tcc never saves callee-saved registers.

**There is no liveness analysis, no graph and no interval.** The allocator is the value stack.

### 2.4 Jumps, forward references and relocations

**Forward jumps are a linked list threaded through the code buffer itself.**
- `gjmp(t)` emits a jump whose displacement field holds `t`, the previous head of the list, and returns the new jump's offset ([x86_64-gen.c L1612-L1616](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/x86_64-gen.c#L1612-L1616), [arm64-gen.c L1624-L1632](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/arm64-gen.c#L1624-L1632)).
- When the label is reached, `gsym_addr(t, a)` walks the chain and writes the real displacements:
  - x86-64 writes `rel32` values ([x86_64-gen.c L233-L241](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/x86_64-gen.c#L233-L241)).
  - arm64 writes `B` instructions, or a `NOP` when the target is the next instruction ([arm64-gen.c L243-L257](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/arm64-gen.c#L243-L257)).
- `gjmp_append` concatenates two lists ([arm64-gen.c L1640-L1652](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/arm64-gen.c#L1640-L1652)).
- Forward references cost no memory outside the code bytes.

**Control flow is a few lines per statement.** For `if` and `while` ([tccgen.c L7251-L7281](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tccgen.c#L7251-L7281)):
- `a = gvtst(1, 0)` starts the "false" list.
- `block()` emits the body.
- `gsym(a)` patches the list.
- Loops record the head with `gind()` and jump back with `gjmp_addr()`.

**Backward jumps know their target.** x86-64 picks the 2-byte short form when it fits ([x86_64-gen.c L1618-L1628](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/x86_64-gen.c#L1618-L1628)). Forward jumps are always 5 bytes, because tcc never relaxes branches.

**arm64 conditional branches take two instructions.** `gjmp_cond` emits `cbz`/`cbnz` (or `b.cond` for floats) to `.+8`, followed by an unconditional `b` that joins the jump list ([arm64-gen.c L1677-L1709](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/arm64-gen.c#L1677-L1709)). This gives every conditional the ±128 MB range of `b`, so tcc needs no veneers or range checks.

**The frame size is unknown until the function ends, so the prologue is patched.**
- x86-64 reserves 11 bytes, and the epilogue writes `push rbp; mov rbp,rsp; sub rsp,N` into them ([x86_64-gen.c L1417-L1438](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/x86_64-gen.c#L1417-L1438), [L1577-L1603](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/x86_64-gen.c#L1577-L1603)).
- arm64 reserves 6 `NOP` slots and overwrites them with the stack adjustment ([arm64-gen.c L1224](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/arm64-gen.c#L1224), [L1352-L1356](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/arm64-gen.c#L1352-L1356), [L1584-L1611](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/arm64-gen.c#L1584-L1611)).

**Cross-symbol references become ELF relocations at the moment of emission.**
- `greloca` → `put_elf_reloca` appends one `Elf64_Rela` to the `.rela<section>` of the current section ([tccelf.c L789-L817](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tccelf.c#L789-L817)).
- arm64 examples: calls use `R_AARCH64_CALL26`. Static data uses `adrp` + `add` with `ADR_PREL_PG_HI21` and `ADD_ABS_LO12_NC`. Non-static data goes through the GOT ([arm64-gen.c L471-L515](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/arm64-gen.c#L471-L515), [L673-L687](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/arm64-gen.c#L673-L687)).
- Symbols are ELF symbols in `symtab_section` from the start, so the compiler's output is already linker input.

### 2.5 The linker and `tcc -run`

**ELF is tcc's only internal format.**
- Every target, including Mach-O and PE, compiles into ELF sections, ELF symbols and ELF relocations ([tcc-doc.texi L1680](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tcc-doc.texi#L1680)).
- `tcc -c` always writes an ELF object, even on macOS and Windows ([tccelf.c L3223-L3237](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tccelf.c#L3223-L3237)).
- Mach-O and PE executables are converted from the ELF state at output time.

**Linking is a few loops over flat arrays.**
- `relocate_syms` adds each section's base address to its symbols. For `-run` it resolves undefined names with `dlsym` ([tccelf.c L1084-L1140](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tccelf.c#L1084-L1140)).
- `relocate_section` walks the `Rela` array and calls the per-architecture `relocate()` ([L1143-L1228](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tccelf.c#L1143-L1228)).
- The per-architecture code lives in `x86_64-link.c` and `arm64-link.c`. The GOT and PLT are built by `build_got_entries` ([L1422](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tccelf.c#L1422)).
- It makes "a single pass… on the object and library list", like GNU ld ([tcc-doc.texi L1199-L1202](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tcc-doc.texi#L1199-L1202)).

**Mach-O** ([tccmacho.c L20-L32](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tccmacho.c#L20-L32)):
- tcc writes 64-bit Mach-O executables with classic symbol tables, `LC_MAIN` and no lazy binding.
- On Apple silicon it signs the output by shelling out to `codesign -f -s -` when built with `CONFIG_CODESIGN` ([L2236-L2246](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tccmacho.c#L2236-L2246)).
- For contrast, Go signs in process with its own ad-hoc signer ([cmd/internal/codesign](https://github.com/golang/go/blob/go1.27.3/src/cmd/internal/codesign/codesign.go#L5-L11)).

**PE.** `tccpe.c` assigns addresses and builds the import and base-relocation tables from the ELF state ([tccpe.c L1267](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tccpe.c#L1267), [L2252](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tccpe.c#L2252)).

**`-run` in memory** (`tcc_relocate` → `tcc_relocate_ex`, [tccrun.c L154-L174](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tccrun.c#L154-L174), [L330-L469](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tccrun.c#L330-L469)):
1. Measure the layout with a null base. Sections are grouped as rx, ro and rw, and each group starts on a new page.
2. Allocate the memory (`rt_mem`, [L116-L152](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tccrun.c#L116-L152)):
   - malloc plus one page;
   - `VirtualAlloc` on Windows;
   - or, under SELinux, a dual RX/RW mapping of one file.
3. Assign addresses, relocate symbols and sections, and copy the bytes.
4. `mprotect` each group, then flush the instruction cache on arm64 ([L471-L507](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tccrun.c#L471-L507)).
5. `tcc_run` calls `main` under `setjmp` ([L215-L274](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tccrun.c#L215-L274)).

**`-run` is one-shot.** Calling `tcc_relocate` a second time exits with "'tcc_relocate()' twice is no longer supported" ([L158-L159](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tccrun.c#L158-L159)). Local symbols and section data are freed after relocation ([L276-L328](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tccrun.c#L276-L328)). There is no incremental relinking.

### 2.6 Parallelism

**None.**
- The parser and code generator use globals: `vtop`, `ind`, `loc`, `nocode_wanted` and the symbol stacks ([tcc.h L1429-L1452](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tcc.h#L1429-L1452)).
- `tcc_compile` holds a process-wide semaphore around them ([libtcc.c L138-L152](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/libtcc.c#L138-L152), [L801-L810](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/libtcc.c#L801-L810)).
- V works around this by running several tcc processes over chunks of generated C ([V fastc.md](https://github.com/vlang/v/blob/ac880aa9bc5a1e7f16ae43ec27d7a89220692ffa/doc/fastc.md)).

### 2.7 Measured speed

| Number | Hardware | Version | Workload | Kind | Link |
|---|---|---|---|---|---|
| 2.27 s, 859,000 lines/s, 29.6 MB/s; GCC 3.2 -O0 takes 20.0 s (98,000 lines/s), so tcc is "about 9 times faster" | 2.4 GHz Pentium 4, real time | tcc 0.9.22 | Links browser: 76,936 unique lines, but 1,950,947 lines (67.2 MB) compiled because headers repeat. "Compilation time includes compilation, assembly and linking." | measured by the author | [bellard.org/tcc](https://bellard.org/tcc/) |
| About 2,800 cycles per line | as above | as above | as above | derived | — |
| Generated code at 0.50 of gcc -O2 speed (geomean; 0.54 average). For comparison: gcc -O0 0.57, chibicc 0.30, cproc/QBE 0.65, c2m 0.91 | Intel i5-13600K | tcc 0.9.27, gcc 12.3.1 | 15 small C benchmarks | measured by Makarov | [MIR README L377-L409](https://github.com/vnmakarov/mir/blob/dc2c763f1c8bc746102ce87933d866335f9e6c06/README.md#L377-L409) |
| 0.7M lines/s | 2.5 GHz Skylake laptop, Windows 98 VM | unstated | synthetic | third party, **UNVERIFIED** | [Per Vognsen tweet](https://twitter.com/pervognsen/status/1265582295575982080) |

**The 859k figure flatters tcc on code generation.** About 96% of the lines are repeated header declarations, which emit no code.

**There is no published tcc measurement on modern hardware, including the M4.**
- At 2004 IPC, tcc needed about 2,800 cycles per line.
- A modern core runs several times more instructions per cycle. So a tcc-style pipeline on an M4 performance core probably exceeds 1M lines/s on one thread.
- This is an estimate, not a measurement.

### 2.8 What tcc gives up

**Optimization.**
- Only expression-level folding, strength reduction, flag caching and `&&`/`||` jump lists.
- The manual says: "No other jump optimization is currently performed because it would require to store the code in a more abstract fashion" ([tcc-doc.texi L1962-L1978](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tcc-doc.texi#L1962-L1978)).
- No inlining. `inline` functions are compiled out of line when referenced ([tccgen.c L8739-L8767](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tccgen.c#L8739-L8767)).

**Registers.**
- Every named local lives in memory, and every live temporary is spilled at each call.
- Its code runs at about half the speed of gcc -O2, slower than gcc -O0.

**Code size.** No branch relaxation, and two-instruction conditional branches on arm64.

**Threads.** One thread, global state.

**Hot reload.** One-shot in-memory relocation and no per-function code units, so there is no hot-reload path.

**Shared libraries.** "The C compiler does not generate position independent code (PIC)" for shared libraries ([tcc-doc.texi L1195-L1197](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tcc-doc.texi#L1195-L1197)).

**Forward use.** C's declare-before-use rule is what makes one pass possible. The parser never needs a declaration that comes later.

---

## 3. Other compilers

### 3.1 Jai (closed source, Jonathan Blow)

**Sources and pipeline.** The compiler is not public, and I could not get transcripts of Blow's videos. Everything below except the video titles is **UNVERIFIED**.
- `#run` compile-time execution was shown in Blow's 2014 demo ([video, 2014-10-31](https://www.youtube.com/watch?v=UTqZNujQOlA)).
- The compiler message loop and compiler-as-library were shown in 2015 ([video](https://www.youtube.com/watch?v=OHZwYYW9koI)).
- A community wiki says Jai has its own bytecode. An interpreter runs that bytecode for `#run`, and a "fast but naive" x64 backend translates it to machine code. LLVM is used for optimized builds ([Jai Community wiki](https://github-wiki-see.page/m/Jai-Community/Jai-Community-Library/wiki/Overview)).
- Blow's own backend demos: ["LLVM Back-End, Speed Overview"](https://www.youtube.com/watch?v=HLk4eiGUic8) and ["Demo: Backend Speedup"](https://www.youtube.com/watch?v=14zlJ98gJKA) (2016).

**Linking.** External: `link.exe` on Windows, a bundled lld elsewhere ([The Way to Jai](https://github.com/Ivo-Balbaert/The_Way_to_Jai/blob/main/book/04A_More_info_about_the_compiler.md)).

| Number | Kind | Link |
|---|---|---|
| Goal: 1M lines/s from scratch | Blow's stated goal; the timestamp comes from a third-party clip | [clip chapter](https://www.youtube.com/watch?v=XRxg7sOHOfE&t=231) |
| 80k-line game in about 1.5 s on a laptop | paraphrase of a Gamelab 2018 talk, **UNVERIFIED** | [talk](https://www.youtube.com/watch?v=uZgbKrDEzAs) |
| 250k lines/s with the x64 backend | community changelog quote, **UNVERIFIED** | [wiki](https://github-wiki-see.page/m/Jai-Community/Jai-Community-Library/wiki/Overview) |
| 6,481 lines: front end 0.054 s, LLVM 0.042 s, link 0.329 s | community book, **UNVERIFIED** | [The Way to Jai](https://github.com/Ivo-Balbaert/The_Way_to_Jai/blob/main/book/04A_More_info_about_the_compiler.md) |

The last row suggests that on small programs the external linker dominates the build.

### 3.2 Zig (self-hosted backends)

**Pipeline and IRs.**
- One compilation unit for the whole program ([Zcu.zig L1-L6](https://github.com/ziglang/zig/blob/e4cbd752c8c05f131051f8c873cff7823177d7d3/src/Zcu.zig#L1-L6)).
- Tokens and AST are struct-of-arrays. A token is a 1-byte tag plus a u32 offset; a node is a tag plus a u32 main token plus 8 bytes of data ([Ast.zig L3014-L3017](https://github.com/ziglang/zig/blob/e4cbd752c8c05f131051f8c873cff7823177d7d3/lib/std/zig/Ast.zig#L3014-L3017)).
- **AstGen produces ZIR.** ZIR is untyped, one per file, and cached on disk ([Zir.zig L1-L13](https://github.com/ziglang/zig/blob/e4cbd752c8c05f131051f8c873cff7823177d7d3/lib/std/zig/Zir.zig#L1-L13), [PerThread.zig L73-L115](https://github.com/ziglang/zig/blob/e4cbd752c8c05f131051f8c873cff7823177d7d3/src/Zcu/PerThread.zig#L73-L115)).
- **Sema produces AIR.** Sema does type checking and comptime evaluation; AIR is per function ([Sema.zig L1-L6](https://github.com/ziglang/zig/blob/e4cbd752c8c05f131051f8c873cff7823177d7d3/src/Sema.zig#L1-L6), [Air.zig L1-L25](https://github.com/ziglang/zig/blob/e4cbd752c8c05f131051f8c873cff7823177d7d3/src/Air.zig#L1-L25)).
- **Then Liveness, then the backend's MIR.** Liveness uses 4 bits per AIR instruction ([Liveness.zig L1-L29](https://github.com/ziglang/zig/blob/e4cbd752c8c05f131051f8c873cff7823177d7d3/src/Air/Liveness.zig#L1-L29)). x86 MIR maps 1:1 to machine instructions and exists so that offsets can be assigned later ([x86_64/Mir.zig L1-L7](https://github.com/ziglang/zig/blob/e4cbd752c8c05f131051f8c873cff7823177d7d3/src/arch/x86_64/Mir.zig#L1-L7)).
- **The InternPool holds all types and values as u32 indices.** Equal index means equal value ([InternPool.zig L4580-L4587](https://github.com/ziglang/zig/blob/e4cbd752c8c05f131051f8c873cff7823177d7d3/src/InternPool.zig#L4580-L4587)). It is built for threads:
  - each thread owns an append-only `Local`, and the thread id sits in the top bits of the index ([L1029-L1060](https://github.com/ziglang/zig/blob/e4cbd752c8c05f131051f8c873cff7823177d7d3/src/InternPool.zig#L1029-L1060), [L4829-L4836](https://github.com/ziglang/zig/blob/e4cbd752c8c05f131051f8c873cff7823177d7d3/src/InternPool.zig#L4829-L4836));
  - the hash maps are sharded, with lock-free reads ([L7684-L7760](https://github.com/ziglang/zig/blob/e4cbd752c8c05f131051f8c873cff7823177d7d3/src/InternPool.zig#L7684-L7760)).

**Instruction selection and register allocation.**
- The x86_64 backend makes one forward walk over AIR. It uses comptime pattern tables for instruction selection and a `RegisterManager` that allocates on the fly, with Liveness deaths freeing registers. There is no global allocator ([register_manager.zig L19-L42](https://github.com/ziglang/zig/blob/e4cbd752c8c05f131051f8c873cff7823177d7d3/src/register_manager.zig#L19-L42)).
- The x86 backend is about 206,873 lines.
- The new aarch64 backend generates MIR in reverse after its own liveness pass. Its MIR is the real instruction encoding. It is about 25,945 lines ([devlog 2025-07-23](https://ziglang.org/devlog/2025/#2025-07-23), [codegen/aarch64.zig L12-L60](https://github.com/ziglang/zig/blob/e4cbd752c8c05f131051f8c873cff7823177d7d3/src/codegen/aarch64.zig#L12-L60)).

**Linking and incremental builds.**
- Zig has its own ELF, Mach-O, COFF and Wasm linkers ([src/link](https://github.com/ziglang/zig/tree/e4cbd752c8c05f131051f8c873cff7823177d7d3/src/link)).
- The old ELF linker gives each function's atom spare capacity (size/3) so it can grow in place ([Elf.zig L180-L189](https://github.com/ziglang/zig/blob/e4cbd752c8c05f131051f8c873cff7823177d7d3/src/link/Elf.zig#L180-L189)).
- An experimental hot code swap writes updates into a running process through ptrace ([link.zig L570-L612](https://github.com/ziglang/zig/blob/e4cbd752c8c05f131051f8c873cff7823177d7d3/src/link.zig#L570-L612)).
- The new ELF linker memory-maps the output as a tree of movable nodes ([MappedFile.zig @0.17.1](https://codeberg.org/ziglang/zig/src/tag/0.17.1/src/link/MappedFile.zig#L235-L255)).
- Incremental compilation is in-process only, because compiler state is not serialized. In 0.17 "most projects targeting x86_64-linux" can use it ([0.14.0 notes](https://ziglang.org/download/0.14.0/release-notes.html), [0.17.0 notes](https://ziglang.org/download/0.17.0/release-notes.html)).

**Parallelism.**
- AstGen runs one job per file ([Compilation.zig L4871-L4876](https://github.com/ziglang/zig/blob/e4cbd752c8c05f131051f8c873cff7823177d7d3/src/Compilation.zig#L4871-L4876)).
- Sema is single-threaded. Codegen runs per function on worker threads ([L5052-L5100](https://github.com/ziglang/zig/blob/e4cbd752c8c05f131051f8c873cff7823177d7d3/src/Compilation.zig#L5052-L5100)).
- One link thread consumes results in a fixed order, for reproducibility ([link/Queue.zig L1-L13](https://github.com/ziglang/zig/blob/e4cbd752c8c05f131051f8c873cff7823177d7d3/src/link/Queue.zig#L1-L13)).
- AIR in flight is capped at 10 MiB, because MIR runs "around 20" to 50 times the size of AIR ([Queue.zig L70-L73](https://github.com/ziglang/zig/blob/e4cbd752c8c05f131051f8c873cff7823177d7d3/src/link/Queue.zig#L70-L73)).

**Measured speed.** The source does not state the hardware for any of these.

| Number | Version | Workload | Link |
|---|---|---|---|
| 918 ms with LLVM vs 275 ms self-hosted (−70%); 4.53G vs 1.57G cycles | master, June 2025 | `zig build-exe hello.zig` | [devlog 2025-06-08](https://ziglang.org/devlog/2025/#2025-06-08) |
| 75 s vs 20 s | June 2025 | building the Zig compiler | same |
| 13.8 s → 10.0 s from parallel codegen; RSS +25.7% | June 2025 | compiler self-build | [devlog 2025-06-14](https://ziglang.org/devlog/2025/#2025-06-14) |
| aarch64 32.1 ms vs x86_64 42.7 ms; 50.9M vs 167M instructions | July 2025 | hello, `-fno-llvm -OReleaseSmall` | [devlog 2025-07-23](https://ziglang.org/devlog/2025/#2025-07-23) |
| 14 s for a "half-million line codebase", front end only (derived: about 36k lines/s); 63 ms incremental reanalysis | 0.14.0 | `-fno-emit-bin --watch` | [0.14.0 notes](https://ziglang.org/download/0.14.0/release-notes.html) |
| 194 ms → 65 ms incremental rebuild with the new linker | 0.16.0 | one-line change to the compiler | [0.16.0 notes](https://ziglang.org/download/0.16.0/release-notes.html) |

**Trade-offs.**
- The x86 backend emits slower code than LLVM; no number is published ([0.15.1 notes](https://ziglang.org/download/0.15.1/release-notes.html#x86-Backend)).
- The self-hosted backend is the default only for x86_64 Debug, and not on Windows.
- **aarch64 was "not usable" in 0.15.1 and is still not usable in 0.17, so on an M4 Zig Debug builds use LLVM** ([0.15.1](https://ziglang.org/download/0.15.1/release-notes.html#aarch64-Backend), [0.17.0](https://ziglang.org/download/0.17.0/release-notes.html)).

### 3.3 Go (gc)

**Pipeline and IRs.**
- The stages are ([cmd/compile README](https://go.dev/src/cmd/compile/README)):
  1. parse;
  2. types2;
  3. Unified IR;
  4. inlining, devirtualization and escape analysis;
  5. walk;
  6. SSA with about 60 passes;
  7. `obj.Prog`;
  8. the assembler;
  9. an object file plus export data.
- The driver order is in [gc/main.go L218-L381](https://github.com/golang/go/blob/2aae1000ea1d4b28923b96db28acf4e3b99d6aff/src/cmd/compile/internal/gc/main.go#L218-L381).
- Records are heap pointer graphs: `ssa.Value` is 112 B, `ssa.Block` 312 B and `obj.Prog` 200 B ([ssa/sizeof_test.go L23-L24](https://github.com/golang/go/blob/2aae1000ea1d4b28923b96db28acf4e3b99d6aff/src/cmd/compile/internal/ssa/sizeof_test.go#L23-L24), [obj/sizeof_test.go L25](https://github.com/golang/go/blob/2aae1000ea1d4b28923b96db28acf4e3b99d6aff/src/cmd/internal/obj/sizeof_test.go#L25)).

**Instruction selection.** Rewrite rules are written in `_gen/*.rules` and generated into Go matchers. They are applied until nothing changes, as the lower pass ([rulegen.go L5-L59](https://github.com/golang/go/blob/2aae1000ea1d4b28923b96db28acf4e3b99d6aff/src/cmd/compile/internal/ssa/_gen/rulegen.go#L5-L59), [rewrite.go L41-L59](https://github.com/golang/go/blob/2aae1000ea1d4b28923b96db28acf4e3b99d6aff/src/cmd/compile/internal/ssa/rewrite.go#L41-L59)).

**Register allocation** ([regalloc.go L5-L25](https://github.com/golang/go/blob/2aae1000ea1d4b28923b96db28acf4e3b99d6aff/src/cmd/compile/internal/ssa/regalloc.go#L5-L25)):
- A linear-scan variant that treats the function as one long block.
- When registers run out, it spills the value whose next use is farthest away.
- Merge edges are fixed up afterwards.

**Export data and caching.**
- Each package's export data is indexed and read lazily, and carries inline bodies and escape summaries ([noder/doc.go](https://github.com/golang/go/blob/2aae1000ea1d4b28923b96db28acf4e3b99d6aff/src/cmd/compile/internal/noder/doc.go#L1-L40), [unified.go L151-L191](https://github.com/golang/go/blob/2aae1000ea1d4b28923b96db28acf4e3b99d6aff/src/cmd/compile/internal/noder/unified.go#L151-L191)).
- The go command's cache is keyed by content hashes ([buildid.go L26-L60](https://github.com/golang/go/blob/2aae1000ea1d4b28923b96db28acf4e3b99d6aff/src/cmd/go/internal/work/buildid.go#L26-L60)).

**Linker.**
- Go has its own linker. Symbol references are dense `{pkg, sym}` u32 pairs, and output segments are written in parallel ([goobj/objfile.go L29-L112](https://github.com/golang/go/blob/2aae1000ea1d4b28923b96db28acf4e3b99d6aff/src/cmd/internal/goobj/objfile.go#L29-L112), [ld/asmb.go L16-L70](https://github.com/golang/go/blob/2aae1000ea1d4b28923b96db28acf4e3b99d6aff/src/cmd/link/internal/ld/asmb.go#L16-L70)).
- It came from Austin Clements' redesign ([doc](https://docs.google.com/document/d/1D13QhciikbdLtaI67U6Ble5d_1nsI4befEd6_k1z91U/mobilebasic)).
- No incremental linking. No JIT.

**Parallelism** ([gc/compile.go L98-L200](https://github.com/golang/go/blob/2aae1000ea1d4b28923b96db28acf4e3b99d6aff/src/cmd/compile/internal/gc/compile.go#L98-L200)):
- Inside one package: parsing runs in parallel, types2 through walk run serially, and SSA-to-machine-code runs per function in parallel, longest functions first.
- Across packages: separate compiler processes share a token pool ([work/gc.go L182-L254](https://github.com/golang/go/blob/2aae1000ea1d4b28923b96db28acf4e3b99d6aff/src/cmd/go/internal/work/gc.go#L182-L254)).

| Number | Hardware | Version | Workload | Link |
|---|---|---|---|---|
| `-c=4`: compiling package `cmd/compile` 2.47 s → 1.41 s; no gain beyond 4 workers | 8-core 2.9 GHz Core i7 | tip, March 2017 | compilebench | [commit 756b9ce](https://github.com/golang/go/commit/756b9ce3a5a518555114b0e023eb1674084b38e1) |
| Link of cmd/compile about 1 s; 23.5% of cycles in GC marking, 23% in zlib DWARF compression | 2-core 2.6 GHz Skylake | go1.13beta1 | link cmd/compile | [Clements doc](https://docs.google.com/document/d/1D13QhciikbdLtaI67U6Ble5d_1nsI4befEd6_k1z91U/mobilebasic) |
| Linker 20% faster with 30% less memory (1.15), then 20–25% faster (1.16) | not stated | 1.15, 1.16 | large programs | [1.15](https://go.dev/doc/go1.15), [1.16](https://go.dev/doc/go1.16) |
| Builds about 2× slower after the C→Go translation | not stated | 1.5 | — | [1.5](https://go.dev/doc/go1.5) |

No primary source gives a lines/s figure for Go.

**Trade-offs.**
- Generics use GC-shape stenciling with dictionaries. This gives fewer instances, but less inlining and slower calls ([design](https://github.com/golang/proposal/blob/master/design/generics-implementation-gcshape.md)).
- Records are large and pointer-linked.
- Every build relinks everything.

### 3.4 Carbon toolchain

Carbon is the reference for a data-oriented front end. It has no backend of its own: it lowers to LLVM.

**Stages.**
- Source → `TokenizedBuffer` → postorder `Parse::Tree` → `SemIR` → LLVM IR → object.
- Each stage builds a new flat structure addressed by 32-bit ids. The parser uses an explicit state stack instead of recursion ([toolchain/docs/README.md](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/docs/README.md)).

**Record sizes.**

| Record | Size | Layout | Source |
|---|---|---|---|
| Token | 8 B (`static_assert`) | 1-byte kind, 1-bit leading-space flag, 23-bit payload, 32-bit byte offset. Line and column are not stored. | [token_info.h L186-L210](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/lex/token_info.h#L186-L210) |
| Parse node | 8 B | kind, error bit, token index; one node per token, reserved up front | [parse/tree.h L199-L243](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/parse/tree.h#L199-L243) |
| SemIR instruction | 16 B | kind, `TypeId`, two 32-bit arguments | [sem_ir/inst.h L330-L357](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/sem_ir/inst.h#L330-L357) |

- Packing tokens into 8 bytes made lexing 5–12% faster, and limits a file to 2^23 tokens ([PR #4270](https://github.com/carbon-language/carbon-lang/pull/4270)).

**Lexer.**
- Dispatch is a 256-entry table keyed on the first byte, using `musttail` and `preserve_none` ([lex.cpp L563-L600](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/lex/lex.cpp#L563-L600)).
- **Identifier scanning uses SIMD on x86 only.** On Arm it is scalar on purpose: a NEON-to-general-register move costs "as much as 10 cycles" on Apple M1 ([lex.cpp L538-L560](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/lex/lex.cpp#L538-L560)).

**Check.**
- One postorder pass. It treats the parse nodes "as a byte code input that the check step interprets". The language's "information accumulation" rule (declare before use) makes one pass enough ([check/README.md L57-L80](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/docs/check/README.md#L57-L80)).
- Types and constants are canonicalized to ids in `CanonicalValueStore` ([canonical_value_store.h L17-L40](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/base/canonical_value_store.h#L17-L40)).
- Generic specifics are deduplicated by fingerprinting their LLVM IR ([lower.md](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/docs/lower.md#L22-L136)).

**Parallelism.** None inside the process ([compile_driver.cpp L581-L665](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/driver/compile_driver.cpp#L581-L665)).

**Speed targets (claims).** From the CppNow 2023 talk ([slides](https://chandlerc.blog/slides/2023-cppnow-compiler/), [video](https://www.youtube.com/watch?v=ZI198eFghJk)):

| Stage | Target | Per line |
|---|---|---|
| Lex + parse | 10M lines/s | about 100 ns, "200-300 cycles, maybe 500 instructions", at most one main-memory access per line on average |
| Semantic check | 1M lines/s | 1 µs |
| Lowering to a binary | 0.1M lines/s, marked "Maybe…" | — |

**Measured speed** ([PR #4408](https://github.com/carbon-language/carbon-lang/pull/4408), merged 2024-10-22). The workload is a generated "API file, dense decls". The hardware is not stated in the PR; the earlier [PR #4124](https://github.com/carbon-language/carbon-lang/pull/4124) says "recent AMD server CPU".

| Phase (cumulative) | 16,384 lines | Range over 256–262,144 lines | Derived cost of this phase alone |
|---|---|---|---|
| Lex | 6.17M lines/s, 231 MB/s, 36.6M tokens/s | 4.3–6.6M lines/s | about 162 ns per line; about 5.9 tokens and 37 bytes per line |
| + Parse | 3.74M lines/s | 2.9–3.9M lines/s | about 105 ns per line |
| + Check | 985k lines/s | 289k–985k lines/s | about 750 ns per line |

**Comparison with chibi-v2.** chibi-v2's lexer runs at 1,356 MB/s against Carbon's about 231 MB/s. Carbon's figure also includes building the token buffer, interning and bracket matching.

### 3.5 Turbo Pascal and Delphi

**Turbo Pascal.**
- Hejlsberg: "a one-pass compiler could do it all". Adding hash tables in TP 2.0 made "the compiler [go] twice as fast" ([interview transcript](https://www.aarthiandsriram.com/p/our-dream-conversation-anders-hejlsberg)).
- TP 3.0 compiled to memory by default, or to a `.COM` file. On a run-time error, the editor found the failing source location ([TP 3.0 manual](https://archive.org/stream/bitsavers_borlandturVersion3.0ReferenceManual1985_10962042/Turbo_Pascal_Version_3.0_Reference_Manual_1985_djvu.txt)).
- **TP 4.0 added units.** A `.TPU` holds "the symbol information and compiled code for the unit". A unit recompiles when its own source changed or when the interface of a unit it uses changed ([TP 4.0 manual](https://archive.org/stream/bitsavers_borlandturVersion4.0OwnersManual1987_23144965/Turbo_Pascal_Version_4.0_Owners_Manual_1987_djvu.txt)).

**Delphi.**
- An implementation-only change does not recompile dependents.
- Units may not form a cycle through their interfaces ([docwiki](https://docwiki.embarcadero.com/RADStudio/Florence/en/Programs_and_Units_(Delphi)); text taken from search snippets because the site returned 403).

**Free Pascal** decides what to recompile from three CRCs per `.ppu`: the whole unit, the interface, and indirect dependencies ([ppu.pas L150-L160](https://github.com/fpc/FPCSource/blob/36123ae4fff1be4eec662f90fa2a1b35d7d79b21/compiler/ppu.pas#L150-L160), [fppu.pas L2239-L2258](https://github.com/fpc/FPCSource/blob/36123ae4fff1be4eec662f90fa2a1b35d7d79b21/compiler/fppu.pas#L2239-L2258)).

| Number | Kind | Link |
|---|---|---|
| TP 4.0 is "two to three times faster" than TP 3.0, on an 8 MHz IBM AT | vendor claim | [TP 4.0 manual](https://archive.org/stream/bitsavers_borlandturVersion4.0OwnersManual1987_23144965/Turbo_Pascal_Version_4.0_Owners_Manual_1987_djvu.txt) |
| Delphi 10.1: about 1M lines in about 4–5 s | vendor video; number **UNVERIFIED** | [video](https://www.youtube.com/watch?v=Yq2mNUzcpE0) |
| Delphi 2: 1.2M lines/s; tcc 0.7M; FPC 55k; modern Delphi 15k (Windows 98 VM) | third party, **UNVERIFIED** | [tweet](https://twitter.com/pervognsen/status/1265582295575982080) |

### 3.6 QBE with cproc and Hare

**QBE.**
- Goal: "70% of the performance of industrial optimizing compilers in 10% of the code", with one IL used at every stage ([c9x.me/compile](https://c9x.me/compile/)).
- It processes one function at a time and frees its memory afterwards ([main.c L53-L122](https://c9x.me/git/qbe.git/tree/main.c?id=180d2eb4157b706dc480bce50ff6d03a65ec334b#n53)). The pass order is:
  1. ABI;
  2. SSA construction;
  3. loadopt, GVN, GCM;
  4. isel;
  5. spill;
  6. a linear allocator with hints;
  7. emission of **assembly text**.
- Spilling is split from register assignment thanks to SSA form ([spill.c](https://c9x.me/git/qbe.git/tree/spill.c?id=180d2eb4157b706dc480bce50ff6d03a65ec334b#n313), [rega.c](https://c9x.me/git/qbe.git/tree/rega.c?id=180d2eb4157b706dc480bce50ff6d03a65ec334b#n477)).
- Targets: amd64 (SysV, Apple, Windows), arm64 (including Apple) and rv64 ([main.c L22-L37](https://c9x.me/git/qbe.git/tree/main.c?id=180d2eb4157b706dc480bce50ff6d03a65ec334b#n22)).
- No threads.

**cproc** emits each function's IL right after parsing its body. Its driver runs cpp, cproc, qbe, as and ld as separate processes ([decl.c L1144-L1150](https://github.com/michaelforney/cproc/blob/d1c53ddf56571573a7025324c8dd5c6d547a4d1f/decl.c#L1144-L1150), [driver.c](https://github.com/michaelforney/cproc/blob/d1c53ddf56571573a7025324c8dd5c6d547a4d1f/driver.c#L150-L200)).

**Hare** ([design.txt](https://git.sr.ht/~sircmpwn/hare/tree/27d9d9257d6975ac4f00b73d4e12ee2bad9e4351/item/cmd/hare/design.txt)):
- Runs harec, qbe, as and ld for each module.
- Caches by hash. The typedef file is hashed by content alone, so dependents rebuild only when a module's API changes.
- Schedules the harec jobs first, which "roughly halved most compilation times at -j4".

**Numbers.**
- No lines/s figure is published for QBE, cproc or Hare.
- Code quality: cproc's code runs at 0.65 of gcc -O2 ([MIR README L401](https://github.com/vnmakarov/mir/blob/dc2c763f1c8bc746102ce87933d866335f9e6c06/README.md#L401)).
- Makarov says QBE is about 30× slower than MIR at machine-code generation, because it emits assembly ([README L417](https://github.com/vnmakarov/mir/blob/dc2c763f1c8bc746102ce87933d866335f9e6c06/README.md#L417)).

### 3.7 MIR (Vladimir Makarov)

**The IR.** Typed and register-based. Instructions are heap nodes in doubly-linked lists ([mir.h L254-L288](https://github.com/vnmakarov/mir/blob/dc2c763f1c8bc746102ce87933d866335f9e6c06/mir.h#L254-L288)).

**Generator passes are gated by optimization level** ([generate_func_code L9341-L9569](https://github.com/vnmakarov/mir/blob/dc2c763f1c8bc746102ce87933d866335f9e6c06/mir-gen.c#L9341-L9569)):
1. At level 2: SSA, GVN, DSE, DCE, LICM.
2. Machinize.
3. Register allocation: a priority-based linear scan with live-range splitting ([L7318](https://github.com/vnmakarov/mir/blob/dc2c763f1c8bc746102ce87933d866335f9e6c06/mir-gen.c#L7318)).
4. `combine`, which does code selection after register allocation ([L9089](https://github.com/vnmakarov/mir/blob/dc2c763f1c8bc746102ce87933d866335f9e6c06/mir-gen.c#L9089)).
5. Encoding from a string pattern table ([mir-gen-x86_64.c L1336](https://github.com/vnmakarov/mir/blob/dc2c763f1c8bc746102ce87933d866335f9e6c06/mir-gen-x86_64.c#L1336)).

**JIT only.**
- Output goes straight to executable memory; there are no object files.
- `MIR_link` resolves names and picks one of four modes: interpret, generate eagerly, generate lazily per function, or generate lazily per basic block ([MIR.md L712-L734](https://github.com/vnmakarov/mir/blob/dc2c763f1c8bc746102ce87933d866335f9e6c06/MIR.md#L712-L734)).
- **Calls go through thunks.** A function's identity is its thunk, so its code can be replaced by retargeting the thunk ([2022 post](https://developers.redhat.com/articles/2022/02/16/code-specialization-mir-lightweight-jit-compiler)). This is the closest design here to Edit and Continue.

**Parallelism.** Parallel generation was removed in 2023 ([commit 97a9a2b](https://github.com/vnmakarov/mir/commit/97a9a2bfd786f418260721acf1848bd07dc9c17a)).

| Number | Hardware | Workload | Link |
|---|---|---|---|
| MIR-gen at level 2: 249 µs vs gcc -O2 27.1 ms (109×); c2m: 336 µs; code at 0.92 of gcc -O2 speed | i5-13600K, GCC 12.3.1 | sieve, a tiny input (latency, not throughput) | [README L335-L375](https://github.com/vnmakarov/mir/blob/dc2c763f1c8bc746102ce87933d866335f9e6c06/README.md#L335-L375) |
| c2m code at 0.91 of gcc -O2 speed (geomean) | same | 15 C benchmarks | [README L377-L409](https://github.com/vnmakarov/mir/blob/dc2c763f1c8bc746102ce87933d866335f9e6c06/README.md#L377-L409) |
| Interpreter 6–10× slower than generated code | — | — | [README L232](https://github.com/vnmakarov/mir/blob/dc2c763f1c8bc746102ce87933d866335f9e6c06/README.md#L232) |

### 3.8 TPDE (Schwarz, Kamm, Engelke, TUM)

**No IR of its own.** The user writes an IR adaptor as a C++ template parameter, so its calls inline; TPDE never translates into its own IR ([paper](https://arxiv.org/html/2505.22610), [overview.md](https://github.com/tpde2/tpde/blob/9779acf4ada3736e779391da1e4b3369dba08024/docs/tpde/overview.md)).

**Two passes per function.**
1. **Analyzer** ([Analyzer.hpp](https://github.com/tpde2/tpde/blob/9779acf4ada3736e779391da1e4b3369dba08024/tpde/include/tpde/Analyzer.hpp#L63-L77)):
   - loop detection;
   - a block layout that keeps each loop's blocks together;
   - liveness, stored as a first block, a last block and a reference count per value.
2. **One linear pass** that does instruction selection, register allocation and encoding together ([CompilerBase.hpp L2269-L2462](https://github.com/tpde2/tpde/blob/9779acf4ada3736e779391da1e4b3369dba08024/tpde/include/tpde/CompilerBase.hpp#L2269)).

**Register allocation** ([L1383](https://github.com/tpde2/tpde/blob/9779acf4ada3736e779391da1e4b3369dba08024/tpde/include/tpde/CompilerBase.hpp#L1383), [L1537](https://github.com/tpde2/tpde/blob/9779acf4ada3736e779391da1e4b3369dba08024/tpde/include/tpde/CompilerBase.hpp#L1537)):
- Greedy: take the lowest free register, evict round-robin.
- Values in the innermost loop may get a fixed register.
- Before a branch to a block with several predecessors, it "spill[s] all values that have no fixed register and are live at the entry of any of these successors".

**Encoders are generated, not hand-written.** EncodeGen compiles C snippets with Clang and turns the resulting machine IR into encoder functions ([encodegen.md](https://github.com/tpde2/tpde/blob/9779acf4ada3736e779391da1e4b3369dba08024/docs/tpde/encodegen.md)).

**Output.** Its own ELF writer and an in-memory mapper. **x86-64 and AArch64 ELF only: no Mach-O, no COFF** ([README](https://github.com/tpde2/tpde/blob/9779acf4ada3736e779391da1e4b3369dba08024/README.md)).

| Number | Hardware | Workload | Link |
|---|---|---|---|
| 8–24× faster than LLVM -O0; geomean 12.15× (x86-64) and 18.96× (AArch64); run time "on-par" | Xeon Gold 6430 / Apple M1 under Asahi Linux | SPECint 2017, LLVM 19 IR | [arXiv 2505.22610](https://arxiv.org/abs/2505.22610) |
| On -O1 IR: 85.8× / 80.0× faster than LLVM -O1, with code 1.54× / 1.77× slower | same | same | [paper](https://arxiv.org/html/2505.22610) |
| Share of TPDE's time: codegen 49%, LLVM preparation pass 14%, analysis 12%. Corrected 2026-11-12: this note first gave the 14% to the IR adaptor | same | same | [paper](https://arxiv.org/html/2505.22610), section 5 |
| `tpde-llc`: 77–95% of the time is LLVM bitcode parsing | x86-64 | SPEC | [LLVM Discourse](https://discourse.llvm.org/t/tpde-llvm-10-20x-faster-llvm-o0-back-end/86664) |
| `.text` +43% (x86-64) / +49% (AArch64) | paper | SPEC | [paper](https://arxiv.org/html/2505.22610) |

**Lesson.** Once the backend is this fast, the cost of reading its input dominates. A compiler that feeds the backend from its own in-memory IR avoids that cost.

### 3.9 Cranelift

**Pipeline** ([context.rs L131-L199](https://github.com/bytecodealliance/wasmtime/blob/3e0ee3fbfdd88d06c3f8c169168fe0d3b0c0096d/cranelift/codegen/src/context.rs#L131-L199), [machinst/compile.rs L16-L96](https://github.com/bytecodealliance/wasmtime/blob/3e0ee3fbfdd88d06c3f8c169168fe0d3b0c0096d/cranelift/codegen/src/machinst/compile.rs#L16-L96)):
1. CLIF, with 16-byte instructions.
2. An optional aegraph mid-end.
3. ISLE lowering to VCode, with 48-byte machine instructions.
4. regalloc2.
5. `MachBuffer`.

**Lowering.** It walks blocks backward and lowers pure instructions only when they are used. This merges and sinks code without a separate pass ([lower.rs L830-L860](https://github.com/bytecodealliance/wasmtime/blob/3e0ee3fbfdd88d06c3f8c169168fe0d3b0c0096d/cranelift/codegen/src/machinst/lower.rs#L830-L860)).

**Branches.** `MachBuffer` simplifies branches and resolves labels while streaming the code out, with no extra pass ([Fallin 2021](https://cfallin.org/blog/2021/01/22/cranelift-isel-2/)).

**Optimizer budget.** The aegraph's effort is capped by constants ([egraph/mod.rs L128-L137](https://github.com/bytecodealliance/wasmtime/blob/3e0ee3fbfdd88d06c3f8c169168fe0d3b0c0096d/cranelift/codegen/src/egraph/mod.rs#L128-L137)).

**Register allocation.**
- The default is regalloc2 "Ion", a backtracking allocator over live-range bundles ([Fallin 2022](https://cfallin.org/blog/2022/06/09/cranelift-regalloc2/)).
- `single_pass` selects Fastalloc, a reverse linear scan ([FASTALLOC.md](https://github.com/bytecodealliance/regalloc2/blob/4a233442f781dd7b84d5ea40504d3be07263b3e3/doc/FASTALLOC.md)).

**Output and parallelism.** No linker of its own: objects are written through the `object` crate, and there is a JIT module. Functions compile independently and in parallel.

| Number | Kind | Link |
|---|---|---|
| Compile time is "dominated by regalloc time" | author claim | [Fallin 2022](https://cfallin.org/blog/2022/06/09/cranelift-regalloc2/) |
| Fastalloc: register allocation about 6× faster than Ion; whole compile 1.07–5× faster; generated code 1.06–7.5× slower | measured by Fastalloc's author; hardware not stated | [d-sonuga](https://d-sonuga.netlify.app/gsoc/regalloc-iii/) |
| aegraph mid-end: +7–8% compile time for about 2% faster code | author | [Fallin 2026](https://cfallin.org/blog/2026/04/09/aegraph/) |
| rustc's Cranelift backend vs its LLVM backend: "most benchmarks 10-50% faster" | author; hardware not stated | [bjorn3 2024-11](https://bjorn3.github.io/2024/11/14/progress-report-nov-2024.html) |
| TPDE on CLIF compiles 4.27× faster than Cranelift and 2.68× faster than Cranelift with Fastalloc | TPDE paper | [paper](https://arxiv.org/html/2505.22610) |

### 3.10 V

**Pipeline and backends.**
- Front end: parser, checker, transformer, then dead-code marking ([vlib/v/README.md](https://github.com/vlang/v/blob/ac880aa9bc5a1e7f16ae43ec27d7a89220692ffa/vlib/v/README.md#L8-L15)).
- Backends: C; "FastC", which goes from scanner tokens straight to C; native ARM64 with its own Mach-O writer ([macho.v](https://github.com/vlang/v/blob/ac880aa9bc5a1e7f16ae43ec27d7a89220692ffa/vlib/v/gen/arm64/macho.v)); and WebAssembly.
- Non-`-prod` builds compile the C output with tcc ([README L269-L285](https://github.com/vlang/v/blob/ac880aa9bc5a1e7f16ae43ec27d7a89220692ffa/README.md#L269-L285)).

**Hot reload.** `@[live]` "currently has no effect with the default compiler" ([docs.md L11924-L11930](https://github.com/vlang/v/blob/ac880aa9bc5a1e7f16ae43ec27d7a89220692ffa/doc/docs.md#L11924-L11930)).

| Number | Kind | Link |
|---|---|---|
| About 700k LOC/s with arm64 or tcc; about 100k with Clang (Apple M5) | claim, **UNVERIFIED** | [vlang.io](https://vlang.io/) |
| V→C only: 108k–201k lines/s (290k lines in 1.44–2.68 s), excluding C compile and link; base M5 MacBook Pro | measured by the project, public script | [fast.vlang.io](https://fast.vlang.io/), [bench.v](https://github.com/vlang/v/blob/ac880aa9bc5a1e7f16ae43ec27d7a89220692ffa/cmd/tools/fast/bench.v#L152-L194) |

### 3.11 Odin

**Pipeline.** Files are parsed on worker threads, and procedure bodies are checked on workers ([parser.cpp L6500-L6515](https://github.com/odin-lang/Odin/blob/85afcb386c389aafcec4f2e33d27ff8b42579822/src/parser.cpp#L6500-L6515), [checker.cpp L7287](https://github.com/odin-lang/Odin/blob/85afcb386c389aafcec4f2e33d27ff8b42579822/src/checker.cpp#L7287)).

**Backend.** LLVM, split into one module per package. Modules are generated and emitted in parallel ([llvm_backend.cpp L2733-L2770](https://github.com/odin-lang/Odin/blob/85afcb386c389aafcec4f2e33d27ff8b42579822/src/llvm_backend.cpp#L2733-L2770)).

**Non-LLVM backend.**
- The Tilde backend was removed in March 2026 ([PR #6483](https://github.com/odin-lang/Odin/pull/6483)).
- The plan now is "to just write out own backend instead" ([forum](https://forum.odin-lang.org/t/tilde-backend-got-removed/1697)).

**Claims and limits.**
- "Around ~90% of the compilation time is spent on LLVM" ([newsletter 2023-07](https://odin-lang.org/news/newsletter-2023-07/)).
- Precompiling packages is blocked by generics and by RTTI, which "requires everything to be in a specific order" ([comment](https://github.com/odin-lang/Odin/issues/1870#issuecomment-1171014422)).

### 3.12 C3 and Tilde (TB)

**C3.**
- **C3 does not ship a TB backend.** The TB code generator was deleted in 2024, "because it's sure to have code-rotted by now" ([commit 65c2126](https://github.com/c3lang/c3c/commit/65c212620255fa740386813aefb3dc4b3f479778)). Only the `--backend=tb` string remains ([build_internal.h L158-L161](https://github.com/c3lang/c3c/blob/b2e1e99baaf2add3a40bd084823416a4437f3ec6/src/build/build_internal.h#L158-L161)).
- The default backend is LLVM, with an experimental C backend.
- Lernö: "LLVM codegen and linking takes over 98% of the total compilation time" ([blog](https://c3.handmade.network/blog/p/8852-how_bad_is_llvm_really)).

**Tilde (Cuik).**
- Sea-of-nodes IR, per-function optimization and codegen ([tb.h L1-L3](https://github.com/RealNeGate/Cuik/blob/a7677f74e5391dcc7fdedad61468f4f207c9998f/include/tb.h#L1-L3)).
- Object writers for COFF, ELF and Mach-O, its own linker, and a JIT ([tb/objects](https://github.com/RealNeGate/Cuik/tree/a7677f74e5391dcc7fdedad61468f4f207c9998f/tb/objects)).
- The README says it is "unfinished and buggy".
- No lines/s figure.

### 3.13 chibicc (readable reference only)

- Stages: tokenize, preprocess, recursive-descent parse to an AST, then a codegen that emits assembly text as a stack machine (`push %rax`/`pop`) ([codegen.c L31-L39](https://github.com/rui314/chibicc/blob/90d1f7f199cc55b13c7fdb5839d1409806633fdb/codegen.c#L31-L39)).
- Its code runs at 0.30 of gcc -O2 ([MIR README L398](https://github.com/vnmakarov/mir/blob/dc2c763f1c8bc746102ce87933d866335f9e6c06/README.md#L398)).
- Read it to compare an AST-then-emit design with tcc's no-AST design. Do not read it for speed.

### 3.14 Linking: mold

**Design** ([design.md](https://github.com/rui314/mold/blob/v2.40.0/docs/design.md#L36-L80)):
- Fix the output layout early, then copy at `cp` speed.
- Use data-parallel loops and a concurrent symbol map ([L309-L360](https://github.com/rui314/mold/blob/v2.40.0/docs/design.md#L309-L360)).
- **No incremental linking.** A null incremental link of Chrome with gold took "almost 30 seconds", so the aim is "to make full link as fast as possible" ([L399-L459](https://github.com/rui314/mold/blob/v2.40.0/docs/design.md#L399-L459)).

**Current release.** mold 3.0.0 (2026-10-05) is a Rust rewrite whose performance is "on par with 2.42.1" ([release](https://github.com/rui314/mold/releases/tag/v3.0.0)).

**Measured** ([README L32-L115](https://github.com/rui314/mold/blob/331ac663668dc0b1f4949403854613b9b1913025/README.md#L32-L115)):
- Chromium 145 debug build, 4.51 GiB output: mold 1.65 s vs lld 16.64 s on a 64-core Threadripper 7980X.
- The same link: 2.22 s vs 9.54 s on an M1 Ultra under Asahi Linux.

**macOS.** mold's README covers ELF only. Mach-O support is undocumented (**UNVERIFIED**).

---

## 4. Comparison table

| Compiler | Stages and IRs | Instruction selection | Register allocation | Objects and linking | Parallelism | Best published speed | Gives up |
|---|---|---|---|---|---|---|---|
| tcc | tokens → machine code; value stack only | operand location chosen on the value stack | value stack; spill the oldest entry; all named locals in memory | ELF internally; own ELF, Mach-O and PE linker; in-memory `-run` | none | 859k lines/s full build (P4, 2004) | optimization (0.50 of gcc -O2), threads, reload |
| Jai | AST → bytecode → x64, or LLVM | ? | ? | external linker | job system (UNVERIFIED) | 250k lines/s (UNVERIFIED) | — |
| Zig | AST → ZIR → AIR → MIR | comptime pattern tables | on the fly, driven by Liveness | own linkers; incremental; hot swap | parallel AstGen and codegen; serial Sema | hello 275 ms; no lines/s | code quality; no aarch64 yet |
| Go | AST → types2 → IR → SSA → Prog | rewrite rules | linear scan | own linker; package cache | per-function backend, per-package processes | no lines/s | 112–312 B records; full relink |
| Carbon | tokens → postorder tree → SemIR → LLVM | LLVM | LLVM | LLVM, lld | none in process | 985k lines/s through check | backend is LLVM |
| Turbo Pascal / Delphi | single pass; units | ? | ? | own linker; in-memory run | none | 1.2M lines/s (UNVERIFIED) | language must be declare-before-use |
| QBE (cproc, Hare) | one SSA IL → assembly text | isel pass | spill pass, then linear | external as and ld | processes only | none | 0.65 of gcc -O2; process overhead |
| MIR | MIR → passes → code | `combine` + pattern table | priority linear scan | JIT only; thunks | none | 109× faster than gcc -O2 on a tiny input | no objects |
| TPDE | your SSA → analysis → one pass | per-instruction compilers + generated encoders | greedy; spill at joins | ELF and in-memory | not discussed | 12–19× faster than LLVM -O0 | ELF only; 1.5–1.8× slower code than -O1 |
| Cranelift | CLIF → aegraph → VCode | ISLE | regalloc2 or Fastalloc | `object` crate; JIT | per function | 10–50% faster than LLVM in rustc | regalloc dominates compile time |
| V | AST → C → tcc | tcc | tcc | tcc or its own Mach-O writer | parallel C chunks | 108k–201k lines/s to C (M5) | — |
| Odin, C3 | AST → LLVM | LLVM | LLVM | lld, radlink, mold | per-package LLVM modules | 90–98% of time in LLVM | speed |

---

## 5. Which designs reached about 1M lines/s for a full build?

| Design | Full build with link? | Published number | Status |
|---|---|---|---|
| Single pass, no IR, own linker (tcc) | yes | 859k lines/s; P4 2.4 GHz; tcc 0.9.22; Links browser | measured by the author ([bellard.org/tcc](https://bellard.org/tcc/)) |
| Single pass with units (Delphi 2) | yes | 1.2M lines/s in a Windows 98 VM | third party, **UNVERIFIED** |
| Single pass with units (Delphi 10.1) | yes | about 1M lines in 4–5 s, so about 200–250k lines/s | vendor, **UNVERIFIED** |
| Bytecode plus naive x64 backend (Jai) | yes, with an external link | 250k lines/s; goal 1M | **UNVERIFIED** |
| Lowering to C, then tcc (V) | yes | 700k claimed; 108k–201k measured, without the C step | claim vs measurement |
| Typed IR plus own backend (Zig, Go) | yes | no lines/s published | — |
| Data-oriented front end plus LLVM (Carbon) | no | 985k lines/s through check, one core | measured (front end only) |
| Front end plus LLVM (Odin, C3) | yes | LLVM and link take 90–98% of the time | author claims |

**Answer.**
- Only single-pass compilers with no IR and their own linker have published full-build numbers near 1M lines/s: tcc measured, Delphi 2 unverified.
- Those numbers come from single-threaded runs on old hardware, over code that is mostly declarations.
- No IR-based or multithreaded compiler publishes a full-build lines/s number near 1M.
- The best IR-based evidence is per stage. Carbon's front end runs at about 1M lines/s per core through check, and TPDE's backend is 12–19× faster than LLVM -O0. Nobody has combined the two with a fast linker and published the result.

---

## 6. Cost per stage

| Stage | Number | Per unit | Source | Kind |
|---|---|---|---|---|
| Lex (chibi-v2) | 1,356 MB/s | about 135 cycles per line | owner | measured |
| Lex (Carbon) | 6.17M lines/s, 36.6M tokens/s | about 162 ns per line, about 27 ns per token | [PR #4408](https://github.com/carbon-language/carbon-lang/pull/4408) | measured; per-unit derived |
| Parse (Carbon) | 3.74M lines/s cumulative | about +105 ns per line | same | derived |
| Check (Carbon) | 985k lines/s cumulative | about +750 ns per line | same | derived |
| Lex + parse (target) | 10M lines/s | 100 ns, 200–300 cycles per line | [Carbon slides](https://chandlerc.blog/slides/2023-cppnow-compiler/) | claim |
| Check (target) | 1M lines/s | 1 µs per line | same | claim |
| Whole pipeline (tcc) | 859k lines/s | about 2,800 cycles per line at 2.4 GHz | [bellard.org/tcc](https://bellard.org/tcc/) | derived |
| Backend, IR to code | 12–19× faster than LLVM -O0 | no ns per instruction published | [TPDE](https://arxiv.org/abs/2505.22610) | measured |
| Inside TPDE | codegen 49%, LLVM preparation 14%, analysis 12% | — | same | measured |
| Register allocation (Cranelift) | dominates; Fastalloc about 6× cheaper | — | [Fallin](https://cfallin.org/blog/2022/06/09/cranelift-regalloc2/), [d-sonuga](https://d-sonuga.netlify.app/gsoc/regalloc-iii/) | claim, measured |
| Memory per stage (Zig) | MIR is 20–50× AIR; AIR about 5 B per instruction + 4 B per extra word | — | [Queue.zig](https://github.com/ziglang/zig/blob/e4cbd752c8c05f131051f8c873cff7823177d7d3/src/link/Queue.zig#L70-L73), [Compilation.zig](https://github.com/ziglang/zig/blob/e4cbd752c8c05f131051f8c873cff7823177d7d3/src/Compilation.zig#L5073-L5076) | source comment |
| Link (Go 1.13) | about 1 s for cmd/compile; 23.5% GC, 23% DWARF compression | — | [Clements](https://docs.google.com/document/d/1D13QhciikbdLtaI67U6Ble5d_1nsI4befEd6_k1z91U/mobilebasic) | measured |
| Link (mold) | 4.51 GiB in 1.65 s on 64 cores, 2.22 s on an M1 Ultra | — | [mold README](https://github.com/rui314/mold/blob/331ac663668dc0b1f4949403854613b9b1913025/README.md#L32-L115) | measured |
| Small build (Jai) | link 0.329 of 0.426 s | — | [The Way to Jai](https://github.com/Ivo-Balbaert/The_Way_to_Jai/blob/main/book/04A_More_info_about_the_compiler.md) | **UNVERIFIED** |

**Record sizes, for comparison.**

| Compiler | Record | Size |
|---|---|---|
| Carbon | token | 8 B |
| Carbon | parse node | 8 B |
| Carbon | SemIR instruction | 16 B |
| Zig | token | 5 B |
| Zig | AST node | 13 B |
| Zig | MIR instruction | 10 B |
| Cranelift | CLIF instruction | 16 B |
| Cranelift | machine instruction | 48 B |
| Go | `ssa.Value` | 112 B |
| Go | `obj.Prog` | 200 B |
| tcc | — | no per-node record, only about 512 live `SValue`s |

---

## 7. Single pass versus IR for chibi-v2 (interactions only)

Another agent covers these features in depth. This section only notes where they interact with the choice between a strict single pass and a per-function IR.

| Feature | Strict single pass (tcc) | Per-function IR (Zig, Carbon, TPDE input) | Interaction for chibi-v2 |
|---|---|---|---|
| Compile-time execution | A call to a function defined later cannot run, because nothing has been compiled yet. tcc's in-memory `-run` is one-shot ([tccrun.c L158](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tccrun.c#L158-L159)). | Zig evaluates comptime inside Sema and interns the results as values ([Sema.zig](https://github.com/ziglang/zig/blob/e4cbd752c8c05f131051f8c873cff7823177d7d3/src/Sema.zig#L1-L6)). MIR can interpret its IR or generate code lazily ([MIR.md](https://github.com/vnmakarov/mir/blob/dc2c763f1c8bc746102ce87933d866335f9e6c06/MIR.md#L712-L734)). | Comptime needs demand-driven compilation of the callee first: a work queue, not a fixed source order. An IR that can be interpreted, or a backend that can emit one function to memory and run it, both work. Comptime must use the target's sizes and layout, not the host's, when building x86_64 on an M4. |
| Generics | tcc keeps `inline` bodies as token strings and re-parses them on use ([tccgen.c L8739](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tccgen.c#L8739-L8767)). Each instantiation would re-parse. | Instantiate from stored IR. Deduplicate by interned type ids (Zig InternPool, Carbon `CanonicalValueStore`). Go shares code by GC shape at a run-time cost. | Store generic bodies as checked, compact IR (or as a parse tree plus the declaration) and key instances by interned type-tuple id. Re-lexing is cheap at 135 cycles per line, but re-parsing and re-checking per instance is not. |
| Reflection | Type tables can be emitted as data with relocations at the end of a unit. | The same, but driven from interned types. | Odin found that RTTI order blocks separate package compilation ([comment](https://github.com/odin-lang/Odin/issues/1870#issuecomment-1171014422)). Emit type info per unit, refer to it through relocations, and order it deterministically at link time. |
| Hot reload (Edit and Continue) | One `.text`, direct PC-relative calls, relocations consumed and freed after `-run`. No unit to replace. | Zig gives atoms spare capacity and writes updates into a live process ([Elf.zig](https://github.com/ziglang/zig/blob/e4cbd752c8c05f131051f8c873cff7823177d7d3/src/link/Elf.zig#L180-L189), [link.zig](https://github.com/ziglang/zig/blob/e4cbd752c8c05f131051f8c873cff7823177d7d3/src/link.zig#L570-L612)). MIR calls through thunks ([2022 post](https://developers.redhat.com/articles/2022/02/16/code-specialization-mir-lightweight-jit-compiler)). | Edit and Continue needs one code atom per function with its relocations kept, and calls through an indirection table, so a new body can go anywhere and only one pointer changes. tcc's "named locals always live at fixed frame offsets" model makes it simpler to map state at safe points than a register allocator that keeps locals in registers. On an M4, in-memory code needs Apple's JIT rules: `MAP_JIT` and per-thread write protection ([Apple guide](https://developer.apple.com/documentation/apple-silicon/porting-just-in-time-compilers-to-apple-silicon); text not fetched). |
| Coroutines and tasks | Frame size is known only at the function's end, and tcc already back-patches the prologue for that ([arm64-gen.c L1584-L1611](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/arm64-gen.c#L1584-L1611)). With every named local in memory, a heap frame plus a resume index works in one pass. | With an IR, liveness across suspend points can shrink the frame, but layout needs the whole function first. | Both work. One pass gives larger frames (every local) and no register-held values across a suspend. An IR gives smaller frames at the cost of a liveness pass. Even TPDE runs one analysis pass before its emission pass. |
| Packages | One pass needs declare-before-use. Pascal units and Go export data supply interfaces. | Zig checks lazily per declaration in one compilation unit. Carbon uses "information accumulation". | Out-of-order declarations inside a package need a declaration pass before bodies. Across packages, interface files with interface-only invalidation are proven by Pascal CRCs, Hare `.td` hashes and Go export data. |
| Parallelism | tcc keeps all state in globals behind one lock. | Zig: serial Sema, parallel codegen, ordered link thread. Go: parallel backend per function. | A one-pass emitter per function is parallel as soon as its state is a struct passed by reference. Order the outputs as Zig's link queue does, so builds are reproducible. |

**What this points to.** This is an inference, not a measured result.
- Keep tcc's backend ideas, and run them per function from a compact IR instead of from the parser:
  - value-location-driven instruction selection;
  - forward-jump chains threaded through the code;
  - a patched prologue;
  - relocations recorded at emission;
  - a built-in linker with an in-memory mode.
- TPDE shows that this kind of backend can sit on an SSA-like IR and still be 12–19× faster than LLVM -O0.
- Carbon shows that a front end that builds such an IR can run at about 1M lines/s per core.

---

## 8. Not verified

**Jai.** Every speed number and design detail comes from community wikis, a book or third-party clips. I verified no timestamps inside Blow's videos.

**Delphi and Turbo Pascal.**
- The Delphi 10.1 "1M lines in 4–5 s" figure: the vendor's blog fetch timed out.
- The Delphi 2 1.2M lines/s figure and the tcc 0.7M figure come from a third-party tweet.
- I found no primary source on Turbo Pascal's code generator or register use.

**Lines/s figures that are missing or unconfirmed.**
- Carbon: a secondary summary says the 10M/1M lines/s goals were reached on an M4; the slides have no numbers.
- V: the 700k LOC/s claim.
- Odin: "150 KLOC front end in about 500 ms" comes from a tweet I could not fetch.
- Go, Cranelift, MIR, QBE, cproc, Hare and Tilde publish no lines/s figure.

**Missing hardware and missing gaps.**
- The Zig, Go 1.15/1.16, Carbon PR #4408 and Cranelift numbers do not state their hardware.
- I found no number for the code-quality gap of the Zig x86 backend.

**Other unknowns.**
- TPDE: the paper does not discuss multithreading.
- mold: Mach-O support is undocumented.
- Apple's JIT guide did not render, so the `MAP_JIT` requirements above come from its title and general knowledge.

**My own estimate.** No one has published a tcc measurement on an M4 or any modern CPU. The claim that a tcc-like pipeline would pass 1M lines/s on one M4 core is extrapolated from 2,800 cycles per line on a Pentium 4.

**Clock.** The M4 clock (about 4 GHz) behind the budget in section 1 is an assumption.

---

## 9. Code to read first

Read in this order. Each item names what to learn.

1. **tcc manual, developer chapter.** [`tcc-doc.texi` L1500-L1980](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tcc-doc.texi#L1500-L1980). The whole design in 500 lines. Note the `VT_LVAL` explanation and the closing note on which optimizations need an IR.
2. **tcc `SValue` and location codes.** [`tcc.h` L482-L496](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tcc.h#L482-L496), [L1026-L1034](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tcc.h#L1026-L1034), and the backend interface [L1625-L1653](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tcc.h#L1625-L1653). The backend contract is about 20 functions.
3. **tcc value stack and flags.** [`tccgen.c` L864-L1097](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tccgen.c#L864-L1097): `vcheck_cmp`, `vsetc`, `vswap`, `vset_VT_CMP`, `gvtst`. Look at how a comparison stays in the flags and how `&&`/`||` become jump lists.
4. **tcc register allocation.** [`tccgen.c` L1394-L1571](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tccgen.c#L1394-L1571): `save_regs`, `save_reg_upstack`, `get_reg`, `get_temp_local_var`. The whole allocator, with spilling from the bottom of the stack.
5. **tcc `gv`.** [`tccgen.c` L1847-L2026](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tccgen.c#L1847-L2026). Where deferred values become instructions.
6. **tcc folding.** [`tccgen.c` L2363-L2549](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tccgen.c#L2363-L2549) (`gen_opic`). Constant folding and strength reduction on the value stack, with no IR.
7. **tcc control flow.** [`tccgen.c` L166-L205](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tccgen.c#L166-L205) and [L7234-L7281](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tccgen.c#L7234-L7281). Code suppression after jumps, and `if`/`while` in a dozen lines.
8. **tcc arm64 backend.** [`arm64-gen.c`](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/arm64-gen.c) in this order:
   - `o` [L115](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/arm64-gen.c#L115-L124);
   - `gsym_addr` [L243](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/arm64-gen.c#L243-L257);
   - `arm64_sym` [L471](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/arm64-gen.c#L471-L515);
   - `load` [L519](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/arm64-gen.c#L519);
   - the prologue NOP slots and epilogue patch [L1352](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/arm64-gen.c#L1352-L1356) and [L1584](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/arm64-gen.c#L1584-L1611);
   - jumps [L1624-L1709](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/arm64-gen.c#L1624-L1709);
   - immediate forms [L1725](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/arm64-gen.c#L1725-L1802).

   This is the closest real code to an M4 backend that chibi-v2 could start from.
9. **tcc x86-64 differences.** [`x86_64-gen.c` L172-L262](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/x86_64-gen.c#L172-L262) and [L1577-L1700](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/x86_64-gen.c#L1577-L1700). Byte emission, `rel32` jump chains, the short backward jump, and the 11-byte prologue patch. Note the "XXX: make it faster ?" on `g()`: one byte per call, with a bounds check each time.
10. **tcc linking and in-memory run.** [`tccelf.c` L789-L817](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tccelf.c#L789-L817), [L1084-L1228](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tccelf.c#L1084-L1228), then [`tccrun.c` L330-L507](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tccrun.c#L330-L507). A linker is a few loops over `Rela` arrays. Note what `-run` frees, and why that rules out reload.
11. **tcc Mach-O writer.** [`tccmacho.c` L20-L32](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tccmacho.c#L20-L32) and [L2179-L2249](https://github.com/TinyCC/tinycc/blob/aade7bafb59641660bc9a0e21ad0a9a4a1ed346e/tccmacho.c#L2179-L2249). The minimum Mach-O an M4 accepts. Compare it with Go's in-process signer [codesign.go](https://github.com/golang/go/blob/go1.27.3/src/cmd/internal/codesign/codesign.go).
12. **TPDE.** [`Analyzer.hpp`](https://github.com/tpde2/tpde/blob/9779acf4ada3736e779391da1e4b3369dba08024/tpde/include/tpde/Analyzer.hpp) and [`CompilerBase.hpp` L1383-L1660, L2269-L2462](https://github.com/tpde2/tpde/blob/9779acf4ada3736e779391da1e4b3369dba08024/tpde/include/tpde/CompilerBase.hpp#L2269). How to get tcc-like speed from an SSA input: one analysis pass, one emission pass, and spills at join points.
13. **Carbon's flat front end.** [`token_info.h` L154-L210](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/lex/token_info.h#L154-L210), [`parse/tree.h` L199-L243](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/parse/tree.h#L199-L243), [`sem_ir/inst.h` L330-L357](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/sem_ir/inst.h#L330-L357), [`lex.cpp` L538-L600](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/lex/lex.cpp#L538-L600). The 8 B and 16 B records, and the NEON-on-Apple caveat. Also its per-phase benchmark [`compile_benchmark.cpp`](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/benchmarking/compile_benchmark.cpp), which is worth copying.
14. **Zig's parallel interning and ordered link.** [`InternPool.zig` L1029-L1060, L7684-L7760](https://github.com/ziglang/zig/blob/e4cbd752c8c05f131051f8c873cff7823177d7d3/src/InternPool.zig#L7684-L7760), then [`link/Queue.zig` L1-L80](https://github.com/ziglang/zig/blob/e4cbd752c8c05f131051f8c873cff7823177d7d3/src/link/Queue.zig#L1-L80) and [`Air/Liveness.zig` L1-L40](https://github.com/ziglang/zig/blob/e4cbd752c8c05f131051f8c873cff7823177d7d3/src/Air/Liveness.zig#L1-L40). Thread-tagged u32 handles, deterministic output from parallel codegen, and 4-bit liveness.
15. **Go's parallel backend and allocator.** [`gc/compile.go` L98-L200](https://github.com/golang/go/blob/2aae1000ea1d4b28923b96db28acf4e3b99d6aff/src/cmd/compile/internal/gc/compile.go#L98-L200) and [`ssa/regalloc.go` L5-L120](https://github.com/golang/go/blob/2aae1000ea1d4b28923b96db28acf4e3b99d6aff/src/cmd/compile/internal/ssa/regalloc.go#L5-L120). Function-level fan-out in about 60 lines, and a cheap linear scan.
16. **MIR thunks and lazy generation.** [`MIR.md` L712-L734](https://github.com/vnmakarov/mir/blob/dc2c763f1c8bc746102ce87933d866335f9e6c06/MIR.md#L712-L734) and [`mir-gen.c` L9831-L10069](https://github.com/vnmakarov/mir/blob/dc2c763f1c8bc746102ce87933d866335f9e6c06/mir-gen.c#L9831-L10069). Call indirection that lets code be replaced. Relevant to Edit and Continue.
17. **Cranelift backward lowering and single-pass allocation.** [`machinst/lower.rs` L830-L860](https://github.com/bytecodealliance/wasmtime/blob/3e0ee3fbfdd88d06c3f8c169168fe0d3b0c0096d/cranelift/codegen/src/machinst/lower.rs#L830-L860) and [regalloc2 `FASTALLOC.md`](https://github.com/bytecodealliance/regalloc2/blob/4a233442f781dd7b84d5ea40504d3be07263b3e3/doc/FASTALLOC.md). Instruction merging without a pass, and what a reverse linear scan costs.
18. **QBE pass order.** [`main.c` `func()`](https://c9x.me/git/qbe.git/tree/main.c?id=180d2eb4157b706dc480bce50ff6d03a65ec334b#n53). A complete SSA backend on one page. Note the streaming per function and freeing after each.
19. **mold design.** [`design.md`](https://github.com/rui314/mold/blob/v2.40.0/docs/design.md#L36-L80) and [L399-L459](https://github.com/rui314/mold/blob/v2.40.0/docs/design.md#L399-L459). Why a fast full link beats incremental linking, and how to lay out output early.
20. **chibicc codegen.** [`codegen.c`](https://github.com/rui314/chibicc/blob/90d1f7f199cc55b13c7fdb5839d1409806633fdb/codegen.c#L31-L39). Only as a contrast: an AST-then-emit stack machine, slower to compile and slower to run than tcc.
