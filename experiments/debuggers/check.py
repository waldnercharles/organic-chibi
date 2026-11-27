import re
import subprocess
import sys

debugger = sys.argv[1]
program = sys.argv[2]
host = sys.argv[3]

ADDR = r"0x[0-9a-f]+"
TRAPS = r"(SIGILL|SIGTRAP|EXC_BREAKPOINT|EXC_BAD_INSTRUCTION)"


def run(args):
    return subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=120).stdout


def gdb(function, commands):
    args = [program, "-nx", "-batch", "-ex", "set debuginfod enabled off", "-ex", "set breakpoint pending on"]
    for c in commands:
        args += ["-ex", c]
    return run(args + ["--args", host, "debug.chibi", function])


def lldb(function, commands, on_crash):
    args = [program, "-b", "-o", "settings set plugin.jit-loader.gdb.enable on"]
    for c in commands:
        args += ["-o", c]
    for c in on_crash:
        args += ["-k", c]
    return run(args + ["--", host, "debug.chibi", function])


def frame(function, line):
    return rf"JIT\({ADDR}\)`{function} at debug\.chibi:{line}"


PERF_MAP = (
    "the perf map names the functions",
    r"perf map: [0-9a-f]+ [0-9a-f]+ fib\nperf map: [0-9a-f]+ [0-9a-f]+ sum\nperf map: [0-9a-f]+ [0-9a-f]+ run\n",
)

checks = []
if debugger == "gdb":
    out = gdb("run", ["break debug.chibi:5", "run", "bt", "next", "next", "finish", "delete", "continue"])
    checks += [
        ("a breakpoint on a chibi line stops there", r"Breakpoint 1, fib \(\) at debug\.chibi:5", out),
        ("the backtrace walks through every JIT frame", rf"#0  fib \(\) at debug\.chibi:5\n#1  {ADDR} in sum \(\) at debug\.chibi:13\n#2  {ADDR} in run \(\) at debug\.chibi:19\n#3  {ADDR} in main ", out),
        ("next steps over a call to the following line", r"\n6\s+b := fib\(n - 2\)\n7\s+return a \+ b\n", out),
        ("finish returns to the caller's line", rf"7\s+return a \+ b\n(Run till exit from #0 +fib \(\) at debug\.chibi:7\n)?{ADDR} in sum \(\) at debug\.chibi:13\n", out),
        ("the program runs to its end", r"run returned 88", out),
        PERF_MAP + (out,),
    ]
    out = gdb("boom", ["run", "bt"])
    checks += [
        ("a trap stops in the function that trapped", rf"Program received signal {TRAPS}.*\n{ADDR} in cube \(\) at debug\.chibi:\d+", out),
        ("the backtrace from a trap reaches main", rf"#1  {ADDR} in boom \(\) at debug\.chibi:29\n#2  {ADDR} in main ", out),
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
        ("a trap stops in the function that trapped", rf"stop reason = [^\n]*{TRAPS}[^\n]*\n +frame #0: {ADDR} {frame('cube', r'\d+')}", out),
        ("the backtrace from a trap reaches main", rf"frame #1: {ADDR} {frame('boom', 29)}\n +frame #2: {ADDR} [^`\n]+`main", out),
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
