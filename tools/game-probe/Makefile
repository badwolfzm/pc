CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic
.PHONY: test clean
probe.o: probe.c probe.h
	$(CC) $(CFLAGS) -ffreestanding -fno-builtin -c probe.c -o $@
test_probe: probe.o test_probe.c
	$(CC) $(CFLAGS) test_probe.c probe.o -o $@
test: test_probe
	./test_probe
clean:
	rm -f probe.o test_probe
