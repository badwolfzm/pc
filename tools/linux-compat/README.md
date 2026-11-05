# Linux ELF compatibility preflight

**Issue:** #29, scoped to a read-only preflight and test matrix. This does not
implement the dynamic linker, new syscalls, process lifecycle, or filesystem.

Run:

```sh
python3 tools/linux-compat/preflight.py /path/to/program
python3 tools/linux-compat/preflight.py --target native /path/to/program
python3 -m unittest discover -v -s tools/linux-compat -p 'test_*.py'
```

Exit codes: **0** means the file is a **candidate**, **1** means a known
incompatibility, **2** means inspection failed (including I/O or the 256MiB
size limit). JSON includes exact blockers and observed ELF facts. No code
from the inspected file is executed. Do not treat a successful preflight as
proof of security or compatibility: it is necessary-but-not-sufficient
inspection against the present `warden-elf` loader's guards.

## Loader baseline from repository

`crates/warden-elf/src/lib.rs` accepts ELF64 little-endian x86-64 ET_EXEC,
non-overlapping PT_LOAD mappings within its designated guest window, at most
eight nonempty load segments, a valid executable entry, and no W+X load
segment. Warden has two windows: Linux `0x400000..0x2000000`; native
`0x800000..0x900000`. PT_INTERP and general dynamic-library loading have
no functioning userspace implementation. This scanner records PT_INTERP
and PT_DYNAMIC as explicit blockers even if the current Rust parser skips
unknown program headers; this is deliberately stricter than the parser.

A candidate may still fail inside Warden because of missing syscalls, libc,
memory accounting, unimplemented process/thread semantics, runtime linker,
or security policy. This tool is **not an ELF validator for untrusted
execution**, and must not be wired into any kernel trust boundary.

## Coordination / handoff

- **Claimed subtask:** ELF capability classification / preflight evidence.
- **Owner:** this ChatGPT implementation session; owner claim in issue #29.
- **Base:** current `main` (no dependency on PRs #24, #26, #30).
- **Exclusive paths:** `tools/linux-compat/**`,
  `.github/workflows/elf-compat-preflight.yml`.
- **No concurrent edits** to `warden-elf`, the kernel loader,
  `control/STATUS.md`, or existing agent coordination files.
- **Tests actually executed:** GitHub Actions run [42008925083](https://github.com/badwolfzm/pc/actions/runs/42008925083): Python regression suite passed (17 tests), and real host ELF classification check passed. This validates the scanner, not Warden runtime compatibility.
- **Unverified:** real Warden execution, Windows/WSL2 target host,
  libc/static-musl/dynamic-linker behavior.
- **Review:** independent comparison with `warden-elf` parser invariants,
  boundary/overflow checks, and potential false positives/negatives.
- **Next work after this subtask:** classify named unmodified binaries,
  implement isolated readonly filesystem, PT_INTERP loader/relocation policy,
  then run actual workloads inside Warden/QEMU. Owner of issue #29 should
  coordinate before claiming those areas.
