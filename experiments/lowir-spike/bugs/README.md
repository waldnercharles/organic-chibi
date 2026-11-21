# cppgm 0.10.2 optimizer bugs

Each `.chibi` file is a reduced program, and the `.lowir` file next to it is the LowIR the translator gave cppgm for it, written with `--ir` before optimization. Every one compiles correctly at `-O0` and goes wrong at `-O1` and up. `chibi-lowir lowir -c --shared -O2 -o x.o FILE.chibi` reproduces the crashes; the others need a call, given below.

| File | What goes wrong | Call that shows it |
|---|---|---|
| `switch-on-i1` | A `switch` on an `i1` with cases 0 and 1 always takes the default, here the trap block | `p_f(ctx, 1)` traps with kind 5; it should return 1 |
| `truncated-compare` | `u16(i32(b & 2147483647) & 65535) == 0` compiles to `test $0x7fffffff, %esi` | `p_f(ctx, 65536)` returns 0; it should return 1 |
| `inliner-crash` | Segmentation fault in `opt::clone_call`, inlining `h4` into `e1`: `h4` has a switch whose selector can't reach any case, a check in one arm, and a loop after it | compiling |
| `remove-loops-crash` | Segmentation fault in `opt::remove_loops`: a loop in a switch arm the selector can't reach (`u16(v4 & 127)` is never 256) | compiling |
| `lost-trap-1` | A division by zero that should trap doesn't | `p_e2(ctx, -4, 2, INT64_MIN, 33.625)` returns; it should trap with kind 2 |
| `lost-trap-2` | A division by zero that should trap doesn't, and a later range check traps instead | `p_e2(ctx, -16, 256, -13, -203.375)` traps with kind 4; it should trap with kind 2 |

The translator works around `switch-on-i1` (it switches on the `bool` widened to `i64`) and `inliner-crash` (it drops enum member ranges the source type can't reach); the `.lowir` files are from before those workarounds.
