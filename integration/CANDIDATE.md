# Single-commit combined code candidate — DRAFT / NOT VERIFIED

Base: `19aff8a13dab2fe1f1ea95de88ff2d318717809b` (main at creation).
This commit overlays exact committed blobs from **PR #24 + stacked #26** (`3a5b031f4c2ad9525c091a16f7e8e27155aba36f`, 15 files), **PR #34** (`42641391fd512ddf4bf3781e04c8e72e97b9cf0f`, 4 files), **PR #37** (`2d00c30a2f8940b43ea164ebdaff16e3d1abc276`, 3 files), and **PR #33** (`e0ca5686ef634ff7057553c63eec2f4a03c03595`, 8 files). Total: 30 distinct source paths plus this document. Source blobs were not manually modified. Work already merged as PR #39 is retained from main.

## Known hazards and merge blockers

- PR #26 depends on #24. Its `crates/warden-kernel/src/tests.rs` supersedes #24's version (not a naïve independent patch). Independently check source semantics.
- PR #42 adds a network namespace negative regression and is **not** included; evaluate it before merge, especially whether it exposes a defect in #24's listener rules.
- PR #33's broker prototype is separate from Warden kernel integration. It does not prove secure `getrandom` or wall clock in actual guests.
- The competing PR #31 entropy/time design was **not** included. Require owner/lead reconciliation before adopting #33's contract.
- Do not assert general Linux compatibility or real game execution; #34 is a read-only classifier and #37 explicitly rejects unsupported ELF headers.
- This is a mechanical Git blob integration. It is **not a code review**, nor test evidence. Review current `main` and source heads for concurrent changes and conflicts before merge.

## Blocking acceptance gates

1. `cargo test`; `bash tests/kernel/run.sh`; `bash tests/compat/run.sh`; `bash tests/escape/run.sh` in supported Linux environment, with exact exit statuses/logs.
2. `python3 -m unittest discover -s tools/linux-compat` or the documented PR #34 test command. PR #37 loader guard, PR #33 broker/provider unit tests, and PR #24/#42 namespace integration tests.
3. Independent security review of **combined** Warden ELF, Linux ABI, networking and entropy changes; validate capability isolation, memory bounds, W^X, unsupported syscalls and failure behavior.
4. QEMU guest workloads with negative cases, then target Windows/WSL2 PC test per `HOST.md`. Do not substitute Linux for the Warden microkernel.
5. Confirm complete provenance and obtain explicit user approval for a merge. No automatic merge.

Tests run by this assembling session: **none**. Verification limited to matching source tree entries and checking GitHub's resulting commit/diff. Keep PR in draft.
