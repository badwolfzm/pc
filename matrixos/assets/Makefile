CC      ?= gcc
CFLAGS  ?= -O2 -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -D_GNU_SOURCE
LDFLAGS ?= -pthread

all: assetpack test_asset_store bench_asset_store

asset_store.o: asset_store.c asset_store.h
	$(CC) $(CFLAGS) -c -o $@ $<

assetpack: assetpack.c asset_store.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_asset_store: test_asset_store.c asset_store.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

bench_asset_store: bench_asset_store.c asset_store.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test: all
	./test_asset_store ./.scratch-test

asan: CFLAGS += -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
asan: LDFLAGS += -fsanitize=address,undefined
asan: clean all test

bench: all
	./bench_asset_store ./.scratch-bench 64 4

clean:
	rm -rf assetpack test_asset_store bench_asset_store asset_store.o .scratch-test .scratch-bench

.PHONY: all test asan bench clean
