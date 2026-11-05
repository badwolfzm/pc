//! Static ELF loader: `warden-elf` validates, this module maps.
//!
//! Nothing is allocated until the whole image has been accepted, so a
//! refused image costs no frame. Segment permissions follow the program
//! headers under W^X: `PF_W` gives `Rw`, `PF_X` gives `Rx`, anything else
//! (read-only data) gives `Ro`. A segment that asks for both W and X is
//! refused by the parser.
//!
//! Two windows use it: native Warden-ABI programs load below the IPC buffer
//! (`LOAD_BASE..LOAD_END`), Linux programs at their link address
//! (`linux::IMAGE_LO..linux::IMAGE_HI`) into an address space of their own.

use crate::mm::{AddressSpace, FrameAlloc, Perm, PAGE};
use warden_elf::{ElfError, Image};

/// Where native ELF segments may live: the user window below the IPC buffer.
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

/// What `load_into` learned about the image, for the Linux initial stack.
#[derive(Clone, Copy, Debug, Default)]
pub struct Placed {
    pub entry: u64,
    pub pages: u64,
    /// First page after the highest segment: where a Linux heap starts.
    pub end: u64,
    /// User address of the program header table (AT_PHDR), 0 if it is not
    /// inside a loaded segment.
    pub phdr: u64,
    pub phnum: u64,
}

/// Validate `bytes` against the window `lo..hi` and map its segments into
/// `aspace`. Every check runs before the first frame is taken.
pub fn load_into(fa: &mut FrameAlloc, aspace: &mut AddressSpace, bytes: &[u8], lo: u64, hi: u64) -> Result<Placed, LoadError> {
    let img: Image = warden_elf::parse(bytes, lo, hi).map_err(LoadError::Elf)?;
    let mut end = lo;
    for seg in img.segments() {
        let perm = if seg.writable() {
            Perm::Rw
        } else if seg.executable() {
            Perm::Rx
        } else {
            Perm::Ro
        };
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
        end = end.max(seg.end_page());
    }
    // The program header table, if a segment carries it (static glibc and
    // musl read it through AT_PHDR for TLS and IRELATIVE relocations).
    let phoff = u64::from_le_bytes(bytes[32..40].try_into().unwrap());
    let phnum = u16::from_le_bytes(bytes[56..58].try_into().unwrap()) as u64;
    let phdr = img
        .segments()
        .iter()
        .find(|s| phoff >= s.file_off as u64 && phoff + phnum * 56 <= (s.file_off + s.filesz) as u64)
        .map(|s| s.vaddr + (phoff - s.file_off as u64))
        .unwrap_or(0);
    Ok(Placed { entry: img.entry, pages: img.pages(), end, phdr, phnum })
}

/// Validate `bytes` and build a fresh address space holding it, in the
/// native window.
pub fn load(fa: &mut FrameAlloc, bytes: &[u8]) -> Result<Loaded, LoadError> {
    // Parse first so a refused image never costs the page tables either.
    warden_elf::parse(bytes, LOAD_BASE, LOAD_END).map_err(LoadError::Elf)?;
    let mut aspace = AddressSpace::new(fa).ok_or(LoadError::NoMemory)?;
    let p = load_into(fa, &mut aspace, bytes, LOAD_BASE, LOAD_END)?;
    Ok(Loaded { aspace, entry: p.entry, pages: p.pages })
}
