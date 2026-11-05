//! Read-only, bounded analysis of x86-64 ELF dynamic metadata.
//!
//! This is NOT a dynamic loader: it never maps memory, opens libraries,
//! applies relocations or executes input bytes. Validated metadata is
//! necessary but not sufficient for a safe capability-scoped loader.
#![no_std]
#![forbid(unsafe_code)]

const PT_LOAD: u32 = 1;
const PT_DYNAMIC: u32 = 2;
const PT_INTERP: u32 = 3;
const PF_W: u32 = 2;
const DT_NULL: i64 = 0;
const DT_NEEDED: i64 = 1;
const DT_STRTAB: i64 = 5;
const DT_STRSZ: i64 = 10;
const DT_RELA: i64 = 7;
const DT_RELASZ: i64 = 8;
const DT_RELAENT: i64 = 9;
const DT_TEXTREL: i64 = 22;
const DT_JMPREL: i64 = 23;
const DT_REL: i64 = 17;
const DT_RELSZ: i64 = 18;
const DT_PLTRELSZ: i64 = 2;
const DT_FLAGS: i64 = 30;
const DT_FLAGS_1: i64 = 0x6fff_fffb;
const DT_BIND_NOW: i64 = 24;
const MAX_PHDRS: usize = 64;
pub const MAX_NEEDED: usize = 16;
pub const MAX_RELAS: usize = 64;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Error {
    Truncated, InvalidHeader, WrongArchitecture, UnsupportedElfType,
    TooManyHeaders, MultipleDynamicTables, InvalidProgramHeader,
    InvalidDynamicTable, MissingStringTable, MissingRelaTable,
    OutOfBounds, TooManyNeeded, TooManyRelocations,
    InvalidLibraryName, UnsupportedRelocation, TextRelocation,
    UnsupportedDynamicFeature, InvalidInterpreter,
}

#[derive(Clone, Copy, Default, Debug, PartialEq, Eq)]
pub struct Rela {
    /// Virtual address to be written by a future loader, not a file offset.
    pub target: u64,
    /// Signed addend; the future loader must use checked arithmetic.
    pub addend: i64,
}

#[derive(Clone, Copy)]
struct Load {
    addr: u64,
    memsz: u64,
    off: usize,
    filesz: usize,
    flags: u32,
}
const EMPTY_LOAD: Load = Load { addr: 0, memsz: 0, off: 0, filesz: 0, flags: 0 };

/// Borrowed strings refer to verified slices of the original ELF bytes.
/// No result owns or mutates any guest memory.
pub struct Metadata<'a> {
    pub interpreter: Option<&'a [u8]>,
    pub needed: [Option<&'a [u8]>; MAX_NEEDED],
    pub needed_count: usize,
    pub relas: [Rela; MAX_RELAS],
    pub rela_count: usize,
}

fn u16le(b: &[u8], o: usize) -> Result<u16, Error> {
    let s = b.get(o..o.checked_add(2).ok_or(Error::OutOfBounds)?).ok_or(Error::Truncated)?;
    Ok(u16::from_le_bytes([s[0], s[1]]))
}
fn u32le(b: &[u8], o: usize) -> Result<u32, Error> {
    let s = b.get(o..o.checked_add(4).ok_or(Error::OutOfBounds)?).ok_or(Error::Truncated)?;
    Ok(u32::from_le_bytes([s[0], s[1], s[2], s[3]]))
}
fn u64le(b: &[u8], o: usize) -> Result<u64, Error> {
    let s = b.get(o..o.checked_add(8).ok_or(Error::OutOfBounds)?).ok_or(Error::Truncated)?;
    Ok(u64::from_le_bytes([s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7]]))
}
fn region(b: &[u8], off: usize, size: usize) -> Result<&[u8], Error> {
    b.get(off..off.checked_add(size).ok_or(Error::OutOfBounds)?).ok_or(Error::OutOfBounds)
}
fn string_at(table: &[u8], off: usize) -> Result<&[u8], Error> {
    let s = table.get(off..).ok_or(Error::InvalidLibraryName)?;
    let end = s.iter().position(|&x| x == 0).ok_or(Error::InvalidLibraryName)?;
    let name = &s[..end];
    if name.is_empty() || name.iter().any(|&x| x < 0x20 || x == 0x7f || x == b'/' || x == b'\\') ||
        name == b"." || name == b".." {
        return Err(Error::InvalidLibraryName);
    }
    Ok(name)
}
/// Map virtual address to a FILE-BACKED PT_LOAD slice, never to BSS.
/// This does not grant guest access or copy anything.
fn file_va<'a>(b: &'a [u8], loads: &[Load], va: u64, size: usize) -> Result<&'a [u8], Error> {
    let n = u64::try_from(size).map_err(|_| Error::OutOfBounds)?;
    let end = va.checked_add(n).ok_or(Error::OutOfBounds)?;
    for l in loads {
        let load_end = l.addr.checked_add(l.filesz as u64).ok_or(Error::OutOfBounds)?;
        if va >= l.addr && end <= load_end {
            let delta = usize::try_from(va - l.addr).map_err(|_| Error::OutOfBounds)?;
            return region(b, l.off.checked_add(delta).ok_or(Error::OutOfBounds)?, size);
        }
    }
    Err(Error::OutOfBounds)
}
fn writable_target(loads: &[Load], va: u64) -> bool {
    let Some(end) = va.checked_add(8) else { return false };
    loads.iter().any(|l| l.flags & PF_W != 0 &&
        l.addr <= va && l.addr.checked_add(l.memsz).is_some_and(|high| end <= high))
}

/// Parses x86-64 ET_DYN / ET_EXEC dynamic metadata with bounded arrays.
/// It only understands R_X86_64_RELATIVE (type 8, symbol index 0).
/// Any other relocation kind is refused; mapping remains unimplemented.
pub fn inspect(bytes: &[u8]) -> Result<Metadata<'_>, Error> {
    if bytes.len() < 64 { return Err(Error::Truncated) }
    if bytes.get(0..4) != Some(b"\x7fELF") || bytes[4] != 2 ||
       bytes[5] != 1 || bytes[6] != 1 { return Err(Error::InvalidHeader) }
    if u16le(bytes, 18)? != 62 { return Err(Error::WrongArchitecture) }
    if !matches!(u16le(bytes, 16)?, 2 | 3) { return Err(Error::UnsupportedElfType) }
    let phoff = usize::try_from(u64le(bytes, 32)?).map_err(|_| Error::OutOfBounds)?;
    let phent = u16le(bytes, 54)? as usize;
    let phnum = u16le(bytes, 56)? as usize;
    if phent != 56 { return Err(Error::InvalidProgramHeader) }
    if phnum > MAX_PHDRS { return Err(Error::TooManyHeaders) }
    region(bytes, phoff, phnum.checked_mul(phent).ok_or(Error::OutOfBounds)?)?;

    let mut loads = [EMPTY_LOAD; MAX_PHDRS];
    let mut nload = 0;
    let mut dyn_slice = None;
    let mut interp = None;
    for i in 0..phnum {
        let p = phoff + i * phent;
        let kind = u32le(bytes, p)?;
        let flags = u32le(bytes, p + 4)?;
        let off = usize::try_from(u64le(bytes, p + 8)?).map_err(|_| Error::OutOfBounds)?;
        let addr = u64le(bytes, p + 16)?;
        let filesz = usize::try_from(u64le(bytes, p + 32)?).map_err(|_| Error::OutOfBounds)?;
        let memsz = u64le(bytes, p + 40)?;
        match kind {
            PT_LOAD => {
                if filesz as u64 > memsz || addr.checked_add(memsz).is_none() {
                    return Err(Error::InvalidProgramHeader)
                }
                region(bytes, off, filesz)?;
                loads[nload] = Load { addr, memsz, off, filesz, flags };
                nload += 1;
            }
            PT_DYNAMIC => {
                if dyn_slice.is_some() { return Err(Error::MultipleDynamicTables) }
                if filesz == 0 || filesz % 16 != 0 || filesz as u64 > memsz {
                    return Err(Error::InvalidDynamicTable)
                }
                dyn_slice = Some(region(bytes, off, filesz)?);
            }
            PT_INTERP => {
                if interp.is_some() { return Err(Error::InvalidInterpreter) }
                let raw = region(bytes, off, filesz)?;
                if raw.len() < 2 || raw.last() != Some(&0) ||
                    raw[..raw.len()-1].contains(&0) ||
                    raw[..raw.len()-1].iter().any(|&x| x < 32 || x == 127) {
                    return Err(Error::InvalidInterpreter)
                }
                interp = Some(&raw[..raw.len()-1]);
            }
            _ => {}
        }
    }
    let ds = dyn_slice.ok_or(Error::InvalidDynamicTable)?;
    let mut needed_off = [0usize; MAX_NEEDED];
    let mut needed_n = 0usize;
    let mut strtab = None;
    let mut strsz = None;
    let mut rela = None;
    let mut relasz = None;
    let mut relaent = None;
    let mut terminated = false;
    for i in (0..ds.len()).step_by(16) {
        let tag = i64::from_le_bytes(ds[i..i+8].try_into().map_err(|_| Error::InvalidDynamicTable)?);
        let v = u64::from_le_bytes(ds[i+8..i+16].try_into().map_err(|_| Error::InvalidDynamicTable)?);
        if tag == DT_NULL { terminated = true; break }
        match tag {
            DT_NEEDED => {
                if needed_n == MAX_NEEDED { return Err(Error::TooManyNeeded) }
                needed_off[needed_n] = usize::try_from(v).map_err(|_| Error::OutOfBounds)?;
                needed_n += 1;
            }
            DT_STRTAB => strtab = Some(v),
            DT_STRSZ => strsz = Some(usize::try_from(v).map_err(|_| Error::OutOfBounds)?),
            DT_RELA => rela = Some(v),
            DT_RELASZ => relasz = Some(usize::try_from(v).map_err(|_| Error::OutOfBounds)?),
            DT_RELAENT => relaent = Some(v),
            DT_TEXTREL | DT_REL | DT_RELSZ | DT_JMPREL | DT_PLTRELSZ => return Err(Error::UnsupportedDynamicFeature),
            DT_FLAGS if v & 4 != 0 => return Err(Error::TextRelocation),
            DT_FLAGS_1 | DT_BIND_NOW | DT_FLAGS => {},
            // Dynamic linker behavior must not be guessed from unknown tags.
            _ => return Err(Error::UnsupportedDynamicFeature),
        }
    }
    if !terminated { return Err(Error::InvalidDynamicTable) }
    let mut result = Metadata { interpreter: interp, needed: [None; MAX_NEEDED],
        needed_count: needed_n, relas: [Rela::default(); MAX_RELAS], rela_count: 0 };
    if needed_n != 0 || strtab.is_some() || strsz.is_some() {
        let va = strtab.ok_or(Error::MissingStringTable)?;
        let size = strsz.ok_or(Error::MissingStringTable)?;
        let strings = file_va(bytes, &loads[..nload], va, size)?;
        for (i, offset) in needed_off[..needed_n].iter().enumerate() {
            result.needed[i] = Some(string_at(strings, *offset)?);
        }
    }
    if rela.is_some() || relasz.is_some() || relaent.is_some() {
        let va = rela.ok_or(Error::MissingRelaTable)?;
        let size = relasz.ok_or(Error::MissingRelaTable)?;
        if relaent != Some(24) || size % 24 != 0 { return Err(Error::InvalidDynamicTable) }
        if size / 24 > MAX_RELAS { return Err(Error::TooManyRelocations) }
        let body = file_va(bytes, &loads[..nload], va, size)?;
        for i in 0..size / 24 {
            let p = i * 24;
            let target = u64le(body, p)?;
            let info = u64le(body, p + 8)?;
            let addend = i64::from_le_bytes(body[p+16..p+24].try_into().map_err(|_| Error::Truncated)?);
            if info != 8 { return Err(Error::UnsupportedRelocation) }
            if !writable_target(&loads[..nload], target) { return Err(Error::TextRelocation) }
            result.relas[i] = Rela { target, addend };
            result.rela_count += 1;
        }
    }
    Ok(result)
}
