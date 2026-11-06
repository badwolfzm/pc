CC ?= cc
FLAGS = -std=c11 -Wall -Wextra -Werror -Wpedantic -fno-builtin
.PHONY: test freestanding sanitize clean
test:
	$(CC) $(FLAGS) -O2 mini_string.c test.c -o test-crt
	./test-crt
freestanding:
	$(CC) $(FLAGS) -O2 -ffreestanding -nostdlib -fno-stack-protector -mgeneral-regs-only -c mini_string.c -o mini_string.o
	@! nm -u mini_string.o | grep -q .
sanitize:
	$(CC) $(FLAGS) -O1 -g -fsanitize=undefined mini_string.c test.c -o test-ubsan
	./test-ubsan
clean:
	rm -f test-crt test-ubsan mini_string.o
