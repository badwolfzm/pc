CC ?= cc
CFLAGS = -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic
STREAM = ../doom-wad-stream/wad_stream.c
.PHONY: test freestanding clean
test: adapter-test
	./adapter-test
adapter-test: adapter.c adapter.h test.c $(STREAM)
	$(CC) $(CFLAGS) adapter.c $(STREAM) test.c -o $@
freestanding:
	$(CC) $(CFLAGS) -ffreestanding -fno-builtin -c adapter.c -o adapter.o
clean:
	rm -f adapter-test adapter.o
