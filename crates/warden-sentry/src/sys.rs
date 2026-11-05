//! Thin FFI wrappers over the Linux interfaces the supervisor needs.
#![allow(clippy::missing_safety_doc)]

use std::ffi::CString;
use std::io;
use std::os::fd::{FromRawFd, OwnedFd, RawFd};

#[repr(C)]
#[derive(Clone, Copy, Default, Debug)]
pub struct SeccompData {
    pub nr: i32,
    pub arch: u32,
    pub ip: u64,
    pub args: [u64; 6],
}

#[repr(C)]
#[derive(Clone, Copy, Default, Debug)]
pub struct Notif {
    pub id: u64,
    pub pid: u32,
    pub flags: u32,
    pub data: SeccompData,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
struct NotifResp {
    id: u64,
    val: i64,
    error: i32,
    flags: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
struct NotifAddFd {
    id: u64,
    flags: u32,
    srcfd: u32,
    newfd: u32,
    newfd_flags: u32,
}

const fn ioc(dir: u64, nr: u64, size: u64) -> u64 {
    (dir << 30) | (size << 16) | ((b'!' as u64) << 8) | nr
}
const IOCTL_RECV: u64 = ioc(3, 0, std::mem::size_of::<Notif>() as u64);
const IOCTL_SEND: u64 = ioc(3, 1, std::mem::size_of::<NotifResp>() as u64);
const IOCTL_ID_VALID: u64 = ioc(1, 2, 8);
const IOCTL_ADDFD: u64 = ioc(1, 3, std::mem::size_of::<NotifAddFd>() as u64);

const FLAG_CONTINUE: u32 = 1;
const ADDFD_FLAG_SEND: u32 = 2;
const SECCOMP_SET_MODE_FILTER: libc::c_ulong = 1;
const SECCOMP_FILTER_FLAG_NEW_LISTENER: libc::c_ulong = 1 << 3;

fn cvt(r: libc::c_long) -> io::Result<libc::c_long> {
    if r < 0 { Err(io::Error::last_os_error()) } else { Ok(r) }
}

/// Install the filter on the calling thread and return the listener fd.
/// Called in the forked child before exec; must not allocate.
pub unsafe fn install_filter(prog: &[libc::sock_filter]) -> libc::c_long {
    let fprog = libc::sock_fprog { len: prog.len() as u16, filter: prog.as_ptr() as *mut _ };
    if libc::prctl(libc::PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0 {
        return -1;
    }
    libc::syscall(libc::SYS_seccomp, SECCOMP_SET_MODE_FILTER, SECCOMP_FILTER_FLAG_NEW_LISTENER, &fprog as *const _)
}

pub fn recv(listener: RawFd) -> io::Result<Notif> {
    let mut n = Notif::default();
    cvt(unsafe { libc::ioctl(listener, IOCTL_RECV as _, &mut n as *mut Notif) } as libc::c_long)?;
    Ok(n)
}

/// Is the notification still pending (the task hasn't died / been replaced)?
pub fn id_valid(listener: RawFd, id: u64) -> bool {
    let mut id = id;
    unsafe { libc::ioctl(listener, IOCTL_ID_VALID as _, &mut id as *mut u64) == 0 }
}

fn send(listener: RawFd, r: NotifResp) -> io::Result<()> {
    let mut r = r;
    cvt(unsafe { libc::ioctl(listener, IOCTL_SEND as _, &mut r as *mut NotifResp) } as libc::c_long).map(|_| ())
}

pub fn reply_errno(listener: RawFd, id: u64, errno: i32) -> io::Result<()> {
    send(listener, NotifResp { id, val: 0, error: -errno, flags: 0 })
}
pub fn reply_val(listener: RawFd, id: u64, val: i64) -> io::Result<()> {
    send(listener, NotifResp { id, val, error: 0, flags: 0 })
}
/// Let the original syscall run in the kernel. Only safe when the decision
/// depended on register arguments, not on memory the caller can change.
pub fn reply_continue(listener: RawFd, id: u64) -> io::Result<()> {
    send(listener, NotifResp { id, val: 0, error: 0, flags: FLAG_CONTINUE })
}
/// Install `fd` into the caller and make it the syscall's return value.
pub fn reply_addfd(listener: RawFd, id: u64, fd: RawFd, cloexec: bool) -> io::Result<i64> {
    let mut a = NotifAddFd {
        id,
        flags: ADDFD_FLAG_SEND,
        srcfd: fd as u32,
        newfd: 0,
        newfd_flags: if cloexec { libc::O_CLOEXEC as u32 } else { 0 },
    };
    cvt(unsafe { libc::ioctl(listener, IOCTL_ADDFD as _, &mut a as *mut NotifAddFd) } as libc::c_long)
}

pub fn pidfd_open(pid: libc::pid_t) -> io::Result<OwnedFd> {
    let r = cvt(unsafe { libc::syscall(libc::SYS_pidfd_open, pid, 0) })?;
    Ok(unsafe { OwnedFd::from_raw_fd(r as RawFd) })
}

/// Duplicate another process's fd into ours (same open file description).
pub fn pidfd_getfd(pidfd: RawFd, target: RawFd) -> io::Result<OwnedFd> {
    let r = cvt(unsafe { libc::syscall(libc::SYS_pidfd_getfd, pidfd, target, 0) })?;
    Ok(unsafe { OwnedFd::from_raw_fd(r as RawFd) })
}

#[repr(C)]
struct OpenHow {
    flags: u64,
    mode: u64,
    resolve: u64,
}
pub const RESOLVE_NO_MAGICLINKS: u64 = 0x02;
pub const RESOLVE_BENEATH: u64 = 0x08;

/// openat2 with RESOLVE_BENEATH: the kernel refuses any path, `..` or symlink
/// that would leave `dirfd`. This is what makes a directory grant structural.
pub fn openat2_beneath(dirfd: RawFd, rel: &str, flags: i32, mode: u32) -> io::Result<OwnedFd> {
    let c = CString::new(rel).map_err(|_| io::Error::from_raw_os_error(libc::EINVAL))?;
    let how = OpenHow {
        flags: (flags | libc::O_CLOEXEC) as u32 as u64,
        mode: if flags & libc::O_CREAT != 0 || flags & libc::O_TMPFILE == libc::O_TMPFILE { mode as u64 } else { 0 },
        resolve: RESOLVE_BENEATH | RESOLVE_NO_MAGICLINKS,
    };
    let r = cvt(unsafe {
        libc::syscall(libc::SYS_openat2, dirfd, c.as_ptr(), &how as *const OpenHow, std::mem::size_of::<OpenHow>())
    })?;
    Ok(unsafe { OwnedFd::from_raw_fd(r as RawFd) })
}

pub fn open_path(path: &str, flags: i32) -> io::Result<OwnedFd> {
    let c = CString::new(path).map_err(|_| io::Error::from_raw_os_error(libc::EINVAL))?;
    let r = cvt(unsafe { libc::open(c.as_ptr(), flags | libc::O_CLOEXEC) } as libc::c_long)?;
    Ok(unsafe { OwnedFd::from_raw_fd(r as RawFd) })
}

/// Read up to `buf.len()` bytes of another process's memory.
pub fn read_mem(mem: RawFd, addr: u64, buf: &mut [u8]) -> io::Result<usize> {
    let r = unsafe { libc::pread(mem, buf.as_mut_ptr() as *mut _, buf.len(), addr as libc::off_t) };
    if r < 0 { Err(io::Error::last_os_error()) } else { Ok(r as usize) }
}

pub fn connect_raw(fd: RawFd, addr: &[u8]) -> i32 {
    let r = unsafe { libc::connect(fd, addr.as_ptr() as *const libc::sockaddr, addr.len() as libc::socklen_t) };
    if r < 0 { io::Error::last_os_error().raw_os_error().unwrap_or(libc::EIO) } else { 0 }
}

pub fn bind_raw(fd: RawFd, addr: &[u8]) -> i32 {
    let r = unsafe { libc::bind(fd, addr.as_ptr() as *const libc::sockaddr, addr.len() as libc::socklen_t) };
    if r < 0 { io::Error::last_os_error().raw_os_error().unwrap_or(libc::EIO) } else { 0 }
}

/// Write into another process's memory (results the kernel would have
/// written itself: a `struct stat`, a readlink buffer). FOLL_FORCE writes
/// through the caller's own read-only private pages but the kernel refuses
/// them on shared mappings, so a hostile buffer pointer can't make us change
/// a file; only the caller's own address space is reachable.
pub fn write_mem(mem: RawFd, addr: u64, buf: &[u8]) -> io::Result<usize> {
    let r = unsafe { libc::pwrite(mem, buf.as_ptr() as *const _, buf.len(), addr as libc::off_t) };
    if r < 0 { Err(io::Error::last_os_error()) } else { Ok(r as usize) }
}

/// SIGKILL through a pidfd: the pid cannot have been recycled.
pub fn pidfd_kill(pidfd: RawFd) -> io::Result<()> {
    cvt(unsafe { libc::syscall(libc::SYS_pidfd_send_signal, pidfd, libc::SIGKILL, std::ptr::null::<libc::siginfo_t>(), 0) }).map(|_| ())
}

/// (st_dev, st_ino) of an open file.
pub fn ident(fd: RawFd) -> io::Result<(u64, u64)> {
    let mut st: libc::stat = unsafe { std::mem::zeroed() };
    cvt(unsafe { libc::fstat(fd, &mut st) } as libc::c_long)?;
    Ok((st.st_dev, st.st_ino))
}

/// (st_dev, st_ino) of a path (following symlinks).
pub fn ident_path(path: &str) -> io::Result<(u64, u64)> {
    let c = CString::new(path).map_err(|_| io::Error::from_raw_os_error(libc::EINVAL))?;
    let mut st: libc::stat = unsafe { std::mem::zeroed() };
    cvt(unsafe { libc::stat(c.as_ptr(), &mut st) } as libc::c_long)?;
    Ok((st.st_dev, st.st_ino))
}

/// The path `/proc/self/fd/N` names the object behind our open fd; a
/// lookup through it reaches that object and nothing the sandbox can swap.
pub fn proc_fd_path(fd: RawFd) -> CString {
    CString::new(format!("/proc/self/fd/{fd}")).unwrap()
}

pub fn errno() -> i32 {
    io::Error::last_os_error().raw_os_error().unwrap_or(libc::EIO)
}

/// Device number as `/proc/PID/maps` prints it.
pub fn maps_dev(dev: u64) -> String {
    format!("{:02x}:{:02x}", libc::major(dev), libc::minor(dev))
}
