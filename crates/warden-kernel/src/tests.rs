//! The in-guest escape suite. Every function in `.user` runs in ring 3 with
//! nothing but its CSpace. They use only registers and the user page: no
//! string constants, no calls into kernel code, no panics.
//!
//! Test ids reported through SYS_LOG and what the kernel expects back.

#![allow(clippy::empty_loop)]

use crate::thread::{Death, Kernel};
use core::arch::asm;

pub const USER_BASE: u64 = 0x80_0000;
pub const USER_END: u64 = 0xA0_0000;

macro_rules! sys {
    ($n:expr, $a:expr, $b:expr, $c:expr) => {{
        let r: u64;
        asm!("int 0x80", inlateout("rax") $n as u64 => r, in("rdi") $a as u64, in("rsi") $b as u64, in("rdx") $c as u64,
             options(nostack));
        r
    }};
    ($n:expr, $a:expr, $b:expr, $c:expr, $d:expr) => {{
        let r: u64;
        asm!("int 0x80", inlateout("rax") $n as u64 => r, in("rdi") $a as u64, in("rsi") $b as u64, in("rdx") $c as u64,
             in("r10") $d as u64, options(nostack));
        r
    }};
}
/// retype(untyped_slot, kind, dst_slot, size) -> remaining budget
macro_rules! retype {
    ($u:expr, $kind:expr, $dst:expr, $size:expr) => {
        sys!(4, $u, $kind, $dst, $size)
    };
}
const K_UNTYPED: u64 = 0;
const K_FRAME: u64 = 1;
const K_ENDPOINT: u64 = 2;
const FRAME_COST: u64 = 4096 + 64; // object + its capability node
const EP_COST: u64 = 256 + 64;
/// Untyped budget of the memory accountant: exactly three frames.
pub const ACCOUNTANT_BUDGET: u64 = 3 * FRAME_COST;
const MAP_PAGE: u64 = 0x91_0000;

const SLOT_CONSOLE: u64 = 0; // Endpoint, WRITE only
const SLOT_DIR: u64 = 1; // Dir, READ | GRANT
const SLOT_FREE: u64 = 2;
const SLOT_UNTYPED: u64 = 3; // Untyped, 1 MiB
const SLOT_HOST: u64 = 4; // not held by any test thread
const SLOT_LOCK: u64 = 5; // slot with no cap and never will be
const SLOT_EP: u64 = 6; // Endpoint: server ALL (creator, paid for it), client WRITE|GRANT badge 42, eavesdropper WRITE badge 7
const SLOT_EMPTY: u64 = 7;
pub const DATA_PAGE: u64 = 0x90_0000; // one private, zeroed, writable page per thread
pub const STACK_TOP: u64 = USER_END; // same VA in every thread: spaces are private

macro_rules! send {
    ($slot:expr, $w0:expr, $w1:expr, $w2:expr, $w3:expr) => {{
        let r: u64;
        asm!("int 0x80", inlateout("rax") 8u64 => r, in("rdi") $slot as u64, in("rsi") $w0 as u64, in("rdx") $w1 as u64,
             in("r10") $w2 as u64, in("r8") $w3 as u64, options(nostack));
        r
    }};
}
/// -> (status, badge, w0, w1, w2, w3)
macro_rules! recv {
    ($slot:expr) => {{
        let (r, badge, w0, w1, w2, w3): (u64, u64, u64, u64, u64, u64);
        asm!("int 0x80", inlateout("rax") 9u64 => r, inlateout("rdi") $slot as u64 => badge, lateout("rsi") w0,
             lateout("rdx") w1, lateout("r10") w2, lateout("r8") w3, options(nostack));
        (r, badge, w0, w1, w2, w3)
    }};
}

const E_INVALID: u64 = -(1i64) as u64;
const E_RIGHTS: u64 = -(3i64) as u64;
const E_REVOKED: u64 = -(4i64) as u64;
const E_BUDGET: u64 = -(5i64) as u64;
const E_KIND: u64 = -(6i64) as u64;
const E_OCCUPIED: u64 = -(2i64) as u64;
const E_QUEUE_FULL: u64 = -(7i64) as u64;
const SLOT_FLOOD: u64 = 8; // Endpoint with queue capacity 2 and no receiver, held by the three flood senders

/// (id, name, expected observed value)
pub const EXPECT: &[(u64, &str, u64)] = &[
    (1, "legit invoke of held console cap", 0),
    (2, "invoke empty slot", E_INVALID),
    (3, "invoke slot 0xFFFF_FFFF", E_INVALID),
    (4, "mint READ|GRANT with mask ALL gives 17", 17),
    (5, "invoke minted copy for WRITE", E_RIGHTS),
    (6, "revoke parent removes 1 child", 1),
    (7, "invoke revoked copy", E_REVOKED),
    (8, "retype 2 MiB untyped from 1 MiB budget", E_BUDGET),
    (9, "host-exit via slot not held", E_INVALID),
    (10, "mint from cap without GRANT", E_RIGHTS),
    (11, "retype one frame from 1 MiB (legit)", (1 << 20) - FRAME_COST),
    (12, "retype unknown kind 99", E_KIND),
    (13, "mint into empty slot 5 then invoke", 1),
    (14, "host-exit via console cap (WRITE, wrong object)", E_KIND),
    (15, "5000 mints into an occupied slot: all refused", 0),
    (16, "10000 revocations: only the one live child dies", 1),
    (20, "IPC: server sees client's badge", 42),
    (21, "IPC: server gets word 0", 0x1234),
    (22, "IPC: server gets word 3", 0x4444),
    (23, "IPC: client send completes", 0),
    (24, "IPC: recv with WRITE-only cap", E_RIGHTS),
    (25, "IPC: send on empty slot", E_INVALID),
    (26, "isolation: writer reads back its own page", 0x5EC),
    (27, "isolation: reader sees nothing of the writer", 0),
    (28, "isolation: reader's own page is still writable", 0x7),
    (29, "IPC: third sender on a full endpoint queue refused", E_QUEUE_FULL),
    (95, "badge: client's self-minted copy still carries 42", 42),
    (96, "badge: eavesdropper claiming to be the client arrives as 7", 7),
    // Wrong-capability use (ids 40..49): the right bits on the wrong object,
    // or the right object through the wrong right, must all be refused.
    (40, "send via untyped cap (ALL rights, wrong kind)", E_KIND),
    (41, "recv via dir cap (READ, wrong kind)", E_KIND),
    (42, "retype via dir cap (wrong kind)", E_KIND),
    (43, "host-exit via untyped cap (ALL rights)", E_KIND),
    (44, "retype through an untyped copy spends the parent's budget", (1 << 20) - FRAME_COST),
    (45, "invoke console asking for every right bit", E_RIGHTS),
    (46, "mint a slot onto itself", E_OCCUPIED),
    (47, "revoke an empty slot", E_INVALID),
    (48, "mint from the host slot nobody holds", E_INVALID),
    (49, "recv via console cap (WRITE only)", E_RIGHTS),
    // Revocation mid-flight: a server revokes a client's endpoint copy.
    (50, "revoker: one client copy revoked", 1),
    (51, "victim: send on revoked endpoint copy", E_REVOKED),
    (52, "victim: recv on revoked endpoint copy", E_REVOKED),
    // Resource exhaustion (ids 60..68): floods of refused requests must
    // neither succeed nor consume anything. The kernel's heap and capability
    // table checks at the end measure the "consume" half.
    (60, "flood: 5000 retypes past budget: all refused", 0),
    (61, "flood: budget reads exactly 0 afterwards", 0),
    (62, "flood: 5000 revokes of the untyped: first reclaims its child, rest 0", 1),
    (63, "flood: 5000 sends via wrong kind: all refused", 0),
    (64, "flood: 5000 recvs on empty slot: all refused", 0),
    (65, "flood: 5000 invokes of slot 2^32-1: all refused", 0),
    (66, "flood: 5000 mints without GRANT: all refused", 0),
    (67, "flood: 5000 host-exits via console: all refused", 0),
    (68, "flood: 2000 yields come back", 2000),
    (70, "memory: frame 1 of 3 from untyped", 2 * FRAME_COST),
    (71, "memory: frame 2 of 3", FRAME_COST),
    (72, "memory: frame 3 of 3", 0),
    (73, "memory: frame 4 refused", E_BUDGET),
    (74, "memory: 5000 over-budget retypes, all refused", 0),
    (75, "memory: budget still exactly 0 after them", 0),
    (76, "memory: delete one frame cap frees 1 node", 1),
    (77, "memory: budget refunded one frame", FRAME_COST),
    (78, "memory: frame 4 now fits", 0),
    (79, "memory: map frame, write, read back", 0xABC),
    (80, "memory: map same frame twice refused", E_OCCUPIED),
    (81, "memory: map onto an occupied address refused", E_OCCUPIED),
    (82, "memory: map with READ-only cap as writable refused", E_RIGHTS),
    (83, "memory: 1000 endpoint create/delete cycles, all consistent", 0),
    (84, "memory: delete the mapped frame", 1),
    (85, "memory: budget after deleting the mapped frame", 2 * FRAME_COST),
    (90, "untyped death: frame cap paid by deleted untyped", E_REVOKED),
    (91, "untyped death: deleted untyped slot is empty", E_INVALID),
    (92, "untyped death: delete freed the untyped and its frame (2 nodes)", 2),
    // Phase 2b: reported by the rogue application (100..109, 112) and the
    // personality server (110, 111). Values are Linux errnos, negative.
    (100, "rogue: read of httpd's fd 3 is EBADF", -(9i64) as u64),
    (101, "rogue: open without fs.read is EACCES", -(13i64) as u64),
    (102, "rogue: socket without net grant is EACCES", -(13i64) as u64),
    (103, "rogue: write of 1 MiB from a 16 KiB buffer is EFAULT", -(14i64) as u64),
    (104, "rogue: unknown syscall is ENOSYS", -(38i64) as u64),
    (105, "rogue: 5000 unknown syscalls, all ENOSYS", 0),
    (106, "rogue: host-exit via personality endpoint", E_KIND),
    (107, "rogue: host-exit via reply endpoint (READ only)", E_RIGHTS),
    (108, "rogue: slots 4..0xFFFF name nothing", 0),
    (109, "rogue: 8 epoll objects then namespace quota", 8),
    (110, "personality: HTTP connections accepted", 62), // 60 native + 2 Linux
    (111, "personality: exit_group from every client", 10),
    // Linux personality (src/linux.rs): real static Linux binaries on the
    // `syscall` ABI, served by the same personality server. Reported as
    // `CHECK <id> <value>` lines on their stdout, which the kernel reads.
    (140, "linux httpd: socket, bind 8081, listen", 1),
    (141, "linux httpd: requests served", 2),
    (142, "linux client: GET /index.html status", 200),
    (143, "linux client: body length", 265),
    (144, "linux client: GET /missing status", 404),
    (145, "linux client: openat without fs.read is EACCES", -(13i64) as u64),
    (146, "linux client: uname says Linux x86_64", 1),
    (147, "linux client: brk grows a usable heap", 1),
    (148, "linux client: mmap anonymous memory is usable", 1),
    (149, "linux client: munmap then mmap again reuses the range", 1),
    (150, "linux hostile: ptrace is EPERM (denied tier)", -(1i64) as u64),
    (151, "linux hostile: openat /etc/passwd without fs.read is EACCES", -(13i64) as u64),
    (152, "linux hostile: socket without net grant is EACCES", -(13i64) as u64),
    (153, "linux hostile: write from kernel memory is EFAULT", -(14i64) as u64),
    (154, "linux hostile: mmap 64 MiB from a 1 MiB budget is ENOMEM", -(12i64) as u64),
    (155, "linux hostile: mprotect RWX is EACCES (W^X)", -(13i64) as u64),
    (156, "linux hostile: MAP_FIXED over the kernel is EINVAL", -(22i64) as u64),
    (157, "linux hostile: syscall 999 is ENOSYS", -(38i64) as u64),
    (158, "linux hostile: brk into the stack is refused", 1),
    (159, "linux hostile: bpf is EPERM (denied tier)", -(1i64) as u64),
    (160, "linux hostile: write to an unopened fd is EBADF", -(9i64) as u64),
    (161, "linux hostile: read of fd 3 (not open) is EBADF", -(9i64) as u64),
    (162, "linux hostile: 1 MiB write from a 64-byte buffer is EFAULT", -(14i64) as u64),
    (163, "linux hostile: 5000 bad syscalls, all ENOSYS, still alive", 1),
    // Unmodified static musl BusyBox 1.36.1 (linux/busybox/), judged on its
    // exact output and exit code.
    (170, "busybox echo: prints its argument", 1),
    (171, "busybox cat: /site/style.css byte for byte", 1),
    (172, "busybox sh -c: arithmetic, printf builtin, expansion", 1),
    (173, "busybox cat without fs.read: Permission denied, exit 1", 1),
    (112, "rogue: its own buffer is read/write", 0xB0B),
];

pub fn judge(k: &mut Kernel, id: u64, observed: u64) {
    k.checks += 1;
    match EXPECT.iter().find(|(i, _, _)| *i == id) {
        Some((_, name, want)) if *want == observed => crate::kprintln!("PASS {name:<40} got {}", observed as i64),
        Some((_, name, want)) => {
            k.failures += 1;
            crate::kprintln!("FAIL {name:<40} got {} want {}", observed as i64, *want as i64);
        }
        None => {
            k.failures += 1;
            crate::kprintln!("FAIL unknown test id {id}");
        }
    }
}

#[link_section = ".user"]
#[inline(never)]
pub extern "C" fn hostile_caps() -> ! {
    unsafe {
        let r = sys!(1, SLOT_CONSOLE, 2u64, 0); // Rights::WRITE
        sys!(0, SLOT_CONSOLE, 1, r);
        let r = sys!(1, SLOT_FREE, 0, 0);
        sys!(0, SLOT_CONSOLE, 2, r);
        let r = sys!(1, 0xFFFF_FFFFu64, 0, 0);
        sys!(0, SLOT_CONSOLE, 3, r);
        let r = sys!(2, SLOT_DIR, SLOT_FREE, 0x1F);
        sys!(0, SLOT_CONSOLE, 4, r);
        let r = sys!(1, SLOT_FREE, 2u64, 0);
        sys!(0, SLOT_CONSOLE, 5, r);
        let r = sys!(3, SLOT_DIR, 0, 0);
        sys!(0, SLOT_CONSOLE, 6, r);
        let r = sys!(1, SLOT_FREE, 0, 0);
        sys!(0, SLOT_CONSOLE, 7, r);
        let r = retype!(SLOT_UNTYPED, K_UNTYPED, SLOT_FREE, 2u64 << 20);
        sys!(0, SLOT_CONSOLE, 8, r);
        let r = sys!(7, SLOT_HOST, 0, 0);
        sys!(0, SLOT_CONSOLE, 9, r);
        let r = sys!(2, SLOT_CONSOLE, SLOT_LOCK, 0x1F);
        sys!(0, SLOT_CONSOLE, 10, r);
        let r = retype!(SLOT_UNTYPED, K_FRAME, SLOT_FREE, 0);
        sys!(0, SLOT_CONSOLE, 11, r);
        let r = retype!(SLOT_UNTYPED, 99u64, SLOT_EMPTY, 0);
        sys!(0, SLOT_CONSOLE, 12, r);
        let _ = sys!(10, SLOT_FREE, 0, 0); // delete the frame again
        // Legit attenuation: mint console (no GRANT) is refused above, so mint
        // from the dir cap failed too after revoke? No: revoke kills children,
        // not the parent. Mint a READ-only copy into slot 5 and read it.
        let _ = sys!(2, SLOT_DIR, SLOT_LOCK, 1);
        let r = sys!(1, SLOT_LOCK, 1u64, 0);
        sys!(0, SLOT_CONSOLE, 13, if r == 0 { 1 } else { r });
        // Review finding: WRITE on the console must not stop the machine.
        let r = sys!(7, SLOT_CONSOLE, 0, 0);
        sys!(0, SLOT_CONSOLE, 14, r);
        // Review finding: refused mints must not allocate. The kernel checks
        // the node count after this thread; here we count wrong results.
        let mut bad = 0u64;
        let mut i = 0u64;
        while i < 5000 {
            let r = sys!(2, SLOT_DIR, SLOT_CONSOLE, 0x1F);
            if r != E_OCCUPIED {
                bad += 1;
            }
            i += 1;
            // Under the unoptimised build 5000 syscalls outrun the 50-tick budget:
            // yield now and then (free, refills the budget) so both profiles pass.
            if i & 511 == 0 {
                sys!(5, 0, 0, 0);
            }
        }
        sys!(0, SLOT_CONSOLE, 15, bad);
        // Assessment finding: revoke() used a temporary Vec per call and the
        // kernel heap never frees. 10000 revocations: the first one on the
        // dir kills the READ copy minted into slot 5 above (1), every other
        // returns 0, and the kernel checks after the run that the heap did
        // not move.
        let mut sum = 0u64;
        let mut i = 0u64;
        while i < 5000 {
            sum += sys!(3, SLOT_DIR, 0, 0);
            sum += sys!(3, SLOT_UNTYPED, 0, 0);
            i += 1;
            // Under the unoptimised build 5000 syscalls outrun the 50-tick budget:
            // yield now and then (free, refills the budget) so both profiles pass.
            if i & 511 == 0 {
                sys!(5, 0, 0, 0);
            }
        }
        sys!(0, SLOT_CONSOLE, 16, sum);
        // Finally: write to kernel memory. Expected: #PF, thread killed.
        asm!("mov qword ptr [{0}], 0xDEAD", in(reg) 0x10_0000u64, options(nostack));
        sys!(6, 0, 0, 0);
        loop {}
    }
}

#[link_section = ".user"]
#[inline(never)]
pub extern "C" fn hostile_cli() -> ! {
    unsafe { asm!("cli", options(nomem, nostack)) };
    loop {}
}
#[link_section = ".user"]
#[inline(never)]
pub extern "C" fn hostile_hlt() -> ! {
    unsafe { asm!("hlt", options(nomem, nostack)) };
    loop {}
}
#[link_section = ".user"]
#[inline(never)]
pub extern "C" fn hostile_spin() -> ! {
    loop {
        unsafe { asm!("pause", options(nomem, nostack)) }
    }
}
#[link_section = ".user"]
#[inline(never)]
pub extern "C" fn hostile_outport() -> ! {
    unsafe { asm!("out dx, eax", in("dx") 0xF4u16, in("eax") 0u32, options(nomem, nostack)) };
    loop {}
}
#[link_section = ".user"]
#[inline(never)]
pub extern "C" fn hostile_badvector() -> ! {
    unsafe { asm!("int 0x81", options(nomem, nostack)) };
    loop {}
}
#[link_section = ".user"]
#[inline(never)]
pub extern "C" fn hostile_readkernel() -> ! {
    unsafe {
        let v: u64;
        asm!("mov {0}, qword ptr [{1}]", out(reg) v, in(reg) 0x10_0000u64, options(nostack));
        sys!(0, SLOT_CONSOLE, 99, v);
    }
    loop {}
}
#[link_section = ".user"]
#[inline(never)]
pub extern "C" fn hostile_callkernel() -> ! {
    unsafe {
        asm!("call {0}", in(reg) 0x10_0000u64, options(nostack));
    }
    loop {}
}
#[link_section = ".user"]
#[inline(never)]
pub extern "C" fn hostile_cr3() -> ! {
    unsafe { asm!("mov cr3, rax", in("rax") 0u64, options(nomem, nostack)) };
    loop {}
}
#[link_section = ".user"]
#[inline(never)]
pub extern "C" fn hostile_rdmsr() -> ! {
    unsafe { asm!("rdmsr", in("ecx") 0xC000_0080u32, out("eax") _, out("edx") _, options(nomem, nostack)) };
    loop {}
}
#[link_section = ".user"]
#[inline(never)]
pub extern "C" fn hostile_syscall_insn() -> ! {
    // `syscall` is the Linux personality's entry; a native thread is killed.
    unsafe { asm!("syscall", out("rcx") _, out("r11") _, options(nomem, nostack)) };
    loop {}
}
#[link_section = ".user"]
#[inline(never)]
pub extern "C" fn hostile_sse() -> ! {
    // Native threads run with CR0.TS set: `xorps xmm0, xmm0` is #NM. The FPU
    // belongs to the Linux personality; nothing of a Linux thread's
    // registers can be read from here, and nothing has to be saved for us.
    unsafe { asm!(".byte 0x0f, 0x57, 0xc0", options(nomem, nostack)) };
    loop {}
}
#[link_section = ".user"]
#[inline(never)]
pub extern "C" fn hostile_stack_outside() -> ! {
    unsafe { asm!("mov rsp, {0}; push rax", in(reg) 0x20_0000u64, options(nomem)) };
    loop {}
}
/// W^X: user code is mapped read-only. Writing one byte into our own text
/// must fault, not patch the program.
#[link_section = ".user"]
#[inline(never)]
pub extern "C" fn hostile_write_code() -> ! {
    unsafe { asm!("mov byte ptr [{0}], 0x90", in(reg) USER_BASE, options(nostack)) };
    loop {}
}
/// W^X: data pages are NX. Write `ud2` into the data page and call it. With
/// NX honoured this is a #PF (instruction fetch); without it the ud2 would
/// run and report #UD, so the test tells the two apart.
#[link_section = ".user"]
#[inline(never)]
pub extern "C" fn hostile_exec_data() -> ! {
    unsafe {
        asm!("mov word ptr [{0}], 0x0B0F; call {0}", in(reg) DATA_PAGE, options(nostack));
    }
    loop {}
}
/// NX also covers the stack: push `ud2` and jump to it.
#[link_section = ".user"]
#[inline(never)]
pub extern "C" fn hostile_exec_stack() -> ! {
    unsafe {
        asm!("sub rsp, 16; mov word ptr [rsp], 0x0B0F; jmp rsp", options(nostack));
    }
    loop {}
}
/// Flood senders: nobody ever receives on SLOT_FLOOD. The first two block in
/// the endpoint's fixed queue and are reported as deadlocked; the third,
/// which yields first so that the other two have queued, is refused.
#[link_section = ".user"]
#[inline(never)]
pub extern "C" fn flood_sender() -> ! {
    unsafe {
        send!(SLOT_FLOOD, 1u64, 1u64, 1u64, 1u64);
    }
    loop {}
}
#[link_section = ".user"]
#[inline(never)]
pub extern "C" fn flood_sender_last() -> ! {
    unsafe {
        let mut i = 0u64;
        while i < 4 {
            sys!(5, 0, 0, 0);
            i += 1;
        }
        let r = send!(SLOT_FLOOD, 1u64, 1u64, 1u64, 1u64);
        sys!(0, SLOT_CONSOLE, 29, r);
        sys!(6, 0, 0, 0);
    }
    loop {}
}
#[link_section = ".user"]
#[inline(never)]
pub extern "C" fn good_citizen() -> ! {
    unsafe {
        // Yields a few times, then exits cleanly.
        let mut i = 0u64;
        while i < 3 {
            sys!(5, 0, 0, 0);
            i += 1;
        }
        sys!(6, 7u64, 0, 0);
    }
    loop {}
}

#[link_section = ".user"]
#[inline(never)]
pub extern "C" fn ipc_server() -> ! {
    unsafe {
        let (r, badge, w0, _w1, _w2, w3) = recv!(SLOT_EP);
        sys!(0, SLOT_CONSOLE, 20, if r == 0 { badge } else { r });
        sys!(0, SLOT_CONSOLE, 21, w0);
        sys!(0, SLOT_CONSOLE, 22, w3);
        // Two more messages arrive in either order: one through the client's
        // self-minted copy (must still carry 42) and one from the eavesdropper
        // claiming in word 0 to be the client (must carry 7). The badge is
        // the only identity a server trusts.
        let mut i = 0;
        while i < 2 {
            let (r, badge, w0, _, _, _) = recv!(SLOT_EP);
            let v = if r == 0 { badge } else { r };
            sys!(0, SLOT_CONSOLE, if w0 == 0xC0DE { 95 } else { 96 }, v);
            i += 1;
        }
        sys!(6, 0, 0, 0);
    }
    loop {}
}
#[link_section = ".user"]
#[inline(never)]
pub extern "C" fn ipc_client() -> ! {
    unsafe {
        let r = send!(SLOT_EP, 0x1234u64, 0x2222u64, 0x3333u64, 0x4444u64);
        sys!(0, SLOT_CONSOLE, 23, r);
        // Copy our own endpoint cap (we hold GRANT) and send through the
        // copy. Badges are sticky: the copy carries 42 whatever we ask for.
        let _ = sys!(2, SLOT_EP, SLOT_FREE, 0x1F);
        let _ = send!(SLOT_FREE, 0xC0DEu64, 0u64, 0u64, 0u64);
        sys!(6, 0, 0, 0);
    }
    loop {}
}
#[link_section = ".user"]
#[inline(never)]
pub extern "C" fn ipc_eavesdropper() -> ! {
    unsafe {
        let (r, _, _, _, _, _) = recv!(SLOT_EP);
        sys!(0, SLOT_CONSOLE, 24, r);
        let r = send!(SLOT_EMPTY, 1u64, 2u64, 3u64, 4u64);
        sys!(0, SLOT_CONSOLE, 25, r);
        // Claim to be the client in the message words; the badge says 7.
        let _ = send!(SLOT_EP, 42u64, 42u64, 42u64, 42u64);
        sys!(6, 0, 0, 0);
    }
    loop {}
}
#[link_section = ".user"]
#[inline(never)]
pub extern "C" fn iso_writer() -> ! {
    unsafe {
        asm!("mov qword ptr [{0}], 0x5EC", in(reg) DATA_PAGE, options(nostack));
        // Let the reader run before and after the write.
        sys!(5, 0, 0, 0);
        let v: u64;
        asm!("mov {0}, qword ptr [{1}]", out(reg) v, in(reg) DATA_PAGE, options(nostack));
        sys!(0, SLOT_CONSOLE, 26, v);
        sys!(6, 0, 0, 0);
    }
    loop {}
}
#[link_section = ".user"]
#[inline(never)]
pub extern "C" fn iso_reader() -> ! {
    unsafe {
        let mut i = 0u64;
        while i < 4 {
            sys!(5, 0, 0, 0);
            i += 1;
        }
        let v: u64;
        asm!("mov {0}, qword ptr [{1}]", out(reg) v, in(reg) DATA_PAGE, options(nostack));
        sys!(0, SLOT_CONSOLE, 27, v);
        asm!("mov qword ptr [{0}], 7", in(reg) DATA_PAGE, options(nostack));
        let v: u64;
        asm!("mov {0}, qword ptr [{1}]", out(reg) v, in(reg) DATA_PAGE, options(nostack));
        sys!(0, SLOT_CONSOLE, 28, v);
        sys!(6, 0, 0, 0);
    }
    loop {}
}

// ---- wrong-capability use ---------------------------------------------------
// Every syscall checks three things in order: the slot names a live cap, the
// cap carries the right the call needs, and the object is of the kind the
// call works on. These threads hold valid caps and use each one wrongly.

const SLOT_EP2: u64 = 9; // revoker: Endpoint root; victim: READ|WRITE copy badge 9

#[link_section = ".user"]
#[inline(never)]
pub extern "C" fn wrong_kind() -> ! {
    unsafe {
        // Untyped has ALL rights, including WRITE: still not an endpoint.
        let r = send!(SLOT_UNTYPED, 1u64, 2u64, 3u64, 4u64);
        sys!(0, SLOT_CONSOLE, 40, r);
        // Dir has READ: still not an endpoint.
        let (r, _, _, _, _, _) = recv!(SLOT_DIR);
        sys!(0, SLOT_CONSOLE, 41, r);
        // Dir is not untyped memory.
        let r = retype!(SLOT_DIR, K_FRAME, SLOT_FREE, 0);
        sys!(0, SLOT_CONSOLE, 42, r);
        // ALL rights on untyped memory do not reach the machine.
        let r = sys!(7, SLOT_UNTYPED, 0, 0);
        sys!(0, SLOT_CONSOLE, 43, r);
        // A copy of the untyped cap names the same memory: spending through
        // the copy spends the parent's budget, never a second one.
        let _ = sys!(2, SLOT_UNTYPED, SLOT_FREE, 0x1F);
        let _ = retype!(SLOT_FREE, K_FRAME, SLOT_LOCK, 0);
        let r = sys!(12, SLOT_UNTYPED, 0, 0);
        sys!(0, SLOT_CONSOLE, 44, r);
        let _ = sys!(10, SLOT_LOCK, 0, 0); // give the frame back; slot 5 is empty again
        // Bits above Rights::ALL are masked off; asking for 0xFF is asking
        // for ALL, which the WRITE-only console does not have.
        let r = sys!(1, SLOT_CONSOLE, 0xFFu64, 0);
        sys!(0, SLOT_CONSOLE, 45, r);
        let r = sys!(2, SLOT_DIR, SLOT_DIR, 0x1F);
        sys!(0, SLOT_CONSOLE, 46, r);
        let r = sys!(3, SLOT_LOCK, 0, 0);
        sys!(0, SLOT_CONSOLE, 47, r);
        let r = sys!(2, SLOT_HOST, SLOT_LOCK, 0x1F);
        sys!(0, SLOT_CONSOLE, 48, r);
        let (r, _, _, _, _, _) = recv!(SLOT_CONSOLE);
        sys!(0, SLOT_CONSOLE, 49, r);
        sys!(6, 0, 0, 0);
    }
    loop {}
}

/// Holds the root of a second endpoint and revokes the victim's copy before
/// the victim gets to use it. Runs before the victim in CASES order.
#[link_section = ".user"]
#[inline(never)]
pub extern "C" fn ipc_revoker() -> ! {
    unsafe {
        let r = sys!(3, SLOT_EP2, 0, 0);
        sys!(0, SLOT_CONSOLE, 50, r);
        sys!(6, 0, 0, 0);
    }
    loop {}
}
/// Yields twice so the revoker runs first, then tries its copy both ways.
#[link_section = ".user"]
#[inline(never)]
pub extern "C" fn ipc_victim() -> ! {
    unsafe {
        sys!(5, 0, 0, 0);
        sys!(5, 0, 0, 0);
        let r = send!(SLOT_EP2, 1u64, 2u64, 3u64, 4u64);
        sys!(0, SLOT_CONSOLE, 51, r);
        let (r, _, _, _, _, _) = recv!(SLOT_EP2);
        sys!(0, SLOT_CONSOLE, 52, r);
        sys!(6, 0, 0, 0);
    }
    loop {}
}

// ---- resource exhaustion ------------------------------------------------------
// Floods of refused syscalls. Each loop counts answers that were not the
// refusal; the kernel's end-of-run checks (capability nodes, heap bytes)
// show whether any refusal cost memory.

#[link_section = ".user"]
#[inline(never)]
pub extern "C" fn exhaust() -> ! {
    unsafe {
        // Spend the whole budget legitimately (one child untyped that costs
        // exactly the 1 MiB with its node), then hammer it.
        let _ = retype!(SLOT_UNTYPED, K_UNTYPED, SLOT_FREE, (1u64 << 20) - 64);
        let mut bad = 0u64;
        let mut i = 0u64;
        while i < 5000 {
            if retype!(SLOT_UNTYPED, K_FRAME, SLOT_LOCK, 0) != E_BUDGET {
                bad += 1;
            }
            i += 1;
            // Under the unoptimised build 5000 syscalls outrun the 50-tick budget:
            // yield now and then (free, refills the budget) so both profiles pass.
            if i & 511 == 0 {
                sys!(5, 0, 0, 0);
            }
        }
        sys!(0, SLOT_CONSOLE, 60, bad);
        let r = sys!(12, SLOT_UNTYPED, 0, 0);
        sys!(0, SLOT_CONSOLE, 61, r);
        // Revoking the untyped takes the child untyped it paid for (1 node,
        // and the whole budget comes back); every later revoke finds nothing.
        let mut sum = 0u64;
        let mut i = 0u64;
        while i < 5000 {
            sum += sys!(3, SLOT_UNTYPED, 0, 0);
            i += 1;
            // Under the unoptimised build 5000 syscalls outrun the 50-tick budget:
            // yield now and then (free, refills the budget) so both profiles pass.
            if i & 511 == 0 {
                sys!(5, 0, 0, 0);
            }
        }
        sys!(0, SLOT_CONSOLE, 62, sum);
        let mut bad = 0u64;
        let mut i = 0u64;
        while i < 5000 {
            if send!(SLOT_UNTYPED, 0u64, 0u64, 0u64, 0u64) != E_KIND {
                bad += 1;
            }
            i += 1;
            // Under the unoptimised build 5000 syscalls outrun the 50-tick budget:
            // yield now and then (free, refills the budget) so both profiles pass.
            if i & 511 == 0 {
                sys!(5, 0, 0, 0);
            }
        }
        sys!(0, SLOT_CONSOLE, 63, bad);
        let mut bad = 0u64;
        let mut i = 0u64;
        while i < 5000 {
            let (r, _, _, _, _, _) = recv!(SLOT_EMPTY);
            if r != E_INVALID {
                bad += 1;
            }
            i += 1;
            // Under the unoptimised build 5000 syscalls outrun the 50-tick budget:
            // yield now and then (free, refills the budget) so both profiles pass.
            if i & 511 == 0 {
                sys!(5, 0, 0, 0);
            }
        }
        sys!(0, SLOT_CONSOLE, 64, bad);
        let mut bad = 0u64;
        let mut i = 0u64;
        while i < 5000 {
            if sys!(1, 0xFFFF_FFFFu64, 0, 0) != E_INVALID {
                bad += 1;
            }
            i += 1;
            // Under the unoptimised build 5000 syscalls outrun the 50-tick budget:
            // yield now and then (free, refills the budget) so both profiles pass.
            if i & 511 == 0 {
                sys!(5, 0, 0, 0);
            }
        }
        sys!(0, SLOT_CONSOLE, 65, bad);
        let mut bad = 0u64;
        let mut i = 0u64;
        while i < 5000 {
            if sys!(2, SLOT_CONSOLE, SLOT_FREE, 0x1F) != E_RIGHTS {
                bad += 1;
            }
            i += 1;
            // Under the unoptimised build 5000 syscalls outrun the 50-tick budget:
            // yield now and then (free, refills the budget) so both profiles pass.
            if i & 511 == 0 {
                sys!(5, 0, 0, 0);
            }
        }
        sys!(0, SLOT_CONSOLE, 66, bad);
        let mut bad = 0u64;
        let mut i = 0u64;
        while i < 5000 {
            if sys!(7, SLOT_CONSOLE, 0, 0) != E_KIND {
                bad += 1;
            }
            i += 1;
            // Under the unoptimised build 5000 syscalls outrun the 50-tick budget:
            // yield now and then (free, refills the budget) so both profiles pass.
            if i & 511 == 0 {
                sys!(5, 0, 0, 0);
            }
        }
        sys!(0, SLOT_CONSOLE, 67, bad);
        // Yielding is free: 2000 round trips through the scheduler.
        let mut n = 0u64;
        let mut i = 0u64;
        while i < 2000 {
            if sys!(5, 0, 0, 0) == 0 {
                n += 1;
            }
            i += 1;
        }
        sys!(0, SLOT_CONSOLE, 68, n);
        sys!(6, 0, 0, 0);
    }
    loop {}
}

/// Grow the stack past its 64 KiB until it hits the unmapped page below.
#[link_section = ".user"]
#[inline(never)]
pub extern "C" fn stack_overflow() -> ! {
    loop {
        unsafe { asm!("push rax", options(nomem)) }
    }
}
/// Run off the end of the single data page.
#[link_section = ".user"]
#[inline(never)]
pub extern "C" fn data_page_overrun() -> ! {
    unsafe {
        let mut p = DATA_PAGE;
        while p < DATA_PAGE + 0x2000 {
            asm!("mov qword ptr [{0}], 1", in(reg) p, options(nostack));
            p += 8;
        }
    }
    loop {}
}
/// Yield refills the budget: 1000 yields on a 2-tick budget are never
/// killed, and the thread exits by itself with code 9.
#[link_section = ".user"]
#[inline(never)]
pub extern "C" fn yield_forever_then_exit() -> ! {
    unsafe {
        let mut i = 0u64;
        while i < 1000 {
            sys!(5, 0, 0, 0);
            i += 1;
        }
        sys!(6, 9u64, 0, 0);
    }
    loop {}
}

/// Phase 3: everything a thread creates is paid for from its untyped memory,
/// a refused request consumes nothing, and what it frees it can use again.
#[link_section = ".user"]
#[inline(never)]
pub extern "C" fn memory_accountant() -> ! {
    unsafe {
        // Slots 2, 5, 7 take frames; slot 6 is the one that must fail.
        let r = retype!(SLOT_UNTYPED, K_FRAME, SLOT_FREE, 0);
        sys!(0, SLOT_CONSOLE, 70, r);
        let r = retype!(SLOT_UNTYPED, K_FRAME, SLOT_LOCK, 0);
        sys!(0, SLOT_CONSOLE, 71, r);
        let r = retype!(SLOT_UNTYPED, K_FRAME, SLOT_EMPTY, 0);
        sys!(0, SLOT_CONSOLE, 72, r);
        let r = retype!(SLOT_UNTYPED, K_FRAME, SLOT_EP, 0);
        sys!(0, SLOT_CONSOLE, 73, r);
        let mut bad = 0u64;
        let mut i = 0u64;
        while i < 5000 {
            if retype!(SLOT_UNTYPED, K_FRAME, SLOT_EP, 0) != E_BUDGET {
                bad += 1;
            }
            if retype!(SLOT_UNTYPED, K_ENDPOINT, SLOT_EP, 0) != E_BUDGET {
                bad += 1;
            }
            i += 1;
        }
        sys!(0, SLOT_CONSOLE, 74, bad);
        let r = sys!(12, SLOT_UNTYPED, 0, 0);
        sys!(0, SLOT_CONSOLE, 75, r);
        let r = sys!(10, SLOT_EMPTY, 0, 0);
        sys!(0, SLOT_CONSOLE, 76, r);
        let r = sys!(12, SLOT_UNTYPED, 0, 0);
        sys!(0, SLOT_CONSOLE, 77, r);
        let r = retype!(SLOT_UNTYPED, K_FRAME, SLOT_EP, 0);
        sys!(0, SLOT_CONSOLE, 78, r);
        // Map the first frame, use it.
        let r = sys!(11, SLOT_FREE, MAP_PAGE, 1);
        let v: u64;
        if r == 0 {
            asm!("mov qword ptr [{0}], 0xABC", in(reg) MAP_PAGE, options(nostack));
            asm!("mov {0}, qword ptr [{1}]", out(reg) v, in(reg) MAP_PAGE, options(nostack));
        } else {
            v = r;
        }
        sys!(0, SLOT_CONSOLE, 79, v);
        let r = sys!(11, SLOT_FREE, MAP_PAGE + 0x1000, 1);
        sys!(0, SLOT_CONSOLE, 80, r);
        let r = sys!(11, SLOT_LOCK, MAP_PAGE, 1);
        sys!(0, SLOT_CONSOLE, 81, r);
        // Free frame 4 (slot 6) so there is budget for a 64-byte node again.
        let _ = sys!(10, SLOT_EP, 0, 0);
        // A READ|MAP copy of frame 2 may not be mapped writable. Slot 7 is
        // free again (deleted above); free the copy afterwards.
        let _ = sys!(2, SLOT_LOCK, SLOT_EMPTY, 1u64 | 8u64);
        let r = sys!(11, SLOT_EMPTY, MAP_PAGE + 0x2000, 1);
        sys!(0, SLOT_CONSOLE, 82, r);
        let _ = sys!(10, SLOT_EMPTY, 0, 0);
        // Churn: an endpoint created and deleted 1000 times must always fit,
        // cost the same, and leave the budget where it was.
        let base = sys!(12, SLOT_UNTYPED, 0, 0);
        let mut bad = 0u64;
        let mut i = 0u64;
        while i < 1000 {
            if retype!(SLOT_UNTYPED, K_ENDPOINT, SLOT_EMPTY, 0) != base - EP_COST {
                bad += 1;
            }
            if sys!(10, SLOT_EMPTY, 0, 0) != 1 {
                bad += 1;
            }
            if sys!(12, SLOT_UNTYPED, 0, 0) != base {
                bad += 1;
            }
            i += 1;
        }
        sys!(0, SLOT_CONSOLE, 83, bad);
        // Delete the mapped frame: the kernel unmaps it and the page below
        // is gone. Budget: frames in slots 2 and 5 were held; now only 5.
        let r = sys!(10, SLOT_FREE, 0, 0);
        sys!(0, SLOT_CONSOLE, 84, r);
        let r = sys!(12, SLOT_UNTYPED, 0, 0);
        sys!(0, SLOT_CONSOLE, 85, r);
        // Touching the unmapped page is a #PF; the thread dies holding one
        // frame (slot 5), which teardown must return.
        let v: u64;
        asm!("mov {0}, qword ptr [{1}]", out(reg) v, in(reg) MAP_PAGE, options(nostack));
        sys!(0, SLOT_CONSOLE, 99, v);
    }
    loop {}
}

/// Deleting an untyped takes every object it paid for, mapped or not.
#[link_section = ".user"]
#[inline(never)]
pub extern "C" fn untyped_death() -> ! {
    unsafe {
        let _ = retype!(SLOT_UNTYPED, K_FRAME, SLOT_FREE, 0);
        let _ = sys!(11, SLOT_FREE, MAP_PAGE, 1);
        asm!("mov qword ptr [{0}], 0x77", in(reg) MAP_PAGE, options(nostack));
        let n = sys!(10, SLOT_UNTYPED, 0, 0);
        let r = sys!(1, SLOT_FREE, 0, 0);
        sys!(0, SLOT_CONSOLE, 90, r);
        let r = sys!(12, SLOT_UNTYPED, 0, 0);
        sys!(0, SLOT_CONSOLE, 91, r);
        sys!(0, SLOT_CONSOLE, 92, n);
        let v: u64;
        asm!("mov {0}, qword ptr [{1}]", out(reg) v, in(reg) MAP_PAGE, options(nostack));
        sys!(0, SLOT_CONSOLE, 99, v);
    }
    loop {}
}

pub struct Case {
    pub name: &'static str,
    pub entry: extern "C" fn() -> !,
    pub expect: Death,
    pub budget: u32,
    /// Bytes of untyped memory in slot 3.
    pub untyped: u64,
}

pub const CASES: &[Case] = &[
    Case { name: "capability attacks", entry: hostile_caps, expect: Death::Fault(14), budget: 50, untyped: 1 << 20 },
    Case { name: "cli", entry: hostile_cli, expect: Death::Fault(13), budget: 50, untyped: 1 << 20 },
    Case { name: "hlt", entry: hostile_hlt, expect: Death::Fault(13), budget: 50, untyped: 1 << 20 },
    Case { name: "infinite loop", entry: hostile_spin, expect: Death::Budget, budget: 5, untyped: 1 << 20 },
    Case { name: "out to debug-exit port", entry: hostile_outport, expect: Death::Fault(13), budget: 50, untyped: 1 << 20 },
    Case { name: "int 0x81", entry: hostile_badvector, expect: Death::Fault(13), budget: 50, untyped: 1 << 20 },
    Case { name: "read kernel memory", entry: hostile_readkernel, expect: Death::Fault(14), budget: 50, untyped: 1 << 20 },
    Case { name: "jump into kernel code", entry: hostile_callkernel, expect: Death::Fault(14), budget: 50, untyped: 1 << 20 },
    Case { name: "mov cr3", entry: hostile_cr3, expect: Death::Fault(13), budget: 50, untyped: 1 << 20 },
    Case { name: "rdmsr", entry: hostile_rdmsr, expect: Death::Fault(13), budget: 50, untyped: 1 << 20 },
    // `syscall` is the Linux entry; a native thread using it is killed.
    Case { name: "syscall instruction", entry: hostile_syscall_insn, expect: Death::WrongAbi, budget: 50, untyped: 1 << 20 },
    Case { name: "sse from native thread", entry: hostile_sse, expect: Death::Fault(7), budget: 50, untyped: 1 << 20 },
    Case { name: "stack outside partition", entry: hostile_stack_outside, expect: Death::Fault(14), budget: 50, untyped: 1 << 20 },
    Case { name: "write own code (W^X)", entry: hostile_write_code, expect: Death::Fault(14), budget: 50, untyped: 1 << 20 },
    Case { name: "execute data page (NX)", entry: hostile_exec_data, expect: Death::Fault(14), budget: 50, untyped: 1 << 20 },
    Case { name: "execute stack (NX)", entry: hostile_exec_stack, expect: Death::Fault(14), budget: 50, untyped: 1 << 20 },
    Case { name: "flood sender 1", entry: flood_sender, expect: Death::Budget, budget: 50, untyped: 1 << 20 },
    Case { name: "flood sender 2", entry: flood_sender, expect: Death::Budget, budget: 50, untyped: 1 << 20 },
    Case { name: "flood sender 3", entry: flood_sender_last, expect: Death::Exited(0), budget: 50, untyped: 1 << 20 },
    Case { name: "good citizen", entry: good_citizen, expect: Death::Exited(7), budget: 50, untyped: 1 << 20 },
    Case { name: "ipc server", entry: ipc_server, expect: Death::Exited(0), budget: 50, untyped: 1 << 20 },
    Case { name: "ipc client", entry: ipc_client, expect: Death::Exited(0), budget: 50, untyped: 1 << 20 },
    Case { name: "ipc eavesdropper", entry: ipc_eavesdropper, expect: Death::Exited(0), budget: 50, untyped: 1 << 20 },
    Case { name: "isolation writer", entry: iso_writer, expect: Death::Exited(0), budget: 50, untyped: 1 << 20 },
    Case { name: "isolation reader", entry: iso_reader, expect: Death::Exited(0), budget: 50, untyped: 1 << 20 },
    Case { name: "wrong-kind caps", entry: wrong_kind, expect: Death::Exited(0), budget: 50, untyped: 1 << 20 },
    Case { name: "ipc revoker", entry: ipc_revoker, expect: Death::Exited(0), budget: 50, untyped: 1 << 20 },
    Case { name: "ipc victim", entry: ipc_victim, expect: Death::Exited(0), budget: 50, untyped: 1 << 20 },
    Case { name: "exhaustion floods", entry: exhaust, expect: Death::Exited(0), budget: 50, untyped: 1 << 20 },
    Case { name: "stack overflow", entry: stack_overflow, expect: Death::Fault(14), budget: 50, untyped: 1 << 20 },
    Case { name: "data page overrun", entry: data_page_overrun, expect: Death::Fault(14), budget: 50, untyped: 1 << 20 },
    Case { name: "yield then exit", entry: yield_forever_then_exit, expect: Death::Exited(9), budget: 2, untyped: 1 << 20 },
    Case { name: "memory accountant", entry: memory_accountant, expect: Death::Fault(14), budget: 50, untyped: ACCOUNTANT_BUDGET },
    Case { name: "untyped death", entry: untyped_death, expect: Death::Fault(14), budget: 50, untyped: 1 << 20 },
];
