import re
import subprocess
import sys

ITERATIONS = 1000


def preprocess(path, triple):
    out = subprocess.run(
        ["clang", "-E", "-P", "-x", "assembler-with-cpp", "--target=" + triple, path],
        capture_output=True,
        text=True,
        check=True,
    )
    return [line.rstrip() for line in out.stdout.splitlines() if line.strip()]


def macros(lines):
    out = []
    inside = False
    for line in lines:
        if line.strip().startswith(".macro"):
            inside = True
        if inside:
            out.append(line)
        if line.strip() == ".endm":
            inside = False
    return out


def function(lines, name):
    start = lines.index(name + ":")
    out = []
    for line in lines[start + 1 :]:
        if line.strip().startswith(".globl") or line.strip() == ".p2align 6":
            break
        out.append(line)
    return out


def loop(body):
    start = body.index("1:")
    out = []
    for line in body[start + 1 :]:
        out.append(line)
        if re.match(r"^\s*(b|jmp)\s+1b$", line):
            return out
    raise ValueError("no back branch")


def without_match(body):
    out = []
    skipping = False
    for line in body:
        if line.strip() == "3:":
            skipping = False
            continue
        if skipping:
            continue
        out.append(line)
        if re.match(r"^\s*(b\.ne|jne)\s+3f$", line):
            skipping = True
    return out


def callee(lines, name):
    out = []
    for line in function(lines, name):
        out.append(line)
        if line.strip() == "ret":
            return out
    raise ValueError("no ret")


def with_callee(body, lines, name):
    out = []
    for line in body:
        out.append(line)
        if re.match(r"^\s*(blr|call)\b", line):
            out.extend(callee(lines, name))
    return out


def standalone(body):
    out = ["top:"]
    for line in body:
        if re.match(r"^\d+:$", line.strip()):
            continue
        out.append(re.sub(r"^(\s*(b|b\.\w+|j\w+)\s+)\d[bf]$", r"\g<1>top", line))
    return out


def estimate(text, triple, cpu, noalias):
    out = subprocess.run(
        ["llvm-mca", "-mtriple=" + triple, "-mcpu=" + cpu, "-iterations=%d" % ITERATIONS, "-noalias=" + noalias],
        input=text,
        capture_output=True,
        text=True,
        check=True,
    )
    assert "error" not in out.stderr, out.stderr
    total = re.search(r"Total Cycles:\s+(\d+)", out.stdout)
    count = re.search(r"Instructions:\s+(\d+)", out.stdout)
    return int(count.group(1)) // ITERATIONS, int(total.group(1)) / ITERATIONS


def main():
    path, triple, cpu = sys.argv[1], sys.argv[2], sys.argv[3]
    lines = preprocess(path, triple)
    header = [".intel_syntax noprefix"] if triple.startswith("x86_64") else []
    header += macros(lines)

    streams = [
        ("sum, A", loop(function(lines, "sum_a"))),
        ("sum, C", loop(function(lines, "sum_c"))),
        ("count, no match, A", without_match(loop(function(lines, "count_a")))),
        ("count, no match, C", without_match(loop(function(lines, "count_c")))),
        ("total, A, hidden pointer", with_callee(loop(function(lines, "total_a")), lines, "first_a")),
        ("total, A, two registers", with_callee(loop(function(lines, "total_a1")), lines, "first_a1")),
        ("total, C", with_callee(loop(function(lines, "total_c")), lines, "first_c")),
    ]

    print("llvm-mca -mcpu=%s, cycles per iteration" % cpu)
    print("  %-28s %12s %18s %22s" % ("", "instructions", "loads never alias", "loads wait for stores"))
    for name, body in streams:
        text = "\n".join(header + standalone(body)) + "\n"
        count, free = estimate(text, triple, cpu, "true")
        _, ordered = estimate(text, triple, cpu, "false")
        print("  %-28s %12d %18.2f %22.2f" % (name, count, free, ordered))


main()
