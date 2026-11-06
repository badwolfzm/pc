# Independent Doom WAD stale-handle regression (issue #118)

This test was introduced against the original bridge at main `c6ab36f`,
which reused one static `wad_file_t *` and was vulnerable to stale-handle
aliasing across revoke/regrant. Security fix PR #126 was subsequently merged
as `fdb6c1f94c1f7af984d5b565a2c4406fb6c34e5a`, adding a bounded
64-slot non-reusable handle pool.

**Expected result:** FAIL on the original vulnerable revision; PASS on
the corrected bridge. GitHub Actions runs the exact test on the PR head.
The independent fixture complements the bridge owner's own regression.

From repo root:

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

The test opens A, closes and revokes it, then opens B and verifies that the
previously issued pointer cannot read B or close B. No proprietary WAD assets,
kernel edits, host filesystem privileges or real Doom execution are involved.
The compile and run checks are host-only and do not establish guest/QEMU
security or playable Doom.
