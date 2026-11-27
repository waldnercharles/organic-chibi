# Debuggers on JIT code

`host.c` compiles `debug.chibi` with `JIT_DEBUGGER | JIT_PERF_MAP` and calls the function named on its command line. It uses the x86-64 backend on x86-64 hosts and the arm64 backend elsewhere. `host FILE FUNCTION SYMFILE` also writes the object that the debugger reads, for `llvm-dwarfdump` and `readelf`.

`make gdb` and `make lldb` run the debugger on it twice and check the output:
- a pending breakpoint on `debug.chibi:5` binds when the code registers, and stops in `fib`;
- the backtrace walks `fib`, `sum` and `run` into C's `main`;
- `next` steps over a recursive call to the next line, twice;
- `finish` returns to `sum` at line 13;
- the program runs to its end, and the perf map names each function;
- a trap stops in the function that trapped, `cube`, with a backtrace through `boom` to `main`.

lldb loads JIT code through gdb's interface only when `plugin.jit-loader.gdb.enable` is on, which is the default on Linux and not on macOS, so the check turns it on. `make lldb LLDB=lldb-18` picks another lldb, as on Ubuntu.

CI runs both debuggers on x86-64 and arm64 Linux, and lldb on macOS.

Both debuggers show a trap at the wrong line: gdb at the function's last statement, lldb at line 0, because every check of one kind shares one trap stub at the end of the function. Step 3x.3.2 is about this.
