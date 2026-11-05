# matrixos/superops - SIMD super-op kernels
CC      ?= cc
# Fixed SIMD tier for the *library's non-kernel code*; kernels select their own
# target via attributes so one binary runs on every x86-64 CPU. No -march=native.
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra -Werror -ffp-contract=off -fno-strict-aliasing
SAN      = -fsanitize=address,undefined -fno-sanitize-recover=all -g -O1
SRC      = mx_superops.c mx_superops_ref.c
# Build products go to $(B) (default: ./build). The project-files share is mounted
# without execute permission, so set B to a local directory when building there,
# e.g.  make B=/tmp/superops-build test
B       ?= build

all: $(B)/libmx_superops.a $(B)/test_superops $(B)/bench_superops

$(B):
	mkdir -p $(B)

$(B)/libmx_superops.a: $(B)/mx_superops.o | $(B)
	$(AR) rcs $@ $^

$(B)/mx_superops.o: mx_superops.c mx_superops.h mx_superops_kernels.inc | $(B)
	$(CC) $(CFLAGS) -c $< -o $@

$(B)/test_superops: test_superops.c $(SRC) mx_superops.h mx_superops_compat.h mx_superops_lower.h mx_superops_kernels.inc | $(B)
	$(CC) $(CFLAGS) $(SAN) test_superops.c $(SRC) -o $@

$(B)/bench_superops: bench_superops.c $(SRC) mx_superops.h mx_superops_kernels.inc | $(B)
	$(CC) $(CFLAGS) bench_superops.c $(SRC) -o $@

test: $(B)/test_superops
	$(B)/test_superops

bench: $(B)/bench_superops
	$(B)/bench_superops > bench.csv && cat bench.csv

clean:
	rm -rf $(B)

.PHONY: all test bench clean
