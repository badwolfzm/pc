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
    // Root of the personality endpoint: queue room for every client, native
    // and Linux, and the server.
    let n_clients = APPS.len() + crate::linux::N_APPS;
    let ep = k.caps.create_root(&mut boot, 0, warden_caps::ObjectKind::Endpoint, Rights::ALL, 0).expect("root");
    k.endpoints.insert(ep.0, thread::Endpoint::with_capacity(n_clients + 1));

    let srv_img = loader::load(&mut k.frames, PERSONALITY).expect("personality server image");
    let mut srv_as = srv_img.aspace;
    for i in 0..STACK_PAGES {
        srv_as.map_user(&mut k.frames, STACK_TOP - (i + 1) * PAGE, Perm::Rw).expect("server stack");
    }
    let srv = k.spawn("personality server", srv_img.entry, STACK_TOP, 50, Death::Exited(0), srv_as);
    thread::root_endpoint(k, srv, SLOT_CONSOLE, Rights::WRITE, 1);
    grant(k, &boot, 0, srv, SLOT_PERSONALITY, Rights::READ, 0);
    let srv_charge = loaded_bytes(srv_img.pages, 0);
    thread::root_cap(k, srv, SLOT_UNTYPED, warden_caps::ObjectKind::Untyped, Rights::ALL, PROGRAM_UNTYPED - srv_charge);

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
        // Reply endpoint: root in the boot CSpace, READ to the app, WRITE to the server.
        let rep = k.caps.create_root(&mut boot, 1 + i, warden_caps::ObjectKind::Endpoint, Rights::ALL, 0).expect("root");
        k.endpoints.insert(rep.0, thread::Endpoint::with_capacity(2));
        grant(k, &boot, 1 + i, t, SLOT_REPLY, Rights::READ, 0);
        grant(k, &boot, 1 + i, srv, SRV_SLOT_REPLY0 + i, Rights::WRITE, 0);
        grant(k, &boot, 0, t, SLOT_PERSONALITY, Rights::WRITE, badge);
        // Phase 3 rule: memory is paid for. The frames the loader just
        // took come out of the program's untyped before it runs.
        let charge = loaded_bytes(img.pages, BUF_SIZE / PAGE);
        thread::root_cap(k, t, SLOT_UNTYPED, warden_caps::ObjectKind::Untyped, Rights::ALL, PROGRAM_UNTYPED - charge);
        if app.console {
            thread::root_endpoint(k, t, SLOT_CONSOLE, Rights::WRITE, 1);
        }
        crate::kprintln!("loaded {:<10} {} KiB image, {} pages, entry {:#x}, badge {}, {} KiB charged to its untyped, {} KiB left",
            app.name, app.image.len() / 1024, img.pages, img.entry, badge, charge / 1024, (PROGRAM_UNTYPED - charge) / 1024);
    }
    crate::kprintln!("loaded personality server: {} KiB image, {} pages, entry {:#x}, {} KiB charged, {} KiB left",
        PERSONALITY.len() / 1024, srv_img.pages, srv_img.entry, srv_charge / 1024, (PROGRAM_UNTYPED - srv_charge) / 1024);
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
