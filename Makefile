ifeq ($(origin CC),default)
CC    := clang
endif
OS    := $(shell uname -s)
ARCH  := $(shell uname -m)

ifeq ($(OS),Darwin)
CPU      := -arch arm64 -arch x86_64 -mmacosx-version-min=11.0
HOST     := -arch $(ARCH) -mmacosx-version-min=11.0
PROFDATA ?= xcrun llvm-profdata
else ifeq ($(ARCH),x86_64)
CPU      := -march=x86-64-v2
HOST     := $(CPU)
QEMU     := $(shell command -v qemu-aarch64-static)
endif
PROFDATA ?= llvm-profdata

WARN  := -std=c23 -Wall -Wextra -Werror
FAST  := $(WARN) -O3 -flto -falign-functions=64 $(HOST)
BENCH := $(FAST) -DNDEBUG -pthread
SAFE  := $(WARN) -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -rdynamic $(CPU)
ARM64 := $(WARN) -g -O1 -fsanitize=undefined -fsanitize-trap=all -fno-omit-frame-pointer -rdynamic --target=aarch64-linux-gnu

SRC       := $(filter-out src/main.c, $(wildcard src/*.c vendor/*/*.c))
TEST_SRC  := test/*.c test/random/*.c $(SRC)
BENCH_SRC := bench/*.c test/random/*.c $(SRC)

run:
	@mkdir -p build
	$(CC) $(FAST) -g -o build/chibi src/main.c $(SRC)
	build/chibi test/hello.chibi

test:
	@mkdir -p build
	@$(CC) $(SAFE) -o build/test $(TEST_SRC)
	@$(CC) $(SAFE) -DCHIBI_SIMD_GENERIC -o build/test-generic $(TEST_SRC)
	@$(CC) $(SAFE) -DCHIBI_LEXER_BULK -o build/test-bulk $(TEST_SRC)
	@build/test
	@build/test-generic
	@build/test-bulk
ifeq ($(OS) $(ARCH),Darwin arm64)
	@arch -x86_64 build/test
	@arch -x86_64 build/test-bulk
endif
ifneq ($(QEMU),)
	@$(CC) $(ARM64) -o build/test-arm64 $(TEST_SRC)
	@QEMU_LD_PREFIX=/usr/aarch64-linux-gnu $(QEMU) build/test-arm64
endif

bench:
	@mkdir -p build
	@$(CC) $(BENCH) -o build/bench $(BENCH_SRC)
	@$(CC) $(BENCH) -fprofile-instr-generate -o build/bench-train $(BENCH_SRC)
	@LLVM_PROFILE_FILE=build/bench.profraw build/bench-train train
	@$(PROFDATA) merge -o build/bench.profdata build/bench.profraw
	@$(CC) $(BENCH) -fprofile-instr-use=build/bench.profdata -Wno-profile-instr-unprofiled -o build/bench-pgo $(BENCH_SRC)
	@echo "plain build"
	@build/bench
	@echo
	@echo "PGO build, trained on other generated programs"
	@build/bench-pgo

clean:
	rm -rf build

.PHONY: run test bench clean
