CC ?= cc
CFLAGS = -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic
.PHONY: test freestanding clean

test: test-adapter
	./test-adapter

test-adapter: doomgeneric_warden.c test/test_adapter.c test/doomgeneric.h
	$(CC) $(CFLAGS) -DWARDEN_DOOM_TEST -Itest doomgeneric_warden.c test/test_adapter.c -o $@

freestanding: doomgeneric_warden.c test/doomgeneric.h
	$(CC) $(CFLAGS) -ffreestanding -nostdlib -fno-builtin -fno-stack-protector -mgeneral-regs-only -Itest -c doomgeneric_warden.c -o adapter-freestanding.o

clean:
	rm -f test-adapter adapter-freestanding.o
