CC     ?= cc
BUILD  := ../../build
SRC    := ../../src/arm64.c ../../src/check.c ../../src/exact.c ../../src/lexer.c ../../src/parser.c ../../src/arena.c ../../src/names.c ../../src/str.c ../../vendor/xxhash/xxhash.c
CORPUS := /tmp/check-bodies-corpus

run:
	@mkdir -p $(BUILD) $(CORPUS)
	@python3 ../check-bodies/gen.py 200 100 $(CORPUS)
	@$(CC) -std=c23 -Wall -Wextra -Werror -O3 -flto -DNDEBUG -falign-functions=64 -o $(BUILD)/arm64-backend bench.c $(SRC)
	@$(BUILD)/arm64-backend $(CORPUS)/*.chibi

output:
	@mkdir -p $(BUILD)
	@sed 's/REF_/O0_/' reference.c > $(BUILD)/reference_o0.c
	@sed 's/REF_/O2_/' reference.c > $(BUILD)/reference_o2.c
	@$(CC) -O0 -c $(BUILD)/reference_o0.c -o $(BUILD)/reference_o0.o
	@$(CC) -O2 -c $(BUILD)/reference_o2.c -o $(BUILD)/reference_o2.o
	@$(CC) -std=c23 -O2 -DNDEBUG -o $(BUILD)/arm64-output output.c $(BUILD)/reference_o0.o $(BUILD)/reference_o2.o $(SRC)
	@$(BUILD)/arm64-output

.PHONY: run output
