# MatrixOS compiler frontend: `mxcc` driver and typed input

Owner: thread "Upgrade MatrixOS compiler frontend". Scope per `control/CONTROL.md`:
driver CLI, build profiles, fixed SIMD tiers, and the typed-input rule. No new
language, no parser. The uploaded files are frozen; nothing here edits them.

```
make            # build/mxcc, build/libmxinput.a
make test       # all tests (needs gcc and clang; the Rust check needs cargo and the kernel tarball)
make install PREFIX=/opt/matrixos
```

## What is here

| path | what |
|---|---|
| `src/mxcc.c` | the driver (single file, C11, no dependencies) |
| `include/mx_input.h`, `src/mx_input.c` | typed input: cells, control values, frames, validators |
| `tests/driver/run.sh` | 113 checks on composed command lines, refusals, real compiles, determinism, build info |
| `tests/typed_input/pass`, `fail` | 6 programs that must compile and run, 18 that must be refused, each with gcc and clang |
| `tests/unit/test_input.c` | unit tests and a 200,000-iteration fuzz loop, run under ASan and UBSan |
| `tests/vectors/cells.vec` | 57 vectors shared with the kernel; `tests/unit/test_vectors.c` (C) and `tests/vectors/rust` (kernel `cells.rs`) both run them |

## The driver

```
mxcc [--cc=clang|gcc] [--profile=P] [--tier=T] [--arch=A] [--typed-input] [--strict]
     [--freestanding] [--no-harden] [--no-reproducible] [--emit-build-info=FILE]
     [--dry-run] [-E|-S|-c] [-o OUT] files... [-- passthrough]
```

Four independent axes compose the command line:

| axis | values | flags added |
|---|---|---|
| profile | `compile-fast` | `-O0`, no unwind tables |
| | `matrixos` (default) | `-O2 -fno-plt -fomit-frame-pointer -ffunction-sections -fdata-sections`, link: `--gc-sections -s` unless `-g` |
| | `balanced` | `-O1`, no unwind tables |
| | `runtime-fast` | `-O3 -fomit-frame-pointer -fno-plt -fno-semantic-interposition` (no `-march=native`) |
| tier (x86-64) | `baseline` (default) | `-march=x86-64` (SSE2), `-DMX_SIMD_TIER=0` |
| | `avx2` | `-march=x86-64-v3` (AVX2, FMA, BMI1/2), `-DMX_SIMD_TIER=1` |
| | `avx512` | `-march=x86-64-v4` (plus AVX-512 F/BW/CD/DQ/VL), `-DMX_SIMD_TIER=2` |
| tier (aarch64) | `baseline` only | `-march=armv8-a` |
| hardening (default on) | | `-fstack-protector-strong -fstack-clash-protection -fcf-protection=full -ftrivial-auto-var-init=zero -fno-delete-null-pointer-checks -fno-strict-aliasing -fPIE -D_FORTIFY_SOURCE=3`; link `-pie -z relro -z now -z noexecstack` |
| reproducibility (default on) | | `-fno-ident -ffile-prefix-map=$PWD=. -frandom-seed=<output name> -Werror=date-time`, no command-line recording, `SOURCE_DATE_EPOCH=0` if unset |
| policy warnings | always | `-Wall -Wextra -Wformat=2 -Wvla -Walloca -Wshadow -Wcast-qual`; errors: implicit function declaration, implicit int, int conversion, incompatible pointer types, missing return |
| `--strict` | | VLA, alloca, conversions, shadowing become errors (every buffer has a hard ceiling) |

`-march=native`, `-mtune=native`, `-mcpu=native` exit 2 unless `--allow-native`.
Any other `-march=`/`-mcpu=` exits 2: tiers are the only ISA selection.
`-DMX_PROFILE_<NAME>=1`, `-DMX_SIMD_TIER=<n>` and `-DMX_SIMD_TIER_NAME="<tier>"` let
the super-op and codegen modules pick kernels per tier at compile time.

### Security review finding 3

The baseline wrapper forwarded every unknown flag and found the compiler via
`$PATH`. `mxcc`:

- exits 2 on `-fplugin*`, `-fpass-plugin*`, `-Xclang`, `-load`, `-specs*`, `-B*`,
  `-wrapper`, `@file`, `-Wp,*`, `-Wa,*`, `-save-temps`, `-fprofile-use*`,
  `-isysroot`, `-T*`, and `-Wl,`/`-Xlinker` values naming a plugin, linker
  script, `-rpath`, `--dynamic-linker` or lazy binding;
- resolves a bare `clang`/`gcc` only in `/usr/bin`, `/usr/local/bin`, `/bin`
  (never `$PATH`); an absolute `--cc` must be a regular, non-world-writable file;
- runs the compiler with `execve` on that absolute path and a minimal
  environment (`PATH=/usr/bin:/bin`, `LC_ALL`, `SOURCE_DATE_EPOCH`, `TMPDIR`),
  so `LD_PRELOAD`, `COMPILER_PATH`, `GCC_EXEC_PREFIX`, `CPATH` and friends from
  the caller never reach it. `--keep-env` opts out.

### MXSB build info

`--emit-build-info=FILE` writes the `BUILD_INFO` lines from the MXSB v2
contract (`profile`, `simd_tier`, `source_sha256`, `toolchain`), plus the
driver version, the three policy switches and the full flag list.
`source_sha256` is SHA-256 over the input files in command-line order; the
test checks it against `sha256sum`.

## Typed input (`mx_input.h`)

The rule from the design doc: every input byte or Unicode scalar becomes a
32-bit cell in `0..=0x10FFFF`; control values (`MX_CTRL_END = 0x110000`,
`MX_CTRL_FRAME = 0x110001`) live only above that; untrusted values cannot be
used where trusted data is expected, and the only exit is a validator.

C has no generics or ownership, so the enforcement rests on one property it
does have: **structs never convert implicitly**. The types are:

| type | invariant | built by |
|---|---|---|
| `mx_cell` | `v <= 0x10FFFF` | `mx_cell_from_byte`, `mx_cell_from_scalar` (refuses `> 0x10FFFF` and surrogates) |
| `mx_ctl` | `v > 0x10FFFF` | `mx_ctl_make` (refuses input-range values), `mx_ctl_end`, `mx_ctl_frame` |
| `mx_sym` | either, disjoint ranges | `mx_sym_from_cell`, `mx_sym_from_ctl`; read back only through `mx_sym_cell` / `mx_sym_ctl`, which refuse the other kind |
| `mx_untrusted_bytes`, `_u32`, `_u64` | raw wire values | `mx_untrusted_*_new`; leave only via `mx_validate_*` |
| `mx_frame` | receiver-owned storage, out-of-band `len` | `mx_frame_init(storage, cap)` |

Mapping to the kernel's Rust model (`warden-caps/src/cells.rs`): `Cell::from_byte`
= `mx_cell_from_byte`; `Cell::from_char` = `mx_cell_from_scalar` (C must check
what Rust's `char` guarantees); `Cell::control` = `mx_ctl_make`; `Cell::is_input`
= `mx_sym_is_input`; `widen(bytes, max)` = `mx_widen_bytes` (one cell per byte,
then `CTRL_END`, `None`/`MX_E_BUDGET` when `len > max`); `Untrusted<T>::validate`
= the `mx_validate_*` family. The Rust `Cell` covers both ranges; the C split
into `mx_cell`/`mx_ctl` is what turns misuse into a compile error. Both sides
run the same 57 vectors.

What a plain C compiler now refuses, with no plugin (all 18 are in `tests/typed_input/fail`):

| attempt | why it fails |
|---|---|
| `table[c]`, `tbl[u]`, `tab[c]()` | subscript is not an integer (R3: input never steers memory or code) |
| `c == 0x110000`, `c + 1`, `1u << c`, `if (c)`, `switch (c)` | invalid operands / not scalar (R2, R3, R4) |
| `int x = c;`, `cmd_t k = c;`, `mx_ctl t = c;`, `mx_cell c = s;` | incompatible struct types (R1, R4) |
| `c = 0x110000;`, `t = 0x110000;` | integer into struct (R4: only constructors build cells and controls) |
| `memcpy(dst, src, n)` with `mx_untrusted_u64 n`, `char buf[c]` | size is not an integer (R5: lengths go through `mx_validate_len`) |
| `uint32_t *p = c_ptr;` | incompatible pointer types, promoted to an error |
| `unknown_sink(c)` | implicit declaration is an error, so no K&R hole |

Receiver ceilings: `mx_widen_*` take the receiver's `max` and write into a
frame of the receiver's `cap`; exceeding either returns `MX_E_BUDGET` with
nothing written. Validators take the destination size and return
`MX_E_BUDGET` rather than truncating. `mx_validate_len(len, offset, backing)`
is the check the security review asked for on super-op lengths (finding 6).

### Honest limits

- A brace initialiser (`mx_cell c = { 0x110000 };`) and direct field access
  (`c.v`) bypass the model; C cannot forbid them. Review rule: outside
  `mx_input.[ch]`, `grep -nE '\.v\b|mx_(cell|ctl|sym) [a-z_]* *= *\{'` must be empty.
  A clang-tidy check for this is planned, not written.
- `--typed-input` injects the header and the `-Werror` set; it cannot prove a
  program only touches input through cells. It makes the wrong thing fail to
  compile, which is the design doc's bar ("misuse is a compile error"), not a
  proof.
- `mx_widen_utf8` has no Rust twin in `cells.rs`; the Rust vector runner uses
  `core::str::from_utf8` + `Cell::from_char` as the reference, and the two
  agree on the 27 UTF-8 vectors including overlongs, surrogates, `> 0x10FFFF`,
  stray continuations and truncations.

## Verification status (status vocabulary from the prototype's own report)

| item | status |
|---|---|
| `mxcc` builds with gcc 13 and clang 18, `-Wall -Wextra` clean | build-tested |
| 113 driver checks (command lines, refusals, PATH hijack, determinism, four profiles emit assembly with both compilers, three tiers compile, hardened link runs, build-info digest equals `sha256sum`) | behavior-verified, run here |
| tests thread `security_regressions.sh` F3 block against `src/mxcc.c` | behavior-verified, run here (all F3 checks pass) |
| 18 fail cases refused and 6 pass cases run by both gcc and clang | behavior-verified, run here |
| `mx_input` unit tests + 200k fuzz iterations under ASan/UBSan | behavior-verified, run here |
| 57 shared vectors agree between C and the kernel's `cells.rs` | behavior-verified, run here (rustc 1.97) |
| baseline prototype (`matrix.c`, read-only copy) builds with the default `matrixos` profile on both compilers; 4 VLA warnings, which `--strict` turns into errors | smoke-tested |
| binary size: baseline prototype, `matrixos` profile, clang, stripped: 30,928 bytes on disk, 22,230 bytes text | measured once, not a benchmark |
| aarch64 flag set | dry-run only, no cross compiler here: **not verified** |
| any speedup from tiers or profiles | **not measured**; the benchmark thread owns that |
| use inside a Warden container or the kernel | **not exercised** |

## Changelog

### 1.0.1 (2026-11-05): independent audit fixes
- **Issue 1 (HIGH): `--` passthrough bypassed the flag deny policy.** Arguments
  after `--` were copied verbatim, so `-- -fplugin=x.so` loaded a plugin. Every
  argument path now goes through the same check (`is_denied_flag`), and options
  that take a following value (`-Xlinker`, `-include`, `-imacros`, `-Xassembler`,
  `-Xpreprocessor`, ...) have that value checked with `is_denied_value`. Also
  denied now: `-fuse-ld=`, `--ld-path`, `-flto`, `-fuse-linker-plugin`,
  `-fsanitize=`, profile generation, `-Xassembler`, `-Xpreprocessor`.
  Regression: `tests/driver/plugin_regression.sh` builds a real shared object whose
  constructor writes a marker, proves with a direct gcc run that the probe loads
  (control), then requires exit 2 and no marker for 11 argument shapes with both
  compilers (25 checks).
- **Harness weakness: a missing compiler counted as a passing rejection.**
  `tests/typed_input/run.sh` now probes that mxcc can invoke each compiler first
  (setup error, exit 3, otherwise), and a fail case passes only when the log holds
  a compiler diagnostic and no `mxcc: error`. `tests/driver/run.sh` real-compile
  blocks use absolute compiler paths from `command -v`, so a clang outside mxcc's
  fixed search directories is still exercised; the fixed list gained
  `/usr/lib/llvm-{17,19,20}/bin` and `/opt/homebrew/opt/llvm/bin`. `$PATH` is
  still never used by mxcc itself.

### 1.0.2 (2026-11-05): tier flags come from the codegen contract
- `src/mxcc.c` includes `../../codegen/tiers/mx_tiers.h` and takes tier names,
  `-march=` strings, `-mtune=generic` and the `-DMX_TIER=<n>` define from it, so the
  driver and the SIMD kernels cannot drift. `-DMX_SIMD_TIER=<n>` is still emitted
  for 1.0.0 callers. `tests/unit/test_tiers.c` (`make test-tiers`) runs a dry run
  per tier and requires exactly the contract's strings, one `-march=` flag, and no
  `-march=native`. Building `src/mxcc.c` on its own still works from the project
  layout (the include is relative); outside it, pass `-I<dir of mx_tiers.h>`.
- **avx512 is build-only.** The user's PC has AVX2 and no AVX-512, so the avx512
  tier compiles (checked by the driver tests with both compilers) but has never
  run: **not verified**. avx2 and baseline builds have run here.
