CC    ?= cc
WARN  := -std=c23 -Wall -Wextra -Werror
ifeq ($(shell uname -s),Darwin)
CPU   := -arch arm64 -arch x86_64 -mmacosx-version-min=11.0
else ifeq ($(shell uname -m),x86_64)
CPU   := -march=x86-64-v2
endif
FAST  := $(WARN) -O3 -flto $(CPU)
SAFE  := $(WARN) -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer $(CPU)
SRC   := $(filter-out src/main.c, $(wildcard src/*.c vendor/*/*.c))

run:
	@mkdir -p build
	$(CC) $(FAST) -g -o build/chibi src/main.c $(SRC)
	build/chibi test/hello.chibi

test:
	@mkdir -p build
	@$(CC) $(SAFE) -o build/test test/*.c $(SRC)
	@$(CC) $(SAFE) -DCHIBI_SIMD_GENERIC -o build/test-generic test/*.c $(SRC)
	@$(CC) $(SAFE) -DCHIBI_LEXER_BULK -o build/test-bulk test/*.c $(SRC)
	@build/test
	@build/test-generic
	@build/test-bulk
ifeq ($(shell uname -sm),Darwin arm64)
	@arch -x86_64 build/test
	@arch -x86_64 build/test-bulk
endif

bench:
	@mkdir -p build
	@$(CC) $(FAST) -DNDEBUG -pthread -o build/bench bench/*.c test/programs.c $(SRC)
	@build/bench

clean:
	rm -rf build

.PHONY: run test bench clean
