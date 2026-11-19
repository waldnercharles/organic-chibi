# The bookkeeping of a one-pass checker

This experiment estimates what a checker that reads the parser's postorder nodes once costs before any type rule runs (section 1 of [docs/research/checker-and-ir.md](../../docs/research/checker-and-ir.md)). It is a throwaway program for measurement, not compiler code.

```bash
make run   # -O2, 400 generated files
```

## What it does

For every node of the parser's 400 generated files, `check_floor` does the bookkeeping a real checker needs, with no type rules:
- It pushes an operand for each node and pops a node's children, using the parser's arity and list-start tables.
- It looks names up in an array indexed by name id, and binds the names that declarations introduce, restoring them when a block ends.
- It writes a 13-byte IR record (kind, type, two operands) for each node that isn't a marker.

The files are lexed and parsed once, outside the timing. All files share one lexer, so name ids are global.

## Result

Measured on the owner's M4 Pro on 2026-11-12, as the median of 5 runs:

| Measure | Value |
|---|---|
| Nodes per line | 6.28 |
| IR records per line | 5.10 |
| Cycles per node | 23.8 |
| Cycles per line | 149 |
| Instructions per node | 34.3 |
| IPC | 1.44 |

The bookkeeping costs about as much per line as the parser does (135 cycles per line, [front-end-optimizations.md](../../docs/research/front-end-optimizations.md)), and 5% of H5's 3,000 cycles per line. The loop has not been tuned: it searches the operand stack for each list's start marker and tests every node's kind against several tables. Type rules come on top of this.

## Against a tree

Added 2026-11-12, to answer the owner's question of whether one postorder pass is the fastest shape (question 1 of the sheet). The same bookkeeping runs as a recursive walk over a tree, in two layouts:
- **linked:** each node has a first-child and a next-sibling index, 8 bytes per node;
- **array:** each node has a start and a count into one array of child indices, as Zig's AST keeps lists in `extra_data`.

Markers become no nodes in either tree, as in Zig's AST. Each tree is built from the parser's postorder output in a separate pass. Before timing, the program checks that both walks write the same IR kinds and types, in the same order, as the postorder pass, file by file. It also checks the parser's list of top-level statements (`Nodes.top_ends`, added 2026-11-12): the roots left on the tree builder's stack must be exactly the nodes `top_ends[i] - 1`, and the last entry must equal the node count.

Measured on 2026-11-12 with the load average at 10–11 from other sessions; each figure is the median of 5 runs, and three invocations agreed within 3%:

| Pass | Cycles per line | Instructions per node |
|---|---|---|
| postorder pass | 153–155 | 42.2 |
| linked tree: build only | 123 | 32.0 |
| linked tree: build and walk | 322 | 85.2 |
| array tree: build only | 126–129 | 40.1 |
| array tree: build and walk | 326–334 | 92.1 |

- The walk alone costs about 200 cycles per line in both layouts, 30% more than the postorder pass. The postorder pass is one flat loop. The walk makes one call per node, with its prologue and epilogue, and reads each child's index before it can visit that child.
- Building the tree in its own pass costs another 123–129 cycles per line. A parser that writes child links as it parses would pay less than that, but not nothing: 8 or more bytes per node in place of the markers' 5.
- So a tree costs 45–180 cycles per line more than the postorder pass, 1.5–6% of H5's checking budget, before any type rule.

The postorder pass measured 149 cycles per line and 34.3 instructions per node in the first run, earlier the same day, with the same `check_floor` function. The parser changed in between; the cause of the 8 extra instructions per node was not investigated.
