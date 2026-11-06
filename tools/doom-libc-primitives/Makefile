CC ?= cc
CFLAGS = -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic -fno-builtin
.PHONY: test freestanding clean

test:
	$(CC) $(CFLAGS) doom_libc.c test_primitives.c -o test-primitives
	./test-primitives

freestanding:
	$(CC) $(CFLAGS) -ffreestanding -fno-stack-protector -mgeneral-regs-only -DWARDEN_DOOM_EXPORT_LIBC_NAMES -c doom_libc.c -o doom-libc-freestanding.o
	nm -g doom-libc-freestanding.o | grep ' T memcpy'
	nm -g doom-libc-freestanding.o | grep ' T memmove'
	nm -g doom-libc-freestanding.o | grep ' T memset'

clean:
	rm -f test-primitives doom-libc-freestanding.o
