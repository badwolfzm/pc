//! Threads, the trap handler, the syscall boundary and the scheduler.
//!
//! One CPU, one kernel stack. Every trap saves the whole register file into
//! the current thread's `TrapFrame`, the kernel decides, then `enter_user`
//! restores whichever thread runs next. The kernel never resumes a kernel
//! continuation, so there is nothing to preempt inside the kernel.

use crate::arch;
use crate::mm::{AddressSpace, FrameAlloc};
use alloc::collections::BTreeMap;
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
    fn enter_user(frame: *const TrapFrame, cr3: u64) -> !;
}

/// Four-word IPC message carried in registers (rsi, rdx, r10, r8).
#[derive(Clone, Copy, Default, Debug)]
pub struct Msg {
    pub badge: u64,
    pub w: [u64; 4],
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum State {
    Runnable,
    /// Waiting for a receiver on this endpoint object, holding the message.
    SendBlocked(u32),
    /// Waiting for a sender on this endpoint object.
    RecvBlocked(u32),
}

#[derive(Default)]
pub struct Endpoint {
    senders: Vec<usize>,
    receivers: Vec<usize>,
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
    pub aspace: AddressSpace,
    pub state: State,
    pub pending: Msg,
    /// Timer ticks left (10 ms each). Refilled on yield.
    pub budget: u32,
    pub budget_period: u32,
    pub death: Option<Death>,
    pub expect: Death,
    pub logs: u32,
}

pub struct Kernel {
    pub caps: CapTable,
    pub frames: FrameAlloc,
    pub endpoints: BTreeMap<u32, Endpoint>,
    pub threads: Vec<Thread>,
    pub current: usize,
    pub ticks: u64,
    pub failures: u32,
    pub checks: u32,
    /// Capability nodes after setup; tests may add exactly `expected_mints`.
    pub cap_nodes_baseline: usize,
    pub expected_mints: usize,
    /// Kernel heap in use after setup. The run may add only a little.
    pub heap_baseline: usize,
}

pub static mut KERNEL: Option<Kernel> = None;

fn k() -> &'static mut Kernel {
    unsafe { (*core::ptr::addr_of_mut!(KERNEL)).as_mut().expect("kernel not initialised") }
}

pub const USER_CS: u64 = 0x18 | 3;
pub const USER_SS: u64 = 0x20 | 3;

impl Kernel {
    pub fn spawn(&mut self, name: &'static str, entry: u64, stack_top: u64, budget: u32, expect: Death, aspace: AddressSpace) -> usize {
        let frame = TrapFrame {
            rip: entry, cs: USER_CS, rflags: 0x202, rsp: stack_top, ss: USER_SS, ..TrapFrame::default()
        };
        self.threads.push(Thread {
            name, frame, cspace: CSpace::new(8), aspace, state: State::Runnable, pending: Msg::default(),
            budget, budget_period: budget, death: None, expect, logs: 0,
        });
        self.threads.len() - 1
    }

    fn pick_next(&mut self) -> Option<usize> {
        let n = self.threads.len();
        (1..=n).map(|i| (self.current + i) % n).find(|&i| self.threads[i].death.is_none() && self.threads[i].state == State::Runnable)
    }

    fn any_alive(&self) -> bool {
        self.threads.iter().any(|t| t.death.is_none())
    }

    /// Deliver `msg` into a receiver's registers and make it runnable.
    fn deliver(&mut self, to: usize, msg: Msg) {
        let f = &mut self.threads[to].frame;
        f.rax = 0;
        f.rdi = msg.badge;
        f.rsi = msg.w[0];
        f.rdx = msg.w[1];
        f.r10 = msg.w[2];
        f.r8 = msg.w[3];
        self.threads[to].state = State::Runnable;
    }

    /// A thread died: drop it from any endpoint queue.
    fn unqueue(&mut self, idx: usize) {
        for ep in self.endpoints.values_mut() {
            ep.senders.retain(|&t| t != idx);
            ep.receivers.retain(|&t| t != idx);
        }
    }

    fn kill(&mut self, idx: usize, how: Death) {
        self.unqueue(idx);
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
                unsafe { enter_user(&self.threads[i].frame, self.threads[i].aspace.cr3) }
            }
            None if self.any_alive() => {
                // Everyone is blocked on IPC with nobody to pair with: a
                // deadlock. Report it as a budget death of each blocked thread.
                let blocked: Vec<usize> = (0..self.threads.len()).filter(|&i| self.threads[i].death.is_none()).collect();
                for i in blocked {
                    crate::kprintln!("  {} blocked forever in {:?}", self.threads[i].name, self.threads[i].state);
                    self.kill(i, Death::Budget);
                }
                crate::finish(self)
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
pub const SYS_SEND: u64 = 8; // (slot, w0, w1, w2) blocks until received -> 0
pub const SYS_RECV: u64 = 9; // (slot) blocks until sent -> rax=0, rdi=badge, rsi/rdx/r10/r8=words

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

/// What the syscall path does with the caller afterwards.
enum After {
    /// Resume the caller with rax = value.
    Resume(u64),
    /// The caller is blocked; run someone else.
    Block,
    /// The caller yielded; run someone else, rax = 0.
    Yield,
}

fn syscall(k: &mut Kernel, f: &TrapFrame) -> Result<After, Death> {
    let cur = k.current;
    let (a, b, c) = (f.rdi, f.rsi, f.rdx);
    match f.rax {
        SYS_SEND | SYS_RECV => return ipc(k, f),
        _ => {}
    }
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
            // No clone of the CSpace: a refused mint must not touch the heap.
            let cs = &mut k.threads[cur].cspace;
            match k.caps.mint_self(cs, a as usize, b as usize, Rights(c as u8 & 0x1F), 0) {
                Ok(()) => k.caps.invoke(cs, b as usize, Rights::NONE).map(|r| r.rights.bits() as u64).map_err(err),
                Err(e) => Err(err(e)),
            }
        }
        SYS_REVOKE => k.caps.revoke(&k.threads[cur].cspace, a as usize).map(|n| n as u64).map_err(err),
        SYS_RETYPE => k.caps.retype(&k.threads[cur].cspace, a as usize, b).map_err(err),
        SYS_YIELD => {
            k.threads[cur].budget = k.threads[cur].budget_period;
            return Ok(After::Yield);
        }
        SYS_EXIT => return Err(Death::Exited(a)),
        SYS_HOST_EXIT => {
            // The capability must name the machine-control object itself;
            // WRITE on some other object (the console) is not enough.
            match k.caps.invoke(&k.threads[cur].cspace, a as usize, Rights::WRITE) {
                Err(e) => return Ok(After::Resume(err(e))),
                Ok(r) if r.kind != ObjectKind::Host => return Ok(After::Resume(err(CapError::WrongKind))),
                Ok(_) => {}
            }
            crate::kprintln!("thread {} holds the host capability and stops the machine", k.threads[cur].name);
            arch::qemu_exit(b as u32)
        }
        _ => Err(-(100i64) as u64),
    };
    Ok(After::Resume(match r {
        Ok(v) => v,
        Err(e) => e,
    }))
}

/// Synchronous rendezvous IPC. SEND needs WRITE on the endpoint capability,
/// RECV needs READ. The sender's badge travels with the message, so a server
/// knows which grant a client is speaking through.
fn ipc(k: &mut Kernel, f: &TrapFrame) -> Result<After, Death> {
    let cur = k.current;
    let slot = f.rdi as usize;
    let sending = f.rax == SYS_SEND;
    let need = if sending { Rights::WRITE } else { Rights::READ };
    let r = match k.caps.invoke(&k.threads[cur].cspace, slot, need) {
        Ok(r) => r,
        Err(e) => return Ok(After::Resume(err(e))),
    };
    if r.kind != ObjectKind::Endpoint {
        return Ok(After::Resume(err(CapError::WrongKind)));
    }
    let ep_id = r.object.0;
    let ep = k.endpoints.entry(ep_id).or_default();
    if sending {
        let msg = Msg { badge: r.badge, w: [f.rsi, f.rdx, f.r10, f.r8] };
        if !ep.receivers.is_empty() {
            let to = ep.receivers.remove(0);
            k.deliver(to, msg);
            return Ok(After::Resume(0));
        }
        ep.senders.push(cur);
        k.threads[cur].pending = msg;
        k.threads[cur].state = State::SendBlocked(ep_id);
        // The sender resumes with rax = 0 once a receiver takes the message.
        k.threads[cur].frame.rax = 0;
        Ok(After::Block)
    } else {
        if !ep.senders.is_empty() {
            let from = ep.senders.remove(0);
            let msg = k.threads[from].pending;
            k.threads[from].state = State::Runnable;
            k.deliver(cur, msg);
            // deliver() wrote the message into cur's frame; resume it.
            return Ok(After::Resume(0));
        }
        ep.receivers.push(cur);
        k.threads[cur].state = State::RecvBlocked(ep_id);
        Ok(After::Block)
    }
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
            Ok(After::Resume(v)) => {
                // deliver() may have filled the frame; only rax is set here,
                // and for a completed RECV it is already 0.
                if !(f.rax == SYS_RECV && v == 0) {
                    k.threads[cur].frame.rax = v;
                }
                // Run-to-completion for the caller: resume it directly.
                k.current = cur;
                unsafe { enter_user(&k.threads[cur].frame, k.threads[cur].aspace.cr3) }
            }
            Ok(After::Yield) => k.threads[cur].frame.rax = 0,
            Ok(After::Block) => {}
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

pub fn mint_to(k: &mut Kernel, from: usize, from_slot: usize, to: usize, to_slot: usize, mask: Rights, badge: u64) {
    let src = k.threads[from].cspace.clone();
    let mut dst = k.threads[to].cspace.clone();
    k.caps.mint(&src, from_slot, &mut dst, to_slot, mask, badge).expect("mint");
    k.threads[to].cspace = dst;
}
