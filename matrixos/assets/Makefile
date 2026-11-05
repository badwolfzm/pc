CC      ?= gcc
CFLAGS  ?= -O2 -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -D_GNU_SOURCE
LDFLAGS ?= -pthread

all: assetpack test_asset_store bench_asset_store test_mx_vfs_compat test_asset_to_gpu

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

# RCS reference (GPU sim) is built as a consumer; it is not edited here.
rcs.o: ../contracts/gpu-commands-ref/rcs.c ../contracts/gpu-commands-ref/rcs.h
	$(CC) -std=c11 -O1 -Wall -Wextra -Wshadow -c -o $@ $<

test_asset_to_gpu: test_asset_to_gpu.c asset_store.o rcs.o assetpack
	$(CC) $(CFLAGS) -I../contracts/gpu-commands-ref -o $@ test_asset_to_gpu.c asset_store.o rcs.o $(LDFLAGS)

test: all
	./test_asset_store ./.scratch-test
	./test_mx_vfs_compat ./.scratch-vfs
	./test_asset_to_gpu ./.scratch-gpu

asan: CFLAGS += -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
asan: LDFLAGS += -fsanitize=address,undefined
asan: clean all test

bench: all
	./bench_asset_store ./.scratch-bench 64 4

clean:
	rm -rf assetpack test_asset_store bench_asset_store test_mx_vfs_compat test_asset_to_gpu asset_store.o rcs.o .scratch-test .scratch-bench .scratch-vfs .scratch-gpu

.PHONY: all test asan bench clean
