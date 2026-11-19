CC    ?= cc
WARN  := -std=c23 -Wall -Wextra -Werror
ifeq ($(shell uname -s),Darwin)
CPU   := -arch arm64 -arch x86_64 -mmacosx-version-min=11.0
HOST  := -arch $(shell uname -m) -mmacosx-version-min=11.0
PROFDATA ?= xcrun llvm-profdata
else ifeq ($(shell uname -m),x86_64)
CPU   := -march=x86-64-v2
HOST  := $(CPU)
endif
PROFDATA ?= llvm-profdata
FAST  := $(WARN) -O3 -flto $(CPU)
BENCH := $(WARN) -O3 -flto $(HOST) -DNDEBUG -pthread
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
	@$(CC) $(BENCH) -o build/bench bench/*.c test/programs.c $(SRC)
	@$(CC) $(BENCH) -fprofile-instr-generate -o build/bench-train bench/*.c test/programs.c $(SRC)
	@LLVM_PROFILE_FILE=build/bench.profraw build/bench-train train
	@$(PROFDATA) merge -o build/bench.profdata build/bench.profraw
	@$(CC) $(BENCH) -fprofile-instr-use=build/bench.profdata -Wno-profile-instr-unprofiled -o build/bench-pgo bench/*.c test/programs.c $(SRC)
	@echo "plain build"
	@build/bench
	@echo
	@echo "PGO build, trained on other generated programs"
	@build/bench-pgo

clean:
	rm -rf build

.PHONY: run test bench clean
