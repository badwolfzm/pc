//! The Linux personality: real static Linux binaries as Warden threads.
//!
//! A Linux thread enters the kernel with the `syscall` instruction and the
//! x86-64 Linux ABI (rax = number, rdi rsi rdx r10 r8 r9 = arguments, rax =
//! result, negative errno on failure). Every number is looked up in the
//! Phase 0 table (`warden-abi`), and its tier decides what happens:
//!
//! - `Pass`: touches only the caller's own state. Memory, time and identity
//!   calls are served here, in the kernel, against the thread's own address
//!   space. Calls on file descriptors (read, write, close, accept, epoll) are
//!   forwarded to the personality server, which owns the caller's descriptor
//!   table, without any policy question.
//! - `Mediated`: names something outside the caller (a path, a port). The
//!   kernel copies the arguments out of the caller's memory and forwards them
//!   to the personality server through the thread's personality capability;
//!   the server applies the manifest and answers. Without that capability
//!   the call fails with EPERM.
//! - `Denied`: an escape primitive. EPERM, logged, as in Phase 0.
//! - `Unimplemented`: ENOSYS, logged.
//!
//! Forwarding uses the same protocol as the native Warden-ABI applications
//! in `user/`: a 16 KiB buffer the server sees at `SRV_BUF_BASE + i*BUF_SIZE`
//! with a 64-byte header of six arguments and the data after it, one CALL on
//! the personality endpoint carrying (nr, a0, a1, a2) and the caller's badge,
//! and the server's SEND_NB on the caller's reply endpoint. The difference
//! is that a Linux program never sees its buffer: the kernel owns the frames
//! and marshals on the program's behalf, so the header cannot be raced. A
//! Linux thread blocked on its reply is exactly a native client blocked in
//! WSYS_CALL; the server cannot tell them apart and needs no change for the
//! protocol, only new manifest rows.
//!
//! Memory a Linux program obtains (its image, stack, brk, mmap) is paid for
//! from its Untyped capability; brk and mmap pages go back to the frame
//! pool when they are unmapped or the thread dies. A Linux thread that dies
//! without exit_group (a fault, the wrong ABI) still releases its descriptors:
//! the kernel posts the exit_group to the server for it.
//!
//! Not built: signals (rt_sigaction and friends return 0 and nothing is ever
//! delivered), fork/clone/execve, fs.write, poll/select, real time (the clock
//! is the partition timer), file-backed mmap.

use crate::apps::{BUF_SIZE, SRV_BUF_BASE};
use crate::loader;
use crate::mm::{Perm, PAGE, USER_END};
use crate::thread::{self, After, Death, Kernel, Msg, Personality, TrapFrame};
use warden_abi::{classify, name_of, Class};
use warden_caps::{CSpace, ObjectKind, Rights};

/// The fake vector `syscall_entry` (boot.asm) puts in the trap frame.
pub const VEC_LINUX: u64 = 256;

/// Where a Linux image may live: the link address of a non-PIE static
/// binary up to the heap limit. The heap (brk) grows from the image end.
pub const IMAGE_LO: u64 = 0x40_0000;
pub const IMAGE_HI: u64 = 0x100_0000;
pub const STACK_PAGES: u64 = 16;
pub const STACK_TOP: u64 = USER_END;
/// mmap grows down from here; 1 MiB guard under the stack.
const MMAP_TOP: u64 = STACK_TOP - STACK_PAGES * PAGE - 0x10_0000;
const MMAP_MAX: u64 = 16 << 20;

/// Slots, as in `user/warden_abi.h`.
pub const SLOT_PERSONALITY: usize = 1;
pub const SLOT_REPLY: usize = 2;
pub const SLOT_UNTYPED: usize = 3;
const SRV_SLOT_REPLY0: usize = 4;
const BUF_DATA_OFF: u64 = 64;
const BUF_DATA_MAX: u64 = BUF_SIZE - BUF_DATA_OFF;
const PATH_MAX: usize = 256;
/// Untyped memory a Linux program starts with; what the loader took for its
/// image, stack, buffer and page tables is charged before it runs.
const PROGRAM_UNTYPED: u64 = 1 << 20;
/// The first badge after the native applications (1..3).
pub const FIRST_BADGE: u64 = 4;
pub const N_APPS: usize = 3;

const EPERM: i64 = 1;
const EBADF: i64 = 9;
const EAGAIN: i64 = 11;
const ENOMEM: i64 = 12;
const EACCES: i64 = 13;
const EFAULT: i64 = 14;
const EEXIST: i64 = 17;
const EINVAL: i64 = 22;
const ENOTTY: i64 = 25;
const ENOSYS: i64 = 38;
const ENAMETOOLONG: i64 = 36;
const ERANGE: i64 = 34;

fn e(n: i64) -> u64 {
    (-n) as u64
}

const LINUX_HTTPD: &[u8] = include_bytes!(concat!(env!("OUT_DIR"), "/linux_httpd.elf"));
const LINUX_CLIENT: &[u8] = include_bytes!(concat!(env!("OUT_DIR"), "/linux_client.elf"));
const LINUX_HOSTILE: &[u8] = include_bytes!(concat!(env!("OUT_DIR"), "/linux_hostile.elf"));

struct App {
    name: &'static str,
    image: &'static [u8],
    expect: Death,
}

/// Badges 4, 5, 6; the server's manifest names them lhttpd, lfetch, lrogue.
const APPS: &[App] = &[
    App { name: "linux httpd", image: LINUX_HTTPD, expect: Death::Exited(0) },
    App { name: "linux client", image: LINUX_CLIENT, expect: Death::Exited(0) },
    App { name: "linux hostile", image: LINUX_HOSTILE, expect: Death::WrongAbi },
];

/// What to do with the server's answer to the call in flight.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Pending {
    None,
    /// The result word is the answer.
    Plain,
    /// On success, copy that many bytes from the buffer to user memory.
    ReadInto { buf: u64, max: u64 },
    /// On success, the result is a count of 12-byte epoll events to copy.
    EventsInto { buf: u64, max: u64 },
}

/// Per-thread Linux state. Plain data, no heap: created at boot.
#[derive(Clone, Copy)]
pub struct Proc {
    /// The client id the personality server sees.
    pub badge: u64,
    /// Physical address of the 16 KiB IPC buffer. Mapped in the server only.
    pub buf: u64,
    pub brk_start: u64,
    pub brk: u64,
    mmap_next: u64,
    pub pending: Pending,
    sleep_until: Option<u64>,
    /// The server has been told this process is gone.
    pub exited: bool,
    /// Refusals logged so far; a flood of bad syscalls is reported once.
    noise: u32,
}

impl Proc {
    pub const fn new() -> Self {
        Proc {
            badge: 0, buf: 0, brk_start: 0, brk: 0, mmap_next: MMAP_TOP, pending: Pending::None, sleep_until: None,
            exited: false, noise: 0,
        }
    }
}

// ---- setup -----------------------------------------------------------------

/// Load the Linux programs and wire them to the personality server `srv`:
/// the same capabilities a native application gets (slot 1 WRITE on the
/// personality endpoint with a badge, slot 2 READ on a reply endpoint, slot
/// 3 untyped), a buffer the server sees at its client index, and a Linux
/// initial stack. Roots live in `boot`, which the caller deletes afterwards.
pub fn setup(k: &mut Kernel, boot: &mut CSpace, srv: usize) {
    for (i, app) in APPS.iter().enumerate() {
        let badge = FIRST_BADGE + i as u64;
        let ci = (badge - 1) as usize; // the server's client index
        let before = k.frames.used;
        let mut a = crate::mm::AddressSpace::new(&mut k.frames).expect("page tables");
        let placed = loader::load_into(&mut k.frames, &mut a, app.image, IMAGE_LO, IMAGE_HI).expect("linux image");
        for p in 0..STACK_PAGES {
            a.map_user(&mut k.frames, STACK_TOP - (p + 1) * PAGE, Perm::Rw).expect("linux stack");
        }
        // The IPC buffer: kernel frames, visible to the server only.
        let buf = k.frames.alloc().expect("linux buffer");
        for p in 1..BUF_SIZE / PAGE {
            let f = k.frames.alloc().expect("linux buffer");
            assert!(f == buf + p * PAGE, "buffer frames must be contiguous at boot");
        }
        for p in 0..BUF_SIZE / PAGE {
            k.threads[srv].aspace.map_frame(buf + p * PAGE, SRV_BUF_BASE + ci as u64 * BUF_SIZE + p * PAGE, Perm::Rw).expect("server view of buffer");
        }
        let sp = init_stack(&a, app.name.as_bytes(), &placed, k.ticks).expect("initial stack");
        let t = k.spawn(app.name, placed.entry, sp, 50, app.expect, a);
        k.threads[t].personality = Personality::Linux;
        k.threads[t].linux = Proc { badge, buf, brk_start: placed.end, brk: placed.end, ..Proc::new() };
        // Reply endpoint: root in the boot CSpace, READ to the program, WRITE to the server.
        let rep = k.caps.create_root(boot, 1 + ci, ObjectKind::Endpoint, Rights::ALL, 0).expect("root");
        k.endpoints.insert(rep.0, thread::Endpoint::with_capacity(2));
        thread::grant(k, boot, 1 + ci, t, SLOT_REPLY, Rights::READ, 0);
        thread::grant(k, boot, 1 + ci, srv, SRV_SLOT_REPLY0 + ci, Rights::WRITE, 0);
        thread::grant(k, boot, 0, t, SLOT_PERSONALITY, Rights::WRITE, badge);
        // Phase 3 rule: memory is paid for. Everything the loader just took
        // (image, stack, buffer, page tables) comes out of the untyped.
        let charge = (k.frames.used - before) * PAGE;
        thread::root_cap(k, t, SLOT_UNTYPED, ObjectKind::Untyped, Rights::ALL, PROGRAM_UNTYPED - charge);
        crate::kprintln!("loaded {:<13} {} KiB Linux image, {} pages, entry {:#x}, brk {:#x}, badge {}, {} KiB charged to its untyped, {} KiB left",
            app.name, app.image.len() / 1024, placed.pages, placed.entry, placed.end, badge, charge / 1024, (PROGRAM_UNTYPED - charge) / 1024);
    }
}

/// Build the Linux initial stack: argc, argv, envp, auxv, AT_RANDOM bytes.
fn init_stack(a: &crate::mm::AddressSpace, argv0: &[u8], placed: &loader::Placed, ticks: u64) -> Option<u64> {
    let mut sp = STACK_TOP;
    sp -= 16;
    let random_at = sp;
    let mut rnd = [0u8; 16];
    for (i, b) in rnd.iter_mut().enumerate() {
        *b = (ticks as u8).wrapping_mul(31).wrapping_add(i as u8 * 7).wrapping_add(argv0.len() as u8);
    }
    if !a.copy_to_user(random_at, &rnd) {
        return None;
    }
    sp -= (argv0.len() + 1 + 15) as u64 & !15;
    let argv0_at = sp;
    if !a.copy_to_user(argv0_at, argv0) || !a.copy_to_user(argv0_at + argv0.len() as u64, &[0]) {
        return None;
    }
    let aux: [(u64, u64); 10] = [
        (3, placed.phdr),  // AT_PHDR
        (4, 56),           // AT_PHENT
        (5, placed.phnum), // AT_PHNUM
        (6, PAGE),         // AT_PAGESZ
        (9, placed.entry), // AT_ENTRY
        (11, 1000),        // AT_UID
        (12, 1000),        // AT_EUID
        (23, 0),           // AT_SECURE
        (25, random_at),   // AT_RANDOM
        (0, 0),            // AT_NULL
    ];
    let words = 1 + 1 + 1 + 1 + aux.len() * 2;
    sp -= (words * 8) as u64;
    sp &= !15;
    let mut off = sp;
    let mut put = |v: u64| -> bool {
        let ok = a.copy_to_user(off, &v.to_le_bytes());
        off += 8;
        ok
    };
    if !(put(1) && put(argv0_at) && put(0) && put(0)) {
        return None;
    }
    for (kx, v) in aux {
        if !(put(kx) && put(v)) {
            return None;
        }
    }
    Some(sp)
}

// ---- dispatch ----------------------------------------------------------------

pub fn syscall(k: &mut Kernel, f: &TrapFrame) -> Result<After, Death> {
    let cur = k.current;
    let nr = f.rax as u32;
    let name = name_of(nr).unwrap_or("?");
    let a = [f.rdi, f.rsi, f.rdx, f.r10, f.r8, f.r9];
    match classify(name) {
        Class::Denied => {
            refused(k, cur, format_args!("{name}: denied by the ABI table (EPERM)"));
            Ok(After::Resume(e(EPERM)))
        }
        Class::Unimplemented => {
            refused(k, cur, format_args!("syscall {nr} ({name}): not implemented (ENOSYS)"));
            Ok(After::Resume(e(ENOSYS)))
        }
        Class::Pass => pass(k, cur, nr, name, &a),
        Class::Mediated(_) => mediated(k, cur, nr, name, &a),
    }
}

/// Log a refusal, the first few times per thread; a program that floods the
/// kernel with bad syscalls does not get to flood the console.
fn refused(k: &mut Kernel, cur: usize, what: core::fmt::Arguments) {
    const LIMIT: u32 = 12;
    let t = &mut k.threads[cur];
    t.linux.noise += 1;
    if t.linux.noise <= LIMIT {
        crate::kprintln!("  [{}] {what}", t.name);
    } else if t.linux.noise == LIMIT + 1 {
        crate::kprintln!("  [{}] further refusals not logged", t.name);
    }
}

fn not_built(k: &mut Kernel, cur: usize, tier: &str, name: &str) -> Result<After, Death> {
    refused(k, cur, format_args!("{name}: {tier} tier, not built in this kernel (ENOSYS)"));
    Ok(After::Resume(e(ENOSYS)))
}

fn ret(r: Result<u64, i64>) -> Result<After, Death> {
    Ok(After::Resume(match r {
        Ok(v) => v,
        Err(n) => e(n),
    }))
}

/// Pass tier: the kernel serves memory, time and identity itself and
/// forwards descriptor operations to the server, which owns the table.
fn pass(k: &mut Kernel, cur: usize, nr: u32, name: &str, a: &[u64; 6]) -> Result<After, Death> {
    match name {
        "read" | "recvfrom" => {
            let max = a[2].min(BUF_DATA_MAX);
            forward(k, cur, 0, [a[0], 0, max, 0, 0, 0], &[], Pending::ReadInto { buf: a[1], max })
        }
        "write" | "sendto" => write(k, cur, a[0], a[1], a[2]),
        "writev" => writev(k, cur, a[0], a[1], a[2]),
        "close" => forward(k, cur, nr, [a[0], 0, 0, 0, 0, 0], &[], Pending::Plain),
        "accept" | "accept4" => forward(k, cur, 43, [a[0], 0, 0, 0, 0, 0], &[], Pending::Plain),
        "epoll_create" | "epoll_create1" => forward(k, cur, 291, [a[0], 0, 0, 0, 0, 0], &[], Pending::Plain),
        "epoll_ctl" => {
            let mut ev = [0u8; 12];
            if a[3] != 0 && !k.threads[cur].aspace.copy_from_user(a[3], &mut ev) {
                return ret(Err(EFAULT));
            }
            forward(k, cur, nr, [a[0], a[1], a[2], 0, 0, 0], &ev, Pending::Plain)
        }
        "epoll_wait" | "epoll_pwait" => {
            let max = (a[2] as i64).clamp(0, (BUF_DATA_MAX / 12) as i64) as u64;
            if max == 0 {
                return ret(Err(EINVAL));
            }
            forward(k, cur, 232, [a[0], 0, max, a[3], 0, 0], &[], Pending::EventsInto { buf: a[1], max })
        }
        "setsockopt" => ret(Ok(0)), // options are advice; the loopback has none
        "brk" => ret(brk(k, cur, a[0])),
        "mmap" => ret(mmap(k, cur, a[0], a[1], a[2], a[3], a[4] as i64)),
        "munmap" => ret(munmap(k, cur, a[0], a[1])),
        "mprotect" => ret(mprotect(k, cur, a[0], a[1], a[2])),
        "madvise" | "msync" | "mlock" | "munlock" => ret(Ok(0)),
        "arch_prctl" => ret(arch_prctl(k, cur, a[0], a[1])),
        "set_tid_address" | "gettid" | "getpid" => ret(Ok(1000 + cur as u64)),
        "getppid" | "getpgrp" | "getpgid" | "getsid" => ret(Ok(1)),
        "getuid" | "geteuid" | "getgid" | "getegid" => ret(Ok(1000)),
        // Affinity and resource queries must fill the caller's output;
        // silently returning success leaves libc inspecting uninitialized data.
        "sched_getaffinity" => ret(sched_getaffinity(k, cur, a[1], a[2])),
        "getrlimit" => ret(getrlimit(k, cur, a[0], a[1])),
        "prlimit64" => ret(prlimit64(k, cur, a[0], a[1], a[2], a[3])),
        "clock_getres" => ret(clock_getres(k, cur, a[0], a[1])),
        "set_robust_list" | "sigaltstack" | "rt_sigaction" | "rt_sigprocmask" | "prctl" | "membarrier" => ret(Ok(0)),
        "umask" => ret(Ok(0o22)),
        "uname" => ret(uname(k, cur, a[0])),
        "exit" | "exit_group" => Err(Death::Exited(a[0] & 0xFF)),
        "sched_yield" => {
            let t = &mut k.threads[cur];
            t.budget = t.budget_period;
            Ok(After::Yield)
        }
        "getrandom" => ret(getrandom(k, cur, a[0], a[1])),
        "clock_gettime" => ret(clock_gettime(k, cur, a[1], false)),
        // x86-64 gettimeofday(tv, tz): tv is argument 0, not argument 1.
        "gettimeofday" => ret(gettimeofday(k, cur, a[0], a[1])),
        "nanosleep" | "clock_nanosleep" => nanosleep(k, cur, if name == "nanosleep" { a[0] } else { a[2] }),
        "ioctl" => ret(Err(ENOTTY)),
        "getcwd" => ret(getcwd(k, cur, a[0], a[1])),
        _ => not_built(k, cur, "pass", name),
    }
}

/// Mediated tier: a path or a port. The arguments are copied out of the
/// caller and handed to the server, which holds the manifest.
fn mediated(k: &mut Kernel, cur: usize, nr: u32, name: &str, a: &[u64; 6]) -> Result<After, Death> {
    match name {
        "open" | "openat" => {
            let (path, flags) = if name == "open" { (a[0], a[1]) } else { (a[1], a[2]) };
            let mut p = [0u8; PATH_MAX];
            let n = match k.threads[cur].aspace.copy_str_from_user(path, &mut p) {
                Some(n) => n,
                None => return ret(Err(if k.threads[cur].aspace.is_mapped(path) { ENAMETOOLONG } else { EFAULT })),
            };
            // The server's open: a1 = flags, a2 = length, path in the data.
            forward(k, cur, 2, [0, flags, n as u64, 0, 0, 0], &p[..n + 1], Pending::Plain)
        }
        "socket" => forward(k, cur, nr, [a[0], a[1], a[2], 0, 0, 0], &[], Pending::Plain),
        "bind" | "connect" => {
            if a[2] != 16 {
                return ret(Err(EINVAL));
            }
            let mut sa = [0u8; 16];
            if !k.threads[cur].aspace.copy_from_user(a[1], &mut sa) {
                return ret(Err(EFAULT));
            }
            forward(k, cur, nr, [a[0], 0, 16, 0, 0, 0], &sa, Pending::Plain)
        }
        "listen" => forward(k, cur, nr, [a[0], a[1], 0, 0, 0, 0], &[], Pending::Plain),
        _ => not_built(k, cur, "mediated", name),
    }
}

// ---- forwarding to the personality server ----------------------------------

/// Marshal one call into the thread's buffer and CALL the personality
/// endpoint through the thread's own capability (slot 1, WRITE), waiting on
/// its reply endpoint (slot 2, READ). The server sees the caller's badge,
/// (nr, a0, a1, a2) in the message and the full header in the buffer.
fn forward(k: &mut Kernel, cur: usize, nr: u32, hdr: [u64; 6], data: &[u8], pending: Pending) -> Result<After, Death> {
    let cs = &k.threads[cur].cspace;
    let ep = match k.caps.invoke(cs, SLOT_PERSONALITY, Rights::WRITE) {
        Ok(r) if r.kind == ObjectKind::Endpoint => r,
        _ => return ret(Err(EPERM)),
    };
    let rep = match k.caps.invoke(cs, SLOT_REPLY, Rights::READ) {
        Ok(r) if r.kind == ObjectKind::Endpoint => r.object.0,
        _ => return ret(Err(EPERM)),
    };
    debug_assert!(data.len() as u64 <= BUF_DATA_MAX);
    let buf = k.threads[cur].linux.buf;
    unsafe {
        for (i, w) in hdr.iter().enumerate() {
            *((buf as *mut u64).add(i)) = *w;
        }
        core::ptr::copy_nonoverlapping(data.as_ptr(), (buf + BUF_DATA_OFF) as *mut u8, data.len());
    }
    let msg = Msg { badge: ep.badge, w: [nr as u64, hdr[0], hdr[1], hdr[2]] };
    k.threads[cur].linux.pending = pending;
    if thread::call(k, cur, ep.object.0, rep, msg) {
        Ok(After::Block)
    } else {
        k.threads[cur].linux.pending = Pending::None;
        ret(Err(EAGAIN)) // the server's queue is full
    }
}

/// The server answered: the first word is the Linux result. Copy out what
/// the pending call asked for and resume the thread with rax = result.
/// Linux keeps every other register across a syscall, so nothing else in
/// the frame changes.
pub fn complete(k: &mut Kernel, t: usize, msg: Msg) {
    let result = msg.w[0];
    let pending = core::mem::replace(&mut k.threads[t].linux.pending, Pending::None);
    let buf = k.threads[t].linux.buf;
    let rax = match pending {
        Pending::ReadInto { buf: ubuf, max } if (result as i64) > 0 => {
            let n = result.min(max) as usize;
            let src = unsafe { core::slice::from_raw_parts((buf + BUF_DATA_OFF) as *const u8, n) };
            if k.threads[t].aspace.copy_to_user(ubuf, src) { n as u64 } else { e(EFAULT) }
        }
        Pending::EventsInto { buf: ubuf, max } if (result as i64) > 0 => {
            let n = result.min(max) as usize;
            let src = unsafe { core::slice::from_raw_parts((buf + BUF_DATA_OFF) as *const u8, n * 12) };
            if k.threads[t].aspace.copy_to_user(ubuf, src) { n as u64 } else { e(EFAULT) }
        }
        _ => result,
    };
    k.threads[t].frame.rax = rax;
}

/// write: the bytes are copied out of the caller once, here, and handed to
/// the server. Lines of the form `CHECK <id> <value>` on stdout or stderr
/// feed the in-guest suite's judge and are not forwarded; the rest goes to
/// the server, which prints it with the program's name.
fn write(k: &mut Kernel, cur: usize, fd: u64, ubuf: u64, len: u64) -> Result<After, Death> {
    let n = len.min(BUF_DATA_MAX) as usize;
    let mut data = [0u8; BUF_DATA_MAX as usize];
    if !k.threads[cur].aspace.copy_from_user(ubuf, &mut data[..n]) {
        return ret(Err(EFAULT));
    }
    write_bytes(k, cur, fd, &data[..n])
}

/// writev: gather the vector into one write.
fn writev(k: &mut Kernel, cur: usize, fd: u64, iov: u64, cnt: u64) -> Result<After, Death> {
    if cnt > 64 {
        return ret(Err(EINVAL));
    }
    let mut data = [0u8; BUF_DATA_MAX as usize];
    let mut n = 0usize;
    for i in 0..cnt {
        let mut ent = [0u8; 16];
        if !k.threads[cur].aspace.copy_from_user(iov + i * 16, &mut ent) {
            return ret(Err(EFAULT));
        }
        let base = u64::from_le_bytes(ent[..8].try_into().unwrap());
        let len = u64::from_le_bytes(ent[8..].try_into().unwrap()) as usize;
        let take = len.min(data.len() - n);
        if !k.threads[cur].aspace.copy_from_user(base, &mut data[n..n + take]) {
            return ret(Err(EFAULT));
        }
        n += take;
        if take < len {
            break;
        }
    }
    write_bytes(k, cur, fd, &data[..n])
}

fn write_bytes(k: &mut Kernel, cur: usize, fd: u64, bytes: &[u8]) -> Result<After, Death> {
    let mut data = [0u8; BUF_DATA_MAX as usize];
    data[..bytes.len()].copy_from_slice(bytes);
    let mut kept = 0usize;
    if fd == 1 || fd == 2 {
        let mut out = [0u8; BUF_DATA_MAX as usize];
        for line in data[..bytes.len()].split_inclusive(|&c| c == b'\n') {
            if let Some(rest) = line.strip_prefix(b"CHECK ") {
                let body = rest.strip_suffix(b"\n").unwrap_or(rest);
                let mut it = body.split(|&c| c == b' ');
                if let (Some(id), Some(val)) = (it.next().and_then(parse_i64), it.next().and_then(parse_i64)) {
                    k.threads[cur].logs += 1;
                    crate::tests::judge(k, id as u64, val as u64);
                    continue;
                }
            }
            out[kept..kept + line.len()].copy_from_slice(line);
            kept += line.len();
        }
        if kept == 0 {
            return ret(Ok(bytes.len() as u64));
        }
        data[..kept].copy_from_slice(&out[..kept]);
    } else {
        kept = bytes.len();
    }
    forward(k, cur, 1, [fd, 0, kept as u64, 0, 0, 0], &data[..kept], Pending::Plain)
}

fn parse_i64(s: &[u8]) -> Option<i64> {
    let (neg, digits) = match s.first() {
        Some(b'-') => (true, &s[1..]),
        _ => (false, s),
    };
    if digits.is_empty() || digits.len() > 18 {
        return None;
    }
    let mut v: i64 = 0;
    for &c in digits {
        if !c.is_ascii_digit() {
            return None;
        }
        v = v * 10 + (c - b'0') as i64;
    }
    Some(if neg { -v } else { v })
}

// ---- death -------------------------------------------------------------------

/// A Linux thread is dying. Its brk and mmap pages go back to the frame
/// pool (the image, stack and buffer were charged at boot and stay in the
/// baseline), and the server is told exit_group so it releases the
/// descriptors, unless the thread said so itself.
pub fn on_death(k: &mut Kernel, t: usize, how: Death) {
    let p = k.threads[t].linux;
    let mut freed = 0u64;
    let mut va = p.brk_start;
    while va < p.brk {
        if let Some(f) = k.threads[t].aspace.unmap(va) {
            k.frames.free(f);
            freed += 1;
        }
        va += PAGE;
    }
    let mut va = p.mmap_next;
    while va < MMAP_TOP {
        if let Some(f) = k.threads[t].aspace.unmap(va) {
            k.frames.free(f);
            freed += 1;
        }
        va += PAGE;
    }
    if freed > 0 {
        crate::kprintln!("  {} released {} heap/mmap frames", k.threads[t].name, freed);
    }
    if p.exited {
        return;
    }
    let code = match how {
        Death::Exited(c) => c,
        _ => 128,
    };
    let cs = &k.threads[t].cspace;
    let Ok(ep) = k.caps.invoke(cs, SLOT_PERSONALITY, Rights::WRITE) else { return };
    if ep.kind != ObjectKind::Endpoint {
        return;
    }
    unsafe {
        let buf = p.buf as *mut u64;
        *buf = code;
        for i in 1..6 {
            *buf.add(i) = 0;
        }
    }
    k.threads[t].linux.exited = true;
    let msg = Msg { badge: ep.badge, w: [231, code, 0, 0] };
    if !thread::notify(k, t, ep.object.0, msg) {
        crate::kprintln!("  {}: the personality server's queue is full; its exit goes unreported", k.threads[t].name);
    }
}

// ---- memory, time, identity ----------------------------------------------------

/// Pay `bytes` from the thread's Untyped capability.
fn charge(k: &mut Kernel, t: usize, bytes: u64) -> bool {
    k.caps.pay(&k.threads[t].cspace, SLOT_UNTYPED, bytes).is_ok()
}

fn map_pages(k: &mut Kernel, t: usize, va: u64, pages: u64, perm: Perm) -> Result<(), i64> {
    if pages == 0 {
        return Ok(());
    }
    // Everything must be free before anything is charged or mapped.
    for i in 0..pages {
        if k.threads[t].aspace.pte(va + i * PAGE).is_some() {
            return Err(EEXIST);
        }
    }
    if !charge(k, t, pages * PAGE) {
        return Err(ENOMEM);
    }
    for i in 0..pages {
        let th = &mut k.threads[t];
        if th.aspace.map_user(&mut k.frames, va + i * PAGE, perm).is_none() {
            return Err(ENOMEM);
        }
        k.linux_run_frames += 1;
    }
    Ok(())
}

fn perm_of(prot: u64) -> Result<Perm, i64> {
    const PROT_WRITE: u64 = 2;
    const PROT_EXEC: u64 = 4;
    if prot & PROT_WRITE != 0 && prot & PROT_EXEC != 0 {
        return Err(EACCES); // W^X
    }
    Ok(if prot & PROT_EXEC != 0 {
        Perm::Rx
    } else if prot & PROT_WRITE != 0 {
        Perm::Rw
    } else {
        Perm::Ro // PROT_NONE is approximated by read-only
    })
}

fn page_ceil(x: u64) -> Option<u64> {
    x.checked_add(PAGE - 1).map(|v| v & !(PAGE - 1))
}

fn brk(k: &mut Kernel, cur: usize, addr: u64) -> Result<u64, i64> {
    let p = k.threads[cur].linux;
    if addr == 0 || addr < p.brk_start {
        return Ok(p.brk);
    }
    let new_end = match page_ceil(addr) {
        Some(v) if v <= IMAGE_HI => v,
        _ => return Ok(p.brk), // Linux: brk reports the old break on failure
    };
    if new_end > p.brk {
        if map_pages(k, cur, p.brk, (new_end - p.brk) / PAGE, Perm::Rw).is_err() {
            return Ok(p.brk);
        }
    } else {
        let mut va = new_end;
        while va < p.brk {
            if let Some(f) = k.threads[cur].aspace.unmap(va) {
                k.frames.free(f);
            }
            va += PAGE;
        }
    }
    k.threads[cur].linux.brk = new_end;
    Ok(new_end)
}

fn mmap(k: &mut Kernel, cur: usize, addr: u64, len: u64, prot: u64, flags: u64, fd: i64) -> Result<u64, i64> {
    const MAP_PRIVATE: u64 = 0x02;
    const MAP_FIXED: u64 = 0x10;
    const MAP_ANONYMOUS: u64 = 0x20;
    if flags & MAP_FIXED != 0 {
        return Err(EINVAL); // the kernel chooses every address
    }
    if flags & MAP_ANONYMOUS == 0 || fd != -1 || flags & MAP_PRIVATE == 0 {
        return Err(ENOSYS); // file-backed and shared mappings: not built
    }
    let _ = addr; // hints are ignored
    if len == 0 || len > MMAP_MAX {
        return Err(if len == 0 { EINVAL } else { ENOMEM });
    }
    let perm = perm_of(prot)?;
    let len = page_ceil(len).ok_or(ENOMEM)?;
    let p = k.threads[cur].linux;
    let start = p.mmap_next.checked_sub(len).ok_or(ENOMEM)?;
    if start < IMAGE_HI {
        return Err(ENOMEM);
    }
    map_pages(k, cur, start, len / PAGE, perm)?;
    k.threads[cur].linux.mmap_next = start;
    Ok(start)
}

fn munmap(k: &mut Kernel, cur: usize, addr: u64, len: u64) -> Result<u64, i64> {
    if addr % PAGE != 0 || len == 0 {
        return Err(EINVAL);
    }
    let end = page_ceil(addr.checked_add(len).ok_or(EINVAL)?).ok_or(EINVAL)?;
    let p = k.threads[cur].linux;
    // Only mmap pages can be unmapped: the image, heap and stack stay.
    if addr < p.mmap_next || end > MMAP_TOP {
        return Err(EINVAL);
    }
    let mut va = addr;
    while va < end {
        if let Some(f) = k.threads[cur].aspace.unmap(va) {
            k.frames.free(f);
        }
        va += PAGE;
    }
    // The mmap area is a stack of mappings growing down; unmapping the
    // lowest one gives its range back. Holes in the middle are not reused.
    if addr == p.mmap_next {
        let mut top = end;
        while top < MMAP_TOP && k.threads[cur].aspace.pte(top).is_none() {
            top += PAGE;
        }
        k.threads[cur].linux.mmap_next = top;
    }
    Ok(0)
}

fn mprotect(k: &mut Kernel, cur: usize, addr: u64, len: u64, prot: u64) -> Result<u64, i64> {
    if addr % PAGE != 0 {
        return Err(EINVAL);
    }
    let perm = perm_of(prot)?;
    let end = page_ceil(addr.checked_add(len).ok_or(EINVAL)?).ok_or(EINVAL)?;
    if !k.threads[cur].aspace.mapped(addr, end - addr) {
        return Err(ENOMEM);
    }
    let mut va = addr;
    while va < end {
        k.threads[cur].aspace.protect(va, perm);
        va += PAGE;
    }
    Ok(0)
}

fn arch_prctl(k: &mut Kernel, cur: usize, code: u64, addr: u64) -> Result<u64, i64> {
    match code {
        0x1002 => {
            // ARCH_SET_FS: the base must be user memory (or 0). Nothing checks
            // what the program then reads through it: FS is its own.
            if addr != 0 && !(crate::mm::USER_BASE..USER_END).contains(&addr) {
                return Err(EPERM);
            }
            k.threads[cur].fs_base = addr;
            Ok(0)
        }
        0x1003 => {
            let v = k.threads[cur].fs_base.to_le_bytes();
            if k.threads[cur].aspace.copy_to_user(addr, &v) { Ok(0) } else { Err(EFAULT) }
        }
        _ => Err(EINVAL),
    }
}

fn uname(k: &mut Kernel, cur: usize, buf: u64) -> Result<u64, i64> {
    let mut b = [0u8; 65 * 6];
    for (i, s) in [&b"Linux"[..], b"warden", b"6.1.0-warden", b"Warden Phase 2b personality", b"x86_64", b""].iter().enumerate() {
        b[i * 65..i * 65 + s.len()].copy_from_slice(s);
    }
    if k.threads[cur].aspace.copy_to_user(buf, &b) { Ok(0) } else { Err(EFAULT) }
}

// Until Warden has an authenticated entropy service, do not return a
// predictable xorshift stream as Linux getrandom. Linux applications use
// this syscall for key material and address randomization. Fail closed.
fn getrandom(_k: &mut Kernel, _cur: usize, _buf: u64, n: u64) -> Result<u64, i64> {
    if n == 0 { Ok(0) } else { Err(ENOSYS) }
}

/// The only implemented limit queries reflect actual kernel/server bounds.
/// Do not pretend to implement unbounded resources not enforced by Warden.
fn resource_limit(resource: u64) -> Result<u64, i64> {
    match resource {
        3 => Ok(STACK_PAGES * PAGE), // RLIMIT_STACK: 64 KiB mapped stack
        7 => Ok(16), // RLIMIT_NOFILE: personality server NFD
        _ => Err(ENOSYS),
    }
}

fn getrlimit(k: &mut Kernel, cur: usize, resource: u64, out: u64) -> Result<u64, i64> {
    let n = resource_limit(resource)?;
    let mut b = [0u8; 16];
    b[..8].copy_from_slice(&n.to_le_bytes());
    b[8..].copy_from_slice(&n.to_le_bytes());
    if k.threads[cur].aspace.copy_to_user(out, &b) { Ok(0) } else { Err(EFAULT) }
}

fn prlimit64(k: &mut Kernel, cur: usize, pid: u64, resource: u64, new: u64, old: u64) -> Result<u64, i64> {
    let own = 1000 + cur as u64;
    if pid != 0 && pid != own { return Err(EPERM); }
    if new != 0 { return Err(EPERM); } // no mutable rlimits yet
    resource_limit(resource)?;
    if old == 0 { return Ok(0); }
    getrlimit(k, cur, resource, old)
}

fn sched_getaffinity(k: &mut Kernel, cur: usize, size: u64, mask: u64) -> Result<u64, i64> {
    if size < 8 { return Err(EINVAL); }
    // Single CPU, CPU 0. The Linux syscall returns the bytes written.
    if k.threads[cur].aspace.copy_to_user(mask, &1u64.to_le_bytes()) { Ok(8) } else { Err(EFAULT) }
}

fn clock_getres(k: &mut Kernel, cur: usize, id: u64, out: u64) -> Result<u64, i64> {
    // Partition timer granularity: 100Hz. Only supported virtual clocks.
    if id != 0 && id != 1 && id != 4 && id != 7 { return Err(EINVAL); }
    if out == 0 { return Ok(0); }
    let mut b = [0u8; 16];
    b[8..].copy_from_slice(&10_000_000u64.to_le_bytes());
    if k.threads[cur].aspace.copy_to_user(out, &b) { Ok(0) } else { Err(EFAULT) }
}

fn gettimeofday(k: &mut Kernel, cur: usize, tv: u64, tz: u64) -> Result<u64, i64> {
    if tv != 0 { clock_gettime(k, cur, tv, true)?; }
    // Linux's legacy timezone data: UTC, zero minutes west and DST.
    if tz != 0 && !k.threads[cur].aspace.copy_to_user(tz, &[0u8; 8]) {
        return Err(EFAULT);
    }
    Ok(0)
}

fn clock_gettime(k: &mut Kernel, cur: usize, ts: u64, micro: bool) -> Result<u64, i64> {
    let ms = k.ticks * 10;
    let mut b = [0u8; 16];
    b[..8].copy_from_slice(&(ms / 1000).to_le_bytes());
    let frac = ms % 1000;
    b[8..].copy_from_slice(&(if micro { frac * 1000 } else { frac * 1_000_000 }).to_le_bytes());
    if ts == 0 {
        return Ok(0);
    }
    if k.threads[cur].aspace.copy_to_user(ts, &b) { Ok(0) } else { Err(EFAULT) }
}

/// Sleep by restarting the instruction until the tick has passed: the
/// thread gives the CPU up each time, so it costs nobody else anything.
fn nanosleep(k: &mut Kernel, cur: usize, req: u64) -> Result<After, Death> {
    let until = match k.threads[cur].linux.sleep_until {
        Some(u) => u,
        None => {
            let mut b = [0u8; 16];
            if !k.threads[cur].aspace.copy_from_user(req, &mut b) {
                return ret(Err(EFAULT));
            }
            let s = u64::from_le_bytes(b[..8].try_into().unwrap());
            let ns = u64::from_le_bytes(b[8..].try_into().unwrap());
            if ns >= 1_000_000_000 {
                return ret(Err(EINVAL));
            }
            let ticks = s.saturating_mul(100).saturating_add(ns.div_ceil(10_000_000));
            let u = k.ticks + ticks;
            k.threads[cur].linux.sleep_until = Some(u);
            u
        }
    };
    if k.ticks >= until {
        k.threads[cur].linux.sleep_until = None;
        ret(Ok(0))
    } else {
        Ok(After::Restart)
    }
}

fn getcwd(k: &mut Kernel, cur: usize, buf: u64, n: u64) -> Result<u64, i64> {
    if n < 2 {
        return Err(ERANGE);
    }
    if k.threads[cur].aspace.copy_to_user(buf, b"/\0") { Ok(2) } else { Err(EFAULT) }
}

#[allow(dead_code)]
const _: i64 = EBADF;
