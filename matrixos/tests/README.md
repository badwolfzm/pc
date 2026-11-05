# matrixos/tests — test and benchmark harness (integration owner)

Plan: https://claude.ai/artifact/VU7zP9p25aqDQzpSpfYFRp (MatrixOS compiler and GPU test plan)

## What is here now

- `golden_check.sh <matrixos_v9.c> <swrap.c> [gcc|clang]` — pins the behaviour
  of the two UPLOADED sources (the frozen baseline): 23 built-in asserts, the
  deterministic trace hash 0x3e978b49855e4339, SHA-256 of the canvas PPM and
  the MXSB demo, benchmark CSV shape, plus reference probes for gpu_vadd,
  gpu_clear, crc32 (vs zlib), the LFSR lifter (vs naive and composition at
  2^40), VFS `..` rejection, and the wrapper's profiles and exit codes.
  Run with `bash` (the project-files mount is noexec).
  Status: 25/26 pass under gcc 13 and clang 18. The one failure is the
  wrapper's `--version` writing 35 of 38 bytes (a real defect).

- `security_regressions.sh <matrixos_v9.c> <swrap.c> [gcc|clang]` — one
  regression test per finding in matrixos/reviews/security-review.md (F1
  symlink escape, F3 flag forwarding + PATH lookup, F4 OOB read under ASan,
  F5 fuzz coverage, F6 untrusted lengths under ASan, F1b long-path truncation; F2 and F7 SKIP until
  the frontend module and the MXSB loader exist). Against the baseline it
  fails 14 checks (F1b covers the 512-byte path truncation), which reproduces the review. A module is accepted into the
  wave-2 build only when this passes with zero failures.

## Layout and how to run (everything with `bash`; the mount is noexec)

    tests/
      golden_check.sh          baseline pins: 23 asserts, trace hash, PPM/MXSB SHA-256, reference probes
      security_regressions.sh  one regression test per review finding (F1, F1b, F2, F3, F4, F5, F6; F7 skips)
      integrate.sh             builds integrated.c = frozen baseline + accepted modules swapped in
      fuzz/mx_fuzz_compat.h    replacement fuzzer (finding 5), harness-owned
      unit/rcs/rcs_conformance.c  independent RCS conformance test written from gpu-commands.md (92 checks)
      unit/canvas/gc_dirty_oracle.c  brute-force tile-diff oracle for the ghost canvas (36 checks, 5 sizes)

    # scratch copies, since module Makefiles write build products and the mount is noexec
    cp -r /mnt/project-files/matrixos/{frontend,assets,superops} /tmp/w/
    bash integrate.sh <uploaded matrixos_v9.c> /tmp/w/integ
    bash golden_check.sh /tmp/w/integ/integrated.c <uploaded swrap.c> "gcc -D_GNU_SOURCE -I/tmp/w/integ"
    FRONTEND_DIR=/tmp/w/frontend ASSETS_DIR=/tmp/w/assets \
      bash security_regressions.sh /tmp/w/integ/integrated.c /tmp/w/frontend/src/mxcc.c "gcc -D_GNU_SOURCE -I/tmp/w/integ"
    cc -std=c11 -O1 -g -fsanitize=address,undefined -I<contracts/gpu-commands-ref> \
      unit/rcs/rcs_conformance.c <contracts/gpu-commands-ref>/rcs.c -o rcs_conf && ./rcs_conf

What integrate.sh swaps (baseline line ranges, each wrapped in an #ifndef so the baseline file is never edited):
  251-320 super-op dispatcher -> superops/mx_superops_compat.h + mx_superops.c, via a harness adapter
          mx_superop_dispatch_b that keeps the baseline's cap check and trace event, then calls the
          bounded mx_superop_dispatch2 with real capacities (the two test call sites are rewritten)
  322-408 VFS                 -> assets/mx_vfs_compat.h (openat2 RESOLVE_BENEATH|NO_SYMLINKS)
  435-449 LFSR lifter         -> lfsr/mx_lift_compat.h
  480-482 fuzzer              -> tests/fuzz/mx_fuzz_compat.h

## Status (2026-11-05)

| Target | golden_check | security_regressions | Notes |
|---|---|---|---|
| Frozen baseline | 25/26 | 14 failures | expected: the one golden failure is the wrapper --version bug; the 14 are the review findings |
| Integrated build, gcc | 25/26 with GOLDEN_TRACE=0x3b3805a77980eee1 | 0 failures (F7 skip, F2 Rust side skip) | trace hash differs by design: the replacement fuzzer uses its own oracle, so the baseline fuzzer's trace events are gone; isolated by building with only the fuzz swap |
| Integrated build, clang | 25/26 (same GOLDEN_TRACE) | 0 failures | sanitizer checks fall back to gcc (no clang ASan runtime in this container) |
| lfsr own tests | | | 394,115 passed (gcc, clang, gcc ASan/UBSan) |
| assets own tests | | | 47 passed (gcc, clang, gcc ASan/UBSan) |
| frontend own tests | | | typed_input 48, cells.vec 57 (C), driver tests; Rust vectors skip |
| superops own tests | | | 202 passed (gcc) |
| RCS reference | | | 57,669 passed; harness conformance 92/92 |
| canvas own tests | | | 759 + 22 passed (gcc, ASan, UBSan, clang); golden ghost hash f7da60a90fb9597f; harness dirty oracle 36/36 |

Not run: anything on the user's machine; cold-cache storage numbers; clang sanitizer builds; the Rust
side of the typed-input vectors; F7 (no MXSB loader exists yet).

## Evidence rules (short form; full text in the plan)

1. Same work, output checked against a reference, or the row is `unverified`.
2. consumed_hash non-zero and input-sensitive.
3. Timer floor measured per run; sample >= 100x floor.
4. >= 31 timed runs after >= 5 warmups; report median and p95.
5. Record CPU, governor, pinning, kernel, compiler, exact flags, SIMD tier.
6. Every row under gcc and clang.
7. Suite run 3 times; >10% median drift = `noisy`.
8. No -march=native anywhere.

## Harness-owned fixes (from the baseline defects)

- Fuzz harness: the baseline `crashes` counter is never incremented, so
  `crashes == 0` is vacuous. The new fuzzer runs each case under ASan/UBSan
  and counts sanitizer aborts; it must also generate valid /sandbox/ paths,
  plant symlinks, and fuzz MXSB headers (review finding 5).
- Bench CSV: write the real flag string (baseline writes the literal
  "-O2/-Os demo"), measure timer_floor per run and apply rule 3 instead of
  hardcoding timer_floor_checked, and add a naive-LFSR row so the lifting
  speedup is a ratio.

## Waiting on

- matrixos/contracts/mxsb-manifest.md
- matrixos/contracts/gpu-commands.md (rcs.h draft exists)
- matrixos/bench/ memory-bandwidth numbers
- scalar reference kernels from superops (or written here)
