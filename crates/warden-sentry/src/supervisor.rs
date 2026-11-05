//! The supervisor: receives every non-`Pass` syscall of the sandbox from the
//! kernel, decides it against the capability policy, and answers.
//!
//! Rule for safe answers: a decision may only use `CONTINUE` (let the kernel
//! run the original syscall) when it depends on register arguments, or when
//! the remaining race is documented below as a Phase 0 limitation. Where a
//! decision depends on memory the sandbox can change (paths, socket
//! addresses), the supervisor performs the operation itself on its own copy
//! of the argument and hands back the result (an installed fd or a return
//! value). That removes check-then-use races for open, connect and bind.

use crate::policy::{normalize, Need, Policy};
use crate::sys::{self, Notif};
use std::collections::{BTreeSet, HashMap};
use std::fs::{self, File, OpenOptions};
use std::io::{self, Read, Write};
use std::os::fd::{AsRawFd, OwnedFd, RawFd};
use std::os::unix::fs::MetadataExt;
use std::path::{Path, PathBuf};
use std::time::{SystemTime, UNIX_EPOCH};
use warden_abi::{classify, name_of, Class, Family};
use warden_libgrant::{self as lg, Approval, LibRequest};

const AT_FDCWD: i32 = -100;
const AT_EMPTY_PATH: u64 = 0x1000;
const AT_SYMLINK_NOFOLLOW: u64 = 0x100;
const MSG_FASTOPEN: u64 = 0x2000_0000;
const CLONE_THREAD: u64 = 0x0001_0000;
/// CLONE_NEWNS | NEWCGROUP | NEWUTS | NEWIPC | NEWUSER | NEWPID | NEWNET
const CLONE_NS_MASK: u64 = 0x0002_0000 | 0x0200_0000 | 0x0400_0000 | 0x0800_0000 | 0x1000_0000 | 0x2000_0000 | 0x4000_0000;
const PATH_MAX: usize = 4096;

pub enum Decision {
    Continue,
    Errno(i32),
    Fd(OwnedFd, bool),
    Val(i64),
}

pub struct Supervisor {
    listener: OwnedFd,
    pub policy: Policy,
    state: PathBuf,
    approvals: Vec<Approval>,
    pending: Vec<LibRequest>,
    audit: File,
    hash_cache: HashMap<(u64, u64, i64, i64), String>,
    loaded: BTreeSet<String>,
    root_pid: i32,
    verbose: bool,
    pub allowed: u64,
    pub denied: u64,
    pub new_requests: u64,
}

fn errno_of(e: &io::Error) -> i32 {
    e.raw_os_error().unwrap_or(libc::EIO)
}

/// Escape untrusted text before it reaches the log: input never shares a
/// representation with log framing (newlines, tabs, terminal escapes).
fn esc(s: &str) -> String {
    s.chars().flat_map(|c| c.escape_debug()).collect()
}

struct PreadReader<'a> {
    fd: &'a OwnedFd,
    off: u64,
}
impl Read for PreadReader<'_> {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        let n = sys::read_mem(self.fd.as_raw_fd(), self.off, buf)?;
        self.off += n as u64;
        Ok(n)
    }
}

fn pread_exact(fd: &OwnedFd, off: u64, buf: &mut [u8]) -> bool {
    matches!(sys::read_mem(fd.as_raw_fd(), off, buf), Ok(n) if n == buf.len())
}

fn tgid_of(tid: u32) -> Option<i32> {
    let s = fs::read_to_string(format!("/proc/{tid}/status")).ok()?;
    s.lines().find_map(|l| l.strip_prefix("Tgid:")).and_then(|v| v.trim().parse().ok())
}

fn ppid_of(pid: i32) -> Option<i32> {
    let s = fs::read_to_string(format!("/proc/{pid}/status")).ok()?;
    s.lines().find_map(|l| l.strip_prefix("PPid:")).and_then(|v| v.trim().parse().ok())
}

impl Supervisor {
    pub fn new(listener: OwnedFd, policy: Policy, state: &Path, root_pid: i32, verbose: bool) -> io::Result<Self> {
        let audit = OpenOptions::new().create(true).append(true).open(state.join("audit.log"))?;
        Ok(Supervisor {
            listener,
            policy,
            state: state.to_path_buf(),
            approvals: lg::load_approvals(&state.join("approvals.wdn"))?,
            pending: lg::load_pending(&state.join("pending.wdn"))?,
            audit,
            hash_cache: HashMap::new(),
            loaded: BTreeSet::new(),
            root_pid,
            verbose,
            allowed: 0,
            denied: 0,
            new_requests: 0,
        })
    }

    fn log(&mut self, pid: u32, name: &str, verdict: &str, detail: &str) {
        let ms = SystemTime::now().duration_since(UNIX_EPOCH).map(|d| d.as_millis()).unwrap_or(0);
        let line = format!("{ms} pid={pid} {name} {verdict} {}\n", esc(detail));
        let _ = self.audit.write_all(line.as_bytes());
        if self.verbose && verdict.starts_with("deny") {
            eprint!("warden: {line}");
        }
    }

    /// Serve notifications until every sandboxed task has exited.
    pub fn serve(&mut self) {
        let lfd = self.listener.as_raw_fd();
        loop {
            let mut p = libc::pollfd { fd: lfd, events: libc::POLLIN, revents: 0 };
            let r = unsafe { libc::poll(&mut p, 1, -1) };
            if r < 0 {
                if io::Error::last_os_error().raw_os_error() == Some(libc::EINTR) {
                    continue;
                }
                break;
            }
            if p.revents & libc::POLLIN == 0 && p.revents & (libc::POLLHUP | libc::POLLERR) != 0 {
                break;
            }
            let n = match sys::recv(lfd) {
                Ok(n) => n,
                Err(e) if matches!(e.raw_os_error(), Some(libc::ENOENT) | Some(libc::EINTR)) => continue,
                Err(_) => break,
            };
            self.handle(n);
        }
    }

    fn handle(&mut self, n: Notif) {
        let nr = n.data.nr as u32;
        let name = name_of(nr).unwrap_or("unknown");
        let (d, detail) = match classify(name) {
            _ if name == "ioctl" => (Decision::Errno(libc::EPERM), "terminal-injection ioctl".to_string()),
            Class::Denied => (Decision::Errno(libc::EPERM), "denied by ABI table".to_string()),
            Class::Unimplemented => (Decision::Errno(libc::ENOSYS), format!("nr={nr} not implemented")),
            Class::Pass => (Decision::Continue, String::new()),
            Class::Mediated(f) => match self.mediate(f, name, &n) {
                Ok((d, s)) => (d, s),
                Err((e, s)) => (Decision::Errno(e), s),
            },
        };
        let lfd = self.listener.as_raw_fd();
        // "deny" is a policy refusal; "fail" is an ordinary error such as a
        // missing file, which the program would have seen on Linux too.
        let verdict = match &d {
            Decision::Errno(e) if matches!(*e, libc::EACCES | libc::EPERM | libc::ENOSYS) => {
                self.denied += 1;
                format!("deny errno={e}")
            }
            Decision::Errno(e) => {
                self.allowed += 1;
                format!("fail errno={e}")
            }
            _ => {
                self.allowed += 1;
                "allow".to_string()
            }
        };
        self.log(n.pid, name, &verdict, &detail);
        let _ = match d {
            Decision::Continue => sys::reply_continue(lfd, n.id),
            Decision::Errno(e) => sys::reply_errno(lfd, n.id, e),
            Decision::Val(v) => sys::reply_val(lfd, n.id, v),
            Decision::Fd(fd, cloexec) => match sys::reply_addfd(lfd, n.id, fd.as_raw_fd(), cloexec) {
                Ok(_) => Ok(()),
                // Never leave the caller blocked: if the fd can't be
                // installed, fail the syscall instead.
                Err(e) => {
                    self.log(n.pid, name, "fail", &format!("installing fd: {e}"));
                    sys::reply_errno(lfd, n.id, errno_of(&e))
                }
            },
        };
    }

    // ---- reading the caller's arguments -------------------------------

    fn mem(&self, n: &Notif) -> Result<OwnedFd, (i32, String)> {
        let f = sys::open_path(&format!("/proc/{}/mem", n.pid), libc::O_RDONLY)
            .map_err(|e| (libc::ESRCH, format!("cannot open caller memory: {e}")))?;
        // The pid could have been recycled between recv and open; the id
        // check proves the notification (and so the task) is still live.
        if !sys::id_valid(self.listener.as_raw_fd(), n.id) {
            return Err((libc::ESRCH, "task went away".into()));
        }
        Ok(f)
    }

    fn read_str(&self, n: &Notif, addr: u64) -> Result<String, (i32, String)> {
        if addr == 0 {
            return Err((libc::EFAULT, "null path".into()));
        }
        let mem = self.mem(n)?;
        let mut out = Vec::new();
        let mut chunk = [0u8; 256];
        while out.len() < PATH_MAX {
            let got = sys::read_mem(mem.as_raw_fd(), addr + out.len() as u64, &mut chunk)
                .map_err(|_| (libc::EFAULT, "bad path pointer".to_string()))?;
            if got == 0 {
                return Err((libc::EFAULT, "bad path pointer".into()));
            }
            if let Some(z) = chunk[..got].iter().position(|&b| b == 0) {
                out.extend_from_slice(&chunk[..z]);
                return String::from_utf8(out).map_err(|_| (libc::EACCES, "non-UTF-8 path refused in Phase 0".into()));
            }
            out.extend_from_slice(&chunk[..got]);
        }
        Err((libc::ENAMETOOLONG, "path longer than PATH_MAX".into()))
    }

    fn read_bytes(&self, n: &Notif, addr: u64, len: usize) -> Result<Vec<u8>, (i32, String)> {
        let mem = self.mem(n)?;
        let mut buf = vec![0u8; len];
        match sys::read_mem(mem.as_raw_fd(), addr, &mut buf) {
            Ok(k) if k == len => Ok(buf),
            _ => Err((libc::EFAULT, "bad pointer".into())),
        }
    }

    /// Turn (dirfd, path) from the caller into one absolute, normalized path.
    fn resolve(&self, n: &Notif, dirfd: i32, path: &str) -> Result<String, (i32, String)> {
        if path.is_empty() {
            return Err((libc::ENOENT, "empty path".into()));
        }
        if path.starts_with('/') {
            return Ok(normalize(path));
        }
        let link = if dirfd == AT_FDCWD { format!("/proc/{}/cwd", n.pid) } else { format!("/proc/{}/fd/{}", n.pid, dirfd) };
        let base = fs::read_link(&link).map_err(|_| (libc::EBADF, format!("bad dirfd {dirfd}")))?;
        let base = base.to_str().ok_or((libc::EACCES, "non-UTF-8 base".to_string()))?;
        if !base.starts_with('/') {
            return Err((libc::ENOTDIR, format!("dirfd {dirfd} is not a directory")));
        }
        Ok(normalize(&format!("{base}/{path}")))
    }

    fn path_arg(&self, n: &Notif, dirfd: i32, arg: usize) -> Result<String, (i32, String)> {
        let s = self.read_str(n, n.data.args[arg])?;
        self.resolve(n, dirfd, &s)
    }

    // ---- structural scope ------------------------------------------------

    /// Open `abs` beneath the directory grant that covers it. The returned fd
    /// was always obtained by openat2(RESOLVE_BENEATH) from a granted root, so
    /// it can never name anything outside a grant, whatever the sandbox does
    /// with symlinks or races. Symlinks that leave one grant are followed only
    /// to a target that another grant covers.
    fn open_scoped(&self, abs: &str, flags: i32, mode: u32, need: Need) -> Result<OwnedFd, (i32, String)> {
        let mut cur = abs.to_string();
        for _ in 0..8 {
            let d = self.policy.dir_for(&cur, need).ok_or((libc::EACCES, format!("{cur} outside {need:?} grants")))?;
            let rel = cur[d.root.len()..].trim_start_matches('/');
            let rel = if rel.is_empty() { "." } else { rel };
            match sys::openat2_beneath(Policy::dir_fd(d), rel, flags, mode) {
                Ok(fd) => return Ok(fd),
                Err(e) if errno_of(&e) == libc::EXDEV || (errno_of(&e) == libc::ELOOP && flags & libc::O_NOFOLLOW == 0) => {
                    let canon = canonical(&cur).ok_or((libc::EACCES, format!("{cur}: symlink leaves grant")))?;
                    if canon == cur {
                        return Err((libc::EACCES, format!("{cur}: symlink leaves grant")));
                    }
                    cur = canon;
                }
                Err(e) => return Err((errno_of(&e), cur)),
            }
        }
        Err((libc::ELOOP, format!("{abs}: too many grant hops")))
    }

    fn check_path(&self, abs: &str, nofollow: bool, need: Need) -> Result<(), (i32, String)> {
        let f = libc::O_PATH | if nofollow { libc::O_NOFOLLOW } else { 0 };
        self.open_scoped(abs, f, 0, need).map(|_| ())
    }

    /// For create/delete/rename: the parent directory must be writable.
    fn check_parent_write(&self, abs: &str) -> Result<(), (i32, String)> {
        let (parent, base) = abs.rsplit_once('/').ok_or((libc::EACCES, abs.to_string()))?;
        if base.is_empty() || base == "." || base == ".." {
            return Err((libc::EACCES, format!("{abs}: bad final component")));
        }
        let parent = if parent.is_empty() { "/" } else { parent };
        self.open_scoped(parent, libc::O_PATH | libc::O_DIRECTORY, 0, Need::Write).map(|_| ())
    }

    // ---- libraries -----------------------------------------------------

    fn hash_fd(&mut self, fd: &OwnedFd) -> io::Result<String> {
        let m = File::from(fd.try_clone()?).metadata()?;
        let key = (m.dev(), m.ino(), m.size() as i64, m.mtime_nsec() + m.mtime() * 1_000_000_000);
        if let Some(h) = self.hash_cache.get(&key) {
            return Ok(h.clone());
        }
        let h = lg::sha256_reader(PreadReader { fd, off: 0 })?;
        self.hash_cache.insert(key, h.clone());
        Ok(h)
    }

    /// If `fd` is an ELF shared object, it may only be handed to the sandbox
    /// if an approval with a ticked "load" box exists for its exact hash.
    /// Ticked permissions on that approval join the sandbox's authority.
    fn lib_gate(&mut self, fd: &OwnedFd, path: &str) -> Result<Option<String>, (i32, String)> {
        let mut hdr = [0u8; 18];
        if !pread_exact(fd, 0, &mut hdr) || &hdr[..4] != b"\x7fELF" || u16::from_le_bytes([hdr[16], hdr[17]]) != 3 {
            return Ok(None);
        }
        let sha = self.hash_fd(fd).map_err(|e| (libc::EIO, format!("hash {path}: {e}")))?;
        let name = path.rsplit('/').next().unwrap_or(path).to_string();
        match self.approvals.iter().find(|a| a.sha256 == sha).cloned() {
            Some(a) if a.may_load() => {
                let mut note = format!("library {name} sha256={} approved", &sha[..16]);
                if self.loaded.insert(sha.clone()) {
                    for g in a.granted.iter().filter(|g| g.perm != "load") {
                        match self.policy.add(g) {
                            Ok(true) => note.push_str(&format!("; brings {}", g.render())),
                            Ok(false) => {}
                            Err(e) => note.push_str(&format!("; grant {} unusable: {e}", g.render())),
                        }
                    }
                }
                Ok(Some(note))
            }
            Some(_) => Err((libc::EACCES, format!("library {name} sha256={}: load box not ticked", &sha[..16]))),
            None => {
                if !self.pending.iter().any(|q| q.sha256 == sha) {
                    let wants = lg::wants_for(&self.state.join("libwants.wdn"), &name).unwrap_or_default();
                    self.pending.push(LibRequest { sha256: sha.clone(), name: name.clone(), path: path.to_string(), wants });
                    let _ = lg::save_pending(&self.state.join("pending.wdn"), &self.pending);
                    self.new_requests += 1;
                    if !self.verbose {
                        eprintln!("warden: library {name} is not approved yet; request saved (run `warden approve`)");
                    }
                }
                Err((libc::EACCES, format!("library {name} sha256={} not approved; request queued", &sha[..16])))
            }
        }
    }

    // ---- exec ------------------------------------------------------------

    /// Checks done before the kernel runs execve: the program is inside an
    /// exec grant, and its ELF interpreter (ld.so) or `#!` interpreter is an
    /// approved library / inside an exec grant.
    fn check_exec(&mut self, abs: &str) -> Result<String, (i32, String)> {
        let fd = self.open_scoped(abs, libc::O_RDONLY, 0, Need::Exec)?;
        let mut head = [0u8; 64];
        let got = sys::read_mem(fd.as_raw_fd(), 0, &mut head).unwrap_or(0);
        if got >= 2 && &head[..2] == b"#!" {
            let line = &head[2..got];
            let end = line.iter().position(|&b| b == b'\n').unwrap_or(line.len());
            let interp = String::from_utf8_lossy(&line[..end]).split_whitespace().next().unwrap_or("").to_string();
            if !interp.starts_with('/') {
                return Err((libc::EACCES, format!("{abs}: bad #! line")));
            }
            let note = self.check_exec(&normalize(&interp))?;
            return Ok(format!("{abs} via {interp} ({note})"));
        }
        if got < 64 || &head[..4] != b"\x7fELF" {
            return Ok(format!("{abs} (not ELF; kernel decides)"));
        }
        let phoff = u64::from_le_bytes(head[32..40].try_into().unwrap());
        let phentsize = u16::from_le_bytes([head[54], head[55]]) as u64;
        let phnum = u16::from_le_bytes([head[56], head[57]]) as u64;
        for i in 0..phnum.min(64) {
            let mut ph = [0u8; 56];
            if phentsize < 56 || !pread_exact(&fd, phoff + i * phentsize, &mut ph) {
                break;
            }
            if u32::from_le_bytes(ph[0..4].try_into().unwrap()) == 3 {
                let off = u64::from_le_bytes(ph[8..16].try_into().unwrap());
                let sz = (u64::from_le_bytes(ph[32..40].try_into().unwrap()) as usize).min(PATH_MAX);
                let mut buf = vec![0u8; sz];
                if !pread_exact(&fd, off, &mut buf) {
                    return Err((libc::ENOEXEC, format!("{abs}: bad PT_INTERP")));
                }
                let interp = String::from_utf8_lossy(&buf).trim_end_matches('\0').to_string();
                let ifd = self.open_scoped(&normalize(&interp), libc::O_RDONLY, 0, Need::Read)?;
                let note = self.lib_gate(&ifd, &interp)?;
                return Ok(format!("{abs} interp {interp} {}", note.unwrap_or_default()));
            }
        }
        Ok(format!("{abs} (static)"))
    }

    // ---- sandbox membership for signals ------------------------------------

    fn in_sandbox(&self, pid: i32) -> bool {
        let mut p = pid;
        for _ in 0..64 {
            if p == self.root_pid {
                return true;
            }
            match ppid_of(p) {
                Some(pp) if pp > 1 => p = pp,
                _ => return false,
            }
        }
        false
    }

    // ---- the dispatch ------------------------------------------------------

    fn mediate(&mut self, fam: Family, name: &str, n: &Notif) -> Result<(Decision, String), (i32, String)> {
        let a = n.data.args;
        let dfd = |i: usize| a[i] as i32;
        match (fam, name) {
            (Family::Fs, "open" | "openat" | "creat") => {
                let (dirfd, pi, flags, mode) = match name {
                    "open" => (AT_FDCWD, 0, a[1] as i32, a[2] as u32),
                    "creat" => (AT_FDCWD, 0, libc::O_CREAT | libc::O_WRONLY | libc::O_TRUNC, a[1] as u32),
                    _ => (dfd(0), 1, a[2] as i32, a[3] as u32),
                };
                let abs = self.path_arg(n, dirfd, pi)?;
                let acc = flags & libc::O_ACCMODE;
                // O_TMPFILE contains the O_DIRECTORY bit, so test it as a whole value.
                let writes = acc != libc::O_RDONLY
                    || flags & (libc::O_CREAT | libc::O_TRUNC | libc::O_APPEND) != 0
                    || flags & libc::O_TMPFILE == libc::O_TMPFILE;
                let need = if writes { Need::Write } else { Need::Read };
                if flags & libc::O_PATH != 0 {
                    // The kernel can't install O_PATH files into another
                    // process (fget refuses them), so check and let it run.
                    // An O_PATH fd only reaches metadata, and every later use
                    // of it as a dirfd is re-resolved and re-checked here.
                    self.check_path(&abs, flags & libc::O_NOFOLLOW != 0, need)?;
                    return Ok((Decision::Continue, format!("{abs} O_PATH")));
                }
                let fd = self.open_scoped(&abs, flags & !libc::O_CLOEXEC, mode, need)?;
                let mut detail = format!("{abs} {need:?}");
                if !writes && flags & libc::O_PATH == 0 {
                    if let Some(note) = self.lib_gate(&fd, &abs)? {
                        detail = note;
                    }
                }
                Ok((Decision::Fd(fd, flags & libc::O_CLOEXEC != 0), detail))
            }
            (Family::Fs, "stat" | "lstat" | "newfstatat" | "statx" | "access" | "faccessat" | "faccessat2" | "readlink" | "readlinkat" | "chdir" | "statfs" | "getxattr" | "lgetxattr" | "listxattr" | "llistxattr") => {
                let (dirfd, pi, flags) = match name {
                    "stat" | "access" | "chdir" | "statfs" | "getxattr" | "listxattr" => (AT_FDCWD, 0, 0),
                    "lstat" | "readlink" | "lgetxattr" | "llistxattr" => (AT_FDCWD, 0, AT_SYMLINK_NOFOLLOW),
                    "newfstatat" | "faccessat2" => (dfd(0), 1, a[3]),
                    "statx" => (dfd(0), 1, a[2]),
                    "readlinkat" => (dfd(0), 1, AT_SYMLINK_NOFOLLOW),
                    _ => (dfd(0), 1, 0),
                };
                let raw = self.read_str(n, a[pi])?;
                if raw.is_empty() && flags & AT_EMPTY_PATH != 0 {
                    return Ok((Decision::Continue, "fstat of own fd".into()));
                }
                let abs = self.resolve(n, dirfd, &raw)?;
                let mode = match name { "access" => a[1], "faccessat" | "faccessat2" => a[2], _ => 0 };
                let need = if mode & libc::W_OK as u64 != 0 { Need::Write } else { Need::Read };
                self.check_path(&abs, flags & AT_SYMLINK_NOFOLLOW != 0, need)?;
                // Phase 0 race: metadata is checked, then the kernel re-reads
                // the path. Worst case leaks metadata, never file contents.
                Ok((Decision::Continue, abs))
            }
            (Family::Fs, "mkdir" | "rmdir" | "unlink" | "symlink") => {
                let pi = if name == "symlink" { 1 } else { 0 };
                let abs = self.path_arg(n, AT_FDCWD, pi)?;
                self.check_parent_write(&abs)?;
                Ok((Decision::Continue, abs))
            }
            (Family::Fs, "mkdirat" | "unlinkat") => {
                let abs = self.path_arg(n, dfd(0), 1)?;
                self.check_parent_write(&abs)?;
                Ok((Decision::Continue, abs))
            }
            (Family::Fs, "symlinkat") => {
                let abs = self.path_arg(n, dfd(1), 2)?;
                self.check_parent_write(&abs)?;
                Ok((Decision::Continue, abs))
            }
            (Family::Fs, "rename" | "link") => {
                let from = self.path_arg(n, AT_FDCWD, 0)?;
                let to = self.path_arg(n, AT_FDCWD, 1)?;
                self.check_path(&from, true, Need::Write)?;
                self.check_parent_write(&from)?;
                self.check_parent_write(&to)?;
                Ok((Decision::Continue, format!("{from} -> {to}")))
            }
            (Family::Fs, "renameat" | "renameat2" | "linkat") => {
                let from = self.path_arg(n, dfd(0), 1)?;
                let to = self.path_arg(n, dfd(2), 3)?;
                self.check_path(&from, true, Need::Write)?;
                self.check_parent_write(&from)?;
                self.check_parent_write(&to)?;
                Ok((Decision::Continue, format!("{from} -> {to}")))
            }
            (Family::Fs, "truncate" | "chmod") => {
                let abs = self.path_arg(n, AT_FDCWD, 0)?;
                self.check_path(&abs, false, Need::Write)?;
                Ok((Decision::Continue, abs))
            }
            (Family::Fs, "fchmodat") => {
                let abs = self.path_arg(n, dfd(0), 1)?;
                self.check_path(&abs, false, Need::Write)?;
                Ok((Decision::Continue, abs))
            }
            (Family::Exec, "execve") => {
                let abs = self.path_arg(n, AT_FDCWD, 0)?;
                let note = self.check_exec(&abs)?;
                // Phase 0 race: the kernel re-reads the path after this check.
                // Inside Warden (Phase 1) exec is a loader service, not a path.
                Ok((Decision::Continue, note))
            }
            (Family::Net, "socket") => {
                let dom = a[0] as i32;
                let ty = (a[1] as i32) & 0xf;
                if (dom == libc::AF_INET || dom == libc::AF_INET6) && ty == libc::SOCK_STREAM && self.policy.any_net() {
                    Ok((Decision::Continue, format!("domain={dom} type=stream")))
                } else {
                    Err((libc::EACCES, format!("socket domain={dom} type={ty}: only TCP with a net grant")))
                }
            }
            (Family::Net, "connect" | "bind") => {
                let len = (a[2] as usize).min(128);
                let raw = self.read_bytes(n, a[1], len)?;
                let (host, port) = parse_sockaddr(&raw).ok_or((libc::EAFNOSUPPORT, "unsupported address".to_string()))?;
                let listen = name == "bind";
                if !self.policy.net_allowed(&host, port, listen) {
                    return Err((libc::EACCES, format!("{host}:{port} not granted")));
                }
                // Do it ourselves on a duplicate of their socket, with our
                // copy of the address: nothing left for the sandbox to race.
                let tgid = tgid_of(n.pid).ok_or((libc::ESRCH, "no tgid".to_string()))?;
                let pidfd = sys::pidfd_open(tgid).map_err(|e| (libc::ESRCH, e.to_string()))?;
                let sock = sys::pidfd_getfd(pidfd.as_raw_fd(), a[0] as RawFd).map_err(|e| (errno_of(&e), "bad socket fd".to_string()))?;
                let err = if listen { sys::bind_raw(sock.as_raw_fd(), &raw) } else { sys::connect_raw(sock.as_raw_fd(), &raw) };
                if err == 0 || err == libc::EINPROGRESS {
                    if err == 0 { Ok((Decision::Val(0), format!("{host}:{port}"))) } else { Err((err, format!("{host}:{port} in progress"))) }
                } else {
                    Err((err, format!("{host}:{port}")))
                }
            }
            (Family::Net, "listen") => {
                if self.policy.nets.iter().any(|g| g.listen) { Ok((Decision::Continue, String::new())) } else { Err((libc::EACCES, "no net.listen grant".into())) }
            }
            (Family::Net, "sendto" | "sendmsg" | "sendmmsg") => {
                let flags = match name { "sendmsg" => a[2], _ => a[3] };
                if flags & MSG_FASTOPEN != 0 {
                    Err((libc::EACCES, "MSG_FASTOPEN would connect without a check".into()))
                } else {
                    // Only stream sockets can exist, so a destination address
                    // is ignored by the kernel (or rejected with EISCONN).
                    Ok((Decision::Continue, String::new()))
                }
            }
            (Family::Proc, "clone") => {
                let flags = a[0];
                if flags & CLONE_NS_MASK != 0 {
                    return Err((libc::EPERM, format!("clone with namespace flags {flags:#x}")));
                }
                if flags & CLONE_THREAD == 0 && !self.policy.may_spawn() {
                    return Err((libc::EPERM, "new process needs proc.spawn".into()));
                }
                Ok((Decision::Continue, format!("flags={flags:#x}")))
            }
            (Family::Proc, "fork" | "vfork") => {
                if self.policy.may_spawn() { Ok((Decision::Continue, String::new())) } else { Err((libc::EPERM, "needs proc.spawn".into())) }
            }
            (Family::Signal, "kill") => {
                let pid = a[0] as i32;
                let ok = pid == 0 || (pid > 0 && self.in_sandbox(pid)) || (pid < -1 && -pid == self.root_pid);
                if ok { Ok((Decision::Continue, format!("target {pid}"))) } else { Err((libc::EPERM, format!("target {pid} outside sandbox"))) }
            }
            (Family::Signal, "tkill" | "tgkill") => {
                let t = a[0] as i32;
                let ok = t > 0 && (self.in_sandbox(t) || tgid_of(t as u32).map(|g| self.in_sandbox(g)).unwrap_or(false));
                if ok { Ok((Decision::Continue, format!("target {t}"))) } else { Err((libc::EPERM, format!("target {t} outside sandbox"))) }
            }
            _ => Err((libc::ENOSYS, format!("{name}: mediated family {} has no handler yet", fam.name()))),
        }
    }

    pub fn approvals_count(&self) -> usize {
        self.approvals.len()
    }
}

/// Resolve symlinks in `p` the normal way, only to learn where it points.
/// The result is re-opened under a grant, so a race here can't widen access.
fn canonical(p: &str) -> Option<String> {
    let real = |q: &str| -> Option<String> {
        let fd = sys::open_path(q, libc::O_PATH).ok()?;
        fs::read_link(format!("/proc/self/fd/{}", fd.as_raw_fd())).ok()?.to_str().map(str::to_string)
    };
    if let Some(r) = real(p) {
        return Some(r);
    }
    let (parent, base) = p.rsplit_once('/')?;
    let parent = if parent.is_empty() { "/" } else { parent };
    let r = real(parent)?;
    Some(if r == "/" { format!("/{base}") } else { format!("{r}/{base}") })
}

fn parse_sockaddr(b: &[u8]) -> Option<(String, u16)> {
    if b.len() < 2 {
        return None;
    }
    let fam = u16::from_ne_bytes([b[0], b[1]]) as i32;
    if fam == libc::AF_INET && b.len() >= 8 {
        let port = u16::from_be_bytes([b[2], b[3]]);
        Some((format!("{}.{}.{}.{}", b[4], b[5], b[6], b[7]), port))
    } else if fam == libc::AF_INET6 && b.len() >= 24 {
        let port = u16::from_be_bytes([b[2], b[3]]);
        let ip = std::net::Ipv6Addr::from(<[u8; 16]>::try_from(&b[8..24]).ok()?);
        Some((format!("[{ip}]"), port))
    } else {
        None
    }
}
