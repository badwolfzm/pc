# Linux compatibility compartment — report alignment and acceptance gates

Source documents: `control/CONTROL.md`, `control/STATUS.md`,
`control/contracts/runtime.md`, `HOST.md`, and `README.md`.

## Non-negotiable behavior from the project reports

1. **Escape resistance first.** Deny unsupported syscalls rather than
   implementing them by unsafe fallback; no silently broad privileges.
2. **Independent Warden kernel.** Linux calls use the x86-64 Linux ABI and
   are mediated through a capability endpoint to a ring-3 personality
   service. Linux is not the kernel.
3. **Kernel badges are authority.** Guest-provided data must never select
   its own capability set or namespace.
4. **Bounded resources.** No unbounded kernel IPC queues or uncontrolled
   allocation due to denied/invalid calls.
5. **Preserve existing workloads.** Both native and Linux static HTTP
   server/client pairs must keep working, as must existing tests.
6. **Measured claims only.** Host and QEMU test results must be reported
   separately. Acceptance on the target Windows/WSL2 computer remains open.

## This PR's concrete scope

- Native badges 1–3 map to NETNS_WARDEN; Linux badges 4–6 map to
  NETNS_LINUX, in the fixed personality-server manifest.
- Listener conflicts and connect lookup are scoped to that network identity.
- Tests exercise both namespace comparison and actual socket functions.
- GitHub CI runs Rust library tests and boots the kernel via QEMU TCG.

## Acceptance gates for merging

- [x] Namespace policy regression compiles and passes in GitHub CI (initial run).
- [x] Real server bind/connect integration regression passes in GitHub CI
      (initial run).
- [x] Full Rust host-compatible unit test run passes (GitHub Actions run 42006988389).
- [x] `tests/kernel/run.sh` passes in QEMU TCG (run 42006988389).
- [x] Test socket/listener/epoll compartment exhaustion and stream reclaim in the host integration harness.\n- [ ] Test all object teardown and resource contention under QEMU with hostile concurrent guests.
- [ ] Verify on target Windows/WSL2 computer.

## Security limitations (do not represent as solved)

- Both namespaces still run inside one user-space personality server, and
  share global listener/stream/epoll pools.
- Namespace assignment is selected from a kernel-issued badge but enforced
  by server code, **not by a separate in-kernel container object**.
- There is no complete PID/mount/filesystem namespace, process tree,
  dynamic linker, writable filesystem, or commercial application support.
- Cross-namespace port visibility is restricted; a compromised service,
  shared-capacity exhaustion, or bugs in the personality server remain risks.

## Next implementation slices

1. Per-namespace listener/stream quotas, exhaustive teardown, independent
   failure behavior, tests for resource exhaustion.
2. File service with read-only grants and safe path resolution.
3. Additional process primitives and stable scheduling semantics without
   introducing an unbounded kernel allocator.
4. Static musl/glibc bootstrap and real unmodified-userland compatibility
   test suite, then dynamic linking.
5. GPU/render/input capability services and game workloads only after
   confinement acceptance gates pass.
