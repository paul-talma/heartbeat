CC := clang
CFLAGS := -O2 -Wall -Wextra -pthread

BUILD := build
SRCS := $(wildcard src/*.c)
OBJS := $(SRCS:src/%.c=$(BUILD)/%.o)

all: $(BUILD)/main

$(BUILD)/main: $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -MMD -c $< -o $@

$(BUILD):
	mkdir -p $@

run: $(BUILD)/main
	./$< $(ARGS)

clean:
	rm -rf $(BUILD)

-include $(OBJS:.o=.d)

.PHONY: all run clean
