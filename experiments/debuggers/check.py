import platform
import re
import subprocess
import sys

debugger = sys.argv[1]
program = sys.argv[2]
host = sys.argv[3]

ADDR = r"0x[0-9a-f]+"
TRAPS = r"(SIGILL|SIGTRAP|EXC_BREAKPOINT|EXC_BAD_INSTRUCTION)"
ARM64 = platform.machine() in ("arm64", "aarch64")


def run(args):
    return subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=120).stdout


def gdb(function, commands, source="debug.chibi"):
    args = [program, "-nx", "-batch", "-ex", "set debuginfod enabled off", "-ex", "set breakpoint pending on"]
    for c in commands:
        args += ["-ex", c]
    return run(args + ["--args", host, source, function])


def lldb(function, commands, on_crash, source="debug.chibi"):
    args = [program, "-b", "-o", "settings set plugin.jit-loader.gdb.enable on"]
    for c in commands:
        args += ["-o", c]
    for c in on_crash:
        args += ["-k", c]
    return run(args + ["--", host, source, function])


def frame(function, line):
    return rf"JIT\({ADDR}\)`{function}(\([^)]*\))? at debug\.chibi:{line}"


PERF_MAP = (
    "the perf map names the functions",
    r"perf map: [0-9a-f]+ [0-9a-f]+ fib\nperf map: [0-9a-f]+ [0-9a-f]+ sum\nperf map: [0-9a-f]+ [0-9a-f]+ run\n",
)

checks = []
if debugger == "gdb":
    out = gdb("run", ["break debug.chibi:5", "run", "bt", "next", "next", "finish", "delete", "continue"])
    checks += [
        ("a breakpoint on a chibi line stops there", r"Breakpoint 1, fib \(n=2\) at debug\.chibi:5", out),
        ("the backtrace walks through every JIT frame", rf"#0  fib \(n=2\) at debug\.chibi:5\n#1  {ADDR} in sum \(limit=10\) at debug\.chibi:13\n#2  {ADDR} in run \(\) at debug\.chibi:19\n#3  {ADDR} in main ", out),
        ("next steps over a call to the following line", r"\n6\s+b := fib\(n - 2\)\n7\s+return a \+ b\n", out),
        ("finish returns to the caller's line", rf"7\s+return a \+ b\n(Run till exit from #0 +fib \(n=2\) at debug\.chibi:7\n)?{ADDR} in sum \(limit=10\) at debug\.chibi:13\n", out),
        ("the program runs to its end", r"run returned 88", out),
        PERF_MAP + (out,),
    ]
    out = gdb("boom", ["run", "bt"])
    checks += [
        ("a trap stops at the line of the check that failed", rf"Program received signal {TRAPS}.*\n({ADDR} in )?cube \(x=4000000000\) at debug\.chibi:23\n", out),
        ("the backtrace from a trap reaches main", rf"#1  {ADDR} in boom \(\) at debug\.chibi:29\n#2  {ADDR} in main ", out),
    ]
    out = gdb("inspect", ["break debug.chibi:42", "run", "info args", "info locals", "whatis scale", "bt 2"])
    checks += [
        ("parameters show their values", r"\nn = 7\nscale = -3\n", out),
        ("locals show their values", r"\ncount = 11\nsmall = 200 '\\310'\nok = true\nmode = busy\n", out),
        ("types keep their chibi names", r"\ntype = i32\n", out),
        ("the backtrace shows the arguments", r"locals \(n=7, scale=-3\) at debug\.chibi:42", out),
    ]
    if ARM64:
        out = gdb("run", ["break types.chibi:17", "run", "print k", "print at", "print scale", "print p", "print xs", "print name", "print part.len", "print *q", "print mode", "print total"], "types.chibi")
        checks += [
            ("a struct parameter and a float show their values", r"\$1 = 10\n\$2 = \{x = 7, y = 8\}\n\$3 = 1\.5\n", out),
            ("a struct and an array show their values", r"\$4 = \{x = 5, y = -4\}\n\$5 = \{1, 2, 3\}\n", out),
            ("a string and a slice show their values", rf"\$6 = \{{ptr = {ADDR} \"chibi.*, len = 5\}}\n\$7 = 2\n", out),
            ("a pointer, an enum and an int show their values", r"\$8 = \{x = 5, y = -4\}\n\$9 = busy\n\$10 = 38\n", out),
        ]
else:
    out = lldb("run", ["breakpoint set --file debug.chibi --line 5", "run", "bt", "next", "next", "finish", "breakpoint delete -f", "continue"], [])
    checks += [
        ("a breakpoint on a chibi line stops there", rf"stop reason = breakpoint 1\.1\n +frame #0: {ADDR} {frame('fib', 5)}\n", out),
        ("the backtrace walks through every JIT frame", rf"\* frame #0: {ADDR} {frame('fib', 5)}\n +frame #1: {ADDR} {frame('sum', 13)}\n +frame #2: {ADDR} {frame('run', 19)}\n +frame #3: {ADDR} [^`\n]+`main", out),
        ("next steps over a call to the following line", rf"stop reason = step over\n +frame #0: {ADDR} {frame('fib', 6)}\n(.*\n)*?.*stop reason = step over\n +frame #0: {ADDR} {frame('fib', 7)}\n", out),
        ("finish returns to the caller's line", rf"stop reason = step out\n +frame #0: {ADDR} {frame('sum', 13)}\n", out),
        ("the program runs to its end", r"run returned 88", out),
        PERF_MAP + (out,),
    ]
    out = lldb("boom", ["run"], ["bt", "process kill"])
    checks += [
        ("a trap stops at the line of the check that failed", rf"stop reason = [^\n]*{TRAPS}[^\n]*\n +frame #0: {ADDR} {frame('cube', 23)}\n", out),
        ("the backtrace from a trap reaches main", rf"frame #1: {ADDR} {frame('boom', 29)}\n +frame #2: {ADDR} [^`\n]+`main", out),
    ]
    out = lldb("inspect", ["breakpoint set --file debug.chibi --line 42", "run", "frame variable"], [])
    checks += [
        ("parameters show their values", r"\(int\) n = 7\n\(i32\) scale = -3\n", out),
        ("locals show their values", r"\(int\) count = 11\n\(u8\) small = '\\xc8'\n\(bool\) ok = true\n\(Mode\) mode = busy\n", out),
    ]
    if ARM64:
        out = lldb("run", ["breakpoint set --file types.chibi --line 17", "run", "frame variable", "expression *q"], [], "types.chibi")
        checks += [
            ("a struct parameter and a float show their values", r"\(int\) k = 10\n\(Point\) at = \(x = 7, y = 8\)\n\(f64\) scale = 1\.5\n", out),
            ("a struct and an array show their values", r"\(Point\) p = \(x = 5, y = -4\)\n\(int\[3\]\) xs = \(\[0\] = 1, \[1\] = 2, \[2\] = 3\)\n", out),
            ("a string and a slice show their values", r"\(string\) name = \(ptr = \"chibi\", len = 5\)\n.*part = \(ptr = 0x[0-9a-f]+, len = 2\)\n", out),
            ("a pointer, an enum and an int show their values", r"\(Mode\) mode = busy\n\(int\) total = 38\n(.*\n)*.*\(Point\) \$0 = \(x = 5, y = -4\)", out),
        ]

failed = 0
for name, pattern, text in checks:
    ok = re.search(pattern, text) is not None
    failed += not ok
    print(("ok   " if ok else "FAIL ") + name)
    if not ok:
        print(text)
print(f"{debugger}: {len(checks) - failed} of {len(checks)} checks passed")
sys.exit(1 if failed else 0)
