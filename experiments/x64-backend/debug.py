import re
import subprocess
import sys

host = sys.argv[1]


def session(function, commands):
    args = ["gdb", "-nx", "-batch", "-ex", "set debuginfod enabled off", "-ex", "set breakpoint pending on"]
    for c in commands:
        args += ["-ex", c]
    args += ["--args", host, "debug.chibi", function]
    return subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=120).stdout


checks = []

out = session("run", ["break debug.chibi:5", "run", "bt", "next", "next", "finish", "delete", "continue"])
checks += [
    ("a breakpoint on a chibi line stops there", r"Breakpoint 1, fib \(\) at debug\.chibi:5", out),
    ("the backtrace walks through every JIT frame", r"#0  fib \(\) at debug\.chibi:5\n#1  0x[0-9a-f]+ in sum \(\) at debug\.chibi:13\n#2  0x[0-9a-f]+ in run \(\) at debug\.chibi:19\n#3  0x[0-9a-f]+ in main ", out),
    ("next steps over a call to the following line", r"\n6\s+b := fib\(n - 2\)\n7\s+return a \+ b\n", out),
    ("finish returns to the caller's line", r"7\s+return a \+ b\n(Run till exit from #0 +fib \(\) at debug\.chibi:7\n)?0x[0-9a-f]+ in sum \(\) at debug\.chibi:13\n", out),
    ("the program runs to its end", r"run returned 88", out),
    ("the perf map names the functions", r"perf map: [0-9a-f]+ [0-9a-f]+ fib\nperf map: [0-9a-f]+ [0-9a-f]+ sum\nperf map: [0-9a-f]+ [0-9a-f]+ run\n", out),
]

out = session("boom", ["run", "bt"])
checks += [
    ("a trap stops in the function that trapped", r"Program received signal SIGILL.*\n0x[0-9a-f]+ in cube \(\) at debug\.chibi:\d+", out),
    ("the backtrace from a trap reaches main", r"#1  0x[0-9a-f]+ in boom \(\) at debug\.chibi:29\n#2  0x[0-9a-f]+ in main ", out),
]

failed = 0
for name, pattern, text in checks:
    ok = re.search(pattern, text) is not None
    failed += not ok
    print(("ok   " if ok else "FAIL ") + name)
    if not ok:
        print(text)
print(f"x64 debug: {len(checks) - failed} of {len(checks)} checks passed")
sys.exit(1 if failed else 0)
