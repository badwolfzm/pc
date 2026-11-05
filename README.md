# Warden

Phases 0 and 1 of the Warden secure container OS.

## Phase 1: the kernel boots

`crates/warden-kernel` is a Rust x86-64 microkernel that boots under QEMU
(`-kernel`, Multiboot), switches to long mode, and runs ring-3 threads under
the `warden-caps` capability model. There are no device interrupts: the PIT
is the single partition timer, every other IRQ line is masked, and a thread
that spends its whole budget without yielding is killed. The syscall boundary
is `int 0x80` with `invoke(slot, ...)`-style calls; every syscall, even
logging, needs a capability.

```sh
rustup target add x86_64-unknown-none
tests/kernel/run.sh        # build, boot in QEMU, run the in-guest escape suite
```

The in-guest suite runs 13 hostile ring-3 threads: forged and out-of-range
slots, minting with a wider mask, using a revoked copy, over-budget retype,
host-exit without the host capability, writing and reading kernel memory,
jumping into kernel code, moving the stack outside the partition, `cli`,
`hlt`, `out`, `rdmsr`, `mov cr3`, an unprogrammed `int`, the `syscall`
instruction, and an infinite loop. The kernel then stops QEMU with its verdict.

Phase 2a adds **per-thread address spaces** (each thread gets private page
tables; the same user virtual addresses map to different frames) and
**synchronous IPC endpoints** (`send`/`recv` with four register words and the
sender's badge; `send` needs WRITE on the endpoint capability, `recv` needs
READ). Tests: a client's badge and words reach the server; a WRITE-only holder
cannot receive; a writer's data page is invisible to a reader at the same
address. 41 of 41 checks hold.

Not in the kernel yet: a loader, the Linux personality server, SMP, ARM64.
See the kernel plan.

See [HOST.md](HOST.md) for running everything on the Windows/WSL2 target host.

## Phase 0: the host prototype

Phase 0 is a **host-side prototype**,
not the kernel yet. It runs unmodified Linux programs under a syscall
supervisor that enforces a capability manifest and **per-library permission
checkboxes**, using the same capability model and syscall table the Warden
kernel will use in Phase 1.

What it is not: it is not a kernel, it does not boot, and it is not a
security boundary you should rely on. The real boundary is the Warden
microkernel inside a KVM micro-VM (Phase 1+). Phase 0 exists to settle the
permission model, the library approval flow and the Linux syscall boundary
against real programs before kernel code depends on them.

## Crates

| Crate | What it is | Moves into the kernel? |
|---|---|---|
| `warden-caps` | Capability model: rights, CSpaces, mint (attenuate only), revoke (whole subtree), untyped memory budgets, input cells. `no_std`, no `unsafe`. | Yes, as is |
| `warden-abi` | Every x86-64 Linux syscall (373) classified as pass, mediated, denied or unimplemented; exports the versioned ABI contract `abi/warden-abi.json`. | Yes, drives the personality server |
| `warden-libgrant` | Library requests, checkbox screen, approval records bound to SHA-256. | Yes, in the policy service |
| `warden-sentry` | `warden` CLI and the seccomp user-notification supervisor (host only). | No, replaced by the kernel |
| `warden-kernel` | Phase 1 microkernel (bare metal, `x86_64-unknown-none`). | It is the kernel |

## Use

```sh
cargo build --release
W=target/release/warden
$W init  --state st                    # starter manifest (fs.read /usr/lib ..., exec /usr/bin)
$W scan  --state st /usr/bin/ls        # queue every library ls needs
$W add-lib --state st ./libfoo.so --wants "net.connect=*:443"   # a downloaded library
$W approve --state st                  # tick boxes: one per library and permission
$W run   --state st -v -- ls -l /usr/lib
$W schema                              # ABI contract for the DSL workstream
```

The checkbox screen:

```
  libz.so.1  /usr/lib/x86_64-linux-gnu/libz.so.1.3
     sha256 f61677c7...
       2. [x] load this library
       3. [ ] net.connect=127.0.0.1:47311
```

Nothing is ticked by default. An approval binds to the exact SHA-256, so a
changed or swapped library is a new request. Ticked permissions on a library
join the process's authority only once that library is actually loaded.

## How enforcement works

- A seccomp-BPF filter, generated from `warden-abi`, lets `pass` syscalls
  through and sends everything else to the supervisor. Wrong arch (int 0x80)
  and x32 calls kill the process.
- **Opens** are performed by the supervisor with `openat2(RESOLVE_BENEATH)`
  from the granted root and the resulting fd is installed into the caller
  (`SECCOMP_IOCTL_NOTIF_ADDFD`). `..`, symlinks and races cannot leave a grant.
- **connect/bind** are performed by the supervisor on a duplicate of the
  caller's socket with its own copy of the address, so the address can't be
  swapped after the check.
- Libraries: any ELF shared object opened for reading (and the ELF
  interpreter at exec) must have an approval with the load box ticked.
- Every mediated decision goes to `audit.log`, with untrusted text escaped.

## Verified (run these)

```sh
cargo test                      # 21 unit tests across the crates
tests/kernel/run.sh             # 41 in-guest checks against the kernel
tests/escape/run.sh             # 44 attack checks from a hostile program, running as root
tests/compat/run.sh             # 13 checks with ls, sh, grep, sort, tar, sha256sum, python3
```

## ABI contract

`abi/warden-abi.json` (schema 0.2) is the shared contract between the kernel,
the DSL workstream and the MXSB native-app manifest. v0.2 added the native-app
permissions `render`, `superop=<name>`, `clock=coarse|precise` and `random`;
nothing from v0.1 changed. A test fails if the file and the table drift.

## Known Phase 0 limits

- Runs on the host kernel; a host kernel bug in a `pass` syscall is out of scope here.
- `stat`/`access`/`readlink`, create/delete/rename and `execve` are checked by
  path and then run by the kernel, so a racing thread can swap the path in
  between. For metadata calls the worst case is leaking metadata; for
  create/delete/rename and exec it is a real race. Phase 1 removes paths from
  the kernel interface entirely.
- `ioctl` passes except TIOCSTI/TIOCLINUX. Phase 1 mediates it per request.
- TCP only: no UDP, so no DNS; use IP literals.
- Per-library permissions are per process: inside one address space any code
  can use a permission that one approved library brought. Real per-library
  isolation needs compartments (Phase 3).
- x86-64 only.
