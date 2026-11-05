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
const NX: u64 = 1 << 63;

/// What a user mapping may do. There is no writable-and-executable option.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Perm {
    /// Read and execute: code.
    Rx,
    /// Read and write, never execute: data and stacks.
    Rw,
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

    /// Map a fresh zeroed frame at `va` inside the user window. Every check
    /// runs before the frame is allocated, so a refused mapping costs nothing.
    pub fn map_user(&mut self, fa: &mut FrameAlloc, va: u64, perm: Perm) -> Option<u64> {
        if !(USER_BASE..USER_END).contains(&va) || !va.is_multiple_of(PAGE) {
            return None;
        }
        let idx = ((va - USER_BASE) / PAGE) as usize;
        let e = unsafe { (self.pt_user as *mut u64).add(idx) };
        if unsafe { *e } & P != 0 {
            return None;
        }
        let frame = fa.alloc()?;
        let bits = match perm {
            Perm::Rx => P | U,
            Perm::Rw => P | U | W | NX,
        };
        unsafe { *e = frame | bits };
        Some(frame)
    }

    /// Is `va` mapped in this space's user window?
    pub fn is_mapped(&self, va: u64) -> bool {
        if !(USER_BASE..USER_END).contains(&va) {
            return false;
        }
        let idx = ((va - USER_BASE) / PAGE) as usize;
        unsafe { *(self.pt_user as *const u64).add(idx) & P != 0 }
    }

    /// Copy `bytes` into the user window at `va`, mapping pages as needed.
    /// Code pages are written here through the kernel's identity map and are
    /// then read-only for the user.
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

/// Boot-time self-test of the allocation rule: a refused mapping (duplicate,
/// misaligned, outside the window) must not consume a frame.
pub fn self_test(fa: &mut FrameAlloc) -> bool {
    let Some(mut a) = AddressSpace::new(fa) else { return false };
    if a.map_user(fa, USER_BASE, Perm::Rw).is_none() {
        return false;
    }
    let before = fa.used;
    let refused = a.map_user(fa, USER_BASE, Perm::Rw).is_none()
        && a.map_user(fa, USER_BASE + 1, Perm::Rw).is_none()
        && a.map_user(fa, USER_END, Perm::Rw).is_none()
        && a.map_user(fa, 0x10_0000, Perm::Rx).is_none()
        && a.is_mapped(USER_BASE)
        && !a.is_mapped(USER_BASE + PAGE);
    refused && fa.used == before
}
