# The cppgm LowIR spike

The Compiler row of 2026-11-19 in [decisions.md](../../docs/decisions.md) asks for a spike of cppgm's LowIR as the release backend in place of LLVM, once the C backend works. The C backend reached that point at commit 5347399 (step 3j.4). This experiment translates chibi's IR to LowIR through LowIR's C API, checks the result against the IR reference interpreter, and times compile and run speed against the C backend with clang `-O2`. All timings come from the cloud's x86-64 Xeon (Granite Rapids, 4 vCPUs) and are wall time unless marked as CPU time, with cppgm 0.10.2 (ABI version 5) built from source with GCC 13, and clang 18. Hand-off 2 (below) used cppgm's main branch at 0025136 instead, which has the same ABI and fewer bugs. Since 2026-11-23 the spike builds against chibi's fork of cppgm: release 0.10.3 (7bc2ee42) with the patches in `cppgm/`, which fix every cppgm bug the spike found.

## Result

- **Compile speed:** LowIR `-O2` compiled the 695,000-line corpus in 16.7 s on one thread (18.3 s from source), against 357 s of CPU for clang `-O2` on the C backend's output and 34.3 s for clang `-O0`: 21× and 2× faster. LowIR `-O0` took 2.0 s.
- **Code speed, overflow checks off:** 0.94 of clang `-O2` (geometric mean of 7 programs), or 0.84 without `fib`, where LowIR is 1.9× faster.
- **Code speed, overflow checks on:** 0.79 of clang `-O2`, or 0.65 without `fib`. LowIR has no overflow operation, so a checked `+` is an explicit sign test and a checked 64-bit `*` a 128-bit multiply. The checks cost LowIR 1.51× on average and clang 1.27×, and LowIR up to 3.75× on a matrix multiply.
- **Correctness:** at `-O0`, every test agrees with the reference interpreter, including 30,000 random programs. At `-O1` and up, cppgm 0.10.2 gave wrong code in two patterns and crashed in five places. On 30,000 random programs at `-O2`, after the translator worked around two of the bugs, 0.10.2 crashed 15 times and gave 3 wrong results. cppgm's main branch, 15 hours and 139 commits later, fixes one wrong-code bug and one crash: on the same programs it crashes 7 times and gives no wrong results.

LowIR keeps its promise on compile speed and comes close on code speed without checks. Two things stand between it and the release role: an optimizer that isn't yet correct, and overflow checks with about twice clang's overhead.

## Hand-off 2: the compiler through LowIR

At commit d8c3f9c (step 3t.10) all 11 of the compiler's C files translate to chibi. Each file is its own chibi program, so each one compiles through LowIR into its own object; the 11 objects link with one copy of the runtime and the vendored xxHash.

- **It works.** The compiler built through LowIR `-O2` writes the same C as the C build, byte for byte, for all 11 translated files, and for the 200-file corpus below. The test suite passes with the 10 LowIR objects in place of the compiler's C files.
- **Compile speed:** LowIR `-O2` compiles the 11 files in 1.5 s, against 10.2 s for `chibi c` and clang `-O2`: 6.8× faster. At `-O0` it's 0.23 s against 1.9 s.
- **Run speed:** the compiler built through LowIR `-O2` runs at 0.95 of the one built through clang `-O2` (0.93 to 0.99 in three interleaved runs). `-O1` runs as fast as `-O2`, and `-O0` at 0.66. The compiler built from the C source runs at the speed of the one built from the translations.

| File | Lines | `chibi c` | clang `-O0` | clang `-O2` | LowIR `-O0` | LowIR `-O1` | LowIR `-O2` |
|---|---|---|---|---|---|---|---|
| arena | 148 | 0.004 | 0.052 | 0.063 | 0.005 | 0.007 | 0.007 |
| str | 83 | 0.003 | 0.039 | 0.066 | 0.005 | 0.006 | 0.006 |
| names | 156 | 0.004 | 0.056 | 0.082 | 0.006 | 0.009 | 0.009 |
| exact | 1,267 | 0.005 | 0.107 | 0.490 | 0.011 | 0.052 | 0.092 |
| parser | 2,188 | 0.006 | 0.152 | 0.526 | 0.013 | 0.072 | 0.081 |
| lexer | 13,991 | 0.019 | 0.124 | 0.359 | 0.063 | 0.096 | 0.131 |
| check | 10,011 | 0.021 | 0.751 | 5.802 | 0.067 | 0.622 | 0.737 |
| arm64 | 5,065 | 0.011 | 0.287 | 1.858 | 0.030 | 0.209 | 0.320 |
| driver | 798 | 0.005 | 0.056 | 0.187 | 0.007 | 0.019 | 0.025 |
| c_source | 1,983 | 0.007 | 0.138 | 0.673 | 0.014 | 0.063 | 0.093 |
| main | 7 | 0.003 | 0.032 | 0.036 | 0.004 | 0.004 | 0.004 |
| **Total** | | **0.088** | **1.794** | **10.142** | **0.225** | **1.159** | **1.505** |

Seconds, the fastest of 3, one file after another on one thread. The LowIR times include chibi's front end; the C path's front end is the `chibi c` column, which a C build adds to clang's. clang runs with `-march=x86-64-v2`, the project's setting, and each run pays about 30 ms of start-up. The lexer is mostly constant tables, which `chibi c` writes as C initializers and LowIR as data items.

Run speed, as the time to write the corpus as C (`c -o /dev/null` on 200 files, 694,816 lines; fastest of 5 or 7 runs, taken in rounds):

| Build of the compiler | Run 1 | Run 2 | Run 3 (output to a file) |
|---|---|---|---|
| C source, clang `-O2` | 0.992 | 0.958 | 1.093 |
| translations, clang `-O2` | 0.957 | 0.980 | 1.169 |
| translations, clang `-O0` | 3.651 | | 3.819 |
| translations, LowIR `-O2` | 1.033 | 1.029 | 1.186 |
| translations, LowIR `-O1` | 1.043 | 1.039 | 1.167 |
| translations, LowIR `-O0` | 1.454 | | 1.729 |

The translations keep chibi's overflow checks, which cost LowIR more than clang on the small programs below. What makes up the 5% on the compiler wasn't studied. The binary is 857 KB through LowIR `-O2` and 717 KB through clang `-O2`.

## The compiler through LowIR again, at step 3k.25

From step 3k.19 the identifier hash multiplies in `u128`, so the compiler stopped building through LowIR until the translator took 128-bit integers. At commit 45954a1 (step 3k.25) it does, and `make translated` now writes the compiler as one chibi program of 36,435 lines, which builds through LowIR in one run.

- **It works.** The compiler built through LowIR `-O0` and `-O2` writes the same C as the C build, byte for byte, for its own source and for the 200-file corpus. The test suite passes with the compiler's library built through LowIR `-O2` (`--runtime-suffix _translated`) in place of `lib.o`.
- **Compile speed:** LowIR `-O2` builds the object in 1.7 s, against 11.0 s for `chibi c` and clang `-O2` on the one C file: 6.4× faster. At `-O0` it's 0.20 s against 1.8 s.
- **Run speed:** on the corpus, pinned to one core, 9 interleaved runs: the clang `-O2` build of the translations takes 931 ms at best (987 median), LowIR `-O2` 1,002 ms (1,113), `-O1` 1,030 ms (1,113) and `-O0` 1,632 ms (1,732). LowIR `-O2` runs at 0.93 of clang at best and 0.89 at the median, against 0.95 at hand-off 2. Callgrind counts 9.49 billion instructions against clang's 9.01 billion (+5.3%), two thirds of either in the C library's `printf` and `putc`.
- **Two fixes found the slowdown.** The translator ignored `@inline` and `@noinline` (step 3k.10), so the lexer's `block_masks` and `simd_mask64`, marked `@inline`, stayed calls; the translator now passes them as LowIR's force-inline and no-inline facts. And LowIR doesn't know a bit count's range, so the lexer's narrowing of each `trailing_zeros` to `i32` kept two range checks a token; the translator now skips the range check when it narrows a bit count to a type of 8 value bits or more. The two took the count from +7.8% to +5.3%. Most of the rest is the lexer, 518 million instructions against clang's 332 million, where LowIR moves 16-byte vectors through the stack in 64-bit words, and `line_at`'s newline count, 205 million against 97 million, which clang vectorizes and LowIR doesn't. The binary is 936 KB.
- **Step 3t.16** (3b59582) removes most of the translation's nil checks. The compiler still builds through LowIR and writes the same C, and the count on the corpus is 9.36 billion against clang's 8.96 billion (+4.5%). [../../docs/research/optimizer-from-lowir.md](../../docs/research/optimizer-from-lowir.md) prices each LowIR pass on the compiler's own code and traces the gap.

## What's here

- `lowir.c` and `lowir.h`: the translator, 2,647 lines. `lowir_package` adds one package's functions, data and globals to a LowIR module. It translates all of chibi's IR.
- `main.c`: `chibi-lowir`, the `chibi` driver with one more command. `chibi-lowir lowir -o program [-O0..3] [-c] [--wrap] [--shared] [--ir file.lowir] [--times] [--runtime-suffix name] [--release-fast] file.chibi... [-- linker arguments]` builds a program through LowIR; `--wrap` turns the overflow checks off, `--release-fast` checks the program as `chibi build --release-fast` does, `--times` prints the time of each phase, and `--runtime-suffix _x` names the runtime `chibi_trap_x` and `chibi_context_x`, so that objects can link into a program that has its own runtime, such as the test suite. The other commands are `chibi`'s.
- `test.c`: the checks below. It includes `test/c_source_test.c` to reuse its programs, its interop functions, its vector programs and its comparison with the reference interpreter.
- `bench/`: the seven programs that were timed, with `print.c`, which prints their results.
- `bugs/`: the cppgm bugs: chibi programs that hit them with the LowIR the translator gave cppgm, standalone LowIR repros in `bugs/lowir/`, and draft reports in `bugs/reports.md`, which won't be filed.
- `cppgm/`: chibi's fork of cppgm as patches on release 0.10.3, from `git format-patch`. Each fix comes with a fixture in cppgm's PA33 regression lane, and `0005` marks each changed source file, as cppgm's Apache License 2.0 asks of a shared modified file.

```bash
git clone https://github.com/vishvananda/cppgm /tmp/cppgm && git -C /tmp/cppgm checkout -b chibi 7bc2ee42
git -C /tmp/cppgm am "$PWD"/cppgm/*.patch
make -C /tmp/cppgm/dev lowir-library CXX=g++ CPPGM_HOST_CXX=g++ -j4
make chibi CPPGM=/tmp/cppgm   # build/lowir-spike/chibi-lowir
make test CPPGM=/tmp/cppgm    # CHIBI_LOWIR_LEVEL=0..3, CHIBI_DIFFERENTIAL, CHIBI_DIFFERENTIAL_SEED
```

The compiler through LowIR, after `make translated` in the repository's root:

```bash
cd build/translated/host
../../lowir-spike/chibi-lowir lowir -c -O2 -o chibi.lowir.o ../entry.chibi *.chibi
../../lowir-spike/chibi-lowir runtime > runtime.c
cc -O2 -o chibi-lowir-built chibi.lowir.o runtime.c ../../../vendor/xxhash/xxhash.c -lm
```

Since step 3t.12, `make translated` writes the compiler as one program, in a chibi file for each C file and header in `build/translated/host`, so these commands build it in one run. Run from `build/translated/host`, the compiler's `#line` directives name the files as given, so its C differs from `chibi.c` in those lines only; run from the repository's root with the paths `make translated` uses, it writes `chibi.c` byte for byte.

`CHIBI_LOWIR_CRASHES=dir` compiles each random program in a child process, saves the ones that crash the compiler into `dir`, and counts wrong results instead of stopping at the first.

## How the translator works

- **One LowIR function per chibi function**, with the context as the first parameter, and one LowIR slot per chibi slot, as MYC's GM backend does. LowIR's slot promotion turns slots into SSA values; the translator makes no phis.
- **A parameter that is never stored or addressed has no slot**: loads read the parameter itself. A struct or array parameter arrives as an object and is copied into its slot with `copyobj`.
- **Types:** integers map to LowIR's integer types (`u64` is `i64` and `u128` is `i128`, since operations carry the signedness), `bool` to `i1`, pointers and `[*]T` to `ptr`. A struct is a record type with C's layout, built with `cppgm_lowir_record`, so LowIR passes it as C does; a slice is a record of a pointer and an `i64`. An array is a record of one repeated element, so it passes as C passes a struct that holds it (step 3k.21).
- **Control flow:** each `block`, `loop`, `if`, `else` and `switch` arm becomes a LowIR block. A value an `if` or `block` carries goes through a slot. An `if` on a constant becomes a jump.
- **Switches:** a range of 16 values or more becomes a subtraction and an unsigned compare; smaller ranges become cases of one LowIR `switch`.
- **Checks** branch, marked unlikely, to one block per kind of trap in each function, which calls `chibi_trap`, declared cold and noreturn. A checked `+` or `-` is followed by the sign test cppgm's own C front end uses for `__builtin_add_overflow`, a checked 64-bit `*` by a 128-bit multiply, and a narrower `*` by a 64-bit multiply and a range compare. With a constant operand, a checked `+`, `-` or `*` is one compare against a precomputed bound.
- **Calls into C** use C's convention and no context. Variadic arguments narrower than 32 bits are widened, which C does and LowIR leaves to the producer. Callbacks and exports get adapters that load `chibi_context` and call the chibi function, as in the C backend.
- **Data and globals** become two internal LowIR globals. An 8-byte slot that holds an offset into the data becomes an address item, so that the linker fills in the address.
- **Vectors** are `v128` values computed with LowIR's SSE operations. A vector constant, or a splat, puts one 64-bit pattern in the low lane with `fromi64` and copies it with `pshufd`. Lane-wise `+%`, `-%`, `&`, `|`, `^` and `~` are one or two operations; `*%` is `pmullw` for 16-bit lanes and a few operations from `pmullw` or `pmuludq` for the others. Compares give masks: unsigned lanes flip their sign bits before `pcmpgt`, `u8` uses `pminub`, and 64-bit lanes combine 32-bit compares. Shifts of 8-bit lanes shift 16-bit lanes and mask; a signed shift of 8-bit or 64-bit lanes flips the sign bit, shifts logically and subtracts the shifted sign bit. `lookup` is `pshufb` after a saturating add of 0x70, so that an index of 16 or more gives 0, and `top_bits` is `pmovmskb`, `movmskps` or `movmskpd`. Everything is SSE2 but `pshufb`, which is SSSE3; the C build's `-march=x86-64-v2` has both. Since step 3k.22 a vector variable is an array of lanes in an array slot, which operators read with a `v128` load and write with a `v128` store; LowIR's slot promotion keeps it in a register. A loop that counts identifier bytes 16 at a time has no stack loads or stores, and with checks on it ran at 0.84 of clang `-O2` (the fastest of 5 runs each, 2,000 passes over 1 MB).
- **128-bit integers** are LowIR's `i128`, which cppgm computes in pairs of 64-bit registers and divides with `__divti3` and its kin. A 128-bit constant is one literal of two words. LowIR has no `select` of an `i128`, so negation by sign goes through a mask: `(x ^ m) - m` with `m = x >> 127`. A checked 128-bit `*` multiplies the magnitudes as the C backend does, in three 64×64→128 products: it overflows when both high halves are nonzero, when the cross products reach 2^64, or when adding them shifted wraps, and a signed one also when the product passes the limit for its sign. The bit counts work on the two halves, and a `bitcast` between a vector and a 128-bit integer goes through memory.
- **Optionals of plain values** (step 3e.3): a `?T` of a `bool`, an integer, a float or an enum is two IR values, the value and a `bool` flag, as a slice is two. Each half is a LowIR value of its own, and a parameter takes two LowIR parameters. A result is a record of the value and a byte flag, `struct { T value; uint8_t present; }` as the C backend returns it, so its halves go through the spill slot on both sides of a call. `x!!` checks the flag. In memory (step 3e.4) a `?T` has the same layout: a load reads the value and the flag byte at `size_of(T)`, a store writes both, and a field of a struct that crosses into C is that record, so that a flag byte beside a float makes its eightbyte an integer one, as C's convention has it. A typed `if` whose type is a pair, a slice or a `?T`, passes both halves at `else` and `end` (step 3e.5): both branches store them into one stack slot of the pair's record, and the `end` loads them.
- **Errors** (step 3f.1): an `error` is a `u16`. An `E!T` result is a pair of the value and the code, returned as the record `struct { T value; uint16_t code; }` that the C backend declares, with the code at the next even offset after the value, so it goes through the spill slot as a `?T` result does. A `!void` result is the code alone. `try` and `catch` need nothing new, since the checker writes them with the kinds the translator already takes. An error set, written (step 3f.2) or inferred (steps 3f.3 and 3f.4), is an `error` too, and a `switch` on codes takes the path of a `switch` on integers.
- **`assert`** branches to the `chibi_trap` block for its kind of trap, and a release-fast **`assume`** to a block that holds only `unreachable`, so the optimizer may take the condition as true.

## Correctness

| Test | `-O0` | `-O1` to `-O3` |
|---|---|---|
| The C backend's grid: 493 functions of two `int`s on 288 argument pairs, against the reference interpreter (229 at hand-off 1, 371 at hand-off 2; steps 3k.23 to 3f.5 add strings, tuples, named results, two-result overflow checks, `defer`, optionals of plain values in registers and in memory, slices, strings and optionals from conditionals and `??`, `E!T` results with `try` and `catch`, also in functions without `->`, and error sets, written and inferred, with handled errors left out), with the 128-bit programs `MUL_HIGH` and `INT128_GRID` | agrees | agrees |
| 11 functions of the spike's own at the edges of 128-bit conversions, division, checked multiplication and bit counts, on 288 argument pairs against the reference interpreter: floats at ±2^127 and 2^128, the minimum divided by -1 and by 2^64 - 1, its remainder by a variable -1, products that overflow only through each of the three tests, and bit counts of 128-bit values narrowed to `i8` and `u8` | agrees | agrees |
| The C backend's wide error codes (step 3f.2): a set of 33,000 names, whose codes above 32,767 pass through a struct field, a `switch`, a `catch` and an `E!void`, in 2 functions of two `int`s on 288 argument pairs against the reference interpreter | agrees | agrees |
| The C backend's interop test: calls into C, variadic calls, callbacks, exports, `main`, C globals, and structs of one `?f32` or `?f64` through C (step 3e.5) | agrees | agrees |
| The C backend's array program (step 3k.21): 15 functions of two `int`s on 288 argument pairs, against the reference interpreter; once by hand, 16 calls from C, built with clang and GCC, into its functions that take and give arrays | agrees | agrees |
| The C backend's vector programs: 185 calls with hand-computed results (168 at hand-off 2), and a grid of 8 functions, one per lane type, on 5,120 argument sets each (main) | agrees | agrees |
| 15,000 random programs from `test/random` (seeds 1111, 2222, 3333), 4 entry points × 6 calls each | agrees | `-O2`: 4 crashes, 2 wrong results; main: none |
| 15,000 more (seeds 4444, 5555, 6666) | agrees | `-O2`: 11 crashes, 1 wrong result; main: 7 crashes |
| 15,000 more at hand-off 2 (seeds 7777, 8888, 9999), main only | not run | `-O2`: 9 crashes, no wrong results |
| All 45,000 at step 3k.24 through the fork (`cppgm/`) | not run | `-O2`: no crashes; 1 wrong result (bug 5), none since `cppgm/0004` |
| 20,000 from the generator that writes `defer` (step 3c), through the fork: seeds 1111, 2222 and 4444 at `-O2`, 3333 at `-O0` | agrees | no crashes, no wrong results |
| 5,000 more at step 3e.5 (seed 3505), through the fork | not run | `-O2`: no crashes, no wrong results |
| 5,000 more at step 3f.1 (seed 3606), through the fork | not run | `-O2`: no crashes, no wrong results |
| 5,000 more at step 3f.2 (seed 3707), through the fork | not run | `-O2`: no crashes, no wrong results |

Seven planted faults in the vector translation, in lookups, shifts, the 64-bit multiply and 64-bit compares, each made the vector programs fail. Of 26 planted faults in the 128-bit translation, the grid caught 16; the edge test above caught the other 8 it could, among them a lost overflow test in each of the three parts of the checked multiply, a float bound off by one at -2^127, and 64-bit bounds on a narrowing from 128 bits. Two can't change a result: an unguarded remainder by -1, since `__modti3` gives 0 for the minimum without trapping, and the width of the bit counts' carrier. Two planted faults in the skipped range check on bit counts, one that ignores the sign bit and one that skips it at every width, each made the edge test fail; a dropped `@inline` mark changes no result. Of 14 planted faults in the translation of optionals of plain values, the grid caught 5 at `-O2` and `-O0` and 1 more at `-O0` only: flags read or written at the wrong offset, a flag argument dropped, the nil check reversed, and a flag field of the wrong width. Six of the rest give a value the wrong LowIR type, such as an `i64` load of an `f64` or an `i64` parameter for the flag. LowIR doesn't check that a value's type matches its use, and cppgm compiles each of these to working code, so no test of the spike can see them. The last two change nothing: one stores the flag twice, and one leaves the result record's size unpadded. Of 13 planted faults in optionals in memory (step 3e.4), the tests caught 10 at `-O2` and `-O0` and 1 more at `-O0` only. One of the two survivors writes the flag before the value, which changes nothing. The other leaves the flag out of a struct field's record, which changes how C passes a small struct whose `?f32` or `?f64` shares an eightbyte with nothing else: a program that passes and returns `struct { a: ?f32 }` and `struct { a: ?f64 }` through C gives the right results through the C backend and through LowIR, and wrong ones with this fault, and since step 3e.5 the interop test passes such structs and fails with this fault. Of 16 planted faults in pairs carried through `else` and `end` (step 3e.5), the tests caught 13 at `-O2` and `-O0`: the second half read or written at the wrong offset or with the wrong type, a half dropped or taken from the wrong branch, and a new slot for each branch. One survivor writes the second half before the first, which changes nothing. The other two give the slot the other pair's record. LowIR ignores the field types of a slot's record, and the slot is big enough either way while the checker refuses `?i128`. Of 16 planted faults in the translation of errors (step 3f.1), the tests caught 10 at `-O2` and `-O0`: the code read or written at the wrong offset or narrowed to a bit or a byte, the code dropped from a return or a call's copy, an `error` as a 64-bit value, and a record too small for the code. Of the 6 survivors, three give a value the wrong LowIR type, such as an `i64` for a `!void` code, which LowIR doesn't check. One makes the code signed, which no operation on codes can see. Two change only the record's layout, the code at `size_of(T)` and an alignment of 1, which C would see if an `E!T` crossed into C, but step 3f.1 refuses `E!T` in `extern fn` and `export fn`. Three planted faults in the paths of step 3f.2 change no result: codes as signed values, the sign taken from a set's list index, and a `switch` on codes as an `i64`. The translation never widens or orders a code, and a `switch` matches exact values and tests a wide range as an unsigned difference, so the sign of a 16-bit value can't show. The test of wide error codes still guards the C backend and arm64, where a code widens into a register. The 9 crashes at hand-off 2 are all in functions already in bug 3: 5 in `opt::compact_body`, 3 in `opt::clone_call` and 1 in `opt::sink_values`.

The bugs, in 0.10.2 at `-O1` and up. [bugs/README.md](bugs/README.md) has a standalone LowIR file for each that needs only cppgm's own tools.

1. **A `switch` on an `i1`** with cases 0 and 1 always takes the default, in `lowiropt`. Worked around: the translator switches on the `bool` widened to `i64`. Not fixed on main or in 0.10.3. Fixed in the fork (`cppgm/0001`): `range_switch` took the selector's width from its storage, 8 bits for an `i1`, so the bound 2 wrapped to 0.
2. **A compare of a truncated `and` against zero** tests the whole mask, in instruction selection. `u16(i32(b & 2147483647) & 65535) == 0` compiles to `test $0x7fffffff`, so `b = 65536` gives false. The two wrong results in the first 15,000 random programs, both a lost division-by-zero trap, are this bug. No workaround: the mask is the program's own. Fixed on main.
3. **Four crashes on a `switch` with a case its selector can't reach**, in the inliner (`opt::clone_call`), jump threading, `opt::compact_body` and `opt::sink_values`. In chibi the inliner crash came from converting a `u8` to an enum whose members are all negative, and the translator now drops enum member ranges the source type can't reach. A `switch` on a narrow value still gets through: 7 of the second 15,000 programs crash this way. Not fixed on main or in 0.10.3. Fixed in the fork (`cppgm/0002`): all four have one cause, which `lowiropt --verify-passes` found. Switch formation dropped a case whose value the switched root never takes, so its target never counted as an exit of the region, and that target's phis kept naming a block the rewrite made unreachable. Each later pass that read the stale label crashed.
4. **A crash in `opt::remove_loops`** on a loop that stores a truncated value into a slot, in 4 of the first 15,000 random programs and 4 of the second. Fixed on main.
5. **A copy of an object value's first bytes copies the whole object**, in `split_aggregate_slots`, so the bytes after them lose what was stored there. Found on 2026-11-23 through the fork, in 1 of the 45,000 random programs: a struct built from part of a by-value argument lost a field's default. 0.10.2, main and 0.10.3 all have it. Fixed in the fork (`cppgm/0004`): when the pass gives an object value a slot of its own, the copies from the value keep their own size.

The second 15,000 also gave 0.10.2 one wrong result, which main doesn't give; it wasn't reduced. [bugs/reports.md](bugs/reports.md) drafts reports for what main still has. They won't be filed: on 2026-11-23 Charles chose to fix the bugs in a private fork instead.

## Code speed

Seven programs in `bench/`, each built four ways. "Checked" is the default. "Unchecked" turns off the overflow checks of `+`, `-`, `*` and negation and keeps the others (bounds, division, shifts, conversions): `--wrap` for LowIR, and for clang the C backend's output compiled with `-fwrapv` and `__builtin_{add,sub,mul}_overflow` defined as wrapping arithmetic. The table gives the fastest of 7 runs, in seconds, taken in rounds that run each build in turn so that a change in the machine's state hits every build alike. The ratio is clang's time over LowIR's, so above 1 means LowIR is faster. A first run, one build after another, had LowIR's sieve at 0.81 s checked and 0.62 s unchecked; the interleaved run doesn't reproduce that, and these numbers replace it.

| Program | What it stresses | clang checked | LowIR checked | Ratio | clang unchecked | LowIR unchecked | Ratio |
|---|---|---|---|---|---|---|---|
| `fib(40)` | calls | 0.467 | 0.185 | 2.52 | 0.250 | 0.133 | 1.88 |
| sieve to 20 million, 5 times | byte stores, bounds checks | 0.459 | 0.491 | 0.93 | 0.458 | 0.494 | 0.93 |
| longest Collatz chain below 3 million | `%`, `/`, `*` | 0.673 | 0.882 | 0.76 | 0.442 | 0.848 | 0.52 |
| 400×400 `int` matrix multiply, 6 times | multiply-add, bounds checks | 0.336 | 1.102 | 0.30 | 0.223 | 0.294 | 0.76 |
| spectral norm, n = 2000 | `f64`, calls | 0.411 | 0.516 | 0.80 | 0.338 | 0.335 | 1.01 |
| 4,096 points × 40,000 steps | 24-byte structs by value | 0.320 | 0.723 | 0.44 | 0.307 | 0.328 | 0.94 |
| FNV-1a over 1 MB, 300 times | wrapping arithmetic only | 0.336 | 0.335 | 1.00 | 0.336 | 0.336 | 1.00 |
| **Geometric mean** | | | | **0.79** | | | **0.94** |
| without `fib` | | | | 0.65 | | | 0.84 |

LowIR `-O1`, checked: 0.386, 0.507, 0.865, 2.300, 0.834, 1.071 and 0.334 s, about 0.7 of its `-O2` speed. LowIR from cppgm's main branch at 0025136 ran in the same rounds and matched 0.10.2 within 2% on every program, checked and unchecked.

Where the time goes, from the machine code:
- **Checks.** clang turns each `__builtin_*_overflow` into the operation and a `jo`. LowIR keeps the sign test as written: four more instructions after a checked add, and a one-operand `imul` with a compare of the high half for a checked multiply. Nor does it hoist an invariant check out of a loop: in the matrix multiply, the check on `i * n` runs in the innermost loop.
- **Peepholes.** `x % 2 == 0` on a signed `int` compiles to the full remainder sequence, five instructions, where clang tests one bit. That alone puts unchecked Collatz at 0.52 of clang.
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
- On code speed it reaches clang only where overflow checks don't run. With checks on, chibi's default, it runs at 0.65–0.8 of clang, between QBE's 0.6 and MIR's 0.9 of gcc. Closing that gap needs an overflow operation in LowIR, or instruction selection that fuses the sign test into a flag check, plus loop-invariant code motion of checks. That work is upstream, or in chibi's own optimizer.
- Its optimizer isn't trustworthy yet: on 0.10.2, two wrong-code bugs and five crash sites in one day of testing, all at `-O1` and up. Main fixed one of each within 15 hours, in 139 commits. That pace cuts both ways: bugs get fixed fast, and each version chibi adopts needs the random programs run against it first. A release backend that miscompiles a mask-and-compare is a risk chibi's users would carry.

- Hand-off 2 tried it on real code: the compiler built through LowIR passes the bootstrap check and the test suite, compiles 6.8× faster than through clang `-O2`, and runs at 0.95 of clang's speed with checks on, where the small programs gave 0.79.
- Since 2026-11-23 the fork fixes every bug the spike found, and the 45,000 random programs give no crashes and no wrong results at `-O2`. At step 3k.25 the compiler, now one program with 128-bit integers, builds through LowIR 6.4× faster than through clang `-O2` and runs at 0.93 of clang's speed at best and 0.89 at the median. What's left of the gap is code the optimizer could do better: arrays kept on the stack in a hot loop, a loop clang vectorizes, and the range of a bit count.

Not done: debug information from the IR's line table (step 3j.5); the M4 Pro; and the 15,000 hand-off 2 random programs at `-O0`.
