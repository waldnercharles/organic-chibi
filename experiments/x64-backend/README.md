# The x86-64 development backend

`src/jit/x64_target.h`, `x64_encode.h` and `x64_select.h` are the x86-64 parts of the JIT, built into `src/jit.c` in place of the arm64 parts when `CHIBI_JIT_X64` is defined. Every build still uses the arm64 parts by default until the x86-64 backend compiles everything the arm64 one does (step 3x.7); then the default follows the host.

`make check` builds the backend with `CHIBI_JIT_X64` and runs, on an x86-64 host:
- every function that takes two `int`s and returns one in the backend test programs (`test/backend_programs.c`), over the same grid of inputs as the C backend's test, against the IR reference interpreter, traps included. A program the backend refuses is split into its functions, and each function that checks on its own is tried alone;
- seven programs written for this backend, in `check.c`: loops with more locals than registers, calls with stack arguments and values live across them, recursion, 8- to 64-bit arithmetic and narrowing, checked `u64` products with values live in `rax` and `rdx`, comparisons as values and as branches, expressions deep enough to spill, and pointers to locals.

A function tried alone keeps the top-level declarations that come before the program's first function, such as its enums. At step 3x.2, 106 functions agree and none fail.

`make check ARGS=-v` also names each refused program and the reason.

The debugger checks for both backends are in [experiments/debuggers](../debuggers/README.md).

`make encode` writes 28,933 instructions with the encoder, covering every register pair, the immediates and displacements at each size boundary, the memory forms that need a SIB byte or a zero displacement, and the indexed load of a jump table, then has `llvm-mc` assemble the same text and compares the bytes. The encoder picks the encodings `llvm-mc` picks.

## What the backend does (step 3x.2, 2026-11-26)

- **Registers.** Values use `rax`, `rcx`, `rdx`, `rsi`, `r8` and `r9`. `r10` and `r11` are scratch, `rdi` holds the context, `rbp` the frame and `rsp` the stack. Locals used at least twice take `rbx` and `r12`–`r15`, which calls preserve; a function without calls also gives one local `rdi`.
- **The invariant** is arm64's: a value narrower than 64 bits sits in the low half, sign- or zero-extended to 32 bits, with the high half zero. 32-bit instructions keep it for free.
- **Two-operand instructions.** Where the result register is the right operand of a subtraction, the backend computes in `r11` and moves the result.
- **Overflow** traps with `jo` for signed types and `jb` for unsigned ones. 8- and 16-bit results are compared with their own extension, and a checked `u32` product is computed in 64 bits and tested above bit 31.
- **Fixed registers.** A checked `u64` product is `mul`, which reads `rax` and writes `rdx:rax`. Before it, a value living in `rax` or `rdx` moves to a free value register, or to the stack when none is free; `free_reg` does this, and the division and shifts of step 3x.4 will use it.
- **`switch`** is arm64's: a jump table of 4-byte offsets when the cases are dense (`lea` of the table from `rip`, `movsxd` of the entry, `add`, `jmp`), otherwise a binary search over the case ranges. A conversion to an enum checks membership with the same search.
- **Calls** go through the function table with `call [rip + disp]`. Before a call, live values move to free callee-saved registers or the stack, so arguments move into `rsi`, `rdx`, `rcx`, `r8` and `r9` without overwriting each other; further arguments go to 8-byte stack slots. The context is reloaded from `[rbp - 8]` before any call that a call precedes or a loop surrounds, as on arm64. Nothing in chibi code reads the context yet, so `make check` can't see a wrong one; the calls into C of step 3x.6 can.
- **Frames.** Each function reserves 48 bytes before its body, and the prologue it needs is written at the end of them: `push rbp`, `mov rbp, rsp`, `push rdi` when the context is saved, the callee-saved registers, then `sub rsp`. A frame of 4 KB or more is reserved a page at a time by a stub after the traps. Every `return` jumps to one epilogue, which ends with `leave` and `ret`.
- **Traps** are `ud1 eax, [rax + kind]`, so a `SIGILL` handler reads the kind at the fourth byte.
- **Refused for now,** each with its own message: floats, arrays, structs and slices, division, shifts and bit counts, optionals, errors and tuples, vectors, 128-bit integers, and calls into C.

## Against clang and gcc (2026-11-27)

Measured on the Ryzen 9 9900X, pinned to one core, with clang 22 and gcc 16 under Fedora 44.

**Generated code.** The backend runs only integer code without division or shifts yet, so `make output`'s `collatz` and `primes` can't run here, and `sum` adds `i ^ (i + 7)` in place of `i ^ (i >> 3)`. The release builds are `chibi build`'s: the C backend's output, compiled with `-std=c11 -O2`. chibi checks `+`, `-` and `*` for overflow in debug and release builds and wraps in release-fast builds (the Integer overflow row of 2026-11-10), so its `fib` checks three operations and its `sum` two. The plain C checks only `sum`'s `total +=`, and "C with chibi's checks" checks the same operations as chibi, with `__builtin_add_overflow` and `__builtin_trap`. Best of 7, in ms, across three processes:

| Build | `fib(35)` | `sum(1000000000)` |
|---|---|---|
| chibi, development backend | 29.9–30.9 | 250–251 |
| `chibi build`, clang | 25.9–26.3 | 226–227 |
| `chibi build`, gcc | 25.7–26.2 | 363–379 |
| `chibi build --release-fast`, clang | 15.3–15.6 | 157 |
| `chibi build --release-fast`, gcc | 5.6–5.7 | 221–224 |
| C, clang -O0 | 26.5–27.3 | 971–985 |
| C, clang -O2 | 15.4–15.8 | 195–198 |
| C, gcc -O0 | 26.4–27.3 | 473–484 |
| C, gcc -O2 | 6.4–6.6 | 238–244 |
| C with chibi's checks, clang -O2 | 25.7–26.1 | 240–247 |
| C with chibi's checks, gcc -O2 | 25.4–26.2 | 375–389 |

The checks account for the release builds' distance from C: with the same checks, the C takes as long as `chibi build`, and without them `--release-fast` matches clang -O2 and gcc -O2. gcc's checked `sum` loop runs 14 instructions to clang's 9, from the C backend's output and from the hand-written C alike.

**Compile time,** for the compiler itself: its 13 C files (23,445 lines), one after the other, against the 41,219-line chibi translation that `make translated` writes, through `experiments/arm64-backend/bench.c`. The C times include starting the compiler 13 times, about 113 ms for clang and 48 ms for gcc; chibi's count lexing, parsing, checking and arm64 code generation in memory, without reading files or linking, and spread by about 15% from run to run at this size, more than debug info costs.

| Compiler | Time |
|---|---|
| chibi | 10.0–11.9 ms |
| chibi with debug info | 10.1–11.5 ms |
| clang -fsyntax-only | 309 ms |
| clang -O0 | 491 ms |
| clang -O0 -g | 655 ms |
| clang -O2 | 3,691 ms |
| gcc -fsyntax-only | 650 ms |
| gcc -O0 | 1,715 ms |
| gcc -O2 | 7,377 ms |
