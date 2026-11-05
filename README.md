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
address. A review of commit bd1e914 found that `SYS_HOST_EXIT` accepted any
WRITE capability (the console would have stopped the machine) and that
refused mints still allocated; both are fixed and tested: host-exit needs the
`Host` object, refused mints and roots allocate nothing, a 5,000-mint flood
leaves the capability table unchanged, and the kernel heap may grow by at most
4 KiB during the whole run.

A third review (of db2841c) found three more holes, all closed and tested:
`map_user` allocated a frame before checking the address (now it checks
first; a boot-time self-test shows refused mappings allocate nothing), user
code was mapped writable (now **W^X**: code RX, data and stack NX; writing
one's own code, executing the data page and executing the stack all fault),
and endpoint wait queues grew on demand (now each endpoint's queues have a
**fixed capacity allocated at creation**; the IPC path never allocates and a
third sender on a full queue is refused with -7). The user's status
assessment then pointed out that `revoke()` allocated a temporary vector per
call on a heap that never frees; it is now allocation-free (a node whose
parent is dead is dead, so no scratch bitmap is needed), proven by a
counting-allocator test of 10,000 revocations in `warden-caps` and by 10,000
in-guest revocations inside the 4 KiB heap bound. An adversarial suite then
uses every capability wrongly (the untyped cap as an endpoint, the dir cap as
memory, ALL rights on the wrong object for host-exit, a derived untyped copy
to spend the parent's budget, a revoked endpoint copy mid-flight) and floods
the kernel with 5,000 refused retypes, revokes, sends, recvs, invokes, mints
and host-exits, then overruns the stack and the data page. 83 of 83 checks
hold; the heap grows 32 bytes during the run.

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
  `O_PATH` opens get a read-only fd to the same pinned inode instead (the
  kernel refuses to install `O_PATH` fds); `O_PATH` on a symlink is `ELOOP`.
- **Metadata** (`stat`, `statx`, `access`, `readlink`, `statfs`, xattrs) is
  read by the supervisor through a pinned `O_PATH` fd and the result is
  copied into the caller's buffer; the kernel never re-reads the path.
- **Create, delete, rename, link, symlink, chmod, truncate** are performed
  by the supervisor as `*at` calls on a pinned parent directory under a write
  grant, with the caller's umask. A hard link needs its source under a write
  grant too.
- **exec** is checked on a pinned fd (grant, `#!` line, ELF interpreter), then
  the kernel runs it; right after, the supervisor verifies the new image is the
  checked inode and that every file-backed mapping lies under a grant, and
  SIGKILLs it otherwise. The check runs at the task's next mediated syscall or
  within 5 ms, whichever comes first.
- **connect/bind** are performed by the supervisor on a duplicate of the
  caller's socket with its own copy of the address, so the address can't be
  swapped after the check.
- **Library grants are per process**: a permission a ticked box brought
  applies only to a process that has that library mapped, for as long as it
  stays mapped (read from `/proc/PID/maps` at check time). A child that never
  loads the library, or a process that `exec`s, does not have it.
- Libraries: any ELF shared object opened for reading (and the ELF
  interpreter at exec) must have an approval with the load box ticked.
- Every mediated decision goes to `audit.log`, with untrusted text escaped.

## Verified (run these)

```sh
cargo test                      # 26 unit tests across the crates
tests/kernel/run.sh             # 83 in-guest checks against the kernel
tests/escape/run.sh             # 67 attack checks: hostile program (58) plus the harness's
                                #   inspection of a directory outside every grant (9)
tests/compat/run.sh             # 14 checks with ls, sh, grep, sort, tar, sha256sum, python3
```

## ABI contract

`abi/warden-abi.json` (schema 0.2) is the shared contract between the kernel,
the DSL workstream and the MXSB native-app manifest. v0.2 added the native-app
permissions `render`, `superop=<name>`, `clock=coarse|precise` and `random`;
nothing from v0.1 changed. A test fails if the file and the table drift.

## Known Phase 0 limits

- Runs on the host kernel; a host kernel bug in a `pass` syscall is out of scope here.
- `chdir` is the one path call the kernel still re-reads after the check. A
  cwd outside every grant conveys nothing (relative paths are re-resolved
  from `/proc/PID/cwd` and re-checked), but the kernel's answer tells the
  caller whether the swapped-in directory exists.
- `execve` cannot be done on the caller's behalf, so a swapped-in image can
  run until the supervisor's post-exec check: its first mediated syscall or
  about 5 ms. In that window it has only `pass` syscalls (its own fds, memory,
  time). An exec-only (mode 0111) file outside the exec grant could leak its
  bytes that way. The escape suite swaps a `#!/bin/sh` script in 300 times:
  every loaded swap was killed, none ran to completion.
- A library-brought grant is recognized by the library's (real path, inode)
  in `/proc/PID/maps`. If the sandbox also holds a write grant over the
  library's directory it could change the file after approval; keep library
  directories read-only. Phase 1 removes paths from the kernel interface
  entirely.
- `ioctl` passes except TIOCSTI/TIOCLINUX. Phase 1 mediates it per request.
- TCP only: no UDP, so no DNS; use IP literals.
- Per-library permissions are per process: inside one address space any code
  can use a permission that one approved library brought (by calling into
  it, or by issuing the syscall itself while the library is mapped). Real
  per-library isolation needs compartments (Phase 3).
- x86-64 only.
