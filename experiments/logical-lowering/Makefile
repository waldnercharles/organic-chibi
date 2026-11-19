CC     ?= cc
BUILD  := ../../build
COMMON := ../../src/exact.c ../../src/lexer.c ../../src/parser.c ../../src/arena.c ../../src/names.c ../../src/str.c ../../vendor/xxhash/xxhash.c
FLAGS  := -std=c23 -Wall -Wextra -Werror -O3 -flto -DNDEBUG -I../../src
BODIES := /tmp/check-bodies-corpus
DENSE  := /tmp/logical-dense-corpus

run:
	@mkdir -p $(BUILD) $(BODIES) $(DENSE)
	@python3 ../check-bodies/gen.py 200 100 $(BODIES)
	@python3 gen_dense.py 200 100 $(DENSE)
	@$(CC) $(FLAGS) -o $(BUILD)/logical-a ../check-bodies/bench.c check_a.c $(COMMON)
	@$(CC) $(FLAGS) -o $(BUILD)/logical-b ../check-bodies/bench.c ../../src/check.c $(COMMON)
	@$(CC) -O2 -Wall -Wextra -Werror -o $(BUILD)/logical-runtime runtime.c runtime.S
	@for i in 1 2 3; do \
		echo "A, step-2 corpus"; $(BUILD)/logical-a $(BODIES)/*.chibi | tail -3; \
		echo "B, step-2 corpus"; $(BUILD)/logical-b $(BODIES)/*.chibi | tail -3; \
		echo "A, and/or corpus"; $(BUILD)/logical-a $(DENSE)/*.chibi | tail -3; \
		echo "B, and/or corpus"; $(BUILD)/logical-b $(DENSE)/*.chibi | tail -3; \
	done
	@$(BUILD)/logical-runtime

.PHONY: run
