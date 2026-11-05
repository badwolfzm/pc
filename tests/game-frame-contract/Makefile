# Independent host-only oracle for game-frame-probe PR #68.
CC ?= cc
CFLAGS = -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic -UNDEBUG
SRC = test_render_contract.c ../../tools/game-frame-probe/render.c
.PHONY: test sanitize clean

test:
	$(CC) $(CFLAGS) $(SRC) -o /tmp/warden-render-contract-test
	/tmp/warden-render-contract-test

sanitize:
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -Wpedantic -UNDEBUG -fsanitize=address,undefined -fno-omit-frame-pointer $(SRC) -o /tmp/warden-render-contract-sanitize
	/tmp/warden-render-contract-sanitize

clean:
	rm -f /tmp/warden-render-contract-test /tmp/warden-render-contract-sanitize
