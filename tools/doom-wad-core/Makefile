CC ?= cc
CFLAGS = -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic
.PHONY: test freestanding clean
test: test-wad
	./test-wad
test-wad: wad.c wad.h test_wad.c
	$(CC) $(CFLAGS) wad.c test_wad.c -o $@
freestanding: wad.c wad.h
	$(CC) $(CFLAGS) -ffreestanding -fno-builtin -fno-stack-protector -mgeneral-regs-only -c wad.c -o wad.o
clean:
	rm -f test-wad wad.o
