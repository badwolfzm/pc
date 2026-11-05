# MatrixOS C source — security review (wave 0)

Baseline reviewed (frozen, not edited):

- `uploads/hearth/80bdbc6c-5943-4bfa-bb12-10d83f3d6e4f` — the MatrixOS v9
  compact phase55-61 prototype. Line numbers below are for this file after
  stripping CRLF to LF (593 lines). It is referred to as `matrix.c`.
- `uploads/hearth/ebc8e1ed-bb9c-409f-b145-6e696ca9e262` — the
  `matrixos_faststripcc_mini_swrap` compiler wrapper (104 lines). Referred
  to as `swrap.c`.

Method: read both files, built `matrix.c` under GCC AddressSanitizer +
UndefinedBehaviorSanitizer and ran it, built `swrap.c` and exercised its
argument forwarding against a stub compiler, and wrote small harnesses to
drive the VFS jail directly. Each finding is marked **verified** (reproduced
at runtime) or **suspected** (found by inspection, not run).

The typed-input rule in the task (input values `0..=0x10FFFF`, control
values strictly above, so input can never forge a terminator or command)
lives in the Rust `warden-caps` crate (`crates/warden-caps/src/cells.rs`).
It is **not present anywhere in these C files** — see finding 2.

---

## Ranked findings

### 1. VFS jail does not resolve symlinks — arbitrary read/write escape — CRITICAL — verified

- `mx_vfs_map_guest_to_host` — `matrix.c:355`
- `mx_vfs_read_all` — `matrix.c:370`
- `mx_vfs_write_all` — `matrix.c:394`
- `mx_guest_path_allowed` / `mx_path_has_dotdot_segment` — `matrix.c:338`, `matrix.c:328`

The jail validates the *guest path string*: it must start with `/sandbox/`
and contain no `..` segment. It then builds a host path by string
concatenation (`matrix.c:364`) and calls `fopen` on it (`matrix.c:376`,
`matrix.c:401`). It never resolves the final path, so a symlink that lives
inside the sandbox directory and points outside it is followed.

Reproduced: with `sandbox/link.txt` a symlink to `../outside.txt`, a call to
`mx_vfs_read_all(v, "/sandbox/link.txt", ...)` returned rc=0 and the contents
of the file outside the sandbox. The same hole applies to `mx_vfs_write_all`,
giving arbitrary write. This defeats the central "deterministic VFS jail"
claim. The string checks stop `../` in the request but do nothing about a
symlink the guest (or a prior write) placed in the tree.

For comparison, the Warden Phase 0 README states opens are meant to be done
with `openat2(RESOLVE_BENEATH)` so `..`, symlinks and races cannot leave a
grant. This prototype's jail does none of that. Any port of this VFS to the
kernel must open with `RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS` (or `O_NOFOLLOW`
plus a beneath-check), not validate a string and then `fopen`.

### 2. Typed-input rule is not enforced anywhere in the C — HIGH (design) — verified by inspection

Scope asked: is the typed-input rule (input `0..=0x10FFFF`, control above)
enforced anywhere? Answer: no. There is no `Cell` type, no widening step, no
separation of untrusted input from control values in either C file. Guest
paths, super-op arguments, MXSB header fields and canvas coordinates are all
handled as raw bytes or raw integers. Untrusted input and control/command
values share the same representation, which is exactly the confusion the
Rust cell model exists to prevent. Wave-1 frontend/typed-input work should
treat the Rust `warden-caps::Cell` model as the contract and port it; the C
prototype provides no enforcement to build on.

### 3. `swrap` forwards attacker-controlled compiler flags and resolves the compiler via PATH — HIGH — verified (forwarding) / suspected (impact)

- argument loop — `swrap.c:26`–`swrap.c:57`
- `execvp(cc, cmd)` — `swrap.c:101`

Any argument the wrapper does not recognise is pushed straight into the
compiler command line (`swrap.c:52`). Reproduced against a stub compiler:
`-fplugin=/path/evil.so`, `-Xclang -load -Xclang /path/evil.so`,
`-specs=/path/evil.specs` and `@response-file` all pass through unchanged.
Each of these causes the compiler to load and run attacker-chosen code at
compile time. If this wrapper ever compiles untrusted source or untrusted
build flags inside the "secure" system, that is code execution on the build
host.

Separately, the compiler is launched with `execvp` (`swrap.c:101`), which
searches `PATH`. The `--cc` value is restricted to `clang`/`gcc`
(`swrap.c:62`), but which binary those names resolve to is whatever `PATH`
points at, so a planted `clang`/`gcc` earlier in `PATH` is run. Suspected
impact only because it depends on the caller's environment and trust model;
forwarding and the `execvp` behaviour are both confirmed by reading and
running the code. Mitigation: allowlist the exact flags the profile needs,
reject `-fplugin`/`-load`/`-specs`/`@file`/`-B`, and invoke an absolute
compiler path.

### 4. One-byte out-of-bounds read in `mx_path_has_dotdot_segment` — MEDIUM — verified

- `matrix.c:330`

```c
char c0 = p[i], c1 = p[i+1], c2 = p[i+2];
```

The loop condition only guarantees `p[i]` is in bounds. When `p[i]` is the
last character, `p[i+1]` is the NUL terminator (in bounds) but `p[i+2]`
reads one byte past the end of the buffer. AddressSanitizer flagged this on
the normal test run (global-buffer-overflow at `matrix.c:330`, reading one
byte past the literal `"/sandbox/hello.txt"`), and a standalone
heap-allocated reproducer triggered the same ASan read-of-size-1. It fires
on essentially every path that does not end in `..`. Worst realistic case is
a crash when the path sits at the end of a mapped page; it is reached before
any file I/O. Fix: read `c1` only after confirming `c0`, and `c2` only after
confirming `c1`, or bound-check `i+2 <= len`.

### 5. Fuzz harness exercises almost none of the jail or parsers — MEDIUM (coverage gap) — verified by inspection

- `mx_fuzz_run` — `matrix.c:482`

The fuzzer builds a random path of 1..50 characters that always begins with
`/` followed by random bytes from a 31-character alphabet. For
`mx_vfs_map_guest_to_host` to succeed the path must begin with the literal
`/sandbox/`, which this generator effectively never produces. So every
iteration is rejected at the prefix check and the harness only ever
increments `vfs_rejects`. It never reaches a successful map, never calls
`mx_vfs_read_all`/`mx_vfs_write_all`, never creates or follows a symlink, and
never feeds bytes to `mx_sniff_guest`/MXSB parsing or the canvas. The
reported result "fuzz 20k iters, no crash" (`matrix.c:507`,
`matrix.c:536`) therefore says almost nothing about the jail — finding 1
sits entirely outside what it covers. The shipped binary is also not built
with sanitizers, so even the paths it does hit would not report memory
errors. A useful harness should generate valid `/sandbox/...` inputs, plant
symlinks, and fuzz the MXSB header and canvas, under ASan/UBSan in CI.

### 6. Super-op dispatch trusts caller-supplied lengths with no capacity check — LOW/MEDIUM — suspected

- `mx_superop_dispatch` — `matrix.c:287`

The MEMCPY case copies `min(a1, a3)` bytes (`matrix.c:293`), which is safe
only if `a0` really has `a1` bytes and `a2` really has `a3` bytes; the
function cannot verify that. CRC32 reads `a1` bytes from `a0`
(`matrix.c:298`) with no length argument to cross-check at all. GPU_CLEAR
does `memset(a0, ..., a1)` (`matrix.c:314`) with no second-buffer bound.
GPU_VADD does check `a3 < a1` (`matrix.c:306`), so it is the only case that
cross-checks. These are API-contract weaknesses rather than bugs reproducible
from the current callers (the test and fuzz callers pass correctly sized
buffers), but a kernel super-op boundary must treat these lengths as
untrusted and validate them against the real backing capability, not trust
the caller. Marked suspected because I did not drive a caller that overflows
them.

### 7. No MXSB header parser exists; a future loader that trusts header fields is unsafe — LOW (forward-looking) — suspected

- `mx_sniff_guest` — `matrix.c:104`
- `mxsb_header` + `mx_emit_superbin_demo` — `matrix.c:473`, `matrix.c:478`

Scope asked about MXSB header parsing. There is no parser in this file: only
a 4-byte magic sniff (correctly bounded by `n >= 4`) and an *emitter* that
writes a fixed 64-byte text / 32-byte data blob. So there is no current
untrusted-header parse to exploit. The forward-looking risk is in the
contract: `text_size`, `data_size`, `entry_off` and `caps_required`
(`matrix.c:474`) are attacker-controlled in any real `.mxsb` file. A loader
that allocates `sizeof(header) + text_size + data_size` (as the emitter does
at `matrix.c:478`) will integer-overflow on 32-bit sizes near `UINT32_MAX`,
and `entry_off`/the sizes must be bounds-checked against the actual file
length before use. The MXSB manifest contract (wave 0, `matrixos/contracts/
mxsb-manifest.md`) should pin: sizes validated against file length, overflow-
checked allocation, `entry_off` within `text`, and `caps_required` never
granting more than the loading subject already holds.

---

## Summary table

| # | Finding | Location | Severity | Status |
|---|---------|----------|----------|--------|
| 1 | Symlink escapes VFS jail (read + write) | `matrix.c:355,370,394` | Critical | verified |
| 2 | Typed-input rule not enforced in the C | whole file | High (design) | verified by inspection |
| 3 | `swrap` forwards `-fplugin`/`-load`/`-specs`/`@file`; `execvp` uses PATH | `swrap.c:52,101` | High | verified / suspected impact |
| 4 | 1-byte OOB read in dotdot check | `matrix.c:330` | Medium | verified |
| 5 | Fuzz harness never reaches the jail/parsers | `matrix.c:482` | Medium | verified by inspection |
| 6 | Super-op dispatch trusts caller lengths | `matrix.c:287` | Low/Medium | suspected |
| 7 | No MXSB parser; future loader must validate header | `matrix.c:104,473` | Low | suspected |

## For the wave-1 threads

- **Frontend / typed input**: port the Rust `Cell` model (finding 2); do not
  build on the C prototype, which has no typed-input layer.
- **Any thread touching the VFS**: opens must be beneath-rooted and
  symlink-safe at the syscall, not string-validated then `fopen` (finding 1).
- **Super-ops / GPU**: validate every length and offset against the real
  backing buffer at the boundary (finding 6).
- **MXSB contract owner**: pin overflow-checked allocation and
  length/offset/caps validation before any loader is written (finding 7).
- **Tests owner**: build everything with ASan/UBSan in CI and make the fuzzer
  generate valid in-jail paths, symlinks, and malformed MXSB headers
  (finding 5).

Not verified in this review: behaviour on Windows (`_WIN32` paths were read
but not run), and any concurrency/race behaviour (the prototype is
single-threaded). The `swrap` PATH-hijack impact depends on the deployment's
environment and was not demonstrated end to end.
