# The x86-64 development backend

`src/jit/x64_target.h`, `x64_encode.h` and `x64_select.h` are the x86-64 parts of the JIT, built into `src/jit.c` in place of the arm64 parts when `CHIBI_JIT_X64` is defined. Every build still uses the arm64 parts by default until the x86-64 backend compiles everything the arm64 one does (step 3x.7); then the default follows the host.

`make check` builds the backend with `CHIBI_JIT_X64` and runs, on an x86-64 host:
- every function that takes two `int`s and returns one in the backend test programs (`test/backend_programs.c`), over the same grid of inputs as the C backend's test, against the IR reference interpreter, traps included. A program the backend refuses is split into its functions, and each function that checks on its own is tried alone;
- seven programs written for this backend, in `check.c`: loops with more locals than registers, calls with stack arguments and values live across them, recursion, 8- to 64-bit arithmetic and narrowing, checked `u64` products with values live in `rax` and `rdx`, comparisons as values and as branches, expressions deep enough to spill, and pointers to locals.

A function tried alone keeps the top-level declarations that come before the program's first function, such as its enums. At step 3x.2, 106 functions agree and none fail.

`make check ARGS=-v` also names each refused program and the reason.

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
