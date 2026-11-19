import random
import sys

INTS = ["int", "i32", "u8", "u32", "u64"]
FLOATS = ["f32", "f64"]
TYPES = INTS + FLOATS + ["bool"]


class Gen:
    def __init__(self, rng, fns):
        self.rng = rng
        self.fns = fns
        self.out = []
        self.vars = []
        self.fixed = set()
        self.counter = 0

    def name(self, prefix):
        self.counter += 1
        return f"{prefix}{self.counter}"

    def pick_var(self, t):
        names = [n for n, vt in self.vars if vt == t]
        return self.rng.choice(names) if names else None

    def literal(self, t):
        if t == "bool":
            return self.rng.choice(["true", "false"])
        if t in FLOATS:
            return f"{self.rng.randrange(1, 100)}.{self.rng.randrange(0, 100)}"
        return str(self.rng.randrange(1, 10))

    def call(self, t, depth):
        options = [f for f in self.fns if f[2] == t]
        if not options:
            return None
        name, params, _ = self.rng.choice(options)
        args = ", ".join(self.expr(p, depth + 1) for p in params)
        return f"{name}({args})"

    def atom(self, t, depth):
        r = self.rng.random()
        v = self.pick_var(t)
        if v and r < 0.6:
            return v
        if r < 0.7 and depth < 2:
            c = self.call(t, depth)
            if c:
                return c
        if r < 0.8 and t in INTS + FLOATS:
            other = self.rng.choice([x for x in INTS + FLOATS if x != t])
            ov = self.pick_var(other)
            if ov:
                return f"{t}({ov})"
        return v or self.literal(t)

    def expr(self, t, depth=0):
        if t == "bool":
            r = self.rng.random()
            if r < 0.5 or depth > 1:
                ct = self.rng.choice(INTS + FLOATS)
                a = self.pick_var(ct)
                if a:
                    op = self.rng.choice(["<", "<=", ">", ">=", "==", "!="])
                    return f"{a} {op} {self.atom(ct, depth + 1)}"
                return self.atom("bool", depth)
            if r < 0.8:
                op = self.rng.choice(["and", "or"])
                return f"{self.expr('bool', depth + 1)} {op} {self.expr('bool', depth + 1)}"
            return f"!{self.atom('bool', depth)}"
        v = self.pick_var(t)
        if not v or depth > 2 or self.rng.random() < 0.3:
            return self.atom(t, depth)
        ops = ["+", "-", "*"] + (["&", "|", "^", "%"] if t in INTS else ["/"])
        op = self.rng.choice(ops)
        right = self.atom(t, depth + 1)
        if op in ("%", "/") and not right.isidentifier():
            right = self.literal(t)
        if op == "%" and right == "0":
            right = "7"
        text = f"{v} {op} {right}"
        if self.rng.random() < 0.3 and depth < 2:
            text = f"({text}) {self.rng.choice(['+', '-'])} {self.atom(t, depth + 1)}"
        return text

    def emit(self, indent, text):
        self.out.append("    " * indent + text)

    def block(self, indent, n, loop_depth):
        saved = len(self.vars)
        for _ in range(n):
            self.statement(indent, loop_depth)
        del self.vars[saved:]

    def statement(self, indent, loop_depth):
        r = self.rng.random()
        if r < 0.35 or not self.vars:
            t = self.rng.choice(TYPES)
            name = self.name("v")
            if self.rng.random() < 0.5:
                self.emit(indent, f"{name}: {t} = {self.expr(t)}")
            else:
                e = self.expr(t)
                if t not in ("int", "f32", "bool") or e.isdigit() or "." in e.split()[0]:
                    self.emit(indent, f"{name}: {t} = {e}")
                else:
                    self.emit(indent, f"{name} := {e}")
            self.vars.append((name, t))
        elif r < 0.6:
            name, t = self.rng.choice(self.vars)
            if t == "bool":
                e = self.expr('bool')
                self.emit(indent, f"_ = {e}" if name in self.fixed else f"{name} = {e}")
            else:
                op = self.rng.choice(["=", "+=", "-=", "*="] + (["|=", "^="] if t in INTS else []))
                e = self.expr(t)
                if name not in self.fixed:
                    self.emit(indent, f"{name} {op} {e}")
                elif op == "=":
                    self.emit(indent, f"_ = {e}")
                else:
                    self.emit(indent, f"_ = {name} {op[0]} ({e})")
        elif r < 0.75 and indent < 4:
            self.emit(indent, f"if {self.expr('bool')} {{")
            self.block(indent + 1, self.rng.randrange(1, 4), loop_depth)
            if self.rng.random() < 0.4:
                self.emit(indent, "} else {")
                self.block(indent + 1, self.rng.randrange(1, 3), loop_depth)
            self.emit(indent, "}")
        elif r < 0.83 and indent < 4:
            i = self.name("x")
            bound = self.pick_var("int") or str(self.rng.randrange(2, 20))
            self.emit(indent, f"for {i} in 0..{bound} {{")
            self.vars.append((i, "int"))
            self.fixed.add(i)
            self.block(indent + 1, self.rng.randrange(1, 4), loop_depth + 1)
            self.vars.pop()
            self.emit(indent, "}")
        elif r < 0.88 and indent < 4:
            k = self.name("k")
            self.emit(indent, f"{k}: i32 = 0")
            self.emit(indent, f"while {k} < {self.rng.randrange(2, 50)} {{")
            self.emit(indent + 1, f"{k} += 1")
            self.vars.append((k, "i32"))
            self.block(indent + 1, self.rng.randrange(1, 3), loop_depth + 1)
            self.emit(indent, "}")
        elif r < 0.92 and loop_depth:
            self.emit(indent, f"if {self.expr('bool')} {{")
            self.emit(indent + 1, self.rng.choice(["break", "continue"]))
            self.emit(indent, "}")
        else:
            c = self.call(self.rng.choice(TYPES), 0)
            if c:
                self.emit(indent, f"_ = {c}")


def main():
    files = int(sys.argv[1])
    fns_per_file = int(sys.argv[2])
    out_dir = sys.argv[3]
    rng = random.Random(7)
    fns = []
    for f in range(files):
        for i in range(fns_per_file):
            params = [rng.choice(TYPES) for _ in range(rng.randrange(0, 4))]
            fns.append((f"f{f}_{i}", params, rng.choice(TYPES)))

    for f in range(files):
        lines = [f"package bench{f}", ""]
        for i in range(fns_per_file):
            name, params, ret = fns[f * fns_per_file + i]
            g = Gen(rng, fns)
            ps = []
            for p, t in enumerate(params):
                ps.append(f"p{p}: {t}")
                g.vars.append((f"p{p}", t))
                g.fixed.add(f"p{p}")
            lines.append(f"fn {name}({', '.join(ps)}) -> {ret} {{")
            g.block(1, rng.randrange(4, 16), 0)
            g.emit(1, f"return {g.expr(ret)}")
            lines.extend(g.out)
            lines.append("}")
            lines.append("")
        with open(f"{out_dir}/bench{f}.chibi", "w") as out:
            out.write("\n".join(lines))


main()
