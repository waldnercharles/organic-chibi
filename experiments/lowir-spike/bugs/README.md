# cppgm bugs

The spike found seven bugs in cppgm 0.10.2, all at `-O1` and up. cppgm's main branch at commit 0025136 (2026-11-21, 139 commits after 0.10.2) fixes two. [reports.md](reports.md) drafts reports for the other five.

| Bug | Where | Standalone repro | chibi programs that hit it | Main at 0025136 |
|---|---|---|---|---|
| A `switch` on an `i1` with cases 0 and 1 always takes the default | `lowiropt` | `lowir/switch-on-i1.lowir` | `switch-on-i1` | not fixed |
| A crash in `opt::clone_call`, inlining a function with two switches, the second with a case it can't reach, and a loop | `lowiropt` | `lowir/inliner-crash.lowir` | `inliner-crash`; 2 of the second 15,000 random programs | not fixed |
| A crash in `JumpThreading::find_use_dominators` | `lowiropt` | `lowir/jump-threading-crash.lowir` | 1 of the second 15,000 | not fixed |
| A crash in `opt::compact_body` | `lowiropt` | `lowir/compact-body-crash.lowir` | 2 of the second 15,000 | not fixed |
| A crash in `opt::sink_values` | `lowiropt` | `lowir/sink-values-crash.lowir` | 2 of the second 15,000 | not fixed |
| A compare of a truncated `and` against zero tests the mask at the `and`'s width, not the truncated width | instruction selection | `lowir/truncated-compare.lowir` | `truncated-compare`, `lost-trap-1`, `lost-trap-2` | fixed |
| A crash in `LoopPass::fold_exit_values`, from `opt::remove_loops`, on a loop that stores a truncated value into a slot | `lowiropt` | `lowir/remove-loops-crash.lowir` | `remove-loops-crash`; 4 of the first 15,000 and 4 of the second | fixed |

The four crashes that main still has each need a `switch` with a case outside the selector's known range; making the case reachable stops each one. The first 15,000 random programs use seeds 1111, 2222 and 3333, and the second 15,000 seeds 4444, 5555 and 6666, at 5,000 programs per seed.

The files in `lowir/` need only cppgm's own tools: `lowiropt` reads LowIR text and writes the optimized LowIR, and `lowir2native` compiles LowIR to a static executable. `lowir2native` runs no LowIR optimization at any level, so the optimizer bugs show only through `lowiropt`. Build both with `make -C dev lowiropt lowir2native CXX=g++ CPPGM_HOST_CXX=g++ -j4`, then:

```bash
lowiropt -O1 -o o1.lowir lowir/switch-on-i1.lowir && lowir2native -O0 o1.lowir -o o1 && ./o1; echo $?   # 1; 0 at lowiropt -O0
lowiropt -O1 -o o1.lowir lowir/inliner-crash.lowir                                                    # segmentation fault
lowiropt -O1 -o o1.lowir lowir/jump-threading-crash.lowir                                             # segmentation fault
lowiropt -O2 -o o2.lowir lowir/compact-body-crash.lowir                                               # segmentation fault
lowiropt -O2 -o o2.lowir lowir/sink-values-crash.lowir                                                # segmentation fault
lowir2native -O1 lowir/truncated-compare.lowir -o tc && ./tc; echo $?                                  # 1 on 0.10.2; 0 at -O0
lowiropt -O1 -o o1.lowir lowir/remove-loops-crash.lowir                                               # segmentation fault on 0.10.2
```

The jump threading, `compact_body` and `sink_values` repros were reduced from random programs by deleting LowIR lines and blocks while `lowiropt` still crashed in the same function, then given a store to every scalar slot before its first load. The others were reduced from chibi first.

Each `.chibi` file is a reduced chibi program, and the `.lowir` file next to it is the LowIR the translator gave cppgm for it, written with `--ir` before optimization. `chibi-lowir lowir -c --shared -O2 -o x.o FILE.chibi` reproduces the crashes; the others need a call:

| File | Call | Result at `-O2` | Expected |
|---|---|---|---|
| `switch-on-i1` | `p_f(ctx, 1)` | traps with kind 5 | returns 1 |
| `truncated-compare` | `p_f(ctx, 65536)` | returns 0 | returns 1 |
| `lost-trap-1` | `p_e2(ctx, -4, 2, INT64_MIN, 33.625)` | returns | traps with kind 2 |
| `lost-trap-2` | `p_e2(ctx, -16, 256, -13, -203.375)` | traps with kind 4 | traps with kind 2 |

The two lost traps are the truncation bug. After `lowiropt` drops an `and` that a truncation makes redundant, each division check compares a truncated `and` against zero: `trunc u32 (and i64 c, -6)` in `lost-trap-1`, and `trunc u8 (trunc i32 (and i64 b, 0x7fffffff))` in `lost-trap-2`. Instruction selection then tests the whole mask and misses the zero.

The translator works around `switch-on-i1` (it switches on the `bool` widened to `i64`) and the `inliner-crash` program (it drops enum member ranges the source type can't reach); the `.lowir` files are from before those workarounds. The enum workaround covers only conversions to enums, so a `switch` whose case the selector can't reach still gets through, as the second 15,000 programs show.
