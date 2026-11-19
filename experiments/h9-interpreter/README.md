# H9: a compile-time interpreter over bytes

This experiment tests hypothesis H9 in [docs/hypotheses.md](../../docs/hypotheses.md): an interpreter over a typed, flat IR, which stores values as plain bytes, runs at least 10⁸ simple IR operations per second on one core.

It is a throwaway program for measurement, not compiler code.

```bash
make check   # sanitizers, small inputs: results, op counts and rejections
make run     # -O3, full inputs: the timing table
```

Both targets build into the repository's `build/` folder.

## Result

**H9 passes, by about 7×.** These are medians of 5 runs on the owner's M4 Pro, measured on 2026-11-09. Three runs agreed within about 5%.

| Program | What it exercises | Ops | switch: Mops/s | switch: cycles/op | threaded: Mops/s | threaded: cycles/op |
|---|---|---|---|---|---|---|
| count | add, compare, loop | 400,000,001 | 1,306–1,326 | 3.0 | 1,539–1,580 | 2.5 |
| calls | a call and return per iteration | 90,000,001 | 1,231–1,297 | 3.1 | 1,141–1,194 | 3.4 |
| table | byte stores and loads into the heap | 100,663,937 | 1,478–1,489 | 2.7 | 1,647–1,679 | 2.4 |
| fib | deep recursion, `fib(32)` | 42,294,927 | 706–728 | 5.6 | 911–943 | 4.3 |

The slowest row runs at 706 Mops/s, against a threshold of 100 Mops/s.

**The numbers are a best case.** Each program is a loop of 4 to 18 instructions. The branch predictor learns the order of dispatches in such a small loop, so nearly every dispatch is predicted correctly. The instructions-per-cycle column shows this: 4 to 6.7. Real compile-time code has larger bodies and less regular control flow, so expect more cycles per operation. Run the test again once real compile-time code exists (C1).

## How it works

### The IR

An instruction is 8 bytes: an opcode and three 16-bit operands.

```c
typedef struct { u8 op; u16 dst; u16 a; u16 b; } Inst;
```

- **Operands are byte offsets into the current frame.** A frame is a block of bytes, not an array of tagged values. `ADD_I64 dst a b` reads 8 bytes at `frame + a` and 8 bytes at `frame + b`, adds them, and writes 8 bytes at `frame + dst`.
- **The type is part of the opcode** (`ADD_I64`, `LT_I64`, `LOAD_U8`). The interpreter never checks what kind a value is at run time; the checker already decided that.
- **Values are stored in the target's layout.** An `i64` is 8 little-endian bytes, and a `bool` is 1 byte. So a table that compile-time code fills is already the bytes the binary needs.
- **Constants** are copied into the frame when a function is entered.
- **Calls use a sliding window:**
  - The caller writes the arguments at offset `a` in its own frame. That position becomes the callee's frame.
  - The callee returns its result in its slot 0, which is the caller's slot `a`.
  - Nothing is copied except the constants.

### Checks: once at load, or on every operation

`validate()` checks each function once, before it runs:
- every operand lies inside the frame;
- every jump target lies inside the function;
- every call names a real function.

The interpreter therefore never checks frame offsets.

Some checks can only happen while the code runs, because they depend on values. Compile-time code is user input, so a mistake in it must become a diagnostic, not a crash of the compiler. These checks stay in the interpreter, and the rejection tests cover each one:

| Check | Where | Status |
|---|---|---|
| Heap address in range | `LOAD_U8`, `STORE_U8` | `STATUS_OUT_OF_BOUNDS` |
| Step budget | backward jumps (`LOOP_IF`) and calls | `STATUS_OUT_OF_BUDGET` |
| Call depth and stack space | `CALL` | `STATUS_STACK_OVERFLOW` |

The budget is checked only on backward jumps and calls, because only those can repeat work without end. The compiler knows which jumps go backward, so it emits `LOOP_IF` for them and plain `JUMP` or `JUMP_UNLESS` for the others.

### Two dispatch loops

"Dispatch" is how the interpreter gets from one instruction to the next.

- **switch:** a loop around a `switch` on the opcode. The compiler turns it into one indirect jump through a table, shared by every instruction.
- **threaded:** each instruction's code ends with its own jump to the next instruction's code: `goto *targets[op]`, a clang and GCC extension called computed goto. Each instruction then has its own indirect jump, so the branch predictor can learn which instruction usually follows which. CPython's main loop works the same way when the compiler supports it.

Threaded dispatch was faster on three of the four programs, with 16–25% fewer cycles per operation. On `calls` it was 8% slower. On 2026-11-12 that row turned out to depend on code layout (see the tail-call section).

`interpret.inc` holds the interpreter once. `h9.c` includes it four times:
- switch and threaded dispatch;
- each with and without an operation counter.

The counting versions give the exact number of operations. The timed versions don't count, so counting adds nothing to their hot loop. Both dispatch styles must agree on every result and on the operation count.

## Tail-call dispatch (2026-11-12)

`tail.inc` adds a third dispatch loop:
- Each instruction is its own function, marked `preserve_none`.
- Each function ends by calling the next instruction's function with a guaranteed tail call (`[[clang::musttail]]`).
- The interpreter state travels in the arguments, so it stays in registers.
- On arm64 each handler compiles to straight-line code ending in `br x0`, with no stack frame.

A fourth loop, `tail+slot`, looks the handler up through a second, writable table, the way a development build would call through hot-reload slots. `h9.c` includes `tail.inc` twice, once for each table. Both tail loops pass the rejection tests. The op-count check still covers only the switch and threaded loops.

The table shows cycles per operation on the owner's M4 Pro with Apple clang 21, measured on 2026-11-12. Each range covers 3 runs of 5 timed repetitions each, in two builds where both included the loop. Other sessions kept the load average at 10–13 during these runs. The counters are per process, so load affects them less than it affects wall time.

| Program | switch | threaded | tail | tail+slot |
|---|---|---|---|---|
| count | 3.01–3.14 | 2.48–2.57 | 2.35–2.46 | 2.81–2.95 |
| calls | 3.10–3.62 | 3.13–4.86 | 3.02–4.10 | 3.88–4.27 |
| table | 2.71–2.86 | 2.42–2.56 | 2.34–2.40 | 2.68–2.87 |
| fib | 5.65–6.34 | 4.29–4.78 | 3.59–3.94 | 3.92–4.18 |

- **Tail calls were fastest on `count`, `table` and `fib`.** They took about 5% fewer cycles than threaded dispatch on the two loops and about 17% fewer on `fib`, comparing the middles of the ranges. Against switch dispatch they took 15–37% fewer.
- **`calls` depends on code layout, whatever the dispatch.**
  - Adding `tail.inc` to the binary changed none of `interpret_threaded`'s source, and its instruction count per operation stayed at 16.7.
  - Its `calls` row still went from 3.49–3.51 cycles per operation (IPC 4.8) to 4.63–4.86 (IPC 3.5).
  - Adding the second tail loop moved it back to 3.13–3.53, and moved `tail` from 3.02–3.06 to 3.33–4.10.
- **The slot table costs about 3 instructions and 0.1–0.6 cycles per operation.** The extra instructions are a dependent load plus forming the second table's address.

## What to try next

- **C1:** build the lexer's real tables (`kind2`, 131,072 bytes) with this interpreter, and check that it takes under 10 ms.
- **Larger, less regular programs,** to measure the cost of wrong branch predictions without the best-case loops.
- **Fused instructions:** `LT_I64` followed by `LOOP_IF` is the most common pair, and could become one instruction.
- **Tail-call dispatch on real code:** repeat the table above once compile-time code exists. Pass the handler table in a register, so the handlers stop forming its address.
- **A JIT tier:** compile hot compile-time functions with the debug backend. That needs the IR to be shared with the backend.
