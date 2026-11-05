# matrixos profile flags (from the uploaded faststripcc wrapper), fixed SIMD tier instead of -march=native
CC      ?= cc
OUT     ?= .            # build dir; the project-files mount is noexec, so use e.g. OUT=/tmp/gc-build
TIER    ?=            # e.g. -mavx2 for the avx2 tier
CFLAGS  ?= -std=c11 -O2 -pipe -fno-ident -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-plt -fomit-frame-pointer -Wall -Wextra -Wno-misleading-indentation $(TIER)

all: test test_assets bench

test: ghost_canvas.c test_ghost_canvas.c ghost_canvas.h
	$(CC) $(CFLAGS) ghost_canvas.c test_ghost_canvas.c -o $(OUT)/test_gc && $(OUT)/test_gc

test_assets: ghost_canvas.c canvas_assets.c test_canvas_assets.c
	$(CC) $(CFLAGS) ghost_canvas.c canvas_assets.c test_canvas_assets.c -o $(OUT)/test_assets && $(OUT)/test_assets

# compile check of the asset-store glue against ../assets/asset_store.h (needs that module present)
glue: ghost_canvas.c canvas_assets.c
	$(CC) $(CFLAGS) -DGC_HAVE_ASSET_STORE -c canvas_assets.c -o $(OUT)/canvas_assets.o

asan: ghost_canvas.c test_ghost_canvas.c ghost_canvas.h
	$(CC) -std=c11 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer ghost_canvas.c test_ghost_canvas.c -o $(OUT)/test_gc_asan && $(OUT)/test_gc_asan

bench: ghost_canvas.c bench_ghost_canvas.c ghost_canvas.h
	$(CC) $(CFLAGS) ghost_canvas.c bench_ghost_canvas.c -o $(OUT)/bench_gc && $(OUT)/bench_gc bench.csv

clean:
	rm -f $(OUT)/test_gc $(OUT)/test_gc_asan $(OUT)/bench_gc
.PHONY: all test test_assets glue asan bench clean
