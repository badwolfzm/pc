CC ?= cc
CFLAGS = -std=c11 -Wall -Wextra -Werror -Wpedantic -O2
STREAM = ../doom-wad-stream
.PHONY: test freestanding clean
all: test
test:
	$(CC) $(CFLAGS) -Itest -I$(STREAM) -I. doom_wad_bridge.c $(STREAM)/wad_stream.c test/test_bridge.c -o test-bridge
	./test-bridge
freestanding:
	$(CC) $(CFLAGS) -ffreestanding -fno-builtin -fno-stack-protector -mgeneral-regs-only -Itest -I$(STREAM) -I. -c doom_wad_bridge.c -o bridge-freestanding.o
clean:
	rm -f test-bridge bridge-freestanding.o
