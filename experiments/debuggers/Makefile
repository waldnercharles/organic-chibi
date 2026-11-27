CC      := clang
GDB     := gdb
LLDB    := lldb
BUILD   := ../../build
SRC     := ../../src/native.c ../../src/symfile.c ../../src/gdb_jit.c ../../src/jit.c ../../src/check.c ../../src/exact.c ../../src/lexer.c ../../src/parser.c ../../src/arena.c ../../src/names.c ../../src/str.c ../../vendor/xxhash/xxhash.c
BACKEND := $(if $(filter x86_64 amd64,$(shell uname -m)),-DCHIBI_JIT_X64,)
FLAGS   := -std=c23 -g -O1 -Wall -Wextra -Werror -Wno-unused-function $(BACKEND)

gdb: host
	@python3 check.py gdb $(GDB) $(BUILD)/debugger-host

lldb: host
	@python3 check.py lldb $(LLDB) $(BUILD)/debugger-host

host:
	@mkdir -p $(BUILD)
	@$(CC) $(FLAGS) -o $(BUILD)/debugger-host host.c $(SRC)

.PHONY: gdb lldb host
