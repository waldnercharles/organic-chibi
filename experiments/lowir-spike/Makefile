CC     := clang
CXX    := g++
CPPGM  ?= /tmp/cppgm-0.10.2
BUILD  := $(abspath ../../build/lowir-spike)
SRC    := $(abspath $(filter-out ../../src/main.c ../../src/driver.c, $(wildcard ../../src/*.c ../../vendor/*/*.c)))
TESTS  := $(abspath test.c ../../test/ir_reference.c ../../test/backend_programs.c ../../test/vectors.c $(wildcard ../../test/random/*.c))
FLAGS  := -std=c23 -Wall -Wextra -Werror -I$(abspath .) -I$(CPPGM)/dev/src
LIB    := $(CPPGM)/dev/libcppgm_lowir.a -pthread

chibi:
	@mkdir -p $(BUILD)/chibi
	@cd $(BUILD)/chibi && $(CC) $(FLAGS) -O2 -DNDEBUG -c $(abspath main.c lowir.c) $(SRC)
	@$(CXX) -o $(BUILD)/chibi-lowir $(BUILD)/chibi/*.o $(LIB)

test:
	@mkdir -p $(BUILD)/test
	@cd $(BUILD)/test && $(CC) $(FLAGS) -g -O1 -c $(abspath lowir.c) $(TESTS) $(SRC)
	@$(CXX) -rdynamic -o $(BUILD)/lowir-test $(BUILD)/test/*.o $(LIB)
	@$(BUILD)/lowir-test

.PHONY: chibi test
