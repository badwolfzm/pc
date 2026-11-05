//! Static ELF64 parsing for the Warden loader.
//!
//! The kernel maps a program only after this module has accepted the whole
//! image: every check here runs before a single frame is allocated, so a
//! refused image costs nothing. The parser is `no_std`, has no `unsafe` and
//! allocates nothing; it reads the bytes it is given and nothing else.
//!
//! Accepted images: ELF64, little-endian, x86-64, `ET_EXEC` (static,
//! non-PIE), at most [`MAX_SEGMENTS`] `PT_LOAD` segments that lie inside the
//! caller's window, do not share pages with each other, are never both
//! writable and executable, and whose file bytes lie inside the image. The
//! entry point must fall in an executable segment.
#![no_std]
#![forbid(unsafe_code)]

pub const MAX_SEGMENTS: usize = 8;
pub const PAGE: u64 = 4096;

pub const PF_X: u32 = 1;
pub const PF_W: u32 = 2;
pub const PF_R: u32 = 4;

const PT_LOAD: u32 = 1;
const ET_EXEC: u16 = 2;
const EM_X86_64: u16 = 0x3E;
const EHDR_SIZE: usize = 64;
const PHDR_SIZE: usize = 56;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ElfError {
    /// Shorter than an ELF header, or a header or segment points past the end.
    Truncated,
    BadMagic,
    /// Not ELF64 little-endian.
    BadClass,
    /// Not ET_EXEC (dynamic and PIE images are not supported yet).
    NotExecutable,
    /// Not x86-64.
    WrongMachine,
    /// e_phentsize is not 56, or there are more PT_LOAD segments than MAX_SEGMENTS.
    BadProgramHeaders,
    /// A segment lies outside the window the caller allows, or wraps around.
    OutsideWindow,
    /// p_filesz > p_memsz.
    BadSegmentSize,
    /// A segment is both writable and executable.
    WritableAndExecutable,
    /// Two segments touch the same page.
    Overlap,
    /// The entry point is not inside an executable segment.
    BadEntry,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Default)]
pub struct Segment {
    pub vaddr: u64,
    pub memsz: u64,
    pub file_off: usize,
    pub filesz: usize,
    pub flags: u32,
}

impl Segment {
    pub fn first_page(&self) -> u64 {
        self.vaddr & !(PAGE - 1)
    }
    /// One past the last page (exclusive, page-aligned).
    pub fn end_page(&self) -> u64 {
        (self.vaddr + self.memsz).div_ceil(PAGE) * PAGE
    }
    pub fn writable(&self) -> bool {
        self.flags & PF_W != 0
    }
    pub fn executable(&self) -> bool {
        self.flags & PF_X != 0
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Image {
    pub entry: u64,
    pub segments: [Segment; MAX_SEGMENTS],
    pub count: usize,
}

impl Image {
    pub fn segments(&self) -> &[Segment] {
        &self.segments[..self.count]
    }
    /// Number of 4 KiB pages the image occupies once loaded.
    pub fn pages(&self) -> u64 {
        self.segments().iter().map(|s| (s.end_page() - s.first_page()) / PAGE).sum()
    }
}

fn u16_at(b: &[u8], off: usize) -> Result<u16, ElfError> {
    let s = b.get(off..off + 2).ok_or(ElfError::Truncated)?;
    Ok(u16::from_le_bytes([s[0], s[1]]))
}
fn u32_at(b: &[u8], off: usize) -> Result<u32, ElfError> {
    let s = b.get(off..off + 4).ok_or(ElfError::Truncated)?;
    Ok(u32::from_le_bytes([s[0], s[1], s[2], s[3]]))
}
fn u64_at(b: &[u8], off: usize) -> Result<u64, ElfError> {
    let s = b.get(off..off + 8).ok_or(ElfError::Truncated)?;
    Ok(u64::from_le_bytes([s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7]]))
}

/// Validate `bytes` as a static x86-64 executable whose loadable segments
/// all lie in `[lo, hi)`. Nothing is copied or mapped.
pub fn parse(bytes: &[u8], lo: u64, hi: u64) -> Result<Image, ElfError> {
    if bytes.len() < EHDR_SIZE {
        return Err(ElfError::Truncated);
    }
    if &bytes[0..4] != b"\x7fELF" {
        return Err(ElfError::BadMagic);
    }
    if bytes[4] != 2 || bytes[5] != 1 {
        return Err(ElfError::BadClass);
    }
    if u16_at(bytes, 16)? != ET_EXEC {
        return Err(ElfError::NotExecutable);
    }
    if u16_at(bytes, 18)? != EM_X86_64 {
        return Err(ElfError::WrongMachine);
    }
    let entry = u64_at(bytes, 24)?;
    let phoff = u64_at(bytes, 32)?;
    let phentsize = u16_at(bytes, 54)? as usize;
    let phnum = u16_at(bytes, 56)? as usize;
    if phentsize != PHDR_SIZE {
        return Err(ElfError::BadProgramHeaders);
    }
    let phoff = usize::try_from(phoff).map_err(|_| ElfError::Truncated)?;
    let table_end = phnum.checked_mul(PHDR_SIZE).and_then(|n| n.checked_add(phoff)).ok_or(ElfError::Truncated)?;
    if table_end > bytes.len() {
        return Err(ElfError::Truncated);
    }

    let mut img = Image { entry, segments: [Segment::default(); MAX_SEGMENTS], count: 0 };
    for i in 0..phnum {
        let p = phoff + i * PHDR_SIZE;
        if u32_at(bytes, p)? != PT_LOAD {
            continue;
        }
        let flags = u32_at(bytes, p + 4)?;
        let offset = u64_at(bytes, p + 8)?;
        let vaddr = u64_at(bytes, p + 16)?;
        let filesz = u64_at(bytes, p + 32)?;
        let memsz = u64_at(bytes, p + 40)?;
        if memsz == 0 {
            continue; // an empty segment maps nothing
        }
        if filesz > memsz {
            return Err(ElfError::BadSegmentSize);
        }
        if flags & PF_W != 0 && flags & PF_X != 0 {
            return Err(ElfError::WritableAndExecutable);
        }
        let end = vaddr.checked_add(memsz).ok_or(ElfError::OutsideWindow)?;
        if vaddr < lo || end > hi {
            return Err(ElfError::OutsideWindow);
        }
        let file_off = usize::try_from(offset).map_err(|_| ElfError::Truncated)?;
        let filesz = usize::try_from(filesz).map_err(|_| ElfError::Truncated)?;
        let file_end = file_off.checked_add(filesz).ok_or(ElfError::Truncated)?;
        if file_end > bytes.len() {
            return Err(ElfError::Truncated);
        }
        if img.count == MAX_SEGMENTS {
            return Err(ElfError::BadProgramHeaders);
        }
        img.segments[img.count] = Segment { vaddr, memsz, file_off, filesz, flags };
        img.count += 1;
    }
    // Page-granular overlap check: O(n^2) over at most 8 segments.
    for a in 0..img.count {
        for b in 0..a {
            let (x, y) = (img.segments[a], img.segments[b]);
            if x.first_page() < y.end_page() && y.first_page() < x.end_page() {
                return Err(ElfError::Overlap);
            }
        }
    }
    let in_code = img.segments().iter().any(|s| s.executable() && entry >= s.vaddr && entry < s.vaddr + s.memsz);
    if !in_code {
        return Err(ElfError::BadEntry);
    }
    Ok(img)
}

#[cfg(test)]
mod tests {
    use super::*;
    extern crate std;
    use std::vec::Vec;

    const LO: u64 = 0x80_0000;
    const HI: u64 = 0x90_0000;

    /// Build a minimal image: header, `segs` program headers, then one byte
    /// of file content per segment byte (p_offset points into the tail).
    fn build(entry: u64, segs: &[(u64, u64, u64, u32)]) -> Vec<u8> {
        let mut b = Vec::new();
        b.extend_from_slice(b"\x7fELF");
        b.extend_from_slice(&[2, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0]);
        b.extend_from_slice(&ET_EXEC.to_le_bytes());
        b.extend_from_slice(&EM_X86_64.to_le_bytes());
        b.extend_from_slice(&1u32.to_le_bytes());
        b.extend_from_slice(&entry.to_le_bytes());
        b.extend_from_slice(&64u64.to_le_bytes()); // phoff
        b.extend_from_slice(&0u64.to_le_bytes()); // shoff
        b.extend_from_slice(&0u32.to_le_bytes()); // flags
        b.extend_from_slice(&64u16.to_le_bytes());
        b.extend_from_slice(&56u16.to_le_bytes());
        b.extend_from_slice(&(segs.len() as u16).to_le_bytes());
        b.extend_from_slice(&[0u8; 6]);
        assert_eq!(b.len(), 64);
        let data_start = 64 + 56 * segs.len() as u64;
        let mut off = data_start;
        for &(vaddr, filesz, memsz, flags) in segs {
            b.extend_from_slice(&PT_LOAD.to_le_bytes());
            b.extend_from_slice(&flags.to_le_bytes());
            b.extend_from_slice(&off.to_le_bytes());
            b.extend_from_slice(&vaddr.to_le_bytes());
            b.extend_from_slice(&vaddr.to_le_bytes());
            b.extend_from_slice(&filesz.to_le_bytes());
            b.extend_from_slice(&memsz.to_le_bytes());
            b.extend_from_slice(&PAGE.to_le_bytes());
            off += filesz;
        }
        b.resize(off as usize, 0xCC);
        b
    }

    fn good() -> Vec<u8> {
        build(LO + 0x10, &[(LO, 0x100, 0x100, PF_R | PF_X), (LO + PAGE, 0x40, 0x2000, PF_R | PF_W)])
    }

    #[test]
    fn accepts_a_well_formed_image() {
        let img = parse(&good(), LO, HI).unwrap();
        assert_eq!(img.count, 2);
        assert_eq!(img.entry, LO + 0x10);
        assert_eq!(img.pages(), 3);
        assert!(img.segments()[0].executable() && !img.segments()[0].writable());
        assert!(img.segments()[1].writable() && !img.segments()[1].executable());
    }

    #[test]
    fn rejects_truncated_and_wrong_headers() {
        let g = good();
        assert_eq!(parse(&g[..32], LO, HI), Err(ElfError::Truncated));
        assert_eq!(parse(&g[..g.len() - 1], LO, HI), Err(ElfError::Truncated));
        let mut b = g.clone();
        b[1] = b'X';
        assert_eq!(parse(&b, LO, HI), Err(ElfError::BadMagic));
        let mut b = g.clone();
        b[4] = 1; // ELF32
        assert_eq!(parse(&b, LO, HI), Err(ElfError::BadClass));
        let mut b = g.clone();
        b[16..18].copy_from_slice(&3u16.to_le_bytes()); // ET_DYN
        assert_eq!(parse(&b, LO, HI), Err(ElfError::NotExecutable));
        let mut b = g.clone();
        b[18..20].copy_from_slice(&0xB7u16.to_le_bytes()); // AArch64
        assert_eq!(parse(&b, LO, HI), Err(ElfError::WrongMachine));
        let mut b = g.clone();
        b[54..56].copy_from_slice(&32u16.to_le_bytes());
        assert_eq!(parse(&b, LO, HI), Err(ElfError::BadProgramHeaders));
        let mut b = g.clone();
        b[56..58].copy_from_slice(&1000u16.to_le_bytes()); // phnum past the end
        assert_eq!(parse(&b, LO, HI), Err(ElfError::Truncated));
    }

    #[test]
    fn rejects_segments_outside_the_window() {
        let b = build(0x10_0010, &[(0x10_0000, 0x100, 0x100, PF_R | PF_X)]); // kernel memory
        assert_eq!(parse(&b, LO, HI), Err(ElfError::OutsideWindow));
        let b = build(HI - 0x10, &[(HI - 0x100, 0x200, 0x200, PF_R | PF_X)]); // straddles the end
        assert_eq!(parse(&b, LO, HI), Err(ElfError::OutsideWindow));
        let b = build(LO, &[(u64::MAX - 0x10, 0x100, 0x100, PF_R | PF_X)]); // wraps
        assert_eq!(parse(&b, LO, HI), Err(ElfError::OutsideWindow));
        let b = build(LO - 0x1000 + 4, &[(LO - 0x1000, 0x100, 0x100, PF_R | PF_X)]); // below
        assert_eq!(parse(&b, LO, HI), Err(ElfError::OutsideWindow));
    }

    #[test]
    fn rejects_bad_segments() {
        let b = build(LO, &[(LO, 0x100, 0x100, PF_R | PF_W | PF_X)]);
        assert_eq!(parse(&b, LO, HI), Err(ElfError::WritableAndExecutable));
        let b = build(LO, &[(LO, 0x100, 0x100, PF_R | PF_X), (LO + 0x800, 0x10, 0x10, PF_R | PF_W)]);
        assert_eq!(parse(&b, LO, HI), Err(ElfError::Overlap));
        let b = build(LO, &[(LO, 0x100, 0x100, PF_R | PF_X), (LO + 0x2000, 0x10, 0x10, PF_R)]);
        assert!(parse(&b, LO, HI).is_ok(), "non-adjacent segments are fine");
        // filesz > memsz is forged by hand.
        let mut b = good();
        let p = 64 + 56; // second phdr
        b[p + 32..p + 40].copy_from_slice(&0x3000u64.to_le_bytes()); // filesz
        assert_eq!(parse(&b, LO, HI), Err(ElfError::BadSegmentSize));
        // A segment whose file bytes lie past the end of the image.
        let mut b = good();
        b[p + 8..p + 16].copy_from_slice(&(u64::MAX - 8).to_le_bytes()); // offset
        assert_eq!(parse(&b, LO, HI), Err(ElfError::Truncated));
        let mut b = good();
        b[p + 8..p + 16].copy_from_slice(&1_000_000u64.to_le_bytes());
        assert_eq!(parse(&b, LO, HI), Err(ElfError::Truncated));
    }

    #[test]
    fn rejects_entry_outside_code() {
        let b = build(LO + PAGE, &[(LO, 0x100, 0x100, PF_R | PF_X), (LO + PAGE, 0x40, 0x40, PF_R | PF_W)]);
        assert_eq!(parse(&b, LO, HI), Err(ElfError::BadEntry));
        let b = build(0, &[(LO, 0x100, 0x100, PF_R | PF_X)]);
        assert_eq!(parse(&b, LO, HI), Err(ElfError::BadEntry));
    }

    #[test]
    fn too_many_segments_refused() {
        let segs: Vec<_> = (0..MAX_SEGMENTS as u64 + 1).map(|i| (LO + i * PAGE, 0x10, 0x10, if i == 0 { PF_R | PF_X } else { PF_R })).collect();
        let b = build(LO, &segs);
        assert_eq!(parse(&b, LO, HI), Err(ElfError::BadProgramHeaders));
    }

    #[test]
    fn nothing_but_the_bytes_matter() {
        // 10,000 random mutations of a good image never panic.
        let g = good();
        let mut seed = 0x1234_5678u32;
        for _ in 0..10_000 {
            let mut b = g.clone();
            for _ in 0..4 {
                seed ^= seed << 13;
                seed ^= seed >> 17;
                seed ^= seed << 5;
                let i = (seed as usize) % b.len();
                b[i] = (seed >> 8) as u8;
            }
            let _ = parse(&b, LO, HI);
        }
    }
}
