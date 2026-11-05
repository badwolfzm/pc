# MatrixOS compiler frontend: driver + typed-input library + tests.
#   make            build mxcc and libmxinput.a into build/
#   make test       run every test (driver, typed-input compile cases, unit+fuzz under ASan/UBSan, shared vectors in C and Rust)
#   make install PREFIX=/opt/matrixos
CC      ?= cc
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra -Wformat-truncation=0
SAN      = -std=c11 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all
B        = build
ROOT    := $(abspath .)
# kernel cell model for the shared-vector test (Phase 0 tarball layout)
WARDEN_CELLS_RS ?= $(abspath ../../kernel/src/crates/warden-caps/src/cells.rs)

.PHONY: all test test-driver test-typed test-unit test-vectors test-vectors-rust clean install

all: $(B)/mxcc $(B)/libmxinput.a

$(B):
	mkdir -p $(B)

$(B)/mxcc: src/mxcc.c | $(B)
	$(CC) $(CFLAGS) -o $@ $<

$(B)/libmxinput.a: src/mx_input.c include/mx_input.h | $(B)
	$(CC) $(CFLAGS) -Wconversion -Iinclude -c src/mx_input.c -o $(B)/mx_input.o
	ar rcs $@ $(B)/mx_input.o

test: test-driver test-typed test-unit test-vectors test-vectors-rust
	@echo "all frontend tests passed"

test-driver: $(B)/mxcc
	bash tests/driver/run.sh $(ROOT)/$(B)/mxcc $(ROOT)

test-typed: $(B)/mxcc
	bash tests/typed_input/run.sh $(ROOT)/$(B)/mxcc $(ROOT)

test-unit: | $(B)
	$(CC) $(SAN) -Iinclude -o $(B)/test_input src/mx_input.c tests/unit/test_input.c
	ASAN_OPTIONS=detect_leaks=0 $(B)/test_input

test-vectors: | $(B)
	$(CC) $(SAN) -Iinclude -o $(B)/test_vectors src/mx_input.c tests/unit/test_vectors.c
	ASAN_OPTIONS=detect_leaks=0 $(B)/test_vectors tests/vectors/cells.vec

test-vectors-rust:
	@if command -v cargo >/dev/null && [ -f "$(WARDEN_CELLS_RS)" ]; then \
	  cd tests/vectors/rust && WARDEN_CELLS_RS="$(WARDEN_CELLS_RS)" cargo run --quiet --release -- ../cells.vec; \
	else echo "SKIP rust vectors (cargo or $(WARDEN_CELLS_RS) missing); the C side ran"; fi

install: all
	install -d $(PREFIX)/bin $(PREFIX)/include $(PREFIX)/lib
	install -m 755 $(B)/mxcc $(PREFIX)/bin/mxcc
	install -m 644 include/mx_input.h $(PREFIX)/include/
	install -m 644 $(B)/libmxinput.a $(PREFIX)/lib/

clean:
	rm -rf $(B) tests/vectors/rust/target
