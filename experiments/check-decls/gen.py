import bisect
import random
import sys

def main():
    files = int(sys.argv[1])
    decls_per_file = int(sys.argv[2])
    out_dir = sys.argv[3]
    rng = random.Random(1)
    total = files * decls_per_file
    kinds = []
    for i in range(total):
        r = rng.random()
        kinds.append("struct" if r < 0.25 else "enum" if r < 0.35 else "const" if r < 0.55 else "fn")
    names = [f"{k[0].upper()}{i}" if k != "fn" else f"f{i}" for i, k in enumerate(kinds)]
    structs = [i for i, k in enumerate(kinds) if k == "struct"]
    enums = [i for i, k in enumerate(kinds) if k == "enum"]
    consts = [i for i, k in enumerate(kinds) if k == "const"]
    scalars = ["int", "u8", "u16", "u32", "i32", "f32", "f64", "bool", "uintptr"]

    def pick(pool, i):
        if not pool:
            return None
        return pool[rng.randrange(len(pool))]

    def type_expr(i, by_value_ok):
        r = rng.random()
        if r < 0.45:
            return rng.choice(scalars)
        if r < 0.65 and structs:
            return "*" + names[pick(structs, i)]
        if r < 0.75 and structs:
            return "?*" + names[pick(structs, i)]
        if r < 0.85:
            return "[]" + rng.choice(scalars)
        if r < 0.92 and enums:
            return names[pick(enums, i)]
        n = names[pick(consts, i)] if consts and rng.random() < 0.5 else str(rng.randrange(1, 64))
        return f"[{n}]" + rng.choice(scalars)

    for f in range(files):
        lines = [f"package bench{f}", ""]
        for i in range(f * decls_per_file, (f + 1) * decls_per_file):
            k = kinds[i]
            pub = "pub " if rng.random() < 0.3 else ""
            if k == "struct":
                lines.append(f"{pub}struct {names[i]} {{")
                for m in range(rng.randrange(2, 8)):
                    lines.append(f"    field{m}: {type_expr(i, False)},")
                lines.append("}")
            elif k == "enum":
                lines.append(f"{pub}enum {names[i]}: u8 {{")
                v = 0
                for m in range(rng.randrange(3, 10)):
                    if rng.random() < 0.2:
                        v += rng.randrange(1, 5)
                        lines.append(f"    member{m} = {v},")
                    else:
                        lines.append(f"    member{m},")
                    v += 1
                lines.append("}")
            elif k == "const":
                lower = bisect.bisect_left(consts, i)
                if lower and rng.random() < 0.6:
                    a = names[consts[rng.randrange(lower)]]
                    lines.append(f"{pub}const {names[i]} := {a} % 1000 * {rng.randrange(1, 8)} + {rng.randrange(0, 100)} % 7")
                else:
                    lines.append(f"{pub}const {names[i]}: u32 = {rng.randrange(0, 1 << 20)} >> {rng.randrange(0, 8)}")
            else:
                params = ", ".join(f"p{p}: {type_expr(i, True)}" for p in range(rng.randrange(0, 5)))
                ret = f" -> {type_expr(i, True)}" if rng.random() < 0.7 or pub else ""
                lines.append(f"{pub}fn {names[i]}({params}){ret} {{")
                if ret:
                    lines.append("    while true {}")
                lines.append("}")
            lines.append("")
        with open(f"{out_dir}/bench{f}.chibi", "w") as out:
            out.write("\n".join(lines))

main()
