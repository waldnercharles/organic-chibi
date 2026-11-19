CC    ?= cc
BUILD := ../../build
SRC   := ../../src/lexer.c ../../src/parser.c ../../src/arena.c ../../src/names.c ../../src/str.c ../../vendor/xxhash/xxhash.c ../../test/random/code.c

run:
	@mkdir -p $(BUILD)
	@$(CC) -std=c23 -Wall -Wextra -Werror -O2 -o $(BUILD)/check-floor floor.c $(SRC)
	@$(BUILD)/check-floor

.PHONY: run
