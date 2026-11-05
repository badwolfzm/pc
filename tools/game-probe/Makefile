CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic
.PHONY: test clean
probe.o: probe.c probe.h
	$(CC) $(CFLAGS) -ffreestanding -fno-builtin -c probe.c -o $@
transport.o: transport.c transport.h
	$(CC) $(CFLAGS) -ffreestanding -fno-builtin -c transport.c -o $@
game_loop.o: game_loop.c game_loop.h probe.h transport.h
	$(CC) $(CFLAGS) -ffreestanding -fno-builtin -c game_loop.c -o $@
test_probe: probe.o test_probe.c
	$(CC) $(CFLAGS) test_probe.c probe.o -o $@
test_transport: transport.o test_transport.c
	$(CC) $(CFLAGS) test_transport.c transport.o -o $@
test_game_loop: game_loop.o probe.o transport.o test_game_loop.c
	$(CC) $(CFLAGS) test_game_loop.c game_loop.o probe.o transport.o -o $@
test: test_probe test_transport test_game_loop
	./test_probe
	./test_transport
	./test_game_loop
clean:
	rm -f probe.o transport.o game_loop.o test_probe test_transport test_game_loop
