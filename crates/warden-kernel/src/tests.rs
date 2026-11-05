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
}

const SLOT_CONSOLE: u64 = 0; // Endpoint, WRITE only
const SLOT_DIR: u64 = 1; // Dir, READ | GRANT
const SLOT_FREE: u64 = 2;
const SLOT_UNTYPED: u64 = 3; // Untyped, 1 MiB
const SLOT_HOST: u64 = 4; // not held by any test thread
const SLOT_LOCK: u64 = 5; // slot with no cap and never will be
const SLOT_EP: u64 = 6; // Endpoint: server READ|WRITE|GRANT, client WRITE badge 42, eavesdropper WRITE badge 7
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
    (8, "retype 2 MiB from 1 MiB budget", E_BUDGET),
    (9, "host-exit via slot not held", E_INVALID),
    (10, "mint from cap without GRANT", E_RIGHTS),
    (11, "retype 1 MiB from 1 MiB budget (legit)", 0),
    (12, "retype 1 more byte", E_BUDGET),
    (13, "mint into empty slot 5 then invoke", 1),
    (14, "host-exit via console cap (WRITE, wrong object)", E_KIND),
    (15, "5000 mints into an occupied slot: all refused", 0),
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
        let r = sys!(4, SLOT_UNTYPED, 2u64 << 20, 0);
        sys!(0, SLOT_CONSOLE, 8, r);
        let r = sys!(7, SLOT_HOST, 0, 0);
        sys!(0, SLOT_CONSOLE, 9, r);
        let r = sys!(2, SLOT_CONSOLE, SLOT_LOCK, 0x1F);
        sys!(0, SLOT_CONSOLE, 10, r);
        let r = sys!(4, SLOT_UNTYPED, 1u64 << 20, 0);
        sys!(0, SLOT_CONSOLE, 11, r);
        let r = sys!(4, SLOT_UNTYPED, 1, 0);
        sys!(0, SLOT_CONSOLE, 12, r);
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
        }
        sys!(0, SLOT_CONSOLE, 15, bad);
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
    // EFER.SCE is off: the `syscall` instruction is an invalid opcode.
    unsafe { asm!("syscall", out("rcx") _, out("r11") _, options(nomem, nostack)) };
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

pub struct Case {
    pub name: &'static str,
    pub entry: extern "C" fn() -> !,
    pub expect: Death,
    pub budget: u32,
}

pub const CASES: &[Case] = &[
    Case { name: "capability attacks", entry: hostile_caps, expect: Death::Fault(14), budget: 50 },
    Case { name: "cli", entry: hostile_cli, expect: Death::Fault(13), budget: 50 },
    Case { name: "hlt", entry: hostile_hlt, expect: Death::Fault(13), budget: 50 },
    Case { name: "infinite loop", entry: hostile_spin, expect: Death::Budget, budget: 5 },
    Case { name: "out to debug-exit port", entry: hostile_outport, expect: Death::Fault(13), budget: 50 },
    Case { name: "int 0x81", entry: hostile_badvector, expect: Death::Fault(13), budget: 50 },
    Case { name: "read kernel memory", entry: hostile_readkernel, expect: Death::Fault(14), budget: 50 },
    Case { name: "jump into kernel code", entry: hostile_callkernel, expect: Death::Fault(14), budget: 50 },
    Case { name: "mov cr3", entry: hostile_cr3, expect: Death::Fault(13), budget: 50 },
    Case { name: "rdmsr", entry: hostile_rdmsr, expect: Death::Fault(13), budget: 50 },
    Case { name: "syscall instruction", entry: hostile_syscall_insn, expect: Death::Fault(6), budget: 50 },
    Case { name: "stack outside partition", entry: hostile_stack_outside, expect: Death::Fault(14), budget: 50 },
    Case { name: "write own code (W^X)", entry: hostile_write_code, expect: Death::Fault(14), budget: 50 },
    Case { name: "execute data page (NX)", entry: hostile_exec_data, expect: Death::Fault(14), budget: 50 },
    Case { name: "execute stack (NX)", entry: hostile_exec_stack, expect: Death::Fault(14), budget: 50 },
    Case { name: "flood sender 1", entry: flood_sender, expect: Death::Budget, budget: 50 },
    Case { name: "flood sender 2", entry: flood_sender, expect: Death::Budget, budget: 50 },
    Case { name: "flood sender 3", entry: flood_sender_last, expect: Death::Exited(0), budget: 50 },
    Case { name: "good citizen", entry: good_citizen, expect: Death::Exited(7), budget: 50 },
    Case { name: "ipc server", entry: ipc_server, expect: Death::Exited(0), budget: 50 },
    Case { name: "ipc client", entry: ipc_client, expect: Death::Exited(0), budget: 50 },
    Case { name: "ipc eavesdropper", entry: ipc_eavesdropper, expect: Death::Exited(0), budget: 50 },
    Case { name: "isolation writer", entry: iso_writer, expect: Death::Exited(0), budget: 50 },
    Case { name: "isolation reader", entry: iso_reader, expect: Death::Exited(0), budget: 50 },
];
