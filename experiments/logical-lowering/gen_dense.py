import random
import sys


def compare(rng, names):
    a, b = rng.sample(names, 2)
    return f"{a} {rng.choice(['<', '>', '<=', '>=', '==', '!='])} {b}"


def operand(rng, names, flags):
    return rng.choice(flags) if rng.random() < 0.3 else compare(rng, names)


def logical(rng, names, flags):
    n = rng.choice([2, 2, 2, 3])
    parts = [operand(rng, names, flags) for _ in range(n)]
    out = parts[0]
    for p in parts[1:]:
        out += f" {rng.choice(['and', 'or'])} {p}"
    return out


def main():
    files = int(sys.argv[1])
    fns_per_file = int(sys.argv[2])
    out_dir = sys.argv[3]
    rng = random.Random(11)
    for f in range(files):
        lines = [f"package dense{f}", ""]
        for i in range(fns_per_file):
            lines.append(f"fn d{f}_{i}(x: int, y: int, z: int, p: bool, q: bool) -> int {{")
            lines.append("    n: int = 0")
            lines.append("    w := x + y")
            names = ["x", "y", "z", "n", "w"]
            flags = ["p", "q"]
            for s in range(rng.randrange(6, 14)):
                r = rng.random()
                if r < 0.4:
                    lines.append(f"    if {logical(rng, names, flags)} {{")
                    lines.append("        n += 1")
                    if rng.random() < 0.4:
                        lines.append("    } else {")
                        lines.append("        n -= 1")
                    lines.append("    }")
                elif r < 0.6:
                    lines.append(f"    while {logical(rng, names, flags)} {{")
                    lines.append("        n += 1")
                    lines.append("    }")
                elif r < 0.85:
                    b = f"b{s}"
                    lines.append(f"    {b} := {logical(rng, names, flags)}")
                    flags.append(b)
                else:
                    lines.append(f"    n += {rng.randrange(1, 9)}")
            lines.append("    return n")
            lines.append("}")
            lines.append("")
        with open(f"{out_dir}/dense{f}.chibi", "w") as out:
            out.write("\n".join(lines))


main()
