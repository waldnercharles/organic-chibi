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

TRANSLATED     := src/arena.c src/str.c src/names.c src/exact.c src/parser.c src/lexer.c \
                  src/check.c src/arm64.c src/driver.c src/c_source.c
TRANSLATED_SRC := test/*.c test/random/*.c $(filter-out $(TRANSLATED), $(SRC))
BOOTSTRAP      := $(TRANSLATED) src/main.c

run:
	@mkdir -p build
	$(CC) $(FAST) -g -o build/chibi src/main.c $(SRC)
	build/chibi tokens test/hello.chibi

test:
	@mkdir -p build
	@$(CC) $(SAFE) -DCHIBI_TEST_CC='"$(CC)"' -o build/test $(TEST_SRC)
	@$(CC) $(SAFE) -DCHIBI_SIMD_GENERIC -o build/test-generic $(TEST_SRC)
	@$(CC) $(SAFE) -DCHIBI_LEXER_BULK -o build/test-bulk $(TEST_SRC)
	@build/test
	@build/test-generic
	@build/test-bulk
ifeq ($(OS) $(ARCH),Darwin arm64)
	@CHIBI_TEST_SECONDS=180 arch -x86_64 build/test
	@CHIBI_TEST_SECONDS=180 arch -x86_64 build/test-bulk
endif
ifneq ($(QEMU),)
	@$(CC) $(ARM64) -o build/test-arm64 $(TEST_SRC)
	@QEMU_LD_PREFIX=/usr/aarch64-linux-gnu $(QEMU) build/test-arm64
endif

translated:
	@mkdir -p build/translated/host build/translated/arm64
	@rm -f build/translated/host/*.chibi build/translated/arm64/*.chibi
	@$(CC) $(WARN) -g -O2 -o build/translate translate/*.c
	@$(CC) $(WARN) -g -O1 -o build/chibi-translated src/main.c $(SRC)
	@: > build/translated/entry.chibi
	@for f in $(BOOTSTRAP); do \
		$(CC) -std=c23 $(HOST) -DCHIBI_SIMD_GENERIC -Xclang -ast-dump=json -fsyntax-only $$f \
			> build/translated/host/$$(basename $$f .c).json || exit 1; \
	done
	@build/translate build/translated/host $(foreach f,$(BOOTSTRAP),build/translated/host/$(notdir $(f:.c=.json)) $(f))
	@build/chibi-translated c -o build/translated/host/lib.c build/translated/entry.chibi \
		$$(ls build/translated/host/*.chibi | grep -v -x build/translated/host/main.chibi)
	@build/chibi-translated c -o build/translated/host/chibi.c build/translated/entry.chibi build/translated/host/*.chibi
	@build/chibi-translated c --layouts -o build/translated/host/layouts.c build/translated/entry.chibi \
		$$(ls build/translated/host/*.chibi | grep -v -x build/translated/host/main.chibi)
	@$(CC) $(WARN) $(CPU) -fsyntax-only $(foreach h,$(wildcard src/*.h),-include $(h)) build/translated/host/layouts.c
	@$(CC) $(SAFE) -w -Dchibi_trap=chibi_trap_translated -Dchibi_context=chibi_context_translated \
		-c -o build/translated/host/lib.o build/translated/host/lib.c & lib=$$!; \
	$(CC) $(SAFE) -w -c -o build/translated/host/chibi.o build/translated/host/chibi.c && wait $$lib
	@$(CC) $(SAFE) -DCHIBI_TEST_CC='"$(CC)"' -o build/test-translated $(TRANSLATED_SRC) build/translated/host/lib.o
	@build/test-translated
	@$(CC) $(SAFE) -o build/chibi-bootstrap build/translated/host/chibi.o $(filter vendor/%, $(SRC))
	@build/chibi-bootstrap c -o build/translated/host/chibi.2.c build/translated/entry.chibi build/translated/host/*.chibi
	@cmp build/translated/host/chibi.c build/translated/host/chibi.2.c
	@echo "bootstrap: passed ($(ARCH))"
ifneq ($(QEMU),)
	@for f in $(BOOTSTRAP); do \
		$(CC) -std=c23 --target=aarch64-linux-gnu -DCHIBI_SIMD_GENERIC -Xclang -ast-dump=json -fsyntax-only $$f \
			> build/translated/arm64/$$(basename $$f .c).json || exit 1; \
	done
	@build/translate build/translated/arm64 $(foreach f,$(BOOTSTRAP),build/translated/arm64/$(notdir $(f:.c=.json)) $(f))
	@build/chibi-translated c --target arm64-linux -o build/translated/arm64/lib.c build/translated/entry.chibi \
		$$(ls build/translated/arm64/*.chibi | grep -v -x build/translated/arm64/main.chibi)
	@build/chibi-translated c --target arm64-linux -o build/translated/arm64/chibi.c build/translated/entry.chibi \
		build/translated/arm64/*.chibi
	@build/chibi-translated c --target arm64-linux --layouts -o build/translated/arm64/layouts.c \
		build/translated/entry.chibi $$(ls build/translated/arm64/*.chibi | grep -v -x build/translated/arm64/main.chibi)
	@$(CC) $(WARN) --target=aarch64-linux-gnu -fsyntax-only $(foreach h,$(wildcard src/*.h),-include $(h)) \
		build/translated/arm64/layouts.c
	@$(CC) $(ARM64) -w -Dchibi_trap=chibi_trap_translated -Dchibi_context=chibi_context_translated \
		-c -o build/translated/arm64/lib.o build/translated/arm64/lib.c & lib=$$!; \
	$(CC) $(ARM64) -w -c -o build/translated/arm64/chibi.o build/translated/arm64/chibi.c && wait $$lib
	@$(CC) $(ARM64) -o build/test-translated-arm64 $(TRANSLATED_SRC) build/translated/arm64/lib.o
	@QEMU_LD_PREFIX=/usr/aarch64-linux-gnu $(QEMU) build/test-translated-arm64
	@$(CC) $(ARM64) -o build/chibi-bootstrap-arm64 build/translated/arm64/chibi.o $(filter vendor/%, $(SRC))
	@QEMU_LD_PREFIX=/usr/aarch64-linux-gnu $(QEMU) build/chibi-bootstrap-arm64 c -o build/translated/arm64/chibi.2.c \
		build/translated/entry.chibi build/translated/arm64/*.chibi
	@cmp build/translated/arm64/chibi.c build/translated/arm64/chibi.2.c
	@echo "bootstrap: passed (arm64)"
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

.PHONY: run test translated bench clean
