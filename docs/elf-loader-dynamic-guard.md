# Warden ELF static-loader guard

## Purpose

Issue #29 requires a path to dynamically linked Linux programs. The
existing Warden ELF loader does **not** implement PT_INTERP interpreters,
dynamic symbols, library search, relocations, or a filesystem-backed linker.

The pre-existing parser silently ignored both PT_INTERP and PT_DYNAMIC
program headers. This change rejects them as
`ElfError::UnsupportedDynamicLinking`, instead of allowing an ET_EXEC
binary that needs a dynamic linker to pass the static loader's validation.

This is a fail-closed contract, **not dynamic Linux compatibility**.

## Security properties and limits

- The check runs in `warden-elf::parse`, before the loader allocates any
  physical frame for the rejected image.
- Valid non-dynamic ET_EXEC programs retain the prior code/stack/segment
  constraints (W^X, memory-window bounds and segment overlap checks).
- A future loader may add PT_INTERP/PT_DYNAMIC support only after specifying
  the interpreter's file-grant provenance, relocation validation, memory
  accounting, loaded-library capabilities and failure/rollback model.
- ELF inspection does not establish that a program can use Warden syscalls
  or even launch. QEMU guest tests are separate proof.
- PT_DYNAMIC may exist in ELF binaries that do not require a conventional
  external interpreter. This deliberate conservative rejection can therefore
  exclude some otherwise portable programs until explicit relocation support
  is implemented.

## Agent handoff

**Owner:** ChatGPT implementation session

**Claim:** bounded subtask of #29, separate from preflight PR #34.
**Base:** `main`.
**Owned paths:** `crates/warden-elf/src/lib.rs`,
`docs/elf-loader-dynamic-guard.md`,
`.github/workflows/elf-loader-guard.yml`.
**Out of scope:** `crates/warden-kernel/src/linux.rs`,
`user/personality.c`, `tools/linux-compat/`, and all project control
documents (respect PRs #24/#26/#34 and other agents).

**Acceptance:**
- [ ] Rust ELF crate regression and workspace-library unit tests pass.
- [ ] Bare-metal Warden boots in QEMU, and existing in-guest tests pass.
- [ ] Independent security review of program-header parsing.
- [ ] Windows/WSL2 target-host verification.

## Next prerequisite

Introduce a capability-scoped filesystem service and validated ELF
interpreter metadata, then a relocator and libc integration. Do not relax
this check just to get a dynamic executable past the parser.
