# Independent Doomgeneric adapter negative tests

This is an isolated host mock suite stacked **on PR #88**, exercising the
existing `tools/doomgeneric-warden/doomgeneric_warden.c`. It never modifies
that adapter or grants any Warden kernel capabilities.

Run from the repository root after checking out the stacked branch:

```sh
cc -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic -DWARDEN_DOOM_TEST \
  -Itools/doomgeneric-warden/test \
  tools/doomgeneric-warden/doomgeneric_warden.c \
  tests/doomgeneric-adapter-negative/test_negative.c -o /tmp/doom-negative
/tmp/doom-negative
```

Tests cover invalid display dimensions, missing screen pointer, display
present refusal, failed ticks and sleep, keyboard syscall denial, extended
keyboard prefix persistence, and zero-duration sleeping.

These are **host mock tests**. They neither run the Doom engine nor measure
Warden QEMU, resident RAM, single-vCPU FPS or Albion compatibility. An
independent review and integration-owner approval are required. Merge only
after PR #88; do not replace its original positive-path tests.
