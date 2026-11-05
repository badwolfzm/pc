//! The Linux syscall boundary, as data.
//!
//! Every x86-64 Linux syscall gets exactly one class. The table is the single
//! source of truth for three consumers:
//! - the Phase 0 host supervisor (`warden-sentry`), which compiles it into a
//!   seccomp filter and a dispatch table;
//! - the Phase 1+ personality server inside Warden, which implements the
//!   `Mediated` ones on top of capabilities;
//! - the DSL workstream, which reads the exported schema (`schema_json`) to
//!   describe what a Linux library is allowed to do.
//!
//! Growing toward full Linux ABI compatibility means moving entries from
//! `Unimplemented` to `Mediated` (or `Pass`), one family at a time.
#![forbid(unsafe_code)]

mod linux_x86_64;
pub use linux_x86_64::SYSCALLS;

/// Bump when the meaning of a class, family or permission name changes.
/// The DSL pins this; additive changes bump the minor number only.
pub const ABI_SCHEMA_VERSION: &str = "0.1";

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Family {
    /// Paths and file descriptors that name filesystem objects.
    Fs,
    /// Program loading.
    Exec,
    /// Sockets.
    Net,
    /// Process and thread creation.
    Proc,
    /// Signals sent to other tasks.
    Signal,
}

impl Family {
    pub fn name(self) -> &'static str {
        match self {
            Family::Fs => "fs",
            Family::Exec => "exec",
            Family::Net => "net",
            Family::Proc => "proc",
            Family::Signal => "signal",
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Class {
    /// Touches only the caller's own state (memory, its own fds, time). The
    /// kernel lets it through without asking the supervisor.
    Pass,
    /// Needs a capability check by the supervisor / personality server.
    Mediated(Family),
    /// Never allowed: host-kernel attack surface or a sandbox escape primitive.
    Denied,
    /// Not supported yet. Returns ENOSYS and is logged, so we learn what real
    /// programs need next.
    Unimplemented,
}

impl Class {
    pub fn name(self) -> &'static str {
        match self {
            Class::Pass => "pass",
            Class::Mediated(_) => "mediated",
            Class::Denied => "denied",
            Class::Unimplemented => "unimplemented",
        }
    }
}

const PASS: &[&str] = &[
    "read", "write", "close", "fstat", "lseek", "mmap", "mprotect", "munmap", "brk",
    "rt_sigaction", "rt_sigprocmask", "rt_sigreturn", "pread64", "pwrite64", "readv", "writev",
    "pipe", "pipe2", "sched_yield", "mremap", "msync", "mincore", "madvise", "dup", "dup2", "dup3",
    "pause", "nanosleep", "clock_nanosleep", "getitimer", "setitimer", "alarm", "getpid", "gettid",
    "getppid", "exit", "exit_group", "wait4", "waitid", "uname", "fcntl", "flock", "fsync",
    "fdatasync", "ftruncate", "getdents", "getdents64", "getcwd", "fchdir", "umask",
    "gettimeofday", "getrlimit", "getrusage", "sysinfo", "times", "getuid", "getgid", "geteuid",
    "getegid", "getpgrp", "getpgid", "getsid", "getgroups", "getresuid", "getresgid",
    "sigaltstack", "arch_prctl", "set_tid_address", "set_robust_list", "get_robust_list",
    "futex", "futex_waitv", "rseq", "prlimit64", "getrandom", "clock_gettime", "clock_getres",
    "time", "poll", "ppoll", "select", "pselect6", "epoll_create", "epoll_create1", "epoll_ctl",
    "epoll_wait", "epoll_pwait", "epoll_pwait2", "eventfd", "eventfd2", "timerfd_create",
    "timerfd_settime", "timerfd_gettime", "signalfd", "signalfd4", "rt_sigpending",
    "rt_sigtimedwait", "rt_sigsuspend", "rt_sigqueueinfo", "sched_getaffinity",
    "sched_setaffinity", "sched_getparam", "sched_getscheduler", "sched_get_priority_max",
    "sched_get_priority_min", "getpriority", "membarrier", "memfd_create", "close_range",
    "recvfrom", "recvmsg", "recvmmsg", "shutdown", "getsockname", "getpeername", "getsockopt",
    "setsockopt", "socketpair", "accept", "accept4", "sendfile", "splice", "tee", "copy_file_range",
    "fadvise64", "fallocate", "readahead", "prctl", "capget", "mlock", "munlock", "mlock2",
    "fgetxattr", "flistxattr",
    // ioctl reaches device drivers; it passes in Phase 0 only because stdio
    // needs TCGETS. Phase 1 mediates it per request code.
    "ioctl",
];

const DENIED: &[&str] = &[
    "ptrace", "process_vm_readv", "process_vm_writev", "mount", "umount2", "pivot_root", "chroot",
    "bpf", "perf_event_open", "kexec_load", "kexec_file_load", "init_module", "finit_module",
    "delete_module", "keyctl", "add_key", "request_key", "unshare", "setns", "io_uring_setup",
    "io_uring_enter", "io_uring_register", "userfaultfd", "swapon", "swapoff", "reboot",
    "settimeofday", "clock_settime", "clock_adjtime", "adjtimex", "iopl", "ioperm", "quotactl",
    "quotactl_fd", "name_to_handle_at", "open_by_handle_at", "personality", "lookup_dcookie",
    "acct", "syslog", "vhangup", "mknod", "mknodat", "fanotify_init", "pidfd_getfd", "kcmp",
    "move_mount", "fsopen", "fsmount", "fsconfig", "fspick", "open_tree", "mount_setattr",
    "modify_ldt", "_sysctl", "uselib", "ustat", "nfsservctl", "vserver", "create_module",
    "get_kernel_syms", "query_module", "setuid", "setgid", "setreuid", "setregid", "setresuid",
    "setresgid", "setfsuid", "setfsgid", "setgroups", "capset", "sethostname", "setdomainname",
    "seccomp", "landlock_create_ruleset", "landlock_add_rule", "landlock_restrict_self",
    "execveat", "openat2", "process_madvise", "pidfd_open", "pidfd_send_signal",
    "set_mempolicy", "mbind", "migrate_pages", "move_pages", "lsm_set_self_attr",
    "fanotify_mark", "inotify_init", "inotify_init1", "inotify_add_watch", "inotify_rm_watch",
];

const MEDIATED: &[(&str, Family)] = &[
    ("open", Family::Fs), ("openat", Family::Fs), ("creat", Family::Fs),
    ("stat", Family::Fs), ("lstat", Family::Fs), ("newfstatat", Family::Fs), ("statx", Family::Fs),
    ("access", Family::Fs), ("faccessat", Family::Fs), ("faccessat2", Family::Fs),
    ("readlink", Family::Fs), ("readlinkat", Family::Fs), ("chdir", Family::Fs),
    ("mkdir", Family::Fs), ("mkdirat", Family::Fs), ("rmdir", Family::Fs),
    ("unlink", Family::Fs), ("unlinkat", Family::Fs),
    ("rename", Family::Fs), ("renameat", Family::Fs), ("renameat2", Family::Fs),
    ("truncate", Family::Fs), ("chmod", Family::Fs), ("fchmodat", Family::Fs),
    ("statfs", Family::Fs), ("symlink", Family::Fs), ("symlinkat", Family::Fs),
    ("link", Family::Fs), ("linkat", Family::Fs),
    ("getxattr", Family::Fs), ("lgetxattr", Family::Fs), ("listxattr", Family::Fs), ("llistxattr", Family::Fs),
    ("execve", Family::Exec),
    ("socket", Family::Net), ("connect", Family::Net), ("bind", Family::Net),
    ("listen", Family::Net), ("sendto", Family::Net), ("sendmsg", Family::Net),
    ("sendmmsg", Family::Net),
    ("clone", Family::Proc), ("fork", Family::Proc), ("vfork", Family::Proc),
    ("kill", Family::Signal), ("tkill", Family::Signal), ("tgkill", Family::Signal),
];

/// `clone3` passes its flags in memory the filter can't inspect, so we return
/// ENOSYS and glibc falls back to `clone`, whose flags we can check.
const UNIMPLEMENTED_ON_PURPOSE: &[&str] = &["clone3"];

pub fn classify(name: &str) -> Class {
    if let Some((_, f)) = MEDIATED.iter().find(|(n, _)| *n == name) {
        return Class::Mediated(*f);
    }
    if DENIED.contains(&name) {
        return Class::Denied;
    }
    if PASS.contains(&name) {
        return Class::Pass;
    }
    let _ = UNIMPLEMENTED_ON_PURPOSE;
    Class::Unimplemented
}

pub fn name_of(nr: u32) -> Option<&'static str> {
    SYSCALLS.iter().find(|(_, n)| *n == nr).map(|(s, _)| *s)
}

pub fn nr_of(name: &str) -> Option<u32> {
    SYSCALLS.iter().find(|(s, _)| *s == name).map(|(_, n)| *n)
}

/// Every syscall with its class, in number order.
pub fn table() -> impl Iterator<Item = (&'static str, u32, Class)> {
    SYSCALLS.iter().map(|&(n, nr)| (n, nr, classify(n)))
}

/// Permission names a manifest, a library approval or the DSL may use.
/// Each takes a scope after `=` (a path prefix or host:port pattern).
pub const PERMISSIONS: &[(&str, &str)] = &[
    ("load", "Map this library into the process (no scope)."),
    ("fs.read", "Read files under a path prefix."),
    ("fs.write", "Create, change and delete files under a path prefix."),
    ("exec", "Execute programs under a path prefix."),
    ("net.connect", "Open TCP connections to host:port (\"*\" wildcards allowed)."),
    ("net.listen", "Listen on a local port."),
    ("proc.spawn", "Create processes and threads (no namespaces)."),
];

/// The versioned ABI contract, as JSON. Kernel and DSL both build against it;
/// this is what lets the two workstreams move in parallel.
pub fn schema_json() -> String {
    let mut s = String::new();
    s.push_str("{\n");
    s.push_str(&format!("  \"schema\": \"warden-abi\",\n  \"version\": \"{}\",\n", ABI_SCHEMA_VERSION));
    s.push_str("  \"arch\": \"x86_64\",\n  \"permissions\": [\n");
    for (i, (p, d)) in PERMISSIONS.iter().enumerate() {
        let comma = if i + 1 < PERMISSIONS.len() { "," } else { "" };
        s.push_str(&format!("    {{\"name\": \"{}\", \"doc\": \"{}\"}}{}\n", p, d.replace('"', "\\\""), comma));
    }
    s.push_str("  ],\n  \"syscalls\": [\n");
    let rows: Vec<_> = table().collect();
    for (i, (n, nr, c)) in rows.iter().enumerate() {
        let fam = match c {
            Class::Mediated(f) => format!(", \"family\": \"{}\"", f.name()),
            _ => String::new(),
        };
        let comma = if i + 1 < rows.len() { "," } else { "" };
        s.push_str(&format!("    {{\"nr\": {}, \"name\": \"{}\", \"class\": \"{}\"{}}}{}\n", nr, n, c.name(), fam, comma));
    }
    s.push_str("  ]\n}\n");
    s
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn every_listed_name_is_a_real_syscall() {
        for n in PASS.iter().chain(DENIED).chain(MEDIATED.iter().map(|(n, _)| n)) {
            assert!(nr_of(n).is_some(), "unknown syscall name {n}");
        }
    }

    #[test]
    fn no_syscall_in_two_lists() {
        for n in PASS {
            assert!(!DENIED.contains(n), "{n} in PASS and DENIED");
            assert!(!MEDIATED.iter().any(|(m, _)| m == n), "{n} in PASS and MEDIATED");
        }
        for n in DENIED {
            assert!(!MEDIATED.iter().any(|(m, _)| m == n), "{n} in DENIED and MEDIATED");
        }
    }

    #[test]
    fn escape_primitives_are_denied() {
        for n in ["ptrace", "mount", "bpf", "unshare", "setns", "init_module", "io_uring_setup", "userfaultfd", "kexec_load", "open_by_handle_at"] {
            assert_eq!(classify(n), Class::Denied, "{n}");
        }
        assert_eq!(classify("clone3"), Class::Unimplemented);
    }

    #[test]
    fn schema_is_well_formed_enough() {
        let s = schema_json();
        assert!(s.contains("\"version\": \"0.1\""));
        assert_eq!(s.matches("\"nr\":").count(), SYSCALLS.len());
    }
}

#[cfg(test)]
mod contract {
    /// The checked-in contract must match the table. Regenerate with
    /// `warden schema > abi/warden-abi.json` when the table changes.
    #[test]
    fn checked_in_schema_is_current() {
        let on_disk = include_str!("../../../abi/warden-abi.json");
        assert_eq!(on_disk, super::schema_json());
    }
}
