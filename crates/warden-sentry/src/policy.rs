//! The authority a sandboxed process holds, as capabilities.
//!
//! Each grant (from the app manifest, or from a ticked box on an approved
//! library) becomes a root capability in the sandbox's CSpace, using the
//! same `warden-caps` model the kernel will enforce. Every check below is a
//! capability invocation with the rights the operation needs.

use std::collections::BTreeSet;
use std::os::fd::{AsRawFd, OwnedFd};
use warden_caps::{CSpace, CapTable, ObjectKind, Rights};
use warden_libgrant::Grant;

pub struct DirGrant {
    pub root: String,
    pub fd: OwnedFd,
    slot: usize,
    /// `None` for a manifest grant; `Some(sha256)` for a grant a ticked box
    /// on an approved library brought. The latter counts only for a caller
    /// that has that library mapped right now.
    pub origin: Option<String>,
}

pub struct NetGrant {
    pub host: String,
    pub port: String,
    pub listen: bool,
    slot: usize,
    pub origin: Option<String>,
}

/// The libraries (by SHA-256) mapped in the calling process at the time of
/// the check. Library-brought grants apply only through this set.
pub type Active = BTreeSet<String>;

fn live(origin: &Option<String>, active: &Active) -> bool {
    match origin {
        None => true,
        Some(sha) => active.contains(sha),
    }
}

pub struct Policy {
    table: CapTable,
    cs: CSpace,
    next_slot: usize,
    pub dirs: Vec<DirGrant>,
    pub nets: Vec<NetGrant>,
    spawn_slot: Option<usize>,
    spawn_origins: Vec<Option<String>>,
    held: Vec<(Grant, Option<String>)>,
    lib_grants: usize,
}

/// What an operation on a path needs.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Need {
    Read,
    Write,
    Exec,
}

impl Policy {
    pub fn new() -> Self {
        Policy {
            table: CapTable::new(),
            cs: CSpace::new(256),
            next_slot: 0,
            dirs: Vec::new(),
            nets: Vec::new(),
            spawn_slot: None,
            spawn_origins: Vec::new(),
            held: Vec::new(),
            lib_grants: 0,
        }
    }

    fn mint_root(&mut self, kind: ObjectKind, rights: Rights) -> Result<usize, String> {
        let slot = self.next_slot;
        self.table
            .create_root(&mut self.cs, slot, kind, rights, 0)
            .map_err(|e| format!("capability space full: {e:?}"))?;
        self.next_slot += 1;
        Ok(slot)
    }

    /// Add one manifest grant. Returns false if the same grant was already held.
    pub fn add(&mut self, g: &Grant) -> Result<bool, String> {
        self.add_from(g, None)
    }

    /// Add a grant; `origin` is the library that brought it (see `DirGrant`).
    /// A manifest grant is held once; a library grant is held once per
    /// library, since each applies under a different condition.
    pub fn add_from(&mut self, g: &Grant, origin: Option<String>) -> Result<bool, String> {
        if self.held.contains(&(g.clone(), origin.clone())) {
            return Ok(false);
        }
        if origin.is_some() {
            self.lib_grants += 1;
        }
        match g.perm.as_str() {
            "load" => {}
            "fs.read" | "fs.write" | "exec" => {
                let root = normalize(&g.scope);
                let rights = match g.perm.as_str() {
                    "fs.read" => Rights::READ,
                    // Writing a tree implies reading it.
                    "fs.write" => Rights::READ.union(Rights::WRITE),
                    _ => Rights::READ.union(Rights::EXEC),
                };
                let fd = crate::sys::open_path(&root, libc::O_PATH | libc::O_DIRECTORY)
                    .map_err(|e| format!("grant {}: cannot open {root}: {e}", g.render()))?;
                let slot = self.mint_root(ObjectKind::Dir, rights)?;
                self.dirs.push(DirGrant { root, fd, slot, origin: origin.clone() });
            }
            "net.connect" | "net.listen" => {
                let (host, port) = g
                    .scope
                    .rsplit_once(':')
                    .ok_or_else(|| format!("grant {}: scope must be host:port", g.render()))?;
                let slot = self.mint_root(ObjectKind::NetEndpoint, Rights::READ.union(Rights::WRITE))?;
                self.nets.push(NetGrant {
                    host: host.to_string(),
                    port: port.to_string(),
                    listen: g.perm == "net.listen",
                    slot,
                    origin: origin.clone(),
                });
            }
            "proc.spawn" => {
                if self.spawn_slot.is_none() {
                    self.spawn_slot = Some(self.mint_root(ObjectKind::Thread, Rights::WRITE)?);
                }
                self.spawn_origins.push(origin.clone());
            }
            // Native-app permissions (ABI v0.2). No Linux syscall maps to them in
            // Phase 0; they are recorded so manifests and approvals can carry them.
            "render" | "superop" | "clock" | "random" => {}
            other => return Err(format!("unknown permission {other}")),
        }
        self.held.push((g.clone(), origin));
        Ok(true)
    }

    /// The most specific directory grant whose root contains `path` and whose
    /// capability carries the needed right.
    pub fn dir_for(&self, path: &str, need: Need, active: &Active) -> Option<&DirGrant> {
        let r = match need {
            Need::Read => Rights::READ,
            Need::Write => Rights::WRITE,
            Need::Exec => Rights::EXEC,
        };
        self.dirs
            .iter()
            .filter(|d| under(&d.root, path) && live(&d.origin, active))
            .filter(|d| self.table.invoke(&self.cs, d.slot, r).is_ok())
            .max_by_key(|d| d.root.len())
    }

    pub fn dir_fd(d: &DirGrant) -> i32 {
        d.fd.as_raw_fd()
    }

    pub fn net_allowed(&self, host: &str, port: u16, listen: bool, active: &Active) -> bool {
        self.nets.iter().any(|n| {
            n.listen == listen && live(&n.origin, active)
                && self.table.invoke(&self.cs, n.slot, Rights::WRITE).is_ok()
                && (n.host == "*" || n.host == host)
                && (n.port == "*" || n.port == port.to_string())
        })
    }

    pub fn any_net(&self, active: &Active) -> bool {
        self.nets.iter().any(|n| live(&n.origin, active))
    }

    pub fn may_listen(&self, active: &Active) -> bool {
        self.nets.iter().any(|n| n.listen && live(&n.origin, active))
    }

    pub fn may_spawn(&self, active: &Active) -> bool {
        self.spawn_slot.map(|s| self.table.invoke(&self.cs, s, Rights::WRITE).is_ok()).unwrap_or(false)
            && self.spawn_origins.iter().any(|o| live(o, active))
    }

    /// Does any grant depend on which libraries the caller has mapped?
    pub fn has_lib_grants(&self) -> bool {
        self.lib_grants > 0
    }

    /// Every directory grant root that carries READ or EXEC, whatever its
    /// origin: the places a mapped file may legitimately come from.
    pub fn readable_roots(&self) -> impl Iterator<Item = &str> {
        self.dirs.iter().map(|d| d.root.as_str())
    }
}

/// Lexical normalization of an absolute path: collapses `.`, `..` and `//`.
/// Safe here because the result is only ever opened with RESOLVE_BENEATH
/// under a grant root, so a wrong guess can only make access narrower.
pub fn normalize(p: &str) -> String {
    let mut parts: Vec<&str> = Vec::new();
    for c in p.split('/') {
        match c {
            "" | "." => {}
            ".." => {
                parts.pop();
            }
            c => parts.push(c),
        }
    }
    let mut s = String::from("/");
    s.push_str(&parts.join("/"));
    s
}

/// Component-wise prefix test: `/usr` contains `/usr/lib` but not `/usrx`.
pub fn under(root: &str, path: &str) -> bool {
    root == "/" || path == root || (path.starts_with(root) && path.as_bytes().get(root.len()) == Some(&b'/'))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn normalize_and_under() {
        assert_eq!(normalize("/usr/lib/../../etc//passwd"), "/etc/passwd");
        assert_eq!(normalize("/../../.."), "/");
        assert!(under("/usr", "/usr/lib"));
        assert!(!under("/usr", "/usrx/lib"));
        assert!(under("/", "/anything"));
    }

    #[test]
    fn grants_become_capabilities() {
        let mut p = Policy::new();
        p.add(&Grant::parse("fs.read=/usr").unwrap()).unwrap();
        p.add(&Grant::parse("fs.write=/tmp").unwrap()).unwrap();
        p.add(&Grant::parse("net.connect=127.0.0.1:8080").unwrap()).unwrap();
        let none = Active::new();
        assert!(p.dir_for("/usr/lib/x", Need::Read, &none).is_some());
        assert!(p.dir_for("/usr/lib/x", Need::Write, &none).is_none());
        assert!(p.dir_for("/tmp/a", Need::Write, &none).is_some());
        assert!(p.dir_for("/etc/passwd", Need::Read, &none).is_none());
        assert!(p.net_allowed("127.0.0.1", 8080, false, &none));
        assert!(!p.net_allowed("127.0.0.1", 8081, false, &none));
        assert!(!p.net_allowed("127.0.0.1", 8080, true, &none));
        assert!(!p.may_spawn(&none));
    }

    #[test]
    fn library_grants_apply_only_while_mapped() {
        let mut p = Policy::new();
        p.add(&Grant::parse("fs.read=/usr").unwrap()).unwrap();
        let sha = "ab".repeat(32);
        assert!(p.add_from(&Grant::parse("net.connect=*:443").unwrap(), Some(sha.clone())).unwrap());
        assert!(!p.add_from(&Grant::parse("net.connect=*:443").unwrap(), Some(sha.clone())).unwrap());
        assert!(p.add_from(&Grant::parse("fs.write=/tmp").unwrap(), Some(sha.clone())).unwrap());
        assert!(p.has_lib_grants());
        let none = Active::new();
        let with: Active = [sha.clone()].into_iter().collect();
        let other: Active = ["cd".repeat(32)].into_iter().collect();
        assert!(!p.net_allowed("1.2.3.4", 443, false, &none));
        assert!(!p.net_allowed("1.2.3.4", 443, false, &other));
        assert!(p.net_allowed("1.2.3.4", 443, false, &with));
        assert!(!p.any_net(&none));
        assert!(p.any_net(&with));
        assert!(p.dir_for("/tmp/x", Need::Write, &none).is_none());
        assert!(p.dir_for("/tmp/x", Need::Write, &with).is_some());
        // Manifest grants never depend on the mapped set.
        assert!(p.dir_for("/usr/x", Need::Read, &none).is_some());
    }
}
