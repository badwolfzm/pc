# Handoff — Warden Linux executable compatibility corpus

**Task:** issue #29, bounded batch-inventory subtask.
**Owner:** ChatGPT agent session on 2026-11-05.
**Base:** `main` b2e4317cacc1059b81afd78c1ad3f5641498fe31.
**Branch:** `feat/linux-compat-corpus-report`.
**Paths:** `tools/linux-compat/corpus.py`, `test_corpus.py`,
`.github/workflows/linux-compat-corpus.yml`, this document.
No kernel, parser, privilege or syscall code is modified.

## Why

"Run almost all Linux apps" must be measurable on *named executable
corpora*. The previously merged single-file `preflight.py` reports
ELF structure blockers, but there was no deterministic bounded batch
inventory to track baseline compatibility.

## Usage

```sh
python3 tools/linux-compat/corpus.py --max-files 1000 /usr/bin > report.json
python3 -m unittest discover -v -s tools/linux-compat -p 'test_*.py'
```

The scanner recursively examines non-symlink regular files, in sorted
order, without executing code. It skips non-ELF files and files exceeding
the configured per-file cap (default 16 MiB). It emits deterministic JSON
with a per-binary classification and histogram of *preflight* blockers.
The default file-count cap is 1,000. Its results are not guaranteed to
represent a statistically unbiased sample of all Linux packages.

## Interpretation and security limits

**A candidate is NOT evidence that the executable runs in Warden.** This
report validates only the existing `warden-elf`-style static ELF header
constraints. The tool inherits preflight's limitations, cannot verify
syscalls, dynamic libc behavior, process lifecycle, GPU drivers or
sandbox policy, and never launches software inside Warden.

The source directory may change between filesystem enumeration, stat and
read; this is a developer-side inventory tool, **not a security boundary**.
It must never authorize running a binary based on its JSON classification.
The size cap limits memory per inspected file but the total summary can
still grow with max-files.

## Acceptance and handoff

- [ ] Python corpus plus existing preflight regressions pass.
- [ ] GitHub runner scans a bounded real host executable directory.
- [ ] Independent review of sorting, symlink handling, limits and honest
      reporting.
- [ ] Named unchanged Linux executables actually booted in Warden/QEMU.
      **Future integration test only**, not part of this PR.
- [ ] Target PC/WSL2 inventory, separate hardware verification.

The next agent should use reports to choose a specific static-musl
executable and verify it with the full Warden kernel and policy, rather
than conflating structural preflight with runtime compatibility.
