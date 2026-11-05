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
  F5 fuzz coverage, F6 untrusted lengths under ASan, F1b long-path truncation; F2 runs when
  FRONTEND_DIR is set, F7 when MXSB_DIR points at a copy of codegen/mxsb). Against the baseline it
  fails 15 checks (F1b covers the 512-byte path truncation; F3 has 7 cases incl. `-- -fplugin=`), which reproduces the review. A module is accepted into the
  wave-2 build only when this passes with zero failures.

## Layout and how to run (everything with `bash`; the mount is noexec)

    tests/
      golden_check.sh          baseline pins: 23 asserts, trace hash, PPM/MXSB SHA-256, reference probes
      security_regressions.sh  one regression test per review finding (F1, F1b, F2, F3, F4, F5, F6, F7)
      integrate.sh             builds integrated.c = frozen baseline + accepted modules swapped in
      fuzz/mx_fuzz_compat.h    replacement fuzzer (finding 5), harness-owned
      unit/rcs/rcs_conformance.c  independent RCS conformance test written from gpu-commands.md (92 checks)
      unit/canvas/gc_dirty_oracle.c  brute-force tile-diff oracle for the ghost canvas (40 checks, 5 sizes + INT_MAX/INT_MIN coords)
      unit/mxsb/mxsb_contract.c   hand-built MXSB v2 file + 43 mutations, each must hit the rule the contract assigns;
                                  two backends (-DMXSB_CONTRACT_REF = contracts/mxsb-ref, default = codegen/mxsb) must agree
      unit/Makefile               `make -C tests/unit OUT=/tmp/x MX=<matrixos copy> test` builds and runs the three above

    # scratch copies, since module Makefiles write build products and the mount is noexec
    cp -r /mnt/project-files/matrixos/. /tmp/w/mx/      # whole tree: codegen needs ../lfsr, unit tests need ../contracts
    bash integrate.sh <uploaded matrixos_v9.c> /tmp/w/integ
    bash golden_check.sh /tmp/w/integ/integrated.c <uploaded swrap.c> "gcc -D_GNU_SOURCE -I/tmp/w/integ"
    FRONTEND_DIR=/tmp/w/mx/frontend ASSETS_DIR=/tmp/w/mx/assets MXSB_REF_DIR=/tmp/w/mx/contracts/mxsb-ref MXSB_DIR=/tmp/w/mx/codegen/mxsb \
      bash security_regressions.sh /tmp/w/integ/integrated.c /tmp/w/mx/frontend/src/mxcc.c "gcc -D_GNU_SOURCE -I/tmp/w/integ"
    make -C /tmp/w/mx/tests/unit OUT=/tmp/w/unit MX=/tmp/w/mx test      # rcs 92, canvas 40, mxsb 43 x 2 backends

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
| Frozen baseline | 25/26 | 15 failures (F2, F7 skip) | expected: the one golden failure is the wrapper --version bug; the 15 are the review findings |
| Integrated build, gcc | 25/26 with GOLDEN_TRACE=0x3b3805a77980eee1 | 0 failures, 34 PASS incl. F2 and F7 x3 (F2 Rust side skip) | trace hash differs by design: the replacement fuzzer uses its own oracle, so the baseline fuzzer's trace events are gone; isolated by building with only the fuzz swap |
| Integrated build, clang | 25/26 (same GOLDEN_TRACE) | 0 failures | sanitizer checks fall back to gcc (no clang ASan runtime in this container) |
| lfsr own tests | | | 394,115 passed (gcc, clang, gcc ASan/UBSan) |
| assets own tests | | | 47 passed (gcc, clang, gcc ASan/UBSan) |
| frontend 1.0.1 own tests | | | typed_input 48 (2 compilers), cells.vec 57 (C), driver tests; Rust vectors skip; re-accepted with the `-- -fplugin=x.so` case |
| superops own tests | | | 203 passed (gcc) |
| RCS reference | | | 57,669 passed; harness conformance 92/92 |
| canvas own tests | | | 759 + 22 passed (gcc, ASan, UBSan, clang); golden ghost hash f7da60a90fb9597f; harness dirty oracle 40/40 |
| codegen own tests | | | mxsb_test 40113, run_mxsb 21, tiers_test 25, run_tiers 22, run_lifter 21 (gcc and clang; needs sibling lfsr/ and superops/); harness MXSB contract test 43/43 (gcc ASan, clang) |
| contracts/mxsb-ref (reference loader) | | | own run_tests.sh: 348 passed, 0 failed, 0 divergences from codegen (corpus 68, goldens, 20k mutation fuzz under ASan); harness contract test 43/43 (gcc ASan, clang), identical verdicts to codegen on all 43 |

Not run: anything on the user's machine; cold-cache storage numbers; clang sanitizer builds; the Rust
side of the typed-input vectors. F7 runs against the reference loader (contract owner) with codegen's validator as second oracle; both agree on all 43 mutations.

## Not an acceptance gate here: superops `make perfcheck`

`bench_tri_sweep --check` requires the default triangle path to be within 25% of forced scalar at every
span. In this shared container it failed 2 of 3 interleaved runs, every flag at spans under 64 px where
auto and scalar are the same code (sub-100 ns per row), so the allowance sits inside the machine's noise.
Runs only count on the user's PC (asked of the bench thread). superops acceptance rests on its unit suite.

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

- matrixos/bench/ memory-bandwidth numbers
- scalar reference kernels from superops (or written here)
