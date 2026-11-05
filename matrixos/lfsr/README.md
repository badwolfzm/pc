# LFSR / GF(2) affine loop lifter (v2)

Thread: "Upgrade LFSR lifter compiler pass". Owner folder: `matrixos/lfsr/`.
Baseline: the v1 lifter in the uploaded compact prototype (`mx_affine32` block,
lines 435-449, and its single test at line 501). The baseline file is untouched.

## Files

| File | What |
|---|---|
| `mx_lift.h` | The pass. Header-only C99, no dependencies, C++-includable. ~400 lines. |
| `mx_lift_compat.h` | Drop-in for the v1 names (`mx_lfsr_naive_steps`, `mx_lfsr_lifted_steps`, `mx_loop_sig`, `mx_detect_liftable_loop`) so the baseline fuzz call site compiles unchanged; adds `mx_detect_liftable_loop_ir` for the real analysis. For the tests/integration thread. |
| `mx_lift_test.c` | Tests and benchmark. `cc -std=c99 -O2 -o t mx_lift_test.c && ./t` (add `--no-bench` to skip timing). |
| `test-output.txt` | Output of the run reported below. |

## What was weak in v1 and what changed

| v1 (baseline) | Problem | v2 |
|---|---|---|
| `mx_affine32`, 32 bits only | Cannot lift 8/16/64-bit recurrences (xorshift64, CRC-64, byte LFSRs). | `mx_aff`, width 8/16/32/64 with one code path. |
| Polynomial `0xEDB88320` and constant `0xA5A5A5A5` hard-coded in the step matrix and the naive loop | It lifted exactly one recurrence. Not a compiler pass. | The matrix is derived from the loop body. Any body made of XOR, shifts, rotates, AND/XOR with constants, the `-(x&1) & POLY` tap select, OR of disjoint fields, multiply by 2^k. |
| `mx_detect_liftable_loop` tested four booleans (`has_shift_right1 && has_xor && ...`) | A pattern match, not an analysis. It would "detect" any loop with those ops, including non-affine ones, and the result was never used to build a matrix. | `mxl_lift()` runs a symbolic GF(2) bit-slice evaluator over a small straight-line IR (`mxl_insn`). Each register bit is tracked as (set of input bits) XOR constant. It either proves affinity and emits the exact matrix, or rejects with a reason and the offending instruction index. Non-affine ops (`ADD`, `MUL` by non power of two, `AND`/`OR` of two variables) are rejected unless the context makes them affine (one side constant, disjoint bits), which is checked per bit. |
| Opaque step functions could not be lifted at all | | `mx_lift_probe()` lifts a function pointer by probing the basis and verifies with random probes. Probabilistic by design, reported as such (see Uncertainty). |
| Jump = compose `acc = base . acc` per set bit of n: O(w^2) per bit | Matrix times matrix where matrix times vector suffices. | `mx_aff_jump()` applies each `base^(2^k)` to the running value: O(w) per bit. `mx_lift_plan` precomputes the 64 powers once per recurrence, so every later jump is at most 64 vector applies and needs no composition at all. |
| `apply` looped over all 32 bits with a branch each | | Walks set bits with ctz. |
| One test: one seed, n = 1,000,000 | | 394,115 checks, see below. |

## Where it plugs in

Codegen (the clang-side pass) lowers a candidate loop body to `mxl_insn[]`,
fills an `mxl_loop` (width, output register, code) and calls
`mxl_lift(&loop, &step, &report)`. `MXL_OK` means the body is proven affine
and `step` is its exact matrix; the loop can be replaced by
`mx_lift_plan_build` once plus `mx_lift_plan_jump(plan, seed, n)` per use
(or `mx_aff_jump` for a one-off n). Anything else means leave the loop alone;
`mxl_status_str(report.status)` and `report.insn` say why.

Seeds and procedural generation: build one plan per generator at startup
(about 20 us for 32 bits, 64 squarings), then compute chunk k's state as
`plan_jump(seed, k * steps_per_chunk)` in about 150 ns instead of walking the
stream.

## Verification (run on 2026-11-05, gcc 13.3 -O2, also clang 18 -O3 and gcc ASan+UBSan)

All against the naive loop, which is the IR interpreter iterated n times, and
for the v1 recurrence also the original hand-written C loop.

- Algebra: identity, associativity of compose, compose equals function
  composition, `a^(p+q) = a^p . a^q`, `jump == pow-then-apply`, `n = 0` is the
  identity. 4 widths x 50 random matrices.
- Lift vs naive: 8 loop bodies (v1 Galois+affine, Galois 8/16/64, xorshift32,
  xorshift64, 16-bit Fibonacci LFSR using OR, a rotate/mul8/not body with the
  result in a different register). For each: one-step equality on 2000 inputs
  including 0, all-ones, 1 and the top bit; then for 38 values of n from 0 to
  16,777,223 (powers of two and their neighbours included) and up to 6 seeds,
  three jump methods equal the naive loop. Far jumps with 64-bit n use the
  additivity check `jump(jump(x,a),b) == jump(x,a+b)`.
- v1 compatibility: the hand-written C loop equals the lifted result for the
  same grid of n, and for the exact assertion in the baseline
  (`seed 0x12345678, n = 1,000,000`).
- Probe mode: recovers the identical matrix to the IR proof for the v1
  function and xorshift64; rejects an LCG; the documented false-accept case
  (a function affine everywhere except one input) is printed, not asserted.
- Rejections: `ADD`, `ADD_IMM`, `MUL_IMM` by 1664525, `AND` and `OR` of two
  variables, bad width, bad register, bad opcode, each with the right status
  and instruction index. Affine-in-context forms are accepted and verified
  (AND with a constant register, OR with constant ones, ADD of zero, MUL by 16).
- Fuzz: 20,000 random affine-only bodies (1-13 instructions, 4 widths) all
  lift and match the naive loop for 6 (seed, n <= 3000) pairs each. 20,000
  random bodies that may contain nonlinear ops: 10,998 rejected, 9,002
  accepted, and every accepted one matched the naive loop. No accept-and-wrong
  case was found, which is the soundness property the pass must have.

Result: 394,115 checks, 0 failures. Clean under `-Wall -Wextra -Wpedantic`,
AddressSanitizer and UndefinedBehaviorSanitizer.

## Measured timing (one machine, 10,000,000 steps of the v1 recurrence, 21 runs after 3 warmup, outputs consumed, timer floor 21 ns)

| Method | median | p95 |
|---|---|---|
| naive loop (v1 C) | 13.18 ms | 13.33 ms |
| v1 lifted (accumulator compose) | 7.96 us | 8.87 us |
| v2 `mx_aff_jump`, one-off n | 6.38 us | 7.19 us |
| v2 plan jump, plan prebuilt | 149 ns | 158 ns |
| plan build, paid once per recurrence | 19.1 us | |

The plan-jump number is within 10x of the timer floor, so read it as "about
0.15 us", not as an exact figure. These are from this container, not from the
Warden target; the ratios are what matter.

## Uncertainty and limits, stated plainly

- Probe mode is not a proof. A step function that is affine on all but a few
  inputs passes the random probes. Codegen must use the IR path for anything
  it rewrites; probe mode is for tooling and for lifting opaque generators
  the user vouches for.
- The IR is deliberately small (8 registers, 15 opcodes, no memory, no
  branches, no loop-carried state other than the one word). Loop bodies with
  two state words (e.g. xorshift128) need a 128-bit state; the same algebra
  works, the code does not yet (`mx_aff` is 64 bits wide).
- Lowering real clang IR to `mxl_insn` is codegen's job and is not written
  here. Patterns like `(x >> 1) ^ (-(x & 1) & POLY)` have several equivalent
  spellings (`x & 1 ? POLY : 0`, `(0 - (x & 1)) & POLY`, a select); the
  lowering must canonicalise them to `MXL_MASKSEL`.
- `n` is taken modulo 2^64. The pass does not compute the recurrence's period,
  so it cannot reduce huge counts further; it does not need to for 64-bit n.
- Benchmarking: `mx_lfsr_lifted_steps` in the compat header reuses a plan, so
  it is not the same work as the v1 per-call compose. For evidence rows use
  `mx_lift_plan_build` + one `mx_lift_plan_jump` (cold) and `mx_lift_plan_jump`
  alone (warm), and `mx_aff_jump` for the one-off case. All three stay public.
- Integration status (tests thread, 2026-11-05): golden_check 25/26 with the
  compat header swapped into the baseline, same as baseline (the one failure is
  the frozen wrapper's --version bug, not this module); trace hash preserved;
  security_regressions unchanged, none in this code.
