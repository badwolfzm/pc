//! seccomp-BPF filter built from the `warden-abi` table, plus the raw
//! user-notification FFI. All `unsafe` in the host prototype lives in this
//! file and `sys.rs`.

use warden_abi::{table, Class};

const AUDIT_ARCH_X86_64: u32 = 0xC000_003E;
const X32_SYSCALL_BIT: u32 = 0x4000_0000;

pub const RET_KILL_PROCESS: u32 = 0x8000_0000;
pub const RET_USER_NOTIF: u32 = 0x7fc0_0000;
pub const RET_ALLOW: u32 = 0x7fff_0000;

const BPF_LD_W_ABS: u16 = 0x20; // BPF_LD | BPF_W | BPF_ABS
const BPF_JEQ_K: u16 = 0x15; // BPF_JMP | BPF_JEQ | BPF_K
const BPF_JGE_K: u16 = 0x35; // BPF_JMP | BPF_JGE | BPF_K
const BPF_RET_K: u16 = 0x06;
const TIOCSTI: u32 = 0x5412;
const TIOCLINUX: u32 = 0x541C;

fn stmt(code: u16, k: u32) -> libc::sock_filter {
    libc::sock_filter { code, jt: 0, jf: 0, k }
}
fn jump(code: u16, k: u32, jt: u8, jf: u8) -> libc::sock_filter {
    libc::sock_filter { code, jt, jf, k }
}

/// Program: wrong arch or x32 ABI -> kill. `Pass` syscalls -> allow.
/// Everything else (mediated, denied, unimplemented, unknown) -> the
/// supervisor, which decides and writes the audit log.
pub fn build() -> Vec<libc::sock_filter> {
    let mut p = vec![
        stmt(BPF_LD_W_ABS, 4), // seccomp_data.arch
        jump(BPF_JEQ_K, AUDIT_ARCH_X86_64, 1, 0),
        stmt(BPF_RET_K, RET_KILL_PROCESS),
        stmt(BPF_LD_W_ABS, 0), // seccomp_data.nr
        jump(BPF_JGE_K, X32_SYSCALL_BIT, 0, 1),
        stmt(BPF_RET_K, RET_KILL_PROCESS),
        // ioctl passes except terminal-injection requests (TIOCSTI pushes
        // keystrokes into the controlling terminal; TIOCLINUX can too).
        jump(BPF_JEQ_K, libc::SYS_ioctl as u32, 0, 5),
        stmt(BPF_LD_W_ABS, 24), // low 32 bits of args[1]
        jump(BPF_JEQ_K, TIOCSTI, 2, 0),
        jump(BPF_JEQ_K, TIOCLINUX, 1, 0),
        stmt(BPF_RET_K, RET_ALLOW),
        stmt(BPF_RET_K, RET_USER_NOTIF),
    ];
    for (name, nr, class) in table() {
        if class == Class::Pass && name != "ioctl" {
            p.push(jump(BPF_JEQ_K, nr, 0, 1));
            p.push(stmt(BPF_RET_K, RET_ALLOW));
        }
    }
    p.push(stmt(BPF_RET_K, RET_USER_NOTIF));
    p
}

#[cfg(test)]
mod tests {
    #[test]
    fn filter_fits_kernel_limit() {
        // BPF_MAXINSNS is 4096.
        assert!(super::build().len() < 4096);
    }
}
