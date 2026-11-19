# Front-end profiling snapshot

This folder holds the sources from the profiling run behind [docs/research/front-end-optimizations.md](../../docs/research/front-end-optimizations.md). They were copied out of `/tmp/chibi-v2-frontend` on 2026-11-09, because macOS deletes `/tmp` on reboot.

## What's here

- **`src/`, `test/`, `bench/` and `Makefile`:** the working copy as it was at 19:35 on 2026-11-09 (commit `ae4d366` plus uncommitted changes), with the profiling agent's additions:
  - **`bench/prof.c`:** times each phase (`lex`, `lexsum`, `parse`, `both`, `stats`). Set `NFILES` and `SEED` to choose the input.
  - **`bench/mt.c`:** compares splitting the programs across threads with giving every thread the full set.
  - **`bench/realcorpus.c`:** lexes the old Chibi code in `corpus-old/`.
  - **`src/lexer_*.c` and `src/parser_proto.c`:** prototypes of the optimizations measured in the note. They were never applied to your tree.
- **`exp-ranges/`:** a second copy, used for the operand-range experiment.
- **`prof-tools/` and `build_prof.sh`:** the `xctrace` recording and analysis scripts.
- **`traces/*.out`:** text exports of the hardware-counter runs.

## What was dropped

- the raw `.trace` bundles (742 MB);
- build output;
- `vendor/`, which is the same as in your tree;
- the agent's copies of `AGENTS.md`, `CLAUDE.md` and `docs/`.

## Status

Nothing here is wired into your build. `experiments/` is listed in `.git/info/exclude`, as `docs/` is, so git ignores it, and it exists only on this disk.
