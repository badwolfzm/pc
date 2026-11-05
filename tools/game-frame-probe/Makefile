CC ?= cc
CFLAGS ?= -O2
WARN = -std=c11 -Wall -Wextra -Werror -pedantic
.PHONY: all test clean
all: game-frame-probe
game-frame-probe: main.c render.c render.h
	$(CC) $(WARN) $(CFLAGS) -o $@ main.c render.c
test: game-frame-probe
	python3 test_probe.py ./game-frame-probe
clean:
	rm -f game-frame-probe
