# Stale WAD handle security regression (issue #118)

A standalone **negative** test for the merged Doom WAD class bridge. This test
is expected to **FAIL on current main** (commit c6ab36f) because the bridge
returns the same static `wad_file_t *` for every opening. It is not a CI gate
until the WAD owner has fixed or explicitly narrowed the handle-lifecycle
contract.

Run from repo root:

```sh
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=undefined \
  -Itools/doom-wad-class-bridge/test \
  -Itools/doom-wad-class-bridge \
  -Itools/doom-wad-stream \
  tools/doom-wad-class-bridge/doom_wad_bridge.c \
  tools/doom-wad-stream/wad_stream.c \
  tests/doom-wad-stale-handle/test_stale.c \
  -o /tmp/warden-stale-test && /tmp/warden-stale-test
```

No external dependencies, WAD assets, hardware, kernel modifications, or file
ownership overlap. Scenario: old handle A -> close/revoke -> regrant/open B;
old read and close must not access or destroy B while fresh handle still works.

This is a reproduction **source artifact**; the author has not executed this
exact integrated compile in Warden or GitHub CI. A test failure on main is
expected and should not be misreported as a newly introduced regression.
Kernel/game owner should design a bounded, generation-safe API or prove a
trusted non-escaping handle contract; only then include in required CI.
