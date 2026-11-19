# How `and` and `or` are lowered

The owner asked to time the options of [checker-and-ir.md](../../docs/research/checker-and-ir.md) section 5.6 rather than choose by argument (2026-11-14). This experiment measures three things:
1. the checker's time;
2. the size of the IR it writes;
3. the speed of the machine code a one-pass backend would emit for each form.

The forms:
- **A.** A hidden `bool` local: store `a`, `if` on it, store `b` inside the branch, load the result. Step 2 built this. `check_a.c` is that checker, kept whole so both versions build side by side.
- **B.** An `if` that carries a value: `if bool %a`, then `else %b` and `end false`, where the `end` is the result. `src/check.c` now builds this.
- **C.** Branches, in conditions only: `if a { if b { … } }`. Only the machine code is measured. The checker would need parser support first (a marker before each logical operator, so it knows the condition's shape before reading `a`), and a conditional `exit`.

`make run` generates both corpora, builds the benchmark with each checker, runs each three times, then runs the machine-code benchmark. Each run reports the median of 11. All numbers are from the owner's M4 Pro, at about 4.0 GHz.

## The checker

Two corpora:
- **The step-2 corpus** from [check-bodies](../check-bodies/README.md): 694,816 lines, with an `and` or `or` every 12 lines.
- **The `and`/`or` corpus** from `gen_dense.py`: 599,246 lines, with 202,186 `and`s and `or`s, one every 3 lines. The operators appear in `if` and `while` conditions and in assignments, with two or three operands each.

For comparison, the old Chibi's 932,000 lines of C use `&&` or `||` once every 36 lines.

| Corpus | Form | Cycles per line | Instructions per node | IR instructions per line | IR bytes per line |
|---|---|---|---|---|---|
| step 2 | A | 245–248 | 140.5 | 4.23 | 71.2 |
| step 2 | B | 245–254 | 138.8 | 4.02 | 68.6 |
| `and`/`or` | A | 135–143 | 147.9 | 5.45 | 83.8 |
| `and`/`or` | B | 131–141 | 137.2 | 4.27 | 69.8 |

The ranges cover six runs: three with the Makefile's flags and three with `-falign-functions=64`. The first B run on the step-2 corpus, at 254, is the only one above 249.

- **Time doesn't separate them.** The ranges overlap on both corpora. Differences of 2–3% move with code layout alone (section 6.1 of the design sheet).
- **B executes fewer instructions.** It runs about 170 fewer instructions for each operator: no slot, two fewer stores and two fewer loads. The checker's IPC is high enough (3.5 to 5.8) that these cheap instructions cost few cycles.
- **B's IR is smaller.** It is 3.7% smaller on the step-2 corpus and 17% smaller on the `and`/`or` corpus. Every backend reads 3 instructions for each operator instead of 6 or 7.

## The machine code

`runtime.S` holds the code a one-pass backend would emit for `if x < n and y < m { count += 1 }`, with every local in a frame slot, as section 5.2 plans for the development backend. `runtime.c` runs each version over 262,144 pairs of inputs, 40 times. It reports cycles for each statement, including the shared loads of the inputs and the loop.
- **A** writes the `bool` to its slot and reads it back twice.
- **B** keeps the result in a register: `cset` for `b`, `mov` into the result register, and one `cbz` at the end.
- **B'** is B from a backend that fuses a comparison with the `if` that uses it, as Liftoff does.
- **C** is two compares, each followed by a branch.

Cycles for each statement; the ranges cover three sessions:

| Form | Both true (predictable) | 90% true each | 50% true each (unpredictable) |
|---|---|---|---|
| A, hidden local | 4.21–4.26 | 6.0–7.9 | 24.6–26.3 |
| B, `if` with a value | 2.94–2.96 | 5.1–6.8 | 20.7–22.3 |
| B', B with fused compare | 2.92–2.94 | 5.4–6.3 | 20.0–21.2 |
| C, branches | 2.96–2.97 | 6.3–7.4 | 21.0–21.4 |

- **A is the slowest everywhere.** It is 43% slower than B when the branches are predictable, and 12–20% slower when they aren't. Its store and reload sit between the comparison and the branch.
- **C is no faster than B on this machine.** It saves instructions, but B's extra `cset`, `mov` and `cbz` seem to run in parallel with the loads. With 90% true inputs C was slower than B, probably because it has two branches that can mispredict where B has one. That is not verified: macOS doesn't expose the misprediction counter without root.
- Only arm64 is measured. x86-64 under Rosetta or QEMU doesn't give meaningful timings.

## Result

B is at least as fast as A in every measurement, and smaller. C would cost parser and IR changes, and multi-level exits for the GPU writers, for no measured gain. `src/check.c` therefore uses B. Copying `check_a.c` over it restores A.
