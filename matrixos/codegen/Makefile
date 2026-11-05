# MatrixOS codegen: MXSB emitter, SIMD tiers, LFSR lifter hook.
# `make test` runs every test suite; `make` builds the two tools.
CC      ?= gcc
CLANG   ?= /usr/bin/clang
LFSR_DIR ?= ../lfsr
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -fstack-protector-strong -D_FORTIFY_SOURCE=2
SAN      = -std=c11 -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -fsanitize=address,undefined -fno-omit-frame-pointer
BIN     ?= bin

all: $(BIN)/mxsb_emit $(BIN)/mxlift_hook

$(BIN):
	mkdir -p $(BIN)
$(BIN)/mxsb_emit: mxsb/mxsb.c mxsb/mxsb_elf.c mxsb/mxsb_emit.c mxsb/mxsb.h mxsb/mxsb_elf.h mxsb/mx_sha256.h | $(BIN)
	$(CC) $(CFLAGS) -o $@ mxsb/mxsb.c mxsb/mxsb_elf.c mxsb/mxsb_emit.c
$(BIN)/mxlift_hook: lifter/mxlift_hook.c lifter/mxl_json.h $(LFSR_DIR)/mx_lift.h | $(BIN)
	$(CC) $(CFLAGS) -I$(LFSR_DIR) -o $@ lifter/mxlift_hook.c

test: test-mxsb test-tiers test-lifter
test-mxsb:
	CLANG=$(CLANG) bash tests/run_mxsb.sh
test-tiers:
	CLANG=$(CLANG) bash tests/run_tiers.sh
test-lifter:
	CLANG=$(CLANG) LFSR_DIR=$(abspath $(LFSR_DIR)) bash tests/run_lifter.sh

clean:
	rm -rf $(BIN)
.PHONY: all test test-mxsb test-tiers test-lifter clean
