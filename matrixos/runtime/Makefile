# matrixos/runtime — render service runtime: ring + validator + canvas + assets, plus a sample game.
# Builds against the sibling folders (../contracts/gpu-commands-ref, ../canvas, ../assets,
# ../frontend/include). The project-files mount is noexec: build with OUT=/tmp/mxrt-build.
CC      ?= gcc
OUT     ?= .
TIER    ?=
CFLAGS  ?= -std=c11 -O2 -g -pipe -Wall -Wextra -Werror -Wshadow -Wno-misleading-indentation $(TIER)
LDFLAGS ?= -pthread

DEPS = ../contracts/gpu-commands-ref/rcs.c ../canvas/ghost_canvas.c ../assets/asset_store.c
SRC  = mx_ring.c mx_render.c
HDRS = mx_ring.h mx_render.h mx_rcs_build.h ../contracts/gpu-commands-ref/rcs.h ../canvas/ghost_canvas.h ../assets/asset_store.h ../frontend/include/mx_input.h

all: $(OUT)/test_runtime $(OUT)/bench_runtime $(OUT)/game_demo $(OUT)/assetpack

$(OUT)/assetpack: ../assets/assetpack.c ../assets/asset_store.c
	mkdir -p $(OUT) && $(CC) -std=c11 -O2 -Wall -Wno-format-truncation -o $@ $^ $(LDFLAGS)

$(OUT)/test_runtime: test_runtime.c $(SRC) $(DEPS) $(HDRS)
	mkdir -p $(OUT) && $(CC) $(CFLAGS) -o $@ test_runtime.c $(SRC) $(DEPS) $(LDFLAGS)

$(OUT)/bench_runtime: bench_runtime.c $(SRC) $(DEPS) $(HDRS)
	mkdir -p $(OUT) && $(CC) $(CFLAGS) -o $@ bench_runtime.c $(SRC) $(DEPS) $(LDFLAGS)

$(OUT)/game_demo: game_demo.c $(SRC) $(DEPS) $(HDRS)
	mkdir -p $(OUT) && $(CC) $(CFLAGS) -o $@ game_demo.c $(SRC) $(DEPS) $(LDFLAGS)

test: $(OUT)/test_runtime $(OUT)/assetpack
	cd $(OUT) && ./test_runtime ./.scratch-runtime

asan: CFLAGS += -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all
asan: LDFLAGS += -fsanitize=address,undefined
asan: clean test

bench: $(OUT)/bench_runtime
	cd $(OUT) && ./bench_runtime bench.csv

demo: $(OUT)/game_demo $(OUT)/assetpack
	cd $(OUT) && ./game_demo ./.scratch-demo 120

clean:
	rm -rf $(OUT)/test_runtime $(OUT)/bench_runtime $(OUT)/game_demo $(OUT)/assetpack $(OUT)/.scratch-runtime $(OUT)/.scratch-demo
.PHONY: all test asan bench demo clean
