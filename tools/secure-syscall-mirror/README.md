# Secure syscall mirror — isolated reference slice

Task ID: secure-syscall-mirror-reference-20261106
Base: `235e1d1fece421d87305ebba0627a3f9d02bf6b3`
Owned paths: `tools/secure-syscall-mirror/**` and `.github/workflows/secure-syscall-mirror.yml`.
Overlap: no edits to claimed kernel/personality source or ABI contracts.
Status: proposed reference implementation; review required before integration.

## What this does

A fail-closed **reference** dispatcher for Linux x86-64 syscall requests, with
kernel-issued badges, fixed-size immutable argument copies, explicit read-only
filesystem grants, network grants and denial of escape operations. The route
decision is separate from the privileged implementation. Local syscalls are
explicitly refused by this userspace reference rather than falsely executing.

The existing Warden implementation already forwards system calls in
`crates/warden-kernel/src/linux.rs` and handles mediated operations in
`crates/warden-kernel/user/personality.c`. This slice does **not** alter or
replace those working paths. It must be reviewed by the kernel/personality
owner before adapting to the native Rust/C code and its actual ABI table.

Crucial trust rule: NEVER accept badge or grants from an untrusted request.
The kernel must populate both. The service can receive only copied, bounded
bytes, not a live user pointer. No new rights or syscalls are granted.

## Test

```sh
python3 -m unittest discover -s tools/secure-syscall-mirror -p 'test_*.py' -v
```

Tests cover successful routes, forged/mismatched badges, filesystem read-only
rules, invalid paths, embedded NUL, mutable buffers, oversize transfers,
network permissions, privileged syscall denial and local-call refusal.

## Limitations / next handoff

- Python reference model only: **not wired into Warden kernel or QEMU**.
- The list is proposed for integration, not evidence Warden implements every
  Linux syscall listed under LOCAL (notably futex).
- Not a performance benchmark, not full Linux compatibility, and does not
  run Doom or Albion. No assertion of end-to-end guest security.
- For integration: independently review the ABI numbers, marshal formats
  (especially open/openat), policy fidelity, and C/Rust boundary. Add
  native guest negative tests and measure syscall latency against baseline.
- Nothing from the guest is copied through a Python object in the real OS;
  its real kernel copy_from_user and capability checks remain authoritative.
