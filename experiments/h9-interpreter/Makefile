CC    ?= cc
BUILD := ../../build
WARN  := -std=c23 -Wall -Wextra -Werror
FAST  := $(WARN) -O3
SAFE  := $(WARN) -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer

run:
	@mkdir -p $(BUILD)
	@$(CC) $(FAST) -o $(BUILD)/h9 h9.c
	@$(BUILD)/h9

check:
	@mkdir -p $(BUILD)
	@$(CC) $(SAFE) -o $(BUILD)/h9-safe h9.c
	@$(BUILD)/h9-safe quick

.PHONY: run check
