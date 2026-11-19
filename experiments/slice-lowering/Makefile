ifeq ($(origin CC),default)
CC    := clang
endif
ARCH  := $(shell uname -m)
BUILD := ../../build
FLAGS := -std=c23 -Wall -Wextra -Werror -O2

ifeq ($(ARCH),x86_64)
KERNELS := kernels_x86_64.S
else
KERNELS := kernels_arm64.S
endif

run:
	@mkdir -p $(BUILD)
	@$(CC) $(FLAGS) -c -o $(BUILD)/slice-reference.o reference.c
	@$(CC) -c -o $(BUILD)/slice-kernels.o $(KERNELS)
	@$(CC) $(FLAGS) -o $(BUILD)/slice-lowering bench.c $(BUILD)/slice-reference.o $(BUILD)/slice-kernels.o
	@$(BUILD)/slice-lowering

check-arm64:
	@mkdir -p $(BUILD)
	@$(CC) $(FLAGS) --target=aarch64-linux-gnu -c -o $(BUILD)/slice-reference-arm64.o reference.c
	@$(CC) --target=aarch64-linux-gnu -c -o $(BUILD)/slice-kernels-arm64.o kernels_arm64.S
	@$(CC) $(FLAGS) --target=aarch64-linux-gnu -o $(BUILD)/slice-lowering-arm64 bench.c $(BUILD)/slice-reference-arm64.o $(BUILD)/slice-kernels-arm64.o
	@QEMU_LD_PREFIX=/usr/aarch64-linux-gnu qemu-aarch64-static $(BUILD)/slice-lowering-arm64 check

mca:
	@python3 mca.py kernels_x86_64.S x86_64-linux-gnu graniterapids
	@python3 mca.py kernels_arm64.S aarch64-linux-gnu apple-m1

.PHONY: run check-arm64 mca
