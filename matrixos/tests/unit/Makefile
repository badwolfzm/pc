# Harness-side unit tests. The project-files mount is noexec, so build products go to OUT.
#   make -C matrixos/tests/unit OUT=/tmp/x MX=/path/to/matrixos test
MX   ?= ../..
OUT  ?= build
CC   ?= gcc
SAN  ?= -fsanitize=address,undefined -fno-omit-frame-pointer
CFLAGS ?= -std=c11 -O1 -g -Wall -Wextra -Wno-missing-field-initializers

test: rcs canvas mxsb

rcs: | $(OUT)
	$(CC) $(CFLAGS) $(SAN) -I$(MX)/contracts/gpu-commands-ref -o $(OUT)/rcs_conformance rcs/rcs_conformance.c $(MX)/contracts/gpu-commands-ref/rcs.c
	$(OUT)/rcs_conformance

canvas: | $(OUT)
	$(CC) $(CFLAGS) $(SAN) -I$(MX)/canvas -o $(OUT)/gc_dirty_oracle canvas/gc_dirty_oracle.c $(MX)/canvas/ghost_canvas.c
	$(OUT)/gc_dirty_oracle

mxsb: | $(OUT)
	$(CC) $(CFLAGS) $(SAN) -D_GNU_SOURCE -DMXSB_CONTRACT_REF -I$(MX)/contracts/mxsb-ref -o $(OUT)/mxsb_contract_ref mxsb/mxsb_contract.c $(MX)/contracts/mxsb-ref/mxsb_load.c
	$(OUT)/mxsb_contract_ref
	$(CC) $(CFLAGS) $(SAN) -D_GNU_SOURCE -I$(MX)/codegen/mxsb -o $(OUT)/mxsb_contract mxsb/mxsb_contract.c $(MX)/codegen/mxsb/mxsb.c
	$(OUT)/mxsb_contract

$(OUT):
	mkdir -p $(OUT)

clean:
	rm -rf $(OUT)
.PHONY: test rcs canvas mxsb clean
