# The first arm64 backend

`src/arm64.c` compiles the step-2 IR to arm64 machine code in one pass, writing straight into executable memory. The owner moved this ahead of checker step 3 on 2026-11-14 ([decisions.md](../../docs/decisions.md), Compiler). This experiment measures what it costs, from source text to machine code, on the corpus from [check-bodies](../check-bodies/README.md).

`make run` generates the corpus, then runs the four phases 11 times and reports the median of each:
1. lexing and parsing;
2. checking;
3. code generation;
4. all four together.

`make output` times the generated code against clang. `dump.c` compiles one `.chibi` file and prints each function's machine code as bytes for `llvm-mc --disassemble -triple=aarch64`.

## What the backend does

- **Locals** live in registers when they are used enough, chosen by use counts before the function is emitted (2026-11-15):
  - The checker counts each load and store of a local, weighted ×8 for each enclosing loop, and counts each function's calls.
  - In a function without calls, parameters stay in their argument registers, and other locals take the free `x0`–`x7` and `v0`–`v7`, then `x19`–`x28` and `d8`–`d15`.
  - In a function with calls, locals used at least twice take `x19`–`x28` and `d8`–`d15`, which calls preserve.
  - When there are more such locals than registers, the heaviest win. The rest live in stack slots at fixed offsets from `sp`, grouped by size, and locals never used get no slot.
  - Parameters of 32 bits or less are zero- or sign-extended on entry, because C callers may leave the upper bits set.
- **Loads of a register-held local** make no copy: the value names the local's register. A store to the local while such a value is still live copies the value out first. The checker never emits that order today; one test makes it by reordering IR.
- **Pointers** (step 3d.1, 2026-11-19): a local whose address is taken stays in its stack slot for the whole function, and `&x` is one `add` from `sp` (two beyond 4 KB). `p.*` loads and stores with the pointee's size and sign.
- **Arrays** (step 3d.2, 2026-11-19) live in the frame after the scalar slots, largest alignment first, and never in registers. `index` with a constant is one `add`; otherwise the index is sign- or zero-extended, compared unsigned with the length, and scaled with `add ..., lsl #k`, or with `madd` when the element size is not a power of two. A failed bounds check is `brk #6`. `copy` and `zero` are unrolled up to 64 bytes with `q` registers and `stp xzr`, and loop over 32-byte chunks beyond that.
- **Structs** (step 3d.4, 2026-11-19) live in the frame as arrays do, with C's layout. `field` adds the field's offset to the struct's address with one `add`; folding it into the load or store that follows is not done yet. `copy` and `zero` work on structs as on arrays.
- **Slices** (step 3d.3, 2026-11-19) are two values, a pointer and a length, each in its own register, and a slice local is two slots that can each be pinned to a register. A slice parameter takes two integer argument registers, and a slice result comes back in `x0` and `x1`, as C passes a 16-byte `struct { T *ptr; int64_t len; }`. `index-len` compares the index with the length, unsigned. `slice` compares `hi` with the length and `lo` with `hi`, unsigned, and skips a compare whose two sides are constants or whose lower side is 0. A failed check is `brk #6`. When the result goes straight into a local or a return, each half is computed into that register, unless a live value still names it.
- **Stack probes:** a frame of 4 KB or more is reserved one page at a time by a stub after the traps, which stores zero at each new page, so a large frame cannot step over the guard page. The prologue branches to the stub and the stub branches back. A frame of 16 MB or more is an error.
- **Values** each get a register when they are made, and free it at their one use. Integer values use `x9`–`x15` and floats `v16`–`v28`. When none is free, the oldest value is spilled to the stack, because it will be used last. A value about to be stored to a register-held local, returned, or passed as the last argument of a call is computed straight into that register.
- **Calls** go through a table of function addresses at the start of the mapping, using `adrp`, `ldr` and `blr`. Replacing one entry redirects every call to that function, and a test does so while the program is loaded. Values still needed after a call move to free callee-saved registers, and are spilled only when none is left. Calling directly with `bl` instead measured the same on `fib` (7.7 against 7.8 ms), so the table stays.
- **The context** (step 3d.9, 2026-11-20) arrives in `x0`, and integer arguments start at `x1`. A function passes `x0` unchanged to a call that no call precedes in the code and no loop surrounds. Before any other call it reloads `x0` with `ldr x0, [x29, #16]`, and a function that reloads pushes a 32-byte frame record and stores `x0` there in its prologue. A function without calls uses `x0` as one more free register.
- **Calls into C** (step 3h.1, 2026-11-20) follow C's convention: no context, and integer arguments from `x0`. A C result narrower than 64 bits is put into the form chibi keeps in registers, as parameters are on entry, because C may leave the upper bits set: below 32 bits it is sign- or zero-extended to 32, and at 32 bits the upper half is cleared. The first call to an `extern fn` that the backend compiles looks its name up with `dlsym` in the running process and writes the address into the function table, so later calls and a declaration that nothing calls cost no lookup. A call into C never reloads the context, and a chibi call after one reloads it, because C doesn't keep `x0`.
- **Frames:** each function reserves 13 words before its body. When the body is done, the prologue it needs is written at the end of those words and the function's entry skips the rest. A function without calls, stack slots or callee-saved registers has no prologue. Every `return` jumps to one shared epilogue.
- **Comparisons** that feed the next `if` become a compare and a conditional branch, with no `bool` in a register. `x % 2^k == 0` and `x & c == 0` become one `tst`.
- **Constants** become immediates where arm64 has them: 12-bit and shifted 12-bit for `+`, `-` and comparisons, negated as `sub` or `cmn` where the flags still mean the same, and bitmask immediates for `&`, `|` and `^`. A constant on the left of a commutative operator or a comparison moves to the right.
- **Division by a constant** uses shifts and masks for powers of two, and a multiply by a "magic number" otherwise (Hacker's Delight, chapter 10), as clang does. On the M4 Pro a magic-number division of 64-bit integers ran 1.8× the throughput of `sdiv` and cut latency from about 9 cycles to 6. Dividing by a constant other than 0 and -1 can't trap, so those checks go too.
- **Every `block`, `loop` and `if`** spills the live values first. At statement level none are live, so this costs nothing; inside an expression it keeps every value in the same place on both paths. An `if` that carries a value picks its result register when its first arm ends.
- **An `if` on a constant** emits only the arm that runs. The checker uses such an `if` to type-check the right side of `false and f()` without running it.
- **Loop heads** get one `nop` and are then padded to 16 bytes; "Speed of the generated code" explains why.
- **Checks** follow the decisions.md overflow row. `+`, `-`, `*` and negation use flags or a range check. Division checks for a zero divisor and for the most negative value divided by -1, a shift checks its count, and narrowing checks the range, including float to integer and NaN. A range check on an 8-, 16- or 32-bit result, or on narrowing between integers, is one compare against the value's own extension (`cmp w0, w0, sxtb`) or one `tst`. A failed check branches to `brk #kind` at the end of the function, so a debugger or signal handler can read which check failed.

**Limits:**
- arm64 only;
- at most 7 integer arguments after the context, and 8 float arguments; a call into C takes at most 8 integer and 8 float arguments, and none after C's `...`;
- a local keeps one register for the whole function, so a function with more hot locals than registers keeps the lighter ones in memory even where they are hot;
- values are integers, floats, `bool`, enums, pointers, structs, and arrays and slices of those. An optional or error union is rejected with an error, and so is a struct or array passed or returned by value. A slice argument counts as two integer arguments.

## Prediction and result

Predicted before building ([checker-and-ir.md](../../docs/research/checker-and-ir.md) section 8, step 2b):
- about 40 cycles per IR instruction (25–60);
- about 3 arm64 instructions per IR instruction;
- about 670 cycles per line for all four phases, 6 million lines per second.

| Version | Cycles per IR instruction | Instructions per IR instruction | IPC | Code generation, M lines/s | All four, cycles per line | All four, M lines/s |
|---|---|---|---|---|---|---|
| First version | 70.6–73.8 | 208 | 2.82 | 13.5–14.1 | 757 | 5.25 |
| Type facts from a table, register tracking by bit masks | 52.9–56.2 | 135 | 2.4–2.6 | 17.8–18.7 | 683–685 | 5.8–5.9 |
| Loop heads padded, arms of constant `if`s skipped, review fixes (2026-11-15) | 51.7 | 129 | 2.5 | 19.9 | 653 | 6.3 |
| Locals in registers, constant division, immediates (2026-11-15) | 55.5–57.2 | 140–143 | 2.5–2.6 | 17.1–18.5 | 667–692 | 5.7–6.1 |

The last version emits 3,506,602 arm64 instructions for the same IR, 1.26 for each IR instruction, 22% fewer than before. Code generation costs 7–10% more: choosing registers and tracking which values name a local's register cost more than the loads and stores they remove. The first cut cost 33% more; a profile found two causes:
- Picking registers searched all the function's locals once per register. It now assigns them in one pass when they fit, and otherwise keeps the heaviest in one pass.
- Removing a value from the list of values that name a local's register searched the list from the front. The independent review below found this quadratic; it now takes constant time.

The first two versions emit the same 4,521,042 arm64 instructions for 2,790,514 IR instructions, 1.62 each. That is fewer than predicted, because loads, stores and comparisons fused with branches take one instruction each.

Breakdown of the second version, per line:

| Phase | Cycles per line | M lines per second |
|---|---|---|
| Lexing and parsing | 204–210 | 18.8–19.5 |
| Checking | 255–258 | 15.3–15.7 |
| arm64 code generation | 213–226 | 17.8–18.7 |
| All four | 683–685 | 5.8–5.9 |

At 1M lines per second, one core has about 4,000 cycles per line. These four phases use 17% of that.

## Why the first version missed

The first version ran 208 instructions per IR instruction to write 1.6. A profile with inlining off showed overhead, not code generation:
- **Saving registers before each call** walked all 64 register slots. It now walks only the occupied registers, from bit masks.
- **Every type question** (is it a float, how wide, is it signed) took three dependent loads through the checker's type tables, several times per operand. One byte of facts per type, built once per compile, replaced them. Each value also records its byte when it gets a register.
- **Frame layout** made four passes over the slots; it now makes two.
- **Each write of an instruction** stored and reloaded the output position, because C lets a `u32` store change a `u32` field. The position is now a pointer.

**What remains:**
- Page faults cost about 3 of the 37 ms: 1,114 first touches of 16 KB pages, at 2.3–2.8 µs each for JIT memory, against 1.0–1.7 µs for ordinary memory.
- Flushing the instruction cache and unmapping cost about 4%.
- The rest is the dispatch on each IR instruction's kind and type, at an IPC of about 2.5. Every IR instruction pays for at least one hard-to-predict branch.

## Speed of the generated code

`make output` compiles four functions with the backend and times them against the same functions compiled by clang at `-O0` and `-O2` (`reference.c`):
- `fib(32)`, recursive;
- `sum(100000000)`, a checked addition in a loop;
- `collatz(300000)`, which counts steps with `%`, `/` and `*`;
- `primes(2000000)`, trial division.

The C versions don't check for overflow, except in `sum`. Best of 3 runs, in ms, with the range over three sessions (2026-11-15):

| Function | chibi, locals in memory | chibi, locals in registers | clang -O0 | clang -O2 | now / -O0 | now / -O2 |
|---|---|---|---|---|---|---|
| fib | 8.0 | 6.9–7.0 | 7.7–8.2 | 3.7–3.9 | 0.86–0.91 | 1.81–1.83 |
| sum | 58.3 | 26.1–28.3 | 51.9–67.3 | 23.5–25.0 | 0.42–0.52 | 1.08–1.20 |
| collatz | 86.7 | 45.6–46.5 | 88.3–89.9 | 24.8–25.3 | 0.51–0.52 | 1.84–1.86 |
| primes | 124.0 | 112.9–116.0 | 113.1–113.7 | 107.5–110.0 | 0.99–1.02 | 1.03–1.08 |

- `sum` runs one iteration per cycle, which is the limit for a loop with one taken branch. Rotating the loop to test at the bottom would save one instruction but not that branch, so it isn't done.
- `collatz` gained most from `x % 2 == 0` becoming `tst` and `x / 2` becoming two shifts. What remains is the overflow check on `3 * x + 1` and `steps`, and clang's `-O2` branchless loop.
- `primes` spends its time in `sdiv` by a variable, as clang's code does.
- `fib` is within 10% of `-O0`; clang `-O2` turns one of the two recursive calls into a loop.

Before loop heads were padded, `collatz` took 146 ms and `primes` 270 ms. On the M4 Pro, a loop whose first instruction loads a slot that the instruction just before the loop stored ran at half speed. The cost covers the whole loop, not just its first iteration. Clang's `-O0` code avoids it by chance: it puts a `b` to the next instruction before each loop. Removing that one `b` made clang's own `primes` 2.2× slower.

The cause was found by elimination. The overflow checks, the trap stubs, the frame setup, the registers, the slot offsets and the inner loop's instructions each made no difference. Changing the instruction before the loop head did:
- Any one instruction between the store and the loop head was enough to fix it.
- So was storing a different slot last.

Alignment alone did not fix it, but helped about 10% once the store was separated. The backend now emits one `nop` before each loop head, then pads to 16 bytes. This costs 3.3% more code and no measurable compile time. Why the core behaves this way isn't known; macOS doesn't expose the counters that would show it without root.

The `and`/`or` choice was also re-timed on the backend's own code. Two loops that test `and` and `or` in conditions and assignments ran 21% and 27% faster with the `if` that carries a value than with the hidden local.

Before locals moved to registers, the remaining gap to `-O2` came mostly from locals living in stack slots: every use loaded, and every assignment stored.

## Review, 2026-11-15

An independent review ran inputs against the backend and confirmed four bugs that the tests missed. Each is now fixed, with a test that fails on the committed version:
- **A function returning `!void`.** Its `ret` has no operand, but the backend loaded "value 0" and released a stale spill slot, which a later spill reused while it was live. Non-scalar types are now rejected.
- **Pointers, optionals and slices** were cut to the byte width read from their element type's id. They are now rejected too.
- **`false and g()` with a value live across it.** The skipped arm's call marked outer values as spilled, but the stores sat in code that never runs. Constant arms are now skipped, and every block, loop and `if` spills live values on entry. The checker now also drops the skipped `g()` value, which nothing used before, so the IR keeps its rule that every value is used once.
- **Seven nested `and`/`or`** ran out of registers, because each level held its result register through both arms. The register is now chosen when the first arm ends, so nesting has no limit.

Suspected and left for step 3, where they become reachable:
- Narrow integer and `bool` results returned by C functions may have unspecified upper bits on Linux. Calls to C will extend them after the call. Built in step 3h.1.
- Frames larger than a page need stack probes, as clang emits through `___chkstk_darwin`. That arrives with arrays on the stack. Built in step 3d.2.

## Tests added on 2026-11-15

- **A differential test** compiles random well-typed programs (`test/random/program.c`) and runs each entry function on random and edge-case arguments, both natively and on a reference interpreter of the IR (`test/ir_reference.c`). Results and trap kinds must match. Each program has six helper functions at three call depths and four entry functions, with locals of every scalar type, `if`, `while`, `for`, `break`, `continue`, early `return`, every operator, conversions, masks and calls.
  - `make test` runs 150 programs. `CHIBI_DIFFERENTIAL=<count>` and `CHIBI_DIFFERENTIAL_SEED=<n>` scale it up; 1.8 million programs passed across the changes of this round.
  - Bugs planted to check it were all found: a wrong condition code, a missing overflow check on `i8` negation (after 5,000 programs), and a wrong mask in the `tst` fusion.
  - About 40% of runs finish without a trap; the rest stop at overflow (30%), range (23%), division and shift checks.
- **Constant division** runs every integer type against 54 divisors and 160 dividends each, edges included, and compares with C. It found each of three planted bugs.
- **Store while loaded** reorders the IR of a small program to store to a register-held local while an earlier load of it is live.
- **Narrow parameters** call functions taking `u32`, `u8`, `i8` and `bool` with the upper bits of the argument register set, as C callers may.

## Review, 2026-11-15, second round

An independent review of this round ran 400,000 more random programs and 2,000 from its own generator, aimed at deep nesting, many parameters and locals, frames over 32 KB and running out of callee-saved registers, with no wrong result. It checked the bitmask-immediate encoder against the architecture's decoder over every valid encoding. It confirmed two problems, now fixed with a test or a measurement:
- **Parameters of 32 bits or less kept the caller's upper bits,** which a C caller may leave set. Division by a constant reads all 64 bits, so `t(0x1_0000_0009)` with a `u32` parameter returned 1431655768 instead of 3. The first commit stored parameters to memory and reloaded them, which cleared those bits. Parameters are now extended on entry.
- **Releasing a value that names a local's register searched a list,** quadratic in the nesting of calls: 19.2 ms to generate 80,000 IR instructions of calls nested 250 deep, against 1.55 ms before. It now takes constant time, and the same input takes 1.1 ms.

It also listed code that only checker guarantees keep safe; the return hint now also checks the type, and unused results of the bitmask encoder are asserted.

## Not measured

- x86-64.
- Real programs: the corpus is generated, and its random calls between functions would recurse without end if run.
