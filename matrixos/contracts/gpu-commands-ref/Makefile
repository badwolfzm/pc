# Reference validator for the render service command set.
# The project-files mount may be noexec; build somewhere executable:
#   make BUILD=/tmp/rcs-build test
CC      ?= gcc
BUILD   ?= build
CFLAGS  ?= -std=c11 -O1 -g -Wall -Wextra -Werror -Wshadow -Wconversion -Wno-sign-conversion
SAN     ?= -fsanitize=address,undefined -fno-sanitize-recover=all

all: $(BUILD)/rcs_test

$(BUILD)/rcs_test: rcs.c rcs_test.c rcs.h
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(SAN) -o $@ rcs.c rcs_test.c

test: $(BUILD)/rcs_test
	$(BUILD)/rcs_test

clean:
	rm -rf $(BUILD)

.PHONY: all test clean
