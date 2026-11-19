# Testable hypotheses

These are the claims chibi-v2's design depends on. Each is written so that a measurement can confirm or reject it. The evidence behind them is in [research/](research/README.md).

- A claim counts as measured on chibi-v2 only if its status says so.
- The thresholds are our own proposals, worked out from the budget below. Change a threshold only on purpose, and record why.

Each hypothesis states:
- the claim;
- why we think it is true;
- the test;
- the result that passes;
- what we do if it fails;
- its status.

## The budget

These numbers come from `make bench` on the owner's M4 Pro (10 performance and 4 efficiency cores) on 2026-11-09. The tree was commit `ae4d366` plus uncommitted changes.

| Measurement | Value |
|---|---|
| Lexer, 1 thread | 1,312 MB/s, 3.010 cycles/B |
| Lexer and parser, 1 thread | 633 MB/s, 6.276 cycles/B |
| Lexer, 14 threads | 17,502 MB/s (13.3×) |
| Lexer and parser, 14 threads | 7,568 MB/s (12.0×) |
| Clock speed these imply | 1,312 MB/s × 3.010 cycles/B ≈ 3.95 GHz |

What 1M lines per second allows:

- **On one core:** 3,950 cycles per line, for all phases together.
- **On 14 cores:** suppose a phase costs C cycles per line on one thread and runs S× faster on 14 threads. It then uses C ÷ S of the wall-clock budget. A serial phase uses its full cost. All phases together must fit in 3,950 wall-clock cycles per line. If serial work stays under 400 cycles per line and every other phase scales 12×, the other phases can spend about 42,000 cycles per line in total.
- **Bytes per line for chibi-v2 code are unknown.** Old Chibi code averaged 46.5 bytes and 9 tokens per line. At that length, lexing and parsing cost about 292 cycles per line on one thread, which is 7.4% of the single-core budget. On 14 threads they cost about 24 cycles per line.

A later profile ([front-end-optimizations.md](research/front-end-optimizations.md)) corrected two of these numbers:

- **Cost per line:** lexing and parsing cost 243 cycles per line on the generated programs, which average 41.8 bytes and 7.4 tokens per line.
- **Scaling:** the 14-thread figures above are inflated. The bench gives each thread about 590 KB, and the branch predictor learns that input when it repeats. When every thread gets the full corpus instead, lexing scales 11.2× and lexing plus parsing 10.0×. A real build parses each file once, so those lower figures are the honest ones.

Which budget applies is still undecided. It is the first decision in [research/README.md](research/README.md).

## Summary

| ID | Claim | Needs | Status |
|---|---|---|---|
| H1 | The generated test programs predict real code | the C corpus | untested |
| H2 | Lexing and parsing use under 10% of the single-core budget | real line lengths | holds on generated programs: 243 cycles per line |
| H3 | Every parallel phase runs at least 10× faster on 14 threads | each phase | in question: about 7–10× with separate input per thread |
| H4 | Serial work costs at most 400 cycles per line | the whole pipeline | untested |
| H5 | Checking costs at most 3,000 cycles per line and runs 10× faster on 14 threads | the checker | untested |
| H6 | The backend costs at most 1,000 cycles per line on one thread | an IR and an emitter | untested |
| H7 | A call through a slot table costs at most 1 cycle more than a direct call | nothing | **can test now** |
| H8 | A function can be replaced in a running program on macOS | nothing | **can test now** |
| H9 | A compile-time interpreter runs at least 10⁸ IR operations per second | a small test IR | passes on small loops: 706–1,679 Mops/s |
| H10 | Finding a generic instance takes constant time | generics | untested |
| H11 | Using a handle costs at most 2 cycles more than using a pointer | nothing | **can test now** |
| H12 | Migrating 10⁶ live objects after a layout change takes one frame or less | nothing | **can test now** |
| H13 | Computing error sets after checking costs at most 40 cycles per line | the checker | untested |
| H14 | Inferring return types adds at most 3% to parallel checking | the checker | untested |
| H15 | Merging the per-thread name tables costs at most 50 cycles per line | nothing | holds on generated programs: 8–11 cycles per line |
| H16 | One forward pass over the postorder nodes checks the core subset | the checker | untested; the bookkeeping alone costs 149 cycles per line |
| H17 | The typed IR has at most 6 instructions and 80 bytes per line | the checker | untested |
| H18 | Tail-call dispatch is at least as fast as computed goto on real compile-time code | compile-time code | fastest on 3 of 4 small loops |

H7, H8, H9, H11 and H12 don't depend on the parser, so they can be tested while the parser is being written. Each test is a small program to throw away, kept in `experiments/` and not in the compiler.

## Front end

### H1. The generated test programs predict real code

- **Claim:** on the programs that `test/programs.c` generates, lexing and parsing cost within 20% of what they cost per byte on real code.
- **Why it matters:** every front-end optimization is tuned on the generated programs. If those differ from real code in token mix, line length or nesting, the tuning targets the wrong input.
- **Test:** once the C-to-chibi corpus exists, run the benchmark on it. Compare cycles per byte, tokens per line and bytes per line.
- **Passes if:** cycles per byte differ by at most 20%, for lexing and for parsing separately.
- **If it fails:** benchmark on the real corpus, and use the generated programs only for fuzzing.
- **Status:** untested.

### H2. Lexing and parsing use under 10% of the single-core budget

- **Claim:** lexing and parsing together cost at most 395 cycles per line on one thread.
- **Why we think so:** they cost 6.276 cycles per byte (measured). At 46.5 bytes per line, that is about 292 cycles per line.
- **Test:** multiply cycles per byte by the real corpus's bytes per line.
- **Passes if:** the result is at most 395 cycles per line.
- **If it fails:** optimize the front end further before building the later phases on it.
- **Status:** holds on the generated programs, at 243 cycles per line (lexer 106, parser 135). Measured at their real line length of 41.8 bytes ([front-end-optimizations.md](research/front-end-optimizations.md)).

### H3. Every parallel phase runs at least 10× faster on 14 threads

- **Claim:** every phase that works on separate files, declarations or functions runs at least 10× faster on 14 threads than on 1.
- **Why we think so:** with every thread given the full corpus, lexing scaled 11.2× and lexing plus parsing 10.0× (measured). The bench's own split reports 13.3× and 12.0×, but the branch predictor inflates those. Odin's parallel checker and Go's per-function backend are built the same way ([packages.md](research/packages.md), [fast-compilers.md](research/fast-compilers.md)).
- **Test:** each phase's benchmark reports its throughput on 1 thread and on 14.
- **Passes if:** every phase reaches at least 10×.
- **If it fails:** find the shared resource that serializes the threads, such as a lock, an atomic counter, a shared table or memory bandwidth, and split it per thread.
- **Status:** in question. Since 2026-11-09 the bench gives each thread its own 1,600 generated programs. In 4 PGO runs, lexing scaled 7.4–9.9× and lexing plus parsing 8.9–10.3×. The multi-thread runs are noisy: background agents were running, and the 4 efficiency cores are slower than the 10 performance cores. Agree on the measurement method (median of several runs, an idle machine) before deciding whether 10× is the right threshold for 10 performance plus 4 efficiency cores.

### H4. Serial work costs at most 400 cycles per line

- **Claim:** work that can't run in parallel costs at most 400 cycles per line, 10% of the wall-clock budget. Examples are putting outputs in order, building the final image and updating global tables.
- **Why we think so:** extra cores don't shorten serial work (Amdahl's law), so it takes its full cost from the budget. Zig sends all code-generation output through one ordered link thread ([fast-compilers.md](research/fast-compilers.md), section 3.2).
- **Test:** time each serial step on the corpus.
- **Passes if:** the serial steps together cost at most 400 cycles per line.
- **If it fails:** run the program from memory so no files are written or linked, or split the serial step into parallel parts and merge them in a fixed order.
- **Status:** untested.

### H5. Checking costs at most 3,000 cycles per line and runs 10× faster on 14 threads

- **Claim:** type checking costs at most 3,000 cycles per line on one thread. It runs at least 10× faster on 14 threads when each thread claims declarations with an atomic not-started / in-progress / done mark.
- **Why we think so:** Carbon measured about 750 ns per line for checking on one core. At 3.95 GHz that is about 3,000 cycles, though Carbon ran on different hardware ([fast-compilers.md](research/fast-compilers.md), section 6). Odin's checker claims declarations with an atomic compare-and-swap ([packages.md](research/packages.md)).
- **Test:** benchmark the checker on the corpus, on 1 thread and on 14.
- **Passes if:** it costs at most 3,000 cycles per line on 1 thread and runs at least 10× faster on 14.
- **If it fails:** check only the declarations the program reaches from its roots, as Zig does. Then look for contention between threads.
- **Status:** untested. It needs the checker.

### H6. The backend costs at most 1,000 cycles per line on one thread

- **Claim:** a one-pass emitter in the style of tcc and TPDE, working from a compact IR for each function, turns IR into arm64 bytes with relocations. It costs at most 1,000 cycles per source line on one thread.
- **Why we think so:** TPDE's one-pass backend is 12–19× faster than LLVM -O0. tcc runs its whole pipeline in about 2,800 cycles per line on a 2004 Pentium 4 ([fast-compilers.md](research/fast-compilers.md), sections 3.8 and 6).
- **Test:** measure cycles per IR instruction and IR instructions per source line. Their product is the cost per line.
- **Passes if:** the product is at most 1,000 cycles per line.
- **If it fails:** find which part costs the most: instruction selection, register allocation or encoding. Then compare that part with tcc's value-stack approach.
- **Status:** untested. It needs an IR.

### H13. Computing error sets after checking costs at most 40 cycles per line

- **Claim:** function bodies are checked in parallel without error sets. Afterwards, one pass over the call graph computes every inferred error set and runs the checks that need sets, such as exhaustive switches over errors. The pass and those checks cost at most 40 cycles per line on one thread, a tenth of H4's serial budget.
- **Why we think so:** an estimate, not a measurement. 1M lines hold about 50,000 functions and at most about 100,000 calls that pass errors up. Grouping functions that call each other (Tarjan's algorithm) is linear and takes under 1 ms. One union of small sorted sets per call, at about 30 ns, takes about 3 ms. The later checks take under 1 ms. That is about 5 ms, or 20 cycles per line at 3.95 GHz. Resolving sets while checking, as Zig does, saves recording the call edges and the grouping step, about 6 cycles per line. In exchange, checking a function can wait for a callee's body whenever it needs that callee's set.
- **Test:** on the corpus, time the pass and the later checks, and count functions, call edges and set sizes. To compare with Zig's approach, count how often checking would need a callee's set before that callee is checked.
- **Passes if:** the pass and the later checks cost at most 40 cycles per line together.
- **If it fails:** run the pass in parallel, one level of the grouped call graph at a time, or store sets as bitsets over the program's error names.
- **Status:** untested. It needs the checker.

### H14. Inferring return types adds at most 3% to parallel checking

- **Claim:** when a private function leaves out `->`, a caller that needs its return type checks that function's body itself, or waits for the thread that has claimed it. `pub` functions write `->` (decided 2026-11-11), so such chains stay inside one package. These waits, and the longest chain of functions whose types depend on each other, add at most 3% to checking time on 14 threads, compared with the same program with every `->` written.
- **Why we think so:** an estimate, not a measurement. At H5's 3,000 cycles per line, 1M lines take about 3×10⁹ cycles, or about 54 ms on 14 threads at 3.95 GHz. A 20-line function takes about 60,000 cycles, or 15 µs. A chain of 100 inferred functions, each needing the next, adds about 1.5 ms, under 3%. A wait happens only when two threads need the same unchecked function at the same moment, and it lasts at most one function's check.
- **Test:** check the corpus twice, once with every `->` written and once with `->` left out on private functions wherever allowed. Count the waits, their total time and the longest chain, on 1 and 14 threads.
- **Passes if:** checking with inferred return types is at most 3% slower on 14 threads.
- **If it fails:** check callees first. One pass over the parser's call nodes builds a call graph by interned name; a UFCS call adds an edge to every function with that name, which over-approximates but is enough for ordering. Workers then take functions in that order, in batches such as one file's functions, from work-stealing queues (the owner's scheduling direction, 2026-11-11).
- **Status:** untested. It needs the checker. A cycle between two waiting threads, which only mutual recursion with inferred types can cause, must be detected while waiting.

### H15. Merging the per-thread name tables costs at most 50 cycles per line

- **Claim:** each lexer thread interns names into its own table. After parsing, one thread merges the tables into one, so every file gets the same id for the same name ([checker-and-ir.md](research/checker-and-ir.md) section 2.1). The merge costs at most 50 cycles per line, an eighth of H4's serial budget.
- **Why we think so:** measured on 2026-11-12 with 14 tables from generated programs, 2.2M lines and 88,000–98,000 distinct names: 4.4–6.2 ms, or 40–58 ns per name per table, which is 8–11 cycles per line. Real code repeats names across files more often, so each table holds more names. That raises the cost, but not past 50 cycles per line unless each table holds most of the program's names.
- **Test:** merge the tables of the C-to-chibi corpus. Report names per table, distinct names and cycles per line.
- **Passes if:** the merge costs at most 50 cycles per line.
- **If it fails:** split the merge into shards by hash bits, one thread per shard, or intern into one shared table while lexing.
- **Status:** holds on generated programs.

### H16. One forward pass over the postorder nodes checks the core subset

- **Claim:** the checker reads each function's nodes once, in order, with an operand stack, and builds no tree. The one exception is a `defer` body, which it reads again at each exit from its block ([checker-and-ir.md](research/checker-and-ir.md) section 1).
- **Why we think so:** the parser's markers arrive where the checker needs context, and Carbon's checker reads its postorder parse tree the same way. A plain walk that does the bookkeeping but no type rules costs 149 cycles per line on generated programs (measured, [experiments/check-floor](../experiments/check-floor/README.md)).
- **Test:** count the nodes the checker reads per node in the input, over the test suite and the benchmark.
- **Passes if:** outside `defer` bodies, the checker reads each node once.
- **If it fails:** ask the parser for the missing marker, or record the node range of the construct that needs a second look.
- **Status:** untested. It needs the checker.

### H17. The typed IR has at most 6 instructions and 80 bytes per line

- **Claim:** for the core subset, the checker writes at most 6 IR instructions per source line, at 13 bytes each plus side arrays, for at most 80 bytes per line.
- **Why we think so:** generated programs have 6.3 nodes per line, of which 1.2 are markers. The bookkeeping walk writes 5.1 records per line (measured). Constants become operands rather than instructions, which removes some records, and loads of locals add some.
- **Test:** count instructions and bytes per line over the benchmark.
- **Passes if:** both stay under their limits.
- **If it fails:** find the constructs that expand most, and fold the common ones into single instructions.
- **Status:** untested. It needs the checker.

## Can test now

### H7. A call through a slot table costs at most 1 cycle more than a direct call

- **Claim:** in development builds, every function is called through a slot table: the program loads the function's address from the table and calls that address. When the address stays the same, this costs at most 1 cycle more per call than a direct call.
- **Why we think so:** hot reload needs one place per function where the target can be changed. Zig and MIR both call through such a layer of indirection ([metaprogramming-reload-coroutines.md](research/metaprogramming-reload-coroutines.md), section 6).
- **Test:** call a small, non-inlined function 10⁹ times in a loop on one core, once directly and once through a table. Report cycles per call.
- **Passes if:** the table costs at most 1 extra cycle per call.
- **If it fails:** at reload, patch the call instructions in place instead of keeping a table.
- **Status:** untested. Can test now.

### H8. A function can be replaced in a running program on macOS

- **Claim:** on macOS arm64, a running program can write new machine code for a function into `MAP_JIT` memory. At a safe point it can then switch the function's slot to the new code, without restarting.
- **Why we think so:** Apple documents the steps: allocate with `MAP_JIT`, switch the memory between writable and executable with `pthread_jit_write_protect_np`, and flush the instruction cache with `sys_icache_invalidate` ([Apple: Porting just-in-time compilers to Apple silicon](https://developer.apple.com/documentation/apple-silicon/porting-just-in-time-compilers-to-apple-silicon)).
- **Test:** a loop calls `step()` once per frame. Between frames, write a new `step` and update its slot. Run the test both unsigned and as a signed build with Hardened Runtime.
- **Passes if:** the next frame runs the new code, and the old code stays valid until no frame can return into it.
- **If it fails:** reload a whole module with `dlopen`, as Handmade Hero and Odin's hot-reload template do.
- **Status:** untested. Can test now.

### H9. A compile-time interpreter runs at least 10⁸ IR operations per second

- **Claim:** an interpreter over a typed, flat IR, which stores values as plain bytes in the target's layout, runs at least 10⁸ simple IR operations per second on one core. That is about 40 cycles per operation.
- **Why we think so:** in 2023, Zig's compile-time code ran about 5×10⁵ loop iterations per second, because every value goes through its intern pool ([metaprogramming-reload-coroutines.md](research/metaprogramming-reload-coroutines.md), section 1.3). The same note estimates that code doing heavy compile-time work needs about 10⁸ operations per second (section 6). That figure is an estimate, not a measurement.
- **Test:** build a small test IR with integer add, compare, branch, load, store and call. Run a loop of 10⁸ iterations. Report operations per second and cycles per operation.
- **Passes if:** it runs at least 10⁸ operations per second.
- **If it fails:** compile compile-time functions with the debug backend and call them directly.
- **Status:** passes on small test programs ([experiments/h9-interpreter](../experiments/h9-interpreter/README.md), 2026-11-09). The slowest row ran 706 Mops/s at 5.6 cycles per operation (switch dispatch, recursive `fib`). Threaded dispatch ran 911–1,679 Mops/s. These loops are small enough for the branch predictor to learn, so this is a best case. Repeat the test with real compile-time code (C1).

### H11. Using a handle costs at most 2 cycles more than using a pointer

- **Claim:** using a generational handle means indexing into a pool, loading the slot's generation and comparing it with the handle's. In a hot loop where the check always passes, this costs at most 2 cycles more than loading through a raw pointer.
- **Why we think so:** memory design A addresses objects with handles made of a 32-bit index and a 32-bit generation ([memory-models.md](research/memory-models.md), section 9).
- **Test:** sum one field over 10⁶ objects, once through pointers and once through handles. Visit the objects in order, then in random order. Report cycles per element.
- **Passes if:** handles cost at most 2 extra cycles per element in both orders.
- **If it fails:** check generations only in debug builds, or check once per batch.
- **Status:** untested. Can test now.

### H12. Migrating 10⁶ live objects after a layout change takes one frame or less

- **Claim:** at a reload where a struct's layout changes, the runtime copies every live object of that type from the old layout to the new one. It works field by field from a table of fields. For 10⁶ objects on one core, this takes at most 16 ms, one frame at 60 Hz.
- **Why we think so:** under memory design A, all objects of a type live in one pool, so migrating them is one linear pass ([memory-models.md](research/memory-models.md), section 9).
- **Test:** use a 64-byte struct with 8 fields. The new layout adds one field and swaps the order of two others. Migrate 10⁶ objects with a table that matches fields by name and lists each one's old offset, new offset and size.
- **Passes if:** the migration takes at most 16 ms.
- **If it fails:** spread the migration over several frames, or generate a copy function for each pair of old and new types at reload.
- **Status:** untested. Can test now.

## Later

### H10. Finding a generic instance takes constant time

- **Claim:** looking up an existing generic instance costs about the same, within 2×, whether there are 1, 10³ or 10⁵ instances of one generic.
- **Why we think so:** Odin scans a list of earlier instances for each generic, so a lookup slows down as the list grows. Zig interns instances by key instead ([metaprogramming-reload-coroutines.md](research/metaprogramming-reload-coroutines.md), sections 2.2 and 3.2). A hash table keyed by the generic declaration's ID and the argument type IDs avoids the scan.
- **Test:** generate programs with that many different instances, and time each lookup.
- **Passes if:** the time per lookup stays within 2× across the three sizes.
- **If it fails:** make sure the key compares interned IDs, not whole types.
- **Status:** untested. It needs generics.

### H18. Tail-call dispatch is at least as fast as computed goto on real compile-time code

- **Claim:** an interpreter whose handlers end in guaranteed tail calls, with a register-heavy calling convention, takes no more cycles per operation than the same interpreter with computed goto, on real compile-time code.
- **Why we think so:** on H9's small loops, tail calls with `preserve_none` took about 5% fewer cycles than computed goto on two loops and about 17% fewer on deep recursion; a call-heavy loop varied by up to 40% with code layout under every dispatch style (measured 2026-11-12, [experiments/h9-interpreter](../experiments/h9-interpreter/README.md)). Small loops let the branch predictor learn every dispatch, so they are a best case for every style.
- **Test:** run C1's table building and larger, less regular programs under switch, computed goto and tail-call dispatch. Report cycles per operation and the spread across builds.
- **Passes if:** tail-call dispatch is no slower than computed goto on every program.
- **If it fails:** keep computed goto for chibi-v2's own interpreter. The language feature in section 6 of [checker-and-ir.md](research/checker-and-ir.md) still matters for interpreters written in chibi.
- **Status:** fastest on 3 of 4 small loops.

## C interop

chibi and C call each other in both directions ([decisions.md](decisions.md)). These hypotheses are new and untested. Their thresholds are proposals.

| ID | Claim | Needs | Status |
|---|---|---|---|
| I1 | The development backend follows the C calling convention in both directions, for every case in a generated test suite | a backend | untested |
| I2 | A call between chibi and C costs no more than a C-to-C call through a function pointer | a backend | untested |
| I3 | A cached import of SDL3's headers costs at most 1 ms per build, and is redone exactly when a header changes | the bindings generator | untested |
| I4 | A C library that holds a chibi callback calls the newest code after a hot reload, without registering it again | H8 | untested |
| I5 | Generated C headers check every exported type's size and field offsets at C compile time | header generation | untested |

### I1. The development backend follows the C calling convention

- **Claim:** for every signature in a generated test suite, chibi calling C and C calling chibi pass arguments and results exactly as clang does. The suite covers integers, floats, structs up to 16 bytes, structs of two to four floats (passed in float registers on arm64), larger structs (passed by reference) and variadic calls.
- **Why it matters:** Cute Framework passes small vector structs such as `CF_V2 {float x, y}` by value. These are the cases calling conventions treat specially, and where bugs corrupt values silently. Apple's arm64 convention also differs from the standard one: it passes variadic arguments on the stack.
- **Test:** generate pairs of C and chibi functions for each signature. Each side calls the other and checks every argument and the result. Run them on macOS arm64, then on x86-64 System V and Windows x64.
- **Passes if:** every case passes on every target.
- **If it fails:** the failing case shows which convention rule is wrong.
- **Status:** untested. It needs a backend. In release builds, the C backend gets this right through clang, so a differential test between the two backends also catches these bugs.

### I2. A call between chibi and C costs no more than a C-to-C call

- **Claim:** a call from chibi into C, or from C into chibi, costs at most 1 cycle more than a C-to-C call through a function pointer. Nothing is converted or copied at the boundary, and the call doesn't switch stacks.
- **Why we think so:** with C struct layout, no garbage collector (memory design A) and stackless coroutines, chibi code runs on the ordinary native stack, just as C does. Go's cgo is the counterexample: each call switches from the goroutine's stack to a system stack.
- **Test:** call a small function 10⁹ times in each direction, and compare with C calling C through a function pointer.
- **Passes if:** each direction costs at most 1 extra cycle per call.
- **If it fails:** find which part of the runtime the boundary touches, such as an implicit context, a hot-reload slot or a task check.
- **Status:** untested.

### I3. A cached import of SDL3's headers costs at most 1 ms per build

- **Claim:** `import "SDL3/SDL.h"` (a path ending in `.h` is a C header, settled 2026-11-10) produces chibi declarations once, through clang, and caches them under the content hashes of every header clang read. After that, a build that changes no header pays at most 1 ms for the import, and changing any header that was read redoes it.
- **Why we think so:** Carbon parses C++ headers with an embedded Clang. Its own goals document warns that code using interop "will see bottlenecks from C++ compile time" ([philosophy_and_goals.md](https://github.com/carbon-language/carbon-lang/blob/19f1bf94dbb9b13f0595db32ce82e0a4ad19f782/docs/design/interoperability/philosophy_and_goals.md)). A cache keyed by recorded inputs avoids paying that on every build.
- **Test:** time a build that imports SDL3 three ways: cold, cached, and after touching one header.
- **Passes if:** the cached import costs at most 1 ms, and the touched header causes exactly one regeneration.
- **If it fails:** profile the cache lookup, then check whether the recorded header list is complete.
- **Status:** untested. It needs the bindings generator, which can be the corpus translator ([c-corpus.md](research/c-corpus.md)) running in declarations-only mode.

### I4. C libraries keep working across a hot reload

- **Claim:** when a C library such as SDL holds a pointer to a chibi function, it calls the newest version of that function after a hot reload, without registering the callback again.
- **Why we think so:** if every exported or address-taken function gets a fixed stub that jumps through its slot (H7, H8), the address C holds never changes.
- **Test:** a C test library stores a chibi callback and calls it once per frame. Reload a new version of the callback between frames.
- **Passes if:** the next frame runs the new code, and the stub adds at most 1 cycle per call.
- **If it fails:** re-register callbacks at reload, through a hook that the runtime calls.
- **Status:** untested. It needs H8.

### I5. Generated headers check layout at C compile time

- **Claim:** the C header that chibi generates for its exported functions and types contains `static_assert`s for every type's size, alignment and field offsets. A layout mismatch between chibi and the C compiler then fails the C build instead of corrupting data at run time.
- **Why we think so:** rust-bindgen generates layout tests for the same reason.
- **Test:** generate a header for test types, compile it with clang `-std=c11 -Wall -Wextra -Werror`, then change one field's type on one side only.
- **Passes if:** the unchanged header compiles, and the mismatch fails to compile, naming the type and field.
- **If it fails:** emit more of the asserts, for example padding or enum sizes.
- **Status:** untested. It needs header generation.

## Hypotheses from the research notes

Each of these is written out in full, with its test and pass condition, in the note it links to. They are listed here so that every hypothesis has one index. Their thresholds are proposals until measured.

### Front end: [front-end-optimizations.md](research/front-end-optimizations.md)

These come from profiling the working copy as of 2026-11-09, with the generated programs. The estimated gains are the note's own estimates.

| ID | Claim | Needs | Status |
|---|---|---|---|
| F1 | A branch-free core for common expression tokens cuts parse cost by 10% | nothing | **rejected**: the parser thread measured a branch-free table automaton at +91% (2026-11-09) |
| F2 | Writing every token unconditionally cuts the lexer by 4% | nothing | estimated |
| F3 | A shorter fast path for interning cuts the lexer by 4% | nothing | estimated |
| F4 | One shared byte classification cuts the lexer by 3% | nothing | estimated |
| F5 | Declarations that start with a keyword cut parse cost by up to 3% | a syntax decision | estimated |
| F6 | A line-start bit in each token removes the parser's loads from the line bitmap | a syntax decision | estimated |
| F7 | Dropping three-byte operators costs nothing and removes code | a syntax decision | estimated |

### Compile-time execution, generics and reflection: [comptime-generics-reflection.md](research/comptime-generics-reflection.md)

| ID | Claim | Needs | Status |
|---|---|---|---|
| C1 | A compile-time interpreter builds every lexer and parser table in at most 10 ms | comptime | untested |
| C2 | Checking each generic instance separately re-checks at most 0.5 extra nodes per source node | generics | untested |
| C3 | Merging identical instance bodies would save under 10% of debug machine code | generics, backend | untested |
| C4 | Generated declarations whose names are declared up front keep checking 10× parallel | comptime, checker | untested |
| C5 | Output and diagnostics don't depend on analysis order or thread count | checker | untested |
| C6 | Runtime type info for the types a program reaches costs at most 5% of a debug image | reflection, backend | untested |
| C7 | Type IDs built from a path hash plus a layout hash detect every layout change, and nothing else | reflection | untested |
| C8 | After a one-function edit, at most 1% of compile-time results and instances are redone | comptime, hot reload | untested |
| C9 | A format string can be checked at compile time without a code instance per string | comptime | untested |

### Corpus and translation: [c-corpus.md](research/c-corpus.md)

| ID | Claim | Needs | Status |
|---|---|---|---|
| T1 | Rewriting C token by token predicts the lexer cost of a full translation | the token rewrite | untested |
| T2 | Translation keeps line counts honest | the translator | untested |
| T3 | clang on macOS covers at least 1M lines of the codebase mix | the translator | untested |
| T4 | Most macro uses keep their names | the translator | untested |
| T5 | Game code and library code cost the same per byte to lex and parse | the corpus | untested |
| T6 | Translated C predicts idiomatic chibi in every phase | idiomatic programs | untested |
| T7 | chibi-v2's own C translates mechanically | the translator | untested |
| T8 | The translated compiler runs within 10% of the C build's speed | the translator, a release backend | untested; written out below |

### T8. The translated compiler runs within 10% of the C build's speed

- **Claim:** chibi-v2's C, translated to chibi and compiled by chibi's release backend, lexes and parses the bench programs within 10% of the C build's cycles per line, and produces the same fingerprint.
- **Why it matters:** Go's first translated compiler ran about 10× slower, and Go 1.5 still built about 2× slower ([c-corpus.md](research/c-corpus.md), section 1.1). The causes were language costs that C doesn't have, plus a weaker backend. These are not inevitable for chibi: see the conditions below.
- **Conditions:** chibi needs:
  - SIMD vector types and the intrinsics the lexer uses;
  - no hidden bounds checks or allocation in release builds;
  - unions with C's layout;
  - integer semantics that let the backend keep C's fast paths;
  - a release backend that optimizes about as well as clang -O3.
- **Test:** build the bench from the C source and from the translation, run both on the same machine, and compare cycles per line and the fingerprint.
- **Passes if:** the fingerprints match, and cycles per line are within 10% for lexing and for parsing.
- **If it fails:** profile both builds, then find which language rule or backend pass costs the difference.
- **Status:** untested. It needs the translator and a release backend.
