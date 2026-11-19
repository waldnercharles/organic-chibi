# Front-end optimizations: where the lexer's and parser's cycles go

## Summary

- Lex plus parse costs **243 cycles per line** on one core (lexer 106, parser 135): 6% of the 3,950-cycle budget.
- The parser's IPC of 2.2 comes from **branch mispredictions**: 0.46 per token at about 21 cycles each, half its cycles.
- 46% of them are the `switch` in `parse_postfix`. An iterative parser kept the same count; it ranged from −8% to +9%.
- The lexer runs at IPC 4.7: interning 32%, per-token loop 21%, per-block SIMD 47%.
- Half of the lexer's mispredictions are the exit of its per-block token loop.
- **The M4 Pro learns the benchmark:** the parser runs 2× faster on a 340 KB corpus than on 8 MB.
- So the bench's 14-thread split, about 590 KB per thread, inflates the 14-thread lexer figure by about 28%.
- Real old-Chibi code lexes with 17–19% fewer cycles per byte than the generated programs.
- Ranked: F1 branch-free expression core (−10% parser, untested); F2, F3, F4 each cut the lexer by 3–4%.

## What was measured

- **Version.** The working copy as of 19:35 on 2026-11-09: commit `ae4d366` plus uncommitted changes. Tests passed, so the working copy was used. `src/` and `bench/bench.c` still match the working copy. After the snapshot, the owner changed `test/programs.c` (operand weights) and `Makefile`. Rerun the numbers to compare with that generator.
- **Machine.** `sysctl` reports an **Apple M4 Pro** with 10 performance cores and 4 efficiency cores. `hypotheses.md` calls it a "14-core M4". The clock measured during runs was 3.9–4.2 GHz.
- **Scratch copy.** `/tmp/chibi-v2-frontend`. Nothing was built or run inside `~/Personal/chibi-v2`.
- **Harness.** `bench/prof.c` in the scratch copy. Its modes are `lex` (no checksum), `lexsum`, `parse` (from pre-lexed tokens), `both` and `stats`. The environment variables `NFILES` and `SEED` limit the files and choose the seed. It reports cycles per byte, line, token and node.
- **Counters.** Cycles and instructions come from `proc_pid_rusage`, as in the bench. Hardware events come from `xctrace` with the "CPU Counters" template. Its guided modes are chosen through the JSON in the template's `optionsEncoded` field: CPU Bottlenecks, Discarded Sampling, Discarded Indirect and L1D Miss Sampling. The modes and their formulas are defined in Xcode's `RecountDT.framework/Resources/Analysis/bottleneck.json`. On this chip the core maps 10 µops per cycle. PMI samples were mapped to source lines with `atos -i`. The scripts and templates are in `prof-tools/` in the scratch copy.
- **Noise.** Background agents ran during the runs (Kandji, Netskope, Tanium, WindowServer). Each number is the median of 5 runs, with the range in parentheses, unless stated otherwise. Runs vary by about 5%. Multi-threaded runs vary more.
- **Not measured.** A parser run on real code: no real corpus in chibi-v2 syntax exists yet. Per-core-type counters in the 14-thread runs: threads cannot be pinned on macOS.

## Baseline

`make bench` binaries from the snapshot, 5 runs each:

| Row | PGO | Plain |
|---|---|---|
| Lexer, 1 thread | 1,433 MB/s (1,368–1,442); 2.833 cycles/B (2.810–2.906); 14.40 instr/B | 1,429 MB/s (1,398–1,453); 2.823 (2.798–2.835); 14.56 |
| Lexer, 14 threads | 19,579 MB/s (19,525–19,703) | 18,843 (18,686–19,083) |
| Lexer + parser, 1 thread | 669 MB/s (660–678); 6.046 cycles/B (6.032–6.189); 21.63 instr/B | 637 (618–649); 6.329 (6.255–6.466); 22.92 |
| Lexer + parser, 14 threads | 7,773 MB/s (7,303–7,986) | 7,506 (7,497–7,550) |
| Parser alone (difference) | 3.199 cycles/B (3.139–3.371); 7.23 instr/B | 3.495 (3.456–3.630); 8.36 |

The harness on the same PGO profile, without the checksum the bench adds to the lexer row:

| Phase | cycles/B | instr/B | IPC | cycles/line | cycles/token | instr/token |
|---|---|---|---|---|---|---|
| Lexer | 2.544 (2.541–2.620) | 12.09 | 4.75 | 106.3 | 14.3 | 68 |
| Parser (pre-lexed tokens) | 3.218 (3.175–3.314) | 7.24 | 2.25 | 134.5 | 18.1 (20.8 per node) | 40.7 |
| Lexer + parser | 5.813 (5.764–5.911) | 19.32 | 3.32 | 242.9 | 32.7 | — |

The corpus is 1,600 programs: 8,249,791 bytes, 197,411 lines (11,441 of them blank), 1,468,316 tokens, 1,274,261 nodes and 617,775 names (12,706 distinct). That is 41.8 bytes and 7.44 tokens per line, 5.62 bytes per token and 0.87 nodes per token.

## Data layout and hot loops

### Tokens

- Structure of arrays (`lexer.h`): `kinds` is a `u8` per token, `starts` a `u32` byte offset and `ids` a `u32` name id for identifiers and keywords. `line_starts` holds one bit per source byte, set at the first token of each line.
- That is 9 bytes per token plus 1 bit per byte. At 5.62 bytes per token, the token arrays are 1.6 bytes per source byte.
- Buffers hold `len + 9` tokens, the worst case of one token per byte. Lengths and line numbers are not stored; `lexer_token_length` and `print_error_at` recompute them.
- Zig's AST keeps the same two fields per token, `tag` and `start`, as a structure of arrays ([Ast.zig L29-L32](https://codeberg.org/ziglang/zig/src/tag/0.17.1/lib/std/zig/Ast.zig#L29-L32)). Carbon packs each token into 8 bytes ([fast-compilers.md](fast-compilers.md), section 3.4).

### Lexer: one 64-byte block per iteration

The default path is `lexer_tokenize` (`src/lexer.c` L960–L1050).

1. **`block_masks` (L162).** One `ld4` loads 64 bytes into four vectors, de-interleaved. Rotating lanes gives the "next byte" vectors without a second load (`simd_next64`). The function builds nine 64-bit masks: identifier bytes, white space, operator-pair first bytes, digits, `.`, `"`, `\`, `//` and line ends. It compiles to about 170 NEON instructions per block. About 54 of them only turn byte vectors into bit masks, because NEON has no movemask instruction.
2. **`resolve_literals` (L253).** String bodies come from a prefix XOR over the quote bits, with a carry from the previous block. Escapes use the odd/even backslash trick. Comments run to the next line end. Anything unusual sets `fallback` and goes to scalar code. This is simdjson's design ([Langdale and Lemire](https://arxiv.org/abs/1902.08318), section 3.1).
3. **`token_starts` (L343).** A token starts at a byte that is not white space, not inside a name, not the second byte of a two-byte operator and not inside a literal.
4. **`first_on_line` (L362).** An add-with-carry trick marks the first start after each newline. The result is the line-start bitmap.
5. The next block's masks are computed before the token loop. The `PERF` comment at L992 explains why: the CPU works on them while the loop runs.
6. **The token loop (L1002).** It runs once per start bit. Each iteration does a `ctz` and loads 2 source bytes. Those bytes index `kind2`, a 64 KB table that gives a token's kind from its first two bytes, or `SLOW`. Then it stores the kind, the start and the name length, and appends the token to the name list.
7. **Every 64 blocks (4 KB),** `intern_names` (L907) interns the names collected so far.

### Interning

- `names.h` uses open addressing with linear probing and at most 50% load. A slot is 32 bytes: the first 16 bytes of the name as two words `k0` and `k1` (zero padded), the hash, the length and the id.
- Names of at most 16 bytes compare as two 64-bit words, with no `memcmp`. Their hash is one 128-bit multiply. Longer names use XXH3 and `memcmp`. When a short name is found outside its home slot, it moves home (`names_move_home`).
- Keywords are interned first, with ids 1–35, so `id <= NKEYWORDS` means "keyword". Recognizing keywords costs nothing beyond interning.
- The table is sized from source bytes ÷ 256. For the bench that is 64K slots, 2 MB.
- The fast path is 31 instructions, read from the disassembly. The average is 47.7 instructions and about 11 cycles per name. 5.4% of lookups take `intern_cold`: 3.1% are names longer than 16 bytes and 2.1% are first occurrences.

### Parser

- **Output.** Nodes are in postorder in two arrays: a `u8` kind and a `u32` token, 5 bytes per node. Marker nodes such as `CALL_ARGS`, `AND_LEFT` and `BLOCK_START` delimit lists, so later passes find children without pointers. Carbon's parse tree is postorder for the same reason ([parse.md](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/docs/parse.md), "Postorder tree").
- **Control flow.** Statements use recursive descent. Binary operators use precedence climbing (`parse_binary`, L228): one recursive call per operator. Comparisons cannot be chained. Zig's `parseExprPrecedence` has the same shape, with `banned_prec` for comparisons ([Parse.zig L1574-L1630](https://codeberg.org/ziglang/zig/src/tag/0.17.1/lib/std/zig/Parse.zig#L1574-L1630)).
- **State.** `pos`, `depth`, `brackets` and `count` live in `Parser`, behind a pointer. Every `emit` loads `count` and the two output pointers, then stores the token, the kind and the new count. The disassembly stores and reloads `p->pos` around each step.
- **Newlines.** `starts_line` loads `starts[pos]`, then a `line_starts` word: two dependent loads. `ends_statement` calls it, and so does the postfix code before `(` and `[`.
- **Cost.** 40.7 instructions per token, 46.9 per node.

### `bench/bench.c`

The bench generates 1,600 programs with seed 1, between 1 and 9 KB each. It reuses the token and node buffers and resets the name table every pass. Each measurement runs at least 3 passes and at least 0.25 s. Cycles and instructions come from `proc_pid_rusage`. The PGO build is trained on seed 2. "Parser alone" is the difference between two separate runs.

## Where the cycles go

### Lexer

By removing parts of the lexer in the scratch copy (`src/lexer_probe.c`, `-DPROBE_NO_INTERN` and `-DPROBE_NO_TOKENS`):

| Part | cycles/B | share | instr/B | per unit |
|---|---|---|---|---|
| Whole lexer | 2.596 (2.553–2.650) | 100% | 12.09 | 14.3 cycles per token |
| Interning | 0.825 | 32% | 3.57 | 11.0 cycles, 47.7 instructions per name |
| Per-token loop | 0.546 | 21% | 2.96 | 3.1 cycles, 16.6 instructions per token |
| Per-block SIMD work and slow blocks | 1.225 (1.203–1.243) | 47% | 5.56 | 78 cycles, 356 instructions per block |

- The time profile agrees: `lexer_tokenize` 70%, `intern_names` 25%, `intern_cold` 3%.
- Top-down: 47% of issue slots do useful work, 33% are discarded, 15% wait on processing and 3% on instruction delivery.
- Mispredictions: 13.7 per 1,000 cycles, which is 0.197 per token. By source line:
  - the token loop's `while (starts)` (L1002): 49%;
  - the fast-path test in `intern_names` (L926): 12%;
  - `resolve_literals` (L261, L267, L278): 15%;
  - `extend_last_name` (L954): 6%.
- L1D: 11 load misses per 1,000 cycles, 77% of them in `intern_names` (0.28 per name). The 2 MB table misses L1 but hits L2. Out-of-order execution overlaps those misses: an 8 MB table runs at the same speed (2.515 against 2.502 cycles/B).
- 3% of blocks take the slow path. Blocks average 11.7 token starts; 16.5% have more than 16 and 1.2% more than 24.

### Parser

- Time profile by innermost function: `parse_postfix` 34%, `parse_binary` 21%, `parse_operand` 14%, `emit` 5%, `parse_statement` 5%, `parse_binding` 5%.
- Top-down: 25% useful, **58% discarded**, 8% processing, 6% delivery. The core maps 6.9 µops per cycle but retires only 2.5.
- Mispredictions: 25.1 per 1,000 cycles, which is 0.46 per token and 0.53 per node. 91% are conditional branches. 9% are the indirect jump of the statement `switch`. No returns were mispredicted. By source line:
  - `switch (p->tokens.kinds[at])` in `parse_postfix` (L130): 46%;
  - `if (op.prec < min_prec)` in `parse_binary` (L236): 18%;
  - the loop in `parse_statements` (L356): 9%;
  - the leaf test in `parse_operand` (L191): 8%;
  - the `:=`/`:` test in `parse_statement` (L565): 7%;
  - `ends_statement` (L260): 3.5%.
- L1D: 0.03 load misses per token. Memory-order violations: 0.06 per 1,000 cycles.

### Why the parser's IPC is low

The evidence points to mispredicted branches on the token kind.

1. **Top-down.** 58% of issue slots are discarded. The other bottlenecks are small: processing 8%, delivery 6%.
2. **Causal test.** The bench parsed 64 programs (340 KB) over and over. The branch predictor learned them. Mispredictions fell from 0.46 to 0.036 per token, cycles from 18.1 to 9.1 per token and IPC from 2.2 to 4.5. Instructions stayed at 40.6 per token. With the same instructions, mispredictions account for the whole gap: (18.1 − 9.1) ÷ (0.46 − 0.036) ≈ **21 cycles per misprediction**. The lexer gives the same figure, about 21 cycles.
3. **What a misprediction costs.** The CPU guesses each branch and runs ahead on the guess. On a wrong guess it discards the work done after the branch, then fetches again from the right address. The M4 Pro maps up to 10 µops per cycle, so 21 lost cycles can be up to 210 discarded µop slots. The branch only resolves after its inputs arrive: `pos`, then `kinds[pos]`, then a table load. That waiting adds to the cost.
4. **Why they happen.** The branch outcomes are random. After an operand, the next token is a postfix operator 28% of the time. Otherwise it is a binary operator or the end of the expression. The generator picks them independently, so history holds nothing to learn. The `switch` at L130 tests this token with a chain of 3 compares. Precedence climbing then tests the same token again at L236, at each level it returns through.
5. **Not memory.** There are 0.03 L1D misses per token, and the processing bottleneck is 5–8%.
6. **Not recursion.** No return was mispredicted. Keeping the state in registers (the prototype below) cut instructions by 7% but cycles by only 0–2%. That `Parser *p` costs little is a guess with only this partial evidence.

## The branch predictor learns the benchmark

The M4 Pro's predictor memorizes the branch outcomes of about 0.5–1 MB of input when that input repeats. The same is true of the lexer. Lemire showed the effect on AMD and Intel: on 2,000 values, AMD Rome's misprediction rate fell from 52% to 0.1% within 9 repeats ([Benchmarking is hard: processors learn to predict branches](https://lemire.me/blog/2019/10/16/benchmarking-is-hard-processors-learn-to-predict-branches/)).

Parse only, PGO, by the number of programs parsed over and over:

| Programs | Bytes | cycles/token | instr/token |
|---|---|---|---|
| 1 | 3.7 KB | 9.41 | 40.5 |
| 16 | 96 KB | 8.98 | 40.4 |
| 64 | 340 KB | 9.06 | 40.6 |
| 128 | 661 KB | 10.01 | 40.7 |
| 256 | 1.34 MB | 14.16 | 40.7 |
| 400 | 2.10 MB | 16.75 | 40.7 |
| 800 | 4.14 MB | 17.75 | 40.6 |
| 1,600 | 8.25 MB | 18.12 | 40.6 |

The lexer follows the same curve: 1.85 cycles/B at 64 programs against 2.54 at 1,600.

**This changes the 14-thread numbers.** The bench splits the 1,600 programs across threads, so each thread sees about 590 KB. `bench/mt.c` in the scratch copy compares that split with giving every thread all 1,600 programs (medians of 3, PGO):

| Work | 1 thread | 14 threads, split | 14 threads, full corpus each | Scaling, full |
|---|---|---|---|---|
| Lexer | 1,607 MB/s | 23,070 MB/s (2.09 cycles/B) | 18,038 MB/s (2.68 cycles/B) | 11.2× |
| Lexer + parser | 684 MB/s | 7,157 MB/s | 6,825 MB/s | 10.0× |

H3 (at least 10× on 14 threads) still holds with fresh code on each thread, but only just for lexer plus parser. In a real build every file is parsed once, so the "full corpus" column is the honest one.

## Real code against generated code (H1, lexer only)

The lexer accepts all 161 old-Chibi `.chibi` files from `~/Personal/chibi/chibi` (1.47 MB, 32,559 lines), copied into `corpus-old/` in the scratch copy. The parser does not: old Chibi uses another syntax. `bench/realcorpus.c` compares them with generated programs of the same total size. The size matters, because of the memorization effect above.

| Corpus | B/line | tokens/line | cycles/B, repeated | cycles/B, predictor flushed between files |
|---|---|---|---|---|
| Old Chibi | 45.0 | 7.66 | 1.792 (1.756–1.803) | 2.206 (2.185–2.451) |
| Generated | 41.6 | 7.41 | 2.158 (2.033–2.187) | 2.722 (2.681–2.796) |

- "Flushed" means 200 unrelated generated files were lexed before each timed file, so the predictor could not remember it. Each file was timed with `mach_absolute_time`.
- Per line with the predictor flushed: about 99 cycles for old Chibi and 113 for generated code.
- In old Chibi, 0.9% of identifiers are longer than 16 bytes, against 3.1% in the generated programs. Mean identifier length is 6.3 bytes.
- **The generated programs overstate lexer cost per byte by about 20%.** It is likely that they also overstate parser cost, because real code repeats patterns the predictor can learn. That needs a real chibi-v2 corpus to measure.

## Tried and rejected

These prototypes are in the scratch copy. Each one passes `test/` and, for the parser, produces node arrays identical to the original on all 1,600 programs (`bench/same.c`).

| Change | Result | Lesson |
|---|---|---|
| Iterative expression parser with an explicit operator stack, state in locals, table-driven dispatch (`src/parser_proto.c`) | Plain: 3.383 against 3.401 cycles/B (−0.5%; an earlier run gave −2%). PGO: 3.395 against 3.123 (+8.7%). Instructions −7%. Mispredictions 0.51 per token against 0.47. | The shape of the code does not change how many decisions are made. |
| The same, plus token kinds renumbered so that binary and postfix operators are contiguous, with range checks instead of table loads (`exp-ranges/`) | Plain: 3.137 against 3.401 (−7.8%). PGO: 3.234 against 3.123 (+3.6%). Mispredictions 0.50 per token (PGO 0.52); PGO delivery bottleneck 14% against 6%. | Shorter inputs to the branch made each misprediction cheaper in the plain build. PGO's code layout cancelled the gain. |
| Branchless `resolve_literals`: always compute escapes and the comment carry (`src/lexer_nobr.c`) | 2.829 against 2.511 cycles/B (+12.7%) | The removed branches were nearly always predicted. They also cut a serial chain from block to block. Without them, every block's `prefix_xor` waits on the previous block's carry. A predicted branch is cheaper than a dependency chain. |
| Branchless name-length select in the bulk path's `intern_names` (`src/lexer_bulk2.c`) | 2.572 against 2.580 (within noise) | — |
| A name table of 8 MB instead of 2 MB | 2.515 against 2.502 (within noise) | Interning is not limited by memory latency. |
| A name table of 1 MB that must grow | 2.673 against 2.502 (+7%) | Growing costs; size from the source as the code does now. |
| The existing bulk path (`-DCHIBI_LEXER_BULK`) | 2.580 against 2.603 (−0.9%, ranges overlap). Mispredictions 0.114 per token against 0.197. Instructions +0.5 per byte. | See F2. |

## Hypotheses

F1–F4 are available now; F5–F7 depend on open syntax decisions. Each group is ranked by expected gain. All gains are against the PGO baseline above: lexer 106 and parser 135 cycles per line.

| ID | Claim | Needs | Status |
|---|---|---|---|
| F1 | A branch-free expression core cuts parse cost by 10% | nothing | untested |
| F2 | Unconditional token writes cut the lexer by 4% | nothing | partly measured |
| F3 | A shorter interning fast path cuts the lexer by 4% | nothing | untested |
| F4 | One shared byte classification cuts the lexer by 3% | nothing | untested |
| F5 | Declarations that start with a keyword cut parse cost by up to 3% | a syntax decision | untested |
| F6 | A line-start bit in the token removes the parser's bitmap loads | a syntax or token decision | untested |
| F7 | Dropping three-byte operators costs nothing and removes code | an operator-set decision | untested |

## Available now

### F1. A branch-free core for common expression tokens cuts parse cost by 10%

> **Update, 2026-11-09: rejected.** The parser thread built this as a branch-free table automaton that falls back to the recursive parser (2.4% of tokens fall back). It measured +91% cycles, at 190 instructions per token. Every step loads the next state from a table and saves state that the following step reads back, so each token waits for the one before it. Branchy code instead lets the CPU guess ahead. Computed goto (+10–14%) and a flat loop per bracket level (+0.8%) were rejected too. Details are in that thread's `parser-perf-effort` memory.

- **Claim:** handle leaf operands, prefix operators, binary operators, `.name` and `.*` in one loop that chooses with selects instead of branches. Mispredictions then fall from 0.46 to at most 0.25 per token, and parse cost from 18.1 to at most 16.3 cycles per token (−10%; 135 to 122 cycles per line).
- **Why we think so:**
  - Each misprediction costs about 21 cycles here (measured), and the parser makes 0.46 per token. That is about 9.7 of its 18.1 cycles per token.
  - Moving branches around does not help: the iterative prototype kept 0.50–0.52 per token. Only fewer data-dependent branches can lower the count.
  - A branch-free step writes every candidate node with plain stores, then advances `count` by 0 or 1.
  - The operator stack holds at most one entry per precedence level, 5 for binary operators. So "pop while prec ≥ p" unrolls into a fixed number of predicated steps.
  - One branch remains: "this token is not in the fast set". It fires at the end of each expression and at `(`, `[` and `fn`, which node counts put near 0.25 per token.
  - **Break-even:** at IPC 4, one avoided misprediction pays for about 84 instructions. Removing 0.21 mispredictions per token pays for about 18 more instructions per token.
  - Prior art: simdjson removes the same kind of branch from index extraction. It writes indexes without testing whether each one is needed ([paper section 3.1.4](https://arxiv.org/abs/1902.08318); [`bit_indexer::write`, json_structural_indexer.h L93-L122](https://github.com/simdjson/simdjson/blob/v4.0.0/src/generic/stage1/json_structural_indexer.h#L93-L122)). Carbon's parser also loops over an explicit state stack ([parse.md](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/docs/parse.md), "Parse stack"), but still dispatches with a branch per state.
- **Test:**
  - Write `parse_expr_fast` beside `parse_binary`; other tokens fall back to the current code.
  - Compare node arrays on all 1,600 programs, as `bench/same.c` does in the scratch copy.
  - Measure cycles and mispredictions per token with Discarded Sampling on the 8 MB corpus, PGO and plain.
- **Passes if:** at most 16.3 cycles and at most 0.25 mispredictions per token on the PGO build, with identical nodes.
- **If it fails:** keep the recursive parser. Its cost then rests on how predictable real code is (H1). Measure that before doing more parser work.
- **Status:** untested. The range is wide: 0 to −20%. Real code may already be more predictable, which would shrink the gain.

### F2. Unconditional token writes cut the lexer by 4%

- **Claim:** write each block's token slots in unconditional steps of 8: kind, start and name length, with a branch only between steps. The per-block loop-exit mispredictions then fall by more than half, and the lexer drops from 14.3 to about 13.7 cycles per token (−4%; 106 to 102 cycles per line).
- **Why we think so:**
  - `while (starts)` (L1002) runs 11.7 times per block on average. Its exit costs about one misprediction per block: 49% of the lexer's mispredictions, 0.097 per token × 21 cycles ≈ 2 cycles per token.
  - Steps of 8 still branch between steps. "More than 8 starts" is true for 74% of blocks and "more than 16" for 16.5%, so expect about 0.4 mispredictions per block instead of 1.
  - The cost: about 4 wasted slots per block at about 8 instructions each, roughly 0.5 instructions per byte.
  - The bulk path already shows the effect. Its mispredictions fell to 0.114 per token, but its second pass, `read_kinds`, added 0.5 instructions per byte, so it nets −0.9%. Putting the `kind2` lookup into the stepped writes keeps one pass.
  - Prior art: simdjson writes indexes in steps of 4, up to 24, before it branches ([json_structural_indexer.h L93-L122](https://github.com/simdjson/simdjson/blob/v4.0.0/src/generic/stage1/json_structural_indexer.h#L93-L122)). Its paper explains why in section 3.1.4 ([arXiv:1902.08318](https://arxiv.org/abs/1902.08318)).
- **Test:** change the default path. Measure the median of 5 on the 8 MB corpus, and the mispredictions at the loop with Discarded Sampling. Run `make test`, including x86_64.
- **Passes if:** at least 3% fewer lexer cycles per byte on the PGO build.
- **If it fails:** keep the default path and delete the bulk path, since neither wins.
- **Status:** partly measured. The bulk path gave −0.9% and 42% fewer mispredictions.

### F3. A shorter interning fast path cuts the lexer by 4%

- **Claim:** today the fast path takes 31 instructions and the average lookup 47.7. Cutting the fast path to about 25 instructions, and the cold path's share, brings interning from 11 to about 9 cycles per name (−4% lexer, about −4 cycles per line).
- **Why we think so:**
  - Interning is 32% of the lexer. Its time is instructions plus cold-path mispredictions (0.055 per name, about 1.2 cycles), not memory latency: an 8 MB table ran as fast as a 2 MB one.
  - In the fast path, 4 loads and the address arithmetic only re-read `starts[k]` and the length that the token loop already had in registers.
  - Interning inside the token loop, or passing (start, length) records to the batch, removes those re-loads. Keep the batch if the loop's branch structure suffers.
  - Long names pay for XXH3 plus `memcmp`. They are 3.1% of the generated names but 0.9% of old-Chibi names, so the cold path matters less on real code.
  - Prior art: Carbon also interns in the lexer, once per occurrence, after checking for keywords with a `StringSwitch` ([lex.cpp L1787-L1829](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/lex/lex.cpp#L1787-L1829)). Zig does not intern in its tokenizer. AstGen interns each file's strings later ([AstGen.zig L42](https://codeberg.org/ziglang/zig/src/tag/0.17.1/lib/std/zig/AstGen.zig#L42), [L11030](https://codeberg.org/ziglang/zig/src/tag/0.17.1/lib/std/zig/AstGen.zig#L11030)). Deferring the work moves it but does not remove it.
- **Test:** record the per-pass list of (start, length) name records. Time `intern_names` alone on that list, then the whole lexer.
- **Passes if:** at most 9 cycles per name occurrence, and at least 3% off the lexer.
- **If it fails:** interning is near its floor at about 11 cycles per name. Leave it.
- **Status:** untested.

### F4. One shared byte classification cuts the lexer by 3%

- **Claim:** compute one class byte per source byte with two nibble table lookups, then derive the masks with one `cmtst` each. That cuts `block_masks` from about 170 to about 140 instructions per block, −0.5 instructions per byte, and the lexer by about 3%.
- **Why we think so:**
  - Today each mask has its own compares. For example, `is_ident` takes about 8 operations per vector, `is_line_end` 3 and `is_comment_start` 3.
  - Two `tbl` lookups, on the low and high nibbles, can give 8 class bits at once. Each mask then costs one `cmtst` before the movemask.
  - The lexer is limited by how many instructions it issues (IPC 4.7). Fewer instructions are its main remaining lever.
  - The code already classifies white space and operator pairs this way. This extends it to the other masks.
  - Prior art: simdjson's "vectorized classification" ([paper section 3.1.2](https://arxiv.org/abs/1902.08318)). Carbon keeps identifier scanning scalar on Arm, because NEON-to-register moves are slow on Apple cores ([lex.cpp L538-L560](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/lex/lex.cpp#L538-L560)). chibi-v2 avoids those moves by building whole 64-bit masks.
- **Test:** count the instructions of `block_masks` alone, as the scratch copy did. Then run the lexer, median of 5.
- **Passes if:** at least 0.4 fewer instructions per byte and at least 2% fewer cycles per byte.
- **If it fails:** the masks are already near minimal. Stop working on the SIMD part.
- **Status:** untested.

## Depend on open language decisions

### F5. Declarations that start with a keyword cut parse cost by up to 3%

- **Claim:** if every declaration starts with a keyword, the statement is decided by its first token. That removes the second-token test at `parse_statement` L565, about 7% of the parser's mispredictions, and up to 3% of parse cost.
- **Why we think so:** `x := …` and `x: T = …` need the second token. The parser tests `next == TK_DASSIGN || next == TK_COLON` after the first-token `switch`. A keyword puts the choice into that `switch`, which already mispredicts as an indirect jump (9% of mispredictions). The choice between declaration and expression does not go away; only one test of it does. Zig uses `const` and `var` ([Parse.zig](https://codeberg.org/ziglang/zig/src/tag/0.17.1/lib/std/zig/Parse.zig)). Go has both `var` and `:=` ([Go spec, short variable declarations](https://go.dev/ref/spec#Short_variable_declarations)).
- **Test:** add a keyword form to the scratch parser and generator, then compare mispredictions at the statement dispatch and cycles per token.
- **Passes if:** at least 2% fewer parser cycles per token.
- **If it fails:** choose the syntax on other grounds. Speed does not separate the two.
- **Status:** untested.

### F6. A line-start bit in the token removes the parser's bitmap loads

- **Claim:** if the lexer puts "first token on its line" into the token itself, `starts_line` needs no loads. Either put it in the free top bit of `kinds` (kinds use 7 bits; `TK_OP < 0x80`) or emit a terminator token as Go does. Parse cost then falls by 1–2%.
- **Why we think so:** `starts_line` makes two dependent loads, `starts[pos]` and then a `line_starts` word. They are 26% of the parser's L1D misses (few in total). `ends_statement` is 3.5% of its mispredictions. Go's lexer inserts a semicolon when a line ends in an identifier, a literal, `break`, `continue`, `fallthrough`, `return`, `++`, `--`, `)`, `]` or `}` ([Go spec, Semicolons](https://go.dev/ref/spec#Semicolons)). Lua 5.1 forbids a line break before the `(` of a call ([manual section 2.5.8](https://www.lua.org/manual/5.1/manual.html#2.5.8)), the same rule chibi-v2 applies to `(` and `[`.
- **Test:** set bit 7 of `kinds` in the token loop, which is `(line_bits >> pos) & 1`. Index the parser's tables with all 256 kind values, or mask the kind where needed. Measure parse cycles per token.
- **Passes if:** at least 1% fewer parser cycles, with no lexer slowdown above 0.5%.
- **If it fails:** keep the bitmap. It also serves diagnostics.
- **Status:** untested.

### F7. Dropping three-byte operators costs nothing and removes code

- **Claim:** if every operator is decided by its first two bytes, so `<<=` and `>>=` are gone, lexer speed changes by less than 1%. `operators_from_left`, `is_shift_assign`, the triple handling in `token_starts` and the `SLOW` entries for three-byte operators in `kind2[1]` can then be deleted.
- **Why we think so:** `<<=` and `>>=` are 0.06% of the generated tokens. Each one sends its block to the slow path, but only 3% of blocks are slow, for all reasons together. Two-byte operators run at full speed through the 64 KB `kind2` table. A byte-at-a-time tokenizer pays only one more state for them, as Zig does ([tokenizer.zig L859-L863](https://codeberg.org/ziglang/zig/src/tag/0.17.1/lib/std/zig/tokenizer.zig#L859-L863)). The 2-byte table design is what makes them a special case here.
- **Test:** remove them from the scratch copy's token list and generator, then compare lexer cycles per byte.
- **Passes if:** the change is within noise (under 1%).
- **If it fails:** keep them.
- **Status:** untested. The value is simplicity, not speed.

## Grammar and language choices that change lexing and parsing cost

| Choice | Effect | Measured here | Prior art |
|---|---|---|---|
| Decide every expression step from the next token | The main parser cost is one random decision per operand: postfix, binary or end. Syntax that needs more context adds branches or backtracking. The classic case is `<` as both comparison and generic bracket. | 0.46 mispredictions per token with the current one-token decisions | Go chose `[]` for type parameters because "parsing be possible without type information" ([type parameters proposal](https://go.googlesource.com/proposal/+/refs/heads/master/design/43651-type-parameters.md), "Why not use the syntax F\<T\>") |
| Statements decided by their first token (LL(1)) | Removes the second-token test for declarations | 7% of parser mispredictions (F5) | Zig `const`/`var`; Go `var` plus `:=` |
| Five binary precedence levels, comparisons not chained | Precedence climbing makes one call per operator, so the number of levels costs little. The chain check is one compare. | `parse_binary` L236 is 18% of mispredictions | Zig `banned_prec` ([Parse.zig L1574-L1630](https://codeberg.org/ziglang/zig/src/tag/0.17.1/lib/std/zig/Parse.zig#L1574-L1630)); Go has 5 levels ([spec](https://go.dev/ref/spec#Operator_precedence)) |
| Newline ends a statement | The lexer gets line starts almost free (`first_on_line`, a few instructions per block). The parser pays two loads per check. | `ends_statement` 3.5% of mispredictions (F6) | Go inserts semicolons in the lexer; Lua 5.1 section 2.5.8 |
| Every operator decided by its first two bytes | A 2-byte table lookup decides every token kind. Three-byte operators and overlaps need the slow path. | 3% of blocks are slow, for all reasons (F7) | — |
| Number forms with `.` and exponents (`1.`, `1e5`) | Digits followed by `.` or a letter send the block to the slow path (`has_unusual_number`). Requiring a digit after `.` and keeping `..` distinct keeps the test simple. | Floats are 0.1% of tokens | — |
| Fixed keyword set | Keywords are pre-interned with ids 1–35, so recognizing them is free. Contextual keywords would move the decision into the parser. | — | Zig `StaticStringMap` ([tokenizer.zig L12-L62](https://codeberg.org/ziglang/zig/src/tag/0.17.1/lib/std/zig/tokenizer.zig#L12-L62)); Carbon `StringSwitch` |
| ASCII identifiers; names up to 16 bytes take the fast path | One SIMD range test finds names. 16-byte inline keys avoid `memcmp`. Unicode identifiers would need a per-block check and a slow path. | 99.1% of old-Chibi identifiers fit in 16 bytes; 96.9% of generated ones | Carbon lexes ASCII only, with a TODO for Unicode ([lex.cpp L1789-L1792](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/lex/lex.cpp#L1789-L1792)) |
| Line comments only; simple escapes | Strings and comments resolve with bit masks, 64 bytes at a time. Block comments, nested comments, raw strings with custom delimiters or string interpolation would need state across blocks, or a recursive lexer. | `resolve_literals` is 15% of lexer mispredictions | simdjson ([paper section 3.1](https://arxiv.org/abs/1902.08318)) |
| Where names are interned | In the lexer, keywords come free and the parser gets ids. Per-thread tables give per-thread ids; global ids need a merge pass or a shared table. The bench's 14-thread figures use per-thread tables. | Interning is 32% of the lexer | Carbon interns in the lexer; Zig per file in AstGen |
| Operator-precedence (Floyd) grammars | Each parse action depends only on adjacent tokens. That allows one file to be parsed in parallel, and opens table-driven parsing with fewer branches. It only matters for single very large files; chibi-v2 already parallelizes across files. | — | Barenghi et al., [Parallel parsing made practical](https://doi.org/10.1016/j.scico.2015.09.002) (2015) |

## Benchmark review

**What it does well:** generated programs that pass the parser, buffers reused across files, a separate seed for PGO training, cycles and instructions from the kernel, and a fingerprint that catches output changes.

**Problems found:**

1. **The lexer row includes `checksum()`.** It adds 2.14 instructions and 0.32 cycles per byte: 11% of what the lexer row reports. "Parser alone" subtracts it out, but "lexer" overstates the lexer.
2. **The predictor memorizes small inputs.** The 14-thread run gives each thread about 590 KB, which inflates the lexer by about 28% and lexer plus parser by about 5% (see above). Every thread should parse at least 8 MB of code it has not seen in that pass. Better still, generate new programs for each pass, or each thread, from a different seed.
3. **No line, token or node counts.** Cycles per line need bytes per line from elsewhere. Print lines, tokens, nodes and names for each pass, then cycles per line, per token and per node.
4. **Units disagree.** The header prints MiB ("7.87 MB" is 8,249,791 ÷ 2²⁰). Throughput uses 10⁶.
5. **Only one sample per row.** Each row is one 0.25 s window, and the 5% noise is invisible. Run each row at least 5 times and print the median with the minimum and maximum.
6. **"Parser alone" is a difference of two noisy numbers.** Its spread here was 3.139–3.371. Parsing pre-lexed tokens directly gives the same median (3.218 against 3.199) with less spread.
7. **14 threads mixes 10 performance and 4 efficiency cores,** and cycles are read for thread 0's window and printed only for 1 thread. Report the whole process's cycles per byte across all threads, as `bench/mt.c` does. That shows what each byte costs once all cores share the caches.
8. **Generated code is harder to predict than real code** (at least 20% for the lexer). Until a real corpus exists, label results "generated" and keep H1 open.

**To compare results with the hypotheses directly,** print one line per phase:

`phase, cycles/line, cycles/token, instr/token, IPC, % of 3,950-cycle budget, mispredictions/token (when counters are available)`

Then H2 is read straight off the line and F1–F7 can say "cycles per token, before and after".

## Reading list

Read in this order.

1. **`src/lexer.c`, `lexer_tokenize` (default path, L960–L1050).** Follow one block: `block_masks` (L162), `resolve_literals` (L253), `token_starts` (L343), `first_on_line` (L362), then the token loop (L1002). Find where the next block's masks are computed (L994) and why.
2. **`src/lexer.c`, `intern_names` (L907) and `src/names.h`, `names_intern_hashed`.** Count the fast path's instructions against the disassembly (`otool -tvV`). Note what is re-loaded.
3. **`src/parser.c`: `parse_operand` (L185), `parse_postfix` (L126), `parse_binary` (L228).** Trace `a.b + c * d` by hand. Count how many times the token after `d` is tested.
4. **`src/parser.c`: `parse_statement` (L546), `ends_statement` (L257), `starts_line` (L94).** Find the second-token test and the line-start loads.
5. **`bench/bench.c`: `run_pass` (L143), `worker_main` (L162), `measure` (L197).** Find the checksum in the timed loop and how files are split across threads.
6. **The scratch copy, `/tmp/chibi-v2-frontend`:** `bench/prof.c` (the harness), `bench/mt.c`, `bench/realcorpus.c`, `src/parser_proto.c` (iterative parser), `exp-ranges/src/parser_proto.c` (range checks), `src/lexer_probe.c` (cost breakdown), `prof-tools/` (xctrace scripts and templates). Run `prof-tools/rec.sh <template> <name> <binary> <args>`, then `analyze.py` for per-line mispredictions.
7. **simdjson v4.0.0, [`json_structural_indexer.h` L24–L122](https://github.com/simdjson/simdjson/blob/v4.0.0/src/generic/stage1/json_structural_indexer.h#L24-L122).** Look at how `write` turns a bitmask into indexes in unconditional steps (F2, F1).
8. **Langdale and Lemire, [Parsing Gigabytes of JSON per Second](https://arxiv.org/abs/1902.08318).** Read sections 3.1.2 (vectorized classification, F4) and 3.1.4 (index extraction and branch misses, F1, F2).
9. **Zig 0.17.1, [`tokenizer.zig`](https://codeberg.org/ziglang/zig/src/tag/0.17.1/lib/std/zig/tokenizer.zig).** Read the keyword map (L12–L62), then `next` (L391), a labeled `switch` that dispatches with `continue :state`. Compare its byte-at-a-time state machine with chibi-v2's 64-byte blocks.
10. **Zig 0.17.1, [`Parse.zig` L1522–L1630](https://codeberg.org/ziglang/zig/src/tag/0.17.1/lib/std/zig/Parse.zig#L1522-L1630) and [`Ast.zig` L20–L33](https://codeberg.org/ziglang/zig/src/tag/0.17.1/lib/std/zig/Ast.zig#L20-L33).** Read the operator table and precedence climbing (the same algorithm as `parse_binary`), then the token and node arrays.
11. **Carbon `ca189850`, [`toolchain/docs/parse.md`](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/docs/parse.md).** Read "Parse stack" and "Postorder tree". Then [`lex.cpp` L1787–L1829](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/lex/lex.cpp#L1787-L1829), keywords then interning, and L538–L600, dispatch and the note on NEON moves. [fast-compilers.md](fast-compilers.md), section 3.4, has Carbon's measured speeds.
12. **Lemire, [Benchmarking is hard: processors learn to predict branches](https://lemire.me/blog/2019/10/16/benchmarking-is-hard-processors-learn-to-predict-branches/).** The memorization effect in its simplest form.
13. **Barenghi et al., [Parallel parsing made practical](https://doi.org/10.1016/j.scico.2015.09.002).** Read about local parsability of operator-precedence grammars, if parsing within one file ever needs to run in parallel.
