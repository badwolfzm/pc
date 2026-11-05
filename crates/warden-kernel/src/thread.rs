//! Threads, the trap handler, the syscall boundary and the scheduler.
//!
//! One CPU, one kernel stack. Every trap saves the whole register file into
//! the current thread's `TrapFrame`, the kernel decides, then `enter_user`
//! restores whichever thread runs next. The kernel never resumes a kernel
//! continuation, so there is nothing to preempt inside the kernel.

use crate::arch;
use alloc::vec::Vec;
use core::fmt;
use warden_caps::{CSpace, CapError, CapTable, ObjectKind, Rights};

#[repr(C)]
#[derive(Clone, Copy, Default, Debug)]
pub struct TrapFrame {
    pub r15: u64, pub r14: u64, pub r13: u64, pub r12: u64, pub r11: u64, pub r10: u64, pub r9: u64, pub r8: u64,
    pub rbp: u64, pub rdi: u64, pub rsi: u64, pub rdx: u64, pub rcx: u64, pub rbx: u64, pub rax: u64,
    pub vector: u64, pub error: u64,
    pub rip: u64, pub cs: u64, pub rflags: u64, pub rsp: u64, pub ss: u64,
}

extern "C" {
    fn enter_user(frame: *const TrapFrame) -> !;
}

/// Why a thread stopped. Compared against each test thread's expectation.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Death {
    Exited(u64),
    /// CPU exception: (vector, error code, faulting address for #PF).
    Fault(u8),
    /// Used its whole CPU budget without yielding.
    Budget,
}

impl fmt::Display for Death {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            Death::Exited(c) => write!(f, "exited({c})"),
            Death::Fault(6) => write!(f, "#UD invalid opcode"),
            Death::Fault(13) => write!(f, "#GP general protection"),
            Death::Fault(14) => write!(f, "#PF page fault"),
            Death::Fault(v) => write!(f, "exception {v}"),
            Death::Budget => write!(f, "budget exhausted"),
        }
    }
}

pub struct Thread {
    pub name: &'static str,
    pub frame: TrapFrame,
    pub cspace: CSpace,
    /// Timer ticks left (10 ms each). Refilled on yield.
    pub budget: u32,
    pub budget_period: u32,
    pub death: Option<Death>,
    pub expect: Death,
    pub logs: u32,
}

pub struct Kernel {
    pub caps: CapTable,
    pub threads: Vec<Thread>,
    pub current: usize,
    pub ticks: u64,
    pub failures: u32,
    pub checks: u32,
}

pub static mut KERNEL: Option<Kernel> = None;

fn k() -> &'static mut Kernel {
    unsafe { (*core::ptr::addr_of_mut!(KERNEL)).as_mut().expect("kernel not initialised") }
}

pub const USER_CS: u64 = 0x18 | 3;
pub const USER_SS: u64 = 0x20 | 3;

impl Kernel {
    pub fn spawn(&mut self, name: &'static str, entry: u64, stack_top: u64, budget: u32, expect: Death) -> usize {
        let frame = TrapFrame {
            rip: entry, cs: USER_CS, rflags: 0x202, rsp: stack_top, ss: USER_SS, ..TrapFrame::default()
        };
        self.threads.push(Thread {
            name, frame, cspace: CSpace::new(8), budget, budget_period: budget, death: None, expect, logs: 0,
        });
        self.threads.len() - 1
    }

    fn pick_next(&mut self) -> Option<usize> {
        let n = self.threads.len();
        (1..=n).map(|i| (self.current + i) % n).find(|&i| self.threads[i].death.is_none())
    }

    fn kill(&mut self, idx: usize, how: Death) {
        let t = &mut self.threads[idx];
        t.death = Some(how);
        let ok = how == t.expect;
        self.checks += 1;
        if !ok {
            self.failures += 1;
        }
        crate::kprintln!("{} thread {:<22} stopped: {} (expected {})", if ok { "PASS" } else { "FAIL" }, t.name, how, t.expect);
    }

    /// Run the next live thread, or finish when none is left.
    fn schedule(&mut self) -> ! {
        match self.pick_next() {
            Some(i) => {
                self.current = i;
                unsafe { enter_user(&self.threads[i].frame) }
            }
            None => crate::finish(self),
        }
    }
}

// ---- syscalls --------------------------------------------------------------
// rax = number, rdi/rsi/rdx/r10 = args, rax = result (negative = CapError).

pub const SYS_LOG: u64 = 0; // (console_slot, test_id, observed)
pub const SYS_INVOKE: u64 = 1; // (slot, rights) -> 0
pub const SYS_MINT: u64 = 2; // (src_slot, dst_slot, mask) -> rights obtained
pub const SYS_REVOKE: u64 = 3; // (slot) -> count revoked
pub const SYS_RETYPE: u64 = 4; // (slot, bytes) -> remaining budget
pub const SYS_YIELD: u64 = 5;
pub const SYS_EXIT: u64 = 6; // (code)
pub const SYS_HOST_EXIT: u64 = 7; // (host_slot, code): stop the machine

fn err(e: CapError) -> u64 {
    -(match e {
        CapError::InvalidSlot => 1i64,
        CapError::SlotOccupied => 2,
        CapError::InsufficientRights => 3,
        CapError::Revoked => 4,
        CapError::OutOfBudget => 5,
        CapError::WrongKind => 6,
    }) as u64
}

fn syscall(k: &mut Kernel, f: &TrapFrame) -> Result<u64, Death> {
    let cur = k.current;
    let (a, b, c) = (f.rdi, f.rsi, f.rdx);
    let r = match f.rax {
        SYS_LOG => {
            // Even logging needs a capability: the console endpoint with WRITE.
            k.caps.invoke(&k.threads[cur].cspace, a as usize, Rights::WRITE).map_err(err).map(|_| {
                k.threads[cur].logs += 1;
                crate::tests::judge(k, b, c);
                0
            })
        }
        SYS_INVOKE => k.caps.invoke(&k.threads[cur].cspace, a as usize, Rights(b as u8 & 0x1F)).map(|_| 0).map_err(err),
        SYS_MINT => {
            let mut dst = k.threads[cur].cspace.clone();
            let r = k.caps.mint(&k.threads[cur].cspace, a as usize, &mut dst, b as usize, Rights(c as u8 & 0x1F), 0);
            match r {
                Ok(()) => {
                    k.threads[cur].cspace = dst;
                    k.caps.invoke(&k.threads[cur].cspace, b as usize, Rights::NONE).map(|r| r.rights.bits() as u64).map_err(err)
                }
                Err(e) => Err(err(e)),
            }
        }
        SYS_REVOKE => k.caps.revoke(&k.threads[cur].cspace, a as usize).map(|n| n as u64).map_err(err),
        SYS_RETYPE => k.caps.retype(&k.threads[cur].cspace, a as usize, b).map_err(err),
        SYS_YIELD => {
            k.threads[cur].budget = k.threads[cur].budget_period;
            Ok(0)
        }
        SYS_EXIT => return Err(Death::Exited(a)),
        SYS_HOST_EXIT => {
            if let Err(e) = k.caps.invoke(&k.threads[cur].cspace, a as usize, Rights::WRITE) {
                return Ok(err(e));
            }
            crate::kprintln!("thread {} holds the host capability and stops the machine", k.threads[cur].name);
            arch::qemu_exit(b as u32)
        }
        _ => Err(-(100i64) as u64),
    };
    Ok(match r {
        Ok(v) => v,
        Err(e) => e,
    })
}

/// Rust side of every trap. Never returns.
#[no_mangle]
pub extern "C" fn trap(frame: *mut TrapFrame) -> ! {
    let f = unsafe { *frame };
    let k = k();
    let from_user = f.cs & 3 == 3;
    if !from_user {
        crate::kprintln!("KERNEL FAULT vector={} error={:#x} rip={:#x}", f.vector, f.error, f.rip);
        if f.vector == 14 {
            let cr2: u64;
            unsafe { core::arch::asm!("mov {}, cr2", out(reg) cr2) };
            crate::kprintln!("  cr2={cr2:#x}");
        }
        arch::qemu_exit(2);
    }
    let cur = k.current;
    k.threads[cur].frame = f;
    match f.vector {
        arch::VEC_TIMER => {
            k.ticks += 1;
            arch::timer_eoi();
            let t = &mut k.threads[cur];
            t.budget = t.budget.saturating_sub(1);
            if t.budget == 0 {
                k.kill(cur, Death::Budget);
            }
            // Time slice: always rotate.
        }
        arch::VEC_SYSCALL => match syscall(k, &f) {
            Ok(v) => {
                k.threads[cur].frame.rax = v;
                if f.rax != SYS_YIELD {
                    // Run-to-completion for the caller: resume it directly.
                    k.current = cur;
                    unsafe { enter_user(&k.threads[cur].frame) }
                }
            }
            Err(d) => k.kill(cur, d),
        },
        v => {
            if v == 14 {
                let cr2: u64;
                unsafe { core::arch::asm!("mov {}, cr2", out(reg) cr2) };
                crate::kprintln!("  {} faulted at rip={:#x} addr={:#x} err={:#x}", k.threads[cur].name, f.rip, cr2, f.error);
            } else {
                crate::kprintln!("  {} faulted at rip={:#x} err={:#x}", k.threads[cur].name, f.rip, f.error);
            }
            k.kill(cur, Death::Fault(v as u8));
        }
    }
    k.schedule()
}

pub fn start() -> ! {
    let k = k();
    k.current = k.threads.len() - 1;
    k.schedule()
}


pub fn root_cap(k: &mut Kernel, thread: usize, slot: usize, kind: ObjectKind, rights: Rights, budget: u64) {
    let t = &mut k.threads[thread];
    k.caps.create_root(&mut t.cspace, slot, kind, rights, budget).expect("root cap");
}
