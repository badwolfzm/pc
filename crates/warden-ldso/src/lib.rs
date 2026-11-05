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


#[cfg(test)]
mod tests {
    extern crate std;
    use super::*;
    use std::vec::Vec;

    const FILE_LEN: usize = 0x300;
    const LOAD_OFF: usize = 0x100;
    const LOAD_VA: u64 = 0x400000;
    const DYN_OFF: usize = 0x180;
    const STR_OFF: usize = 0x200;
    const RELA_OFF: usize = 0x240;
    const INTERP_OFF: usize = 0x280;

    fn put_u16(b: &mut [u8], off: usize, val: u16) { b[off..off+2].copy_from_slice(&val.to_le_bytes()); }
    fn put_u32(b: &mut [u8], off: usize, val: u32) { b[off..off+4].copy_from_slice(&val.to_le_bytes()); }
    fn put_u64(b: &mut [u8], off: usize, val: u64) { b[off..off+8].copy_from_slice(&val.to_le_bytes()); }
    fn dyn_tag(b: &mut [u8], slot: usize, tag: i64, value: u64) {
        let p = DYN_OFF + slot * 16;
        b[p..p+8].copy_from_slice(&tag.to_le_bytes());
        put_u64(b, p + 8, value);
    }
    fn ph(b: &mut [u8], idx: usize, kind: u32, flags: u32, off: usize, va: u64, sz: usize, mem: u64) {
        let p = 64 + idx * 56;
        put_u32(b, p, kind);
        put_u32(b, p+4, flags);
        put_u64(b, p+8, off as u64);
        put_u64(b, p+16, va);
        put_u64(b, p+32, sz as u64);
        put_u64(b, p+40, mem);
    }
    fn valid() -> Vec<u8> {
        let mut b = std::vec![0u8; FILE_LEN];
        b[..4].copy_from_slice(b"\x7fELF");
        b[4] = 2;
        b[5] = 1;
        b[6] = 1;
        put_u16(&mut b, 16, 3); // ET_DYN
        put_u16(&mut b, 18, 62); // x86-64
        put_u64(&mut b, 32, 64);
        put_u16(&mut b, 54, 56);
        put_u16(&mut b, 56, 3);
        ph(&mut b, 0, PT_LOAD, 6, LOAD_OFF, LOAD_VA, 0x200, 0x1000);
        ph(&mut b, 1, PT_DYNAMIC, 6, DYN_OFF, LOAD_VA + 0x80, 7 * 16, 7 * 16);
        ph(&mut b, 2, PT_INTERP, 4, INTERP_OFF, LOAD_VA + 0x180, 28, 28);
        b[INTERP_OFF..INTERP_OFF + 28].copy_from_slice(b"/lib64/ld-linux-x86-64.so.2\0");
        b[STR_OFF..STR_OFF + 13].copy_from_slice(b"\0libfoo.so.1\0");
        dyn_tag(&mut b, 0, DT_NEEDED, 1);
        dyn_tag(&mut b, 1, DT_STRTAB, LOAD_VA + 0x100);
        dyn_tag(&mut b, 2, DT_STRSZ, 13);
        dyn_tag(&mut b, 3, DT_RELA, LOAD_VA + 0x140);
        dyn_tag(&mut b, 4, DT_RELASZ, 24);
        dyn_tag(&mut b, 5, DT_RELAENT, 24);
        dyn_tag(&mut b, 6, DT_NULL, 0);
        put_u64(&mut b, RELA_OFF, LOAD_VA + 0x300); // RW region, BSS
        put_u64(&mut b, RELA_OFF + 8, 8); // R_X86_64_RELATIVE, no symbol
        put_u64(&mut b, RELA_OFF + 16, 0x1234);
        b
    }
    #[test]
    fn reads_interpreter_needed_and_relative_rela() {
        let b = valid();
        let r = inspect(&b).unwrap();
        assert_eq!(r.interpreter, Some(&b"/lib64/ld-linux-x86-64.so.2"[..]));
        assert_eq!(r.needed_count, 1);
        assert_eq!(r.needed[0], Some(&b"libfoo.so.1"[..]));
        assert_eq!(r.rela_count, 1);
        assert_eq!(r.relas[0], Rela { target: LOAD_VA + 0x300, addend: 0x1234 });
    }
    #[test]
    fn rejects_truncation_and_wrong_abi() {
        assert!(matches!(inspect(&valid()[..32]), Err(Error::Truncated)));
        let mut b = valid(); b[4] = 1;
        assert!(matches!(inspect(&b), Err(Error::InvalidHeader)));
        let mut b = valid(); put_u16(&mut b, 18, 183);
        assert!(matches!(inspect(&b), Err(Error::WrongArchitecture)));
        let mut b = valid(); put_u16(&mut b, 16, 1);
        assert!(matches!(inspect(&b), Err(Error::UnsupportedElfType)));
        let mut b = valid(); put_u16(&mut b, 56, 65);
        assert!(matches!(inspect(&b), Err(Error::TooManyHeaders)));
    }
    #[test]
    fn rejects_bad_header_bounds_and_oversized_segments() {
        let mut b = valid(); put_u64(&mut b, 32, u64::MAX);
        assert!(inspect(&b).is_err());
        let mut b = valid(); put_u64(&mut b, 64 + 32, u64::MAX);
        assert!(matches!(inspect(&b), Err(Error::InvalidProgramHeader)));
        let mut b = valid(); put_u64(&mut b, 64 + 8, u64::MAX);
        assert!(inspect(&b).is_err());
    }
    #[test]
    fn rejects_unsupported_relocation_types_or_symbol_refs() {
        for info in [1u64, 6, 7, 9, (1u64 << 32) | 8] {
            let mut b = valid(); put_u64(&mut b, RELA_OFF + 8, info);
            assert!(matches!(inspect(&b), Err(Error::UnsupportedRelocation)));
        }
    }
    #[test]
    fn refuses_text_relocations_and_cross_boundary_targets() {
        let mut b = valid(); put_u32(&mut b, 64 + 4, 5); // RX load segment
        assert!(matches!(inspect(&b), Err(Error::TextRelocation)));
        let mut b = valid(); put_u64(&mut b, RELA_OFF, LOAD_VA + 0xffc);
        assert!(matches!(inspect(&b), Err(Error::TextRelocation)));
        let mut b = valid(); put_u64(&mut b, RELA_OFF, u64::MAX);
        assert!(matches!(inspect(&b), Err(Error::TextRelocation)));
    }
    #[test]
    fn missing_string_table_or_invalid_name_refused() {
        let mut b = valid(); dyn_tag(&mut b, 1, 0x6fff_ffff, 1);
        assert!(inspect(&b).is_err());
        let mut b = valid(); dyn_tag(&mut b, 0, DT_NEEDED, 99);
        assert!(matches!(inspect(&b), Err(Error::InvalidLibraryName)));
        let mut b = valid(); b[STR_OFF + 4] = b'/';
        assert!(matches!(inspect(&b), Err(Error::InvalidLibraryName)));
    }
    #[test]
    fn malformed_dynamic_table_and_rela_sizes_fail() {
        let mut b = valid(); dyn_tag(&mut b, 5, DT_RELAENT, 16);
        assert!(matches!(inspect(&b), Err(Error::InvalidDynamicTable)));
        let mut b = valid(); dyn_tag(&mut b, 4, DT_RELASZ, 25);
        assert!(matches!(inspect(&b), Err(Error::InvalidDynamicTable)));
        let mut b = valid(); dyn_tag(&mut b, 4, DT_RELASZ, 65 * 24);
        assert!(matches!(inspect(&b), Err(Error::TooManyRelocations)));
        let mut b = valid(); dyn_tag(&mut b, 0, DT_TEXTREL, 1);
        assert!(matches!(inspect(&b), Err(Error::UnsupportedDynamicFeature)));
    }
    #[test]
    fn rejects_duplicate_dynamic_table_or_bad_interpreter() {
        let mut b = valid(); ph(&mut b, 2, PT_DYNAMIC, 6, DYN_OFF, LOAD_VA + 0x80, 7 * 16, 7 * 16);
        assert!(matches!(inspect(&b), Err(Error::MultipleDynamicTables)));
        let mut b = valid(); b[INTERP_OFF + 27] = b'X';
        assert!(matches!(inspect(&b), Err(Error::InvalidInterpreter)));
    }
    #[test]
    fn no_panic_on_10000_mutated_inputs() {
        let baseline = valid();
        let mut seed = 0x9e37_79b9u32;
        for _ in 0..10_000 {
            let mut b = baseline.clone();
            for _ in 0..4 {
                seed ^= seed << 13;
                seed ^= seed >> 17;
                seed ^= seed << 5;
                let idx = (seed as usize) % b.len();
                b[idx] = (seed >> 8) as u8;
            }
            let _ = inspect(&b);
        }
    }
}
