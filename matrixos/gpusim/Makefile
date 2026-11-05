# matrixos/gpusim - simulated memory-based GPU (render service on a CPU core)
# Build products go to $(B); the project-files share is noexec, so set B to a local dir there.
#   make B=/tmp/gpusim test     contract tests on the SIMD backend, all tiers, ASan+UBSan
#   make B=/tmp/gpusim bench    720p game workload, frame times per tier
CC      ?= gcc
B       ?= build
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra -Werror -Wshadow -Wno-sign-conversion -ffp-contract=off -fno-strict-aliasing
SAN      = -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all
REF      = ../contracts/gpu-commands-ref
SUP      = ../superops/mx_superops.c ../superops/mx_superops_ref.c
CORE     = gpusim.c $(REF)/rcs.c $(SUP) ../assets/asset_store.c

all: $(B)/gpusim_game $(B)/assetpack $(B)/rcs_test_simd

$(B):
	mkdir -p $(B)

$(B)/assetpack: ../assets/assetpack.c ../assets/asset_store.c | $(B)
	$(CC) -O2 -std=c11 -D_GNU_SOURCE -o $@ $^ -pthread

$(B)/gpusim_game: gpusim_game.c gpusim.h $(CORE) | $(B)
	$(CC) $(CFLAGS) -DNDEBUG -DRCS_USE_SUPEROPS -o $@ gpusim_game.c $(CORE) -pthread

$(B)/gpusim_game_asan: gpusim_game.c gpusim.h $(CORE) | $(B)
	$(CC) $(CFLAGS) $(SAN) -DRCS_USE_SUPEROPS -o $@ gpusim_game.c $(CORE) -pthread

# The reference contract suite (57 685 checks) run on the super-op backend.
$(B)/rcs_test_simd: $(REF)/rcs_test.c $(REF)/rcs.c $(SUP) tier_env.c | $(B)
	$(CC) $(CFLAGS) $(SAN) -Wconversion -DRCS_USE_SUPEROPS -o $@ $(REF)/rcs.c $(REF)/rcs_test.c $(SUP) tier_env.c

test: $(B)/rcs_test_simd $(B)/gpusim_game_asan $(B)/assetpack
	for t in scalar v128 v256; do MXS_TIER=$$t $(B)/rcs_test_simd 2>&1 | grep -E 'tier|rcs_test:' || exit 1; done
	$(B)/gpusim_game_asan $(B)/assetpack $(B)/scratch-asan 5

bench: $(B)/gpusim_game $(B)/assetpack
	$(B)/gpusim_game $(B)/assetpack $(B)/scratch 300 $(B)/last_frame.ppm

clean:
	rm -rf $(B)

.PHONY: all test bench clean
