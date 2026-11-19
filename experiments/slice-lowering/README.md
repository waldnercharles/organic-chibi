# Slices in the IR: memory only, or two registers

Question 14 of [checker-and-ir.md](../../docs/research/checker-and-ir.md) section 9 asks how structs, arrays and slices live in the IR. The owner chose option C on 2026-11-15, on the condition that A and C are first prototyped on a slice loop and timed, as `and` and `or` were ([decisions.md](../../docs/decisions.md), Language). Step 3d isn't built, so the prototype is machine code written by hand as a one-pass backend would emit it, the method of [logical-lowering](../logical-lowering/README.md).

The forms:
- **A. Memory only.** A slice lives in a 16-byte slot. `xs[i]` loads the pointer and the length, checks the index, then loads the element. `rest = rest[1..]` loads both halves, checks, and stores both. A slice parameter arrives as a pointer to the caller's copy, as option A states, and the caller copies a local slice before each call (rank 8's recommended rule, which the development backend always applies). For calls only, a second A form passes the two halves in registers, as C's calling convention does, and the callee stores them to its slot.
- **C. Two registers.** A slice is a pair of registers, chosen by use counts like any other local (decisions.md, Compiler, 2026-11-15). A slice argument travels in two registers, which is also how C passes a 16-byte struct of two integers, on arm64 and on x86-64.

Both forms check every index and slice (the bounds-check row of 2026-11-15) and every `+`, pad loop heads, call through the slot table and put their traps at the end of the function. They start from what `src/arm64.c` emits for the same loops over scalars, dumped with [arm64-backend/dump.c](../arm64-backend/dump.c).

## The kernels

```
fn sum(xs: []int) -> int {
    total := 0
    for i in 0..xs.len {
        total += xs[i]
    }
    return total
}

fn count(s: []u8, c: u8) -> int {
    n := 0
    rest := s
    while rest.len > 0 {
        if rest[0] == c {
            n += 1
        }
        rest = rest[1..]
    }
    return n
}

fn first(s: []u8) -> int {
    return int(s[0])
}

fn total(s: []u8) -> int {
    n := 0
    rest := s
    while rest.len > 0 {
        n += first(rest)
        rest = rest[1..]
    }
    return n
}
```

- `sum` indexes a slice that never changes, over 4,096 `int`s.
- `count` walks 16 KB of chibi source the way a lexer does: it reads the first byte and drops it. It runs twice, once for a byte that never occurs, so every branch is predictable, and once for `' '`.
- `total` makes the same walk with a call per byte that receives the remaining slice.

`kernels_arm64.S` and `kernels_x86_64.S` hold the forms; `reference.c` holds the same kernels in C with the same checks, built by clang -O2, as a yardstick for release builds.

```bash
make run           # checks every form against C, then times them
make check-arm64   # cross-builds the arm64 forms and checks them under qemu, without timing
make mca           # llvm-mca estimates for both instruction sets
```

`make run` reads the cycle counter on macOS. Elsewhere it times with the wall clock and converts to cycles with a chain of dependent register adds. A chain of `add rax, 1` can't serve: this Xeon ran about 6 of them per cycle, because its renamer folds small immediates.

## Results on x86-64

Measured on 2026-11-19 in a cloud VM: an Intel Xeon 6 (Granite Rapids, family 6 model 173) with 4 vCPUs, which ran the chain of adds at 3.8–4.2 GHz. The ranges cover six runs, three of them pinned to one core; each run reports the median of 11. Cycles:

| Kernel | A | C | C against A | clang -O2, same checks |
|---|---|---|---|---|
| `sum`, per element | 1.50–1.54 | 1.49–1.53 | equal | 0.99–1.02 |
| `count` of a byte that never occurs, per byte | 2.72–2.88 | 1.97–2.06 | 27–28% fewer cycles | 0.98–1.02 |
| `count` of `' '` in source text, per byte | 3.50–3.60 | 2.65–2.82 | 21–26% fewer | 1.75–1.83 |
| `total`, per call | 5.17–5.49 with a hidden pointer; 4.96–5.49 with two registers | 4.44–5.08 | 5–15% fewer | 3.89–4.09 |

Instructions per iteration on x86-64, A against C: 11 against 9 in `sum`, 19 against 12 in `count`, and 27 (or 28) against 17 in `total`, counting the callee. On arm64: 10 against 9, 16 against 12, and 24 (or 25) against 19.

- **An index into a slice that doesn't change costs the same.** A's extra loads of the pointer and length don't lengthen the loop.
- **A walk that re-slices costs A 37–39% more cycles when every branch is predictable.** Each step loads and stores both halves of the slice: 6 loads and 2 stores, against C's 1 load. On this Xeon, A ran faster than llvm-mca predicts when it ignores the path from each store to the next iteration's loads (below), so that path added no visible wait; the cost is the extra memory operations. A core that forwards a store to a load more slowly loses more.
- **A call that passes a slice costs A 5–18% more.** The two A forms are equal within noise, so passing a pointer to a copy and passing two registers that the callee stores cost the same here.
- **clang -O2 is faster than either form** because it removes the checks that the loop condition already guarantees and rotates the loop: 6 instructions per byte in `count`, against C's 12. That is work for the release backend, not for the IR's shape.

## The M4 Pro is still to measure

The owner's M4 Pro was not available, and emulated arm64 code can't be timed. `make run` on the Mac gives the arm64 numbers.

llvm-mca can't stand in for it:
- LLVM 18's `apple-m1` model is the scheduling model of the 2013 Cyclone core: its resources are named `CyUnit…`.
- llvm-mca either ignores the dependency from a store to a later load of the same slot, or makes the load wait for the store's full latency. Neither matches this Xeon. For `count` with A it predicts 3.02 or 34.0 cycles, against 2.72–2.88 measured; for `total` with A, 7.16 or 153, against 5.17–5.49. For C, which keeps nothing in memory, it predicts 1.50, 2.00 and 4.41: within 3% of the measured loops, and at the low end of the calls.
- With the `apple-m1` model, A's `count` lands anywhere between 3.21 and 14.0 cycles per byte, against 3.01 for C.

Prediction for the M4 Pro: C is at least as fast as A on every kernel, and A loses more than on this Xeon on `count`. In [logical-lowering](../logical-lowering/README.md), a store and reload between a comparison and its branch cost the M4 Pro 1.3 cycles per statement.

## Result

C is at least as fast as A in every case measured, and 21–28% faster on the walk that a lexer makes. That supports the decision of 2026-11-15: step 3d builds C. A slice argument in two registers also matches C's convention for a 16-byte struct of two integers, which the proposed context register of 2026-11-18 keeps intact.
