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

# ---- graphics plan M0: the render core built freestanding (no libc) ----
# rcs.c + super-ops against freestanding/include only (plus the compiler's own
# stddef/stdint/stdbool), linked into one relocatable object. `fs-check` proves
# it needs nothing but the four string routines and mxlibc, then runs the full
# contract suite against exactly that object on all three tiers.
FS       = freestanding
GCCINC  := $(shell $(CC) -print-file-name=include)
FSFLAGS  = -std=c11 -O2 -Wall -Wextra -Werror -Wshadow -Wno-sign-conversion -ffp-contract=off -fno-strict-aliasing \
           -ffreestanding -nostdinc -isystem $(GCCINC) -I$(FS)/include -fno-stack-protector -fno-asynchronous-unwind-tables -fpie \
           -DMXS_FREESTANDING -DRCS_USE_SUPEROPS
FS_ALLOWED = memcpy memmove memset memcmp mx_malloc mx_calloc mx_free mx_abort

$(B)/render_fs.o: $(REF)/rcs.c $(REF)/rcs.h $(SUP) ../superops/mx_superops_kernels.inc ../superops/mx_superops.h $(wildcard $(FS)/include/*.h) | $(B)
	mkdir -p $(B)/fs
	$(CC) $(FSFLAGS) -c $(REF)/rcs.c -o $(B)/fs/rcs.o
	$(CC) $(FSFLAGS) -c ../superops/mx_superops.c -o $(B)/fs/mx_superops.o
	$(CC) $(FSFLAGS) -c ../superops/mx_superops_ref.c -o $(B)/fs/mx_superops_ref.o
	ld -r -o $@ $(B)/fs/rcs.o $(B)/fs/mx_superops.o $(B)/fs/mx_superops_ref.o

# The runtime itself, freestanding too (string routines + allocator).
$(B)/mxlibc_fs.o: $(FS)/mxlibc.c $(FS)/mxlibc.h | $(B)
	$(CC) $(FSFLAGS) -fno-builtin -fno-tree-loop-distribute-patterns -c $< -o $@

# Host test of mxlibc: string routines renamed so they do not replace libc's.
TREN = -Dmemcpy=t_memcpy -Dmemmove=t_memmove -Dmemset=t_memset -Dmemcmp=t_memcmp -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0
$(B)/test_mxlibc: $(FS)/test_mxlibc.c $(FS)/mxlibc.c $(FS)/mxlibc.h | $(B)
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fno-builtin $(SAN) $(TREN) -o $@ $(FS)/test_mxlibc.c $(FS)/mxlibc.c

# rcs_test.c (hosted: printf) linked against the freestanding object + mxlibc's allocator.
$(B)/rcs_test_fs: $(REF)/rcs_test.c $(B)/render_fs.o $(FS)/mxlibc.c $(FS)/host_env.c tier_env.c | $(B)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -DMXLIBC_NO_STRING -c $(FS)/mxlibc.c -o $(B)/fs/mxlibc_alloc.o
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -o $@ $(REF)/rcs_test.c $(B)/render_fs.o $(B)/fs/mxlibc_alloc.o $(FS)/host_env.c tier_env.c

fs-check: $(B)/render_fs.o $(B)/mxlibc_fs.o $(B)/test_mxlibc $(B)/rcs_test_fs
	@undef=$$(nm -u $(B)/render_fs.o $(B)/mxlibc_fs.o | awk 'NF==2{print $$2}' | sort -u); \
	 extra=$$(for s in $$undef; do case " $(FS_ALLOWED) " in *" $$s "*) ;; *) echo $$s;; esac; done); \
	 echo "render_fs.o + mxlibc_fs.o undefined symbols:" $$undef; \
	 if [ -n "$$extra" ]; then echo "FAIL: unexpected external symbols: $$extra"; exit 1; fi; \
	 echo "PASS: no libc dependency beyond mxlibc"
	$(B)/test_mxlibc
	for t in scalar v128 v256; do MXS_TIER=$$t $(B)/rcs_test_fs 2>&1 | grep -E 'tier|rcs_test:' || exit 1; done

clean:
	rm -rf $(B)

.PHONY: all test bench clean fs-check
