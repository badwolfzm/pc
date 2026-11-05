//! Physical frames and per-thread address spaces.
//!
//! Every thread gets its own page-table tree. The kernel's identity map of
//! the first 1 GiB is shared (supervisor-only, 2 MiB pages) by copying the
//! boot page directory; the user window `0x800000..0xA00000` is instead a
//! private page table of 4 KiB frames, so one thread's user memory is never
//! visible to another.

use core::ptr;

pub const PAGE: u64 = 4096;
pub const USER_BASE: u64 = 0x80_0000;
pub const USER_END: u64 = 0xA0_0000;

const P: u64 = 1; // present
const W: u64 = 1 << 1; // writable
const U: u64 = 1 << 2; // user
const HUGE: u64 = 1 << 7;
const NX: u64 = 1 << 63; // no-execute (EFER.NXE is set in boot.asm)

/// What a user page may be used for. W^X: no page is both writable and
/// executable, and the kernel never hands out a page that is neither.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Perm {
    /// Read and execute. Code lives here and can never be rewritten.
    Code,
    /// Read and write, never execute. Data pages, heaps and stacks.
    Data,
    /// Read only, never execute.
    RoData,
}

impl Perm {
    fn bits(self) -> u64 {
        match self {
            Perm::Code => 0,
            Perm::Data => W | NX,
            Perm::RoData => NX,
        }
    }
}

extern "C" {
    static boot_pd0: [u64; 512];
}

/// Bump allocator over physical memory above the kernel and the boot image.
pub struct FrameAlloc {
    next: u64,
    end: u64,
    pub used: u64,
}

impl FrameAlloc {
    pub fn new(start: u64, end: u64) -> Self {
        FrameAlloc { next: start, end, used: 0 }
    }
    /// A zeroed 4 KiB frame. Physical == virtual in the kernel identity map.
    pub fn alloc(&mut self) -> Option<u64> {
        if self.next + PAGE > self.end {
            return None;
        }
        let f = self.next;
        self.next += PAGE;
        self.used += 1;
        unsafe { ptr::write_bytes(f as *mut u8, 0, PAGE as usize) };
        Some(f)
    }
}

pub struct AddressSpace {
    pub cr3: u64,
    pt_user: u64,
}

impl AddressSpace {
    /// New space: shared kernel identity map, empty user window.
    pub fn new(fa: &mut FrameAlloc) -> Option<Self> {
        let pml4 = fa.alloc()?;
        let pdpt = fa.alloc()?;
        let pd = fa.alloc()?;
        let pt_user = fa.alloc()?;
        unsafe {
            *(pml4 as *mut u64) = pdpt | P | W | U;
            *(pdpt as *mut u64) = pd | P | W | U;
            let src = ptr::addr_of!(boot_pd0) as *const u64;
            for i in 0..512usize {
                let e = *src.add(i);
                // Kernel 2 MiB pages, supervisor only. Entry 4 is the user
                // window: point it at this space's own 4 KiB page table.
                *(pd as *mut u64).add(i) = if i == 4 { pt_user | P | W | U } else { e & !U };
            }
        }
        Some(AddressSpace { cr3: pml4, pt_user })
    }

    fn slot(&self, va: u64) -> Option<*mut u64> {
        if !(USER_BASE..USER_END).contains(&va) || !va.is_multiple_of(PAGE) {
            return None;
        }
        let idx = ((va - USER_BASE) / PAGE) as usize;
        Some(unsafe { (self.pt_user as *mut u64).add(idx) })
    }

    /// The page-table entry for `va`, or None when `va` is outside the user
    /// window, unaligned, or not mapped. For tests and loaders.
    pub fn pte(&self, va: u64) -> Option<u64> {
        let e = unsafe { *self.slot(va)? };
        if e & P != 0 { Some(e) } else { None }
    }

    /// Map a fresh zeroed frame at `va` inside the user window with `perm`.
    /// Refuses, allocating nothing, when `va` is outside the window,
    /// unaligned, or already mapped: the check comes before the allocation,
    /// so a hostile mapper cannot drain the frame pool by remapping.
    pub fn map_user(&mut self, fa: &mut FrameAlloc, va: u64, perm: Perm) -> Option<u64> {
        let e = self.slot(va)?;
        if unsafe { *e } & P != 0 {
            return None;
        }
        let frame = fa.alloc()?;
        unsafe { *e = frame | P | U | perm.bits() };
        Some(frame)
    }

    /// Copy `bytes` into the user window at `va`, mapping pages as needed.
    /// The copy goes through the kernel's identity map, so `perm` may be
    /// `Code` or `RoData`: the user side never gets a writable view.
    pub fn load(&mut self, fa: &mut FrameAlloc, va: u64, bytes: &[u8], perm: Perm) -> Option<()> {
        let mut off = 0usize;
        while off < bytes.len() {
            let page_va = (va + off as u64) & !(PAGE - 1);
            let frame = self.map_user(fa, page_va, perm)?;
            let in_page = ((va + off as u64) - page_va) as usize;
            let n = core::cmp::min(PAGE as usize - in_page, bytes.len() - off);
            unsafe { ptr::copy_nonoverlapping(bytes.as_ptr().add(off), (frame as usize + in_page) as *mut u8, n) };
            off += n;
        }
        Some(())
    }
}

/// Keep the HUGE flag name used; silences dead-code when not referenced.
#[allow(dead_code)]
const _: u64 = HUGE;

/// Kernel-side checks of the mapping rules, run once at boot before any
/// thread exists. Returns (checks, failures) and prints one line each.
pub fn self_test(fa: &mut FrameAlloc) -> (u32, u32) {
    let mut checks = 0u32;
    let mut fails = 0u32;
    let mut check = |name: &str, ok: bool| {
        checks += 1;
        if ok {
            crate::kprintln!("PASS mm: {name}");
        } else {
            fails += 1;
            crate::kprintln!("FAIL mm: {name}");
        }
    };
    let mut a = AddressSpace::new(fa).expect("page tables for mm self-test");
    let va = USER_BASE + 0x10_000;
    let before = fa.used;
    let first = a.map_user(fa, va, Perm::Data);
    check("first map of a free page succeeds and takes one frame", first.is_some() && fa.used == before + 1);
    let used = fa.used;
    let again = a.map_user(fa, va, Perm::Code);
    check("second map of the same page is refused", again.is_none());
    check("refused remap allocated no frame", fa.used == used);
    check("refused remap left the first mapping intact", a.pte(va).map(|e| e & !0xFFF & !NX) == first);
    let used = fa.used;
    check("map outside the window is refused without a frame", a.map_user(fa, USER_END, Perm::Data).is_none() && fa.used == used);
    check("map below the window is refused without a frame", a.map_user(fa, USER_BASE - PAGE, Perm::Data).is_none() && fa.used == used);
    check("unaligned map is refused without a frame", a.map_user(fa, va + 8, Perm::Data).is_none() && fa.used == used);
    check("load over an existing page is refused", a.load(fa, va, &[1, 2, 3], Perm::Code).is_none());
    // W^X on the entries themselves.
    let code = USER_BASE + 0x20_000;
    let data = USER_BASE + 0x21_000;
    let ro = USER_BASE + 0x22_000;
    a.map_user(fa, code, Perm::Code).expect("code");
    a.map_user(fa, data, Perm::Data).expect("data");
    a.map_user(fa, ro, Perm::RoData).expect("rodata");
    let (c, d, r) = (a.pte(code).unwrap(), a.pte(data).unwrap(), a.pte(ro).unwrap());
    check("code page is user, present, not writable, executable", c & (P | U | W | NX) == P | U);
    check("data page is user, present, writable, not executable", d & (P | U | W | NX) == P | U | W | NX);
    check("read-only data page is neither writable nor executable", r & (P | U | W | NX) == P | U | NX);
    check("no mapped page is both writable and executable", [c, d, r].iter().all(|e| !(e & W != 0 && e & NX == 0)));
    check("unmapped page reports no entry", a.pte(USER_BASE + 0x30_000).is_none());
    (checks, fails)
}
