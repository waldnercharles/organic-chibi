# The cppgm LowIR spike

The Compiler row of 2026-11-19 in [decisions.md](../../docs/decisions.md) asks for a spike of cppgm's LowIR as the release backend in place of LLVM, once the C backend works. The C backend reached that point at commit 5347399 (step 3j.4). This experiment translates chibi's IR to LowIR through LowIR's C API, checks the result against the IR reference interpreter, and times compile and run speed against the C backend with clang `-O2`. All timings come from the cloud's x86-64 Xeon (Granite Rapids, 4 vCPUs) and are wall time unless marked as CPU time, with cppgm 0.10.2 (ABI version 5) built from source with GCC 13, and clang 18.

## Result

- **Compile speed:** LowIR `-O2` compiled the 695,000-line corpus in 16.7 s on one thread (18.3 s from source), against 357 s of CPU for clang `-O2` on the C backend's output and 34.3 s for clang `-O0`: 21× and 2× faster. LowIR `-O0` took 2.0 s.
- **Code speed, overflow checks off:** 0.92 of clang `-O2` (geometric mean of 7 programs), or 0.82 without `fib`, where LowIR is 1.8× faster.
- **Code speed, overflow checks on:** 0.73 of clang `-O2`, or 0.59 without `fib`. LowIR has no overflow operation, so a checked `+` is an explicit sign test and a checked 64-bit `*` a 128-bit multiply. The checks cost LowIR 1.57× on average and clang 1.25×, up to 4.1× on a matrix multiply.
- **Correctness:** at `-O0`, every test agrees with the reference interpreter, including 15,000 random programs. At `-O1` and up, cppgm's optimizer gave wrong code in two reduced patterns and two more programs, and crashed in two places. Of the same 15,000 random programs at `-O2`, 5 crashed the compiler and 2 gave wrong results, after the translator worked around two of the bugs.

LowIR keeps its promise on compile speed and comes close on code speed without checks. Two things stand between it and the release role: an optimizer that isn't yet correct, and checks that cost twice what they cost in clang.

## What's here

- `lowir.c` and `lowir.h`: the translator, 1,916 lines. `lowir_package` adds one package's functions, data and globals to a LowIR module; `lowir_unsupported` names what it can't translate.
- `main.c`: `chibi-lowir`, the `chibi` driver with one more command. `chibi-lowir lowir -o program [-O0..3] [-c] [--wrap] [--shared] [--ir file.lowir] [--times] file.chibi... [-- linker arguments]` builds a program through LowIR; `--wrap` turns the overflow checks off, `--times` prints the time of each phase. The other commands are `chibi`'s.
- `test.c`: the checks below. It includes `test/c_source_test.c` to reuse its programs, its interop functions and its comparison with the reference interpreter.
- `bench/`: the seven programs that were timed, with `print.c`, which prints their results.
- `bugs/`: the reduced programs for cppgm's bugs, with the LowIR the translator gave cppgm for each.

```bash
git clone --depth 1 --branch v0.10.2 https://github.com/vishvananda/cppgm /tmp/cppgm-0.10.2
make -C /tmp/cppgm-0.10.2/dev lowir-library CXX=g++ CPPGM_HOST_CXX=g++ -j4   # about 1 minute
make chibi CPPGM=/tmp/cppgm-0.10.2   # build/lowir-spike/chibi-lowir
make test CPPGM=/tmp/cppgm-0.10.2    # CHIBI_LOWIR_LEVEL=0..3, CHIBI_DIFFERENTIAL, CHIBI_DIFFERENTIAL_SEED
```

`CHIBI_LOWIR_CRASHES=dir` compiles each random program in a child process, saves the ones that crash the compiler into `dir`, and counts wrong results instead of stopping at the first.

## How the translator works

- **One LowIR function per chibi function**, with the context as the first parameter, and one LowIR slot per chibi slot, as MYC's GM backend does. LowIR's slot promotion turns slots into SSA values; the translator makes no phis.
- **A parameter that is never stored or addressed has no slot**: loads read the parameter itself. A struct parameter arrives as an object and is copied into its slot with `copyobj`.
- **Types:** integers map to LowIR's integer types (`u64` is `i64`, since operations carry the signedness), `bool` to `i1`, pointers and `[*]T` to `ptr`. A struct is a record type with C's layout, built with `cppgm_lowir_record`, so LowIR passes it as C does; a slice is a record of a pointer and an `i64`. An array is a record of one repeated element.
- **Control flow:** each `block`, `loop`, `if`, `else` and `switch` arm becomes a LowIR block. A value an `if` or `block` carries goes through a slot. An `if` on a constant becomes a jump.
- **Switches:** a range of 16 values or more becomes a subtraction and an unsigned compare; smaller ranges become cases of one LowIR `switch`.
- **Checks** branch, marked unlikely, to one block per kind of trap in each function, which calls `chibi_trap`, declared cold and noreturn. A checked `+` or `-` is followed by the sign test cppgm's own C front end uses for `__builtin_add_overflow`, a checked 64-bit `*` by a 128-bit multiply, and a narrower `*` by a 64-bit multiply and a range compare. With a constant operand, a checked `+`, `-` or `*` is one compare against a precomputed bound.
- **Calls into C** use C's convention and no context. Variadic arguments narrower than 32 bits are widened, which C does and LowIR leaves to the producer. Callbacks and exports get adapters that load `chibi_context` and call the chibi function, as in the C backend.
- **Data and globals** become two internal LowIR globals.
- **Not translated:** vectors. The compiler's own source uses them, so they come before hand-off 2.

## Correctness

| Test | `-O0` | `-O1` to `-O3` |
|---|---|---|
| The C backend's grid: 229 functions of two `int`s on 288 argument pairs, against the reference interpreter | agrees | agrees |
| The C backend's interop test: calls into C, variadic calls, callbacks, exports, `main` | agrees | agrees |
| 15,000 random programs from `test/random`, 4 entry points × 6 calls each | agrees | `-O2`: 5 crashes, 2 wrong results |

The bugs, all in cppgm's optimizer (`-O1` and up):

1. **A `switch` on an `i1`** with cases 0 and 1 always takes the default. Worked around: the translator switches on the `bool` widened to `i64`.
2. **A truncation dropped from a compare.** `u16(i32(b & 2147483647) & 65535) == 0` compiles to a test of `b & 0x7fffffff`, so `b = 65536` gives false. No workaround: the mask is the program's own.
3. **A crash in the inliner** (`opt::clone_call`) when it inlines a function holding a `switch` whose selector can't reach any case, a check inside one arm, and a loop. In chibi this comes from converting a `u8` to an enum whose members are all negative. Worked around: the translator drops enum member ranges the source type can't reach.
4. **A crash in `opt::remove_loops`**, in 4 of the 15,000 random programs: a loop inside a `switch` arm the selector can't reach. Not worked around.
5. **Two wrong results** in the 15,000, both a division by zero that traps at `-O0` and not at `-O1`. Reduced to about 25 lines each, not explained.

[bugs/README.md](bugs/README.md) lists the reduced programs and how to show each bug. Reporting them upstream needs the owner's go-ahead.

## Code speed

Seven programs in `bench/`, each built four ways. "Checked" is the default. "Unchecked" turns off the overflow checks of `+`, `-`, `*` and negation and keeps the others (bounds, division, shifts, conversions): `--wrap` for LowIR, and for clang the C backend's output compiled with `-fwrapv` and `__builtin_{add,sub,mul}_overflow` defined as wrapping arithmetic. The table gives the fastest of 5 runs, in seconds, and the ratio is clang's time over LowIR's, so above 1 means LowIR is faster.

| Program | What it stresses | clang checked | LowIR checked | Ratio | clang unchecked | LowIR unchecked | Ratio |
|---|---|---|---|---|---|---|---|
| `fib(40)` | calls | 0.475 | 0.181 | 2.62 | 0.246 | 0.135 | 1.82 |
| sieve to 20 million, 5 times | byte stores, bounds checks | 0.444 | 0.809 | 0.55 | 0.547 | 0.616 | 0.89 |
| longest Collatz chain below 3 million | `%`, `/`, `*` | 0.678 | 0.909 | 0.75 | 0.442 | 0.868 | 0.51 |
| 400×400 `int` matrix multiply, 6 times | multiply-add, bounds checks | 0.350 | 1.163 | 0.30 | 0.215 | 0.284 | 0.76 |
| spectral norm, n = 2000 | `f64`, calls | 0.398 | 0.521 | 0.76 | 0.342 | 0.350 | 0.98 |
| 4,096 points × 40,000 steps | 24-byte structs by value | 0.318 | 0.710 | 0.45 | 0.303 | 0.337 | 0.90 |
| FNV-1a over 1 MB, 300 times | wrapping arithmetic only | 0.335 | 0.337 | 0.99 | 0.335 | 0.335 | 1.00 |
| **Geometric mean** | | | | **0.73** | | | **0.92** |
| without `fib` | | | | 0.59 | | | 0.82 |

LowIR `-O1`, checked: 0.388, 0.775, 0.868, 2.111, 0.854, 1.142 and 0.334 s, about 0.6 of its `-O2` speed.

Where the time goes, from the machine code:
- **Checks.** clang turns each `__builtin_*_overflow` into the operation and a `jo`. LowIR keeps the sign test as written: four more instructions after a checked add, and a one-operand `imul` with a compare of the high half for a checked multiply. Nor does it hoist an invariant check out of a loop: in the matrix multiply, the check on `i * n` runs in the innermost loop.
- **Peepholes.** `x % 2 == 0` on a signed `int` compiles to the full remainder sequence, five instructions, where clang tests one bit. That alone puts unchecked Collatz at 0.51 of clang.
- `fib` is faster with LowIR in both modes; the machine code wasn't studied.

## Compile speed

The corpus from [check-bodies](../check-bodies/README.md): 200 files, 694,816 lines, 20,000 functions, compiled as one package into one object, with every function kept (`--shared` for LowIR, `-DCHIBI_FN=` for the C). Both compilers ran on one thread.

| Step | Time |
|---|---|
| chibi's front end (lexing, parsing, checking) | 0.7–0.9 s |
| Translating to LowIR (this experiment's code and the library's commit) | 0.6–0.8 s |
| LowIR validation | 0.3 s |
| LowIR compile `-O0` / `-O1` / `-O2` | 2.0 / 12.3 / 16.7 s |
| Writing the C (`chibi c`) | 1.5 s |
| clang `-O0` on that C (105 MB, one file) | 34.3 s |
| clang `-O2` on that C, split into four files | 357 s of CPU (76–106 s each, 112 s on 4 cores) |

As one file, clang `-O2` hadn't finished after 25 minutes of CPU and was stopped, so the four files are the fair comparison; the research thread measured 354 s the same way (checker-and-ir.md, step 3j.1). The objects: 30.8 MB at LowIR `-O0` and 18.1 MB at `-O2`; clang's are 56.4 MB at `-O0` and 12.6 MB at `-O2`.

On the seven small programs, LowIR's `-O2` compile takes 1.0–2.8 ms, against 34–53 ms for clang `-O2`, most of it clang starting up. A whole build takes about 58 ms through LowIR and 105 ms through clang; linking, which compiles the C runtime with `cc`, is most of LowIR's.

## What this means for the decision

- On compile speed LowIR is what the survey said: much faster than clang at equal optimization, from a library that takes records in place.
- On code speed it reaches clang only where overflow checks don't run. With checks on, chibi's default, it runs at 0.6–0.7 of clang, below MIR's 0.9 of gcc and close to QBE's 0.6. Closing that gap needs an overflow operation in LowIR, or instruction selection that fuses the sign test into a flag check, plus loop-invariant code motion of checks. That work is upstream, or in chibi's own optimizer.
- Its optimizer isn't trustworthy yet: at least three wrong-code bugs and two crashes in one day of testing, all at `-O1` and up. A release backend that miscompiles a mask-and-compare is a risk chibi's users would carry.

Not done: vectors in the translator, debug information from the IR's line table (step 3j.5), the M4 Pro, and hand-off 2 (building the compiler itself through LowIR and timing it on the corpus).
