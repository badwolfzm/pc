# Warden Linux dynamic metadata and relative relocation parser

Task: unclaimed loader prerequisite from issue #29.
Owner: ChatGPT agent session, November 5, 2026.
Branch: `feat/warden-ldso-metadata`, base `main` at
`5d84eb28660a3536f37068debcd75c6b8d851f84`.

## Scope

This new self-contained Rust `no_std` crate parses ELF64 little-endian
x86-64 ET_DYN/ET_EXEC program headers, including PT_INTERP, PT_DYNAMIC,
DT_NEEDED, DT_STRTAB, DT_STRSZ and RELA relocation tables.

- No heap allocation, unsafe code, executable loading, relocation writes
  or host filesystem access.
- Strict header bounds, no more than 64 program headers, 16 dependencies,
  or 64 relocation records. Refuse overlapping PT_LOAD pages, W+X load
  segments, and PT_DYNAMIC pointers not backed by the matching PT_LOAD bytes.
- All inspected string/relocation virtual addresses must map into the
  file-backed portion of an ELF PT_LOAD segment.
- Only R_X86_64_RELATIVE relocations with symbol index zero are recognized.
  Targets must fall in writable PT_LOAD memory. No text relocations.
- Library names are validated as simple names without slash/backslash or
  controls. Capability-scoped library lookup is **not** provided.
- Unknown dynamic tags, PLT, classic REL and other unsupported behavior
  fail closed. This means many ordinary Linux executables currently reject.

## Evidence and explicit limits

Verified in GitHub-hosted CI run [42014039451](https://github.com/badwolfzm/pc/actions/runs/42014039451): **10 Rust tests passed, 0 failed**, including 10,000 deterministic malformed-input mutations; **Clippy -D warnings passed**; **x86_64-unknown-none no_std cargo check passed**. This is parser-level evidence, not a kernel boot or execution test.

**This does not implement a dynamic linker or run Linux software.**
No library loading, per-process address-space assembly, relocation
application, TLS, versioned symbols, GLOB_DAT, JUMP_SLOT, GNU hash,
symbol binding, libc, `fork`, mount namespaces, or security
capability grants are implemented. ET_DYN support requires independent
review of load addresses, RELRO, DT_FLAGS, and relocation provenance,
followed by integration tests inside Warden/QEMU.

No changes to current `warden-elf` static loader are made; its
fail-closed rejection of PT_DYNAMIC/PT_INTERP remains in force.

## Coordination / handoff

Exclusive files: `crates/warden-ldso/**`,
`.github/workflows/warden-ldso.yml`, this document. PRs #54 (epoll),
#51 (corpus), #50 (time), #48 (AT_RANDOM), #47 (network), #46 (agent
coordination), and #45 (process planning) are not modified.

Keep this PR draft until CI verifies and a separate agent reviews the
parser's bounds arithmetic, relocation target scope, and fail-closed
rejection. Building the parser is a prerequisite, **not proof of
compatibility**.
