# What chibi's optimizer can take from LowIR

Research note, 2026-11-23. Question (owner, 2026-11-23): "I'm interested in using LowIR, but, am also interested in translating its ideas to our own compiler for chibi." This note measures which of LowIR's optimizations matter on chibi's own code, where LowIR still trails clang on that code, and what an optimizer of chibi's own should take, change or leave out. The LowIR spike itself is in [../../experiments/lowir-spike/README.md](../../experiments/lowir-spike/README.md); the backend survey that chose LowIR is [optimizing-backends.md](optimizing-backends.md).

**Method.** The program is the compiler at commit 3b59582 (step 3t.16), as the one 36,472-line chibi program that `make translated` writes, built through cppgm's LowIR with chibi's fork of cppgm (0.10.3 and the patches in `experiments/lowir-spike/cppgm/`). The workload is that compiler writing its own source as C (`c -o /dev/null build/translated/entry.chibi build/translated/host/*.chibi`). Callgrind counts the instructions run in the compiler's own code; the C library's `printf` and `putc` add 341 million to every build and don't change with the optimizer, so they are left out. To price each pass, a private copy of the fork skips one named pass at a time (an environment variable read in `run_pass`; it isn't in the fork and won't be). Every variant still writes the same C, byte for byte. One pass off at a time understates passes that cover for each other, so the table below ranks what each pass adds on top of the rest, not what it would add alone.

| Build of the compiler | Instructions in its own code |
|---|---|
| clang `-O2` (the C translation) | 217.2 M |
| LowIR `-O0` | 645.4 M |
| LowIR `-O1` | 277.2 M |
| LowIR `-O2` | 259.3 M (+19.4% over clang) |
| LowIR `-O3` | 254.9 M |

On the 200-file corpus the gap is smaller: 3,233.5 M instructions in the compiler's own code against clang's 2,829.5 M (+14.3%), or +4.5% with the C library counted, against +5.3% at step 3k.25.

---

## Summary

- **Five kinds of work make LowIR's code fast on chibi's code:** inlining, splitting aggregates into scalars, value simplification, jump threading and store-to-load forwarding. Turning off any one of them costs from 9.8% to 57%. No other pass costs more than 6% when it is off, and 29 of the 42 cost under 1% each.
- **Most of the remaining 19% gap to clang is the lexer and one loop.** `lexer_tokenize` runs 20.6 M more instructions than clang's, about three quarters of them loads and stores to the stack, where LowIR moves 16-byte vectors in 64-bit words. `line_at` runs 8.3 M more, because clang vectorizes its newline count and LowIR doesn't.
- **chibi's IR already knows much of what LowIR has to rediscover:** loop structure, each slot's type and whether its address is taken, `@inline` marks, and overflow tests as single instructions. An optimizer of chibi's own should start from those facts and add the value ranges LowIR lacks.
- **LowIR's bugs and process point to three rules:** compute every integer result at its instruction's width, verify the IR after every pass, and run the random programs against the reference interpreter at every optimization level from the first pass.

---

## 1. What each pass is worth on the compiler

Each row turns off one pass (or one group) of LowIR `-O2` and shows the compiler's instruction count, against 259.3 M with every pass on.

| Turned off | Instructions | Change |
|---|---|---|
| All three inlining waves | 408.1 M | +57.4% |
| Aggregate splitting and slot promotion | 327.7 M | +26.4% |
| Aggregate splitting (`split_aggregate_slots`) | 322.0 M | +24.2% |
| The ordinary inlining wave | 293.8 M | +13.3% |
| Value simplification (`simplify_values`) | 293.0 M | +13.0% |
| Jump threading (`thread_jumps`) | 291.8 M | +12.5% |
| Store-to-load forwarding (`forward_memory`) | 284.7 M | +9.8% |
| Dead code removal | 274.6 M | +5.9% |
| CFG cleanup | 269.8 M | +4.0% |
| The late inlining wave | 266.7 M | +2.9% |
| Slot promotion alone (`promote_slots`) | 265.9 M | +2.5% |
| Copy coalescing | 265.4 M | +2.3% |
| 128-bit operations as pairs of 64-bit ones (`expand_wide_integers`) | 264.9 M | +2.1% |
| Select formation | 264.5 M | +2.0% |
| The nine loop passes that run at `-O2` | 262.5 M | +1.2% |
| Short-loop unrolling | 262.1 M | +1.1% |
| Argument specialization, switch formation | 261.3 M each | +0.7% |
| Vector constants, value sinking, address sinking | 260.0–260.8 M | +0.3–0.6% |
| 23 other passes, constant propagation among them | 259.1–259.8 M | within 0.2% |
| Tail-recursion elimination | 257.9 M | −0.6% |

- **Inlining is the pass the others feed on.** It gives every later pass larger bodies to work on, which also makes the build take 1.56 s instead of 0.72 s. LowIR runs it in three waves, ordinary (with `@inline` callees forced, which the spike's translator passes since e3e1136), callees with one caller, and late, under budgets for the whole unit set to a multiple of its input size.
- **Aggregate splitting matters more than promotion.** Splitting is worth 24%, because chibi's IR keeps every struct and array of more than two scalars in a slot (the Compiler row of 2026-11-15, option C). Slot promotion alone is worth only 2.5%, though the translator gives every scalar local a slot: with promotion off, most scalar slots still reach registers some other way, which wasn't traced.
- **Constant propagation is free to drop on this code.** `simplify_values` already folds what `propagate_constants` finds: turning the latter off lowers the count by 33,000 instructions, and it takes 83 ms of the optimizer's 933 ms.
- **Tail-recursion elimination made the compiler slower** by 1.5 M instructions. A pass that pays on benchmarks can lose on real code, so each pass needs a measurement on the compiler before it's kept.

## 2. Where LowIR still trails clang

The 42.1 M instructions between LowIR `-O2` and clang `-O2`, by function:

| Function | clang | LowIR | Difference |
|---|---|---|---|
| `lexer_tokenize` | 35.7 M | 56.3 M | +20.6 M |
| `line_at` | 6.3 M | 14.6 M | +8.3 M |
| `put_line` | 28.5 M | 32.9 M | +4.4 M |
| `intern_names` | 8.0 M | 11.3 M | +3.3 M |
| The rest, spread over functions the two builds inline differently | | | +5.5 M |

**The lexer's 64-byte blocks.** The code that runs once per block (about 30,000 blocks) is about 1,150 instructions in LowIR's build and 630 in clang's, and about 600 of LowIR's are loads and stores to the stack, against about 70 of clang's. The translated lexer holds each 16-byte vector as a `[16]u8` array, and each 64-byte block as a struct of four. LowIR splits a 16-byte array into two 64-bit words and moves the words through the stack between vector operations: in `lowiropt`'s output, `lexer_tokenize` still has 172 slots of 16 bytes. clang keeps an array that is only loaded, stored and passed to vector operations in one vector register. Copying 16-byte arrays with vector loads and stores in the spike's translator changed nothing (259.6 M), because the copies that matter come from calls that return arrays by value, which reach LowIR as object values; the fix belongs in the optimizer.

**The lexer's tokens.** Each token (446,517 of them) runs 44 instructions through LowIR and 34 through clang. LowIR keeps three tests clang drops, all from facts about values:
- `name_last >> pos` checks that `pos < 64`, but `pos` is a trailing-zero count of a nonzero value.
- `trailing_zeros(name_last >> pos)` tests for zero, but `name_last` has bit 63 set, so the shifted value never is.
- `nnames += 1 or 0` tests for overflow in five instructions, right after the bounds check `nnames < 2049` that rules it out.

**`line_at`'s newline count** (`n +%= if src[p] == '\n' then 1 else 0` over a byte range) is a reduction clang vectorizes with `pcmpeqb`. LowIR's vectorizer runs only at `-O3`, and there it vectorizes no loop in any of the compiler's 209 functions that have one.

## 3. Ideas to take, in order

1. **Aggregate splitting that knows vectors.** This is the pass chibi's IR depends on most, since structs and arrays live in slots. Split a slot into fields when every access names a field at a fixed offset, and keep a field whose accesses are vector loads, stores and operations as one vector value, never as 64-bit words. LowIR's design notes add the limits: bound the partitioning work, keep an indexed region in memory while promoting the fields beside it, and don't split every field of a large object that a loop updates, since more loop-carried values than registers turns memory traffic into spills (cppgm's `pa33/design-notes.md`, "Scalar and control-flow simplification").
2. **Inlining in waves, with a growth budget for the whole unit.** Forced and ordinary callees first, then callees with one caller, then a late wave. Compare a callee's size before expansion, since earlier inlining grows it; inline a short returning path around a large body even when the whole body doesn't fit (the same notes, "Calls and exceptions").
3. **One value-simplification pass**: constant folding at the instruction's width, identities, and reuse of a pure value where its definition dominates. Fold constant propagation into it rather than running both.
4. **Jump threading.** It is worth 12.5% on the compiler; which branches it removes there wasn't traced.
5. **Store-to-load forwarding**, with the overwrite proof the notes describe: the same address and access type name the same bytes even when the offset is unknown.
6. **Value ranges from the source.** LowIR has ranges only for loop counters. chibi's IR can give every value a range from its type, from builtins (a bit count of a `u64` is 0 to 64, and 0 to 63 for a nonzero value), from a passed bounds check (the index is below the length afterward), and from enum members. Those ranges drop the lexer's three extra tests per token. The spike now tells LowIR one of them by hand: it skips the range check when it narrows a bit count.
7. **Overflow tests that stay single instructions.** chibi's IR states an overflow test as one instruction (`add-overflows` and its kin); LowIR has none, so a checked add reaches it as an explicit sign test of five instructions. Keep the test whole through the optimizer, so that instruction selection can use the carry and overflow flags, and so that ranges can drop it. The spike measured checks costing LowIR 1.51× on small programs, against 1.27× for clang.
8. **Skip a pass that already found nothing.** LowIR's `run_pass` stamps each function with a version that every change advances, and skips a pass that reported no change at the current version. It's cheap and keeps fixed-point rounds from repeating work.

The smaller passes worth 1–2% each, copy coalescing, select formation, 128-bit expansion and short-loop unrolling, can wait until the eight above are measured on chibi's code.

## 4. What chibi's IR already knows

LowIR's optimizer spends much of its code finding facts that chibi's checker has already written down:
- **Loops.** The IR keeps `block`, `loop`, `if` and `end` structure, so loops, their entries and their exits need no analysis.
- **Slots.** The checker records each slot's type, whether its address is taken, and its loads and stores weighted by loop depth (the Compiler row of 2026-11-15). Splitting and promotion can start from that.
- **Inlining marks.** `@inline` and `@noinline` come from the source.
- **Checks.** Bounds, nil, alignment and overflow checks are IR instructions of their own, not branches to recognize.
- **No exceptions.** LowIR's design notes and passes account for exception edges throughout; chibi has none.

## 5. What to leave out for now

- **LowIR's `-O3` loop passes**: rotation, unswitching, interchange, loop vectorization and lane packing. `-O3` gains 1.7% over `-O2` on the compiler, and its vectorizer changes none of the compiler's loops. chibi has an explicit vector type, so a loop like `line_at`'s can be written with it.
- **Virtual-call speculation and static initialization of globals**, which serve C++: chibi has no virtual calls, and its globals start at compile time. Private-allocation elision changed nothing on the compiler.
- **Tail-recursion elimination**, until a measurement shows it helps.

## 6. Lessons from LowIR's bugs

The spike found five bugs in LowIR's optimizer, all at `-O1` and up (the table in [../../experiments/lowir-spike/README.md](../../experiments/lowir-spike/README.md)).
- **Three are width bugs.** A switch on an `i1` took its range from the 8-bit storage, so the bound wrapped (bug 1). A compare of a truncated `and` tested the whole mask (bug 2). A loop that stores a truncated value crashed loop removal (bug 4). The design notes' first rule for folding is to compute every integer result at the instruction's width; each of these broke it.
- **One is a stale fact after a CFG edit.** Switch formation left phis that named a block it had removed, and four later passes crashed on it (bug 3). `lowiropt --verify-passes`, which checks the IR after every pass, found the cause. A verifier like it should exist before the first pass does.
- **One is in aggregate splitting**, the pass that matters second most: a copy of an object's first bytes copied the whole object (bug 5).
- **chibi already has the tests that found them.** The spike's tests found all five: the C backend's grid and the random programs of `test/random`, each checked against the reference interpreter. They should run against chibi's own optimizer at every level from its first pass.

## 7. Compile time

LowIR `-O2` builds the 36,472-line compiler in 1.65 s, and 0.93 s of that is the optimizer. Value simplification, store-to-load forwarding and jump threading take 0.56 s of it, 60%. They are also three of the five passes that matter most, so the time is well spent; constant propagation's 83 ms is not. LowIR builds the same program 6.4× faster than `chibi c` and clang `-O2` (step 3k.25, the spike's README).

## 8. Questions for the owner

1. **How close to clang is good enough** (question 3 of [optimizing-backends.md](optimizing-backends.md) section 8, now with chibi's own code). LowIR `-O2` runs 19% more instructions than clang in the compiler's own code and 14% more on the corpus, and ran at 0.93 of clang's speed at best at step 3k.25. Section 1 suggests that the eight ideas in section 3 carry most of what LowIR's optimizer does for chibi's code, and section 2 that vectors and value ranges carry most of what it misses. Both are hypotheses until an optimizer of chibi's own is measured.
2. **Where chibi's optimizer starts.** This note recommends aggregate splitting and promotion on chibi's IR first, with the verifier and the random programs from day one, then inlining, then simplification, threading and forwarding, measuring each on the compiler as section 1 does.

## 9. Not verified

- The ranking comes from one workload, the compiler writing its own source. The corpus exercises the same code at a larger scale, and wasn't ablated pass by pass.
- Instruction counts, not time. At step 3k.25 the corpus count through LowIR was 5.3% above clang's and its best time 7.6% above.
- clang's choices were read from its machine code; its pass-by-pass contributions weren't measured.
