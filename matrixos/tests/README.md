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

## Skeleton (filled in once the contracts land)

    tests/
      golden_check.sh          baseline pins (done)
      security_regressions.sh  the seven review findings (done; F2, F7 skip)
      unit/
        wrapper/               fake-cc argv capture, flag tables per profile
        lifter/                naive-vs-lifted sweeps, detector corpus (labelled loops)
        mxsb/                  header byte tables, hash rule, caps bits, malformed inputs
        rcs/                   opcode x batch-kind matrix, rights gating, limits at N/N+1
        canvas/                pixel reference, dirty-bit brute-force diff, clipping
        superops/              SIMD kernel vs scalar reference, all three tiers
      bench/
        run_bench.sh           builds under gcc and clang, pins to one core, 3 repeats
        rules.py               applies the evidence rules to a CSV, marks failing rows
        compare.py             ratio table between two CSVs, rule failures beside each row
      ref/                     scalar reference implementations, no code shared with fast paths

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
