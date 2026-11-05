//! Minimal ELF64 reader used by `warden scan`: finds PT_INTERP and the
//! DT_NEEDED libraries of a program, recursively, without running anything.
//! Input files are untrusted, so every offset is bounds-checked.

use std::collections::BTreeSet;
use std::fs;

pub const LIB_DIRS: &[&str] = &["/lib/x86_64-linux-gnu", "/usr/lib/x86_64-linux-gnu", "/lib64", "/usr/lib64", "/lib", "/usr/lib"];

fn u16_at(b: &[u8], o: usize) -> Option<u16> {
    Some(u16::from_le_bytes(b.get(o..o + 2)?.try_into().ok()?))
}
fn u32_at(b: &[u8], o: usize) -> Option<u32> {
    Some(u32::from_le_bytes(b.get(o..o + 4)?.try_into().ok()?))
}
fn u64_at(b: &[u8], o: usize) -> Option<u64> {
    Some(u64::from_le_bytes(b.get(o..o + 8)?.try_into().ok()?))
}

pub struct Info {
    pub interp: Option<String>,
    pub needed: Vec<String>,
}

pub fn parse(b: &[u8]) -> Option<Info> {
    if b.get(..4)? != b"\x7fELF" || *b.get(4)? != 2 || *b.get(5)? != 1 {
        return None; // not ELF64 little-endian
    }
    let phoff = u64_at(b, 32)? as usize;
    let phentsize = u16_at(b, 54)? as usize;
    let phnum = (u16_at(b, 56)? as usize).min(256);
    if phentsize < 56 {
        return None;
    }
    let mut interp = None;
    let mut dynamic = None;
    let mut loads = Vec::new();
    for i in 0..phnum {
        let o = phoff.checked_add(i.checked_mul(phentsize)?)?;
        let ty = u32_at(b, o)?;
        let off = u64_at(b, o + 8)? as usize;
        let vaddr = u64_at(b, o + 16)?;
        let filesz = u64_at(b, o + 32)? as usize;
        match ty {
            1 => loads.push((vaddr, off, filesz)),
            2 => dynamic = Some((off, filesz)),
            3 => {
                let s = b.get(off..off.checked_add(filesz)?)?;
                interp = Some(String::from_utf8_lossy(s).trim_end_matches('\0').to_string());
            }
            _ => {}
        }
    }
    let mut needed = Vec::new();
    if let Some((doff, dsz)) = dynamic {
        let dynb = b.get(doff..doff.checked_add(dsz)?)?;
        let mut strtab = None;
        let mut need_offs = Vec::new();
        for e in dynb.chunks_exact(16) {
            let tag = u64_at(e, 0)?;
            let val = u64_at(e, 8)?;
            match tag {
                0 => break,
                1 => need_offs.push(val as usize),
                5 => strtab = Some(val),
                _ => {}
            }
        }
        // DT_STRTAB is a virtual address; map it to a file offset.
        let st = strtab.and_then(|va| loads.iter().find(|(v, _, sz)| va >= *v && va < v + *sz as u64).map(|(v, o, _)| (va - v) as usize + o));
        if let Some(st) = st {
            for no in need_offs {
                let start = st.checked_add(no)?;
                let rest = b.get(start..)?;
                let end = rest.iter().position(|&c| c == 0)?.min(255);
                needed.push(String::from_utf8_lossy(&rest[..end]).to_string());
            }
        }
    }
    Some(Info { interp, needed })
}

/// All shared objects a program pulls in, as (soname, resolved path).
pub fn closure(prog: &str) -> Result<Vec<(String, String)>, String> {
    let mut out = Vec::new();
    let mut seen = BTreeSet::new();
    let mut queue: Vec<String> = Vec::new();
    let b = fs::read(prog).map_err(|e| format!("{prog}: {e}"))?;
    let info = parse(&b).ok_or(format!("{prog}: not an ELF64 program"))?;
    if let Some(i) = info.interp {
        let name = i.rsplit('/').next().unwrap_or(&i).to_string();
        seen.insert(name.clone());
        out.push((name, i));
    }
    queue.extend(info.needed);
    while let Some(soname) = queue.pop() {
        if !seen.insert(soname.clone()) || seen.len() > 512 {
            continue;
        }
        let Some(path) = LIB_DIRS.iter().map(|d| format!("{d}/{soname}")).find(|p| std::path::Path::new(p).is_file()) else {
            out.push((soname, String::new()));
            continue;
        };
        if let Ok(b) = fs::read(&path) {
            if let Some(i) = parse(&b) {
                queue.extend(i.needed);
            }
        }
        out.push((soname, path));
    }
    Ok(out)
}
