# Task claim: Linux ABI query fidelity

- **Owner:** ChatGPT session acting as implementation worker and GitHub PR publisher
- **Status:** implementation; awaiting independent security review and CI
- **Claimed on:** 2026-11-05
- **Base:** `feat/linux-network-container-isolation` (PR #24); **not independent of PR #24**
- **Files owned:** `crates/warden-kernel/src/linux.rs`, `crates/warden-kernel/linux/client.c`,
  `crates/warden-kernel/linux/sys.h`, `crates/warden-kernel/src/tests.rs`, this claim,
  and `docs/agents/handoffs/linux-abi-fidelity.md`.
- **Explicitly out of scope:** `crates/warden-kernel/user/personality.c`, network quotas,
  loader/process lifetime, glibc/musl porting, graphics, changes to `CONTROL.md`/`STATUS.md`.

## Contract

Linux x86-64 syscall arguments must be interpreted as defined by their ABI.
Calls that return data through pointers must write validated output, not
pretend success with unchanged output. Unsound cryptographic randomness must
fail closed. Every addition has a kernel/QEMU CHECK with a named expected
result. Existing test suites must continue passing.

The broader goal is improved unchanged Linux application compatibility, but
this slice does not claim libc or dynamic loader compatibility.
