CC ?= cc
CFLAGS = -std=c11 -Wall -Wextra -Werror -Wpedantic -O2
.PHONY: test sanitize freestanding clean
test: test-wad
	./test-wad
test-wad: wad_stream.c wad_stream.h test.c
	$(CC) $(CFLAGS) wad_stream.c test.c -o $@
sanitize:
	$(CC) -std=c11 -Wall -Wextra -Werror -Wpedantic -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer wad_stream.c test.c -o test-wad-sanitize
	./test-wad-sanitize
freestanding:
	$(CC) $(CFLAGS) -ffreestanding -fno-builtin -c wad_stream.c -o wad-stream.o
clean:
	rm -f test-wad test-wad-sanitize wad-stream.o
