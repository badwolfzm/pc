CC      ?= gcc
CFLAGS  ?= -O2 -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -D_GNU_SOURCE
LDFLAGS ?= -pthread

all: assetpack test_asset_store bench_asset_store test_mx_vfs_compat

asset_store.o: asset_store.c asset_store.h
	$(CC) $(CFLAGS) -c -o $@ $<

assetpack: assetpack.c asset_store.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_asset_store: test_asset_store.c asset_store.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

bench_asset_store: bench_asset_store.c asset_store.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_mx_vfs_compat: test_mx_vfs_compat.c mx_vfs_compat.h
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test: all
	./test_asset_store ./.scratch-test
	./test_mx_vfs_compat ./.scratch-vfs

asan: CFLAGS += -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
asan: LDFLAGS += -fsanitize=address,undefined
asan: clean all test

bench: all
	./bench_asset_store ./.scratch-bench 64 4

clean:
	rm -rf assetpack test_asset_store bench_asset_store test_mx_vfs_compat asset_store.o .scratch-test .scratch-bench .scratch-vfs

.PHONY: all test asan bench clean
