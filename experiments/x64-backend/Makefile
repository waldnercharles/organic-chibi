CC    := clang
BUILD := ../../build
SRC   := ../../src/native.c ../../src/jit.c ../../src/check.c ../../src/exact.c ../../src/lexer.c ../../src/parser.c ../../src/arena.c ../../src/names.c ../../src/str.c ../../vendor/xxhash/xxhash.c
TEST  := ../../test/backend_programs.c ../../test/ir_reference.c
FLAGS := -std=c23 -g -O1 -Wall -Wextra -Werror -Wno-unused-function -DCHIBI_JIT_X64

check:
	@mkdir -p $(BUILD)
	@$(CC) $(FLAGS) -o $(BUILD)/x64-check check.c $(SRC) $(TEST) -lm
	@$(BUILD)/x64-check $(ARGS)

encode:
	@mkdir -p $(BUILD)
	@$(CC) $(FLAGS) -o $(BUILD)/x64-encode encode.c
	@$(BUILD)/x64-encode > $(BUILD)/x64-encode.txt
	@python3 encode.py $(BUILD)/x64-encode.txt

.PHONY: check encode
