//! Per-library permissions.
//!
//! Every shared library a sandboxed program wants to load is a *request*:
//! its name, where it came from, its SHA-256, and the permissions it asks
//! for. The user ticks a checkbox per permission. Ticked boxes become an
//! *approval record* bound to that exact hash, so a changed or swapped
//! library is a new, unapproved request.
//!
//! Files (one record per line, tab separated, `#` comments):
//! - `manifest.wdn`   app grants:  `<perm>\t<scope>`
//! - `pending.wdn`    requests:    `<sha256>\t<name>\t<path>\t<perm[=scope]> ...`
//! - `approvals.wdn`  approvals:   `<sha256>\t<name>\t<perm[=scope]> ...`
//! - `libwants.wdn`   what a library asks for, by name prefix:
//!   continued:       `<name-prefix>\t<perm[=scope]> ...`
#![forbid(unsafe_code)]

use sha2::{Digest, Sha256};
use std::collections::BTreeSet;
use std::fs;
use std::io::{self, BufRead, Read, Write};
use std::path::Path;

pub const KNOWN_PERMS: &[&str] = &["load", "fs.read", "fs.write", "exec", "net.connect", "net.listen", "proc.spawn"];

#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct Grant {
    pub perm: String,
    pub scope: String,
}

impl Grant {
    pub fn parse(s: &str) -> Result<Grant, String> {
        let (perm, scope) = match s.split_once('=') {
            Some((p, sc)) => (p.trim(), sc.trim()),
            None => (s.trim(), ""),
        };
        if !KNOWN_PERMS.contains(&perm) {
            return Err(format!("unknown permission {perm:?}"));
        }
        if scope.chars().any(|c| c.is_whitespace() || c.is_control()) {
            return Err(format!("scope for {perm} has whitespace or control characters"));
        }
        if matches!(perm, "fs.read" | "fs.write" | "exec") && !scope.starts_with('/') {
            return Err(format!("{perm} needs an absolute path scope"));
        }
        if matches!(perm, "net.connect" | "net.listen") && scope.is_empty() {
            return Err(format!("{perm} needs a host:port scope"));
        }
        Ok(Grant { perm: perm.to_string(), scope: scope.to_string() })
    }
    pub fn render(&self) -> String {
        if self.scope.is_empty() { self.perm.clone() } else { format!("{}={}", self.perm, self.scope) }
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct LibRequest {
    pub sha256: String,
    pub name: String,
    pub path: String,
    pub wants: Vec<Grant>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Approval {
    pub sha256: String,
    pub name: String,
    pub granted: Vec<Grant>,
}

impl Approval {
    pub fn may_load(&self) -> bool {
        self.granted.iter().any(|g| g.perm == "load")
    }
}

pub fn sha256_hex(data: &[u8]) -> String {
    let d = Sha256::digest(data);
    d.iter().map(|b| format!("{b:02x}")).collect()
}

pub fn sha256_reader(mut r: impl Read) -> io::Result<String> {
    let mut h = Sha256::new();
    let mut buf = [0u8; 64 * 1024];
    loop {
        let n = r.read(&mut buf)?;
        if n == 0 {
            break;
        }
        h.update(&buf[..n]);
    }
    Ok(h.finalize().iter().map(|b| format!("{b:02x}")).collect())
}

fn records(path: &Path) -> io::Result<Vec<Vec<String>>> {
    let text = match fs::read_to_string(path) {
        Ok(t) => t,
        Err(e) if e.kind() == io::ErrorKind::NotFound => return Ok(Vec::new()),
        Err(e) => return Err(e),
    };
    Ok(text
        .lines()
        .map(str::trim_end)
        .filter(|l| !l.is_empty() && !l.starts_with('#'))
        .map(|l| l.split('\t').map(str::to_string).collect())
        .collect())
}

fn bad(msg: String) -> io::Error {
    io::Error::new(io::ErrorKind::InvalidData, msg)
}

fn grants(words: &str) -> io::Result<Vec<Grant>> {
    words.split_whitespace().map(|w| Grant::parse(w).map_err(bad)).collect()
}

fn is_hash(s: &str) -> bool {
    s.len() == 64 && s.bytes().all(|b| b.is_ascii_hexdigit() && !b.is_ascii_uppercase())
}

fn clean_field(s: &str) -> String {
    s.chars().map(|c| if c == '\t' || c.is_control() { '?' } else { c }).collect()
}

pub fn load_manifest(path: &Path) -> io::Result<Vec<Grant>> {
    let mut out = Vec::new();
    for r in records(path)? {
        let scope = r.get(1).map(String::as_str).unwrap_or("");
        let g = if scope.is_empty() { r[0].clone() } else { format!("{}={}", r[0], scope) };
        out.push(Grant::parse(&g).map_err(bad)?);
    }
    Ok(out)
}

pub fn load_pending(path: &Path) -> io::Result<Vec<LibRequest>> {
    let mut out: Vec<LibRequest> = Vec::new();
    for r in records(path)? {
        if r.len() < 3 || !is_hash(&r[0]) {
            return Err(bad(format!("bad pending record: {r:?}")));
        }
        let wants = grants(r.get(3).map(String::as_str).unwrap_or("load"))?;
        if !out.iter().any(|q| q.sha256 == r[0]) {
            out.push(LibRequest { sha256: r[0].clone(), name: r[1].clone(), path: r[2].clone(), wants });
        }
    }
    Ok(out)
}

pub fn save_pending(path: &Path, reqs: &[LibRequest]) -> io::Result<()> {
    let mut s = String::from("# Library permission requests awaiting approval. Run `warden approve`.\n");
    for q in reqs {
        let w: Vec<_> = q.wants.iter().map(Grant::render).collect();
        s.push_str(&format!("{}\t{}\t{}\t{}\n", q.sha256, clean_field(&q.name), clean_field(&q.path), w.join(" ")));
    }
    write_atomic(path, &s)
}

pub fn load_approvals(path: &Path) -> io::Result<Vec<Approval>> {
    let mut out = Vec::new();
    for r in records(path)? {
        if r.len() < 3 || !is_hash(&r[0]) {
            return Err(bad(format!("bad approval record: {r:?}")));
        }
        out.push(Approval { sha256: r[0].clone(), name: r[1].clone(), granted: grants(&r[2])? });
    }
    Ok(out)
}

pub fn save_approvals(path: &Path, list: &[Approval]) -> io::Result<()> {
    let mut s = String::from("# Library approvals. Each line binds ticked permissions to one exact SHA-256.\n");
    for a in list {
        let g: Vec<_> = a.granted.iter().map(Grant::render).collect();
        let g = if g.is_empty() { "-".to_string() } else { g.join(" ") };
        s.push_str(&format!("{}\t{}\t{}\n", a.sha256, clean_field(&a.name), g));
    }
    write_atomic(path, &s.replace("\t-\n", "\t\n"))
}

fn write_atomic(path: &Path, s: &str) -> io::Result<()> {
    let tmp = path.with_extension("tmp");
    fs::write(&tmp, s)?;
    fs::rename(tmp, path)
}

/// Permissions a library asks for, from the `libwants.wdn` registry. Every
/// library at least asks to be loaded.
pub fn wants_for(registry: &Path, libname: &str) -> io::Result<Vec<Grant>> {
    let mut w = vec![Grant::parse("load").unwrap()];
    for r in records(registry)? {
        if libname.starts_with(r[0].as_str()) {
            for g in grants(r.get(1).map(String::as_str).unwrap_or(""))? {
                if !w.contains(&g) {
                    w.push(g);
                }
            }
        }
    }
    Ok(w)
}

/// Add or replace one approval (keyed by hash).
pub fn upsert(list: &mut Vec<Approval>, a: Approval) {
    list.retain(|x| x.sha256 != a.sha256);
    list.push(a);
}

/// The checkbox screen. One box per (library, permission); nothing is ticked
/// by default. Commands: numbers toggle boxes (`1 3 4`), `all`, `none`,
/// `done` saves, `quit` saves nothing. Returns `None` on quit or EOF.
pub fn checklist(reqs: &[LibRequest], input: &mut dyn BufRead, out: &mut dyn Write) -> io::Result<Option<Vec<Approval>>> {
    let boxes: Vec<(usize, usize)> =
        reqs.iter().enumerate().flat_map(|(i, q)| (0..q.wants.len()).map(move |j| (i, j))).collect();
    let mut ticked: BTreeSet<usize> = BTreeSet::new();
    loop {
        writeln!(out, "\nLibrary permission requests. Tick a box to grant it.")?;
        let mut n = 0;
        for (i, q) in reqs.iter().enumerate() {
            writeln!(out, "\n  {}  {}", q.name, q.path)?;
            writeln!(out, "     sha256 {}", q.sha256)?;
            for g in &q.wants {
                n += 1;
                let mark = if ticked.contains(&(n - 1)) { "x" } else { " " };
                let what = match g.perm.as_str() {
                    "load" => "load this library".to_string(),
                    _ => g.render(),
                };
                writeln!(out, "     {n:>3}. [{mark}] {what}")?;
            }
            let _ = i;
        }
        write!(out, "\nToggle numbers (e.g. \"1 3\"), \"all\", \"none\", \"done\" to save, \"quit\" to cancel: ")?;
        out.flush()?;
        let mut line = String::new();
        if input.read_line(&mut line)? == 0 {
            return Ok(None);
        }
        match line.trim() {
            "done" => break,
            "quit" => return Ok(None),
            "all" => ticked = (0..boxes.len()).collect(),
            "none" => ticked.clear(),
            other => {
                for w in other.split([' ', ',']).filter(|w| !w.is_empty()) {
                    match w.parse::<usize>() {
                        Ok(k) if k >= 1 && k <= boxes.len() => {
                            if !ticked.remove(&(k - 1)) {
                                ticked.insert(k - 1);
                            }
                        }
                        _ => writeln!(out, "  ignoring {w:?}: not a box number")?,
                    }
                }
            }
        }
    }
    let mut result = Vec::new();
    for (i, q) in reqs.iter().enumerate() {
        let granted: Vec<Grant> = boxes
            .iter()
            .enumerate()
            .filter(|(k, (bi, _))| *bi == i && ticked.contains(k))
            .map(|(_, (_, j))| q.wants[*j].clone())
            .collect();
        // A permission without "load" is meaningless; keep it recorded so the
        // user's choice is visible, but the loader still refuses the library.
        result.push(Approval { sha256: q.sha256.clone(), name: q.name.clone(), granted });
    }
    Ok(Some(result))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn req(name: &str, wants: &[&str]) -> LibRequest {
        LibRequest {
            sha256: sha256_hex(name.as_bytes()),
            name: name.into(),
            path: format!("/lib/{name}"),
            wants: wants.iter().map(|w| Grant::parse(w).unwrap()).collect(),
        }
    }

    #[test]
    fn grants_validate() {
        assert!(Grant::parse("fs.read=/etc").is_ok());
        assert!(Grant::parse("fs.read=etc").is_err());
        assert!(Grant::parse("root").is_err());
        assert!(Grant::parse("net.connect=").is_err());
        assert!(Grant::parse("fs.read=/a b").is_err());
    }

    #[test]
    fn nothing_ticked_by_default() {
        let reqs = [req("libz.so.1", &["load"]), req("libcurl.so.4", &["load", "net.connect=*:443"])];
        let mut out = Vec::new();
        let r = checklist(&reqs, &mut "done\n".as_bytes(), &mut out).unwrap().unwrap();
        assert!(r.iter().all(|a| a.granted.is_empty()));
        assert!(String::from_utf8(out).unwrap().contains("[ ] net.connect=*:443"));
    }

    #[test]
    fn toggles_bind_to_the_right_library() {
        let reqs = [req("libz.so.1", &["load"]), req("libcurl.so.4", &["load", "net.connect=*:443"])];
        let mut out = Vec::new();
        // Tick 1 and 2, then untick 1 and tick 3, plus junk input.
        let r = checklist(&reqs, &mut "1 2\n1,3 99 x\ndone\n".as_bytes(), &mut out).unwrap().unwrap();
        assert!(r[0].granted.is_empty());
        assert_eq!(r[1].granted.iter().map(Grant::render).collect::<Vec<_>>(), ["load", "net.connect=*:443"]);
    }

    #[test]
    fn quit_and_eof_save_nothing() {
        let reqs = [req("libz.so.1", &["load"])];
        assert!(checklist(&reqs, &mut "all\nquit\n".as_bytes(), &mut Vec::new()).unwrap().is_none());
        assert!(checklist(&reqs, &mut "all\n".as_bytes(), &mut Vec::new()).unwrap().is_none());
    }

    #[test]
    fn records_round_trip() {
        let dir = std::env::temp_dir().join(format!("wlg-{}", std::process::id()));
        fs::create_dir_all(&dir).unwrap();
        let reqs = vec![req("libssl.so.3", &["load", "fs.read=/etc/ssl"])];
        save_pending(&dir.join("pending.wdn"), &reqs).unwrap();
        assert_eq!(load_pending(&dir.join("pending.wdn")).unwrap(), reqs);
        let a = vec![Approval { sha256: reqs[0].sha256.clone(), name: "libssl.so.3".into(), granted: reqs[0].wants.clone() }];
        save_approvals(&dir.join("approvals.wdn"), &a).unwrap();
        assert_eq!(load_approvals(&dir.join("approvals.wdn")).unwrap(), a);
        fs::remove_dir_all(dir).unwrap();
    }
}
