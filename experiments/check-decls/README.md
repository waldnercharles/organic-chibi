# Checker step 1: declarations, types and signatures

This experiment measures step 1 of the checker ([docs/research/checker-and-ir.md](../../docs/research/checker-and-ir.md) section 8): `src/check.c` declares every top-level declaration, then resolves constants, struct, enum and union members, and function signatures, on one thread. Function bodies are step 2.

```bash
make run   # generates 200 files into /tmp/check-decls-corpus, then -O3 -flto -DNDEBUG
```

## The corpus

`gen.py` writes 200 files of 500 declarations each: 25% structs with 2–7 fields, 10% enums with a `u8` backing and 3–9 members, 20% constants with integer arithmetic, and 45% functions with 0–4 parameters and empty bodies. Types refer to other declarations uniformly at random, in any file, before or after their use. That is a worst case for caches: real code mostly refers to nearby declarations and a few common types.

| Measure | Value |
|---|---|
| Lines | 451,264 |
| Nodes | 1,217,784 (2.70 per line) |
| Declarations | 100,000 |
| Types | 118,813 |
| Diagnostics | 0 |

## Result

The timed part is `check_init`, `check_package` and `check_free`. Lexing and parsing run once, before the timing. The harness asks macOS for performance cores (`QOS_CLASS_USER_INTERACTIVE`) and reports the median of 11 runs. Other sessions kept the load average at 5–11, and some runs still landed on slower cores (IPC 1.6 rather than 2.2), so the cycle counts below vary by up to 30% between invocations. Instructions per node vary by under 1%.

| Version | Instructions per node | Cycles per line |
|---|---|---|
| First version | 110 | 153–179 |
| Literal parsing without a 128-bit division per digit | 101 | 119–158 |
| Signature walk stops at the body; 64-bit fast paths for literals, `/` and `%` | 96.6 | 116–146 |
| Type table and intern map reserved from the declaration count | 94.3 | 152–159, all at load average 7 |

Cycle counts are comparable only at similar load; instruction counts are comparable across all rows. Since 2026-11-14 the benchmark also reports lines per second from wall-clock time: 32 million lines per second on one core at 4.09 GHz (127 cycles per line, at low load). My estimate before measuring was about 150 cycles per line. H5 allows 3,000 cycles per line for all of checking.

**Since step 2 (2026-11-13).** Step 2 checks function bodies, so a function that declares a return type and has an empty body is now an error. `gen.py` therefore gives those functions the body `while true {}`, which adds about 35,000 lines. On that corpus the step-1 checker runs at 80 instructions per node (122–132 cycles per line), and the step-2 checker at 126 (155–166). [check-bodies](../check-bodies/README.md) explains the difference.

Two changes didn't help and were reverted:
- **A fingerprint in each type-map slot,** so that a probe compares hashes before reading a type record. Predicted −5%; measured no change.
- **One 16-byte record for the declaration fields a name lookup reads** (kind, phase, type, value) instead of four arrays. Predicted −5 to −7%. Measured no change at 100,000 declarations, where all arrays fit in the L2 cache and the four loads run in parallel, and none at 1,000,000 declarations either (324–339 cycles per line before, 332–338 after, interleaved runs).

## Scaling

At 2,000 files and 1,000,000 declarations, the same checker took 324–339 cycles per line, about 2.8 times the cost per line at 100,000. Every reference goes to a uniformly random declaration, so the declaration, type and name tables no longer fit in the cache. Real code has far more locality, so this is an upper bound. Even so, it is 11% of H5's budget.

## Profile

`sample` on a build without inlining, before the last three changes:
- the node loop (`walk`): 18%;
- name lookups (`resolve_name`): 16%;
- type interning (`intern_type`, `type_key`, `same_type`, `grow_type_map`, `add_type`): 17%;
- integer literals: 5%;
- the pre-scan for a signature's end: 4%, since removed;
- 128-bit division: 3%, now only for operands beyond 64 bits.
