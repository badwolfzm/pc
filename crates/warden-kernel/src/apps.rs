//! Phase 2b: two isolated applications and a hostile third, served by a
//! user-space Linux personality server, all loaded from static ELF images
//! built from `user/` at compile time.
//!
//! Layout and slot numbers must agree with `user/warden_abi.h`.
//!
//! Capabilities (nothing is granted that the program does not need):
//! - personality server: slot 0 console (WRITE), slot 1 its endpoint (READ),
//!   slots 4..6 WRITE on each client's reply endpoint;
//! - httpd / fetch: slot 1 WRITE on the personality endpoint badged 1 / 2,
//!   slot 2 READ on their own reply endpoint, slot 3 a 64 KiB untyped budget;
//!   no console: their only output is write(1) through the server;
//! - rogue: the same as an app plus the console (badge 3), so it can report
//!   what it observed with SYS_LOG.
//!
//! Each client's 16 KiB IPC buffer is mapped in the client at APP_BUF and in
//! the server at SRV_BUF_BASE + i*BUF_SIZE; no client sees another's.

use crate::loader;
use crate::mm::{Perm, PAGE};
use crate::thread::{self, Death, Kernel};
use alloc::vec::Vec;
use warden_caps::{CSpace, Rights};

const PERSONALITY: &[u8] = include_bytes!(concat!(env!("OUT_DIR"), "/personality.elf"));
const HTTPD: &[u8] = include_bytes!(concat!(env!("OUT_DIR"), "/httpd.elf"));
const FETCH: &[u8] = include_bytes!(concat!(env!("OUT_DIR"), "/fetch.elf"));
const ROGUE: &[u8] = include_bytes!(concat!(env!("OUT_DIR"), "/rogue.elf"));

pub const APP_BUF: u64 = 0x90_0000;
pub const BUF_SIZE: u64 = 0x4000;
pub const SRV_BUF_BASE: u64 = 0x98_0000;
pub const STACK_TOP: u64 = 0xA0_0000;
const STACK_PAGES: u64 = 16;

const SLOT_CONSOLE: usize = 0;
const SLOT_PERSONALITY: usize = 1;
const SLOT_REPLY: usize = 2;
const SLOT_UNTYPED: usize = 3;
const SRV_SLOT_REPLY0: usize = 4;
/// Boot-time scratch slot for a freshly created endpoint's full-rights root;
/// emptied again before the thread runs.
const SLOT_SCRATCH: usize = 15;
/// Untyped memory each program starts with. The loader charges the image,
/// the stack, the IPC buffer and the four page-table frames against it
/// before the thread runs; what is left is what the program may retype.
const PROGRAM_UNTYPED: u64 = 1 << 20;
const PAGE_TABLE_FRAMES: u64 = 4;

/// Bytes the loader took for a program, to be charged to its untyped.
fn loaded_bytes(image_pages: u64, buffer_pages: u64) -> u64 {
    (image_pages + STACK_PAGES + buffer_pages + PAGE_TABLE_FRAMES) * PAGE
}

struct App {
    name: &'static str,
    image: &'static [u8],
    expect: Death,
    console: bool,
}

const APPS: &[App] = &[
    App { name: "app httpd", image: HTTPD, expect: Death::Exited(0), console: false },
    App { name: "app fetch", image: FETCH, expect: Death::Exited(0), console: false },
    App { name: "app rogue", image: ROGUE, expect: Death::Fault(14), console: true },
];

fn check(k: &mut Kernel, name: &str, ok: bool) {
    k.checks += 1;
    if ok {
        crate::kprintln!("PASS loader: {name}");
    } else {
        k.failures += 1;
        crate::kprintln!("FAIL loader: {name}");
    }
}

/// Loader gate: images the kernel must refuse, each costing no frame.
fn loader_checks(k: &mut Kernel) {
    let before = k.frames.used;
    let mut bad = Vec::from(HTTPD);
    // First program header's p_vaddr -> kernel memory.
    let phoff = u64::from_le_bytes(bad[32..40].try_into().unwrap()) as usize;
    bad[phoff + 16..phoff + 24].copy_from_slice(&0x10_0000u64.to_le_bytes());
    let r = loader::load(&mut k.frames, &bad).err();
    check(k, "segment in kernel memory refused", r == Some(loader::LoadError::Elf(warden_elf::ElfError::OutsideWindow)));
    let mut bad = Vec::from(HTTPD);
    bad[phoff + 16..phoff + 24].copy_from_slice(&(APP_BUF - 0x1000).to_le_bytes()); // would reach into the IPC buffer
    let r = loader::load(&mut k.frames, &bad).err();
    check(k, "segment reaching the IPC buffer refused", r == Some(loader::LoadError::Elf(warden_elf::ElfError::OutsideWindow)));
    let mut bad = Vec::from(HTTPD);
    bad[phoff + 4..phoff + 8].copy_from_slice(&7u32.to_le_bytes()); // RWX
    let r = loader::load(&mut k.frames, &bad).err();
    check(k, "writable and executable segment refused", r == Some(loader::LoadError::Elf(warden_elf::ElfError::WritableAndExecutable)));
    let r = loader::load(&mut k.frames, &HTTPD[..HTTPD.len() / 2]).err();
    check(k, "truncated image refused", matches!(r, Some(loader::LoadError::Elf(_))));
    let mut bad = Vec::from(HTTPD);
    bad[18] = 0xB7; // AArch64
    let r = loader::load(&mut k.frames, &bad).err();
    check(k, "foreign machine refused", r == Some(loader::LoadError::Elf(warden_elf::ElfError::WrongMachine)));
    check(k, "five refused images allocated no frame", k.frames.used == before);
}

// The roots of the personality and reply endpoints live in a boot-time
// CSpace that no thread ever sees, and every program gets only the
// attenuated copy it needs (thread::grant).
use crate::thread::grant;

pub fn setup(k: &mut Kernel) {
    loader_checks(k);

    let mut boot = CSpace::new(16);
    let n_clients = APPS.len() + crate::linux::N_APPS;

    let srv_img = loader::load(&mut k.frames, PERSONALITY).expect("personality server image");
    let mut srv_as = srv_img.aspace;
    for i in 0..STACK_PAGES {
        srv_as.map_user(&mut k.frames, STACK_TOP - (i + 1) * PAGE, Perm::Rw).expect("server stack");
    }
    let srv = k.spawn("personality server", srv_img.entry, STACK_TOP, 50, Death::Exited(0), srv_as);
    thread::root_endpoint(k, srv, SLOT_CONSOLE, Rights::WRITE, 1);
    let srv_charge = loaded_bytes(srv_img.pages, 0);
    thread::root_cap(k, srv, SLOT_UNTYPED, warden_caps::ObjectKind::Untyped, Rights::ALL, PROGRAM_UNTYPED - srv_charge);
    // The personality endpoint is the server's object: created from, and
    // paid for by, the server's own untyped memory, queues included (room
    // for every client, native and Linux, plus the server). The server keeps
    // only READ; a WRITE|GRANT copy in the boot CSpace hands out the badged
    // client copies and is deleted below.
    let ep_capacity = (n_clients + 1) as u64;
    let before = (k.caps.budget(&k.threads[srv].cspace, SLOT_UNTYPED).unwrap(), k.endpoints.len(), k.caps.live_objects());
    let refused = thread::create_endpoint(k, srv, SLOT_UNTYPED, SLOT_SCRATCH, warden_caps::ENDPOINT_MAX_QUEUE + 1);
    check(k, "pricing: personality endpoint with an over-budget queue refused, nothing charged",
        refused == Err(warden_caps::CapError::OutOfBudget)
            && before == (k.caps.budget(&k.threads[srv].cspace, SLOT_UNTYPED).unwrap(), k.endpoints.len(), k.caps.live_objects()));
    thread::create_endpoint(k, srv, SLOT_UNTYPED, SLOT_SCRATCH, ep_capacity).expect("personality endpoint");
    let paid = before.0 - k.caps.budget(&k.threads[srv].cspace, SLOT_UNTYPED).unwrap();
    check(k, "pricing: personality endpoint cost its queues plus one node from the server's untyped",
        paid == warden_caps::endpoint_bytes(ep_capacity) + warden_caps::NODE_BYTES);
    k.caps.mint_self(&mut k.threads[srv].cspace, SLOT_SCRATCH, SLOT_PERSONALITY, Rights::READ, 0).expect("server READ copy");
    let srv_cs = k.threads[srv].cspace.clone();
    k.caps.mint(&srv_cs, SLOT_SCRATCH, &mut boot, 0, Rights::WRITE.union(Rights::GRANT), 0).expect("boot copy");
    k.caps.delete(&mut k.threads[srv].cspace, SLOT_SCRATCH).expect("drop scratch root");

    for (i, app) in APPS.iter().enumerate() {
        let badge = i as u64 + 1;
        let img = loader::load(&mut k.frames, app.image).expect("application image");
        let mut a = img.aspace;
        for p in 0..STACK_PAGES {
            a.map_user(&mut k.frames, STACK_TOP - (p + 1) * PAGE, Perm::Rw).expect("app stack");
        }
        // The IPC buffer: fresh frames in the app, the same frames in the server.
        for p in 0..BUF_SIZE / PAGE {
            let frame = a.map_user(&mut k.frames, APP_BUF + p * PAGE, Perm::Rw).expect("app buffer");
            k.threads[srv].aspace.map_frame(frame, SRV_BUF_BASE + i as u64 * BUF_SIZE + p * PAGE, Perm::Rw).expect("server view of buffer");
        }
        let t = k.spawn(app.name, img.entry, STACK_TOP, 50, app.expect, a);
        // Phase 3 rule: memory is paid for. The frames the loader just
        // took come out of the program's untyped before it runs.
        let charge = loaded_bytes(img.pages, BUF_SIZE / PAGE);
        thread::root_cap(k, t, SLOT_UNTYPED, warden_caps::ObjectKind::Untyped, Rights::ALL, PROGRAM_UNTYPED - charge);
        // Reply endpoint: the app's own object, paid from its untyped (two
        // queue slots). The app keeps READ, the server gets WRITE, and the
        // full-rights root is dropped so neither side can widen it.
        let budget_before = k.caps.budget(&k.threads[t].cspace, SLOT_UNTYPED).unwrap();
        thread::create_endpoint(k, t, SLOT_UNTYPED, SLOT_SCRATCH, 2).expect("reply endpoint");
        let paid = budget_before - k.caps.budget(&k.threads[t].cspace, SLOT_UNTYPED).unwrap();
        check(k, "pricing: reply endpoint cost its queues plus one node from the app's untyped",
            paid == warden_caps::endpoint_bytes(2) + warden_caps::NODE_BYTES);
        k.caps.mint_self(&mut k.threads[t].cspace, SLOT_SCRATCH, SLOT_REPLY, Rights::READ, 0).expect("app READ copy");
        thread::mint_to(k, t, SLOT_SCRATCH, srv, SRV_SLOT_REPLY0 + i, Rights::WRITE, 0);
        k.caps.delete(&mut k.threads[t].cspace, SLOT_SCRATCH).expect("drop scratch root");
        grant(k, &boot, 0, t, SLOT_PERSONALITY, Rights::WRITE, badge);
        if app.console {
            thread::root_endpoint(k, t, SLOT_CONSOLE, Rights::WRITE, 1);
        }
        let left = k.caps.budget(&k.threads[t].cspace, SLOT_UNTYPED).unwrap();
        crate::kprintln!("loaded {:<10} {} KiB image, {} pages, entry {:#x}, badge {}, {} KiB charged to its untyped, {} bytes for its reply endpoint, {} KiB left",
            app.name, app.image.len() / 1024, img.pages, img.entry, badge, charge / 1024, paid, left / 1024);
    }
    crate::kprintln!("loaded personality server: {} KiB image, {} pages, entry {:#x}, {} KiB charged, {} bytes for its endpoint, {} KiB left",
        PERSONALITY.len() / 1024, srv_img.pages, srv_img.entry, srv_charge / 1024, paid, k.caps.budget(&k.threads[srv].cspace, SLOT_UNTYPED).unwrap() / 1024);
    // The Linux programs: real static binaries on the `syscall` ABI, served
    // by the same personality server through the same protocol.
    crate::linux::setup(k, &mut boot, srv);
    // The boot CSpace's roots are gone now: every endpoint object lives
    // exactly as long as the programs holding a capability to it, so the
    // end-of-run check "nothing survives its holders" covers them too.
    for slot in 0..boot.len() {
        let _ = k.caps.delete(&mut boot, slot);
    }
}
