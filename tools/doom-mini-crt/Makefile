CC ?= cc
FLAGS = -std=c11 -Wall -Wextra -Werror -Wpedantic -fno-builtin
.PHONY: test freestanding sanitize clean
test:
	$(CC) $(FLAGS) -O2 mini_string.c test.c -o test-crt
	./test-crt
	$(CC) $(FLAGS) -O2 mini_extra.c test_extra.c -o test-extra
	./test-extra
freestanding:
	$(CC) $(FLAGS) -O2 -ffreestanding -nostdlib -fno-stack-protector -mgeneral-regs-only -c mini_string.c -o mini_string.o
	$(CC) $(FLAGS) -O2 -ffreestanding -nostdlib -fno-stack-protector -mgeneral-regs-only -c mini_extra.c -o mini_extra.o
	@! nm -u mini_string.o | grep -q .
	@! nm -u mini_extra.o | grep -q .
sanitize:
	$(CC) $(FLAGS) -O1 -g -fsanitize=undefined mini_string.c test.c -o test-ubsan
	./test-ubsan
	$(CC) $(FLAGS) -O1 -g -fsanitize=undefined mini_extra.c test_extra.c -o test-extra-ubsan
	./test-extra-ubsan
clean:
	rm -f test-crt test-extra test-ubsan test-extra-ubsan mini_string.o mini_extra.o
