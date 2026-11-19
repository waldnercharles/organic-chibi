CC    ?= cc
BUILD := ../../build
SRC   := ../../src/check.c ../../src/exact.c ../../src/lexer.c ../../src/parser.c ../../src/arena.c ../../src/names.c ../../src/str.c ../../vendor/xxhash/xxhash.c
CORPUS := /tmp/check-bodies-corpus

run:
	@mkdir -p $(BUILD) $(CORPUS)
	@python3 gen.py 200 100 $(CORPUS)
	@$(CC) -std=c23 -Wall -Wextra -Werror -O3 -flto -DNDEBUG -o $(BUILD)/check-bodies bench.c $(SRC)
	@$(BUILD)/check-bodies $(CORPUS)/*.chibi

.PHONY: run
