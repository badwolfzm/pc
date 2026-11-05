//! Static ELF loader: `warden-elf` validates, this module maps.
//!
//! Nothing is allocated until the whole image has been accepted, so a
//! refused image costs no frame. Segment permissions follow the program
//! headers under W^X: `PF_W` gives `Rw`, anything else gives `Rx` (a
//! read-only data segment is mapped executable-but-unwritable for now; the
//! page table has no read-only-no-execute variant yet, see mm::Perm).

use crate::mm::{AddressSpace, FrameAlloc, Perm, PAGE};
use warden_elf::{ElfError, Image};

/// Where ELF segments may live: the user window below the IPC buffer.
pub const LOAD_BASE: u64 = 0x80_0000;
pub const LOAD_END: u64 = 0x90_0000;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum LoadError {
    Elf(ElfError),
    /// Out of physical frames.
    NoMemory,
}

/// A loaded program: its address space and entry point.
pub struct Loaded {
    pub aspace: AddressSpace,
    pub entry: u64,
    pub pages: u64,
}

/// Validate `bytes` and build a fresh address space holding it.
pub fn load(fa: &mut FrameAlloc, bytes: &[u8]) -> Result<Loaded, LoadError> {
    let img: Image = warden_elf::parse(bytes, LOAD_BASE, LOAD_END).map_err(LoadError::Elf)?;
    let mut aspace = AddressSpace::new(fa).ok_or(LoadError::NoMemory)?;
    for seg in img.segments() {
        let perm = if seg.writable() { Perm::Rw } else { Perm::Rx };
        let mut va = seg.first_page();
        while va < seg.end_page() {
            // The parser rejected page overlaps, so every page here is new.
            let frame = aspace.map_user(fa, va, perm).ok_or(LoadError::NoMemory)?;
            // File bytes that fall in this page; the rest stays zero (bss).
            let page_lo = va.max(seg.vaddr);
            let page_hi = (va + PAGE).min(seg.vaddr + seg.filesz as u64);
            if page_lo < page_hi {
                let src = &bytes[seg.file_off + (page_lo - seg.vaddr) as usize..seg.file_off + (page_hi - seg.vaddr) as usize];
                let dst = (frame + (page_lo - va)) as *mut u8;
                unsafe { core::ptr::copy_nonoverlapping(src.as_ptr(), dst, src.len()) };
            }
            va += PAGE;
        }
    }
    Ok(Loaded { aspace, entry: img.entry, pages: img.pages() })
}
