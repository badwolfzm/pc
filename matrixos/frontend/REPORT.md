# Frontend upgrade report: what changed, how it is proven, what is left

Thread: "Upgrade MatrixOS compiler frontend". Date: 2026-11-05.
Module: `matrixos/frontend/`. Details and flag tables: `README.md` there.

## What the baseline was

The "MatrixOS compiler" in the uploaded files is a 104-line wrapper
(`matrixos_faststripcc_mini_swrap`) that builds a clang or gcc command line
for four profiles and execs it, plus a loop-signature struct filled by hand
in the prototype. There is no lexer, parser or type checker, and the
typed-input rule from the design doc exists only in the kernel's Rust crate.
The coordinator's original brief (parsing, type checking) was therefore
re-scoped with the lead thread to: driver, profiles, SIMD tiers, typed input.
No new language.

Baseline defects fixed (each has a regression test):

| defect | where | fix |
|---|---|---|
| `--version` wrote 35 of 39 bytes, so no newline and a cut version | `swrap.c:49` | `printf`; test asserts the whole line |
| `-S` only; `-c`, `-E` and linking unsupported | `swrap.c:59` | all four modes |
| `runtime-fast` used `-march=native` (not reproducible; control decision 3) | `swrap.c:88` | tiers `baseline`/`avx2`/`avx512` = psABI `x86-64`/`-v3`/`-v4`; `-march=native` exits 2 |
| unknown flags forwarded, incl. `-fplugin`, `-specs`, `@file`, `-B`, `-Xclang -load`; compiler found via `$PATH` with `execvp` (security review finding 3) | `swrap.c:52,101` | denylist exits 2; compiler resolved only from `/usr/bin:/usr/local/bin:/bin`; `execve` on an absolute path with a minimal environment |
| `alloca` for argument vectors, no ceiling | `swrap.c:22,64` | fixed arrays with a hard ceiling (4096); overflow is a fatal error |
| no hardening or reproducibility flags | | both on by default, `--no-harden` / `--no-reproducible` to drop |
| typed-input rule enforced nowhere in C (finding 2) | whole prototype | `mx_input.h` + `mx_input.c`, matched to `warden-caps/src/cells.rs` with a shared vector file |

## What is done (all run here, all green)

| deliverable | proof |
|---|---|
| `src/mxcc.c` driver | 113 checks in `tests/driver/run.sh`: every profile and tier flag, refusals, planted-`clang`-in-PATH not run, two builds byte-identical, four profiles emit assembly with gcc and clang, hardened link runs, `--emit-build-info` digest equals `sha256sum` |
| finding 3 | tests thread's `security_regressions.sh` F3 block passes against `src/mxcc.c` (five refusals plus the PATH check) |
| `include/mx_input.h`, `src/mx_input.c` | 18 must-fail programs refused and 6 must-pass programs compiled, linked and run, by both compilers (48 cases); unit tests and 200,000 fuzz iterations under ASan/UBSan |
| agreement with the kernel's Rust model | `tests/vectors/cells.vec` (57 vectors: bytes, scalars, surrogates, controls, widening, UTF-8 edge cases) passes in C and in a Rust runner that includes the real `cells.rs` |
| MXSB `BUILD_INFO` | `--emit-build-info` writes `profile`, `simd_tier`, `source_sha256`, `toolchain` per contract section 3.3 |
| baseline still builds | the uploaded prototype compiles with the default profile on both compilers (4 VLA warnings; `--strict` makes them errors) |

## Independent audit (2026-11-05), frontend items

| audit finding | status |
|---|---|
| Issue 1 (HIGH): `-- -fplugin=...` bypassed the deny policy and a plugin constructor ran | fixed in `src/mxcc.c` 1.0.1: one check on every argument path, paired values checked; `tests/driver/plugin_regression.sh` reproduces the attack with a real shared object (control run writes the marker) and requires refusal with no marker for 11 shapes on gcc and clang, all passing |
| Harness: missing compiler counted as a passing rejection | fixed: setup probe through mxcc (exit 3 on failure), fail cases need a real compiler diagnostic |
| Driver tests: 13 false failures from bare `clang` outside the fixed directories | fixed: real-compile blocks use absolute paths; search list widened (still no `$PATH`) |
| Canvas integer overflow (issue 2) | not this module; belongs to the canvas thread |
| Lead follow-up: tier flags shared with codegen | done (1.0.2): `mxcc` reads names, `-march` strings and `-DMX_TIER` from `codegen/tiers/mx_tiers.h`; `tests/unit/test_tiers.c` fails if the composed command line ever differs from the contract |
| avx512 tier | build-only; the user's PC has no AVX-512, so **not verified** at run time |

No commit or PR: the control plan's decision 4 keeps all code in the project files until the user names a GitHub repository.

## What is planned, not done

1. **clang-tidy check** for the two C escape hatches (`.v` access and brace
   initialisation of `mx_cell`/`mx_ctl`/`mx_sym` outside `mx_input.[ch]`).
   Today this is a documented grep rule for review.
2. **aarch64**: the flag set exists (`-march=armv8-a`, `-mbranch-protection=standard`)
   but no cross compiler was available, so it is dry-run tested only.
3. **Warden sysroot**: `--freestanding` and `--sysroot` are plumbed; the
   `x86_64-warden-linux-musl` target from the design doc needs the kernel's
   ABI headers, which do not exist yet.
4. **Loop signatures for the LFSR lifter**: the prototype's `mx_loop_sig` is
   hand-filled. Deriving it from source needs a parser, which the control plan
   assigns to the lifter thread's detection; the frontend only passes
   `-DMX_SIMD_TIER` so the lifter can pick a kernel.
5. **Benchmarks**: no speed claim is made for any profile or tier. The
   benchmark thread owns measurement; this module gives it reproducible flags.
6. **Rust widen for UTF-8**: `cells.rs` has byte widening only. If the kernel
   adds a scalar widener, add it to the vector runner so both sides are checked
   by the same lines (the C side already is).

## Open points for other threads

- Tests thread: "missing `-S` exits 2" no longer holds (mxcc links by default);
  "no input files exits 2" is the replacement. F2 can be wired to
  `make -C matrixos/frontend test`.
- Kernel thread: `tests/vectors/rust` reads `WARDEN_CELLS_RS`; if the crate
  moves, only the Makefile default changes. The Rust runner strips `//!` lines
  to `include!` the file; nothing else is altered.
- GPU command set and canvas threads: include `include/mx_input.h` for
  validated command arguments; `mx_validate_len` is the length check the
  security review asks for at the super-op boundary.
- The VFS 512-byte truncation the tests thread reported is in the prototype's
  VFS, outside this module; it needs an owner.
