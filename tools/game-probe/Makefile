CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic
.PHONY: test clean
probe.o: probe.c probe.h
	$(CC) $(CFLAGS) -ffreestanding -fno-builtin -c probe.c -o $@
transport.o: transport.c transport.h
	$(CC) $(CFLAGS) -ffreestanding -fno-builtin -c transport.c -o $@
test_probe: probe.o test_probe.c
	$(CC) $(CFLAGS) test_probe.c probe.o -o $@
test_transport: transport.o test_transport.c
	$(CC) $(CFLAGS) test_transport.c transport.o -o $@
test: test_probe test_transport
	./test_probe
	./test_transport
clean:
	rm -f probe.o transport.o test_probe test_transport
