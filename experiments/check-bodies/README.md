# Checker step 2: function bodies and IR

This experiment measures step 2 of the checker ([docs/research/checker-and-ir.md](../../docs/research/checker-and-ir.md) section 8): `src/check.c` checks function bodies and writes the typed IR of section 5, on one thread.

```bash
make run   # generates 200 files into /tmp/check-bodies-corpus, then -O3 -flto -DNDEBUG
```

## The corpus

`gen.py` writes 200 files of 100 functions. Each function has 0–3 parameters and 4–15 top-level statements, with nested blocks up to 4 deep:
- locals, written as `x := e` or `x: T = e`;
- assignments, including `+=`, `-=`, `*=`, `|=` and `^=`, to locals only, because parameters and `for` variables are read-only (2026-11-14);
- `if` and `else`, `for` over a range, and `while` with a counter;
- `break` and `continue` inside loops;
- calls to any function in any file, inside expressions or discarded with `_ = f()`.

Since 2026-11-14, a statement that would assign to a parameter or a `for` variable becomes `_ = e` or `_ = x op (e)` instead, so the random choices, and therefore the program's shape, stay the same.

Expressions mix `int`, `i32`, `u8`, `u32`, `u64`, `f32`, `f64` and `bool`, with literals, conversions such as `f32(x)`, arithmetic, bitwise operators, comparisons, `and`, `or` and `!`. Every program type-checks: the checker reports no diagnostics.

| Measure | Value |
|---|---|
| Lines | 694,816 |
| Nodes | 4,375,610 (6.30 per line); 4,210,612 (6.06) before the `_ =` change |
| Functions | 20,000 |
| IR instructions | 4.02 per line; 4.23 before `and` and `or` carried their value in an `if` ([logical-lowering](../logical-lowering/README.md)) |
| IR bytes | 68.6 per line (71.2 before): 13 per instruction, plus call arguments, slots and constants |
| Diagnostics | 0 |

Lines are denser than typical code (6.06 nodes per line, against 2.70 in the declarations corpus), so cost per node is the fairer comparison.

## Result

Timing and core selection work as in [check-decls](../check-decls/README.md): the timed part is `check_init`, `check_package` and `check_free`, and the median of 11 runs is reported, at a load average of about 5.

| Version | Instructions per node | Cycles per node | Cycles per line |
|---|---|---|---|
| First version: each body built in a buffer, then copied into the package's store | 153 | 50 | 299–306 |
| Bodies written straight into the store, which is reserved from the node count | 142 | 41.5 | 251–254 |
| Read-only parameters and `for` variables, and `_ =` for discarded calls (2026-11-14) | 141 | 40–41 | 251–257 |

Writing directly saved 13–15%: the copy moved about 50 MB of IR per run, and the store also grew by reallocation. A body checked out of order still uses a separate buffer, because the body that called it is still being written. Such a body is checked out of order only when a call needs a return type that the callee's body infers.

H5 allows 3,000 cycles per line for all of checking. Step 2 uses about 250, roughly 8%.

Since 2026-11-14 the benchmark also reports lines per second, from the wall-clock time of the same timed part: 16.4 million lines per second on one core, which ran at 4.08 GHz. For comparison, `make bench` reports 15.5 million lines per second for the lexer and parser together on one thread, on its own generated programs.

## Profile

`sample` on a build with `-fno-inline-functions`, after the copy was removed:
- the node loop (`walk`) and the IR writes (`emit`) dominate;
- the value stack (`push`, `pop`, `list_start`) comes next;
- float literals cost about 4%: each one is parsed to a fraction, reduced with a gcd, rounded to `f32` or `f64`, and turned back into an exact value. Values that never need folding could skip the last two steps.

## Effect on the declarations corpus

Step 2 also checks the bodies in [check-decls](../check-decls/README.md), whose functions are empty apart from `while true {}` in those that declare a return type. There, the checker went from 80 to 126 instructions per node (122–132 to 155–166 cycles per line), run back to back with a rebuilt step-1 checker on the same corpus:
- about 17 instructions per node come from exact constants and the token stack (97 instructions per node with body checking turned off);
- the rest is about 940 instructions for each of the 45,000 near-empty bodies: the body's setup, its parameters, its structure and its function record.
