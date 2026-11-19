# Optimizing backends other than LLVM

Research note, 2026-11-19. Question (owner, 2026-11-19): is there a backend that reaches most of LLVM's performance with a fraction of its code, compiling fast and producing good-enough code? The owner named QBE, libgccjit, Cranelift, TPDE, MimIR, cppgm (the one of most interest) and MYC. This note adds MIR, Tilde, libFirm and WebKit's B3.

**Scope.** For each backend: code size, compile speed, speed of the generated code against LLVM at `-O2`, x86-64 and arm64 support, license, whether C can embed it, and what chibi could borrow or adopt. [fast-compilers.md](fast-compilers.md) sections 3.6–3.12 already cover QBE, MIR, TPDE, Cranelift and Tilde as fast compilers; this note asks the other question, how close each gets to LLVM's code, and adds what changed since 2026-11-09.

**Sources.** I read each project's source at the commits below, from clones, and counted lines myself with `wc -l` over C, C++, Rust, ISLE, TableGen and Crystal files, leaving out test directories. Counts include comments and blank lines. Speeds come from published measurements; the most useful is a third-party benchmark that runs one program through LLVM, QBE, cppgm and C from one IR (section 3.2). **Nothing here was built or timed in this container.** The sandbox did not allow running the downloaded compilers, and these hosts were unreachable, so their pages weren't read: c9x.me (QBE's site), gcc.gnu.org, medium.com, news.ycombinator.com, arxiv.org and forum.crystal-lang.org. GitHub was reachable, so where a project's docs live in its repository I read them there. Claims I could not read directly are marked "(unverified)" and listed in section 9.

| Project | Commit, date | Project | Commit, date |
|---|---|---|---|
| cppgm | `56c86f75`, 2026-11-19 | MIR | `dc2c763f`, 2026-10-23 |
| MYC | `0e2b1c6c`, 2026-11-18 | TPDE | `050927ee`, 2026-11-13 |
| myc-benchmarks | `b6818315`, 2026-11-17 | Cranelift (in wasmtime) | `e355dc64`, 2026-11-19 |
| index-c | `019ddd8f`, 2026-11-11 | regalloc2 | `4eb655fa`, 2026-11-17 |
| QBE, tag v1.3 | `c0818978`, 2026-05-12 | GCC (gcc/jit only) | `26664387`, 2026-11-19 |
| QBE, as benchmarked (`kostya/qbe`) | `fab6d406`, 2026-07-24 | LLVM (llvm/lib, llvm/include) | `21453404`, 2026-11-19 |
| Tilde (in Cuik) | `a7677f74`, 2026-07-24 | libFirm | `114012d1`, 2025-03-04 |
| MimIR | `210dcb3e`, 2026-11-19 | | |

QBE's own server was unreachable, so I read QBE from Konstantin Makarchev's GitHub fork, which is tag v1.3 plus three upstream fixes and one commit of his own ("prod flags"). The QBE links below point at the upstream tag.

---

## Summary

1. **One candidate meets the ask as stated, on x86-64: cppgm's LowIR.** Its backend is about 138,000 lines of C++, 8% of the 1.68 million lines of the LLVM libraries a two-target optimizing build uses. A third-party benchmark puts its code within 1% of clang `-O2` overall (within 2% of `-O3`), compiling 3.9× faster than clang from IR. It has a C API that builds IR in batches and returns object bytes in memory, under Apache-2.0. Against it: the project is six months old, LLM agents wrote it for one author (3,879 commits, 1,601 of them this month), its AArch64 backend is experimental and has never been timed on Apple Silicon, and the library compiles one module at a time per process.
2. **The small backends don't reach "most".** QBE (16,000 lines) produces code at 0.57–0.67 of clang `-O2`'s speed on the same benchmark. MIR (28,000 lines for its x86-64 and arm64 generator) reaches 0.91 of gcc `-O2` on 15 small benchmarks, but it only JITs: it writes no object files.
3. **Cranelift and libgccjit miss on other counts.** Cranelift's code is about 14% slower than LLVM's and it compiles about 10× faster, but it is 277,000 lines of Rust with no C API. libgccjit gives GCC's code at GCC's speed, through a temporary directory, `as`, `ld` and `dlopen`, behind one process-wide mutex, under GPLv3.
4. **The rest don't fit this role.** TPDE builds `-O0`-class code, which is the role chibi's own arm64 backend already fills, and writes ELF only. MimIR is a research IR for DSLs that emits LLVM IR; its native x86-64 backend is in progress. MYC is not a backend but a 31-opcode stack IR over LLVM, QBE, C and cppgm: the same shape as chibi's IR, and the source of the best side-by-side numbers here. Tilde is unfinished, and Odin and C3 both removed it. libFirm has no AArch64 backend, its amd64 backend is marked unfinished, and it is LGPL.
5. **The price of the last 10%.** Across these projects, about 16,000 lines buy 0.6 of LLVM's code speed, about 28,000 buy 0.9 on small benchmarks, and about 138,000 buy parity on x86-64 (section 4). Every candidate that gets close relies on inlining, promoting memory to registers and a register allocator that splits live ranges.
6. **Recommendation (section 7).** Keep chibi's own backend for development builds, and keep the C backend as the first release path, as decided. Change "LLVM through its C API, later" to: after the C backend works, spend one spike translating chibi's IR to cppgm's LowIR and time it against the C backend plus clang `-O2` on the compiler itself, on x86-64 first. Adopt it only if it holds up on arm64. For chibi's own optimizing backend, which the no-C-compiler goal still needs, aim at MIR's design point and read MIR, then QBE.

---

## Contents

1. [What chibi needs from this backend](#1-what-chibi-needs-from-this-backend)
2. [Comparison table](#2-comparison-table)
3. [Each candidate](#3-each-candidate)
4. [What each step toward LLVM costs](#4-what-each-step-toward-llvm-costs)
5. [Fit with chibi's contracts](#5-fit-with-chibis-contracts)
6. [Ideas to borrow](#6-ideas-to-borrow)
7. [Recommendation](#7-recommendation)
8. [Questions for the owner](#8-questions-for-the-owner)
9. [Not verified](#9-not-verified)

---

## 1. What chibi needs from this backend

**The roles are already split** ([../decisions.md](../decisions.md), Compiler rows):
- **Development builds** use chibi's own one-pass backend, for 1M lines per second and hot reload (2026-11-09). The arm64 version generates code at 17–19M lines per second, about 220 cycles per line ([experiments/arm64-backend](../../experiments/arm64-backend/README.md)). Every optimizing backend in this note is at least a hundred times slower per line, so none of them competes for this role.
- **Release builds** use a C backend first; "LLVM, through its C API and fed from the same IR, comes later if debug info or release build time needs it" (2026-11-09).
- **The long-term goal** is no C compiler, which needs "chibi's own optimizing backend, or LLVM linked as a library" (2026-11-09).

So the candidates compete for two places: the release backend that follows the C backend, and the model for chibi's own optimizing backend.

**The owner's criteria:** code size, compile speed, generated code against LLVM `-O2`, x86-64 and arm64, license, and embedding from C.

**chibi's own contracts** add these (section 5):
- `+`, `-` and `*` trap on overflow in debug and release builds (rank 1, decided 2026-11-10), so the backend needs cheap overflow checks.
- No strict aliasing (the Casts row), so the backend must not assume type-based aliasing.
- No FMA contraction is proposed for every CPU backend (rank 3, still open).
- Mach-O on the owner's M4 Pro, and ELF.
- Code generation per function on all cores (README, "The shape the four notes agree on", point 4).
- The IR is shape A: locals in slots, single-use temporaries, structured control flow (decided 2026-11-12). An optimizing backend promotes slots itself, as LLVM's `mem2reg` promotes Clang's `alloca`s ([checker-and-ir.md](checker-and-ir.md) 5.5).

---

## 2. Comparison table

"Code speed" is the generated code's throughput relative to the named compiler, so 0.65 means it takes 1/0.65 = 1.54 times as long. Line counts are mine (section "Sources").

| Backend | Lines | Language | Code speed | Compile speed | x86-64 / arm64 | Output | License | From C |
|---|---|---|---|---|---|---|---|---|
| LLVM | 1.68M in the libraries a two-target build uses; 4.30M in `llvm/lib` and `llvm/include` | C++ | reference | reference | both | objects, JIT | Apache-2.0 with LLVM exception | C API |
| **cppgm LowIR** | about 138k (backend and optimizer) | C++ | 0.99 of clang `-O2`, 0.98 of `-O3` (index-c, third party); 1.00 of clang `-O3` (80 programs, author) | 3.9× faster than clang `-O2` from IR (third party); 3.5× faster than clang++ `-O3` (author) | x86-64 mature; arm64 experimental, 0.97 of clang on Linux Arm | ELF and Mach-O objects in memory, Wasm | Apache-2.0 | C header; library needs libstdc++ |
| MIR | 28k generator for x86-64 and arm64; 157k repository | C | 0.91 of gcc `-O2` (15 small benchmarks, author) | 109× faster than gcc `-O2` on one small file (author) | both, Linux and macOS | **JIT only** | MIT | C API |
| QBE 1.3 | 16k | C | 0.67 of clang `-O2` (index-c, third party); cproc 0.57 | 5.2× faster than clang `-O2` from IR, whole commands timed (third party) | both, including Apple | **assembly text**, from text IL | MIT | none: a program |
| Cranelift | 277k with regalloc2 and the ISLE compiler | Rust | about 14% slower than LLVM (WebAssembly, 2020 paper) | about 10× faster than LLVM (same paper) | both | objects via `object` crate, JIT | Apache-2.0 with LLVM exception | **no C API** |
| libgccjit | 30k on top of GCC's about 2.4M | C++ | GCC's | GCC's, plus `as`, `ld` and `dlopen` | one target per build | `.s`, `.o`, `.so`, executable | **GPLv3+** | C API |
| TPDE | 15k core plus an adaptor you write | C++20 | `-O0`-class; 0.57–0.65 of LLVM `-O1` | 12–19× faster than LLVM `-O0` | both | **ELF only**, in-memory | Apache-2.0 with LLVM exception | C++ templates |
| Tilde (TB) | 43k | C | none published | none published | x86-64; arm64 is 2k lines | COFF, ELF, Mach-O, JIT | MIT | C API |
| libFirm | 186k | C | cparser `-O3` 0.98 of gcc `-O2` (author of MIR) | none current | ia32 complete; **amd64 unfinished, no arm64** | assembly text, JIT | **LGPL-2.1** | C API |
| MimIR | 45k | C++23 | none for general code | none | emits LLVM IR; native x86-64 in progress | LLVM IR, x86-64 assembly | MIT | no |
| MYC | 18k with its C front end | Crystal | that of the backend it drives | that of the backend it drives | via LLVM, QBE, C; cppgm on x86-64 only | through backends | Apache-2.0 | no |

---

## 3. Each candidate

### 3.1 cppgm and its LowIR library

**What it is.** A C++11/14/17 compiler that Vishvananda Abrams built with LLM agents, starting 2026-05-14. The README links two posts, "I Spent 2 Billion Tokens Writing a C++ Compiler So You Don't Have To" and "How I Built a Compiler That Is Faster Than GCC and Clang" ([README L13-L19](https://github.com/vishvananda/cppgm/blob/56c86f755ec2b56e235b3f5e5ff76e6d147c3f93/README.md#L13-L19)); medium.com was unreachable, so I read neither. The repository doubles as a course: numbered assignments, PA1 to PA39, that lead an agent from tokens to an optimizing backend ([ROADMAP.md](https://github.com/vishvananda/cppgm/blob/56c86f755ec2b56e235b3f5e5ff76e6d147c3f93/ROADMAP.md)). The original 2013 C++ Grandmaster Certification course supplied the early material ([NOTICE](https://github.com/vishvananda/cppgm/blob/56c86f755ec2b56e235b3f5e5ff76e6d147c3f93/NOTICE)).

**History.** 3,879 commits by one author: 76 in May, 263 in June, 695 in July, 680 in August, 83 in September, 481 in October, 1,601 in November up to the 19th. Release 0.9.8 is the latest tag.

**Size.** `dev/src` holds 313,463 lines. The backend and optimizer:

| Part | Lines |
|---|---|
| `lowir/`: the model, reader, writer, validator | 10,105 |
| `opt/`: the LowIR optimizer | 53,996 |
| `native/`: x86-64 selection, register allocation, objects, linking | 46,014 |
| `aarch64/` | 8,541 |
| `abi/`, `asm/`, `machine/`, `backend/` | 13,424 |
| shared headers at the top of `dev/src` (also used by the front end) | 6,035 |
| **Total** | **about 138,000** |

`wasm/` adds 12,693. The C++ front end (`sema/`, `lower/`, `syntax/`, `lex/`, `preprocess/`) is most of the rest.

**The IR.** LowIR is typed SSA with phis, stack slots, and explicit symbols and signatures; its opcodes include `phi`, `load`, `store`, `binary`, `cmp`, `select`, `call`, `switch` and exception markers ([lowir_model.h L210-L270](https://github.com/vishvananda/cppgm/blob/56c86f755ec2b56e235b3f5e5ff76e6d147c3f93/dev/src/lowir_model.h#L210-L270)). The optimizer has 61 files, among them an inliner, slot promotion, aggregate splitting, memory forwarding, jump threading, loop rotation, unrolling, unswitching, interchange and a loop vectorizer ([dev/src/opt](https://github.com/vishvananda/cppgm/tree/56c86f755ec2b56e235b3f5e5ff76e6d147c3f93/dev/src/opt)). The assignment notes for the optimizer and the machine backend describe the reasoning, not the algorithms ([pa33/design-notes.md](https://github.com/vishvananda/cppgm/blob/56c86f755ec2b56e235b3f5e5ff76e6d147c3f93/pa33/design-notes.md), [pa34/design-notes.md](https://github.com/vishvananda/cppgm/blob/56c86f755ec2b56e235b3f5e5ff76e6d147c3f93/pa34/design-notes.md)).

**The library** ([docs/lowir-library.md](https://github.com/vishvananda/cppgm/blob/56c86f755ec2b56e235b3f5e5ff76e6d147c3f93/docs/lowir-library.md)):
- A C header, `dev/src/lowir_interface.h`, and a static or shared library. C code compiles with a C compiler and links with the C++ driver (L16-L26).
- **Construction is batched.** For each function the producer reserves instruction, operand, literal, slot and block pools that the library owns, writes typed records straight into them, and commits once. Commit "neither copies records nor remaps IDs" (L39-L46). There is no per-instruction call.
- It validates, optimizes at `O0`–`O3`, selects and encodes, then hands back the object bytes. "There are no subprocesses or temporary files inside the library" (L47-L51).
- **One compile at a time per process.** "Compilation calls serialize inside the library because the existing optimizer and register allocator reuse process-wide scratch buffers" (L78-L83). Modules can be built and validated concurrently.
- Targets per module: x86-64 ELF and Mach-O, three Wasm flavors, and experimental AArch64 ELF and Apple Silicon Mach-O (L176-L188).
- The ABI version went from 2 to 5 within months, each version adding to the last without changing it.

**Speed, from the author** ([public.md L11-L28](https://github.com/vishvananda/cppgm/blob/56c86f755ec2b56e235b3f5e5ff76e6d147c3f93/docs/performance/preview/public.md#L11-L28), AMD EPYC 9V45, 81 public C++ programs). These time the whole C++ compiler, front end included:

| Measure | vs g++ | vs clang++ |
|---|---|---|
| Front end alone, `-fsyntax-only` | 1.97× faster | 2.42× faster |
| Compile at `-O0` | 2.75× faster | 2.64× faster |
| Compile at `-O3` | 4.02× faster | 3.49× faster |
| Generated code at `-O3` | 0.99 | 1.00 |

Since its front end is only about 2× faster than Clang's while `-O3` compiles are 3.5× faster, its optimizer and backend must be well over 3.5× faster than LLVM's (my arithmetic, assuming the same split of time).

**Speed on arm64, from the author.** On Google Axion, a revision reached 0.966 of Clang's throughput on the public suite and 0.979 on index-cpp ([README L218](https://github.com/vishvananda/cppgm/blob/56c86f755ec2b56e235b3f5e5ff76e6d147c3f93/README.md#L218)). On an Ampere Altra, the self-built compiler ran 1.11–1.14× slower than the Clang-built one, and these runs "give no timing estimate for Apple Silicon" ([aarch64-optimization-gaps.md L547-L618](https://github.com/vishvananda/cppgm/blob/56c86f755ec2b56e235b3f5e5ff76e6d147c3f93/docs/performance/aarch64-optimization-gaps.md#L547-L618)).

**Speed from a third party** (section 3.2): 0.99 of clang `-O2`'s code speed and 3.9× its compile speed, through MYC.

**Caveats.**
- The README itself: "The compiler can contain bugs" and "The generated code can be slow for program shapes that the benchmarks do not cover" ([L240-L241](https://github.com/vishvananda/cppgm/blob/56c86f755ec2b56e235b3f5e5ff76e6d147c3f93/README.md#L240-L241)).
- **The third-party benchmark is not independent of the tuning.** index-c is the C version of LangArena's 50 tasks; its C++ version, index-cpp, is one of the suites cppgm tracks, and cppgm has an "index-cpp optimization plan" ([benchmarks/index_cpp](https://github.com/vishvananda/cppgm/blob/56c86f755ec2b56e235b3f5e5ff76e6d147c3f93/benchmarks/index_cpp/README.md), [docs/index-cpp-optimization-plan.md](https://github.com/vishvananda/cppgm/blob/56c86f755ec2b56e235b3f5e5ff76e6d147c3f93/docs/index-cpp-optimization-plan.md)).
- One author, with LLM agents writing the code. I found no outside review of the backend.

**For chibi.** The only library here that fits the release role on x86-64 today, with an API shaped the way chibi's conventions ask for (batches, records written in place, IDs instead of pointers). The open risks are arm64 on the M4 Pro, one compile per process, and the overflow checks (section 5). It is also a C compiler (`c17` and `gnu17`, optional, [README L228](https://github.com/vishvananda/cppgm/blob/56c86f755ec2b56e235b3f5e5ff76e6d147c3f93/README.md#L228)), so the C backend could be compiled by it instead of clang; that route parses C again and gains nothing over the library.

### 3.2 MYC

**What it is.** Not a backend. Konstantin Makarchev's "small IR for building programming languages": about 31 stack-based opcodes with structured control flow (`IF`, `LOOP`, `SWITCH`, `BREAK`), written in Crystal, that a front end emits in one tree walk and MYC lowers to LLVM, QBE, C or cppgm's LowIR ("GM") ([README L1-L34](https://github.com/kostya/myc/blob/0e2b1c6c8265d27b1c3e51129299987da6dcacc3/README.md#L1-L34)). Started 2026-06-26, 434 commits, Apache-2.0, "Alpha". It comes with mycc, a C11-like front end built "with assist of LLM" on libclang (L127).

**Why it matters to chibi.**
- **The design is chibi's.** Locals live in slots and expressions are temporaries; the GM backend asks LowIR for a slot per local and leaves promotion to cppgm ([gm/bb.cr L11-L12](https://github.com/kostya/myc/blob/0e2b1c6c8265d27b1c3e51129299987da6dcacc3/src/backend/gm/bb.cr#L11-L12)). That is shape A of [checker-and-ir.md](checker-and-ir.md) section 5.
- **It sizes a translator.** Each backend is small: GM 1,844 lines of Crystal, QBE 1,339, LLVM 1,154 ([src/backend](https://github.com/kostya/myc/tree/0e2b1c6c8265d27b1c3e51129299987da6dcacc3/src/backend)).
- **It gives the best side-by-side numbers**, one IR through four backends, from someone who doesn't maintain any of them.
- It has two modes: default, "fast, Go-like", and `--final`, "slow compile, ~5-15% faster runtime" (L32). MYC's own only transformation is inlining (L31). "The GM backend is x86-64 only" (L34).

**The benchmark** ([myc-benchmarks README L61-L86](https://github.com/kostya/myc-benchmarks/blob/b6818315d8ab6c0e9abb44371d3c9e4e3fff7aec/README.md#L61-L86)): index-c, a 16,699-line C file with 50 tasks that each run about a second and check a checksum ([index-c](https://github.com/kostya/index-c/blob/019ddd8f9c4b2cac7d0512ebbf78d5b28aff63f5/README.md)), turned into MYC IR and LLVM IR so that no C front end is timed. Ryzen 3800X, clang 23.1.5, gcc 16.2.1, cppgm 0.9.6, QBE `fab6d40`. Times are the whole command as the script runs it, in seconds; the last column is my arithmetic.

| Compiler | Mode | Compile | Run | Code speed vs clang `-O2` |
|---|---|---|---|---|
| clang from `.ll` | `-O0` | 0.30 | 139.07 | 0.37 |
| clang from `.ll` | `-O2` | 1.81 | 52.10 | 1.00 |
| clang from `.ll` | `-O3` | 1.89 | 51.59 | 1.01 |
| MYC → LLVM | default | 0.94 | 60.33 | 0.86 |
| MYC → LLVM | `--final` | 2.12 | 51.92 | 1.00 |
| MYC → C → gcc | default | 2.45 | 59.13 | 0.88 |
| MYC → C → gcc | `--final` | 5.42 | 52.06 | 1.00 |
| **MYC → cppgm** | default | **0.36** | 57.18 | 0.91 |
| **MYC → cppgm** | `--final` | **0.46** | **52.50** | **0.99** |
| MYC → QBE | default | 0.35 | 78.35 | 0.67 |

From C source, with each compiler's own front end (L30-L59): clang `-O2` 2.06 s and 51.02 s, gcc `-O2` 3.65 s and 52.28 s, mycc → cppgm `--final` 0.88 s and 52.55 s, cproc → QBE 0.24 s and 88.77 s (0.57 of clang `-O2`).

Task by task against clang `-O3` (L163-L224), MYC → cppgm `--final` is 2.0% slower in total. 18 of the 50 tasks are more than 5% slower, the worst GameOfLife at +32%, TextRaytracer +29% and HuffDecode +29%; 8 are more than 5% faster, the best SHA256 at −67% and Sieve at −27%.

At 0.46 s for 16,699 lines, MYC plus cppgm handles about 36,000 lines per second on the Ryzen 3800X (my arithmetic), where chibi's arm64 backend generates code for 17–19M lines per second on the M4 Pro. Different machines, but the gap is two to three orders of magnitude, as expected for an optimizer.

### 3.3 QBE

**What it is.** Quentin Carbonneaux's SSA backend, which aims for "70% of the performance of industrial optimizing compilers in 10% of the code" ([fast-compilers.md](fast-compilers.md) 3.6). It reads a text IL and writes assembly text, one function at a time, and needs an external assembler and linker. MIT.

**Size.** 16,106 lines: 9,082 shared, 3,458 for amd64, 1,970 for arm64, 1,596 for rv64. Targets: amd64 SysV, Apple and (new in 1.3) Windows; arm64 Linux and Apple; rv64 ([main.c L22-L37](https://c9x.me/git/qbe.git/tree/main.c?id=c0818978acec60ebb6167fade60fb7012cbf20ca#n22)).

**What changed.** Version 1.3 (2026-05-12) adds 7,300 lines and removes 1,476 against 1.2 (2024-01-23): global value numbering and global code motion, if-conversion to `cmov` on x86, a CFG simplification pass, the Windows target, and fixes to exponential-time cases in the width analyses (`git log v1.2..v1.3`). The 1.3 release notes reportedly say that measurements with 1.2 showed QBE "far behind our '70% of gcc -O2' goal, closer to 40%" (unverified: a search excerpt of c9x.me/compile/release/qbe-1.3.html, which was unreachable).

**Speed.** 0.67 of clang `-O2` through MYC and 0.57 through cproc, on index-c with 1.3 plus fixes (section 3.2). Makarov measured cproc at 0.65 of gcc `-O2` on his 15 benchmarks with an older QBE ([MIR README L377-L409](https://github.com/vnmakarov/mir/blob/dc2c763f1c8bc746102ce87933d866335f9e6c06/README.md#L377-L409)). Compiling is fast: cproc took 0.24 s for index-c from C, assembly and linking included.

**For chibi.** Not a library, no threads, text in and out. Its value is as reading: the whole pass order fits on one page ([main.c `func()`](https://c9x.me/git/qbe.git/tree/main.c?id=c0818978acec60ebb6167fade60fb7012cbf20ca#n53)), and its numbers show what an SSA backend of this size reaches without inlining or live-range splitting.

### 3.4 MIR

Covered in [fast-compilers.md](fast-compilers.md) 3.7; the pinned commit is the same. What matters here:
- **Size.** The generator for x86-64 and arm64 is 27,926 lines (`mir.c` and headers 9,897, `mir-gen.c` 10,118, x86-64 4,201, arm64 3,710); the interpreter adds 2,055 and the C front end 19,796. Makarov counts 23.4K lines for "a minimal program to create and run MIR code" ([README L335-L375](https://github.com/vnmakarov/mir/blob/dc2c763f1c8bc746102ce87933d866335f9e6c06/README.md#L335-L375)).
- **Code speed.** c2m at 0.91 of gcc `-O2` (geomean of 15 small benchmarks, i5-13600K, 2023), where clang `-O2` is 1.09, cparser (libFirm) 0.98, cproc (QBE) 0.65 and tcc 0.50 (L377-L409). Small benchmarks favor a backend without loop optimizations less than real programs would; no larger suite is published.
- **Compile speed.** 249 µs for the sieve against 27.1 ms for gcc `-O2`.
- **Targets.** x86-64, aarch64, ppc64le, s390x and riscv64 on Linux; x86-64 and aarch64 on macOS, with a CI job on Apple Silicon (README L1-L21, L439).
- **Output.** JIT only: code goes straight into executable memory. `mir2c` translates MIR to C. There is no object-file writer, so release binaries would need one, plus a linker.
- **Overflow.** `ADDO`, `SUBO` and `MULO` set an overflow flag that `BO` and `BNO` branch on ([MIR.md L365-L449](https://github.com/vnmakarov/mir/blob/dc2c763f1c8bc746102ce87933d866335f9e6c06/MIR.md#L365-L449)), which is exactly chibi's trapping arithmetic.
- **Threads.** Parallel generation was removed in 2023 ([fast-compilers.md](fast-compilers.md) 3.7).

**For chibi.** The best ratio of code speed to lines here, in C, MIT, on both targets. Without object files it can't be the release backend as it stands; it is the design to study for chibi's own optimizing backend (section 6).

### 3.5 Cranelift

Covered in [fast-compilers.md](fast-compilers.md) 3.9. What matters here:
- **Code speed.** "~2% slower than that of V8 (TurboFan), and ~14% slower than WAVM (LLVM)", compiling "approximately an order of magnitude faster than WAVM (LLVM)", on WebAssembly benchmarks in a 2020 paper ([cranelift/README.md L63-L72](https://github.com/bytecodealliance/wasmtime/blob/e355dc640d047ab76dbf4402567d0c124bfdecd6/cranelift/README.md#L63-L72), citing arXiv 2011.13127, Fig. 22). I found no newer comparison of its code against LLVM on native code.
- **Size.** 277,269 lines of Rust and ISLE: `codegen` 206,585 for all its targets (x86-64 21,312 and aarch64 45,918 of that), the ISLE compiler 36,654, regalloc2 14,231, the x64 assembler 9,989, `frontend` 8,129, `object` 1,681.
- **Targets.** x86-64, aarch64, riscv64, s390x and the Pulley interpreter ([codegen/src/isa](https://github.com/bytecodealliance/wasmtime/tree/e355dc640d047ab76dbf4402567d0c124bfdecd6/cranelift/codegen/src/isa)).
- **Overflow.** `sadd_overflow`, `ssub_overflow`, `smul_overflow` and `uadd_overflow_trap` exist ([instructions.rs](https://github.com/bytecodealliance/wasmtime/blob/e355dc640d047ab76dbf4402567d0c124bfdecd6/cranelift/codegen/meta/src/shared/instructions.rs#L1946-L2135)).
- **From C.** The repository has no C API. Embedding it means a Rust shim and a Rust toolchain in chibi's build.

**For chibi.** Good numbers, the wrong language for a C-to-chibi compiler that aims to need no other compiler. Its ideas are already in fast-compilers.md: backward lowering, branch simplification while emitting, and a capped e-graph that bought about 2% faster code for 7–8% more compile time.

### 3.6 libgccjit

**What it is.** GCC as a shared library, with a C API (GCC's `gcc/jit`, 29,603 lines on top of GCC; Makarov estimates GCC's x86-64 `cc1` at 2.42M lines, [MIR README L335-L375](https://github.com/vnmakarov/mir/blob/dc2c763f1c8bc746102ce87933d866335f9e6c06/README.md#L335-L375)). Despite the name it also compiles ahead of time: `GCC_JIT_OUTPUT_KIND_ASSEMBLER`, `OBJECT_FILE`, `DYNAMIC_LIBRARY` and `EXECUTABLE` ([libgccjit.h](https://github.com/gcc-mirror/gcc/blob/2666438724c3364795e733f53115af0f7b018770/gcc/jit/libgccjit.h)). rustc_codegen_gcc uses it that way.

**How a compile runs** ([notes.txt](https://github.com/gcc-mirror/gcc/blob/2666438724c3364795e733f53115af0f7b018770/gcc/jit/notes.txt)):
1. It records every API call, creates a temporary directory and takes a mutex.
2. It runs GCC's whole `toplev::main`, middle end and backend included, which writes `fake.s`.
3. An embedded copy of the driver runs `as` and `ld` to make `fake.so`.
4. It `dlopen`s `fake.so`, releases the mutex and returns.

The mutex is one `static std::mutex jit_mutex` for the process ([jit-playback.cc L3133-L3158](https://github.com/gcc-mirror/gcc/blob/2666438724c3364795e733f53115af0f7b018770/gcc/jit/jit-playback.cc#L3133-L3158)), so only one compile runs at a time. The library is built for one target, and building GCC with `--enable-host-shared` for it slows the ordinary compiler "by a few percent" ([docs/internals/index.rst L247](https://github.com/gcc-mirror/gcc/blob/2666438724c3364795e733f53115af0f7b018770/gcc/jit/docs/internals/index.rst#L247)).

**License.** GPLv3 or later ([libgccjit.h](https://github.com/gcc-mirror/gcc/blob/2666438724c3364795e733f53115af0f7b018770/gcc/jit/libgccjit.h)). Linking it into the chibi compiler would put the distributed compiler under the GPL's terms; whether the GCC Runtime Library Exception covers it was asked on GCC's list in 2017 without a clear answer (unverified).

**For chibi.** GCC's code quality, without any of the speed. Its one lesson is what it costs to turn a batch compiler into a library after the fact: a temporary directory, an assembler and a linker as subprocesses, and a global lock.

### 3.7 TPDE

Covered in [fast-compilers.md](fast-compilers.md) 3.8. Unchanged since: "the primary goal is low-latency compilation while maintaining reasonable (`-O0`) code quality", and it "only targets ELF-based x86-64 and AArch64" ([README](https://github.com/tpde2/tpde/blob/050927ee81dc11a9e599fa2524ff0eef16daf51a/README.md)). The core is 14,750 lines of C++20, TPDE-Encodegen 4,894, and TPDE-LLVM, its adaptor for LLVM IR, 9,879: a sign of what an adaptor for chibi's IR would cost. Apache-2.0 with LLVM exception.

**For chibi.** It does the job chibi's own arm64 backend already does, can't write Mach-O, and is a C++ template library. Its design (one analysis pass, one emission pass, spills at joins) is already in the reading list of fast-compilers.md section 9.

### 3.8 MimIR

**What it is.** A higher-order, graph-based IR "rooted in the Calculus of Constructions", for DSL compilers: tensors, automatic differentiation, regular expressions (POPL 2025; the repository moved from AnyDSL/MimIR to [mimir/mimir](https://github.com/mimir/mimir/tree/210dcb3e8b4d6be9cc052b416480069978c04e26)). 44,613 lines of C++23, MIT, about 14,000 commits.

**Code generation.** The `ll` plugin emits LLVM IR ([src/mim/plug/ll](https://github.com/mimir/mimir/tree/210dcb3e8b4d6be9cc052b416480069978c04e26/src/mim/plug/ll)). A native backend is in progress: "An SSA-based native x86-64 backend for MimIR. Instruction selection and register allocation both use the PBQP solver", in 4,151 lines that emit AT&T assembly ([x86/AGENTS.md](https://github.com/mimir/mimir/blob/210dcb3e8b4d6be9cc052b416480069978c04e26/src/mim/plug/x86/AGENTS.md)).

**For chibi.** Not a candidate: it lowers to LLVM, its own backend is x86-64 only and early, and a type-theoretic higher-order IR solves problems chibi doesn't have. The one idea that transfers, folding and normalizing while building the graph, chibi's checker already does for constants.

### 3.9 Tilde (TB)

The backend of Yasser Arguelles Snape's Cuik C compiler: 42,822 lines of C, MIT, with the README warning "unfinished and buggy" ([Cuik README](https://github.com/RealNeGate/Cuik/blob/a7677f74e5391dcc7fdedad61468f4f207c9998f/README.md)). The optimizer is 16,578 lines (GVN, GCM, SCCP, inlining, loop passes), the x86-64 backend 5,335, arm64 2,018, MIPS 2,402 and Wasm 865; it has three register-allocation files and writers for COFF, ELF and Mach-O ([tb](https://github.com/RealNeGate/Cuik/tree/a7677f74e5391dcc7fdedad61468f4f207c9998f/tb)). The last commit is from 2026-07-24, and the history was restarted in March 2025. No compile or code speed is published. Odin removed it in March 2026 and C3 deleted it in 2024 ([fast-compilers.md](fast-compilers.md) 3.11–3.12).

**For chibi.** Too early to depend on, and arm64 is a fifth of the x86-64 code.

### 3.10 libFirm

A graph-based SSA IR in C from KIT, LGPL-2.1, 185,869 lines: 96,724 for the IR and its optimizations, 28,604 shared backend, 9,055 for amd64. Its README says it "includes a complete backend for the IA32 and SPARC architecture, as well as unfinished backends for MIPS, ARM, and AMD64" ([README.md](https://github.com/libfirm/libfirm/blob/114012d1d93427e63ba2f2ab51318e8a1b92f06c/README.md)); there is no AArch64 directory, and the last commit is from 2025-03-04. Its code was good: cparser `-O3` at 0.98 of gcc `-O2` on Makarov's benchmarks ([MIR README L377-L409](https://github.com/vnmakarov/mir/blob/dc2c763f1c8bc746102ce87933d866335f9e6c06/README.md#L377-L409)); which target cparser built for there is not stated.

**For chibi.** No arm64 and no recent work rule it out. Two of its ideas are classics worth knowing: SSA construction straight from the syntax tree (Braun et al., CC 2013, came out of this group and is what Cranelift's frontend uses), and register allocation on SSA form, where spilling and assignment separate because SSA interference graphs are chordal (Hack, Grund and Goos, CC 2006).

### 3.11 B3 (design evidence only)

WebKit replaced LLVM in its top JavaScript tier with B3, a backend written from scratch with two IRs, in January 2016 ([r195562](https://trac.webkit.org/r195562)). The announcement reportedly gave about 5× faster compiles at about the same code quality for their workloads (unverified: the [WebKit post](https://webkit.org/blog/5852/) was read only through search excerpts). It is part of JavaScriptCore and not usable alone. It is earlier evidence that a team can write a backend matching LLVM on its own workload while compiling several times faster, which cppgm repeats for C++.

---

## 4. What each step toward LLVM costs

Code speed against an LLVM- or GCC-class `-O2`, against lines counted for an x86-64 and arm64 build where the project has both. My table, from section 3; the speed sources differ, so treat the steps as rough.

| Backend | Lines | Code speed | Has inlining | Promotes memory | Register allocation |
|---|---|---|---|---|---|
| tcc (no IR) | not counted | 0.50 of gcc `-O2` | no | no | value stack |
| chibi arm64 (development) | 3,334 | 0.54–0.97 of clang `-O2` on four small functions | no | no | by use count |
| QBE 1.3 | 16k | 0.57–0.67 of clang `-O2` | no | yes | spill pass, then linear |
| MIR | 28k | 0.91 of gcc `-O2` (small benchmarks) | yes | yes | priority linear scan with splitting |
| Cranelift | 277k | about 0.87 of LLVM (14% slower) | the mechanism; the caller decides what to inline | the frontend builds SSA from variables | backtracking, with splitting |
| cppgm LowIR | 138k | 0.98–1.00 of clang | yes | yes, with aggregate splitting | its own; not documented |
| LLVM | 1.68M | 1.00 | yes | yes | greedy, with splitting |

The chibi row is from [experiments/arm64-backend](../../experiments/arm64-backend/README.md): `fib`, `sum`, `collatz` and `primes` ran at 1.03–1.86× clang `-O2`'s time, with overflow checks that clang's `fib`, `collatz` and `primes` don't do. Its line count is the whole of `src/arm64.c`.

**Reading it.**
- Up to about 0.65, a backend needs SSA, simple global optimizations and a decent allocator. QBE's 16,000 lines get there.
- From 0.65 to 0.9, the step is inlining and an allocator that splits live ranges. MIR adds both in under twice QBE's code. MIR's figure comes from small benchmarks, where inlining matters less and loops matter more, so it may be generous.
- From 0.9 to 1.0 cost cppgm about five times MIR's code: loop optimizations, vectorization, aggregate splitting, and a long tail of patterns tuned against benchmarks. Its optimizer alone is 54,000 lines.
- LLVM spends twelve times cppgm's code to reach the same place on these benchmarks, because it covers many more targets, languages and program shapes.

---

## 5. Fit with chibi's contracts

| Contract | LLVM | cppgm LowIR | MIR | QBE | Cranelift |
|---|---|---|---|---|---|
| Overflow traps (rank 1) | `*.with.overflow` intrinsics | **no overflow operation**: `binary` has add, sub, mul, div, mod, and, or, xor and shifts, with a `nowrap` flag ([lowir_model.h L780-L787, L885](https://github.com/vishvananda/cppgm/blob/56c86f755ec2b56e235b3f5e5ff76e6d147c3f93/dev/src/lowir_model.h#L780-L787)) | `ADDO`/`MULO` and `BO` | none | `sadd_overflow` and others |
| No type-based aliasing (chibi has no strict aliasing) | none unless the front end emits TBAA | none seen; `noalias` is an opt-in fact on parameters | none seen | none seen; `alias.c` tracks pointer origins | none seen |
| No FMA contraction (rank 3, open) | only with the `contract` flag or `fmuladd` | arm64 contracts only when its `contract_float_` flag is set ([combines.cpp L205](https://github.com/vishvananda/cppgm/blob/56c86f755ec2b56e235b3f5e5ff76e6d147c3f93/dev/src/aarch64/combines.cpp#L205)) | not checked | not checked | not checked |
| Mach-O on the M4 Pro | yes | experimental, untimed | JIT on macOS | assembly for Apple | yes |
| Parallel code generation | per context | **one compile per process** | removed in 2023 | one process per file | per function |
| Builds from shape A's slots | `mem2reg` | slot promotion (MYC does exactly this) | its own SSA pass | SSA construction | the frontend builds SSA |

**Overflow is the gap that matters for cppgm.** With no overflow operation in LowIR, chibi would emit each check as compares and a branch, and whether cppgm's selectors fold those into `jo` on x86-64 or `b.vs` on arm64 is unknown. chibi's checked release builds run these checks on every `+`, `-` and `*`, so this is the first thing a spike should measure.

**One compile per process** costs little in release builds, which can run one process per core and link the objects, as chibi will do with the C backend.

---

## 6. Ideas to borrow

1. **Batch construction, from cppgm's C API.** The producer reserves pools that the library owns, writes typed records in place, and commits once; IDs survive growth, pointers expire at the next reserve or commit ([lowir-library.md L28-L71](https://github.com/vishvananda/cppgm/blob/56c86f755ec2b56e235b3f5e5ff76e6d147c3f93/docs/lowir-library.md#L28-L71)). If chibi ever exposes its IR as a library, for tools or for its own backends in other processes, this is the shape. It is already chibi's rule: "Instead of a per-item call, use a batch."
2. **Overflow as a flag plus a branch, from MIR.** `ADDO` then `BO` maps to `adds` then `b.vs` on arm64 and `add` then `jo` on x86-64, with no extra compare. chibi's IR can keep checked arithmetic as one operation and let each backend choose the sequence; the development backend already does this with flags.
3. **One front IR, several backends, from MYC.** Its LLVM, QBE, C and cppgm translators are 1,154 to 1,844 lines each. A chibi IR translator to any of these is a spike, not a project.
4. **A pass order for chibi's own optimizing backend.** Start where MIR is (SSA from slots, GVN, DCE, LICM, inlining, a linear-scan allocator with live-range splitting, instruction combining after allocation), using QBE's pass order on one page as the outline. Measure each pass's gain on the compiler itself, as Cranelift did with its e-graph, and stop where the gain no longer pays for the compile time.
5. **Tuning against a fixed corpus, from cppgm, as a method and a warning.** cppgm reached parity by measuring the same suites at every commit, with paired A/B runs and A/A calibration ([public.md](https://github.com/vishvananda/cppgm/blob/56c86f755ec2b56e235b3f5e5ff76e6d147c3f93/docs/performance/preview/public.md)). chibi's plan to use the compiler as its own benchmark is the same method. The warning is its README's: code shapes outside the suites can still be slow.
6. **What not to copy, from libgccjit:** a library that writes temporary files and runs subprocesses, and a global lock around the whole compile.

---

## 7. Recommendation

1. **Development builds: no change.** chibi's own one-pass backend already generates code at least a hundred times faster per line than any optimizing backend here, and keeps hot reload. TPDE fills the same role and writes no Mach-O.
2. **Release builds: keep the C backend first.** It is the cheapest route to self-hosting, and it gives clang `-O2` code on every target, including consoles and Wasm. Nothing here changes that.
3. **After the C backend: spike cppgm's LowIR instead of planning for LLVM.** On x86-64, the evidence says it gives clang `-O2` code at a quarter of clang's compile time, from a C API that suits chibi, under Apache-2.0. The spike:
   - translate chibi's IR to LowIR through the C API, giving each slot a LowIR slot as MYC does (about 2,000 lines, from MYC's sizes);
   - build the compiler with it and with the C backend plus clang `-O2`, on x86-64 in the cloud first, which suits the current setup while the M4 Pro is unavailable;
   - measure compile time, the compiler's own speed on the 200-file corpus, and what the overflow checks cost (section 5);
   - adopt it only if it also works and holds up on the M4 Pro, and if one compile per process is acceptable.

   If it fails, LLVM's C API is still there, at about four times the compile time and a much larger dependency.
4. **The no-C-compiler goal still needs chibi's own optimizing backend** eventually, or a library linked in. A library would be cppgm (C++, so it still needs a prebuilt C++ runtime) or LLVM. chibi's own backend should aim at MIR's point, about 0.9 of clang in about 30,000 lines, not at parity, which costs five times the code. Read MIR's `mir-gen.c` first, then QBE.

---

## 8. Questions for the owner

1. **The release backend after the C backend.** Spike cppgm's LowIR (recommended), keep LLVM's C API as the next step, or plan chibi's own optimizing backend straight after the C backend? This changes the Release row of 2026-11-09, which names LLVM.
2. **Whether depending on an LLM-written backend is acceptable at all.** cppgm's 138,000 lines have one author and agents behind them, and it is the only option here that reaches parity. The alternatives are LLVM (large, slow, well tested) and chibi's own backend at about 0.9. Your rule that you must understand every part of chibi applies to chibi's code; whether it extends to a dependency is your call.
3. **How close "good enough" is.** If 0.9 of clang is enough for shipped games, chibi's own MIR-sized backend can be the long-term answer and no external optimizer is needed. If parity matters, a library stays in the picture.

---

## 9. Not verified

- **Nothing was run here.** Every compile and run time is from the sources named. The sandbox did not allow running the downloaded compilers, so I did not reproduce the index-c numbers or time anything on x86-64 myself.
- **Unreachable pages:** the two cppgm blog posts on medium.com; the Hacker News thread on cppgm; QBE's 1.3 release notes on c9x.me (the "closer to 40%" quote is a search excerpt); GCC's online libgccjit docs (I read the same docs in GCC's repository instead); the MYC thread on the Crystal forum (I read MYC's repository instead); the MimIR paper on arxiv.org; WebKit's B3 post (search excerpts only).
- **cppgm's numbers** are its author's, except section 3.2's, which come from a benchmark whose C++ twin cppgm is tuned against. No one has published a timing on Apple Silicon. I did not check how its selectors handle chibi-style overflow checks.
- **MIR's 0.91** comes from 15 small benchmarks in 2023, and libFirm's 0.98 from the same table, target not stated.
- **Cranelift's 14%** comes from WebAssembly benchmarks in 2020, through a Wasm runtime.
- **Line counts** include comments and blank lines and leave out tests. LLVM's 1.68M counts `IR`, `Analysis`, `Transforms`, `CodeGen`, `MC`, `Target/X86` and `Target/AArch64` with their TableGen files, and not `Support`, `Object` or the linker. GCC's 2.42M is Makarov's estimate. tcc's 30k is approximate.
- **libgccjit's license** for programs that link it: whether the GCC Runtime Library Exception applies was not settled in what I could read.
