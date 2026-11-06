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
third sender on a full queue is refused with -7). 53 of 53 checks hold.

Phase 3 (first slice) adds **untyped memory**: every kernel object a thread
creates is paid for from the untyped capability it holds, and so is every
capability node that names such an object (64 bytes each). `retype(untyped,
kind, slot, size)` creates a Frame (4 KiB), an Endpoint (256 bytes, fixed
wait queues included), a Notification or a smaller Untyped; `map(frame, va)`
maps a frame into the caller's window (data only, NX); `delete(slot)` frees
the capability and, when it was the last one, the object; `budget(untyped)`
reads what is left. A refused request consumes nothing, and what is freed is
refunded and reused: the capability table keeps node and object arenas with
free lists and generation counters (a stale slot resolves to `Revoked`, never
to the new occupant), deleting an untyped takes every object it paid for, a
dead thread's capabilities are torn down with it, the kernel heap is now a
real size-class allocator that frees, and physical frames go back on a free
list. The in-guest suite exhausts a 3-frame budget, floods 10,000 refused
retypes, deletes and re-creates, maps a frame and reads it back, churns 1,000
endpoints, loses the mapping when the frame is deleted (#PF), and checks at
the end that 0 capabilities and 0 objects survive their holders, all user
frames came back, and the heap high-water mark grew by 0 bytes during the
run. In `warden-caps`, a counting allocator shows 10,000 rounds of every
operation (and every refused variant) make 0 heap allocations, and the
10,000-revocation test from PR #9 still passes (free lists are threaded
through the unused entries, so freeing never allocates). 108 of 108
checks hold. Not yet: threads and address spaces as retypable objects, frame
sharing between threads, unmap without delete.

### Phase 2b: two isolated applications, one workload

The kernel now **loads static ELF programs** and runs them beside a
**user-space Linux personality server**. The programs are ordinary C, built
from `crates/warden-kernel/user/` by the kernel's `build.rs` with the host C
compiler (`-ffreestanding -static -mgeneral-regs-only`) and embedded in the
kernel image:

- `personality.c`: the personality server. It receives one marshalled Linux
  syscall per `CALL`, identifies the caller by the **badge** the kernel put on
  the caller's capability (never by anything the caller wrote), and serves it
  from per-client state: `read`/`write`/`open`/`close` on a read-only in-memory
  site (`fs.read`), AF_INET stream sockets over an in-server loopback
  (`net.listen`, `net.connect`), `epoll`, `exit_group`. A call that cannot
  complete yet is parked as the client's one pending operation; the client
  stays blocked in the kernel meanwhile, so the server never blocks on a
  client and never spins.
- `httpd.c`: a static HTTP/1.0 server, epoll over a listening socket and its
  connections, files through the `fs.read` grant.
- `fetch.c`: an HTTP/1.0 client that makes 60 requests over fresh connections
  and checks every response byte for byte against the site fixture.
- `rogue.c`: a hostile third application that runs at the same time.

Kernel additions: `warden-elf` (a `no_std`, `unsafe`-free parser that
validates an image completely before a single frame is allocated: window,
page overlap, W^X, file bounds, entry in code), `loader.rs`, shared frames
(`map_frame`) for the per-client IPC buffers, `CALL` (send and wait for the
reply as one step, so a server can answer with the non-blocking `SEND_NB` and
can never be made to block on a client), and `CONSOLE_WRITE` (raw bytes
through the caller's own page table). Blocking in IPC refills the CPU budget
like a yield.

Verified under QEMU, 131 of 131 checks (23 new): httpd serves 60 requests and fetch
finds 60 of 60 responses correct while the rogue runs; the rogue's read of
httpd's descriptor is EBADF, its `open` and `socket` are EACCES (no grant),
a 1 MiB write from a 16 KiB buffer is EFAULT, 5,001 unknown syscalls are
ENOSYS with no kernel allocation, host-exit through either capability it
holds is refused, slots 4..0xFFFF name nothing, it gets 13 descriptors and
then EMFILE while the other apps keep running, and its final read of httpd's
buffer at the server's address faults (#PF). Five malformed images (segment
in kernel memory, segment reaching the IPC buffer, RWX segment, truncated,
foreign machine) are refused without allocating a frame. Under the Phase 3
rules the loader charges each program's image, stack, IPC buffer and page
tables to that program's untyped memory before it runs (httpd starts with
884 KiB of its 1 MiB left), the boot-time roots of the personality and reply
endpoints are deleted once the programs hold their copies, and the
end-of-run checks still find 0 live objects, every frame returned and a heap
high-water mark that grew by 0 bytes.

Honest limits: there is no NIC, TCP stack or VFS service yet; the loopback
and the site live in the personality server's own memory. The permission
table (which client has `fs.read`, `net.listen`, `net.connect`) lives in the
server until the policy service exists.

### Phase 2b, second slice: the Linux personality runs real static binaries

`crates/warden-kernel/src/linux.rs` lets a thread speak the **x86-64 Linux
ABI**: the `syscall` instruction (EFER.SCE, LSTAR; `int 0x80` stays the
native entry, and a thread that uses the other personality's entry is killed,
`WrongAbi`), Linux numbers and arguments, negative errno. The programs in
`crates/warden-kernel/linux/` are built with ordinary Linux flags (linked at
0x400000, SSE allowed, no libc) and loaded by `warden-elf` into an address
space of their own; the user range is now `0x400000..0x2000000` with a page
table per 2 MiB window created on first use, read-only data segments get a
real R-- mapping, and the thread's vector state (see "Vector state" below)
and FS base follow it across switches, so TLS and vector code work. The Linux
initial stack carries argc/argv/envp and an auxv (AT_PHDR, AT_PHNUM,
AT_PAGESZ, AT_ENTRY, AT_RANDOM, ...), what a static glibc or musl start
needs.

Every syscall number is looked up in the **Phase 0 table** (`warden-abi`,
now `no_std`), and its tier decides: `Pass` calls on the thread's own memory,
time and identity (brk, mmap, munmap, mprotect, arch_prctl, uname,
getrandom, clock_gettime, nanosleep, exit_group, ...) are served in the
kernel, paid from the thread's untyped memory and refunded on munmap and
death; `Pass` calls on descriptors (read, write, writev, close, accept4,
epoll) and every `Mediated` call (open/openat, socket, bind, listen, connect)
are **forwarded to `user/personality.c`**, the same server with the same
protocol the native apps use: the kernel owns the client's 16 KiB buffer
(the Linux program never sees it, so the header cannot be raced), writes
the six-word header and the data it copied out of the caller, and CALLs the
personality endpoint through the thread's own capability (slot 1, badge =
client id); the server's SEND_NB on the reply endpoint lands as the syscall's
result in rax, with read data or epoll events copied back. `Denied` is EPERM
and `Unimplemented` is ENOSYS, both logged. A Linux thread that dies without
exit_group (fault, wrong ABI) still releases its descriptors: the kernel
posts the exit_group to the server for it. The server needed only three new
manifest rows and two aliases (openat, accept4).

Verified under QEMU, 182 of 182 checks (51 new): a Linux httpd listens on
8081 and serves a Linux client two requests over the server's loopback (200
with the right body length, 404 for a missing file) alongside the native
pair on 8080 (the server counts 62 connections and 6 exits); the client's own
openat is EACCES, uname says Linux x86_64, brk and mmap give usable zeroed
memory and munmap gives the range back; the hostile Linux program gets EPERM
for ptrace and bpf (denied tier), EACCES for open and socket without grants,
EFAULT for a write from kernel memory and for a 1 MiB write from a 64-byte
buffer, ENOMEM for 64 MiB from a 1 MiB budget, EACCES for an RWX mprotect,
EINVAL for MAP_FIXED over the kernel, EBADF for descriptors it never
opened, ENOSYS 5,000 times without dying, and is killed the moment it tries
`int 0x80`. The mm self-test now has 25 checks (W^X bits per page kind, user
copies refused into read-only or unmapped pages, teardown returns every
page), `warden-caps` gained `pay` (charge untyped memory without creating an
object) with a unit test, and the end-of-run checks still hold: 0 live
objects, every frame returned, 0 fresh frames beyond the Linux heap pages,
heap high-water mark grew by 0 bytes.

**A real libc run: BusyBox.** `crates/warden-kernel/linux/busybox/` holds
an unmodified static musl BusyBox 1.36.1 (158 KiB, built from the upstream
tarball by its `build.sh`; config and hashes in the folder). The kernel
starts it four times with different argv and grants, as it would any Linux
program, and judges the exact bytes each run writes: `echo hello from
busybox`; `cat /site/style.css` through the personality server's fs.read
grant, byte for byte; `sh -c 'echo $((6*7)); printf ...; echo ${x#war}'`
(ash: arithmetic, the printf builtin, parameter expansion); and `cat` of the
same file without the grant, which prints `Permission denied` and exits 1.
musl's start code runs as is: auxv, `arch_prctl(ARCH_SET_FS)`, brk/mmap
malloc, `rt_sigaction`, `fcntl(F_GETFL)`, `ioctl(TIOCGWINSZ)` (ENOTTY),
writev. 191 of 191 checks hold; the heap and frame invariants are unchanged.

Not in the kernel yet: fs.write, signals (installed, never delivered),
fork/clone/execve (so no pipelines or external commands from the shell),
poll/select, the stat family, pipes, dup, file-backed mmap, dynamic loading,
the network container, SMP, ARM64; the frames a loaded image occupies are
charged but not yet returned when the program dies (they are not Frame
objects). See the kernel plan.

**Endpoint queues are priced into untyped memory, and badges are sticky.**
An endpoint's price is now `128 + 2 * capacity * 16` bytes, so its two wait
queues are paid for by whoever creates it, from the same untyped budget as
every other object: a `retype` of kind Endpoint takes the queue capacity as
its size (0 = the default 4, at most 8) and the kernel allocates exactly the
queue it charged for. The suite's own endpoints (server, flood target,
revocation target) are created the same way at boot, paid from their
creators' untyped memory; a boot check shows an over-budget request is
refused with nothing allocated or charged, and that each creator paid exactly
its object, queues and minted copies while holders paid nothing. The kernel
records each endpoint's peak sender depth and refused calls when it reclaims
it and checks both at the end. In `warden-caps`, a badged capability can no
longer be re-badged by `mint`, even with GRANT: a copy keeps its badge and
only an unbadged capability can be given one (the test
`badged_caps_cannot_be_rebadged` the adversarial review left ignored now
runs). In-guest: the client copies its own endpoint capability and still
arrives as badge 42; the eavesdropper claims in its message words to be the
client and still arrives as badge 7. Measured under QEMU/TCG in a cloud
container, not yet on the target PC.

Not in the kernel yet: a loader, the Linux personality server, SMP, ARM64.
See the kernel plan.

### Vector state: SSE and AVX in ring 3 (graphics plan M1)

At boot the kernel enables XSAVE when the CPU has it: CR4.OSXSAVE, and XCR0
set to x87 | SSE | AVX (no AVX-512 state, ever). Each thread carries a
1 KiB, 64-byte-aligned area that holds the XSAVE image (832 bytes for x87,
SSE and AVX). On a CPU without XSAVE the kernel falls back to FXSAVE (SSE
only). The boot log names the mode in use.

State moves **lazily**. Only the thread that owns the FPU runs with CR0.TS
clear. Any other thread's first vector instruction raises #NM, and then:

- **Allowed threads** take over the FPU. These are Linux threads and native
  threads marked `simd`, such as the render service and games that use
  super-ops. The owner's state is saved into its own area with XSAVE, the new
  thread's state is loaded with XRSTOR using the full enabled mask, and the
  instruction is retried. XRSTOR resets every component that is absent from
  the image, so no register of the previous owner stays visible.
- **Any other native thread** is still killed by #NM. The "sse from native
  thread" check keeps its meaning.

A thread that never touches a vector register never costs a save or a
restore. A dead owner's registers are never saved again, and the next owner
overwrites them.

Verified with `tests/kernel/run.sh`, 189 of 189 checks (6 new), run two ways:

- with the default `qemu64` CPU, on the FXSAVE path;
- with `QEMU_CPU=max tests/kernel/run.sh`, on the XSAVE x87|SSE|AVX path,
  with a 832-byte area.

The new checks:

- **Two keepers.** Two `simd` threads load different patterns into
  xmm0..15 (ymm0..15 when AVX is usable), then yield and spin across timer
  ticks four times while the other runs. Each gets back exactly its own
  256 or 512 bytes.
- **Snoop.** A fresh `simd` thread that only reads its registers sees all
  zeros, never a keeper's pattern.

A mutant kernel that skips the owner's save fails both keeper checks
(512 bytes wrong each). Pong (`tests/kernel/pong.sh`) still passes,
15 of 15. Run in the cloud container with QEMU 8.2 TCG; not run under KVM.

The `simd` flag is a spawn-time attribute set by whoever builds the thread,
like the personality. It is not yet a capability or a manifest permission;
the loader will set it from the manifest.

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
| `warden-caps` | Capability model: rights, CSpaces, mint (attenuate only), revoke (whole subtree), untyped memory with every object and capability node paid for and refunded on free, input cells. `no_std`, no `unsafe`. | Yes, as is |
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
cargo test                      # 43 unit tests across the crates
tests/kernel/run.sh             # 196 in-guest checks against the kernel
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
