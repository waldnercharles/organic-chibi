# Folding float constants: f64, f128 or exact

This experiment supports question 6 of [docs/research/checker-and-ir.md](../../docs/research/checker-and-ir.md) (section 4.2). It compares three ways to fold an untyped float constant expression, each rounded into `f32` and `f64`:
- folding in f64 (option F3);
- folding in f128 (option F2);
- exact fractions, rounded once (option F1).

```bash
python3 rounding.py
```

`rounding.py` generates 100,000 expressions of two or three literals shaped like game constants, with `+`, `-`, `*` and `/`. The literals are integers, numbers with one to three decimal places, and long values such as π. Every value is a Python `Fraction`, and each step is rounded to the format's precision, with ties to even.

| Folded in | Into `f32` | Into `f64` |
|---|---|---|
| f64 | 0 differ | 27,144 differ (27%) |
| f128 | 0 differ | 0 differ |

Examples printed by the script:
- `0.1 + 0.2` into `f64` is `0.3` exactly, and `0.30000000000000004` folded in f64;
- `π / 180` gives the same `f64` either way.

The owner chose exact fractions on 2026-11-13.
