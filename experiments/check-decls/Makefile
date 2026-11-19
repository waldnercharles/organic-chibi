CC    ?= cc
BUILD := ../../build
SRC   := ../../src/check.c ../../src/exact.c ../../src/lexer.c ../../src/parser.c ../../src/arena.c ../../src/names.c ../../src/str.c ../../vendor/xxhash/xxhash.c
CORPUS := /tmp/check-decls-corpus

run:
	@mkdir -p $(BUILD) $(CORPUS)
	@python3 gen.py 200 500 $(CORPUS)
	@$(CC) -std=c23 -Wall -Wextra -Werror -O3 -flto -DNDEBUG -o $(BUILD)/check-decls bench.c $(SRC)
	@$(BUILD)/check-decls $(CORPUS)/*.chibi

.PHONY: run
