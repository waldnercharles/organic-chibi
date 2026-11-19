import random
from fractions import Fraction


def round_bits(x, p):
    if x == 0:
        return x
    sign = -1 if x < 0 else 1
    x = abs(x)
    e = x.numerator.bit_length() - x.denominator.bit_length()
    if Fraction(2) ** e > x:
        e -= 1
    scale = Fraction(2) ** (p - 1 - e)
    m = x * scale
    q, r = divmod(m.numerator, m.denominator)
    twice = 2 * r
    if twice > m.denominator or (twice == m.denominator and q & 1):
        q += 1
    return sign * Fraction(q) / scale


LONG = ["3.14159265358979323846", "2.71828182845904523536", "1.41421356237309504880", "0.01745329251994329577"]


def literal(rng):
    k = rng.random()
    if k < 0.3:
        return str(rng.randint(1, 1000))
    if k < 0.9:
        places = rng.randint(1, 3)
        return f"{rng.randint(1, 100000) / 10**places:.{places}f}"
    return rng.choice(LONG)


def apply(op, a, b):
    if op == "+":
        return a + b
    if op == "-":
        return a - b
    if op == "*":
        return a * b
    return a / b


def evaluate(texts, ops, p):
    vals = [Fraction(t) if p is None else round_bits(Fraction(t), p) for t in texts]
    acc = vals[0]
    for op, v in zip(ops, vals[1:]):
        acc = apply(op, acc, v)
        if p is not None:
            acc = round_bits(acc, p)
    return acc


def main():
    rng = random.Random(1)
    n = 100_000
    counts = {("F3", 24): 0, ("F3", 53): 0, ("F2", 24): 0, ("F2", 53): 0}
    examples = {}
    for _ in range(n):
        size = rng.choice([2, 2, 3])
        texts = [literal(rng) for _ in range(size)]
        ops = [rng.choice("+-*/") for _ in range(size - 1)]
        exact = evaluate(texts, ops, None)
        if exact == 0:
            continue
        for name, inner in (("F3", 53), ("F2", 113)):
            approx = evaluate(texts, ops, inner)
            for target in (24, 53):
                if round_bits(approx, target) != round_bits(exact, target):
                    counts[(name, target)] += 1
                    expr = texts[0] + "".join(f" {o} {t}" for o, t in zip(ops, texts[1:]))
                    examples.setdefault((name, target), expr)
    for (name, target), c in counts.items():
        kind = "f32" if target == 24 else "f64"
        print(f"{name} into {kind}: {c} of {n} differ from exact ({100 * c / n:.3f}%), e.g. {examples.get((name, target))}")
    for texts, ops in ((["0.1", "0.2"], ["+"]), (["3.14159265358979323846", "180"], ["/"])):
        expr = texts[0] + "".join(f" {o} {t}" for o, t in zip(ops, texts[1:]))
        exact = float(round_bits(evaluate(texts, ops, None), 53))
        folded = float(evaluate(texts, ops, 53))
        print(f"{expr} into f64: exact {exact!r}, folded in f64 {folded!r}")


if __name__ == "__main__":
    main()
