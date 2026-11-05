//! Physical frames and per-thread address spaces.
//!
//! Every thread gets its own page-table tree. The kernel's identity map of
//! the first 1 GiB is shared (supervisor-only, 2 MiB pages) by copying the
//! boot page directory. The user range `0x400000..0x2000000` (28 MiB, page
//! directory entries 2..16) is instead private to each space: a 4 KiB page
//! table per 2 MiB window, created on first use, so one thread's user memory
//! is never visible to another. Nothing of the kernel lives in that range
//! (the kernel image ends below 4 MiB and frames start at 32 MiB), so the
//! kernel's own view of physical memory is intact in every space.
//!
//! Native (test) threads use the window at 0x800000; Linux programs are
//! loaded at their link address (0x400000 for a non-PIE static binary) with
//! a heap after the program, an mmap area and a stack below 0x2000000.

use alloc::vec::Vec;
use core::ptr;

pub const PAGE: u64 = 4096;
pub const USER_BASE: u64 = 0x40_0000;
pub const USER_END: u64 = 0x200_0000;
const WINDOW: u64 = 2 << 20;

const P: u64 = 1; // present
const W: u64 = 1 << 1; // writable
const U: u64 = 1 << 2; // user
const HUGE: u64 = 1 << 7;
const NX: u64 = 1 << 63; // no-execute (EFER.NXE is set in boot.asm)
const ADDR: u64 = 0x000F_FFFF_FFFF_F000;

/// What a user page may be used for. W^X: no page is both writable and
/// executable, and the kernel never hands out a page that is neither.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Perm {
    /// Read and execute. Code lives here and can never be rewritten.
    Rx,
    /// Read and write, never execute. Data pages, heaps and stacks.
    Rw,
    /// Read only, never execute.
    Ro,
}

impl Perm {
    fn bits(self) -> u64 {
        match self {
            Perm::Rx => 0,
            Perm::Rw => W | NX,
            Perm::Ro => NX,
        }
    }
}

extern "C" {
    static boot_pd0: [u64; 512];
}

/// Physical frame allocator: a free list of returned frames in front of a
/// bump pointer over the memory above the kernel and the boot image.
pub struct FrameAlloc {
    start: u64,
    next: u64,
    end: u64,
    /// A physical range the allocator must step over (boot modules).
    skip: (u64, u64),
    free: Vec<u64>,
    /// Frames currently handed out (net).
    pub used: u64,
}

impl FrameAlloc {
    pub fn new(start: u64, end: u64) -> Self {
        FrameAlloc { start, next: start, end, skip: (0, 0), free: Vec::with_capacity(64), used: 0 }
    }
    /// Return a frame. The caller guarantees no page table maps it any more.
    pub fn free(&mut self, frame: u64) {
        debug_assert!(frame.is_multiple_of(PAGE));
        self.used -= 1;
        self.free.push(frame);
    }
    /// Frames ever taken from fresh memory (high-water mark).
    pub fn high_water(&self) -> u64 {
        (self.next - self.start) / PAGE
    }
    /// Never hand out frames inside `[start, end)`.
    pub fn reserve(&mut self, start: u64, end: u64) {
        self.skip = (start & !(PAGE - 1), (end + PAGE - 1) & !(PAGE - 1));
    }
    /// A zeroed 4 KiB frame. Physical == virtual in the kernel identity map.
    /// A frame freed earlier is reused before fresh memory is touched.
    pub fn alloc(&mut self) -> Option<u64> {
        let f = match self.free.pop() {
            Some(f) => f,
            None => {
                if self.next >= self.skip.0 && self.next < self.skip.1 {
                    self.next = self.skip.1;
                }
                if self.next + PAGE > self.end {
                    return None;
                }
                let f = self.next;
                self.next += PAGE;
                f
            }
        };
        self.used += 1;
        unsafe { ptr::write_bytes(f as *mut u8, 0, PAGE as usize) };
        Some(f)
    }
    /// `n` physically contiguous zeroed frames; the start address.
    pub fn alloc_contiguous(&mut self, n: u64) -> Option<u64> {
        let first = self.alloc()?;
        for i in 1..n {
            let f = self.alloc()?;
            if f != first + i * PAGE {
                return None;
            }
        }
        Some(first)
    }
}

pub struct AddressSpace {
    pub cr3: u64,
    pd: u64,
}

impl AddressSpace {
    /// New space: shared kernel identity map, empty user range.
    pub fn new(fa: &mut FrameAlloc) -> Option<Self> {
        let pml4 = fa.alloc()?;
        let pdpt = fa.alloc()?;
        let pd = fa.alloc()?;
        unsafe {
            *(pml4 as *mut u64) = pdpt | P | W | U;
            *(pdpt as *mut u64) = pd | P | W | U;
            let src = ptr::addr_of!(boot_pd0) as *const u64;
            for i in 0..512usize {
                let e = *src.add(i);
                // Kernel 2 MiB pages, supervisor only. The user windows start
                // empty; a page table appears on the first map into one.
                let user_window = (USER_BASE / WINDOW..USER_END / WINDOW).contains(&(i as u64));
                *(pd as *mut u64).add(i) = if user_window { 0 } else { e & !U };
            }
        }
        Some(AddressSpace { cr3: pml4, pd })
    }

    fn in_range(va: u64) -> bool {
        (USER_BASE..USER_END).contains(&va) && va.is_multiple_of(PAGE)
    }

    /// Pointer to the page-table entry for `va`. With `fa`, a missing page
    /// table is created; without, a missing table means None.
    fn slot(&self, fa: Option<&mut FrameAlloc>, va: u64) -> Option<*mut u64> {
        if !Self::in_range(va) {
            return None;
        }
        let pde = unsafe { (self.pd as *mut u64).add((va / WINDOW) as usize) };
        let mut e = unsafe { *pde };
        if e & P == 0 {
            let fa = fa?;
            let pt = fa.alloc()?;
            e = pt | P | W | U;
            unsafe { *pde = e };
        }
        let pt = e & ADDR;
        Some(unsafe { (pt as *mut u64).add(((va / PAGE) % 512) as usize) })
    }

    /// The page-table entry for `va`, or None when `va` is outside the user
    /// range, unaligned, or not mapped. For tests and loaders.
    pub fn pte(&self, va: u64) -> Option<u64> {
        let e = unsafe { *self.slot(None, va)? };
        if e & P != 0 { Some(e) } else { None }
    }

    /// Map a fresh zeroed frame at `va` inside the user range with `perm`.
    /// Refuses, allocating nothing, when `va` is outside the range,
    /// unaligned, or already mapped: the check comes before the allocation,
    /// so a hostile mapper cannot drain the frame pool by remapping. (The
    /// first map into an empty 2 MiB window also takes one page-table frame.)
    pub fn map_user(&mut self, fa: &mut FrameAlloc, va: u64, perm: Perm) -> Option<u64> {
        if !Self::in_range(va) {
            return None;
        }
        let e = self.slot(Some(fa), va)?;
        if unsafe { *e } & P != 0 {
            return None;
        }
        let frame = fa.alloc()?;
        unsafe { *e = frame | P | U | perm.bits() };
        Some(frame)
    }

    /// Map an existing frame at `va`. Fails if the address is outside the
    /// range or already mapped (the frame is then untouched and still the
    /// caller's to free).
    pub fn map_frame(&mut self, frame: u64, va: u64, perm: Perm) -> Option<()> {
        if !Self::in_range(va) {
            return None;
        }
        let e = self.slot(None, va)?;
        if unsafe { *e } & P != 0 {
            return None;
        }
        unsafe { *e = frame | P | U | perm.bits() };
        Some(())
    }

    /// Unmap `va`; returns the frame that was there. Flushes the TLB entry
    /// (the kernel also reloads CR3 on every return to user space).
    pub fn unmap(&mut self, va: u64) -> Option<u64> {
        match self.slot(None, va) {
            Some(e) if unsafe { *e } & P != 0 => unsafe {
                let frame = *e & ADDR;
                *e = 0;
                core::arch::asm!("invlpg [{}]", in(reg) va, options(nostack));
                Some(frame)
            },
            _ => None,
        }
    }

    /// Return every user page and every user page table of this space to
    /// the frame pool. The three top-level tables stay (the space may be the
    /// one currently loaded). Returns the frames freed.
    pub fn teardown(&mut self, fa: &mut FrameAlloc) -> u64 {
        let mut freed = 0;
        for w in USER_BASE / WINDOW..USER_END / WINDOW {
            let pde = unsafe { (self.pd as *mut u64).add(w as usize) };
            let e = unsafe { *pde };
            if e & P == 0 {
                continue;
            }
            let pt = e & ADDR;
            for i in 0..512usize {
                let pte = unsafe { (pt as *mut u64).add(i) };
                let v = unsafe { *pte };
                if v & P != 0 {
                    fa.free(v & ADDR);
                    freed += 1;
                    unsafe { *pte = 0 };
                }
            }
            unsafe { *pde = 0 };
            fa.free(pt);
            freed += 1;
        }
        freed
    }

    /// Change the permission of a mapped page. W^X is kept by construction:
    /// `Perm` has no writable-and-executable member.
    pub fn protect(&mut self, va: u64, perm: Perm) -> bool {
        match self.slot(None, va) {
            Some(e) if unsafe { *e } & P != 0 => {
                unsafe { *e = (*e & ADDR) | P | U | perm.bits() };
                true
            }
            _ => false,
        }
    }

    /// Copy `bytes` into the user range at `va`, mapping pages as needed.
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

    /// Physical address of the user byte at `va`, if it is mapped for the
    /// user with at least `need` (W for writes). The kernel then reaches it
    /// through its identity map, never through the user's own mapping.
    fn phys(&self, va: u64, need_write: bool) -> Option<u64> {
        let e = self.pte(va & !(PAGE - 1))?;
        if e & U == 0 || (need_write && e & W == 0) {
            return None;
        }
        Some((e & ADDR) + (va & (PAGE - 1)))
    }

    /// Copy `buf.len()` bytes from user memory at `va`. Fails (copying
    /// nothing further) when any byte is unmapped or not user-accessible.
    pub fn copy_from_user(&self, va: u64, buf: &mut [u8]) -> bool {
        let mut done = 0usize;
        while done < buf.len() {
            let cur = va.checked_add(done as u64).unwrap_or(u64::MAX);
            let Some(p) = self.phys(cur, false) else { return false };
            let n = core::cmp::min(PAGE as usize - (cur & (PAGE - 1)) as usize, buf.len() - done);
            unsafe { ptr::copy_nonoverlapping(p as *const u8, buf.as_mut_ptr().add(done), n) };
            done += n;
        }
        true
    }

    /// Copy `bytes` into user memory at `va`; every page must be writable.
    pub fn copy_to_user(&self, va: u64, bytes: &[u8]) -> bool {
        // Check the whole range first so a failure writes nothing.
        let mut off = 0u64;
        while off < bytes.len() as u64 {
            let cur = va.checked_add(off).unwrap_or(u64::MAX);
            if self.phys(cur, true).is_none() {
                return false;
            }
            off += PAGE - (cur & (PAGE - 1));
        }
        let mut done = 0usize;
        while done < bytes.len() {
            let cur = va + done as u64;
            let p = self.phys(cur, true).unwrap();
            let n = core::cmp::min(PAGE as usize - (cur & (PAGE - 1)) as usize, bytes.len() - done);
            unsafe { ptr::copy_nonoverlapping(bytes.as_ptr().add(done), p as *mut u8, n) };
            done += n;
        }
        true
    }

    /// Fill `len` bytes of user memory at `va` with zero.
    pub fn zero_user(&self, va: u64, len: u64) -> bool {
        let mut done = 0u64;
        while done < len {
            let cur = va.checked_add(done).unwrap_or(u64::MAX);
            let Some(p) = self.phys(cur, true) else { return false };
            let n = core::cmp::min(PAGE - (cur & (PAGE - 1)), len - done);
            unsafe { ptr::write_bytes(p as *mut u8, 0, n as usize) };
            done += n;
        }
        true
    }

    /// Read a NUL-terminated string of at most `max` bytes from user memory
    /// into `buf`; the length, or None when unmapped or unterminated.
    pub fn copy_str_from_user(&self, va: u64, buf: &mut [u8]) -> Option<usize> {
        for (i, b) in buf.iter_mut().enumerate() {
            let cur = va.checked_add(i as u64)?;
            let p = self.phys(cur, false)?;
            *b = unsafe { *(p as *const u8) };
            if *b == 0 {
                return Some(i);
            }
        }
        None
    }

    /// Physical address of the user byte at `va`, if that page is mapped.
    pub fn translate(&self, va: u64) -> Option<u64> {
        self.phys(va, false)
    }

    /// Copy `out.len()` bytes of this space's user memory at `va` into
    /// `out` through this space's own page table. False if any byte is
    /// unmapped; nothing is then promised about `out`.
    pub fn read_user(&self, va: u64, out: &mut [u8]) -> bool {
        self.copy_from_user(va, out)
    }

    /// Is `va` mapped in this space's user range?
    pub fn is_mapped(&self, va: u64) -> bool {
        self.pte(va & !(PAGE - 1)).is_some()
    }

    /// True when every page of `[va, va+len)` is mapped for the user.
    pub fn mapped(&self, va: u64, len: u64) -> bool {
        if len == 0 {
            return true;
        }
        let end = match va.checked_add(len - 1) {
            Some(e) => e,
            None => return false,
        };
        let mut p = va & !(PAGE - 1);
        while p <= end {
            if self.phys(p, false).is_none() {
                return false;
            }
            p += PAGE;
        }
        true
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
    let win = USER_BASE + WINDOW * 2; // 0x800000, the native window
    let va = win + 0x10_000;
    // The first map into an empty window also creates its page table.
    a.map_user(fa, va - PAGE, Perm::Rw).expect("page table");
    let before = fa.used;
    let first = a.map_user(fa, va, Perm::Rw);
    check("first map of a free page succeeds and takes one frame", first.is_some() && fa.used == before + 1);
    let used = fa.used;
    let again = a.map_user(fa, va, Perm::Rx);
    check("second map of the same page is refused", again.is_none());
    check("refused remap allocated no frame", fa.used == used);
    check("refused remap left the first mapping intact", a.pte(va).map(|e| e & ADDR) == first);
    let used = fa.used;
    check("map outside the range is refused without a frame", a.map_user(fa, USER_END, Perm::Rw).is_none() && fa.used == used);
    check("map below the range is refused without a frame", a.map_user(fa, USER_BASE - PAGE, Perm::Rw).is_none() && fa.used == used);
    check("map over the kernel image is refused without a frame", a.map_user(fa, 0x10_0000, Perm::Rw).is_none() && fa.used == used);
    check("unaligned map is refused without a frame", a.map_user(fa, va + 8, Perm::Rw).is_none() && fa.used == used);
    check("load over an existing page is refused", a.load(fa, va, &[1, 2, 3], Perm::Rx).is_none());
    // W^X on the entries themselves.
    let code = win + 0x20_000;
    let data = win + 0x21_000;
    let ro = win + 0x22_000;
    a.map_user(fa, code, Perm::Rx).expect("code");
    a.map_user(fa, data, Perm::Rw).expect("data");
    a.map_user(fa, ro, Perm::Ro).expect("rodata");
    let (c, d, r) = (a.pte(code).unwrap(), a.pte(data).unwrap(), a.pte(ro).unwrap());
    check("code page is user, present, not writable, executable", c & (P | U | W | NX) == P | U);
    check("data page is user, present, writable, not executable", d & (P | U | W | NX) == P | U | W | NX);
    check("read-only data page is neither writable nor executable", r & (P | U | W | NX) == P | U | NX);
    check("no mapped page is both writable and executable", [c, d, r].iter().all(|e| !(e & W != 0 && e & NX == 0)));
    check("unmapped page reports no entry", a.pte(win + 0x30_000).is_none());
    // Kernel copies honour the user's mapping, not the kernel's.
    check("copy_to_user into a read-only page is refused", !a.copy_to_user(ro, &[1]));
    check("copy_to_user into an unmapped page is refused", !a.copy_to_user(win + 0x30_000, &[1]));
    check("copy_from_user from kernel memory is refused", !a.copy_from_user(0x10_0000, &mut [0u8; 8]));
    let mut back = [0u8; 4];
    check("copy_to_user then copy_from_user round-trips", a.copy_to_user(data + 100, &[9, 8, 7, 6]) && a.copy_from_user(data + 100, &mut back) && back == [9, 8, 7, 6]);
    check("protect to read-only takes the write bit away", a.protect(data, Perm::Ro) && a.pte(data).unwrap() & W == 0);
    let data_frame = a.pte(data).unwrap() & ADDR;
    check("unmap removes the entry and returns the frame", a.unmap(data) == Some(data_frame) && a.pte(data).is_none());
    check("unmap of an unmapped page is refused", a.unmap(data).is_none());
    fa.free(data_frame);
    check("a freed frame is reused before fresh memory", fa.alloc() == Some(data_frame));
    // Linux program window: its link address is mappable, two windows apart.
    check("a page at the Linux link address 0x400000 maps", a.map_user(fa, 0x40_0000, Perm::Rx).is_some());
    check("a page just below the stack top maps", a.map_user(fa, USER_END - PAGE, Perm::Rw).is_some());
    let used = fa.used;
    let freed = a.teardown(fa);
    check("teardown returns every user page and page table", freed > 0 && fa.used + freed == used && a.pte(code).is_none() && a.pte(0x40_0000).is_none());
    (checks, fails)
}
