# MatrixOS codegen (wave 1)

Owner: thread "Upgrade MatrixOS compiler codegen". Scope set by the lead
thread: clang stays the compiler and the only backend. This folder holds
the three codegen pieces around it.

| piece | files | what it does |
|---|---|---|
| MXSB emitter | `mxsb/` | wraps a clang-built static ELF into an MXSB v2 super-binary per `contracts/mxsb-manifest.md` 2.0.3, validates its own output with rules V1..V25, V27, V28, signs with Ed25519 |
| SIMD tiers | `tiers/` | the baseline / avx2 / avx512 contract: flag strings, CPUID detection, in-process dispatch macro, three-way reproducible build script |
| Lifter hook | `lifter/` | finds LFSR / xorshift / CRC-shaped loops in C source via the clang AST, lowers them to `mxl_insn`, calls `mxl_lift()` from `../lfsr/mx_lift.h`, and replaces the loop with one `mx_aff_jump()` on a `static const` matrix in `.rodata` |
| tests | `tests/` | `make test`: 40k MXSB mutation/fuzz checks, independent Python cross-check, 15-file differential corpus for the lifter, tier build/dispatch tests |

Build: `make` (tools in `bin/`). Test: `make test` (needs `/usr/bin/clang`,
`gcc`, `openssl`, `python3`; the lifter tests need `../lfsr/mx_lift.h`).

## 1. MXSB emitter (`mxsb/`)

```
mxsb_emit build --elf guest.elf --manifest app.manifest --sign key.pem -o app.mxsb \
                [--superop blit]... [--tier baseline|avx2|avx512|multi] [--build-info k=v]...
mxsb_emit verify app.mxsb --pubkey pub.pem          # V1..V25 + V20
mxsb_emit verify app.mxsb --approve <digest-hex>    # unsigned path (approvals list)
mxsb_emit dump app.mxsb | digest app.mxsb
mxsb_emit upgrade old-v1.mxsb --manifest m -o new.mxsb (--sign k | --unsigned)
```

Design points, all from the contract or the security review:

- Every read of an input buffer goes through a bounds-checked accessor;
  every size addition is checked for u64 overflow; a failed decode leaves
  the output image zeroed with no allocation (contract 9a, review finding 7).
- The emitter runs the full validator on the bytes it is about to write
  and refuses to write anything that fails (contract "emitter rule").
- `build_id` is computed by the emitter and written into the manifest;
  `caps_summary` is derived from the normalised manifest; the manifest is
  normalised exactly as section 4 says (trim, collapse, drop comments,
  sort `require`, drop duplicates), so the same grants give the same bytes.
- Signing: digest per section 6, Ed25519 through `/usr/bin/openssl`
  (absolute path, fork/exec with an argv, no shell: review finding 3).
  `key_id = SHA-256(raw 32-byte public key)`. After signing the emitter
  verifies its own signature before writing. `--unsigned` writes a zero
  SIGNATURE section for the approvals path.
- ELF input: static, relocation-free, no `PT_INTERP`, no W+X segment.
  `ET_DYN` sets the PIE flag. Dynamic relocations are refused with the
  reason, because the contract has no relocation section.

### Layout (contract 2.0.3, section 3.4)

The section table carries offsets only; the load layout is fixed by rule,
adopted into the contract from this emitter:

```
base       = page_floor(lowest PT_LOAD vaddr)
TEXT image = [base, page_floor(first writable PT_LOAD vaddr))      -> mapped at B, R+X
DATA image = [page_floor(first writable vaddr), end of filesz)      -> mapped at B + TEXT.size, R+W
BSS        = memsz tail                                             -> zero at B + TEXT.size + DATA.size
entry_off  = TEXT.offset + (e_entry - base)
```

TEXT is zero-padded to a page multiple whenever DATA or BSS exists (V28;
`build_id` covers the padded TEXT). Read-only segments (`.rodata`, RELRO)
end up inside TEXT and are mapped R+X; they are never writable. Build
guests with `clang -static-pie -nostdlib -fPIE -Wl,-z,separate-code
-Wl,-z,max-page-size=4096` (the tests do exactly this).

### Contract notes

1. Resolved in 2.0.3: the digest now covers the whole file except the
   SIGNATURE bytes and all padding must be zero (V27); the v1 header is
   recorded as 40 bytes; the layout rule above is normative.
2. Super-op names for `require superop=<name>` are taken provisionally from
   `contracts/gpu-commands-ref/rcs.h` (`create_surface`, `blit`, `clear`,
   ...). Replace the table in `mxsb.c` when `gpu-commands.md` lands.
3. Per-tier payloads: the contract has one TEXT, so a multi-tier binary
   uses in-process dispatch (section 2) and records `simd_tier=multi` in
   BUILD_INFO. `mx_tier_payload_pick()` is ready if the contract later
   grows one payload per tier.
4. V26 (attenuation against `warden.toml`) is loader policy and is not
   implemented in the emitter.
5. No golden `.mxsb` files are frozen yet; the tests build their inputs
   fresh. Freezing a golden set is the first thing to do once the loader
   exists, and from then on the contract's version policy applies strictly.

## 2. SIMD tiers (`tiers/`)

`mx_tiers.h` is the contract between the frontend driver, the super-op
kernels and the loader:

| tier | flags | CPUID rule |
|---|---|---|
| baseline | `-march=x86-64 -mtune=generic` | always |
| avx2 | `-march=x86-64-v3 -mtune=generic` | OSXSAVE, XCR0 SSE+AVX, AVX, AVX2, FMA, BMI1, BMI2, LZCNT, MOVBE, F16C, all present |
| avx512 | `-march=x86-64-v4 -mtune=generic` | avx2 rule plus AVX512 F, BW, CD, DQ, VL and XCR0 opmask, ZMM_Hi256, Hi16_ZMM |

A machine missing any one feature of a level reports the level below.
`MX_TIER=<name>` in the environment lowers the detected tier (never
raises it) so every build can be exercised on one machine. The header
also fails compilation when a tier build is given the wrong `-march`
(`-DMX_TIER=1` without AVX2 flags, a baseline build with AVX flags, an
avx2 build with AVX-512 flags).

`mx_tier_build.sh kernel.c --dispatch d.c -o libk.a` compiles the kernel
three times (`MX_TIER_FN(name)` gives `name_baseline/_avx2/_avx512`) and
archives them deterministically; `MX_TIER_DISPATCH(ret, name, params,
args)` in one baseline translation unit defines `name()` resolving on first
call. The frontend driver should take the flag strings from
`MX_TIER_MARCH[]`, not retype them.

## 3. Lifter hook (`lifter/`)

```
mxlift_hook in.c -o out.c [--report] [--dry-run] [-- -I... -D... -std=...]
clang -O2 -I../lfsr out.c ...
```

Recognised: `for (i = A; i < N | i <= N | i != N | N > i | N >= i; ++i)`
(also `i++`, `i += 1`, `i = i + 1`, counter declared in the `for` or
outside, and the counter's value after the loop is reproduced),
`while (n--)`, `while (n-- > 0)`, `while (n-- != 0)`; body = one or more
assignments to one unsigned 8/16/32/64-bit variable using `>> << ^ & | ~`,
constants (macros, `1u << k`, `~0u`), `-(x & 1) & P`, `0u - (x & 1)`,
`(x & 1) ? P : Q`, `(x & 1) * P`, `x >> (w-1)` as a bit, and the
`(x << k) | (x >> (w-k))` rotate idiom. Anything else, including a call,
another variable in the body, a signed state, a `volatile`, a counter used
in the body, or an 8/16-bit expression whose integer promotion the hook
cannot prove harmless, is left alone with a reason (`--report`).

The hook does no algebra of its own: it lowers the body to `mxl_insn`
(MOV, SHR, SHL, XOR, XOR_IMM, AND, AND_IMM, OR, NOT, MASKSEL, MUL_IMM,
ADD, ADD_IMM) and asks `mxl_lift()` for the proof and the exact matrix.
Only `MXL_OK` lifts. As a belt-and-braces check it also runs the lifter's
own interpreter against the matrix on eight seeds before rewriting.

Generated code: a prelude after the last `#include` (`#include
"mx_lift.h"`, trip-count helpers, one `static const mx_aff __mxl_aff_N`
per loop, which the tests confirm lands in `.rodata`), and the loop
replaced by a block computing the trip count and calling `mx_aff_jump()`.

Only an allowlist of flags is forwarded to clang (`-I -D -U -std= -isystem
-include -W* -f* -m* -O*` minus `-fplugin`, `-fpass-plugin`, `-Xclang`,
`@file`, `-specs`, `-B`): review finding 3. Clang is invoked by absolute
path with fork/exec, never through a shell or PATH.

## What is verified and what is not

Verified here (this container, x86-64, clang 18.1.3, gcc 13.3, ASan+UBSan):

- `tests/mxsb_test.c`: 40,113 checks pass: SHA-256 vectors; manifest
  grammar, normalisation and scope rules; one targeted mutation per
  validation rule V1..V25, V27, V28 producing the named error and rule;
  digest excludes only SIGNATURE and covers header/table/padding/BUILD_INFO; decode round
  trip; failed decode zeroes the image; 20,000 random mutations with no
  crash and only known error codes.
- `tests/run_mxsb.sh`: a real freestanding guest (static-pie and static
  exec) wrapped, signed, verified with the right key, refused with the
  wrong key and with a tampered TEXT (`MXSB_E_BUILD_ID`) or BUILD_INFO
  (`MXSB_E_SIGNATURE`); dynamically linked input refused; v1 decoded and
  upgraded. An independent Python implementation recomputes the digest (2.0.3
  rule), checks every padding byte is zero, and rebuilds the TEXT/DATA
  images and build_id from the ELF program headers; all match byte for byte.
- `tests/run_tiers.sh`: 22 checks: feature->tier rule on synthetic feature
  sets, payload pick rule, two identical archives from two builds, the
  baseline object contains no ymm/zmm instructions, avx2 contains ymm and
  no zmm, avx512 contains zmm, wrong-flag builds fail to compile, and the
  dispatcher picks each tier under `MX_TIER=` forcing with identical
  results (this machine is avx512, so all three bodies really ran).
- `tests/run_lifter.sh`: 15 cases compiled from original and hooked source
  print identical output across 8 seeds x 14 trip counts (0 to 1,000,003):
  galois LFSR with affine constant, xorshift32 (`while (n--)`), xorshift64
  (`!=` with the counter read afterwards), 8-bit ternary tap, 16-bit
  multiply tap with `N > i`, CRC-32 inner loop (constant 8), rotate idiom,
  inner loop under a non-affine outer loop, signed `<=` counter with
  negative ranges, `while (n-- > 0)`; five must-not-lift cases are left
  alone. Dangerous flags are refused.
- Speed, measured once on this machine, `clang -O2`, 10 million steps of
  the v1 recurrence: naive 16.36 ms per call, lifted 9.5 us per call, same
  result bits. One measurement, not a benchmark row; the tests thread owns
  the harness.

Not verified:

- No loader exists yet, so the layout convention above has not been
  exercised by actually mapping and jumping into an MXSB file.
- Ed25519 correctness rests on OpenSSL 3.0.13; no second implementation.
- The hook was run only on the test corpus and the bench file; it has not
  been run over a real game or library source.
- Windows and non-x86 targets: not built.
- The super-op name table is provisional (see contract notes).
