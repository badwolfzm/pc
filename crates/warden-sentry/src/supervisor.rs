//! The supervisor: receives every non-`Pass` syscall of the sandbox from the
//! kernel, decides it against the capability policy, and answers.
//!
//! Rule for safe answers: a decision may only use `CONTINUE` (let the kernel
//! run the original syscall) when it depends on register arguments, or when
//! the remaining race is documented below as a Phase 0 limitation. Where a
//! decision depends on memory the sandbox can change (paths, socket
//! addresses), the supervisor performs the operation itself on its own copy
//! of the argument and hands back the result (an installed fd, a return
//! value, or bytes written into the caller's buffer). That removes
//! check-then-use races for open, metadata, create/delete/rename/link,
//! chmod/truncate, connect and bind.
//!
//! Two calls still `CONTINUE` on a path: `chdir`, whose only effect is a cwd
//! that every later relative path is re-resolved and re-checked against, and
//! `execve`, which no other process can perform. For exec the binary is
//! pinned and checked first, and the new image is verified right after the
//! kernel has loaded it (see `PendingExec`); a swapped image is killed.

use crate::policy::{normalize, under, Active, Need, Policy};
use crate::sys::{self, Notif};
use std::collections::HashMap;
use std::ffi::CString;
use std::fs::{self, File, OpenOptions};
use std::io::{self, Read, Write};
use std::os::fd::{AsRawFd, FromRawFd, OwnedFd, RawFd};
use std::os::unix::fs::MetadataExt;
use std::path::{Path, PathBuf};
use std::time::{SystemTime, UNIX_EPOCH};
use warden_abi::{classify, name_of, Class, Family};
use warden_libgrant::{self as lg, Approval, LibRequest};

const AT_FDCWD: i32 = -100;
const AT_EMPTY_PATH: u64 = 0x1000;
const AT_SYMLINK_NOFOLLOW: u64 = 0x100;
const AT_SYMLINK_FOLLOW: u64 = 0x400;
const AT_EACCESS: u64 = 0x200;
const AT_REMOVEDIR: u64 = 0x200;
const XATTR_SIZE_MAX: usize = 65536;
/// How long a freshly exec'd image may run before its first check, when it
/// makes no mediated syscall that would trigger one earlier.
const EXEC_VERIFY_POLL_MS: i32 = 5;
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

/// An exec the kernel is running (or has run) that we still have to verify.
/// `execve` cannot be performed on the caller's behalf, so the supervisor
/// checks the binary, lets the kernel run the syscall, and then compares the
/// process image against what was checked: the executable must be the pinned
/// inode (or the old one, if exec failed) and every file-backed mapping must
/// lie under a grant. Anything else is killed through the pidfd.
struct PendingExec {
    pidfd: OwnedFd,
    /// The image before exec: still there if exec failed.
    old: (u64, u64),
    /// Images checked for this task group, oldest first; the last is the
    /// one we expect to see.
    checked: Vec<(u64, u64)>,
}

pub struct Supervisor {
    listener: OwnedFd,
    pub policy: Policy,
    state: PathBuf,
    approvals: Vec<Approval>,
    pending: Vec<LibRequest>,
    audit: File,
    hash_cache: HashMap<(u64, u64, i64, i64), String>,
    /// Approved libraries by (real path, inode), the identity `/proc/PID/maps`
    /// shows, so the libraries a process has mapped can be recognized.
    lib_ids: HashMap<(String, u64), String>,
    /// The caller of the notification being handled: which approved
    /// libraries it has mapped. Library-brought grants apply through this.
    active: Active,
    pending_exec: HashMap<i32, PendingExec>,
    /// Device of anonymous shared memory (memfd, MAP_SHARED|MAP_ANONYMOUS,
    /// SysV shm) as `maps` prints it: such mappings are the process's own.
    shmem_dev: String,
    /// Files the sandbox inherited from `warden run` (stdio): mapping them
    /// is not an escape.
    inherited: Vec<(String, u64)>,
    root_pid: i32,
    verbose: bool,
    pub allowed: u64,
    pub denied: u64,
    pub new_requests: u64,
    pub killed: u64,
}

/// A `#[repr(C)]` result the kernel would have copied to user space.
fn bytes_of<T>(v: &T) -> &[u8] {
    unsafe { std::slice::from_raw_parts(v as *const T as *const u8, std::mem::size_of::<T>()) }
}

fn is_dir(fd: &OwnedFd) -> Option<bool> {
    let mut st: libc::stat = unsafe { std::mem::zeroed() };
    if unsafe { libc::fstat(fd.as_raw_fd(), &mut st) } != 0 {
        return None;
    }
    Some(st.st_mode & libc::S_IFMT == libc::S_IFDIR)
}

fn is_symlink(fd: &OwnedFd) -> bool {
    let mut st: libc::stat = unsafe { std::mem::zeroed() };
    unsafe { libc::fstat(fd.as_raw_fd(), &mut st) == 0 && st.st_mode & libc::S_IFMT == libc::S_IFLNK }
}

/// Real path of the object behind an fd (what `maps` and `exe` print).
fn real_path(fd: &OwnedFd) -> Option<String> {
    fs::read_link(format!("/proc/self/fd/{}", fd.as_raw_fd())).ok()?.to_str().map(str::to_string)
}

fn umask_of(pid: u32) -> Option<u32> {
    let s = fs::read_to_string(format!("/proc/{pid}/status")).ok()?;
    let v = s.lines().find_map(|l| l.strip_prefix("Umask:"))?;
    u32::from_str_radix(v.trim(), 8).ok()
}

/// One file-backed line of `/proc/PID/maps`: (dev, inode, path).
fn maps_files(pid: i32) -> io::Result<Vec<(String, u64, String)>> {
    let s = fs::read_to_string(format!("/proc/{pid}/maps"))?;
    let mut out = Vec::new();
    for l in s.lines() {
        let mut it = l.splitn(6, ' ');
        let (_addr, _perms, _off, dev, ino, rest) = (it.next(), it.next(), it.next(), it.next(), it.next(), it.next());
        let (Some(dev), Some(ino), Some(rest)) = (dev, ino, rest) else { continue };
        let ino: u64 = ino.parse().unwrap_or(0);
        let path = rest.trim_start();
        if ino == 0 || path.is_empty() || path.starts_with('[') {
            continue;
        }
        let path = path.strip_suffix(" (deleted)").unwrap_or(path);
        out.push((dev.to_string(), ino, path.to_string()));
    }
    Ok(out)
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
        let shmem_dev = {
            let name = CString::new("warden-probe").unwrap();
            let fd = unsafe { libc::memfd_create(name.as_ptr(), libc::MFD_CLOEXEC) };
            if fd < 0 {
                return Err(io::Error::last_os_error());
            }
            let fd = unsafe { OwnedFd::from_raw_fd(fd) };
            sys::maps_dev(sys::ident(fd.as_raw_fd())?.0)
        };
        let inherited = (0..3)
            .filter_map(|fd| {
                let ino = sys::ident(fd).ok()?.1;
                let path = fs::read_link(format!("/proc/self/fd/{fd}")).ok()?.to_str()?.to_string();
                Some((path, ino))
            })
            .collect();
        Ok(Supervisor {
            listener,
            policy,
            state: state.to_path_buf(),
            approvals: lg::load_approvals(&state.join("approvals.wdn"))?,
            pending: lg::load_pending(&state.join("pending.wdn"))?,
            audit,
            hash_cache: HashMap::new(),
            lib_ids: HashMap::new(),
            active: Active::new(),
            pending_exec: HashMap::new(),
            shmem_dev,
            inherited,
            root_pid,
            verbose,
            allowed: 0,
            denied: 0,
            new_requests: 0,
            killed: 0,
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
            let timeout = if self.pending_exec.is_empty() { -1 } else { EXEC_VERIFY_POLL_MS };
            let r = unsafe { libc::poll(&mut p, 1, timeout) };
            if r < 0 {
                if io::Error::last_os_error().raw_os_error() == Some(libc::EINTR) {
                    continue;
                }
                break;
            }
            if r == 0 {
                self.verify_execs(None);
                continue;
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
        // A syscall from a task group with an unverified exec means that
        // exec has returned: verify the image now, before serving anything.
        if !self.pending_exec.is_empty() {
            if let Some(tgid) = tgid_of(n.pid) {
                if self.pending_exec.contains_key(&tgid) && !self.verify_execs(Some(tgid)) {
                    self.log(n.pid, name, "deny errno=1", "process image failed exec verification; killed");
                    self.denied += 1;
                    let _ = sys::reply_errno(self.listener.as_raw_fd(), n.id, libc::EPERM);
                    return;
                }
            }
        }
        self.active = if self.policy.has_lib_grants() { self.mapped_libs(n.pid) } else { Active::new() };
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

    /// Copy a result into the caller's buffer, as the kernel would have.
    fn write_bytes(&self, n: &Notif, addr: u64, bytes: &[u8]) -> Result<(), (i32, String)> {
        if bytes.is_empty() {
            return Ok(());
        }
        if addr == 0 {
            return Err((libc::EFAULT, "null buffer".into()));
        }
        let f = sys::open_path(&format!("/proc/{}/mem", n.pid), libc::O_RDWR)
            .map_err(|e| (libc::ESRCH, format!("cannot open caller memory: {e}")))?;
        if !sys::id_valid(self.listener.as_raw_fd(), n.id) {
            return Err((libc::ESRCH, "task went away".into()));
        }
        match sys::write_mem(f.as_raw_fd(), addr, bytes) {
            Ok(k) if k == bytes.len() => Ok(()),
            _ => Err((libc::EFAULT, "bad buffer pointer".into())),
        }
    }

    /// Run `f` with the caller's umask, so files the supervisor creates on
    /// its behalf get the modes the caller would have produced.
    fn with_umask<T>(&self, n: &Notif, f: impl FnOnce() -> T) -> T {
        let theirs = umask_of(n.pid);
        let ours = theirs.map(|m| unsafe { libc::umask(m as libc::mode_t) });
        let r = f();
        if let Some(o) = ours {
            unsafe { libc::umask(o) };
        }
        r
    }

    /// Which approved libraries `pid` has mapped right now, by identity.
    fn mapped_libs(&self, pid: u32) -> Active {
        let mut a = Active::new();
        if let Ok(files) = maps_files(pid as i32) {
            for (_dev, ino, path) in files {
                if let Some(sha) = self.lib_ids.get(&(path, ino)) {
                    a.insert(sha.clone());
                }
            }
        }
        a
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
            let d = self.policy.dir_for(&cur, need, &self.active).ok_or((libc::EACCES, format!("{cur} outside {need:?} grants")))?;
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

    /// Pin the object `abs` names right now, under a grant, as an O_PATH fd.
    /// Everything the supervisor then does through this fd (or through
    /// `/proc/self/fd/N`) reaches that object and nothing the sandbox can
    /// swap in afterwards.
    fn pin(&self, abs: &str, nofollow: bool, need: Need) -> Result<OwnedFd, (i32, String)> {
        let f = libc::O_PATH | if nofollow { libc::O_NOFOLLOW } else { 0 };
        self.open_scoped(abs, f, 0, need)
    }

    /// For create/delete/rename/link: pin the parent directory under a
    /// write grant and return it with the final component. The operation
    /// is then done as `*at(parent_fd, base)`, so it can only touch an entry
    /// of that pinned directory.
    fn parent_dir(&self, abs: &str) -> Result<(OwnedFd, CString), (i32, String)> {
        let (parent, base) = abs.rsplit_once('/').ok_or((libc::EACCES, abs.to_string()))?;
        if base.is_empty() || base == "." || base == ".." {
            return Err((libc::EACCES, format!("{abs}: bad final component")));
        }
        let parent = if parent.is_empty() { "/" } else { parent };
        let fd = self.open_scoped(parent, libc::O_PATH | libc::O_DIRECTORY, 0, Need::Write)?;
        Ok((fd, CString::new(base).map_err(|_| (libc::EINVAL, "NUL in path".to_string()))?))
    }

    /// Return value of a libc call the supervisor made for the caller.
    fn result(r: libc::c_long) -> Result<Decision, (i32, String)> {
        if r < 0 { Err((sys::errno(), String::new())) } else { Ok(Decision::Val(r as i64)) }
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
                if let (Some(real), Ok((_, ino))) = (real_path(fd), sys::ident(fd.as_raw_fd())) {
                    self.lib_ids.insert((real, ino), sha.clone());
                }
                // Grants a library brings are held with the library as their
                // origin: they count only for a process that has the library
                // mapped, and only while it stays mapped.
                for g in a.granted.iter().filter(|g| g.perm != "load") {
                    match self.policy.add_from(g, Some(sha.clone())) {
                        Ok(true) => note.push_str(&format!("; brings {}", g.render())),
                        Ok(false) => {}
                        Err(e) => note.push_str(&format!("; grant {} unusable: {e}", g.render())),
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
    /// approved library / inside an exec grant. Returns the note and the
    /// identity of the file the kernel must end up running.
    fn check_exec(&mut self, abs: &str) -> Result<(String, (u64, u64)), (i32, String)> {
        let fd = self.open_scoped(abs, libc::O_RDONLY, 0, Need::Exec)?;
        let id = sys::ident(fd.as_raw_fd()).map_err(|e| (errno_of(&e), abs.to_string()))?;
        let mut head = [0u8; 64];
        let got = sys::read_mem(fd.as_raw_fd(), 0, &mut head).unwrap_or(0);
        if got >= 2 && &head[..2] == b"#!" {
            let line = &head[2..got];
            let end = line.iter().position(|&b| b == b'\n').unwrap_or(line.len());
            let interp = String::from_utf8_lossy(&line[..end]).split_whitespace().next().unwrap_or("").to_string();
            if !interp.starts_with('/') {
                return Err((libc::EACCES, format!("{abs}: bad #! line")));
            }
            // The kernel runs the interpreter as the image; the script is
            // only an argument. Verify the interpreter's identity.
            let (note, iid) = self.check_exec(&normalize(&interp))?;
            return Ok((format!("{abs} via {interp} ({note})"), iid));
        }
        if got < 64 || &head[..4] != b"\x7fELF" {
            return Ok((format!("{abs} (not ELF; kernel decides)"), id));
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
                return Ok((format!("{abs} interp {interp} {}", note.unwrap_or_default()), id));
            }
        }
        Ok((format!("{abs} (static)"), id))
    }

    /// Remember that `tgid` is exec'ing `expect`, to be verified once the
    /// kernel has run the exec.
    fn expect_exec(&mut self, tgid: i32, expect: (u64, u64)) -> Result<(), (i32, String)> {
        if let Some(p) = self.pending_exec.get_mut(&tgid) {
            p.checked.push(expect);
            return Ok(());
        }
        let pidfd = sys::pidfd_open(tgid).map_err(|e| (libc::ESRCH, e.to_string()))?;
        let old = sys::ident_path(&format!("/proc/{tgid}/exe")).map_err(|e| (libc::ESRCH, e.to_string()))?;
        self.pending_exec.insert(tgid, PendingExec { pidfd, old, checked: vec![expect] });
        Ok(())
    }

    /// Is `path` (as `maps` prints it) somewhere the process may map from?
    fn mapping_allowed(&self, dev: &str, ino: u64, path: &str) -> bool {
        dev == self.shmem_dev
            || self.policy.readable_roots().any(|r| under(r, path))
            || self.inherited.iter().any(|(p, i)| p == path && *i == ino)
    }

    /// Check every pending exec (or just `only`). Returns false when `only`
    /// was killed. An image is accepted when its executable is the checked
    /// inode and every file-backed mapping lies under a grant (or is the
    /// process's own anonymous shared memory, or inherited stdio). A task
    /// that still shows the old image is left pending unless `settled` says
    /// its exec syscall has returned, which means it failed.
    fn verify_execs(&mut self, only: Option<i32>) -> bool {
        let tgids: Vec<i32> = match only {
            Some(t) => vec![t],
            None => self.pending_exec.keys().copied().collect(),
        };
        let mut ok = true;
        for tgid in tgids {
            let Some(p) = self.pending_exec.get(&tgid) else { continue };
            let exe = match sys::ident_path(&format!("/proc/{tgid}/exe")) {
                Ok(e) => e,
                Err(_) => {
                    // Gone (exited or already a zombie): nothing left to verify.
                    self.pending_exec.remove(&tgid);
                    continue;
                }
            };
            let latest = *p.checked.last().unwrap();
            let verdict = if exe == latest || p.checked.contains(&exe) {
                match maps_files(tgid) {
                    Ok(files) => {
                        let bad: Vec<_> = files.iter().filter(|(d, i, path)| !self.mapping_allowed(d, *i, path)).map(|(_, _, p)| p.clone()).collect();
                        if bad.is_empty() {
                            if exe == latest { Some(Ok(())) } else { None }
                        } else {
                            Some(Err(format!("maps files outside grants: {}", bad.join(", "))))
                        }
                    }
                    Err(_) => {
                        self.pending_exec.remove(&tgid);
                        continue;
                    }
                }
            } else if exe == p.old {
                // Exec not finished, or failed. Only a later syscall from the
                // task proves it returned.
                if only == Some(tgid) { Some(Ok(())) } else { None }
            } else {
                Some(Err("executable is not the checked file".into()))
            };
            match verdict {
                None => {}
                Some(Ok(())) => {
                    self.pending_exec.remove(&tgid);
                }
                Some(Err(why)) => {
                    let p = self.pending_exec.remove(&tgid).unwrap();
                    let _ = sys::pidfd_kill(p.pidfd.as_raw_fd());
                    self.killed += 1;
                    self.log(tgid as u32, "execve", "kill", &format!("after exec: {why}"));
                    if self.verbose {
                        eprintln!("warden: killed pid {tgid}: {why}");
                    }
                    if only == Some(tgid) {
                        ok = false;
                    }
                }
            }
        }
        ok
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
                if flags & libc::O_PATH != 0 {
                    // The kernel ignores every other flag but O_NOFOLLOW,
                    // O_DIRECTORY and O_CLOEXEC with O_PATH, and refuses to
                    // install an O_PATH fd into another process. So pin the
                    // object under a read grant and hand out a read-only fd
                    // to that same object instead: same inode, a little more
                    // than O_PATH (read() works, on something the caller
                    // could open for reading anyway), and no path left to
                    // race. A symlink can't be re-opened this way: ELOOP, as
                    // open(O_NOFOLLOW) without O_PATH would say.
                    let fd = self.pin(&abs, flags & libc::O_NOFOLLOW != 0, Need::Read)?;
                    if is_symlink(&fd) {
                        return Err((libc::ELOOP, format!("{abs}: O_PATH on a symlink")));
                    }
                    let dir = is_dir(&fd).ok_or((libc::EIO, abs.clone()))?;
                    if flags & libc::O_DIRECTORY != 0 && !dir {
                        return Err((libc::ENOTDIR, abs));
                    }
                    let f2 = libc::O_RDONLY | libc::O_NONBLOCK | libc::O_NOCTTY | if dir { libc::O_DIRECTORY } else { 0 };
                    let re = sys::open_path(sys::proc_fd_path(fd.as_raw_fd()).to_str().unwrap(), f2).map_err(|e| (errno_of(&e), abs.clone()))?;
                    if sys::ident(fd.as_raw_fd()).ok() != sys::ident(re.as_raw_fd()).ok() {
                        return Err((libc::EIO, format!("{abs}: re-open changed identity")));
                    }
                    if flags & libc::O_NONBLOCK == 0 {
                        unsafe { libc::fcntl(re.as_raw_fd(), libc::F_SETFL, libc::O_RDONLY) };
                    }
                    return Ok((Decision::Fd(re, flags & libc::O_CLOEXEC != 0), format!("{abs} O_PATH (read-only fd to the pinned object)")));
                }
                let acc = flags & libc::O_ACCMODE;
                // O_TMPFILE contains the O_DIRECTORY bit, so test it as a whole value.
                let writes = acc != libc::O_RDONLY
                    || flags & (libc::O_CREAT | libc::O_TRUNC | libc::O_APPEND) != 0
                    || flags & libc::O_TMPFILE == libc::O_TMPFILE;
                let need = if writes { Need::Write } else { Need::Read };
                let fd = self.with_umask(n, || self.open_scoped(&abs, flags & !libc::O_CLOEXEC, mode, need))?;
                let mut detail = format!("{abs} {need:?}");
                if !writes {
                    if let Some(note) = self.lib_gate(&fd, &abs)? {
                        detail = note;
                    }
                }
                Ok((Decision::Fd(fd, flags & libc::O_CLOEXEC != 0), detail))
            }
            // Metadata: pin the object under a grant, read its metadata
            // through the pinned fd, and copy the result into the caller's
            // buffer. The kernel never re-reads the path.
            (Family::Fs, "stat" | "lstat" | "newfstatat" | "statx" | "access" | "faccessat" | "faccessat2" | "readlink" | "readlinkat" | "statfs" | "getxattr" | "lgetxattr" | "listxattr" | "llistxattr") => {
                let (dirfd, pi, flags) = match name {
                    "stat" | "access" | "statfs" | "getxattr" | "listxattr" => (AT_FDCWD, 0, 0),
                    "lstat" | "readlink" | "lgetxattr" | "llistxattr" => (AT_FDCWD, 0, AT_SYMLINK_NOFOLLOW),
                    "newfstatat" | "faccessat2" => (dfd(0), 1, a[3]),
                    "statx" => (dfd(0), 1, a[2]),
                    "readlinkat" => (dfd(0), 1, AT_SYMLINK_NOFOLLOW),
                    _ => (dfd(0), 1, 0),
                };
                let raw = self.read_str(n, a[pi])?;
                if raw.is_empty() && flags & AT_EMPTY_PATH != 0 {
                    // Operates on the caller's own fd, which came from us.
                    return Ok((Decision::Continue, "fstat of own fd".into()));
                }
                let abs = self.resolve(n, dirfd, &raw)?;
                let nofollow = flags & AT_SYMLINK_NOFOLLOW != 0;
                let mode = match name { "access" => a[1], "faccessat" | "faccessat2" => a[2], _ => 0 };
                let need = if mode & libc::W_OK as u64 != 0 { Need::Write } else { Need::Read };
                let fd = self.pin(&abs, nofollow, need)?;
                let fdr = fd.as_raw_fd();
                let empty = CString::new("").unwrap();
                let proc_path = sys::proc_fd_path(fdr);
                let d = match name {
                    "stat" | "lstat" | "newfstatat" => {
                        let buf = if name == "newfstatat" { a[2] } else { a[1] };
                        let mut st: libc::stat = unsafe { std::mem::zeroed() };
                        let r = unsafe { libc::fstatat(fdr, empty.as_ptr(), &mut st, libc::AT_EMPTY_PATH) };
                        if r != 0 {
                            return Err((sys::errno(), abs));
                        }
                        self.write_bytes(n, buf, bytes_of(&st))?;
                        Decision::Val(0)
                    }
                    "statx" => {
                        let (mask, buf) = (a[3] as u32, a[4]);
                        let kflags = ((flags & !AT_SYMLINK_NOFOLLOW) as i32) | libc::AT_EMPTY_PATH;
                        let mut stx: libc::statx = unsafe { std::mem::zeroed() };
                        let r = unsafe { libc::statx(fdr, empty.as_ptr(), kflags, mask, &mut stx) };
                        if r != 0 {
                            return Err((sys::errno(), abs));
                        }
                        self.write_bytes(n, buf, bytes_of(&stx))?;
                        Decision::Val(0)
                    }
                    "access" | "faccessat" | "faccessat2" => {
                        // Same credentials as the caller: the sandbox cannot
                        // change its uids (setuid & co. are denied).
                        let eacc = (flags & AT_EACCESS) as i32;
                        let mut r = unsafe { libc::syscall(libc::SYS_faccessat2, fdr, empty.as_ptr(), mode as i32, libc::AT_EMPTY_PATH | eacc) };
                        if r < 0 && sys::errno() == libc::EINVAL {
                            r = unsafe { libc::faccessat(AT_FDCWD, proc_path.as_ptr(), mode as i32, eacc) } as libc::c_long;
                        }
                        Self::result(r)?
                    }
                    "readlink" | "readlinkat" => {
                        let (buf, size) = if name == "readlink" { (a[1], a[2] as usize) } else { (a[2], a[3] as usize) };
                        if size == 0 {
                            return Err((libc::EINVAL, abs));
                        }
                        let mut out = vec![0u8; size.min(PATH_MAX)];
                        let r = unsafe { libc::readlinkat(fdr, empty.as_ptr(), out.as_mut_ptr() as *mut _, out.len()) };
                        if r < 0 {
                            return Err((sys::errno(), abs));
                        }
                        self.write_bytes(n, buf, &out[..r as usize])?;
                        Decision::Val(r as i64)
                    }
                    "statfs" => {
                        let mut sf: libc::statfs = unsafe { std::mem::zeroed() };
                        let r = unsafe { libc::fstatfs(fdr, &mut sf) };
                        if r != 0 {
                            return Err((sys::errno(), abs));
                        }
                        self.write_bytes(n, a[1], bytes_of(&sf))?;
                        Decision::Val(0)
                    }
                    _ => {
                        // xattrs, through /proc/self/fd/N. That lookup follows
                        // a symlink, so a symlink's own xattrs are out of
                        // reach: ENOTSUP, which is also what most filesystems
                        // say for user xattrs on symlinks.
                        if is_symlink(&fd) {
                            return Err((libc::ENOTSUP, abs));
                        }
                        let get = name.ends_with("getxattr");
                        let (buf, size) = if get { (a[2], (a[3] as usize).min(XATTR_SIZE_MAX)) } else { (a[1], (a[2] as usize).min(XATTR_SIZE_MAX)) };
                        let mut out = vec![0u8; size];
                        let outp = if size == 0 { std::ptr::null_mut() } else { out.as_mut_ptr() as *mut libc::c_void };
                        let r = if get {
                            let key = CString::new(self.read_str(n, a[1])?).map_err(|_| (libc::EINVAL, "NUL in name".to_string()))?;
                            unsafe { libc::getxattr(proc_path.as_ptr(), key.as_ptr(), outp, size) }
                        } else {
                            unsafe { libc::listxattr(proc_path.as_ptr(), outp as *mut libc::c_char, size) }
                        };
                        if r < 0 {
                            return Err((sys::errno(), abs));
                        }
                        if size > 0 {
                            self.write_bytes(n, buf, &out[..r as usize])?;
                        }
                        Decision::Val(r as i64)
                    }
                };
                Ok((d, abs))
            }
            (Family::Fs, "chdir") => {
                let abs = self.path_arg(n, AT_FDCWD, 0)?;
                self.pin(&abs, false, Need::Read)?;
                // The one path the kernel still re-reads. A cwd outside every
                // grant conveys nothing: each later relative path is resolved
                // from /proc/PID/cwd and checked here again. Worst case the
                // kernel's answer tells the caller whether a swapped-in
                // directory exists.
                Ok((Decision::Continue, abs))
            }
            // Create / delete / link / rename: pin the parent directory under
            // a write grant and do the operation there with the *at call. A
            // single final component in a pinned directory can only name an
            // entry of that directory.
            (Family::Fs, "mkdir" | "mkdirat") => {
                let (abs, mode) = if name == "mkdir" { (self.path_arg(n, AT_FDCWD, 0)?, a[1]) } else { (self.path_arg(n, dfd(0), 1)?, a[2]) };
                let (pfd, base) = self.parent_dir(&abs)?;
                let r = self.with_umask(n, || unsafe { libc::mkdirat(pfd.as_raw_fd(), base.as_ptr(), mode as libc::mode_t) });
                Ok((Self::result(r as libc::c_long)?, abs))
            }
            (Family::Fs, "rmdir" | "unlink" | "unlinkat") => {
                let (abs, flags) = match name {
                    "rmdir" => (self.path_arg(n, AT_FDCWD, 0)?, AT_REMOVEDIR),
                    "unlink" => (self.path_arg(n, AT_FDCWD, 0)?, 0),
                    _ => (self.path_arg(n, dfd(0), 1)?, a[2] & AT_REMOVEDIR),
                };
                let (pfd, base) = self.parent_dir(&abs)?;
                let r = unsafe { libc::unlinkat(pfd.as_raw_fd(), base.as_ptr(), flags as i32) };
                Ok((Self::result(r as libc::c_long)?, abs))
            }
            (Family::Fs, "symlink" | "symlinkat") => {
                let abs = if name == "symlink" { self.path_arg(n, AT_FDCWD, 1)? } else { self.path_arg(n, dfd(1), 2)? };
                // The target is arbitrary text: following it later goes
                // through RESOLVE_BENEATH like any other symlink.
                let target = CString::new(self.read_str(n, a[0])?).map_err(|_| (libc::EINVAL, "NUL in target".to_string()))?;
                let (pfd, base) = self.parent_dir(&abs)?;
                let r = unsafe { libc::symlinkat(target.as_ptr(), pfd.as_raw_fd(), base.as_ptr()) };
                Ok((Self::result(r as libc::c_long)?, abs))
            }
            (Family::Fs, "rename" | "renameat" | "renameat2") => {
                let (from, to, flags) = match name {
                    "rename" => (self.path_arg(n, AT_FDCWD, 0)?, self.path_arg(n, AT_FDCWD, 1)?, 0),
                    "renameat" => (self.path_arg(n, dfd(0), 1)?, self.path_arg(n, dfd(2), 3)?, 0),
                    _ => (self.path_arg(n, dfd(0), 1)?, self.path_arg(n, dfd(2), 3)?, a[4] as u32),
                };
                let (pf, bf) = self.parent_dir(&from)?;
                let (pt, bt) = self.parent_dir(&to)?;
                let r = unsafe { libc::syscall(libc::SYS_renameat2, pf.as_raw_fd(), bf.as_ptr(), pt.as_raw_fd(), bt.as_ptr(), flags) };
                Ok((Self::result(r)?, format!("{from} -> {to}")))
            }
            (Family::Fs, "link" | "linkat") => {
                let (from, to, flags) = match name {
                    "link" => (self.path_arg(n, AT_FDCWD, 0)?, self.path_arg(n, AT_FDCWD, 1)?, 0),
                    _ => (self.path_arg(n, dfd(0), 1)?, self.path_arg(n, dfd(2), 3)?, a[4]),
                };
                // A new name for a file is a new way to open it for writing
                // later, so the source must be under a write grant too.
                let (pt, bt) = self.parent_dir(&to)?;
                let r = if flags & AT_SYMLINK_FOLLOW != 0 {
                    let src = self.pin(&from, false, Need::Write)?;
                    unsafe { libc::linkat(AT_FDCWD, sys::proc_fd_path(src.as_raw_fd()).as_ptr(), pt.as_raw_fd(), bt.as_ptr(), AT_SYMLINK_FOLLOW as i32) }
                } else {
                    let (pf, bf) = self.parent_dir(&from)?;
                    unsafe { libc::linkat(pf.as_raw_fd(), bf.as_ptr(), pt.as_raw_fd(), bt.as_ptr(), 0) }
                };
                Ok((Self::result(r as libc::c_long)?, format!("{from} -> {to}")))
            }
            (Family::Fs, "truncate" | "chmod" | "fchmodat") => {
                let (abs, arg) = match name {
                    "fchmodat" => (self.path_arg(n, dfd(0), 1)?, a[2]),
                    _ => (self.path_arg(n, AT_FDCWD, 0)?, a[1]),
                };
                let fd = self.pin(&abs, false, Need::Write)?;
                let pp = sys::proc_fd_path(fd.as_raw_fd());
                let r = if name == "truncate" {
                    unsafe { libc::truncate(pp.as_ptr(), arg as libc::off_t) }
                } else {
                    unsafe { libc::fchmodat(AT_FDCWD, pp.as_ptr(), arg as libc::mode_t, 0) }
                };
                Ok((Self::result(r as libc::c_long)?, abs))
            }
            (Family::Exec, "execve") => {
                let abs = self.path_arg(n, AT_FDCWD, 0)?;
                let (note, expect) = self.check_exec(&abs)?;
                // Nobody can exec on the caller's behalf, so the kernel
                // re-reads the path. The image it loads is verified against
                // `expect` at this task's next syscall or within a few
                // milliseconds, and killed if it is not the checked file.
                let tgid = tgid_of(n.pid).ok_or((libc::ESRCH, "no tgid".to_string()))?;
                self.expect_exec(tgid, expect)?;
                Ok((Decision::Continue, format!("{note}; image verified after exec")))
            }
            (Family::Net, "socket") => {
                let dom = a[0] as i32;
                let ty = (a[1] as i32) & 0xf;
                if (dom == libc::AF_INET || dom == libc::AF_INET6) && ty == libc::SOCK_STREAM && self.policy.any_net(&self.active) {
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
                if !self.policy.net_allowed(&host, port, listen, &self.active) {
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
                if self.policy.may_listen(&self.active) { Ok((Decision::Continue, String::new())) } else { Err((libc::EACCES, "no net.listen grant".into())) }
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
                if flags & CLONE_THREAD == 0 && !self.policy.may_spawn(&self.active) {
                    return Err((libc::EPERM, "new process needs proc.spawn".into()));
                }
                Ok((Decision::Continue, format!("flags={flags:#x}")))
            }
            (Family::Proc, "fork" | "vfork") => {
                if self.policy.may_spawn(&self.active) { Ok((Decision::Continue, String::new())) } else { Err((libc::EPERM, "needs proc.spawn".into())) }
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
